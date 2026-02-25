//! compaction.zig — Auto-compaction of conversation history
//!
//! Zig port of src/compaction.c.
//!
//! When the conversation context exceeds a configurable token threshold,
//! the compaction subsystem:
//! 1. Stores older messages to the SQLite memory database (searchable later)
//! 2. Generates an AI summary of the compacted messages (Phase 8 wires this)
//! 3. Replaces the old messages with a single compaction-notice message
//!
//! ## Phase 6 scope
//!
//! - All configuration types and initialization logic
//! - Token estimation (mirrors the C heuristic: ~4 chars/token)
//! - `shouldTrigger` logic
//! - `estimateMessageTokens` helper
//! - `compact` — stub: returns `error.NotImplemented`
//!   (Phase 8 wires in the actual API summarization call)

const std = @import("std");
const state_mod = @import("conversation/state.zig");
const content_types = @import("conversation/content_types.zig");

pub const ConversationState = state_mod.ConversationState;
pub const Message = state_mod.Message;
pub const ContentBlock = content_types.ContentBlock;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

pub const DEFAULT_COMPACT_THRESHOLD: u32 = 75; // percent
pub const DEFAULT_KEEP_RECENT: u32 = 100;
pub const DEFAULT_TOKEN_LIMIT: u32 = 125_000;

// ---------------------------------------------------------------------------
// CompactionConfig
// ---------------------------------------------------------------------------

/// Configuration for the auto-compaction subsystem.
///
/// Mirrors the C `CompactionConfig` struct.
pub const CompactionConfig = struct {
    enabled: bool = false,
    /// Trigger compaction when context reaches this % of `model_token_limit`.
    threshold_percent: u32 = DEFAULT_COMPACT_THRESHOLD,
    /// Number of recent messages to keep after compaction.
    keep_recent: u32 = DEFAULT_KEEP_RECENT,
    /// Model context window size in tokens.
    model_token_limit: u32 = DEFAULT_TOKEN_LIMIT,
    /// Last message index that was compacted (-1 = none).
    last_compacted_index: i64 = -1,
    /// Current estimated token count (updated by `updateTokenCount`).
    current_tokens: usize = 0,

    /// Initialize from environment variables (mirrors `compaction_init_config`).
    pub fn fromEnv(enabled: bool) CompactionConfig {
        var cfg = CompactionConfig{ .enabled = enabled };

        if (std.process.getEnvVarOwned(std.heap.page_allocator, "KLAWED_COMPACT_THRESHOLD") catch null) |v| {
            defer std.heap.page_allocator.free(v);
            if (std.fmt.parseInt(u32, v, 10) catch null) |n| {
                if (n > 0) cfg.threshold_percent = n;
            }
        }

        if (std.process.getEnvVarOwned(std.heap.page_allocator, "KLAWED_COMPACT_KEEP_RECENT") catch null) |v| {
            defer std.heap.page_allocator.free(v);
            if (std.fmt.parseInt(u32, v, 10) catch null) |n| {
                if (n > 0) cfg.keep_recent = n;
            }
        }

        if (std.process.getEnvVarOwned(std.heap.page_allocator, "KLAWED_CONTEXT_LIMIT") catch null) |v| {
            defer std.heap.page_allocator.free(v);
            if (std.fmt.parseInt(u32, v, 10) catch null) |n| {
                if (n > 0) cfg.model_token_limit = n;
            }
        }

        return cfg;
    }
};

// ---------------------------------------------------------------------------
// CompactionResult
// ---------------------------------------------------------------------------

/// Statistics returned by a compaction operation.
pub const CompactionResult = struct {
    success: bool = false,
    messages_compacted: usize = 0,
    tokens_before: usize = 0,
    tokens_after: usize = 0,
    usage_before_pct: f64 = 0.0,
    usage_after_pct: f64 = 0.0,
    /// AI-generated summary of the compacted context (may be empty).
    summary: []const u8 = "",
};

// ---------------------------------------------------------------------------
// Token estimation
// ---------------------------------------------------------------------------

/// Estimate tokens for a single content block.
/// Uses ~4 chars/token heuristic (same as C code).
pub fn estimateBlockTokens(blk: ContentBlock) usize {
    const chars_per_token = 4;
    return switch (blk) {
        .text => |b| (b.text.len + chars_per_token - 1) / chars_per_token,
        .tool_use => |b| 5 + (b.name.len + b.arguments_json.len + chars_per_token - 1) / chars_per_token,
        .tool_result => |b| 5 + (b.tool_use_id.len + b.content.len + chars_per_token - 1) / chars_per_token,
        .image => 500,
        .thinking => |b| (b.thinking.len + chars_per_token - 1) / chars_per_token,
    };
}

/// Estimate tokens for a single `Message`.
/// Mirrors `compaction_estimate_message_tokens` in C.
pub fn estimateMessageTokens(msg: Message) usize {
    var total: usize = 3; // role overhead
    for (msg.content) |blk| {
        total += estimateBlockTokens(blk);
    }
    total += 10; // message formatting overhead
    return total;
}

