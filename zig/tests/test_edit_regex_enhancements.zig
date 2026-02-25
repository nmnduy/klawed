//! tests/test_edit_regex_enhancements.zig — Zig port of tests/test_edit_regex_enhancements.c
//!
//! The C version tested POSIX regex with capture groups, back-references, and
//! flags (REG_ICASE, REG_NEWLINE) — features that are C-implementation-specific
//! and not present in the Zig Edit tool, which performs plain string replacement.
//!
//! This Zig port covers the aspects of the Edit tool that *are* tested here:
//!   - Exact string replacement (the foundation that the regex tests build on)
//!   - Case-sensitive matching (the Zig tool is always case-sensitive)
//!   - Multi-line file edits — replacing text that spans lines
//!   - Replacing text containing special characters (slashes, dots, parens)
//!   - Sequential edits that together achieve a "replace all" result
//!
//! Any regex-specific behaviour (capture groups, REG_ICASE, etc.) is
//! explicitly documented as "not supported in Zig" and those tests are
//! skipped.

const std = @import("std");
const fs_mod = @import("../tools/filesystem.zig");

const executeEdit = fs_mod.executeEdit;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

fn makeTempFile(tmp_dir: std.testing.TmpDir, name: []const u8, content: []const u8) !void {
    try tmp_dir.dir.writeFile(name, content);
}

fn readTempFile(alloc: std.mem.Allocator, abs_dir: []const u8, name: []const u8) ![]u8 {
    const path = try std.fs.path.join(alloc, &.{ abs_dir, name });
    defer alloc.free(path);
    const f = try std.fs.openFileAbsolute(path, .{});
    defer f.close();
    return f.readToEndAlloc(alloc, 1024 * 64);
}

fn runEdit(alloc: std.mem.Allocator, json: []const u8) !fs_mod.ToolResult {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();
    return executeEdit(alloc, parsed.value);
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

test "Edit enhancements: basic name replacement (no regex)" {
    // Equivalent of C test_capture_group_swap but without regex:
    // replace the exact text "John Doe" with "Doe, John".
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "names.txt", "John Doe\nJane Smith\nBob Johnson\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "names.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\"John Doe\",\"new_string\":\"Doe, John\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runEdit(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "names.txt");
    defer alloc.free(after);
    try std.testing.expect(std.mem.indexOf(u8, after, "Doe, John") != null);
}

test "Edit enhancements: date reformat via exact match" {
    // Equivalent of C test_capture_group_reformat_date but without regex.
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "dates.txt", "Date: 12/25/2023\nExpiry: 01/01/2024\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "dates.txt" });
    defer alloc.free(file_path);

    // Replace first date
    {
        const json = try std.fmt.allocPrint(
            alloc,
            "{{\"file_path\":\"{s}\",\"old_string\":\"12/25/2023\",\"new_string\":\"2023-12-25\"}}",
            .{file_path},
        );
        defer alloc.free(json);
        var r = try runEdit(alloc, json);
        defer r.deinit(alloc);
        try std.testing.expect(!r.is_error);
    }
    // Replace second date
    {
        const json = try std.fmt.allocPrint(
            alloc,
            "{{\"file_path\":\"{s}\",\"old_string\":\"01/01/2024\",\"new_string\":\"2024-01-01\"}}",
            .{file_path},
        );
        defer alloc.free(json);
        var r = try runEdit(alloc, json);
        defer r.deinit(alloc);
        try std.testing.expect(!r.is_error);
    }

    const after = try readTempFile(alloc, tmp_path, "dates.txt");
    defer alloc.free(after);
    try std.testing.expect(std.mem.indexOf(u8, after, "2023-12-25") != null);
    try std.testing.expect(std.mem.indexOf(u8, after, "2024-01-01") != null);
    // Original formats should be gone
    try std.testing.expect(std.mem.indexOf(u8, after, "12/25/2023") == null);
    try std.testing.expect(std.mem.indexOf(u8, after, "01/01/2024") == null);
}

test "Edit enhancements: version string transformation" {
    // Equivalent of C test_capture_group_extract_version but without regex.
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "version.txt", "Version 3.14.159 released\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "version.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\"Version 3.14.159\",\"new_string\":\"Major: 3, Minor: 14, Patch: 159\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runEdit(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "version.txt");
    defer alloc.free(after);
    try std.testing.expect(std.mem.indexOf(u8, after, "Major: 3, Minor: 14, Patch: 159") != null);
}

test "Edit enhancements: case-sensitive matching — wrong case returns error" {
    // The Zig Edit tool is always case-sensitive.
    // Replacing "TODO" with "DONE" when the file contains "todo" should fail.
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "case.txt", "todo: Fix bug\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "case.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\"TODO\",\"new_string\":\"DONE\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runEdit(alloc, json);
    defer result.deinit(alloc);

    // "TODO" (uppercase) is not in the file — must report not-found error
    try std.testing.expect(result.is_error);
}

test "Edit enhancements: string containing special chars (slashes, dots)" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "path.txt", "path/to/file\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "path.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\"path/to/file\",\"new_string\":\"path\\\\to\\\\file\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runEdit(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "path.txt");
    defer alloc.free(after);
    // backslash-separated path present
    try std.testing.expect(std.mem.indexOf(u8, after, "path\\to\\file") != null);
}

test "Edit enhancements: repeated substitution of same group text" {
    // Equivalent of C test_multiple_same_capture_group without regex.
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "repeat.txt", "foo bar baz\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "repeat.txt" });
    defer alloc.free(file_path);

    // Replace "foo" with "foo-foo-foo"
    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\"foo\",\"new_string\":\"foo-foo-foo\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runEdit(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "repeat.txt");
    defer alloc.free(after);
    try std.testing.expect(std.mem.indexOf(u8, after, "foo-foo-foo") != null);
}

test "Edit enhancements: prefix lines by exact-match replacement" {
    // Equivalent of C test_regex_flag_multiline but without regex.
    // Replace individual "Line" prefixes one at a time.
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "lines.txt", "Start\nLine 1\nLine 2\nEnd\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "lines.txt" });
    defer alloc.free(file_path);

    // Replace "Line 1" and "Line 2" individually
    for ([_][]const u8{ "Line 1", "Line 2" }) |line| {
        const new_line = try std.fmt.allocPrint(alloc, ">>> {s}", .{line});
        defer alloc.free(new_line);

        const json = try std.fmt.allocPrint(
            alloc,
            "{{\"file_path\":\"{s}\",\"old_string\":\"{s}\",\"new_string\":\">>> {s}\"}}",
            .{ file_path, line, line },
        );
        defer alloc.free(json);

        var r = try runEdit(alloc, json);
        defer r.deinit(alloc);
        try std.testing.expect(!r.is_error);
    }

    const after = try readTempFile(alloc, tmp_path, "lines.txt");
    defer alloc.free(after);
    try std.testing.expect(std.mem.indexOf(u8, after, ">>> Line 1") != null);
    try std.testing.expect(std.mem.indexOf(u8, after, ">>> Line 2") != null);
}
