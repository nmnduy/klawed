//! config.zig — Configuration persistence for klawed
//!
//! Zig port of src/config.c and src/config.h.
//!
//! Key C→Zig translations:
//!   - `cJSON_Parse`        → `std.json.parseFromSlice`
//!   - `char[N]` fields     → `[]u8` slices owned by an arena allocator
//!   - `KlawedConfig`       → `Config` struct with idiomatic Zig defaults
//!   - Global/local merge   → explicit two-pass load (global then local overrides)
//!
//! ## Usage
//!
//! ```zig
//! var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
//! defer arena.deinit();
//! var cfg = try Config.load(arena.allocator());
//! defer cfg.deinit(arena.allocator());
//! ```

const std = @import("std");

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

pub const config_file_name = "config.json";
pub const global_config_dir = ".klawed";
pub const max_providers = 15;

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

/// TUI input box visual style (matches TUIInputBoxStyle in C).
pub const InputBoxStyle = enum {
    background,
    border,
    horizontal,
    bland,

    pub fn toString(self: InputBoxStyle) []const u8 {
        return switch (self) {
            .background => "background",
            .border => "border",
            .horizontal => "horizontal",
            .bland => "bland",
        };
    }

    pub fn fromString(s: []const u8) InputBoxStyle {
        if (std.mem.eql(u8, s, "background")) return .background;
        if (std.mem.eql(u8, s, "border")) return .border;
        if (std.mem.eql(u8, s, "horizontal")) return .horizontal;
        return .bland;
    }
};

/// TUI response style (matches TUIResponseStyle in C).
pub const ResponseStyle = enum {
    caret,
    border,

    pub fn toString(self: ResponseStyle) []const u8 {
        return switch (self) {
            .caret => "caret",
            .border => "border",
        };
    }

    pub fn fromString(s: []const u8) ResponseStyle {
        if (std.mem.eql(u8, s, "caret")) return .caret;
        return .border;
    }
};

/// LLM provider type (matches LLMProviderType enum in C).
pub const ProviderType = enum {
    auto,
    openai,
    anthropic,
    bedrock,
    deepseek,
    moonshot,
    kimi_coding_plan,
    custom,

    pub fn toString(self: ProviderType) []const u8 {
        return switch (self) {
            .auto => "auto",
            .openai => "openai",
            .anthropic => "anthropic",
            .bedrock => "bedrock",
            .deepseek => "deepseek",
            .moonshot => "moonshot",
            .kimi_coding_plan => "kimi_coding_plan",
            .custom => "custom",
        };
    }

    pub fn fromString(s: []const u8) ProviderType {
        if (std.mem.eql(u8, s, "openai")) return .openai;
        if (std.mem.eql(u8, s, "anthropic")) return .anthropic;
        if (std.mem.eql(u8, s, "bedrock")) return .bedrock;
        if (std.mem.eql(u8, s, "deepseek")) return .deepseek;
        if (std.mem.eql(u8, s, "moonshot") or std.mem.eql(u8, s, "kimi")) return .moonshot;
        if (std.mem.eql(u8, s, "kimi_coding_plan")) return .kimi_coding_plan;
        if (std.mem.eql(u8, s, "custom")) return .custom;
        return .auto;
    }
};

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

/// Per-provider configuration block (matches LLMProviderConfig in C).
/// All string fields are owned by the allocator passed to Config.load / Config.deinit.
pub const ProviderConfig = struct {
    provider_type: ProviderType = .auto,
    provider_name: []const u8 = "",
    model: []const u8 = "",
    api_base: []const u8 = "",
    /// Stored API key (prefer api_key_env for security).
    api_key: []const u8 = "",
    /// Name of the env variable that holds the API key.
    api_key_env: []const u8 = "",
    use_bedrock: bool = false,
};

/// Named provider configuration (key + config pair).
pub const NamedProvider = struct {
    key: []const u8,
    config: ProviderConfig,
};

