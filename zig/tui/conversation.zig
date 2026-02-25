//! TUI Conversation Display module
//!
//! Idiomatic Zig replacement for src/tui_conversation.c and src/tui_conversation.h
//!
//! Provides conversation display functionality including:
//! - Conversation entry management
//! - Scrollable conversation pad
//! - Message rendering with colors

const std = @import("std");
const logger = @import("../logger.zig");

// ncurses FFI
const c = @cImport({
    @cInclude("ncurses.h");
});

// Import related modules
const colorscheme = @import("colorscheme.zig");
const core = @import("core.zig");
const render = @import("render.zig");

// ---------------------------------------------------------------------------
// Conversation Entry
// ---------------------------------------------------------------------------

pub const ConversationEntry = struct {
    prefix: ?[]const u8, // Role prefix (e.g., "[User]", "[Assistant]")
    text: []const u8, // Message text
    color_pair: core.ColorPairNum, // Color for display

    /// Initialize a new conversation entry
    pub fn init(prefix: ?[]const u8, text: []const u8, color: core.ColorPairNum) ConversationEntry {
        return ConversationEntry{
            .prefix = prefix,
            .text = text,
            .color_pair = color,
        };
    }
};

// ---------------------------------------------------------------------------
// Conversation State
// ---------------------------------------------------------------------------

pub const ConversationState = struct {
    allocator: std.mem.Allocator,
    entries: std.ArrayList(ConversationEntry),
    pad: ?*c.WINDOW = null,
    pad_height: i32 = 1000, // Initial pad height
    pad_width: i32 = 80, // Initial pad width

    /// Initialize conversation state
    pub fn init(allocator: std.mem.Allocator) ConversationState {
        return ConversationState{
            .allocator = allocator,
            .entries = std.ArrayList(ConversationEntry).init(allocator),
        };
    }

    /// Deinitialize conversation state
    pub fn deinit(self: *ConversationState) void {
        // Free all entry text
        for (self.entries.items) |entry| {
            if (entry.prefix) |p| {
                self.allocator.free(p);
            }
            self.allocator.free(entry.text);
        }
        self.entries.deinit();

        // Delete pad if created
        if (self.pad) |pad| {
            _ = c.delwin(pad);
            self.pad = null;
        }
    }

    /// Create the conversation pad
    pub fn createPad(self: *ConversationState) !void {
        if (self.pad != null) return;

        self.pad = c.newpad(self.pad_height, self.pad_width);
        if (self.pad == null) {
            return error.PadCreationFailed;
        }
    }

    /// Add a conversation entry
    pub fn addEntry(self: *ConversationState, prefix: ?[]const u8, text: []const u8, color: core.ColorPairNum) !void {
        // Duplicate the strings
        const prefix_copy = if (prefix) |p| try self.allocator.dupe(u8, p) else null;
        errdefer if (prefix_copy) |p| self.allocator.free(p);

        const text_copy = try self.allocator.dupe(u8, text);
        errdefer self.allocator.free(text_copy);

        const entry = ConversationEntry{
            .prefix = prefix_copy,
            .text = text_copy,
            .color_pair = color,
        };

        try self.entries.append(entry);
    }

    /// Update the last conversation entry
    pub fn updateLastEntry(self: *ConversationState, text: []const u8) !void {
        if (self.entries.items.len == 0) return;

        const last_idx = self.entries.items.len - 1;
        const old_text = self.entries.items[last_idx].text;

        const new_text = try self.allocator.dupe(u8, text);
        self.entries.items[last_idx].text = new_text;

        self.allocator.free(old_text);
    }

    /// Clear all entries
    pub fn clear(self: *ConversationState) void {
        for (self.entries.items) |entry| {
            if (entry.prefix) |p| {
                self.allocator.free(p);
            }
            self.allocator.free(entry.text);
        }
        self.entries.clearRetainingCapacity();
    }

    /// Get entry count
    pub fn getEntryCount(self: ConversationState) usize {
        return self.entries.items.len;
    }

    /// Get an entry by index
    pub fn getEntry(self: ConversationState, index: usize) ?ConversationEntry {
        if (index >= self.entries.items.len) return null;
        return self.entries.items[index];
    }

    /// Render all entries to the pad
    pub fn renderToPad(self: *ConversationState, renderer: *render.RenderState) !void {
        if (self.pad == null) {
            try self.createPad();
        }

        const pad = self.pad.?;
        _ = c.werase(pad);

        var y: i32 = 0;
        for (self.entries.items) |entry| {
            y = renderer.renderEntryToPad(pad, y, entry.prefix, entry.text, entry.color_pair);
            y += 1; // Add spacing between entries
        }
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "ConversationState: add and get entries" {
    var cs = ConversationState.init(std.testing.allocator);
    defer cs.deinit();

    try cs.addEntry("[User]", "Hello", core.NCURSES_PAIR_USER);
    try cs.addEntry("[Assistant]", "Hi there!", core.NCURSES_PAIR_ASSISTANT);

    try std.testing.expectEqual(@as(usize, 2), cs.getEntryCount());

    const entry0 = cs.getEntry(0).?;
    try std.testing.expectEqualStrings("[User]", entry0.prefix.?);
    try std.testing.expectEqualStrings("Hello", entry0.text);
}

test "ConversationState: update last entry" {
    var cs = ConversationState.init(std.testing.allocator);
    defer cs.deinit();

    try cs.addEntry("[User]", "Hello", core.NCURSES_PAIR_USER);
    try cs.updateLastEntry("Hello, world!");

    const entry = cs.getEntry(0).?;
    try std.testing.expectEqualStrings("Hello, world!", entry.text);
}

test "ConversationState: clear entries" {
    var cs = ConversationState.init(std.testing.allocator);
    defer cs.deinit();

    try cs.addEntry("[User]", "Hello", core.NCURSES_PAIR_USER);
    cs.clear();

    try std.testing.expectEqual(@as(usize, 0), cs.getEntryCount());
}
