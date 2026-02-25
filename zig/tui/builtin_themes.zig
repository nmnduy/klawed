//! Built-in theme definitions for the TUI
//!
//! Provides embedded Kitty theme configurations that can be loaded
//! without external files. Themes include: dracula, gruvbox, etc.

const std = @import("std");

/// Structure representing a built-in theme
pub const BuiltInTheme = struct {
    name: []const u8,
    content: []const u8,
};

// Theme content as multiline strings using array concatenation
const dracula_content =
    "# Dracula Theme for Kitty\n" ++
    "# https://draculatheme.com/\n" ++
    "\n" ++
    "background #1e1f28\n" ++
    "foreground #f8f8f2\n" ++
    "cursor #bbbbbb\n" ++
    "selection_background #44475a\n" ++
    "selection_foreground #1e1f28\n" ++
    "\n" ++
    "color0 #000000\n" ++
    "color8 #545454\n" ++
    "color1 #ff5555\n" ++
    "color9 #ff5454\n" ++
    "color2 #50fa7b\n" ++
    "color10 #50fa7b\n" ++
    "color3 #f0fa8b\n" ++
    "color11 #f0fa8b\n" ++
    "color4 #bd92f8\n" ++
    "color12 #bd92f8\n" ++
    "color5 #ff78c5\n" ++
    "color13 #ff78c5\n" ++
    "color6 #8ae9fc\n" ++
    "color14 #8ae9fc\n" ++
    "color7 #bbbbbb\n" ++
    "color15 #ffffff\n";

const gruvbox_dark_content =
    "# gruvbox dark by morhetz, https://github.com/morhetz/gruvbox\n" ++
    "# This work is licensed under the terms of the MIT license.\n" ++
    "# For a copy, see https://opensource.org/licenses/MIT.\n" ++
    "\n" ++
    "background #282828\n" ++
    "foreground #ebdbb2\n" ++
    "\n" ++
    "cursor #928374\n" ++
    "selection_foreground #928374\n" ++
    "selection_background #3c3836\n" ++
    "\n" ++
    "color0 #282828\n" ++
    "color8 #928374\n" ++
    "color1 #cc241d\n" ++
    "color9 #fb4934\n" ++
    "color2 #98971a\n" ++
    "color10 #b8bb26\n" ++
    "color3 #d79921\n" ++
    "color11 #fabd2d\n" ++
    "color4 #458588\n" ++
    "color12 #83a598\n" ++
    "color5 #b16286\n" ++
    "color13 #d3869b\n" ++
    "color6 #689d6a\n" ++
    "color14 #8ec07c\n" ++
    "color7 #a89984\n" ++
    "color15 #928374\n";

const kitty_default_content =
    "# Kitty Default Theme\n" ++
    "# Classic high contrast\n" ++
    "\n" ++
    "background #000000\n" ++
    "foreground #ffffff\n" ++
    "\n" ++
    "cursor #ffffff\n" ++
    "\n" ++
    "color0 #000000\n" ++
    "color8 #555555\n" ++
    "color1 #ff0000\n" ++
    "color9 #ff5555\n" ++
    "color2 #00ff00\n" ++
    "color10 #55ff55\n" ++
    "color3 #ffff00\n" ++
    "color11 #ffff55\n" ++
    "color4 #0000ff\n" ++
    "color12 #5555ff\n" ++
    "color5 #ff00ff\n" ++
    "color13 #ff55ff\n" ++
    "color6 #00ffff\n" ++
    "color14 #55ffff\n" ++
    "color7 #cccccc\n" ++
    "color15 #ffffff\n";

const solarized_dark_content =
    "# Solarized Dark Theme for Kitty\n" ++
    "# https://ethanschoonover.com/solarized/\n" ++
    "\n" ++
    "background #001e26\n" ++
    "foreground #708183\n" ++
    "cursor #708183\n" ++
    "selection_background #002731\n" ++
    "selection_foreground #001e26\n" ++
    "\n" ++
    "color0 #002731\n" ++
    "color8 #001e26\n" ++
    "color1 #d01b24\n" ++
    "color9 #bd3612\n" ++
    "color2 #728905\n" ++
    "color10 #465a61\n" ++
    "color3 #a57705\n" ++
    "color11 #52676f\n" ++
    "color4 #2075c7\n" ++
    "color12 #708183\n" ++
    "color5 #c61b6e\n" ++
    "color13 #5856b9\n" ++
    "color6 #259185\n" ++
    "color14 #81908f\n" ++
    "color7 #e9e2cb\n" ++
    "color15 #fcf4dc\n";

