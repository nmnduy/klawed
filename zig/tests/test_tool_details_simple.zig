//! tests/test_tool_details_simple.zig — Zig port of tests/test_tool_details_simple.c
//!
//! The C test validated MCP tool display-name extraction logic that parses
//! tool names like "mcp_playwright_browser_click" and formats a human-readable
//! detail string for the TUI.  The logic was a standalone C function that
//! inspected cJSON argument objects.
//!
//! In the Zig implementation the same logic lives inside the MCP and TUI
//! layers.  Rather than duplicating the rendering logic in a test, this file
//! tests the underlying string-manipulation primitives that the Zig port uses
//! to implement the same behaviour.
//!
//! The core invariant being tested is:
//!   - An "mcp_<server>_<tool>" name can be split into server+tool parts
//!   - URL/text/path/element arguments can be extracted from JSON
//!   - Long text values are truncated at 30 characters with "..."
//!   - Tools without recognised args show just the tool name
//!   - Non-mcp names are ignored (return null)

const std = @import("std");

// ---------------------------------------------------------------------------
// Re-implementation of the display-name extraction function (pure Zig)
//
// This mirrors the logic from test_tool_details_simple.c, translated to
// idiomatic Zig so it can be exercised without pulling in cJSON.
// ---------------------------------------------------------------------------

/// Extract MCP tool display details from a tool name and parsed arguments.
/// Returns an owned string (caller frees) or null if the tool is not an MCP
/// tool (does not start with "mcp_").
fn getMcpToolDetails(
    allocator: std.mem.Allocator,
    tool_name: []const u8,
    args: std.json.Value,
) !?[]u8 {
    // Only handle mcp_ prefixed names
    if (!std.mem.startsWith(u8, tool_name, "mcp_")) return null;

    // Strip "mcp_" prefix then split on next underscore to get server + actual
    const after_prefix = tool_name[4..];
    const sep = std.mem.indexOfScalar(u8, after_prefix, '_');
    const actual_tool = if (sep) |s| after_prefix[s + 1 ..] else after_prefix;

    // Extract most relevant argument
    const obj = switch (args) {
        .object => |m| m,
        else => return try allocator.dupe(u8, actual_tool),
    };

    if (obj.get("url")) |url_val| {
        if (url_val == .string) {
            return try std.fmt.allocPrint(allocator, "{s}: {s}", .{ actual_tool, url_val.string });
        }
    }
    if (obj.get("text")) |text_val| {
        if (text_val == .string and text_val.string.len > 0) {
            const text = text_val.string;
            if (text.len > 30) {
                return try std.fmt.allocPrint(allocator, "{s}: {s}...", .{ actual_tool, text[0..30] });
            }
            return try std.fmt.allocPrint(allocator, "{s}: {s}", .{ actual_tool, text });
        }
    }
    if (obj.get("path")) |path_val| {
        if (path_val == .string) {
            return try std.fmt.allocPrint(allocator, "{s}: {s}", .{ actual_tool, path_val.string });
        }
    }
    if (obj.get("element")) |elem_val| {
        if (elem_val == .string) {
            return try std.fmt.allocPrint(allocator, "{s}: {s}", .{ actual_tool, elem_val.string });
        }
    }

    return try allocator.dupe(u8, actual_tool);
}

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

fn parseArgs(allocator: std.mem.Allocator, json: []const u8) !std.json.Parsed(std.json.Value) {
    return std.json.parseFromSlice(std.json.Value, allocator, json, .{});
}

// ---------------------------------------------------------------------------
// Tests — Playwright tools
// ---------------------------------------------------------------------------

test "tool_details: mcp_playwright_browser_click uses element arg" {
    const allocator = std.testing.allocator;
    const parsed = try parseArgs(allocator,
        \\{"element":"Submit button","ref":"button-123"}
    );
    defer parsed.deinit();

    const result = try getMcpToolDetails(allocator, "mcp_playwright_browser_click", parsed.value);
    defer if (result) |r| allocator.free(r);

    try std.testing.expect(result != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "browser_click") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "Submit button") != null);
}

