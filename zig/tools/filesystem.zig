//! tools/filesystem.zig — Filesystem tool implementations
//!
//! Zig port of src/tools/tool_filesystem.c
//!
//! Implements: Read, Write, Edit, MultiEdit, Glob
//!
//! Key improvements over C:
//!   - `std.fs` for all I/O (no fopen/fclose)
//!   - `std.mem.replace` / `std.mem.indexOf` for Edit
//!   - `std.fs.path.globWalk` (or Dir.walk + manual matching) for Glob
//!   - Allocator-based memory, no manual malloc/free

const std = @import("std");
const utils = @import("utils.zig");

pub const ToolResult = utils.ToolResult;

/// Maximum file size to read into memory (64 MiB).
pub const max_read_size: usize = 64 * 1024 * 1024;

// ---------------------------------------------------------------------------
// Read tool
// ---------------------------------------------------------------------------

/// Execute the Read tool.
///
/// Input: `{ "file_path": <path>, "start_line": <opt int>, "end_line": <opt int> }`
pub fn executeRead(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const file_path = utils.jsonString(input, "file_path") orelse {
        return utils.errLit("Missing 'file_path' parameter");
    };

    const start_line_raw = utils.jsonInt(input, "start_line");
    const end_line_raw = utils.jsonInt(input, "end_line");

    const start_line: ?usize = if (start_line_raw) |v| blk: {
        if (v < 1) return utils.errLit("start_line must be >= 1");
        break :blk @intCast(v);
    } else null;

    const end_line: ?usize = if (end_line_raw) |v| blk: {
        if (v < 1) return utils.errLit("end_line must be >= 1");
        break :blk @intCast(v);
    } else null;

    if (start_line != null and end_line != null and start_line.? > end_line.?) {
        return utils.errLit("start_line must be <= end_line");
    }

    // Open file (handle both absolute and relative paths)
    const content = readFileAny(allocator, file_path) catch |e| {
        return utils.errFmt(allocator, "Failed to read file '{s}': {s}", .{ file_path, @errorName(e) });
    };
    defer allocator.free(content);

    // Count total lines and build filtered slice
    var total_lines: usize = 0;
    var out = std.ArrayList(u8).init(allocator);
    defer out.deinit();

    var line_num: usize = 1;
    var line_start: usize = 0;
    var i: usize = 0;
    while (i <= content.len) : (i += 1) {
        const at_eol = (i == content.len) or (content[i] == '\n');
        if (at_eol) {
            const line_end = if (i < content.len) i + 1 else i; // include '\n'
            const include = blk: {
                if (start_line) |sl| if (line_num < sl) break :blk false;
                if (end_line) |el| if (line_num > el) break :blk false;
                break :blk true;
            };
            if (include) {
                try out.appendSlice(content[line_start..line_end]);
            }
            total_lines = line_num;
            line_num += 1;
            line_start = line_end;
        }
    }
    // Account for last line without trailing newline
    if (line_start < content.len) {
        const include = blk: {
            if (start_line) |sl| if (line_num < sl) break :blk false;
            if (end_line) |el| if (line_num > el) break :blk false;
            break :blk true;
        };
        if (include) try out.appendSlice(content[line_start..]);
        total_lines = line_num;
    }

    const filtered = out.items;

    // Build JSON result
    var result = std.ArrayList(u8).init(allocator);
    defer result.deinit();
    const w = result.writer();

    try w.writeAll("{\"content\":");
    try writeJsonString(w, filtered);
    try std.fmt.format(w, ",\"total_lines\":{d}", .{total_lines});

    if (start_line != null or end_line != null) {
        try std.fmt.format(w, ",\"start_line\":{d},\"end_line\":{d}", .{
            start_line orelse 1,
            end_line orelse total_lines,
        });
    }
    try w.writeByte('}');

    return utils.okOwned(try result.toOwnedSlice());
}

// ---------------------------------------------------------------------------
// Write tool
// ---------------------------------------------------------------------------

