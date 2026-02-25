//! api/api_client.zig — HTTP API client with retry logic
//!
//! Combines the functionality of src/api/api_client.c and src/api/api_builder.c.
//!
//! This module:
//!  1. Takes a provider configuration + request body (JSON string)
//!  2. Builds the appropriate HTTP headers for each provider
//!  3. Dispatches the HTTP call via `http_client.HttpClient`
//!  4. Applies exponential-backoff retry for retryable errors
//!  5. Returns a parsed `ApiResponse`
//!
//! ## Provider-specific headers
//! - **OpenAI-compatible**: `Authorization: Bearer <key>`, `Content-Type: application/json`
//! - **Anthropic**: `x-api-key: <key>`, `anthropic-version: 2023-06-01`, `Content-Type: …`
//! - **Bedrock**: AWS Signature V4 is handled by `providers/bedrock.zig` — the
//!   caller passes pre-signed headers here.
//!
//! ## SSE streaming
//! When `stream: true`, the HTTP body is routed through `api/sse_parser.zig`
//! line-by-line; the callback receives `StreamEvent` values.

const std = @import("std");
const http_client = @import("../http_client.zig");
const retry_logic = @import("../retry_logic.zig");
const api_response = @import("api_response.zig");
const sse_parser = @import("sse_parser.zig");

pub const ApiResponse = api_response.ApiResponse;
pub const ProviderFormat = api_response.ProviderFormat;
pub const StreamEvent = sse_parser.StreamEvent;

// ---------------------------------------------------------------------------
// Provider descriptor
// ---------------------------------------------------------------------------

pub const ProviderKind = enum {
    openai,
    anthropic,
    bedrock,
};

/// Describes a single API endpoint to call.
pub const ProviderConfig = struct {
    kind: ProviderKind,
    /// Full URL including path (e.g. "https://api.openai.com/v1/chat/completions").
    url: []const u8,
    /// API key / token.  For Bedrock this may be empty (auth is in headers).
    api_key: []const u8 = "",
    /// Extra headers to append (e.g. pre-signed AWS headers for Bedrock).
    extra_headers: []const http_client.Header = &.{},
    /// Anthropic API version string (only used when kind == .anthropic).
    anthropic_version: []const u8 = "2023-06-01",
    /// Total request timeout in milliseconds.
    timeout_ms: u32 = 300_000,
};

// ---------------------------------------------------------------------------
// Streaming callback
// ---------------------------------------------------------------------------

pub const StreamCallback = *const fn (event: StreamEvent, userdata: ?*anyopaque) bool;

// ---------------------------------------------------------------------------
// Non-streaming call
// ---------------------------------------------------------------------------

/// Perform a non-streaming API call with retry logic.
///
/// `json_body` must be a JSON-encoded request (built by the provider's
/// `buildRequestBody` method).
///
/// Returns an owned `ApiResponse`; caller must call `.deinit()`.
pub fn call(
    allocator: std.mem.Allocator,
    client: *http_client.HttpClient,
    config: ProviderConfig,
    json_body: []const u8,
) !ApiResponse {
    return callWithRetry(allocator, client, config, json_body, retry_logic.default_config);
}