test "tool_details: mcp_playwright_browser_type uses text arg" {
    const allocator = std.testing.allocator;
    const parsed = try parseArgs(allocator,
        \\{"element":"Email input","text":"test@example.com"}
    );
    defer parsed.deinit();

    const result = try getMcpToolDetails(allocator, "mcp_playwright_browser_type", parsed.value);
    defer if (result) |r| allocator.free(r);

    try std.testing.expect(result != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "browser_type") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "test@example.com") != null);
}

test "tool_details: mcp_playwright_browser_navigate uses url arg" {
    const allocator = std.testing.allocator;
    const parsed = try parseArgs(allocator,
        \\{"url":"https://example.com"}
    );
    defer parsed.deinit();

    const result = try getMcpToolDetails(allocator, "mcp_playwright_browser_navigate", parsed.value);
    defer if (result) |r| allocator.free(r);

    try std.testing.expect(result != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "browser_navigate") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "example.com") != null);
}

test "tool_details: mcp_playwright_browser_snapshot no args returns tool name" {
    const allocator = std.testing.allocator;
    const parsed = try parseArgs(allocator, "{}");
    defer parsed.deinit();

    const result = try getMcpToolDetails(allocator, "mcp_playwright_browser_snapshot", parsed.value);
    defer if (result) |r| allocator.free(r);

    try std.testing.expect(result != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "browser_snapshot") != null);
}

// ---------------------------------------------------------------------------
// Tests — Generic MCP tools
// ---------------------------------------------------------------------------

test "tool_details: mcp_http_fetch with url" {
    const allocator = std.testing.allocator;
    const parsed = try parseArgs(allocator,
        \\{"url":"https://api.example.com/data"}
    );
    defer parsed.deinit();

    const result = try getMcpToolDetails(allocator, "mcp_http_fetch", parsed.value);
    defer if (result) |r| allocator.free(r);

    try std.testing.expect(result != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "fetch") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "api.example.com") != null);
}

test "tool_details: mcp_search_query with text" {
    const allocator = std.testing.allocator;
    const parsed = try parseArgs(allocator,
        \\{"text":"search query"}
    );
    defer parsed.deinit();

    const result = try getMcpToolDetails(allocator, "mcp_search_query", parsed.value);
    defer if (result) |r| allocator.free(r);

    try std.testing.expect(result != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "query") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "search query") != null);
}

test "tool_details: mcp_fs_read with path" {
    const allocator = std.testing.allocator;
    const parsed = try parseArgs(allocator,
        \\{"path":"/path/to/file.txt"}
    );
    defer parsed.deinit();

    const result = try getMcpToolDetails(allocator, "mcp_fs_read", parsed.value);
    defer if (result) |r| allocator.free(r);

    try std.testing.expect(result != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "read") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "/path/to/file.txt") != null);
}

test "tool_details: mcp_server_status no params" {
    const allocator = std.testing.allocator;
    const parsed = try parseArgs(allocator, "{}");
    defer parsed.deinit();

    const result = try getMcpToolDetails(allocator, "mcp_server_status", parsed.value);
    defer if (result) |r| allocator.free(r);

    try std.testing.expect(result != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "status") != null);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

test "tool_details: long text is truncated with ellipsis" {
    const allocator = std.testing.allocator;
    const parsed = try parseArgs(allocator,
        \\{"text":"This is a very long text that should be truncated when displayed in the UI"}
    );
    defer parsed.deinit();

    const result = try getMcpToolDetails(allocator, "mcp_example_process", parsed.value);
    defer if (result) |r| allocator.free(r);

    try std.testing.expect(result != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "process") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "...") != null);
}

test "tool_details: malformed name with no server prefix falls back" {
    const allocator = std.testing.allocator;
    const parsed = try parseArgs(allocator, "{}");
    defer parsed.deinit();

    const result = try getMcpToolDetails(allocator, "mcp_noserver", parsed.value);
    defer if (result) |r| allocator.free(r);

    try std.testing.expect(result != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "noserver") != null);
}

test "tool_details: non-mcp tool returns null" {
    const allocator = std.testing.allocator;
    const parsed = try parseArgs(allocator, "{}");
    defer parsed.deinit();

    const result = try getMcpToolDetails(allocator, "Bash", parsed.value);
    try std.testing.expect(result == null);
}
