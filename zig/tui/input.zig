//! ncurses-based input handling with full keyboard support
//!
//! Idiomatic Zig replacement for src/ncurses_input.h and src/ncurses_input.c
//!
//! Provides a readline-like input experience using ncurses with:
//! - Cursor movement (arrow keys, Ctrl+a/e, Alt+b/f, Home/End)
//! - Text editing (insert, delete, backspace)
//! - Word operations (Alt+d, Alt+backspace)
//! - Line operations (Ctrl+k, Ctrl+u, Ctrl+l)
//! - Multiline input (Ctrl+J for newline)
//! - History navigation (Up/Down arrows)
//! - Tab completion support (via callback)
//! - Paste handling (bracketed paste)

const std = @import("std");
const logger = @import("../logger.zig");

// ---------------------------------------------------------------------------
// ncurses FFI
// ---------------------------------------------------------------------------

const c = @cImport({
    @cInclude("ncurses.h");
});

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const INITIAL_BUFFER_SIZE = 8192;
const DEFAULT_HISTORY_SIZE = 100;

// ---------------------------------------------------------------------------
// Completion Support
// ---------------------------------------------------------------------------

/// Completion result containing suggestions
pub const CompletionResult = struct {
    allocator: std.mem.Allocator,
    options: [][]const u8, // Array of completion options
    count: usize, // Number of options
    selected: usize, // Which option is highlighted (for cycling)

    /// Initialize a new completion result
    pub fn init(allocator: std.mem.Allocator, options: [][]const u8) CompletionResult {
        return CompletionResult{
            .allocator = allocator,
            .options = options,
            .count = options.len,
            .selected = 0,
        };
    }

    /// Create an empty completion result
    pub fn empty(allocator: std.mem.Allocator) CompletionResult {
        return CompletionResult{
            .allocator = allocator,
            .options = &.{},
            .count = 0,
            .selected = 0,
        };
    }

    /// Free the completion result and all owned strings
    pub fn deinit(self: *CompletionResult) void {
        for (self.options) |opt| {
            self.allocator.free(opt);
        }
        self.allocator.free(self.options);
        self.count = 0;
        self.selected = 0;
    }
};

/// Completion callback type: given line + cursor position, return suggestions
pub const CompletionFn = *const fn (line: []const u8, cursor_pos: usize, ctx: ?*anyopaque) ?*CompletionResult;

/// Resize callback type: called when input needs more/less height
/// Returns the new height that was granted (may be less than requested)
pub const ResizeFn = *const fn (ctx: ?*anyopaque, requested_height: i32) i32;

// ---------------------------------------------------------------------------
// NCurses Input State
// ---------------------------------------------------------------------------

