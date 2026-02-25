//! tests/test_token_usage_comprehensive.zig — Zig port of tests/test_token_usage_comprehensive.c
//!
//! Comprehensive token usage extraction tests for all supported API providers:
//! Anthropic, AWS Bedrock, OpenAI, DeepSeek, Moonshot — and edge cases.

const std = @import("std");

// ---------------------------------------------------------------------------
// Token extraction helper — mirrors the logic in test_token_usage_comprehensive.c
// ---------------------------------------------------------------------------

const ExtractedTokens = struct {
    prompt: i64,
    completion: i64,
    total: i64,
    cached: i64,
    cache_hit: i64,
    cache_miss: i64,
};

/// Extract token counts from a `usage` JSON object string.
/// Supports Anthropic (input_tokens/output_tokens), OpenAI
/// (prompt_tokens/completion_tokens), Moonshot (cached_tokens),
/// DeepSeek (prompt_tokens_details.cached_tokens), and detailed
/// cache metrics (prompt_cache_hit_tokens, prompt_cache_miss_tokens).
fn extractTokens(alloc: std.mem.Allocator, usage_json: []const u8) !ExtractedTokens {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, usage_json, .{});
    defer parsed.deinit();

    const obj = parsed.value.object;

    // prompt: Anthropic uses input_tokens; others use prompt_tokens
    const prompt: i64 = blk: {
        if (obj.get("input_tokens")) |v| break :blk v.integer;
        if (obj.get("prompt_tokens")) |v| break :blk v.integer;
        break :blk 0;
    };

    // completion: Anthropic uses output_tokens; others use completion_tokens
    const completion: i64 = blk: {
        if (obj.get("output_tokens")) |v| break :blk v.integer;
        if (obj.get("completion_tokens")) |v| break :blk v.integer;
        break :blk 0;
    };

    const total: i64 = blk: {
        if (obj.get("total_tokens")) |v| break :blk v.integer;
        break :blk 0;
    };

    // cached:
    //   1. Moonshot: direct "cached_tokens"
    //   2. DeepSeek: "prompt_tokens_details".cached_tokens
    //   3. Anthropic: "cache_read_input_tokens"
    var cached: i64 = 0;
    if (obj.get("cached_tokens")) |v| {
        cached = v.integer;
    } else if (obj.get("prompt_tokens_details")) |details| {
        if (details.object.get("cached_tokens")) |v| {
            cached = v.integer;
        }
    } else if (obj.get("cache_read_input_tokens")) |v| {
        cached = v.integer;
    }

    const cache_hit: i64 = blk: {
        if (obj.get("prompt_cache_hit_tokens")) |v| break :blk v.integer;
        break :blk 0;
    };
    const cache_miss: i64 = blk: {
        if (obj.get("prompt_cache_miss_tokens")) |v| break :blk v.integer;
        break :blk 0;
    };

    return ExtractedTokens{
        .prompt = prompt,
        .completion = completion,
        .total = total,
        .cached = cached,
        .cache_hit = cache_hit,
        .cache_miss = cache_miss,
    };
}

// ---------------------------------------------------------------------------
// Anthropic
// ---------------------------------------------------------------------------

test "token_usage_comprehensive: Anthropic no cache" {
    const alloc = std.testing.allocator;
    const result = try extractTokens(alloc,
        \\{"input_tokens": 34122, "output_tokens": 106}
    );
    try std.testing.expectEqual(@as(i64, 34122), result.prompt);
    try std.testing.expectEqual(@as(i64, 106), result.completion);
    try std.testing.expectEqual(@as(i64, 0), result.total);
    try std.testing.expectEqual(@as(i64, 0), result.cached);
    try std.testing.expectEqual(@as(i64, 0), result.cache_hit);
    try std.testing.expectEqual(@as(i64, 0), result.cache_miss);
}

test "token_usage_comprehensive: Anthropic with cache" {
    const alloc = std.testing.allocator;
    const result = try extractTokens(alloc,
        \\{"input_tokens": 5454, "cache_read_input_tokens": 3000, "output_tokens": 69}
    );
    try std.testing.expectEqual(@as(i64, 5454), result.prompt);
    try std.testing.expectEqual(@as(i64, 69), result.completion);
    try std.testing.expectEqual(@as(i64, 0), result.total);
    try std.testing.expectEqual(@as(i64, 3000), result.cached);
}

// ---------------------------------------------------------------------------
// AWS Bedrock
// ---------------------------------------------------------------------------

test "token_usage_comprehensive: AWS Bedrock" {
    const alloc = std.testing.allocator;
    const result = try extractTokens(alloc,
        \\{"input_tokens": 15382, "output_tokens": 145}
    );
    try std.testing.expectEqual(@as(i64, 15382), result.prompt);
    try std.testing.expectEqual(@as(i64, 145), result.completion);
    try std.testing.expectEqual(@as(i64, 0), result.total);
    try std.testing.expectEqual(@as(i64, 0), result.cached);
}

// ---------------------------------------------------------------------------
// OpenAI
// ---------------------------------------------------------------------------

test "token_usage_comprehensive: OpenAI no cache" {
    const alloc = std.testing.allocator;
    const result = try extractTokens(alloc,
        \\{"prompt_tokens": 100, "completion_tokens": 50, "total_tokens": 150}
    );
    try std.testing.expectEqual(@as(i64, 100), result.prompt);
    try std.testing.expectEqual(@as(i64, 50), result.completion);
    try std.testing.expectEqual(@as(i64, 150), result.total);
    try std.testing.expectEqual(@as(i64, 0), result.cached);
}

