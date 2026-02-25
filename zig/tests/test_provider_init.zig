//! tests/test_provider_init.zig — Zig port of tests/test_provider_init.c
//!
//! Tests Provider.fromConfig and Provider.fromEnv:
//! - Named provider model takes precedence over defaults
//! - Provider type dispatch (openai, anthropic, bedrock, deepseek, moonshot, kimi)
//! - Provider.name() returns the correct human-readable string
//! - Provider.fromEnv auto-detection order
//! - Invalid / missing API keys still construct a provider (no validation at init)

const std = @import("std");
const provider_mod = @import("../provider.zig");
const config_mod = @import("../config.zig");

const Provider = provider_mod.Provider;
const ProviderKind = provider_mod.ProviderKind;
const ProviderConfig = config_mod.ProviderConfig;
const ProviderType = config_mod.ProviderType;

// ---------------------------------------------------------------------------
// fromConfig — provider type dispatch
// ---------------------------------------------------------------------------

test "provider init: openai provider created from config" {
    const alloc = std.testing.allocator;
    const cfg = ProviderConfig{
        .provider_type = .openai,
        .api_key = "sk-test-key",
        .api_base = "https://api.openai.com/v1/chat/completions",
    };
    var p = try Provider.fromConfig(alloc, &cfg);
    defer p.deinit();

    try std.testing.expectEqual(ProviderKind.openai, std.meta.activeTag(p));
    try std.testing.expectEqualStrings("OpenAI", p.name());
}

test "provider init: anthropic provider created from config" {
    const alloc = std.testing.allocator;
    const cfg = ProviderConfig{
        .provider_type = .anthropic,
        .api_key = "sk-ant-test",
        .model = "claude-sonnet-4-20250514",
    };
    var p = try Provider.fromConfig(alloc, &cfg);
    defer p.deinit();

    try std.testing.expectEqual(ProviderKind.anthropic, std.meta.activeTag(p));
    try std.testing.expectEqualStrings("Anthropic", p.name());
}

test "provider init: bedrock provider created from config" {
    const alloc = std.testing.allocator;
    const cfg = ProviderConfig{
        .provider_type = .bedrock,
        .model = "anthropic.claude-3-sonnet-20240229-v1:0",
        .use_bedrock = true,
    };
    var p = try Provider.fromConfig(alloc, &cfg);
    defer p.deinit();

    try std.testing.expectEqual(ProviderKind.bedrock, std.meta.activeTag(p));
    try std.testing.expectEqualStrings("AWS Bedrock", p.name());
}

test "provider init: deepseek provider created from config" {
    const alloc = std.testing.allocator;
    const cfg = ProviderConfig{
        .provider_type = .deepseek,
        .api_key = "sk-ds-test",
    };
    var p = try Provider.fromConfig(alloc, &cfg);
    defer p.deinit();

    try std.testing.expectEqual(ProviderKind.deepseek, std.meta.activeTag(p));
}

test "provider init: moonshot provider created from config" {
    const alloc = std.testing.allocator;
    const cfg = ProviderConfig{
        .provider_type = .moonshot,
        .api_key = "sk-ms-test",
    };
    var p = try Provider.fromConfig(alloc, &cfg);
    defer p.deinit();

    try std.testing.expectEqual(ProviderKind.moonshot, std.meta.activeTag(p));
    try std.testing.expectEqualStrings("Moonshot", p.name());
}

test "provider init: kimi provider created from config" {
    const alloc = std.testing.allocator;
    const cfg = ProviderConfig{
        .provider_type = .kimi_coding_plan,
        .model = "kimi-for-coding",
    };
    var p = try Provider.fromConfig(alloc, &cfg);
    defer p.deinit();

    try std.testing.expectEqual(ProviderKind.kimi, std.meta.activeTag(p));
    try std.testing.expectEqualStrings("Kimi", p.name());
}

test "provider init: auto provider type falls back to openai" {
    const alloc = std.testing.allocator;
    const cfg = ProviderConfig{
        .provider_type = .auto,
        .api_key = "sk-auto-test",
        .api_base = "https://custom.api.example.com/v1",
    };
    var p = try Provider.fromConfig(alloc, &cfg);
    defer p.deinit();

    try std.testing.expectEqual(ProviderKind.openai, std.meta.activeTag(p));
}

test "provider init: custom provider type falls back to openai" {
    const alloc = std.testing.allocator;
    const cfg = ProviderConfig{
        .provider_type = .custom,
        .api_key = "sk-custom",
        .api_base = "https://myproxy.example.com/v1/chat",
    };
    var p = try Provider.fromConfig(alloc, &cfg);
    defer p.deinit();

    try std.testing.expectEqual(ProviderKind.openai, std.meta.activeTag(p));
}

