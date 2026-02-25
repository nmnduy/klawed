//! Help Modal module
//!
//! Idiomatic Zig replacement for src/help_modal.c and src/help_modal.h
//!
//! Provides a help modal overlay for displaying keyboard shortcuts.

const std = @import("std");
const logger = @import("../logger.zig");

// ncurses FFI
const c = @cImport({
    @cInclude("ncurses.h");
});

// ---------------------------------------------------------------------------
// Help Modal State
// ---------------------------------------------------------------------------

pub const HelpModalState = struct {
    allocator: std.mem.Allocator,
    active: bool,
    window: ?*c.WINDOW = null,
    width: i32 = 60,
    height: i32 = 20,

    /// Initialize help modal state
    pub fn init(allocator: std.mem.Allocator) HelpModalState {
        return HelpModalState{
            .allocator = allocator,
            .active = false,
        };
    }

    /// Deinitialize help modal state
    pub fn deinit(self: *HelpModalState) void {
        self.hide();
    }

    /// Show help modal
    pub fn show(self: *HelpModalState) !void {
        if (self.active) return;

        // Get screen dimensions
        var max_y: i32 = 0;
        var max_x: i32 = 0;
        _ = c.getmaxyx(c.stdscr, &max_y, &max_x);

        // Center window
        const start_y = @divTrunc(max_y - self.height, 2);
        const start_x = @divTrunc(max_x - self.width, 2);

        // Create window
        self.window = c.newwin(self.height, self.width, start_y, start_x);
        if (self.window == null) {
            return error.WindowCreationFailed;
        }

        self.draw();
        self.active = true;
    }

    /// Hide help modal
    pub fn hide(self: *HelpModalState) void {
        if (!self.active) return;

        if (self.window) |win| {
            _ = c.delwin(win);
            self.window = null;
        }

        self.active = false;

        // Redraw screen
        _ = c.touchwin(c.stdscr);
        _ = c.refresh();
    }

    /// Toggle help modal visibility
    pub fn toggle(self: *HelpModalState) !void {
        if (self.active) {
            self.hide();
        } else {
            try self.show();
        }
    }

    /// Check if modal is active
    pub fn isActive(self: HelpModalState) bool {
        return self.active;
    }

    /// Draw help content
    fn draw(self: HelpModalState) void {
        if (self.window == null) return;

        const win = self.window.?;

        // Draw border
        _ = c.box(win, 0, 0);

        // Title
        _ = c.wattron(win, c.A_BOLD | c.A_REVERSE);
        _ = c.mvwprintw(win, 0, 2, " Keyboard Shortcuts ");
        _ = c.wattroff(win, c.A_BOLD | c.A_REVERSE);

        // Help content
        const help_text = [_][]const u8{
            "General:",
            "  Ctrl+C     Interrupt",
            "  Ctrl+D     Exit",
            "  ?          Toggle help",
            "",
            "Navigation:",
            "  j/↓        Down",
            "  k/↑        Up",
            "  gg         Go to top",
            "  G          Go to bottom",
            "",
            "Modes:",
            "  i          Insert mode",
            "  Esc        Normal mode",
            "  :          Command mode",
            "  /          Search mode",
            "",
            "Press any key to close",
        };

        var row: i32 = 2;
        for (help_text) |line| {
            _ = c.mvwprintw(win, row, 2, "%s", line.ptr);
            row += 1;
        }

        _ = c.wrefresh(win);
    }

    /// Handle input (returns true if modal should close)
    pub fn handleInput(self: *HelpModalState, ch: i32) bool {
        _ = self;
        _ = ch;
        // Any key closes the modal
        return true;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "HelpModalState: basic operations" {
    var hm = HelpModalState.init(std.testing.allocator);
    defer hm.deinit();

    try std.testing.expect(!hm.isActive());

    // Note: Cannot actually show modal in test without ncurses
    // Just verify the state tracking
    hm.active = true;
    try std.testing.expect(hm.isActive());

    hm.active = false;
    try std.testing.expect(!hm.isActive());
}
