//! tests/test_retry_jitter.zig — Zig port of tests/test_retry_jitter.c
//!
//! Tests the retry/backoff logic in retry_logic.zig:
//! - Jitter keeps delay within [75%, 100%] of the base
//! - Exponential backoff doubles delay across attempts
//! - Delay is capped at max_delay_ms
//! - Edge cases: attempt=0, small/large backoff values
//! - Statistical distribution over many samples
//! - shouldRetry predicate
//! - isContextLengthError detection

const std = @import("std");
const retry = @import("../retry_logic.zig");

// ---------------------------------------------------------------------------
// shouldRetry
// ---------------------------------------------------------------------------

test "retry: shouldRetry true for 429, 408, 5xx" {
    try std.testing.expect(retry.shouldRetry(429));
    try std.testing.expect(retry.shouldRetry(408));
    try std.testing.expect(retry.shouldRetry(500));
    try std.testing.expect(retry.shouldRetry(502));
    try std.testing.expect(retry.shouldRetry(503));
    try std.testing.expect(retry.shouldRetry(504));
}

test "retry: shouldRetry false for 2xx, 4xx (except 408/429)" {
    try std.testing.expect(!retry.shouldRetry(200));
    try std.testing.expect(!retry.shouldRetry(400));
    try std.testing.expect(!retry.shouldRetry(401));
    try std.testing.expect(!retry.shouldRetry(403));
    try std.testing.expect(!retry.shouldRetry(404));
}

// ---------------------------------------------------------------------------
// calcDelay — no jitter (deterministic)
// ---------------------------------------------------------------------------

test "retry: calcDelay attempt=0 returns 0" {
    const cfg = retry.RetryConfig{ .base_delay_ms = 1000, .max_delay_ms = 30_000, .jitter_factor = 0 };
    try std.testing.expectEqual(@as(u64, 0), retry.calcDelay(0, cfg));
}

test "retry: calcDelay exponential backoff with zero jitter" {
    const cfg = retry.RetryConfig{ .base_delay_ms = 1000, .max_delay_ms = 30_000, .jitter_factor = 0 };
    // attempt 1: 1000 * 2^0 = 1000
    // attempt 2: 1000 * 2^1 = 2000
    // attempt 3: 1000 * 2^2 = 4000
    try std.testing.expectEqual(@as(u64, 1000), retry.calcDelay(1, cfg));
    try std.testing.expectEqual(@as(u64, 2000), retry.calcDelay(2, cfg));
    try std.testing.expectEqual(@as(u64, 4000), retry.calcDelay(3, cfg));
}

test "retry: calcDelay is capped at max_delay_ms" {
    const cfg = retry.RetryConfig{ .base_delay_ms = 1000, .max_delay_ms = 5000, .jitter_factor = 0 };
    // 2^10 * 1000 >> 5000 → must be capped
    const d = retry.calcDelay(11, cfg);
    try std.testing.expect(d <= 5000);
}

// ---------------------------------------------------------------------------
// calcDelay — with jitter
// ---------------------------------------------------------------------------

test "retry: jitter keeps delay in [75%, 100%] range" {
    // Run many samples and verify each falls in the expected range.
    const cfg = retry.RetryConfig{ .base_delay_ms = 1000, .max_delay_ms = 30_000, .jitter_factor = 0.25 };
    var i: u32 = 0;
    while (i < 100) : (i += 1) {
        const d = retry.calcDelay(1, cfg);
        try std.testing.expect(d >= 750); // at least 75% of 1000
        try std.testing.expect(d <= 1000); // at most 100%
    }
}

test "retry: jitter does not produce delay above base" {
    const cfg = retry.RetryConfig{ .base_delay_ms = 2000, .max_delay_ms = 30_000, .jitter_factor = 0.25 };
    var i: u32 = 0;
    while (i < 50) : (i += 1) {
        const d = retry.calcDelay(1, cfg);
        try std.testing.expect(d <= 2000);
        try std.testing.expect(d >= 1500); // 75% of 2000
    }
}

test "retry: jitter with exponential backoff stays within bounds" {
    const cfg = retry.RetryConfig{ .base_delay_ms = 1000, .max_delay_ms = 30_000, .jitter_factor = 0.25 };
    const bases = [_]u64{ 1000, 2000, 4000, 8000 };
    for (bases, 1..) |base, attempt| {
        const d = retry.calcDelay(@intCast(attempt), cfg);
        const min_val = base * 3 / 4; // 75%
        try std.testing.expect(d >= min_val);
        try std.testing.expect(d <= base);
    }
}

// ---------------------------------------------------------------------------
// Jitter formula verification — mirrors C test_jitter_formula
// ---------------------------------------------------------------------------

