//! Environment Utilities
//!
//! Idiomatic Zig replacements for src/util/env_utils.c
//!
//! Key C→Zig translations:
//!   - `get_env_int_retry`  → `getEnvInt`    (returns value with default, no silent default hiding)
//!   - `get_platform`       → `platform`     (comptime constant, no runtime dispatch)
//!   - `get_os_version`     → `osVersion`    (runs `uname -sr`)
//!   - `exec_shell_command` → `execShell`    (ArrayList-based, no manual realloc)

const std = @import("std");
const builtin = @import("builtin");

// ---------------------------------------------------------------------------
// Platform detection (comptime — zero runtime cost)
// ---------------------------------------------------------------------------

/// Platform identifier string, resolved at compile time.
/// Matches the values returned by the C `get_platform()` function.
pub const platform: []const u8 = switch (builtin.os.tag) {
    .macos => "darwin",
    .linux => "linux",
    .windows => "win32",
    .freebsd => "freebsd",
    .openbsd => "openbsd",
    else => "unknown",
};

// ---------------------------------------------------------------------------
// Environment variable helpers
// ---------------------------------------------------------------------------

/// Read an environment variable and parse it as a non-negative integer.
///
/// Returns `default_value` when:
///   - the variable is unset or empty
///   - the value is not a valid base-10 integer
///   - the value is negative or exceeds `std.math.maxInt(i32)`
///
/// Unlike the C version this never logs internally — callers decide how to
/// handle an invalid value.
pub fn getEnvInt(allocator: std.mem.Allocator, name: []const u8, default_value: i32) i32 {
    const raw = std.process.getEnvVarOwned(allocator, name) catch |err| switch (err) {
        error.EnvironmentVariableNotFound => return default_value,
        else => return default_value,
    };
    defer allocator.free(raw);

    if (raw.len == 0) return default_value;

    const parsed = std.fmt.parseInt(i64, raw, 10) catch return default_value;
    if (parsed < 0 or parsed > std.math.maxInt(i32)) return default_value;

    return @intCast(parsed);
}

/// Read an environment variable as a string, returning `null` when unset.
/// Caller must free the returned slice with `allocator.free`.
pub fn getEnvOwned(allocator: std.mem.Allocator, name: []const u8) !?[]u8 {
    return std.process.getEnvVarOwned(allocator, name) catch |err| switch (err) {
        error.EnvironmentVariableNotFound => return null,
        else => return err,
    };
}

/// Read an environment variable as a boolean.
/// Truthy values (case-insensitive): "1", "true", "yes", "on".
/// Returns `default_value` when unset or empty.
pub fn getEnvBool(allocator: std.mem.Allocator, name: []const u8, default_value: bool) bool {
    const raw = std.process.getEnvVarOwned(allocator, name) catch |err| switch (err) {
        error.EnvironmentVariableNotFound => return default_value,
        else => return default_value,
    };
    defer allocator.free(raw);

    if (raw.len == 0) return default_value;
    var lower_buf: [16]u8 = undefined;
    if (raw.len > lower_buf.len) return default_value;
    const lower = std.ascii.lowerString(lower_buf[0..raw.len], raw);
    return std.mem.eql(u8, lower, "1") or
        std.mem.eql(u8, lower, "true") or
        std.mem.eql(u8, lower, "yes") or
        std.mem.eql(u8, lower, "on");
}

// ---------------------------------------------------------------------------
// Shell execution
// ---------------------------------------------------------------------------

/// Execute a shell command and return its stdout as a string.
/// Trailing newlines are stripped (matching C `exec_shell_command` behaviour).
/// Caller must free the result with `allocator.free`.
pub fn execShell(allocator: std.mem.Allocator, command: []const u8) ![]u8 {
    const result = try std.ChildProcess.run(.{
        .allocator = allocator,
        .argv = &.{ "/bin/sh", "-c", command },
    });
    defer allocator.free(result.stderr);

    var out = result.stdout;
    while (out.len > 0 and (out[out.len - 1] == '\n' or out[out.len - 1] == '\r')) {
        out = out[0 .. out.len - 1];
    }
    const trimmed = try allocator.dupe(u8, out);
    allocator.free(result.stdout);
    return trimmed;
}