/// Same as `call` but with a custom `RetryConfig`.
pub fn callWithRetry(
    allocator: std.mem.Allocator,
    client: *http_client.HttpClient,
    config: ProviderConfig,
    json_body: []const u8,
    retry_cfg: retry_logic.RetryConfig,
) !ApiResponse {
    var attempt: u32 = 0;

    while (true) {
        attempt += 1;

        // Build headers for this provider
        var headers = std.ArrayList(http_client.Header).init(allocator);
        defer headers.deinit();
        try buildHeaders(allocator, config, &headers, false);

        const req = http_client.Request{
            .url = config.url,
            .method = .POST,
            .headers = headers.items,
            .body = json_body,
            .total_timeout_ms = config.timeout_ms,
        };

        const resp = client.request(allocator, req) catch |err| {
            if (attempt > retry_cfg.max_retries) return err;
            const delay = retry_logic.calcDelay(attempt, retry_cfg);
            std.time.sleep(delay * std.time.ns_per_ms);
            continue;
        };
        defer {
            var r = resp;
            r.deinit(allocator);
        }

        // Check HTTP status for retryable errors
        if (retry_logic.shouldRetry(resp.status_code)) {
            if (attempt > retry_cfg.max_retries) {
                // Build error response
                const msg = try std.fmt.allocPrint(allocator, "HTTP {d}", .{resp.status_code});
                return api_response.ApiResponse{
                    .allocator = allocator,
                    .content = try allocator.dupe(u8, ""),
                    .reasoning_content = null,
                    .tool_calls = try allocator.dupe(api_response.ToolCall, &.{}),
                    .stop_reason = try allocator.dupe(u8, "error"),
                    .usage = null,
                    .is_error = true,
                    .error_message = msg,
                };
            }
            const delay = retry_logic.calcDelay(attempt, retry_cfg);
            std.time.sleep(delay * std.time.ns_per_ms);
            continue;
        }

        // Parse the response
        const fmt = providerFormat(config.kind);
        return api_response.parseResponse(allocator, resp.body, fmt);
    }
}

// ---------------------------------------------------------------------------
// Streaming call
// ---------------------------------------------------------------------------

/// Streaming context passed through the curl SSE callback.
const StreamCtx = struct {
    sse: sse_parser.Parser,
    user_callback: StreamCallback,
    userdata: ?*anyopaque,
};

fn sseLineCallback(line: []const u8, userdata: ?*anyopaque) bool {
    const ctx = @as(*StreamCtx, @ptrCast(@alignCast(userdata.?)));
    ctx.sse.feedLine(line) catch return true;
    return false;
}

/// Perform a streaming API call.
///
/// Each SSE event is dispatched to `callback` as it arrives.
/// Returns the HTTP status code on completion.
pub fn callStream(
    allocator: std.mem.Allocator,
    client: *http_client.HttpClient,
    config: ProviderConfig,
    json_body: []const u8,
    callback: StreamCallback,
    userdata: ?*anyopaque,
) !u32 {
    // Build a streaming SSE callback bridge
    const SseBridge = struct {
        user_cb: StreamCallback,
        user_data: ?*anyopaque,

        fn onEvent(event: StreamEvent, ud: ?*anyopaque) bool {
            const self = @as(*@This(), @ptrCast(@alignCast(ud.?)));
            return self.user_cb(event, self.user_data);
        }
    };
    var bridge = SseBridge{ .user_cb = callback, .user_data = userdata };
    var sseParser = sse_parser.Parser.init(allocator, SseBridge.onEvent, &bridge);
    defer sseParser.deinit();

    const LineCtx = struct {
        parser: *sse_parser.Parser,
        fn onLine(line: []const u8, ud: ?*anyopaque) bool {
            const self = @as(*@This(), @ptrCast(@alignCast(ud.?)));
            self.parser.feedLine(line) catch return true;
            return false;
        }
    };
    var line_ctx = LineCtx{ .parser = &sseParser };

    var headers = std.ArrayList(http_client.Header).init(allocator);
    defer headers.deinit();
    try buildHeaders(allocator, config, &headers, true);

    const req = http_client.Request{
        .url = config.url,
        .method = .POST,
        .headers = headers.items,
        .body = json_body,
        .total_timeout_ms = config.timeout_ms,
    };

    return client.streamRequest(allocator, req, LineCtx.onLine, &line_ctx);
}

// ---------------------------------------------------------------------------
// Header builders
// ---------------------------------------------------------------------------

