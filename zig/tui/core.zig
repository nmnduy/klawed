//! TUI Core Initialization and Cleanup
//!
//! Idiomatic Zig replacement for src/tui_core.c
//!
//! Handles core TUI lifecycle operations including initialization,
//! cleanup, suspend/resume, and startup display.

const std = @import("std");
const logger = @import("../logger.zig");

// ---------------------------------------------------------------------------
// ncurses FFI
// ---------------------------------------------------------------------------

const c = @cImport({
    @cInclude("ncurses.h");
    @cInclude("locale.h");
});

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const INPUT_BUFFER_SIZE = 8192;
const INPUT_WIN_MIN_HEIGHT = 2;
const INPUT_WIN_MAX_HEIGHT_PERCENT = 20;
const CONV_WIN_PADDING = 0;
const STATUS_WIN_HEIGHT = 1;

// ---------------------------------------------------------------------------
// Color Pair Constants (matching colorscheme.h)
// ---------------------------------------------------------------------------

pub const NCURSES_PAIR_FOREGROUND: i16 = 1;
pub const NCURSES_PAIR_USER: i16 = 2;
pub const NCURSES_PAIR_ASSISTANT: i16 = 3;
pub const NCURSES_PAIR_STATUS: i16 = 4;
pub const NCURSES_PAIR_ERROR: i16 = 5;
pub const NCURSES_PAIR_PROMPT: i16 = 6;
pub const NCURSES_PAIR_TODO_COMPLETED: i16 = 7;
pub const NCURSES_PAIR_TODO_IN_PROGRESS: i16 = 8;
pub const NCURSES_PAIR_TODO_PENDING: i16 = 9;
pub const NCURSES_PAIR_TOOL: i16 = 10;
pub const NCURSES_PAIR_SEARCH: i16 = 11;
pub const NCURSES_PAIR_INPUT_BG: i16 = 12;
pub const NCURSES_PAIR_INPUT_BORDER: i16 = 13;
pub const NCURSES_PAIR_USER_MSG_BG: i16 = 14;
pub const NCURSES_PAIR_ASSISTANT_BG: i16 = 15;
pub const NCURSES_PAIR_ASSISTANT_BORDER_BG: i16 = 16;
pub const NCURSES_PAIR_TOOL_DIM: i16 = 17;
pub const NCURSES_PAIR_DIFF_CONTEXT: i16 = 18;

// ---------------------------------------------------------------------------
// TUI Mode Enumeration
// ---------------------------------------------------------------------------

pub const TUIMode = enum {
    normal, // Normal mode (vim-like navigation)
    insert, // Insert mode (text input)
    command, // Command mode (entered with ':' from normal mode)
    search, // Search mode (entered with '/' or '?' from normal mode)
    file_search, // File search mode (Ctrl+F)
    history_search, // History search mode (Ctrl+R)
};

// ---------------------------------------------------------------------------
// Input Box Style
// ---------------------------------------------------------------------------

pub const TUIInputBoxStyle = enum {
    background, // Background color + left border
    border, // Full border with no background
    horizontal, // Top and bottom border only
    bland, // Just caret '>>>' with text on general background
};

// ---------------------------------------------------------------------------
// Response Style
// ---------------------------------------------------------------------------

pub const TUIResponseStyle = enum {
    border, // Left border '│ ' on each line
    caret, // Leading '>>> ' caret, no wrapping borders
};

// ---------------------------------------------------------------------------
// Todo Status (forward declaration - actual type in tools/todo.zig)
// ---------------------------------------------------------------------------

pub const TodoStatus = enum {
    pending,
    in_progress,
    completed,
};

pub const TodoItem = struct {
    content: []const u8,
    active_form: []const u8,
    status: TodoStatus,
};

pub const TodoList = struct {
    items: []const TodoItem,
    count: usize,
};

// ---------------------------------------------------------------------------
// Subagent Process Info (forward declaration)
// ---------------------------------------------------------------------------

pub const SubagentProcess = struct {
    pid: i32,
    prompt: ?[]const u8,
    log_file: ?[]const u8,
    last_log_tail: ?[]const u8,
    completed: bool,
};