/// Return the OS version string from `uname -sr`.
/// Caller must free the result with `allocator.free`.
pub fn osVersion(allocator: std.mem.Allocator) ![]u8 {
    return execShell(allocator, "uname -sr 2>/dev/null") catch
        allocator.dupe(u8, "Unknown");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

const c_stdlib = @cImport(@cInclude("stdlib.h"));

test "platform: is a known string" {
    const valid = [_][]const u8{ "darwin", "linux", "win32", "freebsd", "openbsd", "unknown" };
    var found = false;
    for (valid) |v| {
        if (std.mem.eql(u8, platform, v)) {
            found = true;
            break;
        }
    }
    try std.testing.expect(found);
}

test "getEnvInt: missing var returns default" {
    const a = std.testing.allocator;
    const val = getEnvInt(a, "KLAWED_TEST_MISSING_VAR_XYZ_QQQ", 42);
    try std.testing.expectEqual(@as(i32, 42), val);
}

test "getEnvInt: valid var is parsed" {
    const a = std.testing.allocator;
    _ = c_stdlib.setenv("KLAWED_TEST_INT_VAR", "99", 1);
    defer _ = c_stdlib.unsetenv("KLAWED_TEST_INT_VAR");
    const val = getEnvInt(a, "KLAWED_TEST_INT_VAR", 0);
    try std.testing.expectEqual(@as(i32, 99), val);
}

test "getEnvInt: invalid string returns default" {
    const a = std.testing.allocator;
    _ = c_stdlib.setenv("KLAWED_TEST_INT_VAR2", "notanumber", 1);
    defer _ = c_stdlib.unsetenv("KLAWED_TEST_INT_VAR2");
    const val = getEnvInt(a, "KLAWED_TEST_INT_VAR2", 7);
    try std.testing.expectEqual(@as(i32, 7), val);
}

test "getEnvInt: negative value returns default" {
    const a = std.testing.allocator;
    _ = c_stdlib.setenv("KLAWED_TEST_INT_VAR3", "-5", 1);
    defer _ = c_stdlib.unsetenv("KLAWED_TEST_INT_VAR3");
    const val = getEnvInt(a, "KLAWED_TEST_INT_VAR3", 3);
    try std.testing.expectEqual(@as(i32, 3), val);
}

test "getEnvBool: truthy values" {
    const a = std.testing.allocator;
    for ([_][]const u8{ "1", "true", "yes", "on" }) |v| {
        _ = c_stdlib.setenv("KLAWED_TEST_BOOL_VAR", v.ptr, 1);
        defer _ = c_stdlib.unsetenv("KLAWED_TEST_BOOL_VAR");
        try std.testing.expect(getEnvBool(a, "KLAWED_TEST_BOOL_VAR", false));
    }
}

test "getEnvBool: falsy values" {
    const a = std.testing.allocator;
    for ([_][]const u8{ "0", "false", "no", "off" }) |v| {
        _ = c_stdlib.setenv("KLAWED_TEST_BOOL_VAR2", v.ptr, 1);
        defer _ = c_stdlib.unsetenv("KLAWED_TEST_BOOL_VAR2");
        try std.testing.expect(!getEnvBool(a, "KLAWED_TEST_BOOL_VAR2", true));
    }
}

test "getEnvBool: missing returns default" {
    const a = std.testing.allocator;
    try std.testing.expect(getEnvBool(a, "KLAWED_TEST_BOOL_MISSING_XYZ", true));
    try std.testing.expect(!getEnvBool(a, "KLAWED_TEST_BOOL_MISSING_XYZ", false));
}

test "execShell: echo command strips trailing newline" {
    const a = std.testing.allocator;
    const out = try execShell(a, "echo hello");
    defer a.free(out);
    try std.testing.expectEqualStrings("hello", out);
}

test "execShell: multi-word output" {
    const a = std.testing.allocator;
    const out = try execShell(a, "echo 'foo bar'");
    defer a.free(out);
    try std.testing.expectEqualStrings("foo bar", out);
}