/// Execute the Write tool.
///
/// Input: `{ "file_path": <path>, "content": <text> }`
pub fn executeWrite(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const file_path = utils.jsonString(input, "file_path") orelse {
        return utils.errLit("Missing 'file_path' parameter");
    };
    const content = utils.jsonString(input, "content") orelse {
        return utils.errLit("Missing required 'content' parameter. Write tool requires both 'file_path' and 'content' parameters.");
    };

    writeFileAny(file_path, content) catch |e| {
        return utils.errFmt(allocator, "Failed to write file '{s}': {s}", .{ file_path, @errorName(e) });
    };

    const msg = try std.fmt.allocPrint(
        allocator,
        "{{\"status\":\"success\",\"message\":\"File written successfully\",\"file_path\":\"{s}\"}}",
        .{file_path},
    );
    return utils.okOwned(msg);
}

// ---------------------------------------------------------------------------
// Edit tool
// ---------------------------------------------------------------------------

/// Execute the Edit tool — replace first occurrence of `old_string` with `new_string`.
///
/// Input: `{ "file_path": <path>, "old_string": <text>, "new_string": <text> }`
pub fn executeEdit(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const file_path = utils.jsonString(input, "file_path") orelse {
        return utils.errLit("Missing 'file_path' parameter");
    };
    const old_string = utils.jsonString(input, "old_string") orelse {
        return utils.errLit("Missing 'old_string' parameter");
    };
    const new_string = utils.jsonString(input, "new_string") orelse {
        return utils.errLit("Missing 'new_string' parameter");
    };

    const original = readFileAny(allocator, file_path) catch |e| {
        return utils.errFmt(allocator, "Failed to read file '{s}': {s}", .{ file_path, @errorName(e) });
    };
    defer allocator.free(original);

    const idx = std.mem.indexOf(u8, original, old_string) orelse {
        return utils.errFmt(
            allocator,
            "String not found in file '{s}': the exact text to replace was not found",
            .{file_path},
        );
    };

    // Build new content: original[0..idx] + new_string + original[idx+old_string.len..]
    const new_content = try std.mem.concat(allocator, u8, &.{
        original[0..idx],
        new_string,
        original[idx + old_string.len ..],
    });
    defer allocator.free(new_content);

    writeFileAny(file_path, new_content) catch |e| {
        return utils.errFmt(allocator, "Failed to write file '{s}': {s}", .{ file_path, @errorName(e) });
    };

    const msg = try std.fmt.allocPrint(
        allocator,
        "{{\"status\":\"success\",\"message\":\"Edit applied successfully\",\"file_path\":\"{s}\"}}",
        .{file_path},
    );
    return utils.okOwned(msg);
}

// ---------------------------------------------------------------------------
// MultiEdit tool
// ---------------------------------------------------------------------------

/// Execute the MultiEdit tool — apply multiple edits sequentially.
///
/// Input:
/// ```json
/// {
///   "file_path": <path>,
///   "edits": [
///     { "old_string": <text>, "new_string": <text> },
///     ...
///   ]
/// }
/// ```
pub fn executeMultiEdit(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const file_path = utils.jsonString(input, "file_path") orelse {
        return utils.errLit("Missing 'file_path' parameter");
    };

    const edits_val = switch (input) {
        .object => |m| m.get("edits"),
        else => null,
    } orelse return utils.errLit("Missing 'edits' parameter");

    const edits_arr = switch (edits_val) {
        .array => |a| a,
        else => return utils.errLit("'edits' must be an array"),
    };

    // Read original file
    var current = readFileAny(allocator, file_path) catch |e| {
        return utils.errFmt(allocator, "Failed to read file '{s}': {s}", .{ file_path, @errorName(e) });
    };
    // current is owned and mutated as we go

    var success_count: usize = 0;
    var fail_count: usize = 0;

    for (edits_arr.items) |edit_val| {
        const old_s = utils.jsonString(edit_val, "old_string") orelse {
            fail_count += 1;
            continue;
        };
        const new_s = utils.jsonString(edit_val, "new_string") orelse {
            fail_count += 1;
            continue;
        };

        const idx = std.mem.indexOf(u8, current, old_s);
        if (idx == null) {
            fail_count += 1;
            continue;
        }
        const i = idx.?;

        const replaced = try std.mem.concat(allocator, u8, &.{
            current[0..i],
            new_s,
            current[i + old_s.len ..],
        });
        allocator.free(current);
        current = replaced;
        success_count += 1;
    }
    defer allocator.free(current);

    // Write the final content
    writeFileAny(file_path, current) catch |e| {
        return utils.errFmt(allocator, "Failed to write file '{s}': {s}", .{ file_path, @errorName(e) });
    };

    const msg = try std.fmt.allocPrint(
        allocator,
        "{{\"status\":\"success\",\"successful_edits\":{d},\"failed_edits\":{d},\"file_path\":\"{s}\"}}",
        .{ success_count, fail_count, file_path },
    );
    return utils.okOwned(msg);
}

