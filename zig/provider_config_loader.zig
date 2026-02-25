//! provider_config_loader.zig — Unified provider configuration loading
//!
//! Zig port of src/provider_config_loader.c
//!
//! Provides a single-source-of-truth approach to loading LLM provider
//! configuration, merging:
//!   1. Config files (global + local)
//!   2. Synthetic "env" provider built from OPENAI_*, ANTHROPIC_*, KLAWED_* env vars
//!   3. Synthetic "legacy" provider from the old single-provider config field
//!
//! ## Priority for effective provider selection
//!   1. `KLAWED_LLM_PROVIDER` environment variable
//!   2. `active_provider` from config file
//!   3. Synthetic "env" provider
//!   4. Synthetic "legacy" provider

const std = @import("std");
const config_mod = @import("config.zig");

pub const Config = config_mod.Config;
pub const ProviderConfig = config_mod.ProviderConfig;
pub const NamedProvider = config_mod.NamedProvider;
pub const ProviderType = config_mod.ProviderType;

// ---------------------------------------------------------------------------
// Source enum
// ---------------------------------------------------------------------------

pub const EffectiveSource = enum {
    none,
    env_var,          // KLAWED_LLM_PROVIDER
    active_config,    // active_provider in config file
    env_synthetic,    // synthesised from environment variables
    legacy,           // legacy llm_provider in config file

    pub fn description(self: EffectiveSource) []const u8 {
        return switch (self) {
            .none => "none",
            .env_var => "KLAWED_LLM_PROVIDER",
            .active_config => "active_provider",
            .env_synthetic => "environment variables",
            .legacy => "legacy config",
        };
    }
};

// ---------------------------------------------------------------------------
// UnifiedProviderConfig
// ---------------------------------------------------------------------------

/// Maximum number of providers after adding synthetic env + legacy providers.
pub const max_unified_providers = config_mod.max_providers + 2;

/// Unified provider configuration produced by `load`.
pub const UnifiedConfig = struct {
    allocator: std.mem.Allocator,

    /// Base klawed config (loaded from files).
    base: Config,

    /// Flat list of all providers (config-file + synthetic).
    providers: std.ArrayList(NamedProvider),

    /// Index into `providers` for the effective provider (null = none found).
    effective_idx: ?usize,

    /// How the effective provider was selected.
    effective_source: EffectiveSource,

    /// True when an effective provider was found and config is usable.
    is_valid: bool,

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    pub fn deinit(self: *UnifiedConfig) void {
        // Free items added to unified providers beyond what base.deinit handles.
        // Synthetic providers ("env", "legacy") are fully owned here.
        for (self.providers.items) |*np| {
            // Only free providers NOT in base.providers (synthetic ones).
            var found_in_base = false;
            for (self.base.providers.items) |*bp| {
                if (np == bp) {
                    found_in_base = true;
                    break;
                }
            }
            if (!found_in_base) {
                freeNamedProvider(self.allocator, np);
            }
        }
        self.providers.deinit();
        self.base.deinit();
    }

    // ------------------------------------------------------------------
    // Accessors
    // ------------------------------------------------------------------

    pub fn effectiveProvider(self: *const UnifiedConfig) ?*const NamedProvider {
        const idx = self.effective_idx orelse return null;
        if (idx >= self.providers.items.len) return null;
        return &self.providers.items[idx];
    }

    pub fn effectiveName(self: *const UnifiedConfig) ?[]const u8 {
        const ep = self.effectiveProvider() orelse return null;
        return ep.key;
    }

    pub fn effectiveConfig(self: *const UnifiedConfig) ?*const ProviderConfig {
        const ep = self.effectiveProvider() orelse return null;
        return &ep.config;
    }

    pub fn findProvider(self: *const UnifiedConfig, key: []const u8) ?*const NamedProvider {
        for (self.providers.items) |*np| {
            if (std.mem.eql(u8, np.key, key)) return np;
        }
        return null;
    }

    pub fn isBedrock(self: *const UnifiedConfig) bool {
        const pc = self.effectiveConfig() orelse return false;
        return pc.use_bedrock or pc.provider_type == .bedrock;
    }

    pub fn hasApiKey(self: *const UnifiedConfig) bool {
        if (self.isBedrock()) return true; // uses AWS credentials
        const pc = self.effectiveConfig() orelse return false;
        if (pc.api_key.len > 0) return true;
        if (pc.api_key_env.len > 0) return true;
        // Fall back to OPENAI_API_KEY being set
        if (std.process.getEnvVarOwned(self.allocator, "OPENAI_API_KEY")) |v| {
            defer self.allocator.free(v);
            return v.len > 0;
        } else |_| {}
        return false;
    }

    /// Resolve the API key: api_key field, then env var lookup.
    /// Returns null if neither is available.  Caller must free the returned slice.
    pub fn resolveApiKey(self: *const UnifiedConfig, source_out: ?*[]const u8) ?[]const u8 {
        const pc = self.effectiveConfig() orelse return null;

        if (pc.api_key.len > 0) {
            if (source_out) |so| so.* = "config file";
            return self.allocator.dupe(u8, pc.api_key) catch return null;
        }

        const env_name = if (pc.api_key_env.len > 0) pc.api_key_env else "OPENAI_API_KEY";
        const val = std.process.getEnvVarOwned(self.allocator, env_name) catch return null;
        if (val.len == 0) {
            self.allocator.free(val);
            return null;
        }
        if (source_out) |so| so.* = env_name;
        return val;
    }

    pub fn getModel(self: *const UnifiedConfig) ?[]const u8 {
        const pc = self.effectiveConfig() orelse return null;
        if (pc.model.len == 0) return null;
        return pc.model;
    }

    pub fn getApiBase(self: *const UnifiedConfig) ?[]const u8 {
        const pc = self.effectiveConfig() orelse return null;
        if (pc.api_base.len == 0) return null;
        return pc.api_base;
    }

    pub fn getProviderType(self: *const UnifiedConfig) ProviderType {
        const pc = self.effectiveConfig() orelse return .auto;
        return pc.provider_type;
    }
};

