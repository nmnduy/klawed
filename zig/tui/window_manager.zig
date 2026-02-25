//! Window Manager module
//!
//! Idiomatic Zig replacement for src/window_manager.c and src/window_manager.h
//!
//! Provides window management including:
//! - Conversation window/pad management
//! - Status window management
//! - Input window management
//! - Window resizing and layout

const std = @import("std");
const logger = @import("../logger.zig");

// ncurses FFI
const c = @cImport({
    @cInclude("ncurses.h");
});

// ---------------------------------------------------------------------------
// Window Configuration
// ---------------------------------------------------------------------------

pub const WindowConfig = struct {
    min_conv_height: i32 = 5,
    min_input_height: i32 = 2,
    max_input_height: i32 = 5,
    status_height: i32 = 1,
};

// ---------------------------------------------------------------------------
// Window Manager State
// ---------------------------------------------------------------------------

pub const WindowManager = struct {
    allocator: std.mem.Allocator,
    config: WindowConfig,

    // Windows
    stdscr: ?*c.WINDOW = null,
    conv_win: ?*c.WINDOW = null,
    status_win: ?*c.WINDOW = null,
    input_win: ?*c.WINDOW = null,

    // Dimensions
    screen_height: i32 = 0,
    screen_width: i32 = 0,
    conv_height: i32 = 0,
    conv_width: i32 = 0,
    status_y: i32 = 0,
    input_y: i32 = 0,

    /// Initialize window manager
    pub fn init(allocator: std.mem.Allocator, config: WindowConfig) WindowManager {
        return WindowManager{
            .allocator = allocator,
            .config = config,
        };
    }

    /// Deinitialize window manager
    pub fn deinit(self: *WindowManager) void {
        self.cleanupWindows();
    }

    /// Initialize ncurses and create windows
    pub fn setup(self: *WindowManager) !void {
        // Initialize ncurses
        self.stdscr = c.initscr();
        if (self.stdscr == null) {
            return error.NcursesInitFailed;
        }

        // Enable features
        _ = c.cbreak();
        _ = c.noecho();
        _ = c.keypad(self.stdscr, true);
        _ = c.curs_set(1);

        // Get screen dimensions
        self.updateDimensions();

        // Create windows
        try self.createWindows();
    }

    /// Clean up ncurses windows
    pub fn cleanupWindows(self: *WindowManager) void {
        if (self.input_win) |win| {
            _ = c.delwin(win);
            self.input_win = null;
        }
        if (self.status_win) |win| {
            _ = c.delwin(win);
            self.status_win = null;
        }
        if (self.conv_win) |win| {
            _ = c.delwin(win);
            self.conv_win = null;
        }
        if (self.stdscr) |_| {
            _ = c.endwin();
            self.stdscr = null;
        }
    }

    /// Update screen dimensions
    pub fn updateDimensions(self: *WindowManager) void {
        var max_y: i32 = 0;
        var max_x: i32 = 0;
        _ = c.getmaxyx(self.stdscr, &max_y, &max_x);

        self.screen_height = max_y;
        self.screen_width = max_x;

        // Calculate window heights
        self.conv_height = max_y - self.config.status_height - self.config.min_input_height;
        self.conv_width = max_x;

        self.status_y = self.conv_height;
        self.input_y = self.status_y + self.config.status_height;
    }

    /// Create/Recreate windows based on current dimensions
    pub fn createWindows(self: *WindowManager) !void {
        // Clean up existing windows
        if (self.input_win) |win| {
            _ = c.delwin(win);
        }
        if (self.status_win) |win| {
            _ = c.delwin(win);
        }
        if (self.conv_win) |win| {
            _ = c.delwin(win);
        }

        // Create conversation window (pad for scrolling)
        self.conv_win = c.newpad(self.conv_height * 10, self.conv_width);
        if (self.conv_win == null) {
            return error.WindowCreationFailed;
        }

        // Create status window
        self.status_win = c.newwin(self.config.status_height, self.screen_width, self.status_y, 0);
        if (self.status_win == null) {
            return error.WindowCreationFailed;
        }

        // Create input window
        self.input_win = c.newwin(self.config.min_input_height, self.screen_width, self.input_y, 0);
        if (self.input_win == null) {
            return error.WindowCreationFailed;
        }
    }

    /// Handle terminal resize
    pub fn handleResize(self: *WindowManager) !void {
        self.updateDimensions();
        try self.createWindows();
    }

    /// Refresh all windows
    pub fn refreshAll(self: *WindowManager) void {
        _ = c.refresh();
        if (self.status_win) |win| {
            _ = c.wrefresh(win);
        }
        if (self.input_win) |win| {
            _ = c.wrefresh(win);
        }
    }

    /// Get conversation window
    pub fn getConvWin(self: WindowManager) ?*c.WINDOW {
        return self.conv_win;
    }

    /// Get status window
    pub fn getStatusWin(self: WindowManager) ?*c.WINDOW {
        return self.status_win;
    }

    /// Get input window
    pub fn getInputWin(self: WindowManager) ?*c.WINDOW {
        return self.input_win;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "WindowManager: initialization" {
    const config = WindowConfig{};
    const wm = WindowManager.init(std.testing.allocator, config);

    try std.testing.expectEqual(@as(i32, 5), wm.config.min_conv_height);
    try std.testing.expectEqual(@as(i32, 2), wm.config.min_input_height);
}