pub const NCursesInput = struct {
    // ncurses window for input area
    window: ?*c.WINDOW = null,

    // Input buffer (dynamically allocated)
    buffer: []u8,
    buffer_capacity: usize,

    // Cursor and length tracking
    cursor: usize = 0, // Cursor position (0 to length)
    length: usize = 0, // Current length of input

    // Window dimensions
    window_height: i32 = 0,
    window_width: i32 = 0,

    // Scroll offsets
    scroll_offset: i32 = 0, // Horizontal scroll offset for long lines
    line_scroll_offset: i32 = 0, // Vertical scroll offset for multiline (0 = show last lines)

    // History support
    history: [][]u8, // Array of history strings
    history_capacity: usize,
    history_count: usize = 0,
    history_position: i32 = -1, // Current position when navigating (-1 = not navigating)
    saved_input: ?[]u8 = null, // Saved input when navigating history

    // Completion support
    completer: ?CompletionFn = null,
    completer_ctx: ?*anyopaque = null,

    // Resize support
    resizer: ?ResizeFn = null,
    resizer_ctx: ?*anyopaque = null,
    min_height: i32 = 1,
    max_height: i32 = 3,

    // Paste tracking
    paste_content: ?[]u8 = null,
    paste_content_len: usize = 0,
    paste_placeholder_start: i32 = 0,
    paste_placeholder_len: i32 = 0,

    // Allocator for dynamic memory
    allocator: std.mem.Allocator,

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Initialize a new NCursesInput instance
    pub fn init(allocator: std.mem.Allocator, window: *c.WINDOW, completer: ?CompletionFn, ctx: ?*anyopaque) !NCursesInput {
        const buffer = try allocator.alloc(u8, INITIAL_BUFFER_SIZE);
        errdefer allocator.free(buffer);

        const history = try allocator.alloc([]u8, DEFAULT_HISTORY_SIZE);
        errdefer allocator.free(history);

        var input = NCursesInput{
            .allocator = allocator,
            .window = window,
            .buffer = buffer,
            .buffer_capacity = INITIAL_BUFFER_SIZE,
            .history = history,
            .history_capacity = DEFAULT_HISTORY_SIZE,
            .completer = completer,
            .completer_ctx = ctx,
        };

        // Initialize buffer
        input.buffer[0] = 0;

        // Get window dimensions
        var height: i32 = 0;
        var width: i32 = 0;
        _ = c.getmaxyx(window, &height, &width);
        input.window_height = height;
        input.window_width = width;

        // Enable keypad mode for arrow keys and function keys
        _ = c.keypad(window, c.TRUE);

        // Disable echo and set nodelay to non-blocking for paste detection
        _ = c.noecho();

        logger.defaultLogger.log(.debug, "[NCURSES_INPUT] Initialized input (window={d}x{d})", .{ width, height });

        return input;
    }

    /// Free all resources associated with this input
    pub fn deinit(self: *NCursesInput) void {
        // Free buffer
        self.allocator.free(self.buffer);
        self.buffer_capacity = 0;
        self.cursor = 0;
        self.length = 0;

        // Free history
        self.clearHistory();
        self.allocator.free(self.history);
        self.history_capacity = 0;

        // Free saved input
        if (self.saved_input) |saved| {
            self.allocator.free(saved);
            self.saved_input = null;
        }

        // Free paste tracking
        if (self.paste_content) |paste| {
            self.allocator.free(paste);
            self.paste_content = null;
            self.paste_content_len = 0;
        }

        self.window = null;
    }

    // -----------------------------------------------------------------------
    // History Management
    // -----------------------------------------------------------------------

    /// Add an entry to history
    pub fn historyAdd(self: *NCursesInput, entry: []const u8) void {
        if (entry.len == 0) return; // Don't add empty entries

        // Don't add if it's the same as the last entry
        if (self.history_count > 0) {
            const last = self.history[self.history_count - 1];
            if (std.mem.eql(u8, last, entry)) return;
        }

        // If at capacity, remove oldest entry
        if (self.history_count >= self.history_capacity) {
            self.allocator.free(self.history[0]);
            std.mem.copyForwards([]u8, self.history[0 .. self.history_capacity - 1], self.history[1..self.history_count]);
            self.history_count -= 1;
        }

        // Add new entry
        const copy = self.allocator.dupe(u8, entry) catch return;
        self.history[self.history_count] = copy;
        self.history_count += 1;
        self.history_position = -1; // Reset navigation position
    }

    /// Clear all history entries
    fn clearHistory(self: *NCursesInput) void {
        for (0..self.history_count) |i| {
            self.allocator.free(self.history[i]);
        }
        self.history_count = 0;
        self.history_position = -1;
    }

    // -----------------------------------------------------------------------
    // Resize Callback
    // -----------------------------------------------------------------------

    /// Set resize callback for dynamic height adjustment
    pub fn setResizeCallback(self: *NCursesInput, resizer: ResizeFn, ctx: ?*anyopaque, min_height: i32, max_height: i32) void {
        self.resizer = resizer;
        self.resizer_ctx = ctx;
        self.min_height = if (min_height > 0) min_height else 1;
        self.max_height = if (max_height > min_height) max_height else min_height;
    }

    // -----------------------------------------------------------------------
    // Buffer Operations
    // -----------------------------------------------------------------------

    /// Ensure buffer has capacity for at least `needed` bytes
    fn ensureCapacity(self: *NCursesInput, needed: usize) !void {
        if (needed <= self.buffer_capacity) return;

        var new_capacity = self.buffer_capacity * 2;
        while (new_capacity < needed) {
            new_capacity *= 2;
        }

        const new_buffer = try self.allocator.realloc(self.buffer, new_capacity);
        self.buffer = new_buffer;
        self.buffer_capacity = new_capacity;
    }

    /// Insert a character at cursor position
    fn insertChar(self: *NCursesInput, char: u8) !void {
        try self.ensureCapacity(self.length + 2);

        // Make space for the new character
        if (self.cursor < self.length) {
            std.mem.copyBackwards(u8, self.buffer[self.cursor + 1 .. self.length + 1], self.buffer[self.cursor..self.length]);
        }

        self.buffer[self.cursor] = char;
        self.length += 1;
        self.cursor += 1;
        self.buffer[self.length] = 0;
    }

    /// Delete character at cursor position (forward delete)
    fn deleteChar(self: *NCursesInput) bool {
        if (self.cursor >= self.length) return false;

        if (self.cursor < self.length - 1) {
            std.mem.copyForwards(u8, self.buffer[self.cursor .. self.length - 1], self.buffer[self.cursor + 1 .. self.length]);
        }

        self.length -= 1;
        self.buffer[self.length] = 0;
        return true;
    }

    /// Delete character before cursor (backspace)
    fn backspace(self: *NCursesInput) bool {
        if (self.cursor == 0) return false;

        std.mem.copyForwards(u8, self.buffer[self.cursor - 1 .. self.length - 1], self.buffer[self.cursor..self.length]);
        self.length -= 1;
        self.cursor -= 1;
        self.buffer[self.length] = 0;
        return true;
    }

    /// Delete word before cursor (Alt+Backspace)
    fn deleteWordBackward(self: *NCursesInput) bool {
        if (self.cursor == 0) return false;

        const word_start = moveBackwardWord(self.buffer[0..self.length], self.cursor);
        const delete_count = self.cursor - word_start;

        if (delete_count > 0) {
            std.mem.copyForwards(u8, self.buffer[word_start..self.length], self.buffer[self.cursor..self.length]);
            self.length -= delete_count;
            self.cursor = word_start;
            self.buffer[self.length] = 0;
        }

        return delete_count > 0;
    }

    /// Delete word after cursor (Alt+d)
    fn deleteWordForward(self: *NCursesInput) bool {
        if (self.cursor >= self.length) return false;

        const word_end = moveForwardWord(self.buffer[0..self.length], self.cursor);
        const delete_count = word_end - self.cursor;

        if (delete_count > 0) {
            std.mem.copyForwards(u8, self.buffer[self.cursor..self.length], self.buffer[word_end..self.length]);
            self.length -= delete_count;
            self.buffer[self.length] = 0;
        }

        return delete_count > 0;
    }

    /// Kill to end of line (Ctrl+K)
    fn killToEnd(self: *NCursesInput) void {
        self.buffer[self.cursor] = 0;
        self.length = self.cursor;
    }

    /// Kill to beginning of line (Ctrl+U)
    fn killToBeginning(self: *NCursesInput) void {
        if (self.cursor == 0) return;

        std.mem.copyForwards(u8, self.buffer[0..self.length], self.buffer[self.cursor..self.length]);
        self.length -= self.cursor;
        self.cursor = 0;
        self.buffer[self.length] = 0;
    }

    /// Clear entire input (Ctrl+L)
    fn clearInput(self: *NCursesInput) void {
        self.buffer[0] = 0;
        self.length = 0;
        self.cursor = 0;
    }

    // -----------------------------------------------------------------------
    // Word Movement Helpers
    // -----------------------------------------------------------------------

    /// Check if character is a word boundary
    fn isWordBoundary(char: u8) bool {
        return !std.ascii.isAlphanumeric(char) and char != '_';
    }

    /// Move cursor backward by one word
    fn moveBackwardWord(buffer: []const u8, cursor_pos: usize) usize {
        if (cursor_pos == 0) return 0;

        var pos = cursor_pos - 1;

        // Skip trailing whitespace/punctuation
        while (pos > 0 and isWordBoundary(buffer[pos])) {
            pos -= 1;
        }

        // Skip the word characters
        while (pos > 0 and !isWordBoundary(buffer[pos])) {
            pos -= 1;
        }

        // If we stopped at a boundary (not at start), move one forward
        if (pos > 0 and isWordBoundary(buffer[pos])) {
            pos += 1;
        }

        return pos;
    }

    /// Move cursor forward by one word
    fn moveForwardWord(buffer: []const u8, cursor_pos: usize) usize {
        const buffer_len = buffer.len;
        if (cursor_pos >= buffer_len) return buffer_len;

        var pos = cursor_pos;

        // Skip current word characters
        while (pos < buffer_len and !isWordBoundary(buffer[pos])) {
            pos += 1;
        }

        // Skip trailing whitespace/punctuation
        while (pos < buffer_len and isWordBoundary(buffer[pos])) {
            pos += 1;
        }

        return pos;
    }

    // -----------------------------------------------------------------------
    // Display Functions
    // -----------------------------------------------------------------------

    /// Calculate number of visual lines needed for the buffer
    fn calculateNeededLines(buffer: []const u8, buffer_len: usize, available_width: i32, prompt_len: i32) i32 {
        if (buffer_len == 0) return 1;

        var lines: i32 = 1;
        var current_line_width = prompt_len; // First line includes prompt

        for (0..buffer_len) |i| {
            if (buffer[i] == '\n') {
                lines += 1;
                current_line_width = 0;
            } else {
                current_line_width += 1;
                if (current_line_width >= available_width) {
                    lines += 1;
                    current_line_width = 0;
                }
            }
        }

        return lines;
    }

    /// Calculate cursor position in screen coordinates (line, column)
    fn calculateCursorPosition(buffer: []const u8, cursor_pos: usize, available_width: i32, prompt_len: i32, out_line: *i32, out_col: *i32) void {
        var line: i32 = 0;
        var col = prompt_len; // First line starts after prompt

        for (0..cursor_pos) |i| {
            if (buffer[i] == '\n') {
                line += 1;
                col = 0;
            } else {
                col += 1;
                if (col >= available_width) {
                    line += 1;
                    col = 0;
                }
            }
        }

        out_line.* = line;
        out_col.* = col;
    }

    /// Redraw the input window with multiline support
    fn redrawInput(self: *NCursesInput, prompt: []const u8) void {
        const win = self.window orelse return;
        const prompt_len_i: i32 = @intCast(prompt.len);
        const available_width = self.window_width;

        // Calculate how many lines we need
        const needed_lines = calculateNeededLines(self.buffer[0..self.length], self.length, available_width, prompt_len_i);

        // Request resize if needed and callback is available
        if (self.resizer) |resizer| {
            var desired_height = needed_lines;
            if (desired_height < self.min_height) {
                desired_height = self.min_height;
            } else if (desired_height > self.max_height) {
                desired_height = self.max_height;
            }

            if (desired_height != self.window_height) {
                const granted_height = resizer(self.resizer_ctx, desired_height);
                if (granted_height > 0) {
                    self.window_height = granted_height;
                    var h: i32 = 0;
                    var w: i32 = 0;
                    _ = c.getmaxyx(win, &h, &w);
                    self.window_height = h;
                    self.window_width = w;
                }
            }
        }

        _ = c.werase(win);

        const available_height = self.window_height;

        // Calculate cursor screen position
        var cursor_line: i32 = 0;
        var cursor_col: i32 = 0;
        calculateCursorPosition(self.buffer[0..self.length], self.cursor, available_width, prompt_len_i, &cursor_line, &cursor_col);

        // Adjust vertical scroll to keep cursor visible
        if (cursor_line < self.line_scroll_offset) {
            self.line_scroll_offset = cursor_line;
        } else if (cursor_line >= self.line_scroll_offset + available_height) {
            self.line_scroll_offset = cursor_line - available_height + 1;
        }

        // Render visible lines
        var screen_line: i32 = 0;
        var current_line: i32 = 0;
        var render_col = prompt_len_i;

        // Draw prompt on first line
        if (self.line_scroll_offset == 0) {
            _ = c.mvwprintw(win, 0, 0, "%s", prompt.ptr);
        } else {
            render_col = 0;
        }

        var i: usize = 0;
        while (i < self.length and screen_line < available_height) : (i += 1) {
            // Skip lines before scroll offset
            if (current_line < self.line_scroll_offset) {
                if (self.buffer[i] == '\n') {
                    current_line += 1;
                }
                continue;
            }

            // Render character
            if (self.buffer[i] == '\n') {
                // Show newline as special character (U+21B5 ↵ with A_DIM)
                _ = c.mvwaddch(win, screen_line, render_col, '↵' | c.A_DIM);
                screen_line += 1;
                render_col = 0;
                current_line += 1;
            } else {
                _ = c.mvwaddch(win, screen_line, render_col, self.buffer[i]);
                render_col += 1;

                if (render_col >= available_width) {
                    screen_line += 1;
                    render_col = 0;
                    current_line += 1;
                }
            }
        }

        // Position cursor
        const cursor_screen_line = cursor_line - self.line_scroll_offset;
        if (cursor_screen_line >= 0 and cursor_screen_line < available_height) {
            _ = c.wmove(win, cursor_screen_line, cursor_col);
        }

        _ = c.wrefresh(win);
    }

    // -----------------------------------------------------------------------
    // Main Input Loop
    // -----------------------------------------------------------------------

    /// Read a line of input with editing support
    /// Returns: Newly allocated string with input (caller must free)
    ///          null on EOF (Ctrl+D)
    pub fn readLine(self: *NCursesInput, prompt: []const u8) !?[]u8 {
        // Reset buffer
        self.buffer[0] = 0;
        self.length = 0;
        self.cursor = 0;
        self.scroll_offset = 0;

        // Clear saved input from previous history navigation
        if (self.saved_input) |saved| {
            self.allocator.free(saved);
            self.saved_input = null;
        }
        self.history_position = -1;

        // Initial draw
        self.redrawInput(prompt);

        const win = self.window orelse return error.InvalidWindow;

        var running = true;
        while (running) {
            const ch = c.wgetch(win);

            if (ch == c.ERR) {
                // No input available
                continue;
            }

            switch (ch) {
                // ============================================================
                // Navigation keys
                // ============================================================
                c.KEY_LEFT => {
                    if (self.cursor > 0) {
                        self.cursor -= 1;
                        self.redrawInput(prompt);
                    }
                },
                c.KEY_RIGHT => {
                    if (self.cursor < self.length) {
                        self.cursor += 1;
                        self.redrawInput(prompt);
                    }
                },
                c.KEY_HOME, 1 => { // Ctrl+A
                    self.cursor = 0;
                    self.redrawInput(prompt);
                },
                c.KEY_END, 5 => { // Ctrl+E
                    self.cursor = self.length;
                    self.redrawInput(prompt);
                },

                // ============================================================
                // History navigation
                // ============================================================
                c.KEY_UP, 16 => { // Ctrl+P - previous history
                    if (self.history_count > 0) {
                        // Save current input if this is the first Up press
                        if (self.history_position == -1) {
                            if (self.saved_input) |saved| {
                                self.allocator.free(saved);
                            }
                            self.saved_input = self.allocator.dupe(u8, self.buffer[0..self.length]) catch null;
                            self.history_position = @intCast(self.history_count);
                        }

                        // Navigate to previous entry
                        if (self.history_position > 0) {
                            self.history_position -= 1;
                            const hist_entry = self.history[@intCast(self.history_position)];
                            const len = @min(hist_entry.len, self.buffer_capacity - 1);
                            @memcpy(self.buffer[0..len], hist_entry[0..len]);
                            self.length = len;
                            self.cursor = len;
                            self.buffer[len] = 0;
                            self.redrawInput(prompt);
                        }
                    }
                },
                c.KEY_DOWN, 14 => { // Ctrl+N - next history
                    if (self.history_position != -1) {
                        self.history_position += 1;

                        if (self.history_position >= @as(i32, @intCast(self.history_count))) {
                            // Restore saved input
                            if (self.saved_input) |saved| {
                                const len = @min(saved.len, self.buffer_capacity - 1);
                                @memcpy(self.buffer[0..len], saved[0..len]);
                                self.length = len;
                                self.cursor = len;
                                self.buffer[len] = 0;
                            } else {
                                self.buffer[0] = 0;
                                self.length = 0;
                                self.cursor = 0;
                            }
                            self.history_position = -1;
                        } else {
                            // Show next entry
                            const hist_entry = self.history[@intCast(self.history_position)];
                            const len = @min(hist_entry.len, self.buffer_capacity - 1);
                            @memcpy(self.buffer[0..len], hist_entry[0..len]);
                            self.length = len;
                            self.cursor = len;
                            self.buffer[len] = 0;
                        }
                        self.redrawInput(prompt);
                    }
                },

                // ============================================================
                // Editing keys
                // ============================================================
                c.KEY_BACKSPACE, 127, 8 => {
                    if (self.backspace()) {
                        self.redrawInput(prompt);
                    }
                },
                c.KEY_DC => { // Delete key
                    if (self.deleteChar()) {
                        self.redrawInput(prompt);
                    }
                },
                11 => { // Ctrl+K - kill to end of line
                    self.killToEnd();
                    self.redrawInput(prompt);
                },
                21 => { // Ctrl+U - kill to beginning of line
                    self.killToBeginning();
                    self.redrawInput(prompt);
                },
                12 => { // Ctrl+L - clear entire input
                    self.clearInput();
                    self.redrawInput(prompt);
                },

                // ============================================================
                // Word operations (Alt/Esc sequences)
                // ============================================================
                27 => { // ESC - may be Alt key or escape sequence
                    // Set nodelay mode to check for follow-up character
                    _ = c.nodelay(win, c.TRUE);
                    const next_ch = c.wgetch(win);
                    _ = c.nodelay(win, c.FALSE);

                    if (next_ch == c.ERR) {
                        // Standalone ESC - ignore for now
                        break;
                    }

                    switch (next_ch) {
                        'b', 'B' => { // Alt+b - backward word
                            self.cursor = moveBackwardWord(self.buffer[0..self.length], self.cursor);
                            self.redrawInput(prompt);
                        },
                        'f', 'F' => { // Alt+f - forward word
                            self.cursor = moveForwardWord(self.buffer[0..self.length], self.cursor);
                            self.redrawInput(prompt);
                        },
                        'd', 'D' => { // Alt+d - delete next word
                            if (self.deleteWordForward()) {
                                self.redrawInput(prompt);
                            }
                        },
                        127, 8 => { // Alt+Backspace - delete previous word
                            if (self.deleteWordBackward()) {
                                self.redrawInput(prompt);
                            }
                        },
                        else => {},
                    }
                },

                // ============================================================
                // Submit and control
                // ============================================================
                '\r', c.KEY_ENTER => { // Enter - submit
                    running = false;
                },
                '\n' => { // Ctrl+J - insert newline for multiline input
                    try self.insertChar('\n');
                    self.redrawInput(prompt);
                },
                4 => { // Ctrl+D - EOF
                    return null;
                },

                // ============================================================
                // Tab completion
                // ============================================================
                '\t' => {
                    if (self.completer) |completer| {
                        if (completer(self.buffer[0..self.length], self.cursor, self.completer_ctx)) |res| {
                            defer {
                                var r = res;
                                r.deinit();
                                self.allocator.destroy(res);
                            }

                            if (res.count == 0) {
                                // No completions, beep
                                _ = c.beep();
                            } else if (res.count == 1) {
                                // Single completion: replace current word
                                const opt = res.options[0];

                                // Find start of current word
                                var start: usize = if (self.cursor > 0) self.cursor - 1 else 0;
                                while (start > 0 and self.buffer[start] != ' ' and self.buffer[start] != '\t') {
                                    start -= 1;
                                }
                                if (start > 0 or (self.buffer[start] == ' ' or self.buffer[start] == '\t')) {
                                    start += 1;
                                }

                                const tail_len = self.length - self.cursor;
                                const needed = start + opt.len + tail_len + 1;

                                try self.ensureCapacity(needed);

                                // Move tail
                                if (tail_len > 0) {
                                    std.mem.copyBackwards(u8, self.buffer[start + opt.len .. start + opt.len + tail_len], self.buffer[self.cursor .. self.cursor + tail_len]);
                                }

                                // Copy completion
                                @memcpy(self.buffer[start .. start + opt.len], opt);
                                self.cursor = start + opt.len;
                                self.length = start + opt.len + tail_len;
                                self.buffer[self.length] = 0;

                                self.redrawInput(prompt);
                            } else {
                                // Multiple completions - would need to show list
                                // For now, just beep
                                _ = c.beep();
                            }
                        } else {
                            _ = c.beep();
                        }
                    } else {
                        _ = c.beep();
                    }
                },

                // ============================================================
                // Regular printable characters
                // ============================================================
                else => {
                    if (ch >= 32 and ch < 127) {
                        try self.insertChar(@intCast(ch));
                        self.redrawInput(prompt);
                    }
                },
            }
        }

        // Add to history (if not empty)
        if (self.length > 0) {
            self.historyAdd(self.buffer[0..self.length]);
        }

        // Return a copy of the input
        return try self.allocator.dupe(u8, self.buffer[0..self.length]);
    }
};