/// Top-level klawed configuration (matches KlawedConfig in C).
pub const Config = struct {
    allocator: std.mem.Allocator,

    input_box_style: InputBoxStyle = .horizontal,
    response_style: ResponseStyle = .border,
    theme: []const u8 = "",

    /// Legacy single-provider field (preserved for backwards compat).
    llm_provider: ProviderConfig = .{},

    /// Named provider list (corresponds to "providers" JSON object).
    providers: std.ArrayList(NamedProvider),

    /// Key of the currently active named provider.
    active_provider: []const u8 = "",

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    /// Create a Config with all-defaults.  Caller must call `deinit` when done.
    pub fn init(allocator: std.mem.Allocator) Config {
        return Config{
            .allocator = allocator,
            .providers = std.ArrayList(NamedProvider).init(allocator),
        };
    }

    /// Free all allocator-owned memory within this Config.
    pub fn deinit(self: *Config) void {
        const a = self.allocator;
        freeIfOwned(a, self.theme);
        freeIfOwned(a, self.active_provider);
        freeProviderConfig(a, &self.llm_provider);
        for (self.providers.items) |*np| {
            freeIfOwned(a, np.key);
            freeProviderConfig(a, &np.config);
        }
        self.providers.deinit();
    }

    // ------------------------------------------------------------------
    // Loading
    // ------------------------------------------------------------------

    /// Load configuration, merging global (~/.klawed/config.json) then
    /// local (.klawed/config.json). Returns a fully initialised Config.
    pub fn load(allocator: std.mem.Allocator) !Config {
        var cfg = Config.init(allocator);

        // Global path: $HOME/.klawed/config.json
        if (try globalConfigPath(allocator)) |gp| {
            defer allocator.free(gp);
            cfg.loadFromFile(gp, "global") catch {};
        }

        // Local path: <data_dir>/config.json
        const local_path = try localConfigPath(allocator);
        defer allocator.free(local_path);
        cfg.loadFromFile(local_path, "local") catch {};

        return cfg;
    }

    /// Load (overlay) settings from a single JSON file into this Config.
    /// Unknown fields are silently ignored.  Errors return without modifying
    /// previously-loaded settings.
    pub fn loadFromFile(self: *Config, path: []const u8, label: []const u8) !void {
        _ = label; // used only for debug log in C; callers can log themselves

        const file = std.fs.openFileAbsolute(path, .{}) catch |err| switch (err) {
            error.FileNotFound => return error.FileNotFound,
            else => return err,
        };
        defer file.close();

        const max_size = 1024 * 1024; // 1 MiB limit
        const contents = try file.readToEndAlloc(self.allocator, max_size);
        defer self.allocator.free(contents);

        try self.loadFromSlice(contents);
    }

    /// Load (overlay) settings from a JSON byte slice.
    pub fn loadFromSlice(self: *Config, json_text: []const u8) !void {
        const parsed = try std.json.parseFromSlice(
            std.json.Value,
            self.allocator,
            json_text,
            .{ .ignore_unknown_fields = true },
        );
        defer parsed.deinit();

        const root = parsed.value;
        if (root != .object) return error.InvalidJson;

        // input_box_style
        if (root.object.get("input_box_style")) |v| {
            if (v == .string) self.input_box_style = InputBoxStyle.fromString(v.string);
        }

        // response_style
        if (root.object.get("response_style")) |v| {
            if (v == .string) self.response_style = ResponseStyle.fromString(v.string);
        }

        // theme
        if (root.object.get("theme")) |v| {
            if (v == .string and v.string.len > 0) {
                freeIfOwned(self.allocator, self.theme);
                self.theme = try self.allocator.dupe(u8, v.string);
            }
        }

        // active_provider
        if (root.object.get("active_provider")) |v| {
            if (v == .string and v.string.len > 0) {
                freeIfOwned(self.allocator, self.active_provider);
                self.active_provider = try self.allocator.dupe(u8, v.string);
            }
        }

        // legacy llm_provider object
        if (root.object.get("llm_provider")) |v| {
            if (v == .object) {
                try self.loadProviderConfigFromJson(v, &self.llm_provider);
            }
        }

        // providers map  {"key": { ... }, ...}
        if (root.object.get("providers")) |v| {
            if (v == .object) {
                var it = v.object.iterator();
                while (it.next()) |entry| {
                    const key = entry.key_ptr.*;
                    const val = entry.value_ptr.*;
                    if (val != .object) continue;

                    // Find existing provider with this key or create new
                    var found_idx: ?usize = null;
                    for (self.providers.items, 0..) |np, idx| {
                        if (std.mem.eql(u8, np.key, key)) {
                            found_idx = idx;
                            break;
                        }
                    }

                    if (found_idx) |idx| {
                        // Update in-place (local config overrides global)
                        try self.loadProviderConfigFromJson(val, &self.providers.items[idx].config);
                    } else {
                        if (self.providers.items.len >= max_providers) continue;
                        var np = NamedProvider{
                            .key = try self.allocator.dupe(u8, key),
                            .config = .{},
                        };
                        try self.loadProviderConfigFromJson(val, &np.config);
                        try self.providers.append(np);
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Saving
    // ------------------------------------------------------------------

    /// Serialize this config to JSON and write to the local config file.
    pub fn save(self: *const Config) !void {
        const local_path = try localConfigPath(self.allocator);
        defer self.allocator.free(local_path);

        // Ensure parent directory exists
        if (std.fs.path.dirname(local_path)) |dir| {
            try std.fs.makeDirAbsolute(dir);
        } else |_| {}

        var buf = std.ArrayList(u8).init(self.allocator);
        defer buf.deinit();
        try self.writeJson(buf.writer());

        const file = try std.fs.createFileAbsolute(local_path, .{ .truncate = true });
        defer file.close();
        try file.writeAll(buf.items);
    }

    /// Write config as JSON to `writer`.
    pub fn writeJson(self: *const Config, writer: anytype) !void {
        var jw = std.json.writeStream(writer, .{ .whitespace = .indent_2 });
        try jw.beginObject();

        try jw.objectField("input_box_style");
        try jw.write(self.input_box_style.toString());

        try jw.objectField("response_style");
        try jw.write(self.response_style.toString());

        if (self.theme.len > 0) {
            try jw.objectField("theme");
            try jw.write(self.theme);
        }

        if (self.active_provider.len > 0) {
            try jw.objectField("active_provider");
            try jw.write(self.active_provider);
        }

        // Only emit llm_provider if it has non-default values
        if (providerConfigHasValues(&self.llm_provider)) {
            try jw.objectField("llm_provider");
            try writeProviderConfigJson(&jw, &self.llm_provider);
        }

        // Emit named providers
        if (self.providers.items.len > 0) {
            try jw.objectField("providers");
            try jw.beginObject();
            for (self.providers.items) |*np| {
                try jw.objectField(np.key);
                try writeProviderConfigJson(&jw, &np.config);
            }
            try jw.endObject();
        }

        try jw.endObject();
    }

    // ------------------------------------------------------------------
    // Provider helpers
    // ------------------------------------------------------------------

    /// Find a named provider by key. Returns null if not found.
    pub fn findProvider(self: *const Config, key: []const u8) ?*const NamedProvider {
        for (self.providers.items) |*np| {
            if (std.mem.eql(u8, np.key, key)) return np;
        }
        return null;
    }

    /// Get the active named provider. Returns null if active_provider is unset or not found.
    pub fn getActiveProvider(self: *const Config) ?*const NamedProvider {
        if (self.active_provider.len == 0) return null;
        return self.findProvider(self.active_provider);
    }

    /// Add or update a named provider. Returns error if max_providers would be exceeded.
    /// All string fields in `pc` are deep-copied into the Config's allocator.
    pub fn setProvider(self: *Config, key: []const u8, pc: ProviderConfig) !void {
        const owned = try dupeProviderConfig(self.allocator, pc);
        for (self.providers.items) |*np| {
            if (std.mem.eql(u8, np.key, key)) {
                freeProviderConfig(self.allocator, &np.config);
                np.config = owned;
                return;
            }
        }
        if (self.providers.items.len >= max_providers) return error.TooManyProviders;
        try self.providers.append(NamedProvider{
            .key = try self.allocator.dupe(u8, key),
            .config = owned,
        });
    }

    /// Remove a named provider by key. Returns error.ProviderNotFound if absent.
    pub fn removeProvider(self: *Config, key: []const u8) !void {
        for (self.providers.items, 0..) |np, idx| {
            if (std.mem.eql(u8, np.key, key)) {
                var removed = self.providers.orderedRemove(idx);
                freeIfOwned(self.allocator, removed.key);
                freeProviderConfig(self.allocator, &removed.config);
                if (std.mem.eql(u8, self.active_provider, key)) {
                    freeIfOwned(self.allocator, self.active_provider);
                    self.active_provider = "";
                }
                return;
            }
        }
        return error.ProviderNotFound;
    }

    /// Set the active provider by key. Returns error.ProviderNotFound if absent.
    pub fn setActiveProvider(self: *Config, key: []const u8) !void {
        if (self.findProvider(key) == null) return error.ProviderNotFound;
        freeIfOwned(self.allocator, self.active_provider);
        self.active_provider = try self.allocator.dupe(u8, key);
    }

    // ------------------------------------------------------------------
    // Internal helpers
    // ------------------------------------------------------------------

    fn loadProviderConfigFromJson(self: *Config, val: std.json.Value, pc: *ProviderConfig) !void {
        if (val.object.get("provider_type")) |v| {
            if (v == .string) pc.provider_type = ProviderType.fromString(v.string);
        }
        if (val.object.get("provider_name")) |v| {
            if (v == .string and v.string.len > 0) {
                freeIfOwned(self.allocator, pc.provider_name);
                pc.provider_name = try self.allocator.dupe(u8, v.string);
            }
        }
        if (val.object.get("model")) |v| {
            if (v == .string and v.string.len > 0) {
                freeIfOwned(self.allocator, pc.model);
                pc.model = try self.allocator.dupe(u8, v.string);
            }
        }
        if (val.object.get("api_base")) |v| {
            if (v == .string and v.string.len > 0) {
                freeIfOwned(self.allocator, pc.api_base);
                pc.api_base = try self.allocator.dupe(u8, v.string);
            }
        }
        if (val.object.get("api_key")) |v| {
            if (v == .string and v.string.len > 0) {
                freeIfOwned(self.allocator, pc.api_key);
                pc.api_key = try self.allocator.dupe(u8, v.string);
            }
        }
        if (val.object.get("api_key_env")) |v| {
            if (v == .string and v.string.len > 0) {
                freeIfOwned(self.allocator, pc.api_key_env);
                pc.api_key_env = try self.allocator.dupe(u8, v.string);
            }
        }
        if (val.object.get("use_bedrock")) |v| {
            pc.use_bedrock = switch (v) {
                .bool => v.bool,
                .integer => v.integer != 0,
                .float => v.float != 0.0,
                else => false,
            };
        }
    }
};

// ---------------------------------------------------------------------------
// Free helpers
// ---------------------------------------------------------------------------

/// Free a slice if it is non-empty (we use empty string as sentinel for
/// "unset / static literal, don't free").
fn freeIfOwned(allocator: std.mem.Allocator, s: []const u8) void {
    if (s.len > 0) allocator.free(s);
}

fn freeProviderConfig(allocator: std.mem.Allocator, pc: *ProviderConfig) void {
    freeIfOwned(allocator, pc.provider_name);
    freeIfOwned(allocator, pc.model);
    freeIfOwned(allocator, pc.api_base);
    freeIfOwned(allocator, pc.api_key);
    freeIfOwned(allocator, pc.api_key_env);
}

/// Deep-copy all string fields in `pc` using `allocator`.
/// The returned ProviderConfig owns all its strings.
fn dupeProviderConfig(allocator: std.mem.Allocator, pc: ProviderConfig) !ProviderConfig {
    return ProviderConfig{
        .provider_type = pc.provider_type,
        .provider_name = if (pc.provider_name.len > 0) try allocator.dupe(u8, pc.provider_name) else "",
        .model = if (pc.model.len > 0) try allocator.dupe(u8, pc.model) else "",
        .api_base = if (pc.api_base.len > 0) try allocator.dupe(u8, pc.api_base) else "",
        .api_key = if (pc.api_key.len > 0) try allocator.dupe(u8, pc.api_key) else "",
        .api_key_env = if (pc.api_key_env.len > 0) try allocator.dupe(u8, pc.api_key_env) else "",
        .use_bedrock = pc.use_bedrock,
    };
}

fn providerConfigHasValues(pc: *const ProviderConfig) bool {
    return pc.provider_type != .auto or
        pc.provider_name.len > 0 or
        pc.model.len > 0 or
        pc.api_base.len > 0 or
        pc.api_key.len > 0 or
        pc.api_key_env.len > 0 or
        pc.use_bedrock;
}

fn writeProviderConfigJson(jw: anytype, pc: *const ProviderConfig) !void {
    try jw.beginObject();
    try jw.objectField("provider_type");
    try jw.write(pc.provider_type.toString());
    if (pc.provider_name.len > 0) {
        try jw.objectField("provider_name");
        try jw.write(pc.provider_name);
    }
    if (pc.model.len > 0) {
        try jw.objectField("model");
        try jw.write(pc.model);
    }
    if (pc.api_base.len > 0) {
        try jw.objectField("api_base");
        try jw.write(pc.api_base);
    }
    if (pc.api_key.len > 0) {
        try jw.objectField("api_key");
        try jw.write(pc.api_key);
    }
    if (pc.api_key_env.len > 0) {
        try jw.objectField("api_key_env");
        try jw.write(pc.api_key_env);
    }
    if (pc.use_bedrock) {
        try jw.objectField("use_bedrock");
        try jw.write(true);
    }
    try jw.endObject();
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

/// Returns the absolute path to the global config file, or null if $HOME is unset.
/// Caller must free the returned slice.
fn globalConfigPath(allocator: std.mem.Allocator) !?[]u8 {
    const home = std.process.getEnvVarOwned(allocator, "HOME") catch |err| switch (err) {
        error.EnvironmentVariableNotFound => return null,
        else => return err,
    };
    defer allocator.free(home);
    return std.fs.path.join(allocator, &.{ home, global_config_dir, config_file_name });
}

/// Returns the absolute path to the local data-dir config file.
/// Caller must free the returned slice.
fn localConfigPath(allocator: std.mem.Allocator) ![]u8 {
    const data_dir_env = std.process.getEnvVarOwned(allocator, "KLAWED_DATA_DIR") catch |err| switch (err) {
        error.EnvironmentVariableNotFound => return buildLocalPath(allocator, ".klawed"),
        else => return err,
    };
    defer allocator.free(data_dir_env);
    return buildLocalPath(allocator, data_dir_env);
}

fn buildLocalPath(allocator: std.mem.Allocator, base: []const u8) ![]u8 {
    // Resolve base relative to cwd if not absolute
    if (std.fs.path.isAbsolute(base)) {
        return std.fs.path.join(allocator, &.{ base, config_file_name });
    }
    const cwd = try std.process.getCwdAlloc(allocator);
    defer allocator.free(cwd);
    return std.fs.path.join(allocator, &.{ cwd, base, config_file_name });
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "ProviderType round-trip" {
    const types = [_]ProviderType{ .auto, .openai, .anthropic, .bedrock, .deepseek, .moonshot, .kimi_coding_plan, .custom };
    for (types) |t| {
        try std.testing.expectEqual(t, ProviderType.fromString(t.toString()));
    }
}

test "InputBoxStyle round-trip" {
    const styles = [_]InputBoxStyle{ .background, .border, .horizontal, .bland };
    for (styles) |s| {
        try std.testing.expectEqual(s, InputBoxStyle.fromString(s.toString()));
    }
}

test "ResponseStyle round-trip" {
    const styles = [_]ResponseStyle{ .caret, .border };
    for (styles) |s| {
        try std.testing.expectEqual(s, ResponseStyle.fromString(s.toString()));
    }
}

test "Config defaults" {
    var cfg = Config.init(std.testing.allocator);
    defer cfg.deinit();

    try std.testing.expectEqual(InputBoxStyle.horizontal, cfg.input_box_style);
    try std.testing.expectEqual(ResponseStyle.border, cfg.response_style);
    try std.testing.expectEqualStrings("", cfg.theme);
    try std.testing.expectEqual(@as(usize, 0), cfg.providers.items.len);
}

test "Config.loadFromSlice basic fields" {
    const json =
        \\{
        \\  "input_box_style": "border",
        \\  "response_style": "caret",
        \\  "theme": "monokai",
        \\  "active_provider": "mydev"
        \\}
    ;
    var cfg = Config.init(std.testing.allocator);
    defer cfg.deinit();

    try cfg.loadFromSlice(json);

    try std.testing.expectEqual(InputBoxStyle.border, cfg.input_box_style);
    try std.testing.expectEqual(ResponseStyle.caret, cfg.response_style);
    try std.testing.expectEqualStrings("monokai", cfg.theme);
    try std.testing.expectEqualStrings("mydev", cfg.active_provider);
}

test "Config.loadFromSlice llm_provider" {
    const json =
        \\{
        \\  "llm_provider": {
        \\    "provider_type": "anthropic",
        \\    "model": "claude-3-sonnet",
        \\    "api_base": "https://api.anthropic.com/v1/messages",
        \\    "api_key_env": "ANTHROPIC_API_KEY"
        \\  }
        \\}
    ;
    var cfg = Config.init(std.testing.allocator);
    defer cfg.deinit();

    try cfg.loadFromSlice(json);

    try std.testing.expectEqual(ProviderType.anthropic, cfg.llm_provider.provider_type);
    try std.testing.expectEqualStrings("claude-3-sonnet", cfg.llm_provider.model);
    try std.testing.expectEqualStrings("ANTHROPIC_API_KEY", cfg.llm_provider.api_key_env);
}

test "Config.loadFromSlice named providers" {
    const json =
        \\{
        \\  "providers": {
        \\    "dev": {
        \\      "provider_type": "openai",
        \\      "model": "gpt-4o",
        \\      "api_key_env": "OPENAI_API_KEY"
        \\    },
        \\    "bedrock-prod": {
        \\      "provider_type": "bedrock",
        \\      "model": "anthropic.claude-3-sonnet-20240229-v1:0",
        \\      "use_bedrock": true
        \\    }
        \\  },
        \\  "active_provider": "dev"
        \\}
    ;
    var cfg = Config.init(std.testing.allocator);
    defer cfg.deinit();

    try cfg.loadFromSlice(json);

    try std.testing.expectEqual(@as(usize, 2), cfg.providers.items.len);
    try std.testing.expectEqualStrings("dev", cfg.active_provider);

    const dev = cfg.findProvider("dev") orelse return error.TestUnexpectedNull;
    try std.testing.expectEqual(ProviderType.openai, dev.config.provider_type);
    try std.testing.expectEqualStrings("gpt-4o", dev.config.model);

    const bp = cfg.findProvider("bedrock-prod") orelse return error.TestUnexpectedNull;
    try std.testing.expectEqual(ProviderType.bedrock, bp.config.provider_type);
    try std.testing.expect(bp.config.use_bedrock);
}

test "Config global-then-local merge (local overrides)" {
    // Simulate loading global then local
    var cfg = Config.init(std.testing.allocator);
    defer cfg.deinit();

    const global_json =
        \\{"input_box_style": "bland", "response_style": "caret", "theme": "default"}
    ;
    const local_json =
        \\{"response_style": "border", "theme": "monokai"}
    ;

    try cfg.loadFromSlice(global_json);
    try cfg.loadFromSlice(local_json); // local overrides

    try std.testing.expectEqual(InputBoxStyle.bland, cfg.input_box_style); // preserved from global
    try std.testing.expectEqual(ResponseStyle.border, cfg.response_style); // overridden by local
    try std.testing.expectEqualStrings("monokai", cfg.theme);              // overridden by local
}

test "Config.setProvider and findProvider" {
    var cfg = Config.init(std.testing.allocator);
    defer cfg.deinit();

    const pc = ProviderConfig{
        .provider_type = .openai,
        .model = "gpt-4o",
    };
    try cfg.setProvider("test-key", pc);

    const found = cfg.findProvider("test-key") orelse return error.TestUnexpectedNull;
    try std.testing.expectEqual(ProviderType.openai, found.config.provider_type);
    try std.testing.expectEqualStrings("gpt-4o", found.config.model);
}

test "Config.removeProvider" {
    var cfg = Config.init(std.testing.allocator);
    defer cfg.deinit();

    try cfg.setProvider("to-remove", ProviderConfig{ .provider_type = .openai });
    try std.testing.expectEqual(@as(usize, 1), cfg.providers.items.len);

    try cfg.removeProvider("to-remove");
    try std.testing.expectEqual(@as(usize, 0), cfg.providers.items.len);

    try std.testing.expectError(error.ProviderNotFound, cfg.removeProvider("to-remove"));
}

test "Config.setActiveProvider" {
    var cfg = Config.init(std.testing.allocator);
    defer cfg.deinit();

    try cfg.setProvider("myp", ProviderConfig{ .provider_type = .bedrock, .use_bedrock = true });
    try cfg.setActiveProvider("myp");
    try std.testing.expectEqualStrings("myp", cfg.active_provider);

    const got = cfg.getActiveProvider() orelse return error.TestUnexpectedNull;
    try std.testing.expectEqual(ProviderType.bedrock, got.config.provider_type);
}

test "Config.writeJson round-trip" {
    var cfg = Config.init(std.testing.allocator);
    defer cfg.deinit();

    cfg.input_box_style = .border;
    cfg.response_style = .caret;

    try cfg.setProvider("p1", ProviderConfig{
        .provider_type = .anthropic,
        .model = "claude-3-5-sonnet",
        .api_key_env = "ANT_KEY",
    });

    var buf = std.ArrayList(u8).init(std.testing.allocator);
    defer buf.deinit();
    try cfg.writeJson(buf.writer());

    // Parse back and verify
    var cfg2 = Config.init(std.testing.allocator);
    defer cfg2.deinit();
    try cfg2.loadFromSlice(buf.items);

    try std.testing.expectEqual(InputBoxStyle.border, cfg2.input_box_style);
    try std.testing.expectEqual(ResponseStyle.caret, cfg2.response_style);
    const p1 = cfg2.findProvider("p1") orelse return error.TestUnexpectedNull;
    try std.testing.expectEqual(ProviderType.anthropic, p1.config.provider_type);
    try std.testing.expectEqualStrings("claude-3-5-sonnet", p1.config.model);
}

test "Config.loadFromSlice use_bedrock as integer" {
    const json =
        \\{"llm_provider": {"provider_type": "bedrock", "use_bedrock": 1}}
    ;
    var cfg = Config.init(std.testing.allocator);
    defer cfg.deinit();
    try cfg.loadFromSlice(json);
    try std.testing.expect(cfg.llm_provider.use_bedrock);
}

test "Config.loadFromSlice unknown fields ignored" {
    const json =
        \\{"future_feature": 42, "input_box_style": "horizontal"}
    ;
    var cfg = Config.init(std.testing.allocator);
    defer cfg.deinit();
    try cfg.loadFromSlice(json);
    try std.testing.expectEqual(InputBoxStyle.horizontal, cfg.input_box_style);
}
