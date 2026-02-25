//! Theme Explorer - Interactive Theme Selection UI
//!
//! Provides a full-screen TUI panel for browsing and previewing
//! all available color schemes with live preview.

const std = @import("std");
const c = @cImport({
    @cInclude("ncurses.h");
});
const builtin_themes = @import("builtin_themes.zig");
const colorscheme = @import("colorscheme.zig");
const Rgb = colorscheme.Rgb;
const Theme = colorscheme.Theme;

/// Theme explorer result codes
pub const ThemeExplorerResult = enum {
    cancelled, // User cancelled (ESC/q)
    selected, // User selected a theme (Enter)
    error_, // Error occurred
};

/// Preview element for displaying color samples
const PreviewElement = struct {
    label: []const u8,
    text: []const u8,
    color_index: i8, // Kitty color index (0-15, or -1 for foreground)
};

/// Color indices from Kitty theme format
const COLOR_FG: i8 = -1;
const COLOR_RED: i8 = 1; // color1 - errors
const COLOR_GREEN: i8 = 2; // color2 - user/success
const COLOR_YELLOW: i8 = 3; // color3 - status/warnings
const COLOR_BLUE: i8 = 4; // color4 - headers
const COLOR_MAGENTA: i8 = 5; // color5
const COLOR_CYAN: i8 = 6; // color6 - assistant
const COLOR_WHITE: i8 = 7; // color7
const COLOR_BRIGHT_BLUE: i8 = 12; // color12 - tools

/// Preview elements to show for each theme
const PREVIEW_ELEMENTS = [_]PreviewElement{
    .{ .label = "Foreground", .text = "Main text color for content", .color_index = COLOR_FG },
    .{ .label = "User", .text = "[User] Hello, how can you help?", .color_index = COLOR_GREEN },
    .{ .label = "Assistant", .text = "[Assistant] I can help with...", .color_index = COLOR_CYAN },
    .{ .label = "Status", .text = "[Status] Processing request...", .color_index = COLOR_YELLOW },
    .{ .label = "Error", .text = "[Error] Something went wrong!", .color_index = COLOR_RED },
    .{ .label = "Tool", .text = "[Tool] Running bash command...", .color_index = COLOR_BRIGHT_BLUE },
    .{ .label = "Diff Add", .text = "+ Added line in green", .color_index = COLOR_GREEN },
    .{ .label = "Diff Remove", .text = "- Removed line in red", .color_index = COLOR_RED },
};

/// Theme explorer state
pub const ThemeExplorerState = struct {
    win: ?*c.WINDOW = null,
    selected_index: i32 = 0,
    scroll_offset: i32 = 0,
    win_height: i32 = 0,
    win_width: i32 = 0,
    preview_start_col: i32 = 0,
    selected_theme: [64]u8 = undefined,

    pub fn init() ThemeExplorerState {
        var state = ThemeExplorerState{};
        state.selected_theme[0] = 0;
        return state;
    }
};

/// Get the count of available themes
pub fn getThemeCount() i32 {
    return @intCast(builtin_themes.getThemeCount());
}

/// Get the name of a theme by index
pub fn getThemeName(index: i32) ?[]const u8 {
    if (index < 0 or index >= getThemeCount()) return null;
    const theme = builtin_themes.getThemeByIndex(@intCast(index)) orelse return null;
    return theme.name;
}

/// Parse a color from theme content by key
fn parseColorFromContent(content: []const u8, key: []const u8, r: *u8, g: *u8, b: *u8) bool {
    var search_buf: [32]u8 = undefined;
    const search_key = std.fmt.bufPrint(&search_buf, "{s} ", .{key}) catch return false;

    // Find the key in content
    var idx: usize = 0;
    while (idx < content.len) {
        if (std.mem.startsWith(u8, content[idx..], search_key)) {
            const line_start = idx;
            // Find end of line
            var line_end = line_start;
            while (line_end < content.len and content[line_end] != '\n') : (line_end += 1) {}
            const line = content[line_start..line_end];

            // Find hex value after key
            var parts = std.mem.splitScalar(u8, line, ' ');
            _ = parts.next() orelse return false; // Skip key

            while (parts.next()) |part| {
                const trimmed = std.mem.trim(u8, part, &std.ascii.whitespace);
                if (trimmed.len > 0) {
                    if (Rgb.parse(trimmed)) |rgb| {
                        r.* = rgb.r;
                        g.* = rgb.g;
                        b.* = rgb.b;
                        return true;
                    }
                    return false;
                }
            }
            return false;
        }
        idx += 1;
    }

    return false;
}

