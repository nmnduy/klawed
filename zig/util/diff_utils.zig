//! Diff Utilities
//!
//! Idiomatic Zig replacements for src/util/diff_utils.c
//!
//! The C version shelled out to the external `diff` binary and piped its
//! output through ANSI colour codes.  The Zig port keeps that approach (the
//! external `diff` command is a POSIX guaranteed utility and produces very
//! good output), but wraps it cleanly with proper error unions and an
//! allocator-aware API.
//!
//! The heavy coupling to the TUI message queue and global colourscheme from
//! the C code is deliberately NOT ported here — those belong in higher-level
//! modules (Phase 9).  This module provides the *computation* primitives only.
//!
//! Key C→Zig translations:
//!   - `show_diff` (writes to global queue/stdout) → `computeDiff` (returns owned string)
//!   - `emit_diff_line` (global state, ANSI)        → `colorizeDiffLine` (pure function)

const std = @import("std");

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// ANSI colour codes used when rendering diff lines for terminal output.
pub const DiffColors = struct {
    add: []const u8 = "\x1b[32m",
    remove: []const u8 = "\x1b[31m",
    reset: []const u8 = "\x1b[0m",
};

/// Run the external `diff -u` command to compare `original` with `current`
/// content and return the unified diff as an owned string.
///
/// Returns `null` when the files are identical (diff exits 0, no output).
/// Returns an error when `diff` cannot be launched or writing the temp file
/// fails.
///
/// Caller must free the result with `allocator.free`.
pub fn computeDiff(
    allocator: std.mem.Allocator,
    original: []const u8,
    current: []const u8,
) !?[]u8 {
    var arena = std.heap.ArenaAllocator.init(allocator);
    defer arena.deinit();
    const aa = arena.allocator();

    // Create temp file for original content
    const orig_path = try writeTempFile(aa, "klawed_diff_orig_", original);
    defer std.fs.deleteFileAbsolute(orig_path) catch {};

    // Create temp file for current content
    const curr_path = try writeTempFile(aa, "klawed_diff_curr_", current);
    defer std.fs.deleteFileAbsolute(curr_path) catch {};

    const diff_cmd = try std.fmt.allocPrint(aa, "diff -u \"{s}\" \"{s}\"", .{
        orig_path,
        curr_path,
    });

    const result = try std.ChildProcess.run(.{
        .allocator = aa,
        .argv = &.{ "/bin/sh", "-c", diff_cmd },
    });

    // diff exits 0 = identical, 1 = differences, 2 = error
    if (result.stdout.len == 0) return null;

    return try allocator.dupe(u8, result.stdout);
}

/// Write `content` to a temporary file with a given `prefix`.
/// Returns the absolute path of the created file.
/// Caller is responsible for deleting the file and freeing the path.
fn writeTempFile(allocator: std.mem.Allocator, prefix: []const u8, content: []const u8) ![]u8 {
    const tmp_dir_path = "/tmp";
    const name = try std.fmt.allocPrint(allocator, "{s}/{s}{d}", .{
        tmp_dir_path,
        prefix,
        std.crypto.random.int(u32),
    });

    const file = try std.fs.createFileAbsolute(name, .{});
    defer file.close();
    try file.writeAll(content);

    return name;
}

/// Classify a unified diff line and return the appropriate ANSI prefix
/// and reset suffix for colouring.
///
/// Returns `null` if the line needs no colouring (header / context).
pub fn diffLineColor(line: []const u8, colors: DiffColors) ?struct { prefix: []const u8, suffix: []const u8 } {
    if (line.len == 0) return null;
    // Added line: starts with '+' but not '++'
    if (line[0] == '+' and (line.len < 2 or line[1] != '+')) {
        return .{ .prefix = colors.add, .suffix = colors.reset };
    }
    // Removed line: starts with '-' but not '--'
    if (line[0] == '-' and (line.len < 2 or line[1] != '-')) {
        return .{ .prefix = colors.remove, .suffix = colors.reset };
    }
    return null;
}

/// Render a single diff line with ANSI colour into `writer`.
/// Context lines (starting with ' ') and header lines ('---'/'+++') are
/// written without colour.
pub fn writeDiffLine(
    writer: anytype,
    line: []const u8,
    colors: DiffColors,
) !void {
    // Strip trailing CR/LF
    var trimmed = line;
    while (trimmed.len > 0 and (trimmed[trimmed.len - 1] == '\n' or trimmed[trimmed.len - 1] == '\r')) {
        trimmed = trimmed[0 .. trimmed.len - 1];
    }
    if (trimmed.len == 0) return;

    if (diffLineColor(trimmed, colors)) |col| {
        try writer.print("  {s}{s}{s}\n", .{ col.prefix, trimmed, col.suffix });
    } else {
        try writer.print("  {s}\n", .{trimmed});
    }
}

/// Render an entire unified diff with ANSI colours.
/// Returns an owned string; caller must free with `allocator.free`.
pub fn renderDiff(
    allocator: std.mem.Allocator,
    diff_output: []const u8,
    colors: DiffColors,
) ![]u8 {
    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    var iter = std.mem.splitAny(u8, diff_output, "\n");
    while (iter.next()) |line| {
        if (line.len == 0) continue;
        try writeDiffLine(buf.writer(), line, colors);
    }

    return buf.toOwnedSlice();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "diffLineColor: added line" {
    const colors = DiffColors{};
    const col = diffLineColor("+hello", colors).?;
    try std.testing.expectEqualStrings("\x1b[32m", col.prefix);
}

test "diffLineColor: removed line" {
    const colors = DiffColors{};
    const col = diffLineColor("-world", colors).?;
    try std.testing.expectEqualStrings("\x1b[31m", col.prefix);
}

test "diffLineColor: +++ header not coloured" {
    const colors = DiffColors{};
    try std.testing.expect(diffLineColor("+++file.txt", colors) == null);
}

test "diffLineColor: --- header not coloured" {
    const colors = DiffColors{};
    try std.testing.expect(diffLineColor("---file.txt", colors) == null);
}

test "diffLineColor: context line not coloured" {
    const colors = DiffColors{};
    try std.testing.expect(diffLineColor(" unchanged", colors) == null);
}

test "renderDiff: colours added and removed lines" {
    const allocator = std.testing.allocator;
    const diff_text =
        \\--- a/file.txt
        \\+++ b/file.txt
        \\@@ -1,2 +1,2 @@
        \\ context line
        \\-removed line
        \\+added line
    ;
    const colors = DiffColors{};
    const rendered = try renderDiff(allocator, diff_text, colors);
    defer allocator.free(rendered);

    try std.testing.expect(std.mem.indexOf(u8, rendered, "\x1b[32m") != null); // green for +
    try std.testing.expect(std.mem.indexOf(u8, rendered, "\x1b[31m") != null); // red for -
}

test "computeDiff: identical content returns null" {
    const allocator = std.testing.allocator;
    const result = try computeDiff(allocator, "same\n", "same\n");
    try std.testing.expect(result == null);
}

test "computeDiff: different content returns diff" {
    const allocator = std.testing.allocator;
    const result = try computeDiff(allocator, "old\n", "new\n");
    if (result) |diff| {
        defer allocator.free(diff);
        try std.testing.expect(diff.len > 0);
        try std.testing.expect(std.mem.indexOf(u8, diff, "-old") != null);
        try std.testing.expect(std.mem.indexOf(u8, diff, "+new") != null);
    } else {
        // diff returned null unexpectedly — fail the test
        try std.testing.expect(false);
    }
}
