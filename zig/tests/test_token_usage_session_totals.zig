//! tests/test_token_usage_session_totals.zig — Zig port of tests/test_token_usage_session_totals.c
//!
//! Verifies session token aggregation in the TokenUsageDb:
//! - log() inserts token usage records for a session
//! - getSessionUsage() returns the latest (most-recent) cumulative record
//! - getLastPromptTokens() returns the latest prompt token count
//! - getLastCachedTokens() returns the latest cached token count
//! - NULL/all-sessions query returns the same as single-session when only one exists

const std = @import("std");
const token_db = @import("../token_usage_db.zig");

const TokenUsageDb = token_db.TokenUsageDb;
const TokenUsageRecord = token_db.TokenUsageRecord;

// ============================================================================
// Helper: create a temporary TokenUsageDb
// ============================================================================

fn openTempDb(alloc: std.mem.Allocator, tmp: std.testing.TmpDir) !struct {
    db: TokenUsageDb,
    path: []u8,
} {
    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "token_usage_test.db" });
    const db = try TokenUsageDb.init(alloc, path);
    return .{ .db = db, .path = path };
}

// ============================================================================
// Test 1: Log and retrieve session usage
// ============================================================================

test "TokenUsageDb: log and getSessionUsage returns latest record" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var t = try openTempDb(alloc, tmp);
    defer alloc.free(t.path);
    defer t.db.deinit();

    // First API call: 100 prompt, 25 completion, 10 cached
    try t.db.log(1, "session-1", TokenUsageRecord{
        .prompt_tokens = 100,
        .completion_tokens = 25,
        .total_tokens = 125,
        .cached_tokens = 10,
        .prompt_cache_hit_tokens = 10,
        .prompt_cache_miss_tokens = 90,
    });

    // Second API call: 140 prompt, 40 completion, 15 cached (cumulative)
    try t.db.log(2, "session-1", TokenUsageRecord{
        .prompt_tokens = 140,
        .completion_tokens = 40,
        .total_tokens = 180,
        .cached_tokens = 15,
        .prompt_cache_hit_tokens = 15,
        .prompt_cache_miss_tokens = 125,
    });

    // Per-session totals — should get the latest (second) record
    const usage = try t.db.getSessionUsage("session-1");
    try std.testing.expectEqual(@as(i32, 140), usage.prompt);
    try std.testing.expectEqual(@as(i32, 40), usage.completion);
    try std.testing.expectEqual(@as(i32, 15), usage.cached);
}

// ============================================================================
// Test 2: getSessionUsage with null session_id (all sessions)
// ============================================================================

test "TokenUsageDb: getSessionUsage null session_id returns latest" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var t = try openTempDb(alloc, tmp);
    defer alloc.free(t.path);
    defer t.db.deinit();

    try t.db.log(1, "session-1", TokenUsageRecord{
        .prompt_tokens = 100,
        .completion_tokens = 25,
        .total_tokens = 125,
        .cached_tokens = 10,
    });
    try t.db.log(2, "session-1", TokenUsageRecord{
        .prompt_tokens = 140,
        .completion_tokens = 40,
        .total_tokens = 180,
        .cached_tokens = 15,
    });

    // All sessions — with only one session, should match session-1's latest
    const usage = try t.db.getSessionUsage(null);
    try std.testing.expectEqual(@as(i32, 140), usage.prompt);
    try std.testing.expectEqual(@as(i32, 40), usage.completion);
    try std.testing.expectEqual(@as(i32, 15), usage.cached);
}

// ============================================================================
// Test 3: getLastPromptTokens
// ============================================================================

test "TokenUsageDb: getLastPromptTokens returns latest prompt count" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var t = try openTempDb(alloc, tmp);
    defer alloc.free(t.path);
    defer t.db.deinit();

    try t.db.log(1, "session-1", TokenUsageRecord{
        .prompt_tokens = 100,
        .completion_tokens = 25,
        .total_tokens = 125,
        .cached_tokens = 10,
    });
    try t.db.log(2, "session-1", TokenUsageRecord{
        .prompt_tokens = 140,
        .completion_tokens = 40,
        .total_tokens = 180,
        .cached_tokens = 15,
    });

    const last_prompt = try t.db.getLastPromptTokens("session-1");
    try std.testing.expectEqual(@as(i32, 140), last_prompt);
}

// ============================================================================
// Test 4: getLastCachedTokens
// ============================================================================