pub const SubagentManager = struct {
    processes: []SubagentProcess,
    process_count: i32,

    pub fn getRunningCount(self: *const SubagentManager) i32 {
        var count: i32 = 0;
        for (self.processes) |proc| {
            if (!proc.completed) count += 1;
        }
        return count;
    }

    pub fn getProcess(self: *const SubagentManager, index: i32) ?SubagentProcess {
        if (index < 0 or index >= self.process_count) return null;
        return self.processes[@intCast(index)];
    }
};

// ---------------------------------------------------------------------------
// Window Manager Configuration
// ---------------------------------------------------------------------------

pub const WindowManagerConfig = struct {
    min_conv_height: i32 = 5,
    min_input_height: i32 = INPUT_WIN_MIN_HEIGHT,
    max_input_height: i32 = 5,
    status_height: i32 = STATUS_WIN_HEIGHT,
    padding: i32 = CONV_WIN_PADDING,
    conv_h_padding: i32 = 0,
    initial_pad_capacity: i32 = 1000,
    max_todo_height: i32 = 5,
};

pub const DEFAULT_WINDOW_CONFIG = WindowManagerConfig{};

// ---------------------------------------------------------------------------
// Window Manager State
// ---------------------------------------------------------------------------

pub const WindowManager = struct {
    // Screen dimensions
    screen_width: i32 = 0,
    screen_height: i32 = 0,

    // Conversation pad (virtual scrollable window)
    conv_pad: ?*c.WINDOW = null,
    conv_pad_capacity: i32 = 0,
    conv_pad_content_lines: i32 = 0,
    conv_viewport_height: i32 = 0,
    conv_scroll_offset: i32 = 0,

    // Status window
    status_win: ?*c.WINDOW = null,
    status_height: i32 = 0,

    // Input window
    input_win: ?*c.WINDOW = null,
    input_height: i32 = 0,

    // TODO window
    todo_win: ?*c.WINDOW = null,
    todo_height: i32 = 0,

    // Configuration
    config: WindowManagerConfig,

    // State flags
    is_initialized: bool = false,

    /// Initialize window manager
    pub fn init(self: *WindowManager, cfg: *const WindowManagerConfig) !void {
        self.config = cfg.*;

        // Get screen dimensions
        var height: i32 = 0;
        var width: i32 = 0;
        _ = c.getmaxyx(c.stdscr, &height, &width);
        self.screen_height = height;
        self.screen_width = width;

        // Calculate layout
        self.status_height = cfg.status_height;
        const remaining_height = height - self.status_height;
        self.input_height = cfg.min_input_height;
        self.conv_viewport_height = remaining_height - self.input_height;

        // Create windows
        try self.createWindows();

        self.is_initialized = true;

        logger.defaultLogger.log(.debug, "[WINDOW_MANAGER] Initialized (screen={d}x{d})", .{ width, height });
    }

    /// Create all windows
    fn createWindows(self: *WindowManager) !void {
        // Create conversation pad
        self.conv_pad_capacity = self.config.initial_pad_capacity;
        self.conv_pad = c.newpad(self.conv_pad_capacity, self.screen_width);
        if (self.conv_pad == null) {
            return error.WindowCreationFailed;
        }

        // Create status window
        if (self.status_height > 0) {
            const status_y = self.screen_height - self.status_height - self.input_height;
            self.status_win = c.newwin(self.status_height, self.screen_width, status_y, 0);
            if (self.status_win == null) {
                return error.WindowCreationFailed;
            }
        }

        // Create input window
        const input_y = self.screen_height - self.input_height;
        self.input_win = c.newwin(self.input_height, self.screen_width, input_y, 0);
        if (self.input_win == null) {
            return error.WindowCreationFailed;
        }
    }

    /// Destroy all windows
    pub fn deinit(self: *WindowManager) void {
        if (self.conv_pad) |pad| {
            _ = c.delwin(pad);
            self.conv_pad = null;
        }
        if (self.status_win) |win| {
            _ = c.delwin(win);
            self.status_win = null;
        }
        if (self.input_win) |win| {
            _ = c.delwin(win);
            self.input_win = null;
        }
        if (self.todo_win) |win| {
            _ = c.delwin(win);
            self.todo_win = null;
        }
        self.is_initialized = false;
    }

    /// Set content line count
    pub fn setContentLines(self: *WindowManager, lines: i32) void {
        self.conv_pad_content_lines = lines;
    }

    /// Scroll conversation by delta lines
    pub fn scroll(self: *WindowManager, delta: i32) void {
        self.conv_scroll_offset += delta;

        // Clamp to valid range
        const max_scroll = self.getMaxScroll();
        if (self.conv_scroll_offset < 0) {
            self.conv_scroll_offset = 0;
        } else if (self.conv_scroll_offset > max_scroll) {
            self.conv_scroll_offset = max_scroll;
        }
    }

    /// Get maximum scroll offset
    pub fn getMaxScroll(self: *const WindowManager) i32 {
        const content = self.conv_pad_content_lines;
        const viewport = self.conv_viewport_height;
        if (content <= viewport) return 0;
        return content - viewport;
    }

    /// Scroll to bottom
    pub fn scrollToBottom(self: *WindowManager) void {
        self.conv_scroll_offset = self.getMaxScroll();
    }

    /// Scroll to top
    pub fn scrollToTop(self: *WindowManager) void {
        self.conv_scroll_offset = 0;
    }
};