// ---------------------------------------------------------------------------
// Glob tool
// ---------------------------------------------------------------------------

/// Execute the Glob tool — find files matching a glob pattern.
///
/// Input: `{ "pattern": <glob>, "path": <optional dir> }`
pub fn executeGlob(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const pattern = utils.jsonString(input, "pattern") orelse {
        return utils.errLit("Missing 'pattern' parameter");
    };
    const search_root = utils.jsonString(input, "path") orelse ".";

    var matches = std.ArrayList([]const u8).init(allocator);
    defer {
        for (matches.items) |m| allocator.free(m);
        matches.deinit();
    }

    // Walk the directory tree collecting matching paths
    var dir = std.fs.cwd().openDir(search_root, .{ .iterate = true }) catch |e| {
        return utils.errFmt(allocator, "Failed to open directory '{s}': {s}", .{ search_root, @errorName(e) });
    };
    defer dir.close();

    var walker = try dir.walk(allocator);
    defer walker.deinit();

    while (try walker.next()) |entry| {
        if (entry.kind != .file and entry.kind != .sym_link) continue;
        if (matchGlob(pattern, entry.path)) {
            const owned = try allocator.dupe(u8, entry.path);
            try matches.append(owned);
        }
    }

    // Sort for deterministic output
    std.mem.sort([]const u8, matches.items, {}, struct {
        fn lt(_: void, a: []const u8, b: []const u8) bool {
            return std.mem.lessThan(u8, a, b);
        }
    }.lt);

    // Build JSON array
    var out = std.ArrayList(u8).init(allocator);
    defer out.deinit();
    const w = out.writer();

    try w.writeAll("{\"files\":[");
    for (matches.items, 0..) |m, idx| {
        if (idx > 0) try w.writeByte(',');
        try writeJsonString(w, m);
    }
    try std.fmt.format(w, "],\"count\":{d}}}", .{matches.items.len});

    return utils.okOwned(try out.toOwnedSlice());
}

// ---------------------------------------------------------------------------
// Glob pattern matching
// ---------------------------------------------------------------------------

/// Match `path` against a glob `pattern`.
/// Supports `*` (any non-separator chars), `**` (any chars including `/`),
/// and `?` (any single char).
pub fn matchGlob(pattern: []const u8, path: []const u8) bool {
    return matchGlobHelper(pattern, path);
}