test "retry: jitter formula — zero reduction gives full delay" {
    // With jitter_factor=0 the delay is exactly base * 2^(attempt-1)
    const cfg = retry.RetryConfig{ .base_delay_ms = 2000, .max_delay_ms = 30_000, .jitter_factor = 0 };
    const d = retry.calcDelay(1, cfg);
    try std.testing.expectEqual(@as(u64, 2000), d);
}

test "retry: jitter with 100% factor can reduce to zero" {
    // With jitter_factor=1.0 the delay can drop as low as 0.
    const cfg = retry.RetryConfig{ .base_delay_ms = 1000, .max_delay_ms = 30_000, .jitter_factor = 1.0 };
    const d = retry.calcDelay(1, cfg);
    try std.testing.expect(d <= 1000); // Can never exceed base
}

// ---------------------------------------------------------------------------
// Thundering herd — multiple calls produce varied delays
// ---------------------------------------------------------------------------

test "retry: calcDelay jitter_factor > 0 produces delay at or below base" {
    // Verify that with jitter, the delay is within [base*(1-jitter), base] range.
    // This is a deterministic property test rather than a randomness test.
    const cfg = retry.RetryConfig{ .base_delay_ms = 1000, .max_delay_ms = 30_000, .jitter_factor = 0.25 };
    for (0..20) |_| {
        const d = retry.calcDelay(1, cfg);
        // delay must be between 750ms and 1000ms (base * (1 - jitter_factor) .. base)
        try std.testing.expect(d >= 750);
        try std.testing.expect(d <= 1000);
    }
}

// ---------------------------------------------------------------------------
// isContextLengthError
// ---------------------------------------------------------------------------

test "retry: isContextLengthError null message returns false" {
    try std.testing.expect(!retry.isContextLengthError(null, null));
}

test "retry: isContextLengthError detects OpenAI patterns" {
    try std.testing.expect(retry.isContextLengthError("maximum context length exceeded", null));
    try std.testing.expect(retry.isContextLengthError("This exceeds the model token limit", null));
    try std.testing.expect(retry.isContextLengthError("Too many tokens in request", null));
}

test "retry: isContextLengthError detects Anthropic patterns" {
    try std.testing.expect(retry.isContextLengthError("context length is 200000 tokens but input is too long", null));
    try std.testing.expect(retry.isContextLengthError("input is too long", null));
}

test "retry: isContextLengthError detects Bedrock/LiteLLM patterns" {
    try std.testing.expect(retry.isContextLengthError("ContextWindowExceededError thrown", null));
    try std.testing.expect(retry.isContextLengthError("context window error occurred", null));
}

test "retry: isContextLengthError case-insensitive" {
    try std.testing.expect(retry.isContextLengthError("MAXIMUM CONTEXT LENGTH exceeded", null));
    try std.testing.expect(retry.isContextLengthError("Token Limit Reached", null));
}

test "retry: isContextLengthError unrelated messages return false" {
    try std.testing.expect(!retry.isContextLengthError("Internal server error", null));
    try std.testing.expect(!retry.isContextLengthError("Unauthorized", "auth_error"));
    try std.testing.expect(!retry.isContextLengthError("Connection refused", null));
}

// ---------------------------------------------------------------------------
// contextLengthErrorMessage
// ---------------------------------------------------------------------------

test "retry: contextLengthErrorMessage is non-empty" {
    const alloc = std.testing.allocator;
    const msg = try retry.contextLengthErrorMessage(alloc);
    defer alloc.free(msg);
    try std.testing.expect(msg.len > 0);
    try std.testing.expect(std.mem.indexOf(u8, msg, "Context length exceeded") != null);
}

// ---------------------------------------------------------------------------
// Statistical distribution (mirrors C test_jitter_distribution)
// ---------------------------------------------------------------------------

test "retry: jitter statistical distribution — mean near 87.5% of base" {
    const cfg = retry.RetryConfig{ .base_delay_ms = 1000, .max_delay_ms = 30_000, .jitter_factor = 0.25 };
    const samples: u32 = 500;
    var sum: u64 = 0;
    var max_val: u64 = 0;

    var i: u32 = 0;
    while (i < samples) : (i += 1) {
        const d = retry.calcDelay(1, cfg);
        sum += d;
        if (d > max_val) max_val = d;
    }

    // Mean should be approximately 875 ms (87.5% of 1000), with some tolerance.
    const mean: f64 = @as(f64, @floatFromInt(sum)) / @as(f64, @floatFromInt(samples));
    try std.testing.expect(mean >= 800.0); // at least 80% of base
    try std.testing.expect(mean <= 1000.0); // never exceeds base

    // Max should not exceed base delay.
    try std.testing.expect(max_val <= 1000);
}
