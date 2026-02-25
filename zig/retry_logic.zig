//! retry_logic.zig — Retry/backoff logic for HTTP API calls
//!
//! Zig port of src/retry_logic.c.
//!
//! Provides:
//! - `shouldRetry(status_code)` — true for 408, 429, 5xx
//! - `calcDelay(attempt, config)` — exponential backoff with ±jitter
//! - `isContextLengthError(message, error_type)` — detect token-limit errors
//! - `contextLengthErrorMessage()` — user-facing explanation string

const std = @import("std");

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

pub const RetryConfig = struct {
    /// Maximum number of retry attempts (not counting the first try).
    max_retries: u32 = 3,
    /// Base delay for the first retry, in milliseconds.
    base_delay_ms: u64 = 1_000,
    /// Upper cap on the computed delay, in milliseconds.
    max_delay_ms: u64 = 30_000,
    /// Fraction of the computed delay that can be shaved off as jitter.
    /// 0.25 → up to 25% reduction (mirrors C implementation).
    jitter_factor: f64 = 0.25,
};

pub const default_config = RetryConfig{};

// ---------------------------------------------------------------------------
// Core predicates
// ---------------------------------------------------------------------------

/// Returns true for HTTP status codes that warrant a retry.
///
/// Retryable status codes (matching C implementation):
///   429  — Too Many Requests (rate limit)
///   408  — Request Timeout
///   5xx  — Server errors
pub fn shouldRetry(status_code: u32) bool {
    return status_code == 429 or status_code == 408 or status_code >= 500;
}

/// Calculate the delay (in milliseconds) before the `attempt`-th retry.
///
/// Uses exponential backoff:  base * 2^(attempt-1),  capped at max_delay_ms,
/// then reduces by a random jitter of up to `jitter_factor` fraction.
///
/// `attempt` is 1-based: pass 1 for the first retry, 2 for the second, etc.
pub fn calcDelay(attempt: u32, config: RetryConfig) u64 {
    if (attempt == 0) return 0;

    // 2^(attempt-1) — use saturating shift to avoid overflow for large attempts
    const shift: u6 = @intCast(@min(attempt - 1, 62));
    const multiplier: u64 = @as(u64, 1) << shift;

    // Saturating multiply avoids overflow when base_delay_ms * multiplier wraps
    const raw = std.math.mul(u64, config.base_delay_ms, multiplier) catch config.max_delay_ms;
    const capped: u64 = @min(raw, config.max_delay_ms);

    // Jitter: reduce by a random fraction in [0, jitter_factor)
    // We use a simple PRNG seeded from the system's random source.
    var rng = std.rand.DefaultPrng.init(@as(u64, @bitCast(std.time.milliTimestamp())));
    const random = rng.random();
    const jitter_max: f64 = @as(f64, @floatFromInt(capped)) * config.jitter_factor;
    const jitter: u64 = @intFromFloat(random.float(f64) * jitter_max);

    return if (capped > jitter) capped - jitter else 0;
}

// ---------------------------------------------------------------------------
// Context-length error detection
// ---------------------------------------------------------------------------

/// Returns true if the error message or type indicates a token/context-length
/// limit was exceeded.
///
/// Performs a case-insensitive search for well-known patterns from OpenAI,
/// Anthropic, AWS Bedrock and LiteLLM.
pub fn isContextLengthError(message: ?[]const u8, error_type: ?[]const u8) bool {
    const msg = message orelse return false;

    // Inline ASCII-to-lower helper (avoids heap allocation)
    const patterns = [_][]const u8{
        "maximum context length",
        "context length exceeded",
        "too many tokens",
        "exceeded model token limit",
        "token limit",
        "contextwindowexceedederror",
        "context window error",
        "input is too long",
    };

    // Build a lowercase copy on the stack for comparison (up to 4 KiB)
    var lower_buf: [4096]u8 = undefined;
    const copy_len = @min(msg.len, lower_buf.len);
    for (msg[0..copy_len], 0..) |ch, i| {
        lower_buf[i] = std.ascii.toLower(ch);
    }
    const lower = lower_buf[0..copy_len];

    for (patterns) |pat| {
        if (std.mem.indexOf(u8, lower, pat) != null) return true;
    }

    // Special: "context length" + "tokens" together anywhere in the message
    if (std.mem.indexOf(u8, lower, "context length") != null and
        std.mem.indexOf(u8, lower, "tokens") != null)
    {
        return true;
    }

    // invalid_request_error with token mention
    if (error_type) |et| {
        if (std.ascii.eqlIgnoreCase(et, "invalid_request_error") and
            std.mem.indexOf(u8, lower, "tokens") != null)
        {
            return true;
        }
    }

    return false;
}

