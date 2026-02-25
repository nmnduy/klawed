//! tests/test_token_usage.zig — Zig port of tests/test_token_usage.c
//!
//! Tests the TokenUsageDb API: init, log, retrieve session usage,
//! getLastPromptTokens, getLastCachedTokens, and multi-provider token formats.
//!
//! The original C test also parsed raw JSON to verify token extraction logic
//! for Moonshot/OpenAI/Anthropic providers.  Those parsing tests are
//! re-expressed as pure-Zig tests using the standard library JSON parser.

const std = @import("std");
const token_usage_db = @import("../token_usage_db.zig");

const TokenUsageDb = token_usage_db.TokenUsageDb;
const TokenUsageRecord = token_usage_db.TokenUsageRecord;

// ---------------------------------------------------------------------------
// Helper: open a test DB via tmp dir
// ---------------------------------------------------------------------------

fn openDb(alloc: std.mem.Allocator, tmp: std.testing.TmpDir, name: []const u8) !TokenUsageDb {
    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, name });
    defer alloc.free(path);
    return TokenUsageDb.init(alloc, path);
}

// ---------------------------------------------------------------------------
// Init and deinit
// ---------------------------------------------------------------------------

test "token_usage: init and deinit" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var db = try openDb(alloc, tmp, "tu1.db");
    defer db.deinit();
}

// ---------------------------------------------------------------------------
// Log and retrieve
// ---------------------------------------------------------------------------

test "token_usage: log and getSessionUsage" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var db = try openDb(alloc, tmp, "tu2.db");
    defer db.deinit();

    try db.log(1, "sess-abc", .{
        .prompt_tokens = 100,
        .completion_tokens = 50,
        .total_tokens = 150,
        .cached_tokens = 20,
        .prompt_cache_hit_tokens = 20,
        .prompt_cache_miss_tokens = 80,
    });

    const usage = try db.getSessionUsage("sess-abc");
    try std.testing.expectEqual(@as(i32, 100), usage.prompt);
    try std.testing.expectEqual(@as(i32, 50), usage.completion);
    try std.testing.expectEqual(@as(i32, 20), usage.cached);
}

// ---------------------------------------------------------------------------
// getLastPromptTokens
// ---------------------------------------------------------------------------

test "token_usage: getLastPromptTokens returns 0 when no records" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var db = try openDb(alloc, tmp, "tu3.db");
    defer db.deinit();

    const result = try db.getLastPromptTokens("sess-empty");
    try std.testing.expectEqual(@as(i32, 0), result);
}

test "token_usage: getLastPromptTokens returns last value" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var db = try openDb(alloc, tmp, "tu4.db");
    defer db.deinit();

    try db.log(0, "sess-x", .{ .prompt_tokens = 777 });

    const result = try db.getLastPromptTokens("sess-x");
    try std.testing.expectEqual(@as(i32, 777), result);
}

test "token_usage: getLastPromptTokens returns most-recent when multiple records" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var db = try openDb(alloc, tmp, "tu5.db");
    defer db.deinit();

    try db.log(0, "sess-y", .{ .prompt_tokens = 100 });
    try db.log(0, "sess-y", .{ .prompt_tokens = 200 });
    try db.log(0, "sess-y", .{ .prompt_tokens = 300 });

    const result = try db.getLastPromptTokens("sess-y");
    try std.testing.expectEqual(@as(i32, 300), result);
}

// ---------------------------------------------------------------------------
// getLastCachedTokens
// ---------------------------------------------------------------------------

test "token_usage: getLastCachedTokens returns 0 when no records" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var db = try openDb(alloc, tmp, "tu6.db");
    defer db.deinit();

    const result = try db.getLastCachedTokens("sess-empty");
    try std.testing.expectEqual(@as(i32, 0), result);
}

test "token_usage: getLastCachedTokens returns cached_tokens field" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var db = try openDb(alloc, tmp, "tu7.db");
    defer db.deinit();

    try db.log(1, "sess-c", .{
        .cached_tokens = 512,
        .prompt_cache_hit_tokens = 0,
    });

    const result = try db.getLastCachedTokens("sess-c");
    try std.testing.expectEqual(@as(i32, 512), result);
}