// ---------------------------------------------------------------------------
// TUI State
// ---------------------------------------------------------------------------

pub const TUIState = struct {
    // Centralized window manager
    wm: WindowManager,

    // Conversation entries
    entries: ?[]ConversationEntry = null,
    entries_count: usize = 0,
    entries_capacity: usize = 0,
    last_assistant_line: i32 = -1,

    // Status state
    status_message: ?[]u8 = null,
    status_visible: bool = false,
    status_spinner_active: bool = false,
    status_spinner_frame: i32 = 0,
    status_spinner_last_update_ns: u64 = 0,

    // Mode
    mode: TUIMode = .insert,
    input_box_style: TUIInputBoxStyle = .horizontal,
    response_style: TUIResponseStyle = .border,
    normal_mode_last_key: i32 = 0,

    // Command buffer
    command_buffer: ?[]u8 = null,
    command_buffer_len: usize = 0,
    command_buffer_capacity: usize = 0,

    // Search state
    search_buffer: ?[]u8 = null,
    search_buffer_len: usize = 0,
    search_buffer_capacity: usize = 0,
    search_direction: i32 = 1,
    last_search_match_line: i32 = -1,
    last_search_pattern: ?[]u8 = null,

    // State flags
    is_initialized: bool = false,
    terminal_suspended: bool = false,

    // Input history
    input_history: ?[][]u8 = null,
    input_history_count: usize = 0,
    input_history_capacity: usize = 0,
    input_history_index: i32 = -1,
    input_saved_before_history: ?[]u8 = null,

    // vim-fugitive availability
    vim_fugitive_available: i32 = -1, // -1 = unknown
    vim_fugitive_mutex: std.Thread.Mutex = .{},
    vim_fugitive_mutex_initialized: bool = false,

    // Tool output tracking
    last_tool_name: ?[]u8 = null,

    // Subagent display
    subagent_display_active: bool = false,
    subagent_display_scroll_offset: i32 = 0,
    subagent_display_max_lines: i32 = 0,

    // TODO display
    todo_display_active: bool = false,
    todo_display_scroll_offset: i32 = 0,

    // Cursor position for normal mode
    normal_cursor_line: i32 = 0,
    normal_cursor_col: i32 = 0,
    normal_viewport_top_line: i32 = 0,

    // Mouse support
    mouse_enabled: bool = false,

    // Allocator
    allocator: std.mem.Allocator,

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Initialize TUI
    pub fn init(self: *TUIState, allocator: std.mem.Allocator) !void {
        self.allocator = allocator;

        // Set locale for UTF-8 support
        _ = c.setlocale(c.LC_ALL, "");

        // Initialize ncurses
        _ = c.initscr();

        // Set ESC delay to 25ms for responsive ESC/Ctrl+[ mode switching
        _ = c.set_escdelay(25);

        // Use raw mode so Ctrl+C is delivered as a key
        _ = c.raw();
        _ = c.noecho();
        _ = c.nonl();
        _ = c.keypad(c.stdscr, c.TRUE);
        _ = c.nodelay(c.stdscr, c.FALSE);
        _ = c.curs_set(2); // Make cursor very visible (block cursor)

        // Enable bracketed paste mode
        _ = c.printf("\x1b[?2004h");
        _ = c.fflush(c.stdout);

        // Initialize colors
        try reloadColors();

        // Get screen dimensions to calculate max input height
        var screen_height: i32 = 0;
        var screen_width: i32 = 0;
        _ = c.getmaxyx(c.stdscr, &screen_height, &screen_width);

        // Calculate max input height as 20% of screen height
        var calculated_max_height = (screen_height * INPUT_WIN_MAX_HEIGHT_PERCENT) / 100;
        if (calculated_max_height < INPUT_WIN_MIN_HEIGHT) {
            calculated_max_height = INPUT_WIN_MIN_HEIGHT;
        }

        // Initialize window manager
        var cfg = DEFAULT_WINDOW_CONFIG;
        cfg.min_conv_height = 5;
        cfg.min_input_height = INPUT_WIN_MIN_HEIGHT;
        cfg.max_input_height = calculated_max_height;
        cfg.status_height = STATUS_WIN_HEIGHT;
        cfg.padding = CONV_WIN_PADDING;

        try self.wm.init(&cfg);
        self.wm.setContentLines(0);

        // Initialize vim-fugitive mutex
        self.vim_fugitive_mutex = std.Thread.Mutex{};
        self.vim_fugitive_mutex_initialized = true;

        self.is_initialized = true;

        logger.defaultLogger.log(.debug, "[TUI] Initialized (screen={d}x{d})", .{ screen_width, screen_height });
    }

    /// Clean up TUI resources
    pub fn deinit(self: *TUIState) void {
        if (!self.is_initialized) return;

        // Free conversation entries
        self.freeConversationEntries();

        // Free status message
        if (self.status_message) |msg| {
            self.allocator.free(msg);
            self.status_message = null;
        }

        // Free command buffer
        if (self.command_buffer) |buf| {
            self.allocator.free(buf);
            self.command_buffer = null;
        }

        // Free search state
        if (self.search_buffer) |buf| {
            self.allocator.free(buf);
            self.search_buffer = null;
        }
        if (self.last_search_pattern) |pattern| {
            self.allocator.free(pattern);
            self.last_search_pattern = null;
        }

        // Destroy windows
        self.wm.deinit();

        // Disable bracketed paste mode
        _ = c.printf("\x1b[?2004l");
        _ = c.fflush(c.stdout);

        // End ncurses
        _ = c.endwin();

        self.is_initialized = false;

        // Print newline for clean exit
        _ = c.printf("\n");
        _ = c.fflush(c.stdout);

        // Free input history
        if (self.input_history) |history| {
            for (0..self.input_history_count) |i| {
                self.allocator.free(history[i]);
            }
            self.allocator.free(history);
            self.input_history = null;
        }
        if (self.input_saved_before_history) |saved| {
            self.allocator.free(saved);
            self.input_saved_before_history = null;
        }

        // Free tool name tracking
        if (self.last_tool_name) |name| {
            self.allocator.free(name);
            self.last_tool_name = null;
        }
    }

    /// Free conversation entries
    fn freeConversationEntries(self: *TUIState) void {
        if (self.entries) |entries| {
            for (entries) |entry| {
                if (entry.prefix) |prefix| self.allocator.free(prefix);
                if (entry.text) |text| self.allocator.free(text);
            }
            self.allocator.free(entries);
            self.entries = null;
        }
        self.entries_count = 0;
        self.entries_capacity = 0;
    }

    // -----------------------------------------------------------------------
    // Suspend/Resume
    // -----------------------------------------------------------------------

    /// Suspend TUI (restore terminal to normal mode for external commands)
    pub fn suspendTUI(self: *TUIState) !void {
        if (!self.is_initialized or self.terminal_suspended) return;

        logger.defaultLogger.log(.debug, "[TUI] Suspending terminal for external command", .{});

        // Save current terminal state
        _ = c.def_prog_mode();

        // Disable bracketed paste mode
        _ = c.printf("\x1b[?2004l");
        _ = c.fflush(c.stdout);

        // End ncurses mode
        _ = c.endwin();

        self.terminal_suspended = true;
    }

    /// Resume TUI (restore terminal to program mode after external commands)
    pub fn resumeTUI(self: *TUIState) !void {
        if (!self.is_initialized or !self.terminal_suspended) return;

        logger.defaultLogger.log(.debug, "[TUI] Resuming terminal after external command", .{});

        // Restore ncurses mode
        _ = c.reset_prog_mode();
        _ = c.refresh();

        // Re-enable bracketed paste mode
        _ = c.printf("\x1b[?2004h");
        _ = c.fflush(c.stdout);

        // Redraw the TUI
        self.refresh();

        self.terminal_suspended = false;
    }

    // -----------------------------------------------------------------------
    // Refresh and Display
    // -----------------------------------------------------------------------

    /// Refresh the display
    pub fn refresh(self: *TUIState) void {
        // TODO: Implement full refresh
        _ = self;
        _ = c.refresh();
    }

    /// Add a conversation line
    pub fn addConversationLine(self: *TUIState, prefix: ?[]const u8, text: []const u8, color_pair: i32) void {
        // TODO: Implement conversation line addition
        logger.defaultLogger.log(.debug, "[TUI] Add line: {s}", .{text});
        _ = self;
        _ = prefix;
        _ = color_pair;
    }

    // -----------------------------------------------------------------------
    // vim-fugitive Availability
    // -----------------------------------------------------------------------

    /// Get vim-fugitive availability (cached result)
    pub fn getVimFugitiveAvailable(self: *TUIState) i32 {
        if (!self.vim_fugitive_mutex_initialized) return -1;

        self.vim_fugitive_mutex.lock();
        defer self.vim_fugitive_mutex.unlock();
        return self.vim_fugitive_available;
    }

    /// Start background check for vim-fugitive availability
    pub fn startVimFugitiveCheck(self: *TUIState) void {
        if (!self.vim_fugitive_mutex_initialized) return;

        // Only start check if we haven't checked yet
        self.vim_fugitive_mutex.lock();
        const current = self.vim_fugitive_available;
        self.vim_fugitive_mutex.unlock();

        if (current != -1) {
            logger.defaultLogger.log(.debug, "[TUI] vim-fugitive availability already checked: {d}", .{current});
            return;
        }

        // Start background thread
        const thread = std.Thread.spawn(.{}, checkVimFugitiveThread, .{self}) catch |err| {
            logger.defaultLogger.log(.warn, "[TUI] Failed to create background thread for vim-fugitive check: {}", .{err});
            return;
        };
        thread.detach();

        logger.defaultLogger.log(.debug, "[TUI] Started background thread to check vim-fugitive availability", .{});
    }
};

