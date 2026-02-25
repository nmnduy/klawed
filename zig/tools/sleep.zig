//! tools/sleep.zig — Sleep tool implementation
//!
//! Zig port of src/tools/tool_sleep.c
//!
//! Sleeps for the given number of seconds, then returns a success result.

const std = @import("std");
const utils = @import("utils.zig");

pub const ToolResult = utils.ToolResult;

/// Execute the Sleep tool.
///
/// Expected input JSON: `{ "duration": <seconds> }`
pub fn execute(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const duration_val = utils.jsonInt(input, "duration") orelse {
        return utils.errLit("Missing or invalid 'duration' parameter (must be number of seconds)");
    };

    const duration: u64 = if (duration_val < 0) 0 else @intCast(duration_val);
    const duration_ns: u64 = duration * std.time.ns_per_s;

    std.time.sleep(duration_ns);

    return utils.okFmt(
        allocator,
        "{{\"status\":\"success\",\"duration\":{d}}}",
        .{duration},
    );
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "sleep tool: missing parameter returns error" {
    const allocator = std.testing.allocator;
    const json_text = "{}";
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    const result = try execute(allocator, parsed.value);
    // No need to deinit — errLit returns a compile-time literal with no ownership
    try std.testing.expect(result.is_error);
}

test "sleep tool: zero duration returns success" {
    const allocator = std.testing.allocator;
    const json_text = "{\"duration\": 0}";
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try execute(allocator, parsed.value);
    defer result.deinit(allocator);
    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "success") != null);
}

test "sleep tool: negative duration is treated as zero" {
    const allocator = std.testing.allocator;
    const json_text = "{\"duration\": -5}";
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try execute(allocator, parsed.value);
    defer result.deinit(allocator);
    try std.testing.expect(!result.is_error);
}
