//! tools/search.zig — Grep search tool implementation
//!
//! Zig port of src/tools/tool_search.c
//!
//! Uses the system `grep`/`rg`/`ag` tools (just like the C version) rather
//! than reimplementing regex search from scratch.  The Zig layer handles:
//!   - parameter extraction and validation
//!   - subprocess invocation
//!   - result aggregation and truncation
//!   - JSON output formatting

const std = @import("std");
const utils = @import("utils.zig");

pub const ToolResult = utils.ToolResult;

/// Default maximum number of matches to return.
pub const default_max_results: usize = 100;

/// Directories and file patterns that are always excluded from search.
/// Matches the C implementation's exclusion list.
const excluded_dirs = [_][]const u8{
    ".git",     ".svn",     ".hg",          "node_modules",
    "bower_components", "vendor", "build",  "dist",
    "target",   ".cache",   ".venv",        "venv",
    "__pycache__",
};

const excluded_files = [_][]const u8{
    "*.min.js", "*.min.css", "*.pyc",
    "*.o",      "*.a",       "*.so",
    "*.dylib",  "*.exe",     "*.dll",
    "*.class",  "*.jar",     "*.war",
    "*.zip",    "*.tar",     "*.gz",
    "*.log",    ".DS_Store",
};

// ---------------------------------------------------------------------------
// Tool detection
// ---------------------------------------------------------------------------

const GrepTool = enum { rg, ag, grep };

fn detectGrepTool(allocator: std.mem.Allocator) GrepTool {
    if (commandExists(allocator, "rg")) return .rg;
    if (commandExists(allocator, "ag")) return .ag;
    return .grep;
}

