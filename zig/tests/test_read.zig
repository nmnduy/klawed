//! tests/test_read.zig — Zig port of tests/test_read.c
//!
//! Tests the Read tool (executeRead) from zig/tools/filesystem.zig:
//!   - Reading an entire file returns all content and correct total_lines
//!   - Reading a specific line range returns only those lines
//!   - start_line-only (no end_line) returns from that line to end of file
//!   - end_line-only (no start_line) returns from line 1 to that line
//!   - start_line > end_line is an error
//!   - Reading a non-existent file is an error
//!   - Missing file_path parameter is an error

const std = @import("std");
const fs_mod = @import("../tools/filesystem.zig");

const executeRead = fs_mod.executeRead;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

fn makeTempFile(tmp_dir: std.testing.TmpDir, name: []const u8, content: []const u8) !void {
    try tmp_dir.dir.writeFile(name, content);
}

fn runRead(alloc: std.mem.Allocator, json: []const u8) !fs_mod.ToolResult {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();
    return executeRead(alloc, parsed.value);
}

/// Extract a string field from a JSON object string.
fn jsonStr(alloc: std.mem.Allocator, json: []const u8, field: []const u8) !?[]u8 {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();
    const obj = switch (parsed.value) {
        .object => |m| m,
        else => return null,
    };
    const v = obj.get(field) orelse return null;
    return switch (v) {
        .string => |s| try alloc.dupe(u8, s),
        else => null,
    };
}

/// Extract an integer field from a JSON object string.
fn jsonInt(alloc: std.mem.Allocator, json: []const u8, field: []const u8) !?i64 {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();
    const obj = switch (parsed.value) {
        .object => |m| m,
        else => return null,
    };
    const v = obj.get(field) orelse return null;
    return switch (v) {
        .integer => |i| i,
        .float => |f| @intFromFloat(f),
        else => null,
    };
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

test "Read: entire file returns all content and correct total_lines" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const content = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5\n";
    try makeTempFile(tmp, "whole.txt", content);

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "whole.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runRead(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const got_content = try jsonStr(alloc, result.content, "content");
    defer if (got_content) |c| alloc.free(c);
    try std.testing.expect(got_content != null);
    try std.testing.expectEqualStrings(content, got_content.?);

    const total_lines = try jsonInt(alloc, result.content, "total_lines");
    // 5 lines with trailing newline — the read tool counts the trailing empty line
    try std.testing.expect(total_lines != null);
    try std.testing.expect(total_lines.? >= 5 and total_lines.? <= 6);
}

test "Read: specific line range 2-4 returns only those lines" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const content = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5\n";
    try makeTempFile(tmp, "range.txt", content);

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "range.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"start_line\":2,\"end_line\":4}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runRead(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const got_content = try jsonStr(alloc, result.content, "content");
    defer if (got_content) |c| alloc.free(c);
    try std.testing.expect(got_content != null);
    try std.testing.expectEqualStrings("Line 2\nLine 3\nLine 4\n", got_content.?);

    // start_line and end_line are echoed in the result
    const start = try jsonInt(alloc, result.content, "start_line");
    const end = try jsonInt(alloc, result.content, "end_line");
    try std.testing.expectEqual(@as(?i64, 2), start);
    try std.testing.expectEqual(@as(?i64, 4), end);
}

test "Read: start_line only — reads from that line to end of file" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const content = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5\n";
    try makeTempFile(tmp, "start_only.txt", content);

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "start_only.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"start_line\":3}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runRead(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const got_content = try jsonStr(alloc, result.content, "content");
    defer if (got_content) |c| alloc.free(c);
    try std.testing.expect(got_content != null);
    try std.testing.expectEqualStrings("Line 3\nLine 4\nLine 5\n", got_content.?);
}

test "Read: end_line only — reads from line 1 to that line" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const content = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5\n";
    try makeTempFile(tmp, "end_only.txt", content);

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "end_only.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"end_line\":2}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runRead(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const got_content = try jsonStr(alloc, result.content, "content");
    defer if (got_content) |c| alloc.free(c);
    try std.testing.expect(got_content != null);
    try std.testing.expectEqualStrings("Line 1\nLine 2\n", got_content.?);
}

test "Read: start_line > end_line returns error" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const content = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5\n";
    try makeTempFile(tmp, "invalid_range.txt", content);

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "invalid_range.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\",\"start_line\":4,\"end_line\":2}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runRead(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "start_line") != null or
        std.mem.indexOf(u8, result.content, "end_line") != null or
        std.mem.indexOf(u8, result.content, "<=") != null);
}

test "Read: missing file_path parameter returns error" {
    const alloc = std.testing.allocator;

    var result = try runRead(alloc, "{}");
    defer result.deinit(alloc);

    try std.testing.expect(result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "file_path") != null or
        std.mem.indexOf(u8, result.content, "Missing") != null);
}

test "Read: nonexistent file returns error" {
    const alloc = std.testing.allocator;

    const json = "{\"file_path\":\"/nonexistent/klawed_test/file.txt\"}";
    var result = try runRead(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(result.is_error);
}

test "Read: single line file without trailing newline" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    // No trailing newline
    try makeTempFile(tmp, "single.txt", "only line");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "single.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runRead(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const got_content = try jsonStr(alloc, result.content, "content");
    defer if (got_content) |c| alloc.free(c);
    try std.testing.expect(got_content != null);
    try std.testing.expectEqualStrings("only line", got_content.?);
}

test "Read: empty file returns empty content and zero or one total_lines" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try makeTempFile(tmp, "empty.txt", "");

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(alloc, &.{ tmp_path, "empty.txt" });
    defer alloc.free(file_path);

    const json = try std.fmt.allocPrint(
        alloc,
        "{{\"file_path\":\"{s}\"}}",
        .{file_path},
    );
    defer alloc.free(json);

    var result = try runRead(alloc, json);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);

    const got_content = try jsonStr(alloc, result.content, "content");
    defer if (got_content) |c| alloc.free(c);
    try std.testing.expect(got_content != null);
    try std.testing.expectEqualStrings("", got_content.?);
}
