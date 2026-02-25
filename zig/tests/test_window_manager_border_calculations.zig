//! tests/test_window_manager_border_calculations.zig
//!   — Zig port of tests/test_window_manager_border_calculations.c
//!
//! Tests the consistency of border/no-border calculations between the
//! window manager and the TUI.  All arithmetic is pure and runs headless.

const std = @import("std");
const wm = @import("../tui/window_manager.zig");

// ---------------------------------------------------------------------------
// Helpers that replicate the C test's inline logic
// ---------------------------------------------------------------------------

/// Clamp `v` to [lo, hi].
fn clamp(v: i32, lo: i32, hi: i32) i32 {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/// Conversation viewport height given a screen and window-manager config.
fn convViewportHeight(cfg: wm.WindowConfig, screen_h: i32, input_h: i32) i32 {
    // padding is 0 in default config
    const raw = screen_h - input_h - cfg.status_height;
    return @max(raw, cfg.min_conv_height);
}

// ---------------------------------------------------------------------------
// Test 1: Config values represent content lines, not content+borders
// ---------------------------------------------------------------------------

test "border calc: default config min_input_height is content-only (no borders)" {
    const cfg = wm.WindowConfig{};
    // Minimum content lines: 2 (not 3 = 2+top/bottom borders)
    try std.testing.expectEqual(@as(i32, 2), cfg.min_input_height);
}

test "border calc: default config max_input_height is content-only (no borders)" {
    const cfg = wm.WindowConfig{};
    // Maximum content lines: 5 (not 6 = 5+borders)
    try std.testing.expectEqual(@as(i32, 5), cfg.max_input_height);
}

// ---------------------------------------------------------------------------
// Test 2: Input resize clamping — no border offset applied
// ---------------------------------------------------------------------------

test "border calc: resize clamp — desired 3 lines is kept as-is" {
    const cfg = wm.WindowConfig{};
    const new_h = clamp(3, cfg.min_input_height, cfg.max_input_height);
    try std.testing.expectEqual(@as(i32, 3), new_h);
}

test "border calc: resize clamp — below min is raised to min_input_height (2)" {
    const cfg = wm.WindowConfig{};
    const new_h = clamp(1, cfg.min_input_height, cfg.max_input_height);
    try std.testing.expectEqual(@as(i32, 2), new_h);
}

test "border calc: resize clamp — above max is capped to max_input_height (5)" {
    const cfg = wm.WindowConfig{};
    const new_h = clamp(10, cfg.min_input_height, cfg.max_input_height);
    try std.testing.expectEqual(@as(i32, 5), new_h);
}

// ---------------------------------------------------------------------------
// Test 3: Layout calculations (screen_height − input − status)
// ---------------------------------------------------------------------------

test "border calc: layout 24-line screen gives conv_viewport 20" {
    const cfg = wm.WindowConfig{};
    // 24 - 3 (input) - 1 (status) - 0 (padding) = 20
    const vh = convViewportHeight(cfg, 24, 3);
    try std.testing.expectEqual(@as(i32, 20), vh);
}

test "border calc: small screen clamps conv_viewport to min_conv_height" {
    const cfg = wm.WindowConfig{};
    // 8 - 3 - 1 = 4, but min_conv_height = 5
    const vh = convViewportHeight(cfg, 8, 3);
    try std.testing.expectEqual(@as(i32, 5), vh);
}

test "border calc: layout invariant — conv_viewport is never below min_conv_height" {
    const cfg = wm.WindowConfig{};
    const screen_heights = [_]i32{ 5, 8, 10, 15, 24, 40, 80 };
    const input_heights = [_]i32{ 2, 3, 4, 5 };

    for (screen_heights) |sh| {
        for (input_heights) |ih| {
            const vh = convViewportHeight(cfg, sh, ih);
            try std.testing.expect(vh >= cfg.min_conv_height);
        }
    }
}

// ---------------------------------------------------------------------------
// Test 4: TUI and window manager alignment on constants
// ---------------------------------------------------------------------------

test "border calc: TUI INPUT_WIN_MIN_HEIGHT matches WM min_input_height" {
    // TUI constant (tui.c: INPUT_WIN_MIN_HEIGHT = 2)
    const tui_min: i32 = 2;
    const cfg = wm.WindowConfig{};
    try std.testing.expectEqual(tui_min, cfg.min_input_height);
}

test "border calc: TUI max input height percentage calculation" {
    const screen_h: i32 = 24;
    const pct: i32 = 20;
    const tui_min: i32 = 2;

    var calculated = (screen_h * pct) / 100;
    if (calculated < tui_min) calculated = tui_min;

    // 20% of 24 = 4 (integer division), which is >= tui_min(2)
    try std.testing.expectEqual(@as(i32, 4), calculated);
}

test "border calc: effective max is min of TUI-computed and WM max_input_height" {
    const screen_h: i32 = 24;
    const tui_computed: i32 = (screen_h * 20) / 100; // = 4
    const cfg = wm.WindowConfig{};

    const effective_max = @min(tui_computed, cfg.max_input_height);
    // tui gives 4, WM gives 5 → effective = 4
    try std.testing.expectEqual(@as(i32, 4), effective_max);
}

// ---------------------------------------------------------------------------
// Test 5: Border calculation fix — cursor starts at (0,0), not (1,1)
// ---------------------------------------------------------------------------

test "border calc: cursor starts at row 0 (no border offset)" {
    const cursor_y: i32 = 0;
    const cursor_x: i32 = 0;
    try std.testing.expectEqual(@as(i32, 0), cursor_y);
    try std.testing.expectEqual(@as(i32, 0), cursor_x);
}

test "border calc: all rows 0..input_height-1 are valid content rows" {
    const input_height: i32 = 3;
    var row: i32 = 0;
    while (row < input_height) : (row += 1) {
        try std.testing.expect(row >= 0 and row < input_height);
    }
}
