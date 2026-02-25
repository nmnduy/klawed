//! providers/deepseek.zig — DeepSeek API provider
//!
//! Zig port of src/deepseek_provider.c.
//!
//! DeepSeek uses an OpenAI-compatible API but MUST NOT include
//! `reasoning_content` in subsequent requests (discard mode).
//! It is a thin wrapper around OpenAI provider with a different default URL.

const std = @import("std");
const openai = @import("openai.zig");

pub const default_url = "https://api.deepseek.com/v1/chat/completions";

// Re-export OpenAI types for convenience
pub const Request = openai.Request;
pub const Response = openai.Response;
pub const Message = openai.Message;
pub const ContentBlock = openai.ContentBlock;
pub const ToolDefinition = openai.ToolDefinition;
pub const Role = openai.Role;

pub const DeepSeekProvider = struct {
    inner: openai.OpenAIProvider,

    pub fn init(
        allocator: std.mem.Allocator,
        api_key: []const u8,
        base_url: []const u8,
    ) !DeepSeekProvider {
        const url = if (base_url.len > 0) base_url else default_url;
        return DeepSeekProvider{
            .inner = try openai.OpenAIProvider.init(
                allocator,
                api_key,
                url,
                .discard, // DeepSeek: discard reasoning_content
            ),
        };
    }

    pub fn deinit(self: *DeepSeekProvider) void {
        self.inner.deinit();
    }

    pub fn buildRequestBody(self: *const DeepSeekProvider, req: Request) ![]u8 {
        return self.inner.buildRequestBody(req);
    }

    /// HTTP stub — Phase 5 will implement this.
    pub fn sendRequest(
        self: *DeepSeekProvider,
        allocator: std.mem.Allocator,
        body: []const u8,
    ) ![]u8 {
        _ = self;
        _ = allocator;
        _ = body;
        return error.NotImplemented;
    }

    pub fn parseResponse(self: *const DeepSeekProvider, json_body: []const u8) !Response {
        return self.inner.parseResponse(json_body);
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "DeepSeekProvider uses discard reasoning mode" {
    var p = try DeepSeekProvider.init(std.testing.allocator, "sk-deepseek", "");
    defer p.deinit();

    try std.testing.expectEqual(openai.ReasoningMode.discard, p.inner.reasoning_mode);
    try std.testing.expectEqualStrings(default_url, p.inner.base_url);
}

test "DeepSeekProvider uses custom base_url" {
    var p = try DeepSeekProvider.init(std.testing.allocator, "sk-test", "https://custom.example.com/v1");
    defer p.deinit();
    try std.testing.expectEqualStrings("https://custom.example.com/v1", p.inner.base_url);
}

test "DeepSeekProvider buildRequestBody produces valid JSON" {
    var p = try DeepSeekProvider.init(std.testing.allocator, "sk-test", "");
    defer p.deinit();

    const body = try p.buildRequestBody(Request{
        .model = "deepseek-coder",
        .messages = &.{},
    });
    defer std.testing.allocator.free(body);

    const parsed = try std.json.parseFromSlice(std.json.Value, std.testing.allocator, body, .{});
    defer parsed.deinit();
    try std.testing.expectEqualStrings("deepseek-coder", parsed.value.object.get("model").?.string);
}

test "DeepSeekProvider sendRequest returns NotImplemented" {
    var p = try DeepSeekProvider.init(std.testing.allocator, "sk-test", "");
    defer p.deinit();
    try std.testing.expectError(error.NotImplemented, p.sendRequest(std.testing.allocator, "{}"));
}
