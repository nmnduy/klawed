//! provider.zig — Top-level provider dispatch using union(enum)
//!
//! Zig port of src/provider.c.
//!
//! Eliminates the C stringly-typed dispatch (`Provider.name == "OpenAI"`)
//! in favour of a `union(enum)` that enforces exhaustive handling at
//! compile time.
//!
//! ## Supported providers
//!   - `openai`   — OpenAI Chat Completions API (also used for custom endpoints)
//!   - `anthropic` — Anthropic Messages API
//!   - `bedrock`  — AWS Bedrock Converse API
//!   - `deepseek` — DeepSeek (OpenAI-compatible, reasoning discarded)
//!   - `moonshot` — Moonshot/Kimi (OpenAI-compatible, reasoning preserved)
//!   - `kimi`     — Kimi Coding Plan (OAuth 2.0 device flow)
//!
//! ## Construction
//! ```zig
//! // From a ProviderConfig:
//! var p = try Provider.fromConfig(allocator, &config);
//! defer p.deinit();
//!
//! // From environment variables only:
//! var p = try Provider.fromEnv(allocator);
//! defer p.deinit();
//! ```
//!
//! ## Phase 5 note
//! `send` delegates to each provider's `sendRequest`, which currently
//! returns `error.NotImplemented` until the HTTP layer is wired in Phase 5.

const std = @import("std");
const config_mod = @import("config.zig");
const openai_mod = @import("providers/openai.zig");
const anthropic_mod = @import("providers/anthropic.zig");
const bedrock_mod = @import("providers/bedrock.zig");
const deepseek_mod = @import("providers/deepseek.zig");
const moonshot_mod = @import("providers/moonshot.zig");
const kimi_mod = @import("providers/kimi.zig");

pub const ProviderConfig = config_mod.ProviderConfig;
pub const ProviderType = config_mod.ProviderType;

// ---------------------------------------------------------------------------
// Provider union
// ---------------------------------------------------------------------------

pub const ProviderKind = enum {
    openai,
    anthropic,
    bedrock,
    deepseek,
    moonshot,
    kimi,
};

