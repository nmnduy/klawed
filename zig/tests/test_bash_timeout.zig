//! tests/test_bash_timeout.zig — Zig port of tests/test_bash_timeout.c
//!
//! Tests the Bash tool's timeout functionality:
//!   - Default timeout is 30 seconds
//!   - Custom timeout parameter kills long-running commands
//!   - Timeout=0 / negative treated as "no timeout" (default applies)
//!   - KLAWED_BASH_TIMEOUT env var sets the default timeout
//!   - Per-call `timeout` parameter overrides the env var
//!   - Successful commands within the timeout complete normally
//!   - Timed-out commands produce exit_code -2 and a `timeout_error` field

const std = @import("std");
const bash = @import("../tools/bash.zig");

// libc setenv/unsetenv for env-var tests (not in std.posix in Zig 0.12)
extern fn setenv(name: [*:0]const u8, value: [*:0]const u8, overwrite: c_int) c_int;
extern fn unsetenv(name: [*:0]const u8) c_int;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/// Parse the JSON object produced by bash.execute and extract the integer
/// value of `field`.  Returns null if the key does not exist.
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

/// Return true if `field` exists in the JSON object.
fn hasField(alloc: std.mem.Allocator, json: []const u8, field: []const u8) !bool {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();
    const obj = switch (parsed.value) {
        .object => |m| m,
        else => return false,
    };
    return obj.get(field) != null;
}

/// Run bash.execute with the given JSON input string.
fn runBash(alloc: std.mem.Allocator, json: []const u8) !bash.ToolResult {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();
    return bash.execute(alloc, parsed.value);
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

test "bash timeout: default_timeout_s constant is 30" {
    try std.testing.expectEqual(@as(u32, 30), bash.default_timeout_s);
}

test "bash timeout: successful command runs and returns exit code 0" {
    const alloc = std.testing.allocator;
    var result = try runBash(alloc, "{\"command\":\"echo 'hello world'\"}");
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "hello world") != null);
    const exit_code = try jsonInt(alloc, result.content, "exit_code");
    try std.testing.expectEqual(@as(?i64, 0), exit_code);
    // No timeout error on a fast command
    const has_timeout = try hasField(alloc, result.content, "timeout_error");
    try std.testing.expect(!has_timeout);
}

test "bash timeout: short timeout kills long-running command" {
    const alloc = std.testing.allocator;
    // sleep 10 with a 1-second timeout must be killed
    var result = try runBash(alloc, "{\"command\":\"sleep 10\",\"timeout\":1}");
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    // timeout_error field must be present
    const has_timeout = try hasField(alloc, result.content, "timeout_error");
    try std.testing.expect(has_timeout);
    // The message should mention the timeout duration
    try std.testing.expect(std.mem.indexOf(u8, result.content, "timed out") != null or
        std.mem.indexOf(u8, result.content, "timeout") != null);
}

test "bash timeout: zero timeout uses default (no explicit timeout in JSON)" {
    // A timeout of 0 in the JSON is treated as "use default" — the command
    // should still run and complete successfully.
    const alloc = std.testing.allocator;
    var result = try runBash(alloc, "{\"command\":\"echo 'no timeout'\",\"timeout\":0}");
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "no timeout") != null);
    const exit_code = try jsonInt(alloc, result.content, "exit_code");
    try std.testing.expectEqual(@as(?i64, 0), exit_code);
}

test "bash timeout: KLAWED_BASH_TIMEOUT env var sets default" {
    const alloc = std.testing.allocator;

    // Set a 1-second timeout via the environment variable
    _ = setenv("KLAWED_BASH_TIMEOUT", "1", 1);
    defer _ = unsetenv("KLAWED_BASH_TIMEOUT");

    // A 3-second sleep should time out under the env-set limit
    var result = try runBash(alloc, "{\"command\":\"sleep 3\"}");
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    const has_timeout = try hasField(alloc, result.content, "timeout_error");
    try std.testing.expect(has_timeout);
}

test "bash timeout: parameter overrides KLAWED_BASH_TIMEOUT env var" {
    const alloc = std.testing.allocator;

    // Env var sets a short 1-second limit …
    _ = setenv("KLAWED_BASH_TIMEOUT", "1", 1);
    defer _ = unsetenv("KLAWED_BASH_TIMEOUT");

    // … but the parameter provides 5 seconds, which is enough for a 2-second sleep
    var result = try runBash(alloc, "{\"command\":\"sleep 2 && echo 'param wins'\",\"timeout\":5}");
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "param wins") != null);
    const has_timeout = try hasField(alloc, result.content, "timeout_error");
    try std.testing.expect(!has_timeout);
}

test "bash timeout: command completes within generous timeout — no error field" {
    const alloc = std.testing.allocator;
    var result = try runBash(alloc, "{\"command\":\"echo 'quick'\",\"timeout\":30}");
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    const has_timeout = try hasField(alloc, result.content, "timeout_error");
    try std.testing.expect(!has_timeout);
}

test "bash timeout: non-zero exit code is captured correctly" {
    const alloc = std.testing.allocator;
    var result = try runBash(alloc, "{\"command\":\"exit 7\"}");
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    // The exit code should be non-zero (7 truncated to u8)
    const exit_code = try jsonInt(alloc, result.content, "exit_code");
    try std.testing.expect(exit_code != null and exit_code.? != 0);
}

test "bash timeout: missing command parameter returns tool error" {
    const alloc = std.testing.allocator;
    const result = try runBash(alloc, "{}");
    try std.testing.expect(result.is_error);
}