fn commandExists(allocator: std.mem.Allocator, cmd: []const u8) bool {
    const check = std.fmt.allocPrint(
        allocator,
        "command -v {s} >/dev/null 2>&1",
        .{cmd},
    ) catch return false;
    defer allocator.free(check);

    const result = std.process.Child.run(.{
        .allocator = allocator,
        .argv = &.{ "/bin/sh", "-c", check },
        .max_output_bytes = 256,
    }) catch return false;
    allocator.free(result.stdout);
    allocator.free(result.stderr);
    return result.term == .Exited and result.term.Exited == 0;
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

/// Execute the Grep tool.
///
/// Input:
/// ```json
/// { "pattern": <string>, "path": <optional string>, "max_results": <optional int> }
/// ```
pub fn execute(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const pattern = utils.jsonString(input, "pattern") orelse {
        return utils.errLit("Missing 'pattern' parameter");
    };
    const search_path = utils.jsonString(input, "path") orelse ".";

    const max_results: usize = blk: {
        if (utils.jsonInt(input, "max_results")) |v| {
            if (v > 0) break :blk @intCast(v);
        }
        if (std.posix.getenv("KLAWED_GREP_MAX_RESULTS")) |env| {
            const v = std.fmt.parseInt(usize, env, 10) catch 0;
            if (v > 0) break :blk v;
        }
        break :blk default_max_results;
    };

    const grep_tool = detectGrepTool(allocator);

    // Build the grep command
    const cmd = try buildGrepCommand(allocator, grep_tool, pattern, search_path);
    defer allocator.free(cmd);

    // Run command and collect lines
    const raw = std.process.Child.run(.{
        .allocator = allocator,
        .argv = &.{ "/bin/sh", "-c", cmd },
        .max_output_bytes = 10 * 1024 * 1024, // 10 MB
    }) catch |e| {
        return utils.errFmt(allocator, "Failed to execute grep: {s}", .{@errorName(e)});
    };
    defer allocator.free(raw.stdout);
    defer allocator.free(raw.stderr);

    // Split into lines and collect up to max_results
    var matches = std.ArrayList([]const u8).init(allocator);
    defer matches.deinit();

    var iter = std.mem.splitScalar(u8, raw.stdout, '\n');
    var total_matches: usize = 0;
    while (iter.next()) |line| {
        if (line.len == 0) continue;
        total_matches += 1;
        if (matches.items.len < max_results) {
            try matches.append(line);
        }
    }

    const truncated = total_matches > max_results;
    const match_count = matches.items.len;

    // Build JSON result
    var out = std.ArrayList(u8).init(allocator);
    defer out.deinit();
    const w = out.writer();

    try w.writeAll("{\"matches\":[");
    for (matches.items, 0..) |m, i| {
        if (i > 0) try w.writeByte(',');
        try writeJsonString(w, m);
    }
    try w.writeByte(']');

    if (truncated) {
        const warning = try std.fmt.allocPrint(
            allocator,
            "Results truncated: showing {d}/{d} matches. Use KLAWED_GREP_MAX_RESULTS to adjust limit, or refine your search pattern.",
            .{ match_count, total_matches },
        );
        defer allocator.free(warning);
        try w.writeAll(",\"warning\":");
        try writeJsonString(w, warning);
    }

    try std.fmt.format(w, ",\"match_count\":{d},\"total_matches\":{d}}}", .{ match_count, total_matches });

    return utils.okOwned(try out.toOwnedSlice());
}

// ---------------------------------------------------------------------------
// Command builder
// ---------------------------------------------------------------------------

fn buildGrepCommand(
    allocator: std.mem.Allocator,
    tool: GrepTool,
    pattern: []const u8,
    path: []const u8,
) ![]u8 {
    var buf = std.ArrayList(u8).init(allocator);
    const w = buf.writer();

    switch (tool) {
        .rg => {
            try w.writeAll("rg -n ");
            for (excluded_dirs) |d| {
                try std.fmt.format(w, "-g '!{s}' ", .{d});
            }
            for (excluded_files) |f| {
                try std.fmt.format(w, "-g '!{s}' ", .{f});
            }
            // Escape single quotes in pattern
            try w.writeByte('\'');
            try writeShellEscaped(w, pattern);
            try w.writeByte('\'');
            try w.writeByte(' ');
            try w.writeAll(path);
            try w.writeAll(" 2>/dev/null || true");
        },
        .ag => {
            try w.writeAll("ag -n ");
            for (excluded_dirs) |d| {
                try std.fmt.format(w, "--ignore={s} ", .{d});
            }
            try w.writeByte('\'');
            try writeShellEscaped(w, pattern);
            try w.writeByte('\'');
            try w.writeByte(' ');
            try w.writeAll(path);
            try w.writeAll(" 2>/dev/null || true");
        },
        .grep => {
            try w.writeAll("grep -r -n ");
            for (excluded_dirs) |d| {
                try std.fmt.format(w, "--exclude-dir={s} ", .{d});
            }
            for (excluded_files) |f| {
                try std.fmt.format(w, "--exclude='{s}' ", .{f});
            }
            try w.writeByte('\'');
            try writeShellEscaped(w, pattern);
            try w.writeByte('\'');
            try w.writeByte(' ');
            try w.writeAll(path);
            try w.writeAll(" 2>/dev/null || true");
        },
    }

    return buf.toOwnedSlice();
}

/// Escape single quotes for shell embedding inside single-quoted strings.
fn writeShellEscaped(writer: anytype, s: []const u8) !void {
    for (s) |c| {
        if (c == '\'') {
            try writer.writeAll("'\\''");
        } else {
            try writer.writeByte(c);
        }
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

test "search tool: missing pattern returns error" {
    const allocator = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{}", .{});
    defer parsed.deinit();

    const result = try execute(allocator, parsed.value);
    try std.testing.expect(result.is_error);
}

test "search tool: finds pattern in temp dir" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try tmp.dir.writeFile("a.txt", "hello world\n");
    try tmp.dir.writeFile("b.txt", "goodbye world\n");
    try tmp.dir.writeFile("c.txt", "no match here\n");

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const allocator = std.testing.allocator;
    const json_text = try std.fmt.allocPrint(
        allocator,
        "{{\"pattern\":\"world\",\"path\":\"{s}\"}}",
        .{tmp_path},
    );
    defer allocator.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try execute(allocator, parsed.value);
    defer result.deinit(allocator);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "world") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "match_count") != null);
}

test "search tool: max_results respected" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    // Write a file with 10 matching lines
    var content = std.ArrayList(u8).init(std.testing.allocator);
    defer content.deinit();
    var i: usize = 0;
    while (i < 10) : (i += 1) {
        try content.appendSlice("needle line\n");
    }
    try tmp.dir.writeFile("big.txt", content.items);

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const allocator = std.testing.allocator;
    const json_text = try std.fmt.allocPrint(
        allocator,
        "{{\"pattern\":\"needle\",\"path\":\"{s}\",\"max_results\":3}}",
        .{tmp_path},
    );
    defer allocator.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try execute(allocator, parsed.value);
    defer result.deinit(allocator);

    try std.testing.expect(!result.is_error);
    // Should be truncated
    try std.testing.expect(std.mem.indexOf(u8, result.content, "warning") != null);
}