test "token_usage: getLastCachedTokens falls back to prompt_cache_hit_tokens" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var db = try openDb(alloc, tmp, "tu8.db");
    defer db.deinit();

    // cached_tokens = 0, but prompt_cache_hit_tokens is set
    try db.log(1, "sess-h", .{
        .cached_tokens = 0,
        .prompt_cache_hit_tokens = 256,
    });

    const result = try db.getLastCachedTokens("sess-h");
    try std.testing.expectEqual(@as(i32, 256), result);
}

// ---------------------------------------------------------------------------
// Null session_id
// ---------------------------------------------------------------------------

test "token_usage: log with null session_id succeeds" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var db = try openDb(alloc, tmp, "tu9.db");
    defer db.deinit();

    try db.log(0, null, .{ .prompt_tokens = 42 });

    const result = try db.getLastPromptTokens(null);
    try std.testing.expectEqual(@as(i32, 42), result);
}

// ---------------------------------------------------------------------------
// Multi-provider token format tests (pure Zig, ported from test_token_usage.c)
// ---------------------------------------------------------------------------

/// Simulate token extraction from an API response JSON string.
/// Returns .{ prompt, completion, cached }.
fn extractTokensFromJson(
    alloc: std.mem.Allocator,
    json_str: []const u8,
) !struct { prompt: i64, completion: i64, cached: i64 } {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json_str, .{});
    defer parsed.deinit();

    const root = parsed.value.object;
    const usage = root.get("usage") orelse return .{ .prompt = 0, .completion = 0, .cached = 0 };
    const usage_obj = usage.object;

    const prompt = blk: {
        if (usage_obj.get("prompt_tokens")) |v| break :blk v.integer;
        break :blk @as(i64, 0);
    };
    const completion = blk: {
        if (usage_obj.get("completion_tokens")) |v| break :blk v.integer;
        break :blk @as(i64, 0);
    };

    // Cached: try Moonshot direct, then DeepSeek nested, then Anthropic style
    var cached: i64 = 0;
    if (usage_obj.get("cached_tokens")) |v| {
        cached = v.integer;
    } else if (usage_obj.get("prompt_tokens_details")) |details| {
        if (details.object.get("cached_tokens")) |v| {
            cached = v.integer;
        }
    } else if (usage_obj.get("cache_read_input_tokens")) |v| {
        cached = v.integer;
    }

    return .{ .prompt = prompt, .completion = completion, .cached = cached };
}

test "token_usage: Moonshot-style cached_tokens" {
    const alloc = std.testing.allocator;
    const json =
        \\{"usage":{"prompt_tokens":1551,"completion_tokens":232,"total_tokens":1783,"cached_tokens":768}}
    ;
    const result = try extractTokensFromJson(alloc, json);
    try std.testing.expectEqual(@as(i64, 1551), result.prompt);
    try std.testing.expectEqual(@as(i64, 232), result.completion);
    try std.testing.expectEqual(@as(i64, 768), result.cached);
}

test "token_usage: OpenAI response no cache" {
    const alloc = std.testing.allocator;
    const json =
        \\{"usage":{"prompt_tokens":100,"completion_tokens":50,"total_tokens":150}}
    ;
    const result = try extractTokensFromJson(alloc, json);
    try std.testing.expectEqual(@as(i64, 100), result.prompt);
    try std.testing.expectEqual(@as(i64, 50), result.completion);
    try std.testing.expectEqual(@as(i64, 0), result.cached);
}

test "token_usage: Anthropic cache_read_input_tokens" {
    const alloc = std.testing.allocator;
    const json =
        \\{"usage":{"prompt_tokens":200,"completion_tokens":75,"total_tokens":275,"cache_read_input_tokens":150}}
    ;
    const result = try extractTokensFromJson(alloc, json);
    try std.testing.expectEqual(@as(i64, 200), result.prompt);
    try std.testing.expectEqual(@as(i64, 75), result.completion);
    try std.testing.expectEqual(@as(i64, 150), result.cached);
}

test "token_usage: DeepSeek prompt_tokens_details.cached_tokens" {
    const alloc = std.testing.allocator;
    const json =
        \\{"usage":{"prompt_tokens":37667,"completion_tokens":25,"total_tokens":37692,"prompt_tokens_details":{"cached_tokens":37632}}}
    ;
    const result = try extractTokensFromJson(alloc, json);
    try std.testing.expectEqual(@as(i64, 37667), result.prompt);
    try std.testing.expectEqual(@as(i64, 25), result.completion);
    try std.testing.expectEqual(@as(i64, 37632), result.cached);
}