// ---------------------------------------------------------------------------
// Conversation Entry
// ---------------------------------------------------------------------------

pub const ConversationEntry = struct {
    prefix: ?[]u8,
    text: ?[]u8,
    color_pair: i32,
};

// ---------------------------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------------------------

/// Convert RGB (0-255) to ncurses color (0-1000)
fn rgbToNcurses(value: i32) i16 {
    return @intCast(@divTrunc(value * 1000, 255));
}

/// Initialize ncurses color pairs from theme
pub fn reloadColors() !void {
    // Check if terminal supports colors
    if (c.has_colors() == c.FALSE) {
        logger.defaultLogger.log(.debug, "[TUI] Terminal does not support colors", .{});
        return;
    }

    _ = c.start_color();

    // Try to use terminal's default colors as base
    const default_colors_supported = (c.use_default_colors() == c.OK);
    const default_bg: i16 = if (default_colors_supported) -1 else c.COLOR_BLACK;

    if (!default_colors_supported) {
        logger.defaultLogger.log(.debug, "[TUI] Terminal does not support default colors", .{});
    }

    // For now, use standard ncurses colors
    // Custom theme colors would require the full theme loading logic
    _ = c.init_pair(NCURSES_PAIR_FOREGROUND, c.COLOR_WHITE, default_bg);
    _ = c.init_pair(NCURSES_PAIR_USER, c.COLOR_GREEN, default_bg);
    _ = c.init_pair(NCURSES_PAIR_ASSISTANT, c.COLOR_CYAN, default_bg);
    _ = c.init_pair(NCURSES_PAIR_STATUS, c.COLOR_YELLOW, default_bg);
    _ = c.init_pair(NCURSES_PAIR_ERROR, c.COLOR_RED, default_bg);
    _ = c.init_pair(NCURSES_PAIR_PROMPT, c.COLOR_GREEN, default_bg);
    _ = c.init_pair(NCURSES_PAIR_TODO_COMPLETED, c.COLOR_GREEN, default_bg);
    _ = c.init_pair(NCURSES_PAIR_TODO_IN_PROGRESS, c.COLOR_YELLOW, default_bg);
    _ = c.init_pair(NCURSES_PAIR_TODO_PENDING, c.COLOR_MAGENTA, default_bg);
    _ = c.init_pair(NCURSES_PAIR_TOOL, c.COLOR_YELLOW, default_bg);
    _ = c.init_pair(NCURSES_PAIR_SEARCH, c.COLOR_MAGENTA, default_bg);
    _ = c.init_pair(NCURSES_PAIR_INPUT_BG, c.COLOR_WHITE, c.COLOR_BLACK);
    _ = c.init_pair(NCURSES_PAIR_INPUT_BORDER, c.COLOR_GREEN, default_bg);
    _ = c.init_pair(NCURSES_PAIR_USER_MSG_BG, c.COLOR_WHITE, c.COLOR_BLACK);
    _ = c.init_pair(NCURSES_PAIR_ASSISTANT_BG, c.COLOR_WHITE, default_bg);
    _ = c.init_pair(NCURSES_PAIR_ASSISTANT_BORDER_BG, c.COLOR_CYAN, default_bg);
    _ = c.init_pair(NCURSES_PAIR_TOOL_DIM, c.COLOR_WHITE, default_bg);
    _ = c.init_pair(NCURSES_PAIR_DIFF_CONTEXT, c.COLOR_WHITE, default_bg);
}

