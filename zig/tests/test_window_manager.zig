//! tests/test_window_manager.zig — Zig port of tests/test_window_manager.c
//!
//! Tests window manager pure logic: configuration defaults, layout
//! calculations, and dimension invariants — without ncurses.
//!
//! The C test required an actual ncurses session (initscr), so tests that
//! exercise window creation are skipped here.  We test every piece of pure
//! arithmetic that is safe to run headless.

const std = @import("std");
const wm = @import("../tui/window_manager.zig");

// ---------------------------------------------------------------------------
// WindowConfig defaults
// ---------------------------------------------------------------------------

test "WindowManager: default config values are correct" {
    const cfg = wm.WindowConfig{};

    try std.testing.expectEqual(@as(i32, 5), cfg.min_conv_height);
    try std.testing.expectEqual(@as(i32, 2), cfg.min_input_height);
    try std.testing.expectEqual(@as(i32, 5), cfg.max_input_height);
    try std.testing.expectEqual(@as(i32, 1), cfg.status_height);
}

test "WindowManager: custom config values are preserved" {
    const cfg = wm.WindowConfig{
        .min_conv_height = 10,
        .min_input_height = 3,
        .max_input_height = 8,
        .status_height = 2,
    };

    try std.testing.expectEqual(@as(i32, 10), cfg.min_conv_height);
    try std.testing.expectEqual(@as(i32, 3), cfg.min_input_height);
    try std.testing.expectEqual(@as(i32, 8), cfg.max_input_height);
    try std.testing.expectEqual(@as(i32, 2), cfg.status_height);
}

// ---------------------------------------------------------------------------
// WindowManager.init (no ncurses)
// ---------------------------------------------------------------------------

test "WindowManager: init creates instance with config" {
    const cfg = wm.WindowConfig{};
    const mgr = wm.WindowManager.init(std.testing.allocator, cfg);

    try std.testing.expectEqual(@as(i32, 5), mgr.config.min_conv_height);
    try std.testing.expectEqual(@as(i32, 2), mgr.config.min_input_height);
    // Windows should be null before setup()
    try std.testing.expect(mgr.stdscr == null);
    try std.testing.expect(mgr.conv_win == null);
    try std.testing.expect(mgr.status_win == null);
    try std.testing.expect(mgr.input_win == null);
}

test "WindowManager: initial dimensions are zero before setup" {
    const cfg = wm.WindowConfig{};
    const mgr = wm.WindowManager.init(std.testing.allocator, cfg);

    try std.testing.expectEqual(@as(i32, 0), mgr.screen_height);
    try std.testing.expectEqual(@as(i32, 0), mgr.screen_width);
    try std.testing.expectEqual(@as(i32, 0), mgr.conv_height);
    try std.testing.expectEqual(@as(i32, 0), mgr.conv_width);
}

// ---------------------------------------------------------------------------
// Layout arithmetic (replicated from updateDimensions)
// ---------------------------------------------------------------------------

/// Compute the layout the same way WindowManager.updateDimensions does.
fn computeLayout(
    cfg: wm.WindowConfig,
    screen_h: i32,
    screen_w: i32,
) struct { conv_height: i32, status_y: i32, input_y: i32 } {
    _ = screen_w;
    const conv_h = screen_h - cfg.status_height - cfg.min_input_height;
    const status_y = conv_h;
    const input_y = status_y + cfg.status_height;
    return .{
        .conv_height = conv_h,
        .status_y = status_y,
        .input_y = input_y,
    };
}

test "WindowManager: layout arithmetic for 80x24 terminal" {
    const cfg = wm.WindowConfig{};
    const layout = computeLayout(cfg, 24, 80);

    // conv_height = 24 - 1 (status) - 2 (min_input) = 21
    try std.testing.expectEqual(@as(i32, 21), layout.conv_height);
    // status_y = conv_height
    try std.testing.expectEqual(@as(i32, 21), layout.status_y);
    // input_y = status_y + status_height = 21 + 1 = 22
    try std.testing.expectEqual(@as(i32, 22), layout.input_y);
}

test "WindowManager: layout arithmetic for minimal 40x10 terminal" {
    const cfg = wm.WindowConfig{};
    const layout = computeLayout(cfg, 10, 40);

    // conv_height = 10 - 1 - 2 = 7
    try std.testing.expectEqual(@as(i32, 7), layout.conv_height);
    try std.testing.expectEqual(@as(i32, 7), layout.status_y);
    try std.testing.expectEqual(@as(i32, 8), layout.input_y);
}

test "WindowManager: layout arithmetic with custom status_height" {
    const cfg = wm.WindowConfig{ .status_height = 2, .min_input_height = 3 };
    const layout = computeLayout(cfg, 24, 80);

    // conv_height = 24 - 2 - 3 = 19
    try std.testing.expectEqual(@as(i32, 19), layout.conv_height);
    try std.testing.expectEqual(@as(i32, 19), layout.status_y);
    // input_y = 19 + 2 = 21
    try std.testing.expectEqual(@as(i32, 21), layout.input_y);
}

// ---------------------------------------------------------------------------
// setup() / createWindows() require ncurses — skip
// ---------------------------------------------------------------------------

test "WindowManager: setup requires ncurses terminal (skipped headless)" {
    return error.SkipZigTest;
}

test "WindowManager: pad capacity growth requires ncurses (skipped headless)" {
    return error.SkipZigTest;
}

test "WindowManager: input resize requires ncurses (skipped headless)" {
    return error.SkipZigTest;
}
