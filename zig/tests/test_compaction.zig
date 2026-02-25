//! tests/test_compaction.zig — Zig port of tests/test_compaction.c
//!
//! Tests compaction.zig: CompactionConfig defaults/env, shouldTrigger,
//! estimateMessageTokens, updateTokenCount.

const std = @import("std");
const comp = @import("../compaction.zig");
const state_mod = @import("../conversation/state.zig");

const CompactionConfig = comp.CompactionConfig;
const ConversationState = comp.ConversationState;

// ---------------------------------------------------------------------------
// CompactionConfig defaults (mirrors test_init_config_defaults)
// ---------------------------------------------------------------------------

test "compaction: CompactionConfig defaults" {
    const cfg = CompactionConfig{};
    try std.testing.expect(!cfg.enabled);
    try std.testing.expectEqual(comp.DEFAULT_COMPACT_THRESHOLD, cfg.threshold_percent);
    try std.testing.expectEqual(comp.DEFAULT_KEEP_RECENT, cfg.keep_recent);
    try std.testing.expectEqual(comp.DEFAULT_TOKEN_LIMIT, cfg.model_token_limit);
    try std.testing.expectEqual(@as(i64, -1), cfg.last_compacted_index);
    try std.testing.expectEqual(@as(usize, 0), cfg.current_tokens);
}

test "compaction: DEFAULT constants" {
    try std.testing.expectEqual(@as(u32, 75), comp.DEFAULT_COMPACT_THRESHOLD);
    try std.testing.expectEqual(@as(u32, 100), comp.DEFAULT_KEEP_RECENT);
    try std.testing.expectEqual(@as(u32, 125_000), comp.DEFAULT_TOKEN_LIMIT);
}

// ---------------------------------------------------------------------------
// fromEnv (mirrors test_init_config_disabled / test_init_config_env_override)
// ---------------------------------------------------------------------------

test "compaction: CompactionConfig.fromEnv disabled" {
    const cfg = CompactionConfig.fromEnv(false);
    try std.testing.expect(!cfg.enabled);
    // Default threshold and keep_recent should apply when env vars unset
    try std.testing.expect(cfg.threshold_percent > 0);
    try std.testing.expect(cfg.keep_recent > 0);
}

test "compaction: CompactionConfig.fromEnv enabled" {
    const cfg = CompactionConfig.fromEnv(true);
    try std.testing.expect(cfg.enabled);
}

// ---------------------------------------------------------------------------
// shouldTrigger (mirrors test_should_trigger_* tests)
// ---------------------------------------------------------------------------

test "compaction: shouldTrigger false when disabled" {
    const alloc = std.testing.allocator;
    var s = ConversationState.init(alloc);
    defer s.deinit();

    const cfg = CompactionConfig{
        .enabled = false,
        .threshold_percent = 60,
        .model_token_limit = 125_000,
        .current_tokens = 99_000, // Above threshold but disabled
    };
    try std.testing.expect(!comp.shouldTrigger(&s, &cfg));
}

test "compaction: shouldTrigger false below threshold" {
    const alloc = std.testing.allocator;
    var s = ConversationState.init(alloc);
    defer s.deinit();

    const cfg = CompactionConfig{
        .enabled = true,
        .threshold_percent = 75,
        .model_token_limit = 1_000,
        .current_tokens = 500, // 50% < 75% threshold
    };
    try std.testing.expect(!comp.shouldTrigger(&s, &cfg));
}

test "compaction: shouldTrigger true above threshold" {
    const alloc = std.testing.allocator;
    var s = ConversationState.init(alloc);
    defer s.deinit();

    const cfg = CompactionConfig{
        .enabled = true,
        .threshold_percent = 75,
        .model_token_limit = 1_000,
        .current_tokens = 800, // 80% >= 75% threshold
    };
    try std.testing.expect(comp.shouldTrigger(&s, &cfg));
}

test "compaction: shouldTrigger exactly at threshold triggers" {
    const alloc = std.testing.allocator;
    var s = ConversationState.init(alloc);
    defer s.deinit();

    const cfg = CompactionConfig{
        .enabled = true,
        .threshold_percent = 75,
        .model_token_limit = 1_000,
        .current_tokens = 750, // exactly 75%
    };
    try std.testing.expect(comp.shouldTrigger(&s, &cfg));
}

// ---------------------------------------------------------------------------
// estimateBlockTokens / estimateMessageTokens
// ---------------------------------------------------------------------------

test "compaction: estimateBlockTokens text" {
    const content_types = @import("../conversation/content_types.zig");
    const blk = content_types.ContentBlock{ .text = .{ .text = "hello world" } };
    // 11 chars → ceil(11/4) = 3 tokens
    try std.testing.expectEqual(@as(usize, 3), comp.estimateBlockTokens(blk));
}

test "compaction: estimateBlockTokens image is 500" {
    const content_types = @import("../conversation/content_types.zig");
    const blk = content_types.ContentBlock{ .image = .{ .media_type = "image/png", .data = "abc" } };
    try std.testing.expectEqual(@as(usize, 500), comp.estimateBlockTokens(blk));
}

test "compaction: estimateMessageTokens non-zero for text message" {
    const alloc = std.testing.allocator;
    var s = ConversationState.init(alloc);
    defer s.deinit();
    try s.addUserMessage("Hello, world!");

    const msg = s.messages.items[0];
    const tokens = comp.estimateMessageTokens(msg);
    // base overhead (3+10) + ceil(13/4)=4 → 17
    try std.testing.expect(tokens > 0);
}

// ---------------------------------------------------------------------------
// estimateConversationTokens / updateTokenCount
// ---------------------------------------------------------------------------

test "compaction: estimateConversationTokens increases with more messages" {
    const alloc = std.testing.allocator;
    var s = ConversationState.init(alloc);
    defer s.deinit();

    const base = comp.estimateConversationTokens(&s);
    try s.addUserMessage("A longer message that adds more tokens.");
    const after = comp.estimateConversationTokens(&s);
    try std.testing.expect(after > base);
}

test "compaction: updateTokenCount updates config.current_tokens" {
    const alloc = std.testing.allocator;
    var s = ConversationState.init(alloc);
    defer s.deinit();
    try s.addUserMessage("some text here");

    var cfg = CompactionConfig{};
    const tokens = comp.updateTokenCount(&s, &cfg);
    try std.testing.expect(tokens > 0);
    try std.testing.expectEqual(tokens, cfg.current_tokens);
}

// ---------------------------------------------------------------------------
// compact stub
// ---------------------------------------------------------------------------

test "compaction: compact returns NotImplemented" {
    const alloc = std.testing.allocator;
    var s = ConversationState.init(alloc);
    defer s.deinit();

    var cfg = CompactionConfig{ .enabled = true };
    const result = comp.compact(&s, &cfg, "session-123");
    try std.testing.expectError(error.NotImplemented, result);
}
