//! File Search module
//!
//! Idiomatic Zig replacement for src/file_search.c and src/file_search.h
//!
//! Provides a fuzzy file finder popup for selecting files.

const std = @import("std");
const logger = @import("../logger.zig");

// ncurses FFI
const c = @cImport({
    @cInclude("ncurses.h");
});

// ---------------------------------------------------------------------------
// File Search State
// ---------------------------------------------------------------------------

pub const FileSearchState = struct {
    allocator: std.mem.Allocator,
    active: bool,
    query: std.ArrayList(u8),
    files: std.ArrayList([]const u8),
    filtered: std.ArrayList(usize), // Indices into files
    selected_index: usize,
    scroll_offset: usize,

    /// Initialize file search state
    pub fn init(allocator: std.mem.Allocator) FileSearchState {
        return FileSearchState{
            .allocator = allocator,
            .active = false,
            .query = std.ArrayList(u8).init(allocator),
            .files = std.ArrayList([]const u8).init(allocator),
            .filtered = std.ArrayList(usize).init(allocator),
            .selected_index = 0,
            .scroll_offset = 0,
        };
    }

    /// Deinitialize file search state
    pub fn deinit(self: *FileSearchState) void {
        self.query.deinit();

        for (self.files.items) |file| {
            self.allocator.free(file);
        }
        self.files.deinit();

        self.filtered.deinit();
    }

    /// Activate file search
    pub fn activate(self: *FileSearchState) void {
        self.active = true;
        self.query.clearRetainingCapacity();
        self.selected_index = 0;
        self.scroll_offset = 0;
    }

    /// Deactivate file search
    pub fn deactivate(self: *FileSearchState) void {
        self.active = false;
    }

    /// Check if file search is active
    pub fn isActive(self: FileSearchState) bool {
        return self.active;
    }

    /// Add character to query
    pub fn addQueryChar(self: *FileSearchState, ch: u8) !void {
        try self.query.append(ch);
        try self.filterFiles();
    }

    /// Remove last character from query
    pub fn backspaceQuery(self: *FileSearchState) void {
        if (self.query.items.len > 0) {
            _ = self.query.pop();
            self.filterFiles() catch {};
        }
    }

    /// Clear query
    pub fn clearQuery(self: *FileSearchState) void {
        self.query.clearRetainingCapacity();
        self.filterFiles() catch {};
    }

    /// Filter files based on current query
    fn filterFiles(self: *FileSearchState) !void {
        self.filtered.clearRetainingCapacity();

        const query = self.query.items;
        if (query.len == 0) {
            // Show all files
            for (0..self.files.items.len) |i| {
                try self.filtered.append(i);
            }
            return;
        }

        // Simple substring filter (can be enhanced with fuzzy matching)
        for (self.files.items, 0..) |file, i| {
            if (std.ascii.indexOfIgnoreCase(file, query) != null) {
                try self.filtered.append(i);
            }
        }

        // Reset selection if out of bounds
        if (self.selected_index >= self.filtered.items.len) {
            self.selected_index = if (self.filtered.items.len > 0) self.filtered.items.len - 1 else 0;
        }
    }

    /// Load files from directory
    pub fn loadFiles(self: *FileSearchState, dir_path: []const u8) !void {
        // Clear existing files
        for (self.files.items) |file| {
            self.allocator.free(file);
        }
        self.files.clearRetainingCapacity();

        // Open directory
        var dir = try std.fs.cwd().openDir(dir_path, .{ .iterate = true });
        defer dir.close();

        // Iterate and collect files
        var iter = dir.iterate();
        while (try iter.next()) |entry| {
            if (entry.kind == .file) {
                const file_copy = try self.allocator.dupe(u8, entry.name);
                try self.files.append(file_copy);
            }
        }

        // Initial filter
        try self.filterFiles();
    }

    /// Move selection down
    pub fn moveDown(self: *FileSearchState) void {
        if (self.selected_index + 1 < self.filtered.items.len) {
            self.selected_index += 1;
        }
    }

    /// Move selection up
    pub fn moveUp(self: *FileSearchState) void {
        if (self.selected_index > 0) {
            self.selected_index -= 1;
        }
    }

    /// Get selected file
    pub fn getSelected(self: FileSearchState) ?[]const u8 {
        if (self.filtered.items.len == 0) return null;
        if (self.selected_index >= self.filtered.items.len) return null;

        const file_idx = self.filtered.items[self.selected_index];
        return self.files.items[file_idx];
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "FileSearchState: query management" {
    var fs = FileSearchState.init(std.testing.allocator);
    defer fs.deinit();

    fs.activate();
    try std.testing.expect(fs.isActive());

    try fs.addQueryChar('t');
    try fs.addQueryChar('e');
    try fs.addQueryChar('s');
    try std.testing.expectEqualStrings("tes", fs.query.items);

    fs.backspaceQuery();
    try std.testing.expectEqualStrings("te", fs.query.items);

    fs.clearQuery();
    try std.testing.expectEqualStrings("", fs.query.items);
}

test "FileSearchState: selection" {
    var fs = FileSearchState.init(std.testing.allocator);
    defer fs.deinit();

    // Add some test files
    try fs.files.append(try std.testing.allocator.dupe(u8, "file1.txt"));
    try fs.files.append(try std.testing.allocator.dupe(u8, "file2.txt"));
    try fs.files.append(try std.testing.allocator.dupe(u8, "other.txt"));

    // Filter should show all initially
    try fs.filterFiles();
    try std.testing.expectEqual(@as(usize, 3), fs.filtered.items.len);

    // Move selection
    fs.moveDown();
    try std.testing.expectEqual(@as(usize, 1), fs.selected_index);

    fs.moveUp();
    try std.testing.expectEqual(@as(usize, 0), fs.selected_index);
}