/// Estimate total tokens for the full conversation.
pub fn estimateConversationTokens(s: *const ConversationState) usize {
    var total: usize = 100; // request overhead
    for (s.messages.items) |msg| {
        total += estimateMessageTokens(msg);
    }
    return total;
}

/// Update `config.current_tokens` from the conversation state.
/// Returns the updated estimate.
pub fn updateTokenCount(s: *const ConversationState, config: *CompactionConfig) usize {
    const total = estimateConversationTokens(s);
    config.current_tokens = total;
    return total;
}

// ---------------------------------------------------------------------------
// shouldTrigger
// ---------------------------------------------------------------------------

/// Returns `true` if compaction should be triggered now.
/// Mirrors `compaction_should_trigger` in C.
pub fn shouldTrigger(s: *const ConversationState, config: *const CompactionConfig) bool {
    if (!config.enabled) return false;

    const threshold_tokens: usize = @intCast(
        (@as(u64, config.model_token_limit) * @as(u64, config.threshold_percent)) / 100,
    );

    const current = if (config.current_tokens > 0)
        config.current_tokens
    else
        estimateConversationTokens(s);

    return current >= threshold_tokens;
}

// ---------------------------------------------------------------------------
// compact — stub (Phase 8)
// ---------------------------------------------------------------------------

/// Perform context compaction.
///
/// **Status**: stub — returns `error.NotImplemented` until Phase 8.
///
/// The full implementation needs:
/// - Access to the memory database (Phase 3, already ported)
/// - An API call for summarization (Phase 8)
///
/// Usage in Phase 8:
/// ```zig
/// try compaction.compact(&state, &config, session_id);
/// ```
pub fn compact(
    s: *ConversationState,
    config: *CompactionConfig,
    session_id: ?[]const u8,
) !CompactionResult {
    _ = s;
    _ = config;
    _ = session_id;
    return error.NotImplemented;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "CompactionConfig defaults" {
    const cfg = CompactionConfig{};
    try std.testing.expect(!cfg.enabled);
    try std.testing.expectEqual(DEFAULT_COMPACT_THRESHOLD, cfg.threshold_percent);
    try std.testing.expectEqual(DEFAULT_KEEP_RECENT, cfg.keep_recent);
    try std.testing.expectEqual(DEFAULT_TOKEN_LIMIT, cfg.model_token_limit);
    try std.testing.expectEqual(@as(i64, -1), cfg.last_compacted_index);
}

test "estimateBlockTokens text" {
    const blk = ContentBlock{ .text = .{ .text = "hello world" } };
    const est = estimateBlockTokens(blk);
    // "hello world" = 11 chars → ceil(11/4) = 3 tokens
    try std.testing.expectEqual(@as(usize, 3), est);
}

test "estimateBlockTokens image" {
    const blk = ContentBlock{ .image = .{ .media_type = "image/png", .data = "abc" } };
    try std.testing.expectEqual(@as(usize, 500), estimateBlockTokens(blk));
}

test "estimateMessageTokens" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();
    try state.addUserMessage("Hello, world!");

    const msg = state.messages.items[0];
    const tokens = estimateMessageTokens(msg);
    // base overhead (3 + 10) + ceil(13/4) = 13 + 4 = 17
    try std.testing.expect(tokens > 0);
}

test "estimateConversationTokens increases with messages" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    const base = estimateConversationTokens(&state);
    try state.addUserMessage("A longer message that adds more tokens.");
    const after = estimateConversationTokens(&state);
    try std.testing.expect(after > base);
}

test "shouldTrigger false when disabled" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    var cfg = CompactionConfig{ .enabled = false, .current_tokens = 999_999 };
    try std.testing.expect(!shouldTrigger(&state, &cfg));
}

test "shouldTrigger true when above threshold" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    var cfg = CompactionConfig{
        .enabled = true,
        .threshold_percent = 75,
        .model_token_limit = 1000,
        .current_tokens = 800, // 80% of 1000 → above 75% threshold
    };
    try std.testing.expect(shouldTrigger(&state, &cfg));
}

test "shouldTrigger false when below threshold" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    var cfg = CompactionConfig{
        .enabled = true,
        .threshold_percent = 75,
        .model_token_limit = 1000,
        .current_tokens = 500, // 50% of 1000 → below threshold
    };
    try std.testing.expect(!shouldTrigger(&state, &cfg));
}

test "compact returns NotImplemented" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    var cfg = CompactionConfig{ .enabled = true };
    const result = compact(&state, &cfg, "session-123");
    try std.testing.expectError(error.NotImplemented, result);
}

test "updateTokenCount updates config" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();
    try state.addUserMessage("some text here");

    var cfg = CompactionConfig{};
    const tokens = updateTokenCount(&state, &cfg);
    try std.testing.expect(tokens > 0);
    try std.testing.expectEqual(tokens, cfg.current_tokens);
}
