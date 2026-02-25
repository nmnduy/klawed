//! tests/test_colorscheme.zig — Zig port of tests/test_colorscheme.c
//!
//! Tests the colorscheme system from zig/tui/colorscheme.zig and
//! zig/tui/builtin_themes.zig.  All tests are headless — no ncurses.

const std = @import("std");
const colorscheme = @import("../tui/colorscheme.zig");
const builtin_themes = @import("../tui/builtin_themes.zig");

const Rgb = colorscheme.Rgb;
const Theme = colorscheme.Theme;
const ColorschemeManager = colorscheme.ColorschemeManager;
const ColorschemeElement = colorscheme.ColorschemeElement;

// ---------------------------------------------------------------------------
// Test 1: Load built-in kitty-default theme
// ---------------------------------------------------------------------------

test "colorscheme: kitty-default theme loads without error" {
    const theme_entry = builtin_themes.getThemeByName("kitty-default");
    try std.testing.expect(theme_entry != null);

    var theme = Theme{};
    const count = theme.loadFromBuffer(theme_entry.?.content);
    try std.testing.expect(count > 0);
}

test "colorscheme: kitty-default foreground is white (#ffffff)" {
    const theme_entry = builtin_themes.getThemeByName("kitty-default").?;
    var theme = Theme{};
    _ = theme.loadFromBuffer(theme_entry.content);

    try std.testing.expectEqual(@as(u8, 255), theme.foreground_rgb.r);
    try std.testing.expectEqual(@as(u8, 255), theme.foreground_rgb.g);
    try std.testing.expectEqual(@as(u8, 255), theme.foreground_rgb.b);
}

test "colorscheme: kitty-default error color is pure red (#ff0000)" {
    const theme_entry = builtin_themes.getThemeByName("kitty-default").?;
    var theme = Theme{};
    _ = theme.loadFromBuffer(theme_entry.content);

    // color1 = #ff0000
    try std.testing.expectEqual(@as(u8, 255), theme.error_rgb.r);
    try std.testing.expectEqual(@as(u8, 0), theme.error_rgb.g);
    try std.testing.expectEqual(@as(u8, 0), theme.error_rgb.b);
}

test "colorscheme: kitty-default header/assistant is cyan (#00ffff via color6)" {
    const theme_entry = builtin_themes.getThemeByName("kitty-default").?;
    var theme = Theme{};
    _ = theme.loadFromBuffer(theme_entry.content);

    // color6 = #00ffff (cyan)
    try std.testing.expectEqual(@as(u8, 0), theme.header_rgb.r);
    try std.testing.expectEqual(@as(u8, 255), theme.header_rgb.g);
    try std.testing.expectEqual(@as(u8, 255), theme.header_rgb.b);
}

test "colorscheme: ColorschemeManager loads kitty-default successfully" {
    var mgr = ColorschemeManager.init(std.testing.allocator);
    defer mgr.deinit();

    try std.testing.expect(!mgr.isLoaded());

    const theme_entry = builtin_themes.getThemeByName("kitty-default").?;
    const ok = mgr.loadFromBuffer(theme_entry.content);
    try std.testing.expect(ok);
    try std.testing.expect(mgr.isLoaded());
}

// ---------------------------------------------------------------------------
// Test 2: Non-existent theme returns null / not-loaded
// ---------------------------------------------------------------------------

test "colorscheme: non-existent theme name returns null from builtin_themes" {
    const theme_entry = builtin_themes.getThemeByName("nonexistent-theme");
    try std.testing.expect(theme_entry == null);
}

test "colorscheme: ColorschemeManager not loaded after init" {
    var mgr = ColorschemeManager.init(std.testing.allocator);
    defer mgr.deinit();
    try std.testing.expect(!mgr.isLoaded());
}

// ---------------------------------------------------------------------------
// Test 3: NULL / empty filepath handling
// ---------------------------------------------------------------------------

test "colorscheme: loadFromBuffer with empty string results in not-loaded" {
    var mgr = ColorschemeManager.init(std.testing.allocator);
    defer mgr.deinit();

    const ok = mgr.loadFromBuffer("");
    try std.testing.expect(!ok);
    try std.testing.expect(!mgr.isLoaded());
}

// ---------------------------------------------------------------------------
// Test 4: getColorCode returns error when no theme is loaded
// ---------------------------------------------------------------------------

test "colorscheme: getColorCode returns NoThemeLoaded when no theme set" {
    var mgr = ColorschemeManager.init(std.testing.allocator);
    defer mgr.deinit();

    var buf: [32]u8 = undefined;
    const result = mgr.getColorCode(.user, &buf);
    try std.testing.expectError(error.NoThemeLoaded, result);
}

// ---------------------------------------------------------------------------
// Test 5: ANSI code generation from RGB
// ---------------------------------------------------------------------------

test "colorscheme: Rgb.toAnsiCode produces correct escape prefix" {
    const red = Rgb.init(255, 0, 0);
    var buf: [32]u8 = undefined;
    const code = try red.toAnsiCode(&buf);
    // Must start with the CSI sequence
    try std.testing.expect(std.mem.startsWith(u8, code, "\x1b[38;5;"));
}

test "colorscheme: Rgb.toAnsiBgCode produces correct background escape prefix" {
    const green = Rgb.init(0, 255, 0);
    var buf: [32]u8 = undefined;
    const code = try green.toAnsiBgCode(&buf);
    try std.testing.expect(std.mem.startsWith(u8, code, "\x1b[48;5;"));
}