fn buildHeaders(
    allocator: std.mem.Allocator,
    config: ProviderConfig,
    headers: *std.ArrayList(http_client.Header),
    streaming: bool,
) !void {
    _ = streaming; // streaming flag kept for future use (Accept: text/event-stream)

    try headers.append(.{ .name = "Content-Type", .value = "application/json" });

    switch (config.kind) {
        .openai, .bedrock => {
            if (config.api_key.len > 0) {
                // Authorization header stored as a single allocation
                const value = try std.fmt.allocPrint(allocator, "Bearer {s}", .{config.api_key});
                // NOTE: This allocation leaks in the test if not tracked; the
                // caller owns the headers ArrayList (values borrow from this
                // allocator).  In production use an ArenaAllocator per request.
                try headers.append(.{ .name = "Authorization", .value = value });
            }
        },
        .anthropic => {
            try headers.append(.{ .name = "x-api-key", .value = config.api_key });
            try headers.append(.{ .name = "anthropic-version", .value = config.anthropic_version });
        },
    }

    // Append any extra provider-level headers (e.g. AWS SigV4 for Bedrock)
    for (config.extra_headers) |h| {
        try headers.append(h);
    }
}

fn providerFormat(kind: ProviderKind) ProviderFormat {
    return switch (kind) {
        .openai => .openai,
        .anthropic => .anthropic,
        .bedrock => .bedrock,
    };
}

// ---------------------------------------------------------------------------
// Tests — request building and header logic (no real HTTP calls)
// ---------------------------------------------------------------------------

test "buildHeaders openai — includes Authorization and Content-Type" {
    const allocator = std.testing.allocator;
    var headers = std.ArrayList(http_client.Header).init(allocator);
    defer {
        // Free the "Bearer …" value we allocated for the Authorization header
        for (headers.items) |h| {
            if (std.mem.eql(u8, h.name, "Authorization")) {
                allocator.free(h.value);
            }
        }
        headers.deinit();
    }

    const config = ProviderConfig{
        .kind = .openai,
        .url = "https://api.openai.com/v1/chat/completions",
        .api_key = "sk-test",
    };
    try buildHeaders(allocator, config, &headers, false);

    var found_ct = false;
    var found_auth = false;
    for (headers.items) |h| {
        if (std.mem.eql(u8, h.name, "Content-Type")) found_ct = true;
        if (std.mem.eql(u8, h.name, "Authorization")) found_auth = true;
    }
    try std.testing.expect(found_ct);
    try std.testing.expect(found_auth);
}

test "buildHeaders anthropic — includes x-api-key and anthropic-version" {
    const allocator = std.testing.allocator;
    var headers = std.ArrayList(http_client.Header).init(allocator);
    defer headers.deinit();

    const config = ProviderConfig{
        .kind = .anthropic,
        .url = "https://api.anthropic.com/v1/messages",
        .api_key = "sk-ant-test",
    };
    try buildHeaders(allocator, config, &headers, false);

    var found_key = false;
    var found_ver = false;
    for (headers.items) |h| {
        if (std.mem.eql(u8, h.name, "x-api-key")) found_key = true;
        if (std.mem.eql(u8, h.name, "anthropic-version")) found_ver = true;
    }
    try std.testing.expect(found_key);
    try std.testing.expect(found_ver);
}

test "buildHeaders with extra headers" {
    const allocator = std.testing.allocator;
    var headers = std.ArrayList(http_client.Header).init(allocator);
    defer {
        for (headers.items) |h| {
            if (std.mem.eql(u8, h.name, "Authorization")) allocator.free(h.value);
        }
        headers.deinit();
    }

    const extra = [_]http_client.Header{
        .{ .name = "X-Custom", .value = "value" },
    };
    const config = ProviderConfig{
        .kind = .openai,
        .url = "https://api.example.com",
        .api_key = "key",
        .extra_headers = &extra,
    };
    try buildHeaders(allocator, config, &headers, false);

    var found_custom = false;
    for (headers.items) |h| {
        if (std.mem.eql(u8, h.name, "X-Custom")) found_custom = true;
    }
    try std.testing.expect(found_custom);
}

test "providerFormat mapping" {
    try std.testing.expectEqual(ProviderFormat.openai, providerFormat(.openai));
    try std.testing.expectEqual(ProviderFormat.anthropic, providerFormat(.anthropic));
    try std.testing.expectEqual(ProviderFormat.bedrock, providerFormat(.bedrock));
}
