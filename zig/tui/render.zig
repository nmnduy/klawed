//! TUI Rendering module
//!
//! Idiomatic Zig replacement for src/tui_render.c and src/tui_render.h
//!
//! Provides rendering functions for TUI components including:
//! - Status window rendering
//! - Input window rendering
//! - Conversation pad rendering
//! - Color pair management

const std = @import("std");
const logger = @import("../logger.zig");

// ncurses FFI
const c = @cImport({
    @cInclude("ncurses.h");
});

// Import related modules
const colorscheme = @import("colorscheme.zig");
const core = @import("core.zig");

// ---------------------------------------------------------------------------
// Render State
// ---------------------------------------------------------------------------

pub const RenderState = struct {
    allocator: std.mem.Allocator,
    colorscheme_manager: *colorscheme.ColorschemeManager,

    /// Initialize render state
    pub fn init(allocator: std.mem.Allocator, cm: *colorscheme.ColorschemeManager) RenderState {
        return RenderState{
            .allocator = allocator,
            .colorscheme_manager = cm,
        };
    }

    // -----------------------------------------------------------------------
    // Status Window Rendering
    // -----------------------------------------------------------------------

    /// Render the status window
    pub fn renderStatusWindow(self: *RenderState, win: *c.WINDOW, status_text: []const u8) void {
        _ = c.werase(win);

        // Get status color
        var buf: [32]u8 = undefined;
        const color_code = self.colorscheme_manager.getColorCode(.status, &buf) orelse "\x1b[36m";
        _ = color_code;

        // Draw status text
        _ = c.mvwprintw(win, 0, 1, "%s", status_text.ptr);

        _ = c.wrefresh(win);
    }

    // -----------------------------------------------------------------------
    // Input Window Rendering
    // -----------------------------------------------------------------------

    /// Render the input window with prompt
    pub fn renderInputWindow(self: *RenderState, win: *c.WINDOW, prompt: []const u8, input_text: []const u8, cursor_pos: usize) void {
        _ = self;
        _ = c.werase(win);

        // Draw prompt and input
        _ = c.mvwprintw(win, 0, 1, "%s %s", prompt.ptr, input_text.ptr);

        // Position cursor
        const prompt_len = @as(i32, @intCast(prompt.len));
        const cursor_col = @as(i32, @intCast(cursor_pos));
        _ = c.wmove(win, 0, 1 + prompt_len + 1 + cursor_col);

        _ = c.wrefresh(win);
    }

    // -----------------------------------------------------------------------
    // Conversation Rendering
    // -----------------------------------------------------------------------

    /// Render a conversation entry to a pad
    pub fn renderEntryToPad(
        self: *RenderState,
        pad: *c.WINDOW,
        y: i32,
        prefix: ?[]const u8,
        text: []const u8,
        color_pair: core.ColorPairNum,
    ) i32 {
        _ = self;
        _ = color_pair;

        var current_y = y;

        // Render prefix if provided
        if (prefix) |p| {
            _ = c.mvwprintw(pad, current_y, 0, "%s", p.ptr);
            current_y += 1;
        }

        // Render text (simple implementation - just print the text)
        _ = c.mvwprintw(pad, current_y, 2, "%s", text.ptr);
        current_y += 1;

        return current_y;
    }

    // -----------------------------------------------------------------------
    // Color Pair Management
    // -----------------------------------------------------------------------

    /// Initialize ncurses color pairs
    pub fn initColorPairs(self: *RenderState) void {
        // Initialize color pairs from the colorscheme
        const theme = self.colorscheme_manager.getTheme();

        // Foreground pair
        _ = c.init_pair(core.NCURSES_PAIR_FOREGROUND, @intCast(theme.foreground_rgb.to256ColorIndex()), -1);

        // User pair (green)
        _ = c.init_pair(core.NCURSES_PAIR_USER, @intCast(theme.user_rgb.to256ColorIndex()), -1);

        // Assistant pair (cyan)
        _ = c.init_pair(core.NCURSES_PAIR_ASSISTANT, @intCast(theme.assistant_rgb.to256ColorIndex()), -1);

        // Status pair (yellow)
        _ = c.init_pair(core.NCURSES_PAIR_STATUS, @intCast(theme.status_rgb.to256ColorIndex()), -1);

        // Error pair (red)
        _ = c.init_pair(core.NCURSES_PAIR_ERROR, @intCast(theme.error_rgb.to256ColorIndex()), -1);

        // Prompt pair (green)
        _ = c.init_pair(core.NCURSES_PAIR_PROMPT, @intCast(theme.user_rgb.to256ColorIndex()), -1);

        // Tool pair (bright blue)
        _ = c.init_pair(core.NCURSES_PAIR_TOOL, @intCast(theme.tool_rgb.to256ColorIndex()), -1);

        // Search pair (magenta)
        _ = c.init_pair(core.NCURSES_PAIR_SEARCH, @intCast(theme.search_rgb.to256ColorIndex()), -1);
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "RenderState: basic initialization" {
    var cm = colorscheme.ColorschemeManager.init(std.testing.allocator);
    const rs = RenderState.init(std.testing.allocator, &cm);

    try std.testing.expectEqual(@as(usize, @intFromPtr(&cm)), @as(usize, @intFromPtr(rs.colorscheme_manager)));
}