/// Get 256-color index from theme content for a specific color
fn getThemeColor256(content: []const u8, color_idx: i8) u8 {
    var r: u8 = 255;
    var g: u8 = 255;
    var b: u8 = 255;

    if (color_idx == COLOR_FG) {
        if (!parseColorFromContent(content, "foreground", &r, &g, &b)) {
            return 7; // Default white
        }
    } else {
        var key_buf: [16]u8 = undefined;
        const key = std.fmt.bufPrint(&key_buf, "color{d}", .{color_idx}) catch return @intCast(color_idx);
        if (!parseColorFromContent(content, key, &r, &g, &b)) {
            return @intCast(color_idx); // Fallback to basic ANSI
        }
    }

    const rgb = Rgb.init(r, g, b);
    return rgb.to256ColorIndex();
}

/// Initialize the theme explorer
pub fn init(state: *ThemeExplorerState) bool {
    // Get terminal dimensions
    var max_y: c_int = 0;
    var max_x: c_int = 0;
    _ = c.getmaxyx(c.stdscr, &max_y, &max_x);

    // Create a full-screen window
    state.win = c.newwin(max_y, max_x, 0, 0);
    if (state.win == null) {
        std.log.err("[THEME_EXPLORER] Failed to create window", .{});
        return false;
    }

    state.win_height = max_y;
    state.win_width = max_x;
    state.selected_index = 0;
    state.scroll_offset = 0;
    state.preview_start_col = @divTrunc(max_x, 3); // Left 1/3 for list, right 2/3 for preview

    // Enable keypad for arrow keys
    _ = c.keypad(state.win, true);

    std.log.info("[THEME_EXPLORER] Initialized with {d} themes", .{getThemeCount()});
    return true;
}

/// Clean up theme explorer resources
pub fn cleanup(state: *ThemeExplorerState) void {
    if (state.win) |win| {
        _ = c.delwin(win);
        state.win = null;
    }
    std.log.debug("[THEME_EXPLORER] Cleaned up", .{});
}

/// Render the theme list on the left side
fn renderThemeList(state: *ThemeExplorerState) void {
    const list_width = state.preview_start_col - 1;
    const list_height = state.win_height - 4; // Reserve space for header/footer
    const theme_count = getThemeCount();

    // Draw header
    _ = c.wattron(state.win, c.A_BOLD);
    _ = c.mvwprintw(state.win, 1, 2, "Available Themes (%d)", theme_count);
    _ = c.wattroff(state.win, c.A_BOLD);

    // Draw separator line
    _ = c.mvwvline(state.win, 1, state.preview_start_col - 1, c.ACS_VLINE, state.win_height - 2);

    // Calculate visible range
    const visible_count = list_height;
    if (state.selected_index < state.scroll_offset) {
        state.scroll_offset = state.selected_index;
    } else if (state.selected_index >= state.scroll_offset + visible_count) {
        state.scroll_offset = state.selected_index - visible_count + 1;
    }

    // Draw theme names
    var i: i32 = 0;
    while (i < visible_count and (i + state.scroll_offset) < theme_count) : (i += 1) {
        const theme_idx = i + state.scroll_offset;
        const name = getThemeName(theme_idx) orelse continue;

        const row = 3 + i;
        const is_selected = (theme_idx == state.selected_index);

        if (is_selected) {
            _ = c.wattron(state.win, c.A_REVERSE | c.A_BOLD);
        }

        // Clear the line first
        _ = c.wmove(state.win, row, 2);
        var j: i32 = 0;
        while (j < list_width - 3) : (j += 1) {
            _ = c.waddch(state.win, ' ');
        }

        // Print theme name with indicator
        const indicator: []const u8 = if (is_selected) "> " else "  ";
        _ = c.mvwprintw(state.win, row, 2, "%s%s", indicator.ptr, name.ptr);

        if (is_selected) {
            _ = c.wattroff(state.win, c.A_REVERSE | c.A_BOLD);
        }
    }

    // Draw scroll indicators if needed
    if (state.scroll_offset > 0) {
        _ = c.mvwaddch(state.win, 2, @divTrunc(list_width, 2), c.ACS_UARROW);
    }
    if (state.scroll_offset + visible_count < theme_count) {
        _ = c.mvwaddch(state.win, state.win_height - 3, @divTrunc(list_width, 2), c.ACS_DARROW);
    }
}