/// Return the standard user-facing context length error message.
///
/// The caller owns the returned slice and must free it with `allocator.free`.
pub fn contextLengthErrorMessage(allocator: std.mem.Allocator) ![]u8 {
    return allocator.dupe(u8,
        "Context length exceeded. The conversation has grown too large for " ++
        "the model's memory. Try starting a new conversation or reduce the " ++
        "amount of code/files being discussed.");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "shouldRetry — retryable codes" {
    try std.testing.expect(shouldRetry(429));
    try std.testing.expect(shouldRetry(408));
    try std.testing.expect(shouldRetry(500));
    try std.testing.expect(shouldRetry(502));
    try std.testing.expect(shouldRetry(503));
    try std.testing.expect(shouldRetry(504));
}

test "shouldRetry — non-retryable codes" {
    try std.testing.expect(!shouldRetry(200));
    try std.testing.expect(!shouldRetry(400));
    try std.testing.expect(!shouldRetry(401));
    try std.testing.expect(!shouldRetry(403));
    try std.testing.expect(!shouldRetry(404));
}

test "calcDelay — first attempt baseline" {
    const cfg = RetryConfig{ .base_delay_ms = 1000, .max_delay_ms = 30_000, .jitter_factor = 0 };
    // With zero jitter the delay should equal the capped base * 2^(attempt-1)
    try std.testing.expectEqual(@as(u64, 1000), calcDelay(1, cfg));
    try std.testing.expectEqual(@as(u64, 2000), calcDelay(2, cfg));
    try std.testing.expectEqual(@as(u64, 4000), calcDelay(3, cfg));
}

test "calcDelay — cap at max_delay_ms" {
    const cfg = RetryConfig{ .base_delay_ms = 1000, .max_delay_ms = 5000, .jitter_factor = 0 };
    // 2^4 * 1000 = 16000 > 5000 → should be capped
    const d = calcDelay(5, cfg);
    try std.testing.expect(d <= 5000);
}

test "calcDelay — jitter reduces delay" {
    const cfg = RetryConfig{ .base_delay_ms = 1000, .max_delay_ms = 30_000, .jitter_factor = 0.25 };
    // With 25% jitter the result must be in [750, 1000]
    const d = calcDelay(1, cfg);
    try std.testing.expect(d <= 1000);
    try std.testing.expect(d >= 750);
}

test "calcDelay — attempt=0 returns 0" {
    try std.testing.expectEqual(@as(u64, 0), calcDelay(0, default_config));
}

test "isContextLengthError — null message" {
    try std.testing.expect(!isContextLengthError(null, null));
}

test "isContextLengthError — OpenAI pattern" {
    try std.testing.expect(isContextLengthError("maximum context length exceeded", null));
    try std.testing.expect(isContextLengthError("This exceeds the model token limit", null));
    try std.testing.expect(isContextLengthError("Too many tokens in request", null));
}

test "isContextLengthError — Anthropic pattern" {
    try std.testing.expect(isContextLengthError("context length is 200000 tokens but input is too long", null));
    try std.testing.expect(isContextLengthError("input is too long", null));
}

test "isContextLengthError — Bedrock / LiteLLM pattern" {
    try std.testing.expect(isContextLengthError("ContextWindowExceededError thrown", null));
    try std.testing.expect(isContextLengthError("context window error occurred", null));
}

test "isContextLengthError — invalid_request_error with tokens" {
    try std.testing.expect(isContextLengthError("Request has too many tokens", "invalid_request_error"));
}

test "isContextLengthError — unrelated error" {
    try std.testing.expect(!isContextLengthError("Internal server error", null));
    try std.testing.expect(!isContextLengthError("Unauthorized", "auth_error"));
}

test "isContextLengthError — case insensitive" {
    try std.testing.expect(isContextLengthError("MAXIMUM CONTEXT LENGTH exceeded", null));
    try std.testing.expect(isContextLengthError("Token Limit Reached", null));
}

test "contextLengthErrorMessage allocates" {
    const msg = try contextLengthErrorMessage(std.testing.allocator);
    defer std.testing.allocator.free(msg);
    try std.testing.expect(msg.len > 0);
    try std.testing.expect(std.mem.indexOf(u8, msg, "Context length exceeded") != null);
}