/// Background thread to check vim-fugitive availability
fn checkVimFugitiveThread(tui: *TUIState) void {
    logger.defaultLogger.log(.debug, "[TUI] Background thread checking vim-fugitive availability", .{});

    // Check if vim-fugitive is available by running vim with a test command
    const test_cmd = "vim -c \"if exists(':Git') | q | else | cquit 1 | endif\" -c \"q\" 2>&1";

    const result = std.process.Child.run(.{
        .allocator = std.heap.page_allocator,
        .argv = &.{ "sh", "-c", test_cmd },
    }) catch |err| {
        logger.defaultLogger.log(.warn, "[TUI] Failed to check vim-fugitive: {}", .{err});
        return;
    };
    defer {
        std.heap.page_allocator.free(result.stdout);
        std.heap.page_allocator.free(result.stderr);
    }

    // vim returns 0 if fugitive exists, non-zero otherwise
    const available: i32 = if (result.term.Exited == 0) 1 else 0;

    // Update cached value with thread-safe mutex
    if (tui.vim_fugitive_mutex_initialized) {
        tui.vim_fugitive_mutex.lock();
        tui.vim_fugitive_available = available;
        tui.vim_fugitive_mutex.unlock();

        logger.defaultLogger.log(.debug, "[TUI] Background check complete: vim-fugitive {s}", .{if (available == 1) "available" else "not available"});
    }
}

