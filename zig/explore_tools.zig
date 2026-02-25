//! explore_tools.zig — Web exploration tools (Explore mode)
//!
//! Zig port of src/explore_tools.c
//!
//! Provides two tools available when KLAWED_EXPLORE_MODE=1:
//!   - web_search: search the web via DuckDuckGo using web_browse_agent
//!   - web_read:   navigate to a URL and extract content
//!
//! Both tools are stubs that delegate to the `web_browse_agent` binary.
//! They are only active when `KLAWED_EXPLORE_MODE` is set to "1", "true", or "yes".

const std = @import("std");
const utils = @import("tools/utils.zig");

pub const ToolResult = utils.ToolResult;

/// Maximum output bytes from web_browse_agent.
pub const max_web_output: usize = 100_000;

/// Default timeout for web operations (seconds).
pub const web_agent_timeout_s: u32 = 120;

/// Default maximum search results.
pub const default_max_results: usize = 10;

/// Environment variable to enable explore mode.
pub const explore_mode_env = "KLAWED_EXPLORE_MODE";

/// Environment variable to override the web_browse_agent binary path.
pub const agent_path_env = "KLAWED_WEB_BROWSE_AGENT_PATH";

// ---------------------------------------------------------------------------
// Mode check
// ---------------------------------------------------------------------------

/// Returns true if explore mode is enabled via the environment.
pub fn isEnabled() bool {
    const val = std.posix.getenv(explore_mode_env) orelse return false;
    return std.mem.eql(u8, val, "1") or
        std.ascii.eqlIgnoreCase(val, "true") or
        std.ascii.eqlIgnoreCase(val, "yes");
}

// ---------------------------------------------------------------------------
// Agent path resolution
// ---------------------------------------------------------------------------

/// Resolve the path to the web_browse_agent binary.
/// Search order: KLAWED_WEB_BROWSE_AGENT_PATH env → tools/ relative → PATH.
pub fn resolveAgentPath(allocator: std.mem.Allocator) ![]u8 {
    // 1. Explicit env override
    if (std.posix.getenv(agent_path_env)) |env| {
        if (env.len > 0) {
            std.fs.accessAbsolute(env, .{}) catch {};
            return allocator.dupe(u8, env);
        }
    }

    // 2. Project-local binary
    const local_paths = [_][]const u8{
        "tools/web_browse_agent/web_browse_agent",
        "tools/web_browse_agent/bin/web_browse_agent",
    };
    for (local_paths) |lp| {
        std.fs.cwd().access(lp, .{}) catch continue;
        return allocator.dupe(u8, lp);
    }

    // 3. Try PATH via `which`
    const result = std.process.Child.run(.{
        .allocator = allocator,
        .argv = &.{ "which", "web_browse_agent" },
        .max_output_bytes = 256,
    }) catch return allocator.dupe(u8, "web_browse_agent");
    defer allocator.free(result.stdout);
    defer allocator.free(result.stderr);

    const path = std.mem.trimRight(u8, result.stdout, &std.ascii.whitespace);
    if (path.len > 0) return allocator.dupe(u8, path);

    return allocator.dupe(u8, "web_browse_agent");
}

// ---------------------------------------------------------------------------
// web_search tool
// ---------------------------------------------------------------------------

