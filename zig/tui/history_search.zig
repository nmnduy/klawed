//! History Search module
//!
//! Idiomatic Zig replacement for src/history_search.c and src/history_search.h
//!
//! Provides a search interface for command history.

const std = @import("std");
const logger = @import("../logger.zig");

// ncurses FFI
const c = @cImport({
    @cInclude("ncurses.h");
});

// ---------------------------------------------------------------------------
// History Search State
// ---------------------------------------------------------------------------

pub const HistorySearchState = struct {
    allocator: std.mem.Allocator,
    active: bool,
    query: std.ArrayList(u8),
    history: std.ArrayList([]const u8),
    filtered: std.ArrayList(usize),
    selected_index: usize,

    /// Initialize history search state
    pub fn init(allocator: std.mem.Allocator) HistorySearchState {
        return HistorySearchState{
            .allocator = allocator,
            .active = false,
            .query = std.ArrayList(u8).init(allocator),
            .history = std.ArrayList([]const u8).init(allocator),
            .filtered = std.ArrayList(usize).init(allocator),
            .selected_index = 0,
        };
    }

    /// Deinitialize history search state
    pub fn deinit(self: *HistorySearchState) void {
        self.query.deinit();

        for (self.history.items) |item| {
            self.allocator.free(item);
        }
        self.history.deinit();

        self.filtered.deinit();
    }

    /// Activate history search
    pub fn activate(self: *HistorySearchState) void {
        self.active = true;
        self.query.clearRetainingCapacity();
        self.selected_index = 0;
    }

    /// Deactivate history search
    pub fn deactivate(self: *HistorySearchState) void {
        self.active = false;
    }

    /// Check if history search is active
    pub fn isActive(self: HistorySearchState) bool {
        return self.active;
    }

    /// Add character to query
    pub fn addQueryChar(self: *HistorySearchState, ch: u8) !void {
        try self.query.append(ch);
        try self.filterHistory();
    }

    /// Remove last character from query
    pub fn backspaceQuery(self: *HistorySearchState) void {
        if (self.query.items.len > 0) {
            _ = self.query.pop();
            self.filterHistory() catch {};
        }
    }

    /// Clear query
    pub fn clearQuery(self: *HistorySearchState) void {
        self.query.clearRetainingCapacity();
        self.filterHistory() catch {};
    }

    /// Add history entry
    pub fn addHistoryEntry(self: *HistorySearchState, entry: []const u8) !void {
        const copy = try self.allocator.dupe(u8, entry);
        try self.history.append(copy);
    }

    /// Filter history based on current query
    fn filterHistory(self: *HistorySearchState) !void {
        self.filtered.clearRetainingCapacity();

        const query = self.query.items;
        if (query.len == 0) {
            // Show all history (most recent first)
            var i = self.history.items.len;
            while (i > 0) {
                i -= 1;
                try self.filtered.append(i);
            }
            return;
        }

        // Substring filter
        var i = self.history.items.len;
        while (i > 0) {
            i -= 1;
            if (std.ascii.indexOfIgnoreCase(self.history.items[i], query) != null) {
                try self.filtered.append(i);
            }
        }

        // Reset selection if out of bounds
        if (self.selected_index >= self.filtered.items.len) {
            self.selected_index = if (self.filtered.items.len > 0) self.filtered.items.len - 1 else 0;
        }
    }

    /// Move selection down
    pub fn moveDown(self: *HistorySearchState) void {
        if (self.selected_index + 1 < self.filtered.items.len) {
            self.selected_index += 1;
        }
    }

    /// Move selection up
    pub fn moveUp(self: *HistorySearchState) void {
        if (self.selected_index > 0) {
            self.selected_index -= 1;
        }
    }

    /// Get selected history entry
    pub fn getSelected(self: HistorySearchState) ?[]const u8 {
        if (self.filtered.items.len == 0) return null;
        if (self.selected_index >= self.filtered.items.len) return null;

        const history_idx = self.filtered.items[self.selected_index];
        return self.history.items[history_idx];
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "HistorySearchState: basic operations" {
    var hs = HistorySearchState.init(std.testing.allocator);
    defer hs.deinit();

    hs.activate();
    try std.testing.expect(hs.isActive());

    // Add history entries
    try hs.addHistoryEntry("command one");
    try hs.addHistoryEntry("command two");
    try hs.addHistoryEntry("other command");

    try hs.filterHistory();
    try std.testing.expectEqual(@as(usize, 3), hs.filtered.items.len);

    // Search
    try hs.addQueryChar('o');
    try hs.addQueryChar('n');
    try hs.addQueryChar('e');

    try std.testing.expectEqualStrings("one", hs.query.items);
}