fn matchGlobHelper(pattern: []const u8, text: []const u8) bool {
    var pi: usize = 0;
    var ti: usize = 0;

    while (pi < pattern.len) {
        if (pattern[pi] == '*') {
            // Check for **
            const double_star = pi + 1 < pattern.len and pattern[pi + 1] == '*';
            if (double_star) {
                // ** matches zero or more path components
                pi += 2;
                // Skip any following /
                if (pi < pattern.len and pattern[pi] == '/') pi += 1;
                if (pi == pattern.len) return true; // ** at end matches everything
                // Try matching the rest of the pattern at every position
                while (ti <= text.len) {
                    if (matchGlobHelper(pattern[pi..], text[ti..])) return true;
                    if (ti == text.len) break;
                    ti += 1;
                }
                return false;
            } else {
                // * matches any sequence of non-'/' characters
                pi += 1;
                if (pi == pattern.len) {
                    // * at end: match if no '/' in remaining text
                    return std.mem.indexOfScalar(u8, text[ti..], '/') == null;
                }
                while (ti <= text.len) {
                    if (matchGlobHelper(pattern[pi..], text[ti..])) return true;
                    if (ti == text.len) break;
                    if (text[ti] == '/') break; // single * doesn't cross directory boundaries
                    ti += 1;
                }
                return false;
            }
        } else if (pattern[pi] == '?') {
            if (ti >= text.len or text[ti] == '/') return false;
            pi += 1;
            ti += 1;
        } else {
            if (ti >= text.len or pattern[pi] != text[ti]) return false;
            pi += 1;
            ti += 1;
        }
    }
    return ti == text.len;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Read a file, handling both absolute and relative paths.
fn readFileAny(allocator: std.mem.Allocator, path: []const u8) ![]u8 {
    if (std.fs.path.isAbsolute(path)) {
        const f = try std.fs.openFileAbsolute(path, .{});
        defer f.close();
        return f.readToEndAlloc(allocator, max_read_size);
    } else {
        const f = try std.fs.cwd().openFile(path, .{});
        defer f.close();
        return f.readToEndAlloc(allocator, max_read_size);
    }
}

/// Write a file, creating parent directories as needed.
fn writeFileAny(path: []const u8, content: []const u8) !void {
    if (std.fs.path.dirname(path)) |dir_path| {
        if (std.fs.path.isAbsolute(dir_path)) {
            std.fs.makeDirAbsolute(dir_path) catch |e| switch (e) {
                error.PathAlreadyExists => {},
                else => {
                    // Try makePath for nested dirs
                    std.fs.cwd().makePath(dir_path) catch {};
                },
            };
        } else {
            std.fs.cwd().makePath(dir_path) catch {};
        }
    }
    if (std.fs.path.isAbsolute(path)) {
        const f = try std.fs.createFileAbsolute(path, .{ .truncate = true });
        defer f.close();
        try f.writeAll(content);
    } else {
        const f = try std.fs.cwd().createFile(path, .{ .truncate = true });
        defer f.close();
        try f.writeAll(content);
    }
}

/// Write `s` as a JSON-encoded string with surrounding double-quotes.
fn writeJsonString(writer: anytype, s: []const u8) !void {
    try writer.writeByte('"');
    for (s) |c| {
        switch (c) {
            '"' => try writer.writeAll("\\\""),
            '\\' => try writer.writeAll("\\\\"),
            '\n' => try writer.writeAll("\\n"),
            '\r' => try writer.writeAll("\\r"),
            '\t' => try writer.writeAll("\\t"),
            0x00...0x08, 0x0B...0x0C, 0x0E...0x1F => try std.fmt.format(writer, "\\u{X:0>4}", .{c}),
            else => try writer.writeByte(c),
        }
    }
    try writer.writeByte('"');
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "matchGlob: exact match" {
    try std.testing.expect(matchGlob("foo.zig", "foo.zig"));
    try std.testing.expect(!matchGlob("foo.zig", "bar.zig"));
}

test "matchGlob: single star" {
    try std.testing.expect(matchGlob("*.zig", "foo.zig"));
    try std.testing.expect(matchGlob("*.zig", "bar.zig"));
    try std.testing.expect(!matchGlob("*.zig", "src/foo.zig")); // single * doesn't cross /
}

test "matchGlob: double star" {
    try std.testing.expect(matchGlob("**/*.zig", "src/foo.zig"));
    try std.testing.expect(matchGlob("**/*.zig", "a/b/c/foo.zig"));
    try std.testing.expect(!matchGlob("**/*.zig", "src/foo.c"));
}

test "matchGlob: question mark" {
    try std.testing.expect(matchGlob("fo?.zig", "foo.zig"));
    try std.testing.expect(!matchGlob("fo?.zig", "fo.zig"));
    try std.testing.expect(!matchGlob("fo?.zig", "fo/o.zig")); // ? doesn't cross /
}

test "executeRead: reads entire file" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try tmp.dir.writeFile("test.txt", "line1\nline2\nline3\n");

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const allocator = std.testing.allocator;
    const json_text = try std.fmt.allocPrint(allocator, "{{\"file_path\":\"{s}/test.txt\"}}", .{tmp_path});
    defer allocator.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try executeRead(allocator, parsed.value);
    defer result.deinit(allocator);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "line1") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "total_lines") != null);
}

