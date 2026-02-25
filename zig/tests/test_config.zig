//! tests/test_config.zig — Zig port of tests/test_config.c
//!
//! Tests config.zig: InputBoxStyle conversion, ResponseStyle, ProviderType,
//! and Config init/defaults.

const std = @import("std");
const config_mod = @import("../config.zig");

const InputBoxStyle = config_mod.InputBoxStyle;
const ResponseStyle = config_mod.ResponseStyle;
const ProviderType = config_mod.ProviderType;
const Config = config_mod.Config;

// ---------------------------------------------------------------------------
// InputBoxStyle
// ---------------------------------------------------------------------------

test "config: InputBoxStyle.toString" {
    try std.testing.expectEqualStrings("bland", InputBoxStyle.bland.toString());
    try std.testing.expectEqualStrings("background", InputBoxStyle.background.toString());
    try std.testing.expectEqualStrings("border", InputBoxStyle.border.toString());
    try std.testing.expectEqualStrings("horizontal", InputBoxStyle.horizontal.toString());
}

test "config: InputBoxStyle.fromString known values" {
    try std.testing.expectEqual(InputBoxStyle.bland, InputBoxStyle.fromString("bland"));
    try std.testing.expectEqual(InputBoxStyle.background, InputBoxStyle.fromString("background"));
    try std.testing.expectEqual(InputBoxStyle.border, InputBoxStyle.fromString("border"));
    try std.testing.expectEqual(InputBoxStyle.horizontal, InputBoxStyle.fromString("horizontal"));
}

test "config: InputBoxStyle.fromString unknown returns bland (default)" {
    try std.testing.expectEqual(InputBoxStyle.bland, InputBoxStyle.fromString("unknown"));
    try std.testing.expectEqual(InputBoxStyle.bland, InputBoxStyle.fromString(""));
    try std.testing.expectEqual(InputBoxStyle.bland, InputBoxStyle.fromString("BORDER"));
}

test "config: InputBoxStyle round-trip" {
    const styles = [_]InputBoxStyle{ .bland, .background, .border, .horizontal };
    for (styles) |s| {
        try std.testing.expectEqual(s, InputBoxStyle.fromString(s.toString()));
    }
}

// ---------------------------------------------------------------------------
// ResponseStyle
// ---------------------------------------------------------------------------

test "config: ResponseStyle.toString" {
    try std.testing.expectEqualStrings("caret", ResponseStyle.caret.toString());
    try std.testing.expectEqualStrings("border", ResponseStyle.border.toString());
}

test "config: ResponseStyle.fromString" {
    try std.testing.expectEqual(ResponseStyle.caret, ResponseStyle.fromString("caret"));
    try std.testing.expectEqual(ResponseStyle.border, ResponseStyle.fromString("border"));
    // unknown defaults to border
    try std.testing.expectEqual(ResponseStyle.border, ResponseStyle.fromString("unknown"));
}

// ---------------------------------------------------------------------------
// ProviderType
// ---------------------------------------------------------------------------

test "config: ProviderType.toString" {
    try std.testing.expectEqualStrings("auto", ProviderType.auto.toString());
    try std.testing.expectEqualStrings("openai", ProviderType.openai.toString());
    try std.testing.expectEqualStrings("anthropic", ProviderType.anthropic.toString());
    try std.testing.expectEqualStrings("bedrock", ProviderType.bedrock.toString());
    try std.testing.expectEqualStrings("deepseek", ProviderType.deepseek.toString());
    try std.testing.expectEqualStrings("custom", ProviderType.custom.toString());
}

test "config: ProviderType.fromString known values" {
    try std.testing.expectEqual(ProviderType.openai, ProviderType.fromString("openai"));
    try std.testing.expectEqual(ProviderType.anthropic, ProviderType.fromString("anthropic"));
    try std.testing.expectEqual(ProviderType.bedrock, ProviderType.fromString("bedrock"));
    try std.testing.expectEqual(ProviderType.deepseek, ProviderType.fromString("deepseek"));
    try std.testing.expectEqual(ProviderType.custom, ProviderType.fromString("custom"));
}

test "config: ProviderType.fromString unknown returns auto" {
    try std.testing.expectEqual(ProviderType.auto, ProviderType.fromString("unknown"));
    try std.testing.expectEqual(ProviderType.auto, ProviderType.fromString(""));
}

// ---------------------------------------------------------------------------
// Config defaults
// ---------------------------------------------------------------------------

test "config: Config.init defaults" {
    const alloc = std.testing.allocator;
    var cfg = Config.init(alloc);
    defer cfg.deinit();

    // Default input box style is horizontal (matching C's INPUT_STYLE_HORIZONTAL default)
    try std.testing.expectEqual(InputBoxStyle.horizontal, cfg.input_box_style);
    // Empty providers list
    try std.testing.expectEqual(@as(usize, 0), cfg.providers.items.len);
}

test "config: Config.init and deinit does not leak" {
    const alloc = std.testing.allocator;
    var cfg = Config.init(alloc);
    cfg.deinit();
    // If there's a leak, the test allocator will catch it
}