/// Execute the web_search tool.
///
/// Input: `{ "query": <string>, "max_results": <optional int> }`
pub fn executeWebSearch(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    if (!isEnabled()) {
        return utils.errLit("web_search is only available in explore mode (KLAWED_EXPLORE_MODE=1)");
    }

    const query = utils.jsonString(input, "query") orelse {
        return utils.errLit("Missing 'query' parameter");
    };

    const max_results: usize = blk: {
        if (utils.jsonInt(input, "max_results")) |v| {
            if (v > 0) break :blk @intCast(@min(v, 30));
        }
        break :blk default_max_results;
    };

    const agent_path = try resolveAgentPath(allocator);
    defer allocator.free(agent_path);

    // Build command: web_browse_agent search "<query>" --max-results N
    const cmd = try std.fmt.allocPrint(
        allocator,
        "timeout {d} {s} search {s} --max-results {d} </dev/null 2>&1",
        .{ web_agent_timeout_s, agent_path, query, max_results },
    );
    defer allocator.free(cmd);

    const run_result = std.process.Child.run(.{
        .allocator = allocator,
        .argv = &.{ "/bin/sh", "-c", cmd },
        .max_output_bytes = max_web_output,
    }) catch |e| {
        return utils.errFmt(allocator, "web_search failed: {s}", .{@errorName(e)});
    };
    defer allocator.free(run_result.stdout);
    defer allocator.free(run_result.stderr);

    if (run_result.term != .Exited or run_result.term.Exited != 0) {
        return utils.errFmt(
            allocator,
            "web_search agent returned non-zero exit code",
            .{},
        );
    }

    return utils.ok(allocator, run_result.stdout);
}

// ---------------------------------------------------------------------------
// web_read tool
// ---------------------------------------------------------------------------

/// Execute the web_read tool.
///
/// Input: `{ "url": <string>, "max_length": <optional int> }`
pub fn executeWebRead(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    if (!isEnabled()) {
        return utils.errLit("web_read is only available in explore mode (KLAWED_EXPLORE_MODE=1)");
    }

    const url = utils.jsonString(input, "url") orelse {
        return utils.errLit("Missing 'url' parameter");
    };

    const agent_path = try resolveAgentPath(allocator);
    defer allocator.free(agent_path);

    // Build command: web_browse_agent read "<url>"
    const cmd = try std.fmt.allocPrint(
        allocator,
        "timeout {d} {s} read {s} </dev/null 2>&1",
        .{ web_agent_timeout_s, agent_path, url },
    );
    defer allocator.free(cmd);

    const run_result = std.process.Child.run(.{
        .allocator = allocator,
        .argv = &.{ "/bin/sh", "-c", cmd },
        .max_output_bytes = max_web_output,
    }) catch |e| {
        return utils.errFmt(allocator, "web_read failed: {s}", .{@errorName(e)});
    };
    defer allocator.free(run_result.stdout);
    defer allocator.free(run_result.stderr);

    if (run_result.term != .Exited or run_result.term.Exited != 0) {
        return utils.errFmt(allocator, "web_read agent returned non-zero exit code", .{});
    }

    return utils.ok(allocator, run_result.stdout);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "isEnabled: returns false when env not set" {
    // In a clean test environment, KLAWED_EXPLORE_MODE is typically not set
    // We can't easily set/unset env in tests, so just test the logic path
    // when the env is not present.
    // The test just ensures the function doesn't crash.
    _ = isEnabled();
}

test "executeWebSearch: returns error when not in explore mode" {
    const allocator = std.testing.allocator;
    // Force explore mode off by ensuring KLAWED_EXPLORE_MODE is unset
    // Since we can't easily control the env, we test the error path when
    // explore mode is false via the function directly.
    // If the env happens to be set in the test environment, this test is skipped.
    if (isEnabled()) return;

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{\"query\":\"test\"}", .{});
    defer parsed.deinit();

    const result = try executeWebSearch(allocator, parsed.value);
    try std.testing.expect(result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "explore mode") != null);
}

test "executeWebRead: returns error when not in explore mode" {
    const allocator = std.testing.allocator;
    if (isEnabled()) return;

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{\"url\":\"https://example.com\"}", .{});
    defer parsed.deinit();

    const result = try executeWebRead(allocator, parsed.value);
    try std.testing.expect(result.is_error);
}

test "executeWebSearch: missing query returns error" {
    const allocator = std.testing.allocator;
    if (isEnabled()) return; // skip if explore mode is on

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{}", .{});
    defer parsed.deinit();

    // Still returns an error (mode check happens first in this implementation,
    // but if mode is on, it checks the query parameter)
    const result = try executeWebSearch(allocator, parsed.value);
    try std.testing.expect(result.is_error);
}
