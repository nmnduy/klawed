//! tests/test_history_file.zig — Zig port of tests/test_history_file.c
//!
//! Tests the HistoryFile API: newline escaping/unescaping, append,
//! loadRecent, limit handling, and edge cases.

const std = @import("std");
const history_file = @import("../history_file.zig");

const HistoryFile = history_file.HistoryFile;

// ---------------------------------------------------------------------------
// escapeNewlines
// ---------------------------------------------------------------------------

test "history_file: escape basic newlines" {
    const alloc = std.testing.allocator;

    const escaped = try history_file.escapeNewlines(alloc, "hello\nworld");
    defer alloc.free(escaped);

    try std.testing.expectEqualStrings("hello\\nworld", escaped);
}

test "history_file: escape multiple newlines" {
    const alloc = std.testing.allocator;

    const escaped = try history_file.escapeNewlines(alloc, "line1\nline2\nline3");
    defer alloc.free(escaped);

    try std.testing.expectEqualStrings("line1\\nline2\\nline3", escaped);
}

test "history_file: escape no-op when no newlines" {
    const alloc = std.testing.allocator;

    const escaped = try history_file.escapeNewlines(alloc, "no newlines here");
    defer alloc.free(escaped);

    try std.testing.expectEqualStrings("no newlines here", escaped);
}

test "history_file: escape empty string" {
    const alloc = std.testing.allocator;

    const escaped = try history_file.escapeNewlines(alloc, "");
    defer alloc.free(escaped);

    try std.testing.expectEqualStrings("", escaped);
}

test "history_file: escaped output contains no literal newlines" {
    const alloc = std.testing.allocator;

    const escaped = try history_file.escapeNewlines(alloc, "a\nb\nc");
    defer alloc.free(escaped);

    try std.testing.expect(std.mem.indexOf(u8, escaped, "\n") == null);
}

// ---------------------------------------------------------------------------
// unescapeNewlines
// ---------------------------------------------------------------------------

test "history_file: unescape basic escape sequence" {
    const alloc = std.testing.allocator;

    const unescaped = try history_file.unescapeNewlines(alloc, "hello\\nworld");
    defer alloc.free(unescaped);

    try std.testing.expectEqualStrings("hello\nworld", unescaped);
}

test "history_file: unescape multiple sequences" {
    const alloc = std.testing.allocator;

    const unescaped = try history_file.unescapeNewlines(alloc, "line1\\nline2\\nline3");
    defer alloc.free(unescaped);

    try std.testing.expectEqualStrings("line1\nline2\nline3", unescaped);
}

test "history_file: unescape no-op when no escape sequences" {
    const alloc = std.testing.allocator;

    const result = try history_file.unescapeNewlines(alloc, "no escapes here");
    defer alloc.free(result);

    try std.testing.expectEqualStrings("no escapes here", result);
}

test "history_file: unescape empty string" {
    const alloc = std.testing.allocator;

    const result = try history_file.unescapeNewlines(alloc, "");
    defer alloc.free(result);

    try std.testing.expectEqualStrings("", result);
}

// ---------------------------------------------------------------------------
// Round-trip: escape → unescape
// ---------------------------------------------------------------------------

test "history_file: escape/unescape round-trip single newline" {
    const alloc = std.testing.allocator;

    const original = "line1\nline2";
    const escaped = try history_file.escapeNewlines(alloc, original);
    defer alloc.free(escaped);
    const restored = try history_file.unescapeNewlines(alloc, escaped);
    defer alloc.free(restored);

    try std.testing.expectEqualStrings(original, restored);
}

test "history_file: escape/unescape round-trip multiple values" {
    const alloc = std.testing.allocator;

    const cases = [_][]const u8{
        "single line",
        "line1\nline2",
        "line1\nline2\nline3",
        "text with\nmultiple\nnewlines\nin it",
        "",
        "\n",
        "\n\n\n",
        "text\n",
        "\ntext",
        "text\n\ntext",
    };

    for (cases) |original| {
        const escaped = try history_file.escapeNewlines(alloc, original);
        defer alloc.free(escaped);
        const restored = try history_file.unescapeNewlines(alloc, escaped);
        defer alloc.free(restored);
        try std.testing.expectEqualStrings(original, restored);
    }
}

// ---------------------------------------------------------------------------
// HistoryFile open, append, loadRecent
// ---------------------------------------------------------------------------

test "history_file: append and loadRecent basic" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "hist.txt" });
    defer alloc.free(path);

    var hf = try HistoryFile.open(alloc, path);
    defer hf.close();

    try hf.append("first entry");
    try hf.append("second entry");
    try hf.append("third entry");

    const entries = try hf.loadRecent(10);
    defer hf.freeLoaded(entries);

    try std.testing.expectEqual(@as(usize, 3), entries.len);
    try std.testing.expectEqualStrings("first entry", entries[0]);
    try std.testing.expectEqualStrings("second entry", entries[1]);
    try std.testing.expectEqualStrings("third entry", entries[2]);
}