/// Render the preview panel on the right side
fn renderPreview(state: *ThemeExplorerState) void {
    const preview_col = state.preview_start_col + 1;
    _ = state.win_width - preview_col - 2; // preview_width (unused but calculated)

    const theme_name = getThemeName(state.selected_index) orelse return;

    // Get theme content
    const content = builtin_themes.getThemeByName(theme_name) orelse return;

    // Draw preview header
    _ = c.wattron(state.win, c.A_BOLD);
    _ = c.mvwprintw(state.win, 1, preview_col, "Preview: %s", theme_name.ptr);
    _ = c.wattroff(state.win, c.A_BOLD);

    // Draw preview elements
    var row: i32 = 4;
    var i: usize = 0;
    while (i < PREVIEW_ELEMENTS.len and row < state.win_height - 3) : (i += 1) {
        const elem = &PREVIEW_ELEMENTS[i];

        // Get the 256-color index for this element
        const color_idx = getThemeColor256(content.content, elem.color_index);

        // Use extended color pairs (starting from 100 to avoid conflicts)
        const pair_id: c_short = @intCast(100 + i);
        _ = c.init_pair(pair_id, @intCast(color_idx), -1);

        // Draw label
        _ = c.mvwprintw(state.win, row, preview_col, "%-12s ", elem.label.ptr);

        // Draw sample text with theme color
        _ = c.wattron(state.win, c.COLOR_PAIR(pair_id));
        _ = c.wprintw(state.win, "%s", elem.text.ptr);
        _ = c.wattroff(state.win, c.COLOR_PAIR(pair_id));

        row += 2;
    }

    // Draw color palette preview
    row += 1;
    if (row < state.win_height - 5) {
        _ = c.mvwprintw(state.win, row, preview_col, "Color Palette:");
        row += 1;

        // Show colors 0-7 (normal)
        _ = c.mvwprintw(state.win, row, preview_col, "Normal:  ");
        var c_idx: i8 = 0;
        while (c_idx <= 7) : (c_idx += 1) {
            const idx = getThemeColor256(content.content, c_idx);
            const pair: c_short = @intCast(110 + c_idx);
            _ = c.init_pair(pair, @intCast(idx), -1);
            _ = c.wattron(state.win, c.COLOR_PAIR(pair));
            _ = c.wprintw(state.win, "███");
            _ = c.wattroff(state.win, c.COLOR_PAIR(pair));
            _ = c.waddch(state.win, ' ');
        }
        row += 1;

        // Show colors 8-15 (bright)
        _ = c.mvwprintw(state.win, row, preview_col, "Bright:  ");
        c_idx = 8;
        while (c_idx <= 15) : (c_idx += 1) {
            const idx = getThemeColor256(content.content, c_idx);
            const pair: c_short = @intCast(110 + c_idx);
            _ = c.init_pair(pair, @intCast(idx), -1);
            _ = c.wattron(state.win, c.COLOR_PAIR(pair));
            _ = c.wprintw(state.win, "███");
            _ = c.wattroff(state.win, c.COLOR_PAIR(pair));
            _ = c.waddch(state.win, ' ');
        }
    }
}

/// Draw the full explorer UI
fn renderExplorer(state: *ThemeExplorerState) void {
    _ = c.werase(state.win);

    // Draw border
    _ = c.box(state.win, 0, 0);

    // Draw title
    _ = c.wattron(state.win, c.A_BOLD);
    _ = c.mvwprintw(state.win, 0, 2, " Theme Explorer ");
    _ = c.wattroff(state.win, c.A_BOLD);

    // Draw help text at bottom
    _ = c.mvwprintw(state.win, state.win_height - 1, 2, " j/↓:Next  k/↑:Prev  Enter:Select  q/ESC:Cancel ");

    // Render components
    renderThemeList(state);
    renderPreview(state);

    _ = c.wrefresh(state.win);
}