const black_metal_content =
    "# Base16 Black Metal - kitty color config\n" ++
    "# Scheme by metalelf0 (https://github.com/metalelf0)\n" ++
    "\n" ++
    "background #000000\n" ++
    "foreground #c1c1c1\n" ++
    "cursor #c1c1c1\n" ++
    "selection_background #c1c1c1\n" ++
    "selection_foreground #000000\n" ++
    "\n" ++
    "color0 #000000\n" ++
    "color8 #333333\n" ++
    "color1 #5f8787\n" ++
    "color9 #aaaaaa\n" ++
    "color2 #dd9999\n" ++
    "color10 #121212\n" ++
    "color3 #a06666\n" ++
    "color11 #222222\n" ++
    "color4 #888888\n" ++
    "color12 #999999\n" ++
    "color5 #999999\n" ++
    "color13 #999999\n" ++
    "color6 #aaaaaa\n" ++
    "color14 #444444\n" ++
    "color7 #c1c1c1\n" ++
    "color15 #c1c1c1\n";

const tender_content =
    "# Colours (Tender) for Kitty\n" ++
    "\n" ++
    "## name: Tender\n" ++
    "## author: CompEng0001\n" ++
    "## license: MIT\n" ++
    "## upstream: https://github.com/CompEng0001/tender-kitty/raw/main/theme.conf\n" ++
    "## blurb: A Kitty color scheme inspired by the tender_vim color scheme\n" ++
    "\n" ++
    "# Default colours\n" ++
    "background #282828\n" ++
    "foreground #eeeeee\n" ++
    "\n" ++
    "# Normal colours\n" ++
    "color0 #282828\n" ++
    "color1 #f43753\n" ++
    "color2 #c9d05c\n" ++
    "color3 #ffc24b\n" ++
    "color4 #b3deef\n" ++
    "color5 #d3b987\n" ++
    "color6 #73cef4\n" ++
    "color7 #eeeeee\n" ++
    "\n" ++
    "# Bright colours\n" ++
    "color8 #4c4c4c\n" ++
    "color9 #f43753\n" ++
    "color10 #c9d05c\n" ++
    "color11 #ffc24b\n" ++
    "color12 #b3deef\n" ++
    "color13 #d3b987\n" ++
    "color14 #73cef4\n" ++
    "color15 #feffff\n";

const ayu_content =
    "# ayu theme for Kitty\n" ++
    "# Source: https://github.com/dexpota/kitty-themes/blob/master/themes/ayu.conf\n" ++
    "\n" ++
    "background #0e1419\n" ++
    "foreground #e5e1cf\n" ++
    "cursor #f19618\n" ++
    "selection_background #243340\n" ++
    "color0 #000000\n" ++
    "color8 #323232\n" ++
    "color1 #ff3333\n" ++
    "color9 #ff6565\n" ++
    "color2 #b8cc52\n" ++
    "color10 #e9fe83\n" ++
    "color3 #e6c446\n" ++
    "color11 #fff778\n" ++
    "color4 #36a3d9\n" ++
    "color12 #68d4ff\n" ++
    "color5 #f07078\n" ++
    "color13 #ffa3aa\n" ++
    "color6 #95e5cb\n" ++
    "color14 #c7fffc\n" ++
    "color7 #ffffff\n" ++
    "color15 #ffffff\n" ++
    "selection_foreground #0e1419\n";

const belafonte_night_content =
    "# Belafonte Night Theme for Kitty\n" ++
    "# Source: https://github.com/dexpota/kitty-themes/blob/master/themes/Belafonte_Night.conf\n" ++
    "\n" ++
    "background #20111a\n" ++
    "foreground #958b83\n" ++
    "cursor #958b83\n" ++
    "selection_background #45363b\n" ++
    "color0 #20111a\n" ++
    "color8 #5e5252\n" ++
    "color1 #bd100d\n" ++
    "color9 #bd100d\n" ++
    "color2 #858062\n" ++
    "color10 #858062\n" ++
    "color3 #e9a448\n" ++
    "color11 #e9a448\n" ++
    "color4 #416978\n" ++
    "color12 #416978\n" ++
    "color5 #96522b\n" ++
    "color13 #96522b\n" ++
    "color6 #98999c\n" ++
    "color14 #98999c\n" ++
    "color7 #958b83\n" ++
    "color15 #d4ccb9\n" ++
    "selection_foreground #20111a\n";

const molokai_content =
    "background            #121212\n" ++
    "foreground            #bbbbbb\n" ++
    "cursor                #bbbbbb\n" ++
    "selection_background  #b4d5ff\n" ++
    "color0                #121212\n" ++
    "color8                #545454\n" ++
    "color1                #fa2573\n" ++
    "color9                #f5669c\n" ++
    "color2                #97e123\n" ++
    "color10               #b0e05e\n" ++
    "color3                #dfd460\n" ++
    "color11               #fef26c\n" ++
    "color4                #0f7fcf\n" ++
    "color12               #00afff\n" ++
    "color5                #8700ff\n" ++
    "color13               #af87ff\n" ++
    "color6                #42a7cf\n" ++
    "color14               #50cdfe\n" ++
    "color7                #bbbbbb\n" ++
    "color15               #ffffff\n" ++
    "selection_foreground #121212\n";