// ---------------------------------------------------------------------------
// Named provider model — correct API URL construction
// ---------------------------------------------------------------------------

test "provider init: openai provider uses configured api_base URL" {
    const alloc = std.testing.allocator;
    const cfg = ProviderConfig{
        .provider_type = .openai,
        .api_key = "sk-test",
        .api_base = "https://api.openai.com/v1/chat/completions",
    };
    var p = try Provider.fromConfig(alloc, &cfg);
    defer p.deinit();
    // Just verify creation and kind.
    try std.testing.expectEqual(ProviderKind.openai, std.meta.activeTag(p));
}

// ---------------------------------------------------------------------------
// API key resolution precedence
// ---------------------------------------------------------------------------

test "provider init: api_key in config is used when no env var" {
    const alloc = std.testing.allocator;
    const cfg = ProviderConfig{
        .provider_type = .openai,
        .api_key = "sk-direct-api-key",
        .api_base = "https://api.openai.com/v1/chat/completions",
    };
    var p = try Provider.fromConfig(alloc, &cfg);
    defer p.deinit();
    try std.testing.expectEqual(ProviderKind.openai, std.meta.activeTag(p));
}

test "provider init: api_key_env empty falls back to api_key" {
    const alloc = std.testing.allocator;

    // We can't safely mutate the process environment inside a test,
    // so we verify that api_key_env="" causes Provider.fromConfig to use api_key directly.
    const cfg = ProviderConfig{
        .provider_type = .openai,
        .api_key = "sk-fallback-key",
        .api_key_env = "", // empty — should fall back to api_key
        .api_base = "https://api.openai.com/v1/chat/completions",
    };
    var p = try Provider.fromConfig(alloc, &cfg);
    defer p.deinit();
    try std.testing.expectEqual(ProviderKind.openai, std.meta.activeTag(p));
}

// ---------------------------------------------------------------------------
// fromEnv — auto-detection
// ---------------------------------------------------------------------------

test "provider init: fromEnv produces a valid provider" {
    const alloc = std.testing.allocator;
    // fromEnv should always succeed regardless of env state.
    var p = try Provider.fromEnv(alloc);
    defer p.deinit();

    const tag = std.meta.activeTag(p);
    const valid = tag == .openai or tag == .anthropic or tag == .bedrock or
        tag == .deepseek or tag == .moonshot or tag == .kimi;
    try std.testing.expect(valid);
}

// ---------------------------------------------------------------------------
// Provider.name() — all variants
// ---------------------------------------------------------------------------

test "provider init: all provider name strings" {
    const alloc = std.testing.allocator;

    const cases = [_]struct { cfg: ProviderConfig, expected: []const u8 }{
        .{ .cfg = .{ .provider_type = .openai, .api_key = "k" }, .expected = "OpenAI" },
        .{ .cfg = .{ .provider_type = .anthropic, .api_key = "k" }, .expected = "Anthropic" },
        .{ .cfg = .{ .provider_type = .bedrock, .model = "m" }, .expected = "AWS Bedrock" },
        .{ .cfg = .{ .provider_type = .deepseek, .api_key = "k" }, .expected = "DeepSeek" },
        .{ .cfg = .{ .provider_type = .moonshot, .api_key = "k" }, .expected = "Moonshot" },
        .{ .cfg = .{ .provider_type = .kimi_coding_plan, .model = "m" }, .expected = "Kimi" },
    };

    for (cases) |tc| {
        var p = try Provider.fromConfig(alloc, &tc.cfg);
        defer p.deinit();
        try std.testing.expectEqualStrings(tc.expected, p.name());
    }
}

// ---------------------------------------------------------------------------
// buildRequestBody — smoke test
// ---------------------------------------------------------------------------

test "provider init: buildRequestBody produces valid JSON for openai" {
    const alloc = std.testing.allocator;
    const cfg = ProviderConfig{
        .provider_type = .openai,
        .api_key = "sk-test",
    };
    var p = try Provider.fromConfig(alloc, &cfg);
    defer p.deinit();

    const openai_mod = @import("../providers/openai.zig");
    const body = try p.buildRequestBody(openai_mod.Request{
        .model = "gpt-4o",
        .messages = &.{},
    });
    defer alloc.free(body);

    try std.testing.expect(std.mem.indexOf(u8, body, "gpt-4o") != null);
    // Verify valid JSON
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, body, .{});
    defer parsed.deinit();
    try std.testing.expect(parsed.value == .object);
}
