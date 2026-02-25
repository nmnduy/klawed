//! Print Helpers module
//!
//! Idiomatic Zig replacement for src/ui/print_helpers.c and src/ui/print_helpers.h
//!
//! Provides helper functions for formatted printing in the TUI.

const std = @import("std");
const logger = @import("../logger.zig");

/// Print a wrapped text with proper line breaking
pub fn printWrapped(
    allocator: std.mem.Allocator,
    writer: anytype,
    text: []const u8,
    max_width: usize,
    indent: usize,
) !void {
    var words = std.mem.tokenizeAny(u8, text, " \t\n");
    var current_line_len: usize = 0;
    var first_word = true;

    while (words.next()) |word| {
        const word_len = word.len;

        // Check if we need to wrap
        if (!first_word and current_line_len + 1 + word_len > max_width) {
            try writer.writeByte('\n');
            // Add indent
            var i: usize = 0;
            while (i < indent) : (i += 1) {
                try writer.writeByte(' ');
            }
            current_line_len = indent;
        } else if (!first_word) {
            try writer.writeByte(' ');
            current_line_len += 1;
        }

        try writer.writeAll(word);
        current_line_len += word_len;
        first_word = false;
    }
}

/// Print a separator line
pub fn printSeparator(writer: anytype, width: usize, char: u8) !void {
    var i: usize = 0;
    while (i < width) : (i += 1) {
        try writer.writeByte(char);
    }
    try writer.writeByte('\n');
}

/// Print a labeled value
pub fn printLabeledValue(
    writer: anytype,
    label: []const u8,
    value: []const u8,
    label_width: usize,
) !void {
    try writer.writeAll(label);

    // Pad label to fixed width
    if (label.len < label_width) {
        var i: usize = label.len;
        while (i < label_width) : (i += 1) {
            try writer.writeByte(' ');
        }
    }

    try writer.writeAll(": ");
    try writer.writeAll(value);
    try writer.writeByte('\n');
}

/// Print a table row
pub fn printTableRow(
    writer: anytype,
    columns: []const []const u8,
    widths: []const usize,
) !void {
    for (columns, 0..) |col, i| {
        if (i > 0) try writer.writeAll("  ");

        const width = if (i < widths.len) widths[i] else col.len;

        try writer.writeAll(col);

        // Pad to width
        if (col.len < width) {
            var j: usize = col.len;
            while (j < width) : (j += 1) {
                try writer.writeByte(' ');
            }
        }
    }
    try writer.writeByte('\n');
}

/// Print colored text (ANSI escape codes)
pub fn printColored(
    writer: anytype,
    text: []const u8,
    color_code: []const u8,
) !void {
    try writer.writeAll(color_code);
    try writer.writeAll(text);
    try writer.writeAll("\x1b[0m"); // Reset
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "printWrapped: basic wrapping" {
    const text = "This is a long text that needs to be wrapped at a specific width";
    var buf: [256]u8 = undefined;
    var fbs = std.io.fixedBufferStream(&buf);

    try printWrapped(std.testing.allocator, fbs.writer(), text, 20, 4);

    const result = fbs.getWritten();
    try std.testing.expect(std.mem.indexOf(u8, result, "\n") != null);
}

test "printSeparator: generates line" {
    var buf: [50]u8 = undefined;
    var fbs = std.io.fixedBufferStream(&buf);

    try printSeparator(fbs.writer(), 10, '-');

    try std.testing.expectEqualStrings("----------\n", fbs.getWritten());
}

test "printLabeledValue: formats correctly" {
    var buf: [100]u8 = undefined;
    var fbs = std.io.fixedBufferStream(&buf);

    try printLabeledValue(fbs.writer(), "Name", "Value", 10);

    try std.testing.expect(std.mem.startsWith(u8, fbs.getWritten(), "Name"));
    try std.testing.expect(std.mem.indexOf(u8, fbs.getWritten(), ": Value") != null);
}
