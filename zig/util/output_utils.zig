//! Output Utilities
//!
//! Idiomatic Zig replacements for src/util/output_utils.c
//!
//! The C version was tightly coupled to the TUI message queue, global thread-local
//! state, and the oneshot mode flag.  The full TUI integration belongs in Phase 9.
//!
//! This module provides the *pure formatting* primitives that are independent
//! of rendering destination.  A higher-level output coordinator (Phase 8/9)
//! will wire these to the TUI queue or direct stdout as appropriate.
//!
//! Key C→Zig translations:
//!   - `tool_emit_line`   → `formatToolLine`    (pure: returns owned string, no side-effects)
//!   - `emit_diff_line`   → moved to diff_utils.writeDiffLine (pure writer-based)
//!   - Global state       → eliminated; callers thread context through as parameters

const std = @import("std");

// ---------------------------------------------------------------------------
// Tool line formatting
// ---------------------------------------------------------------------------

/// Format a tool output line as `"<prefix> <text>"` or just `"<text>"` when
/// prefix is empty.
///
/// This is the pure formatting core of `tool_emit_line` from the C code,
/// without the side effects (TUI queue posting / printf).
///
/// Caller must free the result with `allocator.free`.
pub fn formatToolLine(
    allocator: std.mem.Allocator,
    prefix: []const u8,
    text: []const u8,
) ![]u8 {
    if (prefix.len > 0 and text.len > 0) {
        return std.fmt.allocPrint(allocator, "{s} {s}", .{ prefix, text });
    } else if (prefix.len > 0) {
        return allocator.dupe(u8, prefix);
    } else {
        return allocator.dupe(u8, text);
    }
}

/// Output destination for tool messages.
/// In interactive TUI mode this would route to the message queue;
/// in one-shot/subagent mode it routes to stdout or is suppressed.
pub const OutputDest = enum {
    /// Write directly to stdout (interactive non-TUI mode).
    stdout,
    /// Suppress output (oneshot/subagent mode).
    suppress,
    /// Buffer into an ArrayList for capture (testing, oneshot output).
    buffer,
};

/// Emit a tool line to the given destination.
///
/// For `OutputDest.buffer` a non-null `buf` must be provided.
pub fn emitToolLine(
    prefix: []const u8,
    text: []const u8,
    dest: OutputDest,
    buf: ?*std.ArrayList(u8),
) !void {
    switch (dest) {
        .suppress => return,
        .stdout => {
            const stdout = std.io.getStdOut().writer();
            if (prefix.len > 0 and text.len > 0) {
                try stdout.print("{s} {s}\n", .{ prefix, text });
            } else if (prefix.len > 0) {
                try stdout.print("{s}\n", .{prefix});
            } else {
                try stdout.print("{s}\n", .{text});
            }
        },
        .buffer => {
            const b = buf orelse return error.NullBuffer;
            if (prefix.len > 0 and text.len > 0) {
                try b.writer().print("{s} {s}\n", .{ prefix, text });
            } else if (prefix.len > 0) {
                try b.writer().print("{s}\n", .{prefix});
            } else {
                try b.writer().print("{s}\n", .{text});
            }
        },
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "formatToolLine: prefix + text" {
    const a = std.testing.allocator;
    const line = try formatToolLine(a, "✓", "file written");
    defer a.free(line);
    try std.testing.expectEqualStrings("✓ file written", line);
}

test "formatToolLine: empty prefix" {
    const a = std.testing.allocator;
    const line = try formatToolLine(a, "", "just text");
    defer a.free(line);
    try std.testing.expectEqualStrings("just text", line);
}

test "formatToolLine: empty text" {
    const a = std.testing.allocator;
    const line = try formatToolLine(a, "prefix", "");
    defer a.free(line);
    try std.testing.expectEqualStrings("prefix", line);
}

test "formatToolLine: both empty" {
    const a = std.testing.allocator;
    const line = try formatToolLine(a, "", "");
    defer a.free(line);
    try std.testing.expectEqualStrings("", line);
}

test "emitToolLine: buffer destination captures output" {
    var buf = std.ArrayList(u8).init(std.testing.allocator);
    defer buf.deinit();

    try emitToolLine("»", "reading file", .buffer, &buf);
    try emitToolLine("", "line two", .buffer, &buf);

    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "» reading file") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "line two") != null);
}

test "emitToolLine: suppress produces no output" {
    // Just verifies no error is returned
    try emitToolLine("any", "thing", .suppress, null);
}