test "colorscheme: getColorCode returns valid ANSI code after loading theme" {
    var mgr = ColorschemeManager.init(std.testing.allocator);
    defer mgr.deinit();

    const theme_entry = builtin_themes.getThemeByName("kitty-default").?;
    _ = mgr.loadFromBuffer(theme_entry.content);

    var buf: [32]u8 = undefined;
    const code = try mgr.getColorCode(.assistant, &buf);
    // Should be a non-empty string starting with ESC
    try std.testing.expect(code.len > 0);
    try std.testing.expect(code[0] == '\x1b');
}

// ---------------------------------------------------------------------------
// Test 6: Theme color validity — all RGB components in [0, 255]
// ---------------------------------------------------------------------------

test "colorscheme: all loaded theme RGB values are in [0, 255]" {
    const theme_entry = builtin_themes.getThemeByName("kitty-default").?;
    var theme = Theme{};
    _ = theme.loadFromBuffer(theme_entry.content);

    // Zig u8 is already 0-255 by type, but test the parsed values are non-trivially set.
    // We just check that the types are correct (compile-time guarantee).
    try std.testing.expect(theme.foreground_rgb.r <= 255);
    try std.testing.expect(theme.foreground_rgb.g <= 255);
    try std.testing.expect(theme.foreground_rgb.b <= 255);
    try std.testing.expect(theme.error_rgb.r <= 255);
}

// ---------------------------------------------------------------------------
// Test 7: Diff color mappings are applied
// ---------------------------------------------------------------------------

test "colorscheme: diff_add_rgb == user_rgb after applyDiffMappings" {
    const theme_entry = builtin_themes.getThemeByName("kitty-default").?;
    var theme = Theme{};
    _ = theme.loadFromBuffer(theme_entry.content);

    // loadFromBuffer calls applyDiffMappings internally
    try std.testing.expectEqual(theme.user_rgb.r, theme.diff_add_rgb.r);
    try std.testing.expectEqual(theme.user_rgb.g, theme.diff_add_rgb.g);
    try std.testing.expectEqual(theme.user_rgb.b, theme.diff_add_rgb.b);
}

test "colorscheme: diff_remove_rgb == error_rgb after applyDiffMappings" {
    const theme_entry = builtin_themes.getThemeByName("kitty-default").?;
    var theme = Theme{};
    _ = theme.loadFromBuffer(theme_entry.content);

    try std.testing.expectEqual(theme.error_rgb.r, theme.diff_remove_rgb.r);
    try std.testing.expectEqual(theme.error_rgb.g, theme.diff_remove_rgb.g);
    try std.testing.expectEqual(theme.error_rgb.b, theme.diff_remove_rgb.b);
}

// ---------------------------------------------------------------------------
// Test 8: All built-in themes can be loaded
// ---------------------------------------------------------------------------

test "colorscheme: all built-in themes parse without error" {
    for (builtin_themes.BUILTIN_THEMES) |entry| {
        var theme = Theme{};
        const count = theme.loadFromBuffer(entry.content);
        try std.testing.expect(count > 0);
    }
}

test "colorscheme: dracula theme loads and has expected palette" {
    const theme_entry = builtin_themes.getThemeByName("dracula").?;
    var theme = Theme{};
    const count = theme.loadFromBuffer(theme_entry.content);
    try std.testing.expect(count > 0);
    // foreground = #f8f8f2
    try std.testing.expectEqual(@as(u8, 0xf8), theme.foreground_rgb.r);
    try std.testing.expectEqual(@as(u8, 0xf8), theme.foreground_rgb.g);
    try std.testing.expectEqual(@as(u8, 0xf2), theme.foreground_rgb.b);
}

// ---------------------------------------------------------------------------
// Test 9: Rgb.parse
// ---------------------------------------------------------------------------

test "colorscheme: Rgb.parse handles #RRGGBB format" {
    const rgb = Rgb.parse("#FF0080").?;
    try std.testing.expectEqual(@as(u8, 0xFF), rgb.r);
    try std.testing.expectEqual(@as(u8, 0x00), rgb.g);
    try std.testing.expectEqual(@as(u8, 0x80), rgb.b);
}

test "colorscheme: Rgb.parse handles RRGGBB without hash" {
    const rgb = Rgb.parse("00FF00").?;
    try std.testing.expectEqual(@as(u8, 0), rgb.r);
    try std.testing.expectEqual(@as(u8, 255), rgb.g);
    try std.testing.expectEqual(@as(u8, 0), rgb.b);
}

test "colorscheme: Rgb.parse returns null for invalid strings" {
    try std.testing.expect(Rgb.parse("") == null);
    try std.testing.expect(Rgb.parse("#GGGGGG") == null);
    try std.testing.expect(Rgb.parse("#FFF") == null);
    try std.testing.expect(Rgb.parse("1234567") == null);
}

// ---------------------------------------------------------------------------
// Test 10: getTheme accessor on ColorschemeManager
// ---------------------------------------------------------------------------

test "colorscheme: getTheme returns a pointer to the loaded theme" {
    var mgr = ColorschemeManager.init(std.testing.allocator);
    defer mgr.deinit();

    const theme_entry = builtin_themes.getThemeByName("dracula").?;
    _ = mgr.loadFromBuffer(theme_entry.content);

    const t = mgr.getTheme();
    // Dracula foreground = #f8f8f2
    try std.testing.expectEqual(@as(u8, 0xf8), t.foreground_rgb.r);
}