test "TokenUsageDb: getLastCachedTokens returns latest cached count" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var t = try openTempDb(alloc, tmp);
    defer alloc.free(t.path);
    defer t.db.deinit();

    try t.db.log(1, "session-1", TokenUsageRecord{
        .prompt_tokens = 100,
        .completion_tokens = 25,
        .total_tokens = 125,
        .cached_tokens = 10,
    });
    try t.db.log(2, "session-1", TokenUsageRecord{
        .prompt_tokens = 140,
        .completion_tokens = 40,
        .total_tokens = 180,
        .cached_tokens = 15,
    });

    const last_cached = try t.db.getLastCachedTokens("session-1");
    try std.testing.expectEqual(@as(i32, 15), last_cached);
}

// ============================================================================
// Test 5: Multiple sessions — each gets their own latest record
// ============================================================================

test "TokenUsageDb: multiple sessions have independent latest records" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var t = try openTempDb(alloc, tmp);
    defer alloc.free(t.path);
    defer t.db.deinit();

    // Session A: two records
    try t.db.log(1, "session-a", TokenUsageRecord{
        .prompt_tokens = 50,
        .completion_tokens = 10,
        .total_tokens = 60,
        .cached_tokens = 5,
    });
    try t.db.log(2, "session-a", TokenUsageRecord{
        .prompt_tokens = 80,
        .completion_tokens = 20,
        .total_tokens = 100,
        .cached_tokens = 8,
    });

    // Session B: one record
    try t.db.log(3, "session-b", TokenUsageRecord{
        .prompt_tokens = 200,
        .completion_tokens = 50,
        .total_tokens = 250,
        .cached_tokens = 30,
    });

    const usage_a = try t.db.getSessionUsage("session-a");
    try std.testing.expectEqual(@as(i32, 80), usage_a.prompt);
    try std.testing.expectEqual(@as(i32, 20), usage_a.completion);

    const usage_b = try t.db.getSessionUsage("session-b");
    try std.testing.expectEqual(@as(i32, 200), usage_b.prompt);
    try std.testing.expectEqual(@as(i32, 50), usage_b.completion);
}

// ============================================================================
// Test 6: getSessionUsage on empty database returns zeros
// ============================================================================

test "TokenUsageDb: getSessionUsage on empty db returns zeros" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var t = try openTempDb(alloc, tmp);
    defer alloc.free(t.path);
    defer t.db.deinit();

    const usage = try t.db.getSessionUsage("nonexistent-session");
    try std.testing.expectEqual(@as(i32, 0), usage.prompt);
    try std.testing.expectEqual(@as(i32, 0), usage.completion);
    try std.testing.expectEqual(@as(i32, 0), usage.cached);
}

// ============================================================================
// Test 7: getLastPromptTokens on empty database returns 0
// ============================================================================

test "TokenUsageDb: getLastPromptTokens on empty db returns 0" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var t = try openTempDb(alloc, tmp);
    defer alloc.free(t.path);
    defer t.db.deinit();

    const last_prompt = try t.db.getLastPromptTokens("no-session");
    try std.testing.expectEqual(@as(i32, 0), last_prompt);
}

// ============================================================================
// Test 8: log with api_call_id=0 (NULL association)
// ============================================================================

test "TokenUsageDb: log with api_call_id zero works" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var t = try openTempDb(alloc, tmp);
    defer alloc.free(t.path);
    defer t.db.deinit();

    // api_call_id = 0 means no association with an API call record
    try t.db.log(0, "session-x", TokenUsageRecord{
        .prompt_tokens = 77,
        .completion_tokens = 22,
        .total_tokens = 99,
        .cached_tokens = 7,
    });

    const usage = try t.db.getSessionUsage("session-x");
    try std.testing.expectEqual(@as(i32, 77), usage.prompt);
    try std.testing.expectEqual(@as(i32, 22), usage.completion);
}

// ============================================================================
// Test 9: log with null session_id
// ============================================================================

test "TokenUsageDb: log with null session_id works" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var t = try openTempDb(alloc, tmp);
    defer alloc.free(t.path);
    defer t.db.deinit();

    // Null session_id is valid (no session association)
    try t.db.log(1, null, TokenUsageRecord{
        .prompt_tokens = 55,
        .completion_tokens = 15,
        .total_tokens = 70,
        .cached_tokens = 5,
    });

    // Query null session = all sessions
    const usage = try t.db.getSessionUsage(null);
    try std.testing.expectEqual(@as(i32, 55), usage.prompt);
}

// ============================================================================
// Test 10: init and deinit are safe (mirrors C test test_token_usage_db_init_free)
// ============================================================================

test "TokenUsageDb: init and deinit" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "test_init.db" });
    defer alloc.free(path);

    var db = try TokenUsageDb.init(alloc, path);
    db.deinit();
    // No crash on deinit = pass
}