test "history_file: append with newlines preserves multi-line content" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "multiline.txt" });
    defer alloc.free(path);

    var hf = try HistoryFile.open(alloc, path);
    defer hf.close();

    const text_with_newlines = "line1\nline2\nline3";
    try hf.append(text_with_newlines);

    const entries = try hf.loadRecent(10);
    defer hf.freeLoaded(entries);

    try std.testing.expectEqual(@as(usize, 1), entries.len);
    try std.testing.expectEqualStrings(text_with_newlines, entries[0]);
}

test "history_file: loadRecent from file with pre-escaped content" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "preescaped.txt" });
    defer alloc.free(path);

    // Write escaped content directly to the file
    {
        const file = try std.fs.cwd().createFile(path, .{});
        defer file.close();
        try file.writeAll("line1\\nline2\\nline3\n");
        try file.writeAll("single line\n");
    }

    var hf = try HistoryFile.open(alloc, path);
    defer hf.close();

    const entries = try hf.loadRecent(10);
    defer hf.freeLoaded(entries);

    try std.testing.expectEqual(@as(usize, 2), entries.len);
    try std.testing.expectEqualStrings("line1\nline2\nline3", entries[0]);
    try std.testing.expectEqualStrings("single line", entries[1]);
}

// ---------------------------------------------------------------------------
// loadRecent limit
// ---------------------------------------------------------------------------

test "history_file: loadRecent respects limit" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "limit.txt" });
    defer alloc.free(path);

    var hf = try HistoryFile.open(alloc, path);
    defer hf.close();

    var i: usize = 0;
    while (i < 5) : (i += 1) {
        const line = try std.fmt.allocPrint(alloc, "entry {d}", .{i});
        defer alloc.free(line);
        try hf.append(line);
    }

    const entries = try hf.loadRecent(3);
    defer hf.freeLoaded(entries);

    try std.testing.expectEqual(@as(usize, 3), entries.len);
    try std.testing.expectEqualStrings("entry 2", entries[0]);
    try std.testing.expectEqualStrings("entry 3", entries[1]);
    try std.testing.expectEqualStrings("entry 4", entries[2]);
}

test "history_file: loadRecent limit 0 returns empty slice" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "zero.txt" });
    defer alloc.free(path);

    var hf = try HistoryFile.open(alloc, path);
    defer hf.close();

    try hf.append("something");

    const entries = try hf.loadRecent(0);
    defer hf.freeLoaded(entries);

    try std.testing.expectEqual(@as(usize, 0), entries.len);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

test "history_file: appending empty string is silently ignored" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "empty.txt" });
    defer alloc.free(path);

    var hf = try HistoryFile.open(alloc, path);
    defer hf.close();

    try hf.append(""); // should succeed but write nothing

    const entries = try hf.loadRecent(10);
    defer hf.freeLoaded(entries);

    try std.testing.expectEqual(@as(usize, 0), entries.len);
}

test "history_file: mix of empty and non-empty appends" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "mixed.txt" });
    defer alloc.free(path);

    var hf = try HistoryFile.open(alloc, path);
    defer hf.close();

    try hf.append("valid text");
    try hf.append(""); // should be skipped
    try hf.append("also valid");

    const entries = try hf.loadRecent(10);
    defer hf.freeLoaded(entries);

    try std.testing.expectEqual(@as(usize, 2), entries.len);
    try std.testing.expectEqualStrings("valid text", entries[0]);
    try std.testing.expectEqualStrings("also valid", entries[1]);
}

test "history_file: loadRecent on non-existent file returns empty slice" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    // Create a HistoryFile pointing at an existing path, then remove the file
    // and try to loadRecent — must return empty, not crash.
    const path = try std.fs.path.join(alloc, &.{ dir, "ghost.txt" });
    defer alloc.free(path);

    var hf = try HistoryFile.open(alloc, path);
    defer hf.close();

    // Remove the underlying file while the handle is still open
    std.fs.cwd().deleteFile(path) catch {};

    const entries = try hf.loadRecent(10);
    defer hf.freeLoaded(entries);

    try std.testing.expectEqual(@as(usize, 0), entries.len);
}

// ---------------------------------------------------------------------------
// Persistence: close and reopen
// ---------------------------------------------------------------------------

test "history_file: entries persist across close/reopen" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "persist.txt" });
    defer alloc.free(path);

    {
        var hf = try HistoryFile.open(alloc, path);
        defer hf.close();
        try hf.append("persistent entry");
    }

    // Re-open and verify
    var hf2 = try HistoryFile.open(alloc, path);
    defer hf2.close();

    const entries = try hf2.loadRecent(10);
    defer hf2.freeLoaded(entries);

    try std.testing.expectEqual(@as(usize, 1), entries.len);
    try std.testing.expectEqualStrings("persistent entry", entries[0]);
}
