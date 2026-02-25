//! tests/test_write_diff_integration.zig — Zig port of tests/test_write_diff_integration.c
//!
//! Integration tests for the Write tool covering:
//!   - Overwriting an existing file with new content
//!   - Creating a new file (no original content)
//!   - Complete content replacement
//!   - Multi-line content written correctly
//!   - Written content is exactly what was specified (byte-for-byte)
//!   - Result JSON contains status=success
//!
//! Note: The C test also checked colorised diff output when overwriting files,
//! which is a display-side concern.  The Zig Write tool does not produce diffs;
//! we verify functional correctness instead.

const std = @import("std");
const fs_mod = @import("../tools/filesystem.zig");

const executeWrite = fs_mod.executeWrite;
const executeRead = fs_mod.executeRead;

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

fn runWrite(alloc: std.mem.Allocator, json: []const u8) !fs_mod.ToolResult {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();
    return executeWrite(alloc, parsed.value);
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

test "Write diff integration: overwrite existing file with simple change" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    // Write original content
    try makeTempFile(tmp, "overwrite.txt", "Hello World\nThis is a test\nGoodbye World\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "overwrite.txt" });
    defer alloc.free(file_path);

    const new_content = "Hello Universe\nThis is a modified test\nGoodbye World\n";
    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"content\":\"Hello Universe\\nThis is a modified test\\nGoodbye World\\n\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runWrite(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "success") != null);

    const after = try readTempFile(alloc, tmp_path, "overwrite.txt");
    defer alloc.free(after);
    try std.testing.expectEqualStrings(new_content, after);
}

test "Write diff integration: create new file" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "new_file.txt" });
    defer alloc.free(file_path);

    const content = "This is a brand new file\nWith some content\n";
    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"content\":\"This is a brand new file\\nWith some content\\n\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runWrite(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "new_file.txt");
    defer alloc.free(after);
    try std.testing.expectEqualStrings(content, after);
}

test "Write diff integration: multi-line overwrite replaces all content" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "multi.txt", "Line 1: original\nLine 2: original\nLine 3: original\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "multi.txt" });
    defer alloc.free(file_path);

    const new_content = "Line 1: MODIFIED\nLine 2: original\nLine 3: MODIFIED\nNew Line 4: added\n";
    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"content\":\"Line 1: MODIFIED\\nLine 2: original\\nLine 3: MODIFIED\\nNew Line 4: added\\n\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runWrite(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "multi.txt");
    defer alloc.free(after);
    try std.testing.expectEqualStrings(new_content, after);
}

test "Write diff integration: complete content replacement" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "replace.txt", "Old content line 1\nOld content line 2\nOld content line 3\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "replace.txt" });
    defer alloc.free(file_path);

    const new_content = "Completely new content\nDifferent structure\nNew format\n";
    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"content\":\"Completely new content\\nDifferent structure\\nNew format\\n\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runWrite(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "replace.txt");
    defer alloc.free(after);
    try std.testing.expectEqualStrings(new_content, after);
    // Old content must be gone
    try std.testing.expect(std.mem.indexOf(u8, after, "Old content") == null);
}

test "Write diff integration: write empty content truncates file" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "truncate.txt", "Some existing content to be erased\n");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "truncate.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"content\":\"\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runWrite(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const after = try readTempFile(alloc, tmp_path, "truncate.txt");
    defer alloc.free(after);
    try std.testing.expectEqualStrings("", after);
}

test "Write diff integration: result JSON contains file_path" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "check_path.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"content\":\"hello\\n\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runWrite(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    // Result should echo back the file path
    try std.testing.expect(std.mem.indexOf(u8, result.content, "check_path.txt") != null);
}

test "Write diff integration: missing content parameter returns error" {
    const alloc = std.testing.allocator;

    const json = "{\"file_path\":\"/tmp/klawed_test_no_content.txt\"}";
    var result = try runWrite(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "content") != null or
        std.mem.indexOf(u8, result.content, "Missing") != null);
}

test "Write diff integration: missing file_path parameter returns error" {
    const alloc = std.testing.allocator;

    const json = "{\"content\":\"hello\"}";
    var result = try runWrite(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(result.is_error);
}
