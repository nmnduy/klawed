//! tests/test_bash_truncation.zig — Zig port of tests/test_bash_truncation.c
//!
//! Tests the Bash tool's output truncation feature.

const std = @import("std");
const bash = @import("../tools/bash.zig");

fn runBash(alloc: std.mem.Allocator, json: []const u8) !bash.ToolResult {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();
    return bash.execute(alloc, parsed.value);
}

fn hasField(alloc: std.mem.Allocator, json: []const u8, field: []const u8) !bool {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();
    const obj = switch (parsed.value) {
        .object => |m| m,
        else => return false,
    };
    return obj.get(field) != null;
}

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

test "bash truncation: bash_output_max constant is 12228" {
    try std.testing.expectEqual(@as(usize, 12_228), bash.bash_output_max);
}

test "bash truncation: small output — no truncation_warning" {
    const alloc = std.testing.allocator;
    var result = try runBash(alloc, "{\"command\":\"echo 'Hello World'\"}");
    defer result.deinit(alloc);
    try std.testing.expect(!result.is_error);
    const has_trunc = try hasField(alloc, result.content, "truncation_warning");
    try std.testing.expect(!has_trunc);
    const out = try jsonStr(alloc, result.content, "output");
    defer if (out) |o| alloc.free(o);
    try std.testing.expect(out != null);
    try std.testing.expect(std.mem.indexOf(u8, out.?, "Hello World") != null);
}

test "bash truncation: output exceeding limit triggers truncation_warning" {
    const alloc = std.testing.allocator;
    // python3 avoids pipe/redirect issues that affect printf|tr
    var result = try runBash(alloc,
        \\{"command":"python3 -c \"print('x' * 15000)\""}
    );
    defer result.deinit(alloc);
    try std.testing.expect(!result.is_error);
    const has_trunc = try hasField(alloc, result.content, "truncation_warning");
    try std.testing.expect(has_trunc);
    const warning = try jsonStr(alloc, result.content, "truncation_warning");
    defer if (warning) |w| alloc.free(w);
    try std.testing.expect(warning != null);
    try std.testing.expect(std.mem.indexOf(u8, warning.?, "bytes") != null);
}

test "bash truncation: truncated output length is at or below limit" {
    const alloc = std.testing.allocator;
    var result = try runBash(alloc,
        \\{"command":"python3 -c \"print('x' * 20000)\""}
    );
    defer result.deinit(alloc);
    try std.testing.expect(!result.is_error);
    const out = try jsonStr(alloc, result.content, "output");
    defer if (out) |o| alloc.free(o);
    try std.testing.expect(out != null);
    try std.testing.expect(out.?.len <= bash.bash_output_max);
}

test "bash truncation: combined large stdout exceeding limit is truncated" {
    const alloc = std.testing.allocator;
    var result = try runBash(alloc,
        \\{"command":"python3 -c \"print('x' * 14000)\""}
    );
    defer result.deinit(alloc);
    try std.testing.expect(!result.is_error);
    const has_trunc = try hasField(alloc, result.content, "truncation_warning");
    try std.testing.expect(has_trunc);
    const out = try jsonStr(alloc, result.content, "output");
    defer if (out) |o| alloc.free(o);
    try std.testing.expect(out != null);
    try std.testing.expect(out.?.len <= bash.bash_output_max);
}
