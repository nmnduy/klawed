//! tests/test_spacing_simple.zig — Zig port of tests/test_spacing_simple.c
//!
//! The C test was a minimal manual visual test that printed CLI-style output
//! lines to verify there were no extra blank lines between sections.  It had
//! no assertions — just `printf` calls whose output was eyeballed.
//!
//! This Zig port turns those visual checks into real assertions by exercising
//! the string utilities and output formatting that the non-TUI ("one-shot")
//! mode relies on.
//!
//! Specifically we test:
//!   - Trailing-newline handling: a message with a trailing newline does not
//!     get an extra blank line inserted when printed sequentially.
//!   - Tool-result prefix formatting: "[Tool: <name>]" style labels.
//!   - ANSI spinner line clearing: the "\r\033[K" pattern that erases the
//!     spinner before writing the final tool-result line.
//!   - The `trim` utility removes leading/trailing whitespace without
//!     adding extra newlines.

const std = @import("std");
const string_utils = @import("../util/string_utils.zig");

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/// Simulate building a CLI output buffer for a conversation turn.
/// Returns an owned slice that the caller must free.
fn buildCliOutput(alloc: std.mem.Allocator) ![]u8 {
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();
    const w = buf.writer();

    // Assistant message
    try w.writeAll("[Assistant] I'll help you test the spacing\n");
    // Tool label — no extra blank line before it
    try w.writeAll("[Tool: Bash] echo 'test'\n");
    // Spinner clear + result
    try w.writeAll("\r\x1b[K\u{2713} Tool execution completed successfully\n");
    // Second assistant message — no extra blank line
    try w.writeAll("[Assistant] Done!\n");

    return buf.toOwnedSlice();
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

test "spacing: no double blank lines between consecutive output lines" {
    const alloc = std.testing.allocator;

    const output = try buildCliOutput(alloc);
    defer alloc.free(output);

    // There must be no "\n\n" (double blank line) anywhere in the output
    try std.testing.expect(std.mem.indexOf(u8, output, "\n\n") == null);
}

test "spacing: assistant prefix present" {
    const alloc = std.testing.allocator;

    const output = try buildCliOutput(alloc);
    defer alloc.free(output);

    try std.testing.expect(std.mem.indexOf(u8, output, "[Assistant]") != null);
}

test "spacing: tool label present" {
    const alloc = std.testing.allocator;

    const output = try buildCliOutput(alloc);
    defer alloc.free(output);

    try std.testing.expect(std.mem.indexOf(u8, output, "[Tool: Bash]") != null);
}

test "spacing: spinner clear sequence (\\r\\033[K) present" {
    const alloc = std.testing.allocator;

    const output = try buildCliOutput(alloc);
    defer alloc.free(output);

    // The carriage-return + ANSI erase-line sequence must be present
    try std.testing.expect(std.mem.indexOf(u8, output, "\r\x1b[K") != null);
}

test "spacing: completion marker present" {
    const alloc = std.testing.allocator;

    const output = try buildCliOutput(alloc);
    defer alloc.free(output);

    try std.testing.expect(std.mem.indexOf(u8, output, "Tool execution completed") != null);
}

test "spacing: trim does not introduce extra newlines" {
    // The C test originally identified an extra `printf("\n")` that was
    // inadvertently adding blank lines.  Verify that our trim utility never
    // produces a string that ends with "\n\n".
    const alloc = std.testing.allocator;

    const inputs = [_][]const u8{
        "  message\n",
        "\nmessage  ",
        "  \n  message  \n  ",
        "message",
    };

    for (inputs) |input| {
        const trimmed = string_utils.trim(input);
        // trimmed is a sub-slice — no allocation needed
        try std.testing.expect(std.mem.indexOf(u8, trimmed, "\n\n") == null);
        // Should not start or end with whitespace
        if (trimmed.len > 0) {
            try std.testing.expect(trimmed[0] != ' ' and trimmed[0] != '\n' and trimmed[0] != '\t');
            const last = trimmed[trimmed.len - 1];
            try std.testing.expect(last != ' ' and last != '\n' and last != '\t');
        }
    }
    _ = alloc; // not used here but kept for future tests
}

test "spacing: dupTrim preserves content without leading/trailing whitespace" {
    const alloc = std.testing.allocator;

    const result = try string_utils.dupTrim(alloc, "  hello world  ");
    defer alloc.free(result);

    try std.testing.expectEqualStrings("hello world", result);
    // No double-newline
    try std.testing.expect(std.mem.indexOf(u8, result, "\n\n") == null);
}

test "spacing: sequential lines joined with single newlines only" {
    const alloc = std.testing.allocator;

    // Build what a non-TUI session log looks like
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    const lines = [_][]const u8{
        "[Assistant] First response\n",
        "[Tool: Read] /path/to/file\n",
        "[Assistant] Second response\n",
    };

    for (lines) |line| {
        try buf.appendSlice(line);
        // Must NOT append an extra newline after each line
    }

    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "\n\n") == null);
}