const bl1nk_content =
    "## name: bl1nk\n" ++
    "## author: dhay3\n" ++
    "## license: CC0 1.0\n" ++
    "## upstream: https://github.com/dhay3/kitty-bl1nk/blob/main/bl1nk.conf\n" ++
    "\n" ++
    "background              #111111\n" ++
    "foreground              #A0A0A0\n" ++
    "selection_foreground    none\n" ++
    "selection_background    none\n" ++
    "cursor                  none\n" ++
    "color0                  #1A1C1D\n" ++
    "color8                  #505354\n" ++
    "color1                  #FF5894\n" ++
    "color9                  #F92571\n" ++
    "color2                  #B5E354\n" ++
    "color10                 #81B313\n" ++
    "color3                  #F5921D\n" ++
    "color11                 #FEED6B\n" ++
    "color4                  #4E81AA\n" ++
    "color12                 #0B72C1\n" ++
    "color5                  #8B54FE\n" ++
    "color13                 #9D6EFE\n" ++
    "color6                  #465456\n" ++
    "color14                 #889BA1\n" ++
    "color7                  #CBCBC5\n" ++
    "color15                 #F8F8F2\n";

/// Built-in themes embedded as raw .conf content
pub const BUILTIN_THEMES = [_]BuiltInTheme{
    .{ .name = "dracula", .content = dracula_content },
    .{ .name = "gruvbox-dark", .content = gruvbox_dark_content },
    .{ .name = "kitty-default", .content = kitty_default_content },
    .{ .name = "solarized-dark", .content = solarized_dark_content },
    .{ .name = "black-metal", .content = black_metal_content },
    .{ .name = "tender", .content = tender_content },
    .{ .name = "ayu", .content = ayu_content },
    .{ .name = "belafonte-night", .content = belafonte_night_content },
    .{ .name = "molokai", .content = molokai_content },
    .{ .name = "bl1nk", .content = bl1nk_content },
};

/// Get the number of built-in themes
pub fn getThemeCount() usize {
    return BUILTIN_THEMES.len;
}

/// Get a built-in theme by index
/// Returns null if index is out of range
pub fn getThemeByIndex(index: usize) ?*const BuiltInTheme {
    if (index >= BUILTIN_THEMES.len) return null;
    return &BUILTIN_THEMES[index];
}

/// Get a theme by name
/// Returns null if not found
pub fn getThemeByName(name: []const u8) ?*const BuiltInTheme {
    for (&BUILTIN_THEMES) |*theme| {
        if (std.mem.eql(u8, theme.name, name)) {
            return theme;
        }
    }
    return null;
}

/// Extract the base name from a filepath and check for .conf extension
fn extractBaseName(filepath: []const u8, buf: []u8) ?[]const u8 {
    // Find last slash
    const last_slash = std.mem.lastIndexOfScalar(u8, filepath, '/');
    const base_start = if (last_slash) |idx| idx + 1 else 0;
    const base = filepath[base_start..];

    // Remove .conf extension if present
    const result = if (std.mem.endsWith(u8, base, ".conf"))
        base[0 .. base.len - 5]
    else
        base;

    if (result.len == 0 or result.len > buf.len) return null;

    @memcpy(buf[0..result.len], result);
    return buf[0..result.len];
}

/// Return the content of a built-in theme matching the given filepath
/// Extracts the base filename (without path and .conf extension) and
/// compares to built-in theme names.
/// Returns null if no built-in theme matches.
pub fn getBuiltinThemeContent(filepath: []const u8) ?[]const u8 {
    var buf: [64]u8 = undefined;
    const key = extractBaseName(filepath, &buf) orelse return null;

    if (getThemeByName(key)) |theme| {
        return theme.content;
    }

    return null;
}

/// Get all theme names as a slice of strings
/// Caller must free the returned array using the provided allocator
pub fn getAllThemeNames(allocator: std.mem.Allocator) ![][]const u8 {
    var names = try allocator.alloc([]const u8, BUILTIN_THEMES.len);
    errdefer allocator.free(names);

    for (&BUILTIN_THEMES, 0..) |theme, i| {
        names[i] = theme.name;
    }

    return names;
}

// Tests
const testing = std.testing;

test "getThemeCount" {
    try testing.expect(getThemeCount() > 0);
}

test "getThemeByIndex" {
    const theme = getThemeByIndex(0);
    try testing.expect(theme != null);
    try testing.expect(theme.?.name.len > 0);
    try testing.expect(theme.?.content.len > 0);

    try testing.expect(getThemeByIndex(1000) == null);
}

test "getThemeByName" {
    const theme = getThemeByName("dracula");
    try testing.expect(theme != null);
    try testing.expectEqualStrings("dracula", theme.?.name);

    try testing.expect(getThemeByName("nonexistent") == null);
}

test "getBuiltinThemeContent" {
    // Test with simple name
    const content1 = getBuiltinThemeContent("dracula");
    try testing.expect(content1 != null);

    // Test with .conf extension
    const content2 = getBuiltinThemeContent("dracula.conf");
    try testing.expect(content2 != null);

    // Test with path
    const content3 = getBuiltinThemeContent("/home/user/.config/kitty/dracula.conf");
    try testing.expect(content3 != null);

    // Test non-existent theme
    try testing.expect(getBuiltinThemeContent("nonexistent") == null);
}