test "executeRead: line range" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try tmp.dir.writeFile("test.txt", "line1\nline2\nline3\n");

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const allocator = std.testing.allocator;
    const json_text = try std.fmt.allocPrint(
        allocator,
        "{{\"file_path\":\"{s}/test.txt\",\"start_line\":2,\"end_line\":2}}",
        .{tmp_path},
    );
    defer allocator.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try executeRead(allocator, parsed.value);
    defer result.deinit(allocator);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "line2") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "line1") == null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "line3") == null);
}

test "executeEdit: replaces first occurrence" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try tmp.dir.writeFile("edit.txt", "hello world hello");

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const allocator = std.testing.allocator;
    const file_path = try std.fmt.allocPrint(allocator, "{s}/edit.txt", .{tmp_path});
    defer allocator.free(file_path);

    const json_text = try std.fmt.allocPrint(
        allocator,
        "{{\"file_path\":\"{s}\",\"old_string\":\"hello\",\"new_string\":\"goodbye\"}}",
        .{file_path},
    );
    defer allocator.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try executeEdit(allocator, parsed.value);
    defer result.deinit(allocator);
    try std.testing.expect(!result.is_error);

    // Read back and verify only first occurrence replaced
    const after = try std.fs.openFileAbsolute(file_path, .{});
    defer after.close();
    const content = try after.readToEndAlloc(allocator, 1024);
    defer allocator.free(content);

    try std.testing.expectEqualStrings("goodbye world hello", content);
}

test "executeEdit: string not found returns error" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try tmp.dir.writeFile("edit.txt", "hello world");

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const allocator = std.testing.allocator;
    const json_text = try std.fmt.allocPrint(
        allocator,
        "{{\"file_path\":\"{s}/edit.txt\",\"old_string\":\"NOTFOUND\",\"new_string\":\"x\"}}",
        .{tmp_path},
    );
    defer allocator.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try executeEdit(allocator, parsed.value);
    defer result.deinit(allocator);
    try std.testing.expect(result.is_error);
}

test "executeMultiEdit: applies multiple edits" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try tmp.dir.writeFile("multi.txt", "foo bar baz");

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const allocator = std.testing.allocator;
    const json_text = try std.fmt.allocPrint(
        allocator,
        \\{{"file_path":"{s}/multi.txt","edits":[{{"old_string":"foo","new_string":"FOO"}},{{"old_string":"baz","new_string":"BAZ"}}]}}
    ,
        .{tmp_path},
    );
    defer allocator.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try executeMultiEdit(allocator, parsed.value);
    defer result.deinit(allocator);
    try std.testing.expect(!result.is_error);

    const file_path = try std.fmt.allocPrint(allocator, "{s}/multi.txt", .{tmp_path});
    defer allocator.free(file_path);
    const after = try std.fs.openFileAbsolute(file_path, .{});
    defer after.close();
    const content = try after.readToEndAlloc(allocator, 1024);
    defer allocator.free(content);
    try std.testing.expectEqualStrings("FOO bar BAZ", content);
}

test "executeGlob: finds matching files" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try tmp.dir.writeFile("a.zig", "");
    try tmp.dir.writeFile("b.zig", "");
    try tmp.dir.writeFile("c.txt", "");

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const allocator = std.testing.allocator;
    const json_text = try std.fmt.allocPrint(
        allocator,
        "{{\"pattern\":\"*.zig\",\"path\":\"{s}\"}}",
        .{tmp_path},
    );
    defer allocator.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try executeGlob(allocator, parsed.value);
    defer result.deinit(allocator);
    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "a.zig") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "b.zig") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "c.txt") == null);
}
