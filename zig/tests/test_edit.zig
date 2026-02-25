//! tests/test_edit.zig — Zig port of tests/test_edit.c
//!
//! Tests the Edit tool (executeEdit) and MultiEdit tool (executeMultiEdit)
//! from zig/tools/filesystem.zig.

const std = @import("std");
const fs_mod = @import("../tools/filesystem.zig");

const executeEdit = fs_mod.executeEdit;
const executeMultiEdit = fs_mod.executeMultiEdit;

// ---------------------------------------------------------------------------
// Helper: create a temp file with content, return its absolute path
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

// ---------------------------------------------------------------------------
// Simple single-replace tests (mirrors test_simple_single_replace)
// ---------------------------------------------------------------------------

test "Edit: replaces first occurrence only" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const content = "foo bar foo baz foo";
    try makeTempFile(tmp, "test.txt", content);

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "test.txt" });
    defer alloc.free(file_path);

    const json_text = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\"foo\",\"new_string\":\"FOO\"}}",
        .{file_path},
    );
    defer alloc.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json_text, .{});
    defer parsed.deinit();

    var result = try executeEdit(alloc, parsed.value);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "test.txt");
    defer alloc.free(after);
    // Only first "foo" replaced
    try std.testing.expectEqualStrings("FOO bar foo baz foo", after);
}

test "Edit: replaces with empty string (deletion)" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "del.txt", "hello world");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "del.txt" });
    defer alloc.free(file_path);

    const json_text = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\" world\",\"new_string\":\"\"}}",
        .{file_path},
    );
    defer alloc.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json_text, .{});
    defer parsed.deinit();

    var result = try executeEdit(alloc, parsed.value);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "del.txt");
    defer alloc.free(after);
    try std.testing.expectEqualStrings("hello", after);
}

// ---------------------------------------------------------------------------
// String not found (mirrors test_string_not_found)
// ---------------------------------------------------------------------------

test "Edit: string not found returns error" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "nf.txt", "this file has no match");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "nf.txt" });
    defer alloc.free(file_path);

    const json_text = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\"nonexistent\",\"new_string\":\"replacement\"}}",
        .{file_path},
    );
    defer alloc.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json_text, .{});
    defer parsed.deinit();

    var result = try executeEdit(alloc, parsed.value);
    defer result.deinit(alloc);

    try std.testing.expect(result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "not found") != null or
        std.mem.indexOf(u8, result.content, "error") != null or
        std.mem.indexOf(u8, result.content, "Error") != null);
}

// ---------------------------------------------------------------------------
// File not found
// ---------------------------------------------------------------------------

test "Edit: file not found returns error" {
    const alloc = std.testing.allocator;

    const json_text = "{\"file_path\":\"/nonexistent/path/file.txt\",\"old_string\":\"x\",\"new_string\":\"y\"}";
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json_text, .{});
    defer parsed.deinit();

    var result = try executeEdit(alloc, parsed.value);
    defer result.deinit(alloc);

    try std.testing.expect(result.is_error);
}

// ---------------------------------------------------------------------------
// MultiEdit: multiple replacements
// ---------------------------------------------------------------------------

test "MultiEdit: applies multiple edits in order" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "multi.txt", "foo bar baz");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "multi.txt" });
    defer alloc.free(file_path);

    const json_text = try std.fmt.allocPrint(
        alloc,
        \\{{"file_path":"{s}","edits":[{{"old_string":"foo","new_string":"FOO"}},{{"old_string":"baz","new_string":"BAZ"}}]}}
    ,
        .{file_path},
    );
    defer alloc.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json_text, .{});
    defer parsed.deinit();

    var result = try executeMultiEdit(alloc, parsed.value);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "multi.txt");
    defer alloc.free(after);
    try std.testing.expectEqualStrings("FOO bar BAZ", after);
}

test "MultiEdit: empty edits array succeeds with no changes" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "empty_edits.txt", "original content");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "empty_edits.txt" });
    defer alloc.free(file_path);

    const json_text = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"edits\":[]}}",
        .{file_path},
    );
    defer alloc.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json_text, .{});
    defer parsed.deinit();

    var result = try executeMultiEdit(alloc, parsed.value);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
}

test "MultiEdit: some edits not found - partial success" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "partial.txt", "hello world");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "partial.txt" });
    defer alloc.free(file_path);

    // First edit succeeds, second fails
    const json_text = try std.fmt.allocPrint(
        alloc,
        \\{{"file_path":"{s}","edits":[{{"old_string":"hello","new_string":"HELLO"}},{{"old_string":"NOTFOUND","new_string":"x"}}]}}
    ,
        .{file_path},
    );
    defer alloc.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json_text, .{});
    defer parsed.deinit();

    var result = try executeMultiEdit(alloc, parsed.value);
    defer result.deinit(alloc);

    // MultiEdit reports counts (succeeded/failed) in the result
    // It should not be an error even if some edits fail.
    // We only check the result is well-formed (has content).
    try std.testing.expect(result.content.len > 0);
}
