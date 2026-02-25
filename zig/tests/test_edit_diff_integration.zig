//! tests/test_edit_diff_integration.zig — Zig port of tests/test_edit_diff_integration.c
//!
//! Integration tests for the Edit tool covering the full edit workflow:
//!   - Simple first-occurrence replacement
//!   - Multi-line string replacement
//!   - Edit followed by verifying the file content on disk
//!   - Editing non-existent text returns an error
//!   - Sequential edits to the same file accumulate correctly
//!
//! Note: The C test also exercised colorised diff output, which is a
//! display-side concern not present in the Zig tool API.  Those visual
//! checks are not ported; we focus on the correctness of the edit
//! operation itself.

const std = @import("std");
const fs_mod = @import("../tools/filesystem.zig");

const executeEdit = fs_mod.executeEdit;
const executeWrite = fs_mod.executeWrite;

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

fn runWrite(alloc: std.mem.Allocator, json: []const u8) !fs_mod.ToolResult {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();
    return executeWrite(alloc, parsed.value);
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

test "Edit diff integration: simple replacement modifies correct text" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "simple.txt", "Hello World\nThis is a test\nGoodbye World\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "simple.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\"World\",\"new_string\":\"Universe\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runEdit(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "simple.txt");
    defer alloc.free(after);
    // First occurrence replaced; second "World" unchanged
    try std.testing.expectEqualStrings("Hello Universe\nThis is a test\nGoodbye World\n", after);
}

test "Edit diff integration: replace all via sequential edits" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "all.txt", "foo bar foo baz foo\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "all.txt" });
    defer alloc.free(file_path);

    // Replace all three "foo" tokens one by one
    for (0..3) |_| {
        const json = try std.fmt.allocPrint(
            alloc,
            "{{\"file_path\":\"{s}\",\"old_string\":\"foo\",\"new_string\":\"qux\"}}",
            .{file_path},
        );
        defer alloc.free(json);

        var result = try runEdit(alloc, json);
        defer result.deinit(alloc);
        try std.testing.expect(!result.is_error);
    }

    const after = try readTempFile(alloc, tmp_path, "all.txt");
    defer alloc.free(after);
    try std.testing.expectEqualStrings("qux bar qux baz qux\n", after);
}

test "Edit diff integration: multi-line replacement" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "multi.txt",
        "Line 1: original\nLine 2: original\nLine 3: original\nLine 4: original\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "multi.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\"Line 2: original\",\"new_string\":\"Line 2: MODIFIED\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runEdit(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "multi.txt");
    defer alloc.free(after);
    try std.testing.expect(std.mem.indexOf(u8, after, "Line 2: MODIFIED") != null);
    try std.testing.expect(std.mem.indexOf(u8, after, "Line 1: original") != null);
    try std.testing.expect(std.mem.indexOf(u8, after, "Line 3: original") != null);
}

test "Edit diff integration: Write then Edit — correct combined result" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "combo.txt" });
    defer alloc.free(file_path);

    // Create the file with Write
    const write_json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"content\":\"Original line 1\\nOriginal line 2\\nOriginal line 3\\n\"}}",
        .{file_path},
    );
    defer alloc.free(write_json);
    var wr = try runWrite(alloc, write_json);
    defer wr.deinit(alloc);
    try std.testing.expect(!wr.is_error);

    // Now edit one of the lines
    const edit_json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\"Original\",\"new_string\":\"Modified\"}}",
        .{file_path},
    );
    defer alloc.free(edit_json);
    var er = try runEdit(alloc, edit_json);
    defer er.deinit(alloc);
    try std.testing.expect(!er.is_error);

    const after = try readTempFile(alloc, tmp_path, "combo.txt");
    defer alloc.free(after);
    try std.testing.expect(std.mem.indexOf(u8, after, "Modified line 1") != null);
    // Second occurrence still "Original"
    try std.testing.expect(std.mem.indexOf(u8, after, "Original line 2") != null);
}

test "Edit diff integration: old_string not found returns error" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "notfound.txt", "Hello World\nThis is a test\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "notfound.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\"NONEXISTENT\",\"new_string\":\"x\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runEdit(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(result.is_error);
}

test "Edit diff integration: result JSON contains status=success on success" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "status.txt", "foo bar\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "status.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"old_string\":\"foo\",\"new_string\":\"baz\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runEdit(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "success") != null);
}
