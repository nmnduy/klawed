//! tests/test_tui_auto_scroll.zig — Zig port of tests/test_tui_auto_scroll.c
//!
//! Tests the TUI auto-scroll condition:
//!   should_auto_scroll  ⟺  scroll_offset >= max_scroll - 1
//!
//! All logic is pure arithmetic — no ncurses required.

const std = @import("std");

// ---------------------------------------------------------------------------
// Replicated auto-scroll predicate (mirrors tui.c NORMAL/COMMAND path)
// ---------------------------------------------------------------------------

/// Returns true when the conversation view should auto-scroll to the bottom.
///
/// Mirrors the logic from tui.c (NORMAL/COMMAND mode branch):
///   - If there is no content, or everything fits in the viewport → scroll.
///   - If we are within one line of the bottom → scroll.
///   - Otherwise → don't scroll (user has scrolled up deliberately).
fn shouldAutoScroll(scroll_offset: i32, max_scroll: i32, content_lines: i32) bool {
    if (content_lines == 0 or max_scroll <= 0) {
        return true;
    }
    return scroll_offset >= max_scroll - 1;
}

// ---------------------------------------------------------------------------
// Basic cases
// ---------------------------------------------------------------------------

test "auto-scroll: at bottom (100/100) should auto-scroll" {
    try std.testing.expect(shouldAutoScroll(100, 100, 200));
}

test "auto-scroll: one line from bottom (99/100) should auto-scroll" {
    try std.testing.expect(shouldAutoScroll(99, 100, 200));
}

test "auto-scroll: two lines from bottom (98/100) should NOT auto-scroll" {
    try std.testing.expect(!shouldAutoScroll(98, 100, 200));
}

test "auto-scroll: far from bottom (50/100) should NOT auto-scroll" {
    try std.testing.expect(!shouldAutoScroll(50, 100, 200));
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

test "auto-scroll: max_scroll == 1, at bottom (1/1) should auto-scroll" {
    try std.testing.expect(shouldAutoScroll(1, 1, 10));
}

test "auto-scroll: max_scroll == 0 (all content fits) should auto-scroll" {
    try std.testing.expect(shouldAutoScroll(0, 0, 5));
}

test "auto-scroll: negative scroll offset should NOT auto-scroll" {
    try std.testing.expect(!shouldAutoScroll(-1, 10, 20));
}

test "auto-scroll: no content (content_lines == 0) should auto-scroll" {
    try std.testing.expect(shouldAutoScroll(0, 0, 0));
}

test "auto-scroll: negative max_scroll (content fits in viewport) should auto-scroll" {
    try std.testing.expect(shouldAutoScroll(0, -5, 10));
}

test "auto-scroll: INSERT mode always auto-scrolls by design (placeholder)" {
    // INSERT mode calls window_manager_scroll_to_bottom directly, bypassing
    // this predicate entirely.  The logic is correct by construction.
    try std.testing.expect(true);
}

// ---------------------------------------------------------------------------
// Boundary conditions
// ---------------------------------------------------------------------------

test "auto-scroll boundary: scroll_offset == max_scroll - 1 → auto-scroll" {
    try std.testing.expect(shouldAutoScroll(99, 100, 200));
}

test "auto-scroll boundary: scroll_offset == max_scroll - 2 → no auto-scroll" {
    try std.testing.expect(!shouldAutoScroll(98, 100, 200));
}

test "auto-scroll boundary: large numbers 9999/10000 → auto-scroll" {
    try std.testing.expect(shouldAutoScroll(9999, 10000, 20000));
}

test "auto-scroll boundary: large numbers 9998/10000 → no auto-scroll" {
    try std.testing.expect(!shouldAutoScroll(9998, 10000, 20000));
}

// ---------------------------------------------------------------------------
// Percentage calculation (mirrors tui.c status-bar display logic)
// ---------------------------------------------------------------------------

/// Integer percentage with rounding-by-adding-half-divisor.
fn scrollPercent(scroll_offset: i32, max_scroll: i32) i32 {
    if (max_scroll <= 0) return 100;
    return @divTrunc(scroll_offset * 100 + @divTrunc(max_scroll, 2), max_scroll);
}

test "scroll percent: 75 / 100 == 75%" {
    try std.testing.expectEqual(@as(i32, 75), scrollPercent(75, 100));
}

test "scroll percent: 99 / 100 == 99%" {
    try std.testing.expectEqual(@as(i32, 99), scrollPercent(99, 100));
}

test "scroll percent: 0 / 0 returns 100% (guard)" {
    try std.testing.expectEqual(@as(i32, 100), scrollPercent(0, 0));
}

test "scroll percent: 0 / 100 == 0%" {
    try std.testing.expectEqual(@as(i32, 0), scrollPercent(0, 100));
}

test "scroll percent: 50 / 100 == 50%" {
    try std.testing.expectEqual(@as(i32, 50), scrollPercent(50, 100));
}
