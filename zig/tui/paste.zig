//! TUI Paste module
//!
//! Idiomatic Zig replacement for src/tui_paste.c and src/tui_paste.h
//!
//! Provides bracketed paste handling for the TUI.

const std = @import("std");

// Paste handling is integrated into the input module
// This file exists for organizational purposes

/// Paste state for bracketed paste handling
pub const PasteState = struct {
    active: bool,
    content: std.ArrayList(u8),

    pub fn init(allocator: std.mem.Allocator) PasteState {
        return PasteState{
            .active = false,
            .content = std.ArrayList(u8).init(allocator),
        };
    }

    pub fn deinit(self: *PasteState) void {
        self.content.deinit();
    }

    pub fn start(self: *PasteState) void {
        self.active = true;
        self.content.clearRetainingCapacity();
    }

    pub fn end(self: *PasteState) []const u8 {
        self.active = false;
        return self.content.items;
    }

    pub fn addContent(self: *PasteState, data: []const u8) !void {
        try self.content.appendSlice(data);
    }

    pub fn isActive(self: PasteState) bool {
        return self.active;
    }
};
