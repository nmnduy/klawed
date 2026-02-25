//! TUI Modes module
//!
//! Idiomatic Zig replacement for src/tui_modes.c and src/tui_modes.h
//!
//! Provides TUI mode handling including:
//! - Mode switching (normal, insert, command, search)
//! - Mode-specific key handling
//! - Command buffer management
//! - Search buffer management

const std = @import("std");
const logger = @import("../logger.zig");

// ncurses FFI
const c = @cImport({
    @cInclude("ncurses.h");
});

// Import related modules
const core = @import("core.zig");

// ---------------------------------------------------------------------------
// Mode State
// ---------------------------------------------------------------------------

pub const ModeState = struct {
    allocator: std.mem.Allocator,
    current_mode: core.TUIMode,

    // Command mode state
    command_buffer: std.ArrayList(u8),
    command_history: std.ArrayList([]const u8),

    // Search mode state
    search_buffer: std.ArrayList(u8),
    search_direction: i32, // 1 for forward, -1 for backward
    last_search_pattern: ?[]const u8,

    // Normal mode state
    normal_last_key: i32,
    normal_cursor_line: i32,
    normal_cursor_col: i32,

    /// Initialize mode state
    pub fn init(allocator: std.mem.Allocator) ModeState {
        return ModeState{
            .allocator = allocator,
            .current_mode = core.TUIMode.normal,
            .command_buffer = std.ArrayList(u8).init(allocator),
            .command_history = std.ArrayList([]const u8).init(allocator),
            .search_buffer = std.ArrayList(u8).init(allocator),
            .search_direction = 1,
            .last_search_pattern = null,
            .normal_last_key = 0,
            .normal_cursor_line = 0,
            .normal_cursor_col = 0,
        };
    }

    /// Deinitialize mode state
    pub fn deinit(self: *ModeState) void {
        self.command_buffer.deinit();

        for (self.command_history.items) |item| {
            self.allocator.free(item);
        }
        self.command_history.deinit();

        self.search_buffer.deinit();

        if (self.last_search_pattern) |pattern| {
            self.allocator.free(pattern);
        }
    }

    /// Get current mode
    pub fn getMode(self: ModeState) core.TUIMode {
        return self.current_mode;
    }

    /// Set current mode
    pub fn setMode(self: *ModeState, mode: core.TUIMode) void {
        logger.defaultLogger.log(.debug, "[TUI] Mode change: {s} -> {s}", .{
            @tagName(self.current_mode),
            @tagName(mode),
        });
        self.current_mode = mode;
    }

    // -----------------------------------------------------------------------
    // Command Mode
    // -----------------------------------------------------------------------

    /// Enter command mode (called when ':' is pressed in normal mode)
    pub fn enterCommandMode(self: *ModeState) void {
        self.command_buffer.clearRetainingCapacity();
        self.setMode(core.TUIMode.command);
    }

    /// Add character to command buffer
    pub fn commandAddChar(self: *ModeState, ch: u8) !void {
        try self.command_buffer.append(ch);
    }

    /// Remove last character from command buffer
    pub fn commandBackspace(self: *ModeState) void {
        if (self.command_buffer.items.len > 0) {
            _ = self.command_buffer.pop();
        }
    }

    /// Get command buffer content
    pub fn getCommandBuffer(self: ModeState) []const u8 {
        return self.command_buffer.items;
    }

    /// Clear command buffer
    pub fn clearCommandBuffer(self: *ModeState) void {
        self.command_buffer.clearRetainingCapacity();
    }

    /// Execute command and return to normal mode
    pub fn executeCommand(self: *ModeState) !?[]const u8 {
        const command = try self.allocator.dupe(u8, self.command_buffer.items);
        errdefer self.allocator.free(command);

        // Save to history
        try self.command_history.append(command);

        self.setMode(core.TUIMode.normal);
        self.command_buffer.clearRetainingCapacity();

        return command;
    }

    // -----------------------------------------------------------------------
    // Search Mode
    // -----------------------------------------------------------------------

    /// Enter search mode
    pub fn enterSearchMode(self: *ModeState, direction: i32) void {
        self.search_buffer.clearRetainingCapacity();
        self.search_direction = direction;
        self.setMode(core.TUIMode.search);
    }

    /// Add character to search buffer
    pub fn searchAddChar(self: *ModeState, ch: u8) !void {
        try self.search_buffer.append(ch);
    }

    /// Remove last character from search buffer
    pub fn searchBackspace(self: *ModeState) void {
        if (self.search_buffer.items.len > 0) {
            _ = self.search_buffer.pop();
        }
    }

    /// Get search buffer content
    pub fn getSearchBuffer(self: ModeState) []const u8 {
        return self.search_buffer.items;
    }

    /// Execute search and return to normal mode
    pub fn executeSearch(self: *ModeState) !?[]const u8 {
        if (self.search_buffer.items.len == 0) {
            self.setMode(core.TUIMode.normal);
            return null;
        }

        // Save last search pattern
        if (self.last_search_pattern) |old| {
            self.allocator.free(old);
        }
        self.last_search_pattern = try self.allocator.dupe(u8, self.search_buffer.items);

        const pattern = try self.allocator.dupe(u8, self.search_buffer.items);

        self.setMode(core.TUIMode.normal);
        self.search_buffer.clearRetainingCapacity();

        return pattern;
    }

    /// Get last search pattern
    pub fn getLastSearchPattern(self: ModeState) ?[]const u8 {
        return self.last_search_pattern;
    }

    /// Cancel search and return to normal mode
    pub fn cancelSearch(self: *ModeState) void {
        self.setMode(core.TUIMode.normal);
        self.search_buffer.clearRetainingCapacity();
    }

    // -----------------------------------------------------------------------
    // Normal Mode Navigation
    // -----------------------------------------------------------------------

    /// Set the last key pressed in normal mode (for multi-key sequences like 'gg')
    pub fn setNormalLastKey(self: *ModeState, key: i32) void {
        self.normal_last_key = key;
    }

    /// Get the last key pressed in normal mode
    pub fn getNormalLastKey(self: ModeState) i32 {
        return self.normal_last_key;
    }

    /// Set cursor position
    pub fn setCursorPosition(self: *ModeState, line: i32, col: i32) void {
        self.normal_cursor_line = line;
        self.normal_cursor_col = col;
    }

    /// Get cursor position
    pub fn getCursorPosition(self: ModeState) struct { line: i32, col: i32 } {
        return .{
            .line = self.normal_cursor_line,
            .col = self.normal_cursor_col,
        };
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "ModeState: mode switching" {
    var ms = ModeState.init(std.testing.allocator);
    defer ms.deinit();

    try std.testing.expectEqual(core.TUIMode.normal, ms.getMode());

    ms.setMode(core.TUIMode.insert);
    try std.testing.expectEqual(core.TUIMode.insert, ms.getMode());

    ms.setMode(core.TUIMode.command);
    try std.testing.expectEqual(core.TUIMode.command, ms.getMode());
}

test "ModeState: command buffer" {
    var ms = ModeState.init(std.testing.allocator);
    defer ms.deinit();

    ms.enterCommandMode();
    try ms.commandAddChar('q');
    try ms.commandAddChar('u');
    try ms.commandAddChar('i');
    try ms.commandAddChar('t');

    try std.testing.expectEqualStrings("quit", ms.getCommandBuffer());

    ms.commandBackspace();
    try std.testing.expectEqualStrings("qui", ms.getCommandBuffer());

    const cmd = try ms.executeCommand();
    defer if (cmd) |c| std.testing.allocator.free(c);

    try std.testing.expectEqual(core.TUIMode.normal, ms.getMode());
    try std.testing.expectEqualStrings("", ms.getCommandBuffer());
}

test "ModeState: search buffer" {
    var ms = ModeState.init(std.testing.allocator);
    defer ms.deinit();

    ms.enterSearchMode(1);
    try ms.searchAddChar('t');
    try ms.searchAddChar('e');
    try ms.searchAddChar('s');
    try ms.searchAddChar('t');

    try std.testing.expectEqualStrings("test", ms.getSearchBuffer());

    const pattern = try ms.executeSearch();
    defer if (pattern) |p| std.testing.allocator.free(p);

    try std.testing.expectEqual(core.TUIMode.normal, ms.getMode());
    try std.testing.expectEqualStrings("test", pattern.?);
    try std.testing.expectEqualStrings("test", ms.getLastSearchPattern().?);
}