// ---------------------------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------------------------

/// Free a completion result (convenience function)
pub fn completionFreeResult(result: *CompletionResult) void {
    result.deinit();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "NCursesInput: basic initialization" {
    // Note: This test requires ncurses initialization
    // In practice, these tests would need a mock window
    // For now, we just test the struct layout and basic operations

    const allocator = std.testing.allocator;

    // Test buffer operations directly
    const buffer = try allocator.alloc(u8, INITIAL_BUFFER_SIZE);
    defer allocator.free(buffer);

    const history = try allocator.alloc([]u8, DEFAULT_HISTORY_SIZE);
    defer allocator.free(history);

    // Test word boundary detection
    try std.testing.expect(NCursesInput.isWordBoundary(' '));
    try std.testing.expect(NCursesInput.isWordBoundary('\t'));
    try std.testing.expect(NCursesInput.isWordBoundary('.'));
    try std.testing.expect(!NCursesInput.isWordBoundary('a'));
    try std.testing.expect(!NCursesInput.isWordBoundary('A'));
    try std.testing.expect(!NCursesInput.isWordBoundary('1'));
    try std.testing.expect(!NCursesInput.isWordBoundary('_'));
}

test "NCursesInput: word movement" {
    const buffer = "hello world test";

    // Move backward word
    try std.testing.expectEqual(@as(usize, 6), NCursesInput.moveBackwardWord(buffer, 11)); // From "test" to "world"
    try std.testing.expectEqual(@as(usize, 0), NCursesInput.moveBackwardWord(buffer, 6)); // From "world" to start

    // Move forward word
    try std.testing.expectEqual(@as(usize, 12), NCursesInput.moveForwardWord(buffer, 6)); // From "world" → start of "test"
    try std.testing.expectEqual(@as(usize, 6), NCursesInput.moveForwardWord(buffer, 0)); // From start → start of "world"
}

test "NCursesInput: calculateNeededLines" {
    const available_width: i32 = 10;
    const prompt_len: i32 = 2;

    // Empty buffer
    try std.testing.expectEqual(@as(i32, 1), NCursesInput.calculateNeededLines("", 0, available_width, prompt_len));

    // Simple line
    try std.testing.expectEqual(@as(i32, 1), NCursesInput.calculateNeededLines("hello", 5, available_width, prompt_len));

    // Line with newline
    try std.testing.expectEqual(@as(i32, 2), NCursesInput.calculateNeededLines("hello\nworld", 11, available_width, prompt_len));

    // Long line that wraps
    try std.testing.expectEqual(@as(i32, 2), NCursesInput.calculateNeededLines("123456789012345", 15, available_width, prompt_len));
}

test "NCursesInput: calculateCursorPosition" {
    const available_width: i32 = 10;
    const prompt_len: i32 = 2;

    var line: i32 = 0;
    var col: i32 = 0;

    // At start
    NCursesInput.calculateCursorPosition("hello world", 0, available_width, prompt_len, &line, &col);
    try std.testing.expectEqual(@as(i32, 0), line);
    try std.testing.expectEqual(@as(i32, 2), col); // After prompt

    // After "hello" - reset values
    line = 0;
    col = 0;
    NCursesInput.calculateCursorPosition("hello\nworld", 6, available_width, prompt_len, &line, &col);
    try std.testing.expectEqual(@as(i32, 1), line); // Second line
    try std.testing.expectEqual(@as(i32, 0), col); // Start of line
}

test "CompletionResult: initialization and cleanup" {
    const allocator = std.testing.allocator;

    // Test empty result
    const empty = CompletionResult.empty(allocator);
    try std.testing.expectEqual(@as(usize, 0), empty.count);

    // Test with options
    const options = try allocator.alloc([]u8, 2);
    options[0] = try allocator.dupe(u8, "option1");
    options[1] = try allocator.dupe(u8, "option2");

    const result = CompletionResult.init(allocator, options);
    try std.testing.expectEqual(@as(usize, 2), result.count);

    var result_mut = result;
    result_mut.deinit();
}
