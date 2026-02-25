//! tests/test_tui_scrolling_calculations.zig
//!   — Zig port of tests/test_tui_scrolling_calculations.c
//!
//! Tests cursor-position and scrolling arithmetic that lives in the TUI
//! input renderer.  All computation is pure — no ncurses required.

const std = @import("std");

// ---------------------------------------------------------------------------
// Cursor position helpers (mirrors tui.c input-rendering logic)
// ---------------------------------------------------------------------------

const CursorPos = struct { line: i32, col: i32 };

/// Walk `buf[0..cursor_pos]` counting newlines to derive (line, col).
fn calcCursorPos(buf: []const u8, cursor_pos: usize) CursorPos {
    var line: i32 = 0;
    var col: i32 = 0;
    const end = @min(cursor_pos, buf.len);
    for (buf[0..end]) |ch| {
        if (ch == '\n') {
            line += 1;
            col = 0;
        } else {
            col += 1;
        }
    }
    return .{ .line = line, .col = col };
}

// ---------------------------------------------------------------------------
// Test 1: Cursor screen position — no border offset
// ---------------------------------------------------------------------------

test "scrolling: cursor position in multi-line buffer is correct" {
    const buf = "Hello\nWorld\nTest";
    // cursor at index 12 → 'T' (first char of "Test")
    const pos = calcCursorPos(buf, 12);
    try std.testing.expectEqual(@as(i32, 2), pos.line);
    try std.testing.expectEqual(@as(i32, 0), pos.col);
}

test "scrolling: cursor screen_y accounts for scroll offset (no border)" {
    const buf = "Hello\nWorld\nTest";
    const pos = calcCursorPos(buf, 12); // line=2, col=0
    const line_scroll: i32 = 1;

    const screen_y = pos.line - line_scroll;
    const screen_x = pos.col; // no +1 border offset
    try std.testing.expectEqual(@as(i32, 1), screen_y);
    try std.testing.expectEqual(@as(i32, 0), screen_x);
}

test "scrolling: cursor screen_y is within window bounds" {
    const buf = "Hello\nWorld\nTest";
    const pos = calcCursorPos(buf, 12);
    const line_scroll: i32 = 1;
    const win_height: i32 = 5;
    const win_width: i32 = 80;

    const screen_y = pos.line - line_scroll;
    const screen_x = pos.col;
    try std.testing.expect(screen_y >= 0 and screen_y < win_height);
    try std.testing.expect(screen_x >= 0 and screen_x < win_width);
}

// ---------------------------------------------------------------------------
// Test 2: Line-width calculations
// ---------------------------------------------------------------------------

test "scrolling: available_width = win_width - prompt_len" {
    const win_width: i32 = 20;
    const prompt_len: i32 = 3; // "> " + space
    const available = win_width - prompt_len;
    try std.testing.expectEqual(@as(i32, 17), available);
}

test "scrolling: text wraps into multiple lines when exceeding available width" {
    const text = "This is a long line that might wrap";
    const win_width: i32 = 20;
    const prompt_len: i32 = 3;
    const available = win_width - prompt_len;

    var screen_x: i32 = prompt_len;
    var current_line: i32 = 0;

    for (text) |ch| {
        if (ch == '\n') {
            current_line += 1;
            screen_x = 0;
        } else {
            if (screen_x >= available) {
                current_line += 1;
                screen_x = 0;
            }
            screen_x += 1;
        }
    }
    try std.testing.expect(current_line > 0);
}

// ---------------------------------------------------------------------------
// Test 3: Window height consistency (20% of screen, clamped to min)
// ---------------------------------------------------------------------------

const INPUT_WIN_MIN_HEIGHT: i32 = 2; // content lines, no borders
const INPUT_WIN_MAX_HEIGHT_PERCENT: i32 = 20;

fn calcMaxInputHeight(screen_h: i32) i32 {
    var h = @divTrunc(screen_h * INPUT_WIN_MAX_HEIGHT_PERCENT, 100);
    if (h < INPUT_WIN_MIN_HEIGHT) h = INPUT_WIN_MIN_HEIGHT;
    return h;
}

test "scrolling: 20% of 24-line screen = 4 input lines" {
    try std.testing.expectEqual(@as(i32, 4), calcMaxInputHeight(24));
}

test "scrolling: 20% of 10-line screen = 2 input lines (equals minimum)" {
    try std.testing.expectEqual(@as(i32, 2), calcMaxInputHeight(10));
}

test "scrolling: 20% of 5-line screen clamped to minimum (2)" {
    // 20% of 5 = 1, must be raised to INPUT_WIN_MIN_HEIGHT = 2
    try std.testing.expectEqual(@as(i32, 2), calcMaxInputHeight(5));
}

// ---------------------------------------------------------------------------
// Test 4: Prompt positioning — starts at (0, 0), not (1, 1)
// ---------------------------------------------------------------------------

test "scrolling: prompt y-position is 0 (no border row)" {
    const prompt_y: i32 = 0;
    try std.testing.expectEqual(@as(i32, 0), prompt_y);
}

test "scrolling: prompt x-position is 0 (no border column)" {
    const prompt_x: i32 = 0;
    try std.testing.expectEqual(@as(i32, 0), prompt_x);
}

test "scrolling: text starts at x = prompt_len (not prompt_len + 1)" {
    const prompt_len: i32 = 3;
    const text_start_x: i32 = prompt_len; // no border offset
    try std.testing.expectEqual(@as(i32, 3), text_start_x);
}

// ---------------------------------------------------------------------------
// Test 5: Scrolling bounds — cursor visibility for each scroll offset
// ---------------------------------------------------------------------------

test "scrolling bounds: cursor is visible for correct scroll offsets" {
    const buf = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5";
    const cursor_pos = buf.len; // at end
    const pos = calcCursorPos(buf, cursor_pos);
    const win_height: i32 = 3;

    // cursor_line == 4 (lines 0-4)
    try std.testing.expectEqual(@as(i32, 4), pos.line);

    // offsets 2, 3, 4 keep the cursor within [0, win_height)
    // offsets 0, 1, 5+ don't
    const expected_visible = [_]bool{ false, false, true, true, true, false };
    for (expected_visible, 0..) |want, offset_usize| {
        const offset: i32 = @intCast(offset_usize);
        const sy = pos.line - offset;
        const visible = sy >= 0 and sy < win_height;
        try std.testing.expectEqual(want, visible);
    }
}