// ---------------------------------------------------------------------------
// Public load function
// ---------------------------------------------------------------------------

/// Load and resolve the unified provider configuration.
/// The returned `UnifiedConfig` must be released with `result.deinit()`.
pub fn load(allocator: std.mem.Allocator) !UnifiedConfig {
    var uc = UnifiedConfig{
        .allocator = allocator,
        .base = Config.init(allocator),
        .providers = std.ArrayList(NamedProvider).init(allocator),
        .effective_idx = null,
        .effective_source = .none,
        .is_valid = false,
    };
    errdefer uc.base.deinit();
    errdefer uc.providers.deinit();

    // 1. Load config files (errors ignored — we continue with defaults)
    uc.base = Config.load(allocator) catch Config.init(allocator);

    // 2. Copy config-file providers into unified list
    for (uc.base.providers.items) |*np| {
        try uc.providers.append(np.*);
    }

    // 3. Add synthetic "env" provider if relevant env vars are set
    if (try buildEnvProvider(allocator)) |env_np| {
        if (uc.providers.items.len < max_unified_providers) {
            try uc.providers.append(env_np);
        } else {
            freeNamedProvider(allocator, @constCast(&env_np));
        }
    }

    // 4. Add synthetic "legacy" provider if llm_provider has values
    const lp = &uc.base.llm_provider;
    const has_legacy = lp.model.len > 0 or lp.api_base.len > 0 or lp.api_key.len > 0;
    if (has_legacy) {
        const legacy_np = NamedProvider{
            .key = try allocator.dupe(u8, "legacy"),
            .config = lp.*,
        };
        if (uc.providers.items.len < max_unified_providers) {
            try uc.providers.append(legacy_np);
        } else {
            allocator.free(legacy_np.key);
        }
    }

    // 5. Determine effective provider
    try determineEffective(&uc);

    uc.is_valid = uc.effective_idx != null;
    return uc;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Build a synthetic NamedProvider{"env", ...} from environment variables.
/// Returns null if no meaningful env config is present.
fn buildEnvProvider(allocator: std.mem.Allocator) !?NamedProvider {
    var pc = ProviderConfig{};

    // Detect provider type from env
    const use_bedrock = blk: {
        const v = std.process.getEnvVarOwned(allocator, "KLAWED_USE_BEDROCK") catch |e| switch (e) {
            error.EnvironmentVariableNotFound => break :blk false,
            else => return e,
        };
        defer allocator.free(v);
        break :blk isEnvTruthy(v);
    };

    if (use_bedrock) {
        pc.provider_type = .bedrock;
        pc.use_bedrock = true;
    } else {
        // Auto-detect: if ANTHROPIC_API_URL is set and OPENAI_API_BASE is not,
        // default to anthropic type.
        const has_anthropic_url = blk: {
            const v = std.process.getEnvVarOwned(allocator, "ANTHROPIC_API_URL") catch |e| switch (e) {
                error.EnvironmentVariableNotFound => break :blk false,
                else => return e,
            };
            defer allocator.free(v);
            break :blk v.len > 0;
        };
        const has_openai_base = blk: {
            const v = std.process.getEnvVarOwned(allocator, "OPENAI_API_BASE") catch |e| switch (e) {
                error.EnvironmentVariableNotFound => break :blk false,
                else => return e,
            };
            defer allocator.free(v);
            break :blk v.len > 0;
        };
        if (has_anthropic_url and !has_openai_base) {
            pc.provider_type = .anthropic;
        }
    }

    // Model: OPENAI_MODEL then ANTHROPIC_MODEL
    const model = (try getEnvOpt(allocator, "OPENAI_MODEL")) orelse
        (try getEnvOpt(allocator, "ANTHROPIC_MODEL"));
    if (model) |m| pc.model = m;

    // API base: OPENAI_API_BASE, ANTHROPIC_API_URL, ANTHROPIC_BASE_URL
    const api_base = (try getEnvOpt(allocator, "OPENAI_API_BASE")) orelse
        (try getEnvOpt(allocator, "ANTHROPIC_API_URL")) orelse
        (try getEnvOpt(allocator, "ANTHROPIC_BASE_URL"));
    if (api_base) |ab| pc.api_base = ab;

    // API key env: always default to OPENAI_API_KEY
    pc.api_key_env = "OPENAI_API_KEY";

    // Only create synthetic provider if something meaningful is present
    const has_openai_key = blk: {
        const v = std.process.getEnvVarOwned(allocator, "OPENAI_API_KEY") catch |e| switch (e) {
            error.EnvironmentVariableNotFound => break :blk false,
            else => return e,
        };
        defer allocator.free(v);
        break :blk v.len > 0;
    };
    const has_anything = pc.model.len > 0 or pc.api_base.len > 0 or has_openai_key or pc.use_bedrock;
    if (!has_anything) {
        if (pc.model.len > 0) allocator.free(pc.model);
        if (pc.api_base.len > 0) allocator.free(pc.api_base);
        return null;
    }

    return NamedProvider{
        .key = try allocator.dupe(u8, "env"),
        .config = pc,
    };
}

fn determineEffective(uc: *UnifiedConfig) !void {
    const allocator = uc.allocator;

    // Priority 1: KLAWED_LLM_PROVIDER
    if (try getEnvOpt(allocator, "KLAWED_LLM_PROVIDER")) |env_key| {
        defer allocator.free(env_key);
        for (uc.providers.items, 0..) |*np, idx| {
            if (std.mem.eql(u8, np.key, env_key)) {
                uc.effective_idx = idx;
                uc.effective_source = .env_var;
                return;
            }
        }
        // env var set but provider not found — fall through
    }

    // Priority 2: active_provider from config
    const active = uc.base.active_provider;
    if (active.len > 0) {
        for (uc.providers.items, 0..) |*np, idx| {
            if (std.mem.eql(u8, np.key, active)) {
                uc.effective_idx = idx;
                uc.effective_source = .active_config;
                return;
            }
        }
    }

    // Priority 3: synthetic "env" provider
    for (uc.providers.items, 0..) |*np, idx| {
        if (std.mem.eql(u8, np.key, "env")) {
            uc.effective_idx = idx;
            uc.effective_source = .env_synthetic;
            return;
        }
    }

    // Priority 4: synthetic "legacy" provider
    for (uc.providers.items, 0..) |*np, idx| {
        if (std.mem.eql(u8, np.key, "legacy")) {
            uc.effective_idx = idx;
            uc.effective_source = .legacy;
            return;
        }
    }

    // Nothing found
    uc.effective_idx = null;
    uc.effective_source = .none;
}

fn getEnvOpt(allocator: std.mem.Allocator, name: []const u8) !?[]u8 {
    return std.process.getEnvVarOwned(allocator, name) catch |err| switch (err) {
        error.EnvironmentVariableNotFound => return null,
        else => return err,
    };
}

fn isEnvTruthy(v: []const u8) bool {
    if (std.mem.eql(u8, v, "1")) return true;
    if (std.ascii.eqlIgnoreCase(v, "true")) return true;
    if (std.ascii.eqlIgnoreCase(v, "yes")) return true;
    return false;
}

fn freeNamedProvider(allocator: std.mem.Allocator, np: *NamedProvider) void {
    allocator.free(np.key);
    const pc = &np.config;
    if (pc.provider_name.len > 0) allocator.free(pc.provider_name);
    if (pc.model.len > 0) allocator.free(pc.model);
    if (pc.api_base.len > 0) allocator.free(pc.api_base);
    if (pc.api_key.len > 0) allocator.free(pc.api_key);
    // api_key_env may be a string literal ("OPENAI_API_KEY") — only free if len>0 and
    // it's heap-owned (we always dupe it when we allocate it, so free is safe)
    if (pc.api_key_env.len > 0 and !isStaticLiteral(pc.api_key_env)) allocator.free(pc.api_key_env);
}

/// A rough heuristic: literals appear in rodata and have the same pointer each run.
/// For testing purposes we track whether api_key_env is "OPENAI_API_KEY" literal.
/// In practice, all our strings are either duped (heap) or the literal "OPENAI_API_KEY".
/// We handle this by NOT freeing api_key_env in buildEnvProvider when it's the literal.
fn isStaticLiteral(s: []const u8) bool {
    // We set api_key_env to the literal "OPENAI_API_KEY" in buildEnvProvider without duping.
    return std.mem.eql(u8, s, "OPENAI_API_KEY");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "EffectiveSource.description" {
    try std.testing.expectEqualStrings("none", EffectiveSource.none.description());
    try std.testing.expectEqualStrings("KLAWED_LLM_PROVIDER", EffectiveSource.env_var.description());
    try std.testing.expectEqualStrings("active_provider", EffectiveSource.active_config.description());
    try std.testing.expectEqualStrings("environment variables", EffectiveSource.env_synthetic.description());
    try std.testing.expectEqualStrings("legacy config", EffectiveSource.legacy.description());
}

test "UnifiedConfig with named provider in config selects via active_provider" {
    // Build a Config with one named provider and set active_provider
    var cfg = Config.init(std.testing.allocator);

    try cfg.setProvider("my-provider", ProviderConfig{
        .provider_type = .anthropic,
        .model = "claude-3-5-sonnet",
        .api_key_env = "ANTHROPIC_API_KEY",
    });
    cfg.active_provider = try std.testing.allocator.dupe(u8, "my-provider");

    // Manually build a UnifiedConfig (bypass load() which hits files)
    var uc = UnifiedConfig{
        .allocator = std.testing.allocator,
        .base = cfg,
        .providers = std.ArrayList(NamedProvider).init(std.testing.allocator),
        .effective_idx = null,
        .effective_source = .none,
        .is_valid = false,
    };
    defer {
        // Only deinit providers we added beyond base
        uc.providers.deinit();
        uc.base.deinit();
    }

    // Copy base providers
    for (uc.base.providers.items) |*np| {
        try uc.providers.append(np.*);
    }
    try determineEffective(&uc);
    uc.is_valid = uc.effective_idx != null;

    try std.testing.expect(uc.is_valid);
    try std.testing.expectEqual(EffectiveSource.active_config, uc.effective_source);
    const name = uc.effectiveName() orelse return error.TestUnexpectedNull;
    try std.testing.expectEqualStrings("my-provider", name);
    const pt = uc.getProviderType();
    try std.testing.expectEqual(ProviderType.anthropic, pt);
}

test "isEnvTruthy" {
    try std.testing.expect(isEnvTruthy("1"));
    try std.testing.expect(isEnvTruthy("true"));
    try std.testing.expect(isEnvTruthy("True"));
    try std.testing.expect(isEnvTruthy("yes"));
    try std.testing.expect(!isEnvTruthy("0"));
    try std.testing.expect(!isEnvTruthy("false"));
    try std.testing.expect(!isEnvTruthy("no"));
}