/// Show startup banner
pub fn showStartupBanner(tui: *TUIState, version: []const u8, model: []const u8, working_dir: []const u8) void {
    if (!tui.is_initialized) return;

    // Check if VLTRN mode is enabled
    const vltrn_mode = std.process.getEnvVarOwned(tui.allocator, "VLTRN_MODE") catch null;
    defer if (vltrn_mode) |v| tui.allocator.free(v);
    const is_vltrn = if (vltrn_mode) |v| std.mem.eql(u8, v, "1") else false;

    // Add padding before mascot
    tui.addConversationLine(null, "", NCURSES_PAIR_FOREGROUND);

    // Add banner lines
    const name = if (is_vltrn) "vltrn" else "klawed";

    var buf1: [256]u8 = undefined;
    var buf2: [256]u8 = undefined;
    var buf3: [256]u8 = undefined;

    const line1 = std.fmt.bufPrint(&buf1, "  /\\_/\\   {s} v{s}", .{ name, version }) catch return;
    const line2 = std.fmt.bufPrint(&buf2, " ( o.o )  {s}", .{model}) catch return;
    const line3 = std.fmt.bufPrint(&buf3, "  > ^ <    {s}", .{working_dir}) catch return;

    if (!is_vltrn) {
        tui.addConversationLine(null, line1, NCURSES_PAIR_ASSISTANT);
        tui.addConversationLine(null, line2, NCURSES_PAIR_ASSISTANT);
        tui.addConversationLine(null, line3, NCURSES_PAIR_ASSISTANT);
    }

    tui.addConversationLine(null, "", NCURSES_PAIR_FOREGROUND);

    // Tips array - randomly select one
    const tips = [_][]const u8{
        "Esc/Ctrl+[ to enter Scroll mode (vim-style); press 'i' to insert.",
        "In Scroll mode, Scroll: j/k (line), Ctrl+D/U (half page), gg/G (top/bottom).",
        "In Scroll mode, use ( and ) to jump between text blocks (paragraphs).",
        "Press Shift+Tab to toggle Plan mode (read-only tools only).",
        "Press Ctrl+C to cancel a running API/tool action.",
        "In Normal mode, :!cmd runs a shell command in the current dir (like Vim).",
        "In Normal mode, :re !cmd puts the command output into the input box.",
        "In Normal mode, :git opens vim-fugitive (requires vim-fugitive plugin).",
        "Press Ctrl+D to exit quickly.",
        "Set KLAWED_THEME to change colors. Available: tender (default), kitty-default, dracula, gruvbox-dark, solarized-dark, black-metal.",
        "Set KLAWED_LOG_LEVEL=DEBUG for verbose logs.",
        "API history stored in ./.klawed/api_calls.db (configurable via KLAWED_DB_PATH).",
        "Insert mode supports readline keys: Ctrl+A, Ctrl+E, Alt+B, Alt+F.",
        "Interrupt long tool runs any time with Ctrl+C.",
        "Press Ctrl+F to open file search popup (fuzzy find files).",
        "Press Ctrl+R to open history search popup (fuzzy find previous commands).",
        "MCP is disabled by default; enable with KLAWED_MCP_ENABLED=1.",
        "Use /clear to clear conversation; /quit or /exit to leave.",
        "Use :help to see all available commands.",
        "Token usage stats shown in status bar when in Normal mode (Esc).",
        "Exit methods: Ctrl+D, /quit, or /exit.",
    };

    // Simple pseudo-random selection
    const seed = @as(u64, @intCast(std.time.timestamp())) ^ @as(u64, @intCast(std.process.getPid()));
    const tip_index = seed % tips.len;

    var tip_buf: [512]u8 = undefined;
    const tip_line = std.fmt.bufPrint(&tip_buf, "Tip: {s}", .{tips[tip_index]}) catch return;

    tui.addConversationLine(null, tip_line, NCURSES_PAIR_STATUS);
    tui.addConversationLine(null, "", NCURSES_PAIR_FOREGROUND);
}