/// Run the theme explorer in a modal loop
/// Returns the selected theme name or null if cancelled
pub fn run(state: *ThemeExplorerState) ThemeExplorerResult {
    if (state.win == null) {
        return .error_;
    }

    const theme_count = getThemeCount();
    if (theme_count <= 0) {
        std.log.err("[THEME_EXPLORER] No themes available", .{});
        return .error_;
    }

    // Initial render
    renderExplorer(state);

    // Event loop
    var running = true;
    var result = ThemeExplorerResult.cancelled;

    while (running) {
        const ch = c.wgetch(state.win);

        switch (ch) {
            'q', 27 => { // ESC
                running = false;
                result = .cancelled;
            },
            '\n', '\r', c.KEY_ENTER => { // Enter
                const name = getThemeName(state.selected_index);
                if (name) |n| {
                    const len = @min(n.len, state.selected_theme.len - 1);
                    @memcpy(state.selected_theme[0..len], n[0..len]);
                    state.selected_theme[len] = 0;
                    result = .selected;
                    running = false;
                }
            },
            'j', c.KEY_DOWN => {
                if (state.selected_index < theme_count - 1) {
                    state.selected_index += 1;
                    renderExplorer(state);
                }
            },
            'k', c.KEY_UP => {
                if (state.selected_index > 0) {
                    state.selected_index -= 1;
                    renderExplorer(state);
                }
            },
            'g' => { // gg - go to top (simplified - just 'g')
                state.selected_index = 0;
                state.scroll_offset = 0;
                renderExplorer(state);
            },
            'G' => { // G - go to bottom
                state.selected_index = theme_count - 1;
                renderExplorer(state);
            },
            c.KEY_PPAGE => { // Page Up
                state.selected_index -= (state.win_height - 6);
                if (state.selected_index < 0) state.selected_index = 0;
                renderExplorer(state);
            },
            c.KEY_NPAGE => { // Page Down
                state.selected_index += (state.win_height - 6);
                if (state.selected_index >= theme_count) {
                    state.selected_index = theme_count - 1;
                }
                renderExplorer(state);
            },
            c.KEY_HOME => {
                state.selected_index = 0;
                state.scroll_offset = 0;
                renderExplorer(state);
            },
            c.KEY_END => {
                state.selected_index = theme_count - 1;
                renderExplorer(state);
            },
            c.KEY_RESIZE => {
                // Handle terminal resize
                var max_y: c_int = 0;
                var max_x: c_int = 0;
                _ = c.getmaxyx(c.stdscr, &max_y, &max_x);
                _ = c.wresize(state.win, max_y, max_x);
                _ = c.mvwin(state.win, 0, 0);
                state.win_height = max_y;
                state.win_width = max_x;
                state.preview_start_col = @divTrunc(max_x, 3);
                renderExplorer(state);
            },
            else => {},
        }
    }

    return result;
}

/// Get the name of the selected theme (after run returns selected)
/// Returns null if no theme was selected
pub fn getSelected(state: *ThemeExplorerState) ?[]const u8 {
    if (state.selected_theme[0] == 0) return null;
    return std.mem.sliceTo(&state.selected_theme, 0);
}

/// Convenience function to run the theme explorer and return selected theme name
/// Returns null if cancelled or error
pub fn showThemeExplorer() ?[]const u8 {
    var state = ThemeExplorerState.init();

    if (!init(&state)) {
        return null;
    }
    defer cleanup(&state);

    const result = run(&state);
    if (result == .selected) {
        return getSelected(&state);
    }

    return null;
}

// Tests
const testing = std.testing;

test "getThemeCount" {
    try testing.expect(getThemeCount() > 0);
}

test "getThemeName" {
    const name = getThemeName(0);
    try testing.expect(name != null);
    try testing.expect(name.?.len > 0);

    try testing.expect(getThemeName(-1) == null);
    try testing.expect(getThemeName(1000) == null);
}

test "parseColorFromContent" {
    const content = "foreground #FF5500\ncolor1 #00AA00\n";
    var r: u8 = 0;
    var g: u8 = 0;
    var b: u8 = 0;

    try testing.expect(parseColorFromContent(content, "foreground", &r, &g, &b));
    try testing.expectEqual(@as(u8, 255), r);
    try testing.expectEqual(@as(u8, 85), g);
    try testing.expectEqual(@as(u8, 0), b);
}

test "getThemeColor256" {
    const content = "color2 #00FF00\nforeground #FFFFFF\n";

    const green_idx = getThemeColor256(content, 2);
    try testing.expect(green_idx >= 16 and green_idx <= 255);

    const white_idx = getThemeColor256(content, -1);
    try testing.expect(white_idx >= 232 or white_idx < 16);
}
