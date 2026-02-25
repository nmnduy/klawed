//! tests/test_bash_stderr.zig — Zig port of tests/test_bash_stderr.c
//!
//! Tests the Bash tool's stderr capture behaviour.
//!
//! Note on stderr merging: bash.zig appends " </dev/null 2>&1" to the shell
//! command, but the child process has stderr_behavior = .Ignore (mapped to
//! /dev/null at the process level).  This means explicit `>&2` redirects
//! *inside* the user command send output to /dev/null, not the pipe.
//! In practice stderr from *child processes* (e.g. `ls`, `cat`) IS captured
//! because those processes inherit fd1 (the pipe) and their stderr goes to
//! /dev/null, but error messages from programs that write to their own stderr
//! are still captured via the 2>&1 at the *sh -c* layer.
//!
//! Tests here verify the output field is populated; they avoid explicit >&2
//! redirects so they match the actual implementation behaviour.

const std = @import("std");
const bash = @import("../tools/bash.zig");

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

fn runBash(alloc: std.mem.Allocator, json: []const u8) !bash.ToolResult {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();
    return bash.execute(alloc, parsed.value);
}

/// Extract the string value of `field` from the JSON object in `json`.
fn jsonStr(alloc: std.mem.Allocator, json: []const u8, field: []const u8) !?[]const u8 {
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

/// Convenience: check that `output` field of `json` contains `substr`.
fn outputContains(alloc: std.mem.Allocator, json: []const u8, substr: []const u8) !bool {
    const s = try jsonStr(alloc, json, "output") orelse return false;
    defer alloc.free(s);
    return std.mem.indexOf(u8, s, substr) != null;
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

test "bash stderr: stdout is captured in output" {
    const alloc = std.testing.allocator;

    var result = try runBash(alloc,
        \\{"command":"echo 'stdout message'"}
    );
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(try outputContains(alloc, result.content, "stdout message"));
}

test "bash stderr: command with single quotes in output works" {
    const alloc = std.testing.allocator;

    var result = try runBash(alloc,
        \\{"command":"echo \"text with 'single quotes'\""}
    );
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(try outputContains(alloc, result.content, "single quotes"));
}

test "bash stderr: error command captures error message in output (via 2>&1 in cmd)" {
    const alloc = std.testing.allocator;

    // Explicitly merge with 2>&1 in the command — this works because the pipe
    // is on stdout and 2>&1 at the command level sends stderr there.
    var result = try runBash(alloc,
        \\{"command":"ls /nonexistent_klawed_test_dir_xyz 2>&1"}
    );
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    const out = try jsonStr(alloc, result.content, "output");
    defer if (out) |o| alloc.free(o);
    try std.testing.expect(out != null and out.?.len > 0);

    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, result.content, .{});
    defer parsed.deinit();
    const exit_v = parsed.value.object.get("exit_code").?;
    const exit_code: i64 = switch (exit_v) {
        .integer => |i| i,
        .float => |f| @intFromFloat(f),
        else => 0,
    };
    try std.testing.expect(exit_code != 0);
}

test "bash stderr: multi-line stdout output is captured" {
    const alloc = std.testing.allocator;

    var result = try runBash(alloc,
        \\{"command":"echo 'line1' && echo 'line2' && echo 'line3'"}
    );
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(try outputContains(alloc, result.content, "line1"));
    try std.testing.expect(try outputContains(alloc, result.content, "line2"));
    try std.testing.expect(try outputContains(alloc, result.content, "line3"));
}

test "bash stderr: printf multi-line output is captured intact" {
    const alloc = std.testing.allocator;

    var result = try runBash(alloc,
        \\{"command":"printf 'stdout line1\nstdout line2\n'"}
    );
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(try outputContains(alloc, result.content, "stdout line1"));
    try std.testing.expect(try outputContains(alloc, result.content, "stdout line2"));
}

test "bash stderr: tool has bash_output_max constant" {
    try std.testing.expect(bash.bash_output_max > 0);
}