// ---------------------------------------------------------------------------
// DeepSeek
// ---------------------------------------------------------------------------

test "token_usage_comprehensive: DeepSeek with cache" {
    const alloc = std.testing.allocator;
    const result = try extractTokens(alloc,
        \\{"prompt_tokens": 37667, "completion_tokens": 25, "total_tokens": 37692,
        \\  "prompt_tokens_details": {"cached_tokens": 37632},
        \\  "prompt_cache_hit_tokens": 37632, "prompt_cache_miss_tokens": 35}
    );
    try std.testing.expectEqual(@as(i64, 37667), result.prompt);
    try std.testing.expectEqual(@as(i64, 25), result.completion);
    try std.testing.expectEqual(@as(i64, 37692), result.total);
    try std.testing.expectEqual(@as(i64, 37632), result.cached);
    try std.testing.expectEqual(@as(i64, 37632), result.cache_hit);
    try std.testing.expectEqual(@as(i64, 35), result.cache_miss);
}

test "token_usage_comprehensive: DeepSeek no cache" {
    const alloc = std.testing.allocator;
    const result = try extractTokens(alloc,
        \\{"prompt_tokens": 2000, "completion_tokens": 300, "total_tokens": 2300}
    );
    try std.testing.expectEqual(@as(i64, 2000), result.prompt);
    try std.testing.expectEqual(@as(i64, 300), result.completion);
    try std.testing.expectEqual(@as(i64, 2300), result.total);
    try std.testing.expectEqual(@as(i64, 0), result.cached);
}

// ---------------------------------------------------------------------------
// Moonshot
// ---------------------------------------------------------------------------

test "token_usage_comprehensive: Moonshot with cache" {
    const alloc = std.testing.allocator;
    const result = try extractTokens(alloc,
        \\{"prompt_tokens": 1551, "completion_tokens": 232, "total_tokens": 1783, "cached_tokens": 768}
    );
    try std.testing.expectEqual(@as(i64, 1551), result.prompt);
    try std.testing.expectEqual(@as(i64, 232), result.completion);
    try std.testing.expectEqual(@as(i64, 1783), result.total);
    try std.testing.expectEqual(@as(i64, 768), result.cached);
    try std.testing.expectEqual(@as(i64, 0), result.cache_hit);
    try std.testing.expectEqual(@as(i64, 0), result.cache_miss);
}

test "token_usage_comprehensive: Moonshot no cache" {
    const alloc = std.testing.allocator;
    const result = try extractTokens(alloc,
        \\{"prompt_tokens": 500, "completion_tokens": 100, "total_tokens": 600}
    );
    try std.testing.expectEqual(@as(i64, 500), result.prompt);
    try std.testing.expectEqual(@as(i64, 100), result.completion);
    try std.testing.expectEqual(@as(i64, 600), result.total);
    try std.testing.expectEqual(@as(i64, 0), result.cached);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

test "token_usage_comprehensive: minimal response with only prompt and completion" {
    const alloc = std.testing.allocator;
    const result = try extractTokens(alloc,
        \\{"prompt_tokens": 10, "completion_tokens": 5}
    );
    try std.testing.expectEqual(@as(i64, 10), result.prompt);
    try std.testing.expectEqual(@as(i64, 5), result.completion);
    try std.testing.expectEqual(@as(i64, 0), result.total);
    try std.testing.expectEqual(@as(i64, 0), result.cached);
}

test "token_usage_comprehensive: large token numbers" {
    const alloc = std.testing.allocator;
    const result = try extractTokens(alloc,
        \\{"prompt_tokens": 1000000, "completion_tokens": 50000, "total_tokens": 1050000}
    );
    try std.testing.expectEqual(@as(i64, 1000000), result.prompt);
    try std.testing.expectEqual(@as(i64, 50000), result.completion);
    try std.testing.expectEqual(@as(i64, 1050000), result.total);
    try std.testing.expectEqual(@as(i64, 0), result.cached);
}

test "token_usage_comprehensive: zero tokens" {
    const alloc = std.testing.allocator;
    const result = try extractTokens(alloc,
        \\{"prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0}
    );
    try std.testing.expectEqual(@as(i64, 0), result.prompt);
    try std.testing.expectEqual(@as(i64, 0), result.completion);
    try std.testing.expectEqual(@as(i64, 0), result.total);
    try std.testing.expectEqual(@as(i64, 0), result.cached);
}

test "token_usage_comprehensive: empty usage object" {
    const alloc = std.testing.allocator;
    const result = try extractTokens(alloc, "{}");
    try std.testing.expectEqual(@as(i64, 0), result.prompt);
    try std.testing.expectEqual(@as(i64, 0), result.completion);
    try std.testing.expectEqual(@as(i64, 0), result.total);
    try std.testing.expectEqual(@as(i64, 0), result.cached);
}

// ---------------------------------------------------------------------------
// All providers agree on zero cached when no cache field present
// ---------------------------------------------------------------------------

test "token_usage_comprehensive: no cache field means zero cached" {
    const alloc = std.testing.allocator;

    const cases = [_][]const u8{
        \\{"prompt_tokens": 100, "completion_tokens": 50}
        ,
        \\{"input_tokens": 200, "output_tokens": 80}
        ,
        \\{"prompt_tokens": 300, "completion_tokens": 90, "total_tokens": 390}
        ,
    };

    for (cases) |json| {
        const result = try extractTokens(alloc, json);
        try std.testing.expectEqual(@as(i64, 0), result.cached);
        try std.testing.expectEqual(@as(i64, 0), result.cache_hit);
        try std.testing.expectEqual(@as(i64, 0), result.cache_miss);
    }
}
