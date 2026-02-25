//! interactive/input_handler.zig — Non-TUI stdin input reading
//!
//! Zig port of src/interactive/input_handler.c (non-TUI portion).
//!
//! In Phase 8, the TUI is not yet integrated; this module provides a
//! simple line-reading loop that reads from stdin.  Phase 9 will replace
//! this with the ncurses TUI.

const std = @import("std");

// ---------------------------------------------------------------------------
// InputHandler
// ---------------------------------------------------------------------------

pub const InputResult = enum {
    /// User submitted input; `text` contains the line.
    input,
    /// EOF or Ctrl+D — caller should exit.
    eof,
    /// Ctrl+C interrupt.
    interrupt,
};

pub const ReadLineResult = struct {
    kind: InputResult,
    /// Owned slice.  Only valid when `kind == .input`.  Caller must free.
    text: ?[]u8 = null,
};

/// Read a single line from `stdin`, trimming the trailing newline.
/// Returns ownership of the allocated line to the caller.
pub fn readLine(
    allocator: std.mem.Allocator,
    prompt: []const u8,
) !ReadLineResult {
    const stdout = std.io.getStdOut().writer();
    const stdin = std.io.getStdIn().reader();

    try stdout.writeAll(prompt);

    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    stdin.streamUntilDelimiter(buf.writer(), '\n', null) catch |err| {
        if (err == error.EndOfStream) {
            if (buf.items.len == 0) {
                return ReadLineResult{ .kind = .eof };
            }
            // Partial line before EOF — treat as input.
        } else {
            return err;
        }
    };

    if (buf.items.len == 0) {
        // Empty line.
        return ReadLineResult{ .kind = .input, .text = try allocator.dupe(u8, "") };
    }

    // Strip trailing '\r' for Windows compatibility.
    if (buf.items[buf.items.len - 1] == '\r') {
        _ = buf.pop();
    }

    return ReadLineResult{ .kind = .input, .text = try buf.toOwnedSlice() };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "ReadLineResult: input kind has text" {
    // We can't easily test actual stdin reading in a unit test, so we just
    // verify the struct fields work as expected.
    const alloc = std.testing.allocator;
    const text = try alloc.dupe(u8, "hello");
    const result = ReadLineResult{ .kind = .input, .text = text };
    defer if (result.text) |t| alloc.free(t);

    try std.testing.expectEqual(InputResult.input, result.kind);
    try std.testing.expectEqualStrings("hello", result.text.?);
}

test "ReadLineResult: eof has no text" {
    const result = ReadLineResult{ .kind = .eof };
    try std.testing.expectEqual(InputResult.eof, result.kind);
    try std.testing.expectEqual(@as(?[]u8, null), result.text);
}