/// Render TODO list
pub fn renderTodoList(tui: *TUIState, list: *const TodoList) void {
    if (list.count == 0) return;

    for (list.items) |item| {
        var line_buf: [1024]u8 = undefined;
        const color: i32 = switch (item.status) {
            .completed => NCURSES_PAIR_TODO_COMPLETED,
            .in_progress => NCURSES_PAIR_TODO_IN_PROGRESS,
            .pending => NCURSES_PAIR_TODO_PENDING,
        };
        const symbol: []const u8 = switch (item.status) {
            .completed => "✓",
            .in_progress => "⋯",
            .pending => "○",
        };
        const text: []const u8 = switch (item.status) {
            .in_progress => item.active_form,
            else => item.content,
        };

        const line = std.fmt.bufPrint(&line_buf, "    {s} {s}", .{ symbol, text }) catch continue;
        tui.addConversationLine(null, line, color);
    }
}

/// Render active subagents
pub fn renderActiveSubagents(tui: *TUIState, manager: *const SubagentManager) void {
    const running_count = manager.getRunningCount();
    if (running_count == 0) return;

    // Add header
    var header_buf: [256]u8 = undefined;
    const header = std.fmt.bufPrint(&header_buf, "━━━━━━━ Active Subagents ({d} running) ━━━━━━━", .{running_count}) catch return;
    tui.addConversationLine(null, header, NCURSES_PAIR_TOOL);

    // Iterate through processes
    var i: i32 = 0;
    while (i < manager.process_count) : (i += 1) {
        const proc = manager.getProcess(i) orelse continue;
        if (proc.completed) continue;

        // Display PID and prompt
        const prompt = proc.prompt orelse "(no prompt)";
        var prompt_truncated: [100]u8 = undefined;
        const display_prompt = if (prompt.len > 80)
            std.fmt.bufPrint(&prompt_truncated, "{s:.77}...", .{prompt}) catch prompt
        else
            prompt;

        var info_buf: [512]u8 = undefined;
        const info = std.fmt.bufPrint(&info_buf, "  [PID {d}] {s}", .{ proc.pid, display_prompt }) catch continue;
        tui.addConversationLine(null, info, NCURSES_PAIR_STATUS);

        // Display log tail if available
        if (proc.last_log_tail) |tail| {
            if (tail.len > 0) {
                // Show up to 5 lines
                var lines_iter = std.mem.splitScalar(u8, tail, '\n');
                var line_count: i32 = 0;
                const max_lines = 5;

                while (lines_iter.next()) |line| {
                    if (line_count >= max_lines) break;

                    var indent_buf: [1024]u8 = undefined;
                    const indented = std.fmt.bufPrint(&indent_buf, "    {s}", .{line}) catch continue;
                    tui.addConversationLine(null, indented, NCURSES_PAIR_FOREGROUND);
                    line_count += 1;
                }

                if (lines_iter.next() != null) {
                    tui.addConversationLine(null, "    [... more output in log file ...]", NCURSES_PAIR_STATUS);
                }
            } else {
                tui.addConversationLine(null, "    (waiting for output...)", NCURSES_PAIR_STATUS);
            }
        }

        // Add spacing between subagents
        tui.addConversationLine(null, "", NCURSES_PAIR_FOREGROUND);
    }

    // Add footer
    tui.addConversationLine(null, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━", NCURSES_PAIR_TOOL);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "WindowManager: basic initialization" {
    // This test would require ncurses initialization
    // For now, just test the struct layout
    const wm = WindowManager{
        .config = DEFAULT_WINDOW_CONFIG,
    };

    try std.testing.expectEqual(@as(i32, 5), wm.config.min_conv_height);
    try std.testing.expectEqual(@as(i32, 2), wm.config.min_input_height);
}

test "TUIState: mode enumeration" {
    try std.testing.expectEqual(@as(usize, 0), @intFromEnum(TUIMode.normal));
    try std.testing.expectEqual(@as(usize, 1), @intFromEnum(TUIMode.insert));
    try std.testing.expectEqual(@as(usize, 2), @intFromEnum(TUIMode.command));
}

test "TUIState: input box style" {
    try std.testing.expectEqual(@as(usize, 0), @intFromEnum(TUIInputBoxStyle.background));
    try std.testing.expectEqual(@as(usize, 1), @intFromEnum(TUIInputBoxStyle.border));
    try std.testing.expectEqual(@as(usize, 2), @intFromEnum(TUIInputBoxStyle.horizontal));
    try std.testing.expectEqual(@as(usize, 3), @intFromEnum(TUIInputBoxStyle.bland));
}

test "rgbToNcurses: conversion" {
    try std.testing.expectEqual(@as(i16, 0), rgbToNcurses(0));
    try std.testing.expectEqual(@as(i16, 1000), rgbToNcurses(255));
    try std.testing.expectEqual(@as(i16, 498), rgbToNcurses(127)); // 127 * 1000 / 255 = 498 (integer division)
}

test "TodoList: rendering structure" {
    const items = [_]TodoItem{
        .{ .content = "Task 1", .active_form = "Working on task 1", .status = .pending },
        .{ .content = "Task 2", .active_form = "Working on task 2", .status = .in_progress },
        .{ .content = "Task 3", .active_form = "Working on task 3", .status = .completed },
    };
    const list = TodoList{ .items = &items, .count = 3 };

    try std.testing.expectEqual(@as(usize, 3), list.count);
    try std.testing.expectEqual(TodoStatus.pending, list.items[0].status);
    try std.testing.expectEqual(TodoStatus.in_progress, list.items[1].status);
    try std.testing.expectEqual(TodoStatus.completed, list.items[2].status);
}

test "SubagentManager: counting" {
    var processes = [_]SubagentProcess{
        .{ .pid = 1, .prompt = "test1", .log_file = null, .last_log_tail = null, .completed = false },
        .{ .pid = 2, .prompt = "test2", .log_file = null, .last_log_tail = null, .completed = true },
        .{ .pid = 3, .prompt = "test3", .log_file = null, .last_log_tail = null, .completed = false },
    };
    const manager = SubagentManager{
        .processes = &processes,
        .process_count = 3,
    };

    try std.testing.expectEqual(@as(i32, 2), manager.getRunningCount());
}