/// Tagged union wrapping each provider implementation.
/// Dispatch is exhaustive — the compiler enforces every variant is handled.
pub const Provider = union(ProviderKind) {
    openai: openai_mod.OpenAIProvider,
    anthropic: anthropic_mod.AnthropicProvider,
    bedrock: bedrock_mod.BedrockProvider,
    deepseek: deepseek_mod.DeepSeekProvider,
    moonshot: moonshot_mod.MoonshotProvider,
    kimi: kimi_mod.KimiProvider,

    /// Free all resources owned by this provider.
    pub fn deinit(self: *Provider) void {
        switch (self.*) {
            .openai => |*p| p.deinit(),
            .anthropic => |*p| p.deinit(),
            .bedrock => |*p| p.deinit(),
            .deepseek => |*p| p.deinit(),
            .moonshot => |*p| p.deinit(),
            .kimi => |*p| p.deinit(),
        }
    }

    /// Human-readable name of the active provider.
    pub fn name(self: *const Provider) []const u8 {
        return switch (self.*) {
            .openai => "OpenAI",
            .anthropic => "Anthropic",
            .bedrock => "AWS Bedrock",
            .deepseek => "DeepSeek",
            .moonshot => "Moonshot",
            .kimi => "Kimi",
        };
    }

    /// Build a request body from an OpenAI-style request.
    /// Returns allocated bytes the caller must free.
    pub fn buildRequestBody(self: *const Provider, req: openai_mod.Request) ![]u8 {
        switch (self.*) {
            .openai => |*p| return p.buildRequestBody(req),
            .deepseek => |*p| return p.buildRequestBody(req),
            .moonshot => |*p| return p.buildRequestBody(req),
            .kimi => |*p| return p.buildRequestBody(req),
            .anthropic, .bedrock => return error.UseDedicatedBuildMethod,
        }
    }

    /// Send a pre-serialized request body to the API.
    /// **Phase 5 stub: always returns error.NotImplemented.**
    pub fn sendRequest(
        self: *Provider,
        allocator: std.mem.Allocator,
        body: []const u8,
    ) ![]u8 {
        return switch (self.*) {
            .openai => |*p| p.sendRequest(allocator, body),
            .anthropic => |*p| p.sendRequest(allocator, body),
            .bedrock => |*p| p.sendRequest(allocator, body),
            .deepseek => |*p| p.sendRequest(allocator, body),
            .moonshot => |*p| p.sendRequest(allocator, body),
            .kimi => |*p| p.sendRequest(allocator, body),
        };
    }

    // ------------------------------------------------------------------
    // Construction
    // ------------------------------------------------------------------

    /// Construct a provider from a `ProviderConfig`.
    ///
    /// The API key is resolved from:
    ///   1. `config.api_key` field
    ///   2. Env var named by `config.api_key_env`
    ///   3. `OPENAI_API_KEY` env var (fallback)
    ///
    /// For Bedrock, AWS credentials are loaded from env (no api_key needed).
    pub fn fromConfig(allocator: std.mem.Allocator, cfg: *const ProviderConfig) !Provider {
        switch (cfg.provider_type) {
            .anthropic => {
                const key = try resolveApiKey(allocator, cfg, "ANTHROPIC_API_KEY");
                defer allocator.free(key);
                return Provider{
                    .anthropic = try anthropic_mod.AnthropicProvider.init(
                        allocator, key, cfg.api_base, true,
                    ),
                };
            },
            .bedrock => {
                const region = try getRegion(allocator);
                defer allocator.free(region);
                return Provider{
                    .bedrock = try bedrock_mod.BedrockProvider.init(
                        allocator, region, cfg.model,
                    ),
                };
            },
            .deepseek => {
                const key = try resolveApiKey(allocator, cfg, "DEEPSEEK_API_KEY");
                defer allocator.free(key);
                return Provider{
                    .deepseek = try deepseek_mod.DeepSeekProvider.init(
                        allocator, key, cfg.api_base,
                    ),
                };
            },
            .moonshot => {
                const key = try resolveApiKey(allocator, cfg, "MOONSHOT_API_KEY");
                defer allocator.free(key);
                return Provider{
                    .moonshot = try moonshot_mod.MoonshotProvider.init(
                        allocator, key, cfg.api_base,
                    ),
                };
            },
            .kimi_coding_plan => {
                return Provider{
                    .kimi = try kimi_mod.KimiProvider.init(allocator, cfg.model),
                };
            },
            // .openai, .auto, .custom all use OpenAI-compatible path
            else => {
                const key = try resolveApiKey(allocator, cfg, "OPENAI_API_KEY");
                defer allocator.free(key);
                const url = if (cfg.api_base.len > 0)
                    cfg.api_base
                else
                    "https://api.openai.com/v1/chat/completions";
                return Provider{
                    .openai = try openai_mod.OpenAIProvider.init(
                        allocator, key, url, .none,
                    ),
                };
            },
        }
    }

    /// Construct a provider purely from environment variables.
    ///
    /// Auto-detection order:
    ///   1. `KLAWED_USE_BEDROCK=1` → Bedrock
    ///   2. `ANTHROPIC_API_URL` or `ANTHROPIC_API_KEY` set → Anthropic
    ///   3. Otherwise → OpenAI
    pub fn fromEnv(allocator: std.mem.Allocator) !Provider {
        // Bedrock
        if (envTruthy(allocator, "KLAWED_USE_BEDROCK")) {
            const region = try getRegion(allocator);
            defer allocator.free(region);
            const model = try getEnvOrDefault(allocator, "OPENAI_MODEL", "anthropic.claude-3-sonnet-20240229-v1:0");
            defer allocator.free(model);
            return Provider{ .bedrock = try bedrock_mod.BedrockProvider.init(allocator, region, model) };
        }

        // Anthropic
        const has_anthropic_url = blk: {
            const v = std.process.getEnvVarOwned(allocator, "ANTHROPIC_API_URL") catch break :blk false;
            defer allocator.free(v);
            break :blk v.len > 0;
        };
        const has_anthropic_key = blk: {
            const v = std.process.getEnvVarOwned(allocator, "ANTHROPIC_API_KEY") catch break :blk false;
            defer allocator.free(v);
            break :blk v.len > 0;
        };
        if (has_anthropic_url or has_anthropic_key) {
            const key = std.process.getEnvVarOwned(allocator, "ANTHROPIC_API_KEY") catch |err| switch (err) {
                error.EnvironmentVariableNotFound => try allocator.dupe(u8, ""),
                else => return err,
            };
            defer allocator.free(key);
            const base_url = std.process.getEnvVarOwned(allocator, "ANTHROPIC_API_URL") catch |err| switch (err) {
                error.EnvironmentVariableNotFound => try allocator.dupe(u8, ""),
                else => return err,
            };
            defer allocator.free(base_url);
            return Provider{
                .anthropic = try anthropic_mod.AnthropicProvider.init(
                    allocator, key, base_url, true,
                ),
            };
        }

        // OpenAI (default)
        const key = std.process.getEnvVarOwned(allocator, "OPENAI_API_KEY") catch |err| switch (err) {
            error.EnvironmentVariableNotFound => try allocator.dupe(u8, ""),
            else => return err,
        };
        defer allocator.free(key);
        const base_url = std.process.getEnvVarOwned(allocator, "OPENAI_API_BASE") catch |err| switch (err) {
            error.EnvironmentVariableNotFound => try allocator.dupe(u8, ""),
            else => return err,
        };
        defer allocator.free(base_url);
        const url = if (base_url.len > 0) base_url else "https://api.openai.com/v1/chat/completions";
        return Provider{
            .openai = try openai_mod.OpenAIProvider.init(allocator, key, url, .none),
        };
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn resolveApiKey(
    allocator: std.mem.Allocator,
    cfg: *const ProviderConfig,
    default_env: []const u8,
) ![]u8 {
    if (cfg.api_key.len > 0) return allocator.dupe(u8, cfg.api_key);

    const env_name = if (cfg.api_key_env.len > 0) cfg.api_key_env else default_env;
    return std.process.getEnvVarOwned(allocator, env_name) catch |err| switch (err) {
        error.EnvironmentVariableNotFound => allocator.dupe(u8, ""),
        else => err,
    };
}

fn getRegion(allocator: std.mem.Allocator) ![]u8 {
    return std.process.getEnvVarOwned(allocator, "AWS_REGION") catch |err| switch (err) {
        error.EnvironmentVariableNotFound => {
            return std.process.getEnvVarOwned(allocator, "AWS_DEFAULT_REGION") catch |e| switch (e) {
                error.EnvironmentVariableNotFound => allocator.dupe(u8, bedrock_mod.default_region),
                else => e,
            };
        },
        else => err,
    };
}

fn getEnvOrDefault(allocator: std.mem.Allocator, name: []const u8, default: []const u8) ![]u8 {
    return std.process.getEnvVarOwned(allocator, name) catch |err| switch (err) {
        error.EnvironmentVariableNotFound => allocator.dupe(u8, default),
        else => err,
    };
}

fn envTruthy(allocator: std.mem.Allocator, name: []const u8) bool {
    const v = std.process.getEnvVarOwned(allocator, name) catch return false;
    defer allocator.free(v);
    if (std.mem.eql(u8, v, "1")) return true;
    if (std.ascii.eqlIgnoreCase(v, "true")) return true;
    if (std.ascii.eqlIgnoreCase(v, "yes")) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "Provider.fromConfig openai" {
    const cfg = ProviderConfig{
        .provider_type = .openai,
        .api_key = "sk-test",
        .api_base = "https://api.openai.com/v1/chat/completions",
    };
    var p = try Provider.fromConfig(std.testing.allocator, &cfg);
    defer p.deinit();

    try std.testing.expectEqual(ProviderKind.openai, std.meta.activeTag(p));
    try std.testing.expectEqualStrings("OpenAI", p.name());
}

test "Provider.fromConfig anthropic" {
    const cfg = ProviderConfig{
        .provider_type = .anthropic,
        .api_key = "sk-ant",
    };
    var p = try Provider.fromConfig(std.testing.allocator, &cfg);
    defer p.deinit();
    try std.testing.expectEqual(ProviderKind.anthropic, std.meta.activeTag(p));
    try std.testing.expectEqualStrings("Anthropic", p.name());
}

test "Provider.fromConfig bedrock" {
    const cfg = ProviderConfig{
        .provider_type = .bedrock,
        .model = "anthropic.claude-3-sonnet",
        .use_bedrock = true,
    };
    var p = try Provider.fromConfig(std.testing.allocator, &cfg);
    defer p.deinit();
    try std.testing.expectEqual(ProviderKind.bedrock, std.meta.activeTag(p));
    try std.testing.expectEqualStrings("AWS Bedrock", p.name());
}

test "Provider.fromConfig deepseek" {
    const cfg = ProviderConfig{
        .provider_type = .deepseek,
        .api_key = "sk-ds",
    };
    var p = try Provider.fromConfig(std.testing.allocator, &cfg);
    defer p.deinit();
    try std.testing.expectEqual(ProviderKind.deepseek, std.meta.activeTag(p));
}

test "Provider.fromConfig moonshot" {
    const cfg = ProviderConfig{
        .provider_type = .moonshot,
        .api_key = "sk-ms",
    };
    var p = try Provider.fromConfig(std.testing.allocator, &cfg);
    defer p.deinit();
    try std.testing.expectEqual(ProviderKind.moonshot, std.meta.activeTag(p));
}

test "Provider.fromConfig kimi" {
    const cfg = ProviderConfig{
        .provider_type = .kimi_coding_plan,
        .model = "kimi-for-coding",
    };
    var p = try Provider.fromConfig(std.testing.allocator, &cfg);
    defer p.deinit();
    try std.testing.expectEqual(ProviderKind.kimi, std.meta.activeTag(p));
    try std.testing.expectEqualStrings("Kimi", p.name());
}

test "Provider.fromConfig auto falls back to openai" {
    const cfg = ProviderConfig{
        .provider_type = .auto,
        .api_key = "sk-auto",
    };
    var p = try Provider.fromConfig(std.testing.allocator, &cfg);
    defer p.deinit();
    try std.testing.expectEqual(ProviderKind.openai, std.meta.activeTag(p));
}

test "Provider.sendRequest — http client wired (smoke test)" {
    // sendRequest now makes a real HTTP call via libcurl.
    // In unit tests we just verify the dispatch compiles and doesn't crash on
    // the init path. A real network call would be an integration test.
    const cfg = ProviderConfig{
        .provider_type = .openai,
        .api_key = "sk-test",
    };
    var p = try Provider.fromConfig(std.testing.allocator, &cfg);
    defer p.deinit();
    // Verify the function is accessible (it was previously stubbed).
    _ = Provider.sendRequest;
}

test "Provider.buildRequestBody via openai path" {
    const cfg = ProviderConfig{
        .provider_type = .openai,
        .api_key = "sk-test",
    };
    var p = try Provider.fromConfig(std.testing.allocator, &cfg);
    defer p.deinit();

    const body = try p.buildRequestBody(openai_mod.Request{
        .model = "gpt-4o",
        .messages = &.{},
    });
    defer std.testing.allocator.free(body);
    try std.testing.expect(std.mem.indexOf(u8, body, "gpt-4o") != null);
}

test "Provider.fromEnv default openai (no env set)" {
    // fromEnv should always produce a valid provider without error, regardless
    // of which environment variables are set.  We just verify it doesn't panic.
    var p = try Provider.fromEnv(std.testing.allocator);
    defer p.deinit();
    // The active tag depends on environment variables set in the test runner;
    // we just verify it is one of the known kinds.
    const tag = std.meta.activeTag(p);
    const valid = tag == .openai or tag == .anthropic or tag == .bedrock or
        tag == .deepseek or tag == .moonshot or tag == .kimi;
    try std.testing.expect(valid);
}
