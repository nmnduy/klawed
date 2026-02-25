//! tools/utils.zig — Common utilities for tool implementations
//!
//! Zig port of src/tool_utils.c
//!
//! Provides:
//!   - `ToolResult` type with content + is_error flag
//!   - Result formatting helpers (ok, err, errFmt)
//!   - Output truncation

const std = @import("std");

// ---------------------------------------------------------------------------
// Core types
// ---------------------------------------------------------------------------

/// The result returned by every tool execute function.
/// `content` is an owned, allocator-managed slice.
/// When `owned` is false, `content` points to a compile-time constant
/// and must NOT be freed.
pub const ToolResult = struct {
    content: []const u8,
    is_error: bool = false,
    owned: bool = true,

    pub fn deinit(self: *ToolResult, allocator: std.mem.Allocator) void {
        if (self.owned) allocator.free(self.content);
    }
};

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

/// Build a successful ToolResult from a constant string literal.
/// The literal is *not* duplicated — safe to call `deinit` (no-op for literals).
pub fn okLit(comptime text: []const u8) ToolResult {
    return .{ .content = text, .is_error = false, .owned = false };
}

/// Build a successful ToolResult from an allocated string.
/// The caller transfers ownership of `content` to the result.
pub fn okOwned(content: []const u8) ToolResult {
    return .{ .content = content, .is_error = false };
}

/// Build a successful ToolResult by duplicating `text`.
pub fn ok(allocator: std.mem.Allocator, text: []const u8) !ToolResult {
    return .{ .content = try allocator.dupe(u8, text), .is_error = false };
}

/// Build an error ToolResult from a constant string literal.
/// Safe to call `deinit` on (no-op for literals).
pub fn errLit(comptime text: []const u8) ToolResult {
    return .{ .content = text, .is_error = true, .owned = false };
}

/// Build an error ToolResult by duplicating `text`.
pub fn err(allocator: std.mem.Allocator, text: []const u8) !ToolResult {
    return .{ .content = try allocator.dupe(u8, text), .is_error = true };
}

/// Build an error ToolResult by formatting a message.
pub fn errFmt(allocator: std.mem.Allocator, comptime fmt: []const u8, args: anytype) !ToolResult {
    const msg = try std.fmt.allocPrint(allocator, fmt, args);
    return .{ .content = msg, .is_error = true };
}

/// Build a successful ToolResult by formatting a message.
pub fn okFmt(allocator: std.mem.Allocator, comptime fmt: []const u8, args: anytype) !ToolResult {
    const msg = try std.fmt.allocPrint(allocator, fmt, args);
    return .{ .content = msg, .is_error = false };
}

// ---------------------------------------------------------------------------
// Truncation helper
// ---------------------------------------------------------------------------

/// Maximum byte length for tool output before truncation.
pub const default_max_output: usize = 12_228;

/// Truncate `output` to at most `max_bytes`, appending a warning line.
/// If `output.len <= max_bytes`, returns a duplicate (no truncation).
/// Caller owns the returned slice.
pub fn truncateOutput(
    allocator: std.mem.Allocator,
    output: []const u8,
    max_bytes: usize,
) ![]u8 {
    if (output.len <= max_bytes) {
        return allocator.dupe(u8, output);
    }
    // Hard-cut at max_bytes and append a warning.
    const warning = try std.fmt.allocPrint(
        allocator,
        "\n[Output truncated at {d} bytes (total: {d} bytes)]",
        .{ max_bytes, output.len },
    );
    defer allocator.free(warning);

    const result = try allocator.alloc(u8, max_bytes + warning.len);
    @memcpy(result[0..max_bytes], output[0..max_bytes]);
    @memcpy(result[max_bytes..], warning);
    return result;
}

// ---------------------------------------------------------------------------
// JSON helper — extract a string field from a parsed json.Value object
// ---------------------------------------------------------------------------

/// Get a string field from a JSON object, returning null if missing or wrong type.
pub fn jsonString(obj: std.json.Value, key: []const u8) ?[]const u8 {
    const map = switch (obj) {
        .object => |m| m,
        else => return null,
    };
    const v = map.get(key) orelse return null;
    return switch (v) {
        .string => |s| s,
        else => null,
    };
}

/// Get an integer field from a JSON object, returning null if missing or wrong type.
pub fn jsonInt(obj: std.json.Value, key: []const u8) ?i64 {
    const map = switch (obj) {
        .object => |m| m,
        else => return null,
    };
    const v = map.get(key) orelse return null;
    return switch (v) {
        .integer => |i| i,
        .float => |f| @as(i64, @intFromFloat(f)),
        else => null,
    };
}

/// Get a bool field from a JSON object.
pub fn jsonBool(obj: std.json.Value, key: []const u8) ?bool {
    const map = switch (obj) {
        .object => |m| m,
        else => return null,
    };
    const v = map.get(key) orelse return null;
    return switch (v) {
        .bool => |b| b,
        else => null,
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "ok/err constructors" {
    const allocator = std.testing.allocator;

    var r = try ok(allocator, "hello");
    defer r.deinit(allocator);
    try std.testing.expectEqualStrings("hello", r.content);
    try std.testing.expect(!r.is_error);

    var e = try err(allocator, "bad thing");
    defer e.deinit(allocator);
    try std.testing.expect(e.is_error);
}

test "okFmt / errFmt" {
    const allocator = std.testing.allocator;

    var r = try okFmt(allocator, "exit_code={d}", .{0});
    defer r.deinit(allocator);
    try std.testing.expectEqualStrings("exit_code=0", r.content);

    var e = try errFmt(allocator, "failed: {s}", .{"oops"});
    defer e.deinit(allocator);
    try std.testing.expect(e.is_error);
    try std.testing.expectEqualStrings("failed: oops", e.content);
}

test "truncateOutput: short output passes through" {
    const allocator = std.testing.allocator;
    const input = "short";
    const result = try truncateOutput(allocator, input, 100);
    defer allocator.free(result);
    try std.testing.expectEqualStrings(input, result);
}

test "truncateOutput: long output is truncated" {
    const allocator = std.testing.allocator;
    // Create a 20-byte string, truncate to 10
    const input = "0123456789abcdefghij";
    const result = try truncateOutput(allocator, input, 10);
    defer allocator.free(result);
    // First 10 bytes should match
    try std.testing.expectEqualSlices(u8, input[0..10], result[0..10]);
    // Should contain warning text
    try std.testing.expect(std.mem.indexOf(u8, result, "[Output truncated") != null);
}

test "jsonString helper" {
    const allocator = std.testing.allocator;
    const json_text =
        \\{"key": "value", "num": 42}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    try std.testing.expectEqualStrings("value", jsonString(parsed.value, "key").?);
    try std.testing.expect(jsonString(parsed.value, "num") == null);
    try std.testing.expect(jsonString(parsed.value, "missing") == null);
}

test "jsonInt helper" {
    const allocator = std.testing.allocator;
    const json_text =
        \\{"n": 42, "s": "hello"}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    try std.testing.expectEqual(@as(i64, 42), jsonInt(parsed.value, "n").?);
    try std.testing.expect(jsonInt(parsed.value, "s") == null);
}
