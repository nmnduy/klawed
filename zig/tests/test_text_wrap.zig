//! tests/test_text_wrap.zig — Zig port of tests/test_text_wrap.c
//!
//! Tests text-wrapping logic: finding the byte offset at which a line of text
//! should wrap to fit within a given display-column width, correctly handling
//! UTF-8 multi-byte characters and wide (double-column) CJK characters.
//!
//! The C test embedded a copy of `find_wrap_point` from `src/tui_render.c`.
//! The Zig TUI renderer does not yet export a standalone function for this,
//! so we provide an equivalent Zig implementation here and test it directly.
//!
//! Ported test groups:
//!   - ASCII text wrapping (width 0/1/5/11/20/negative)
//!   - UTF-8 multi-byte wrapping (café, Japanese CJK)
//!   - Mixed ASCII + UTF-8 wrapping
//!   - Edge cases (empty string, single char, long text, embedded newlines)
//!   - Border string byte-length

const std = @import("std");

// ---------------------------------------------------------------------------
// findWrapPoint — Zig equivalent of the C find_wrap_point function
//
// Returns the number of *bytes* from the start of `text` that fit within
// `max_display_width` display columns.  UTF-8 multi-byte sequences are never
// split.  Wide characters (double-column CJK, etc.) count as 2 columns.
//
// Returns at least 1 to guarantee forward progress even when the first
// character exceeds the available width.
// ---------------------------------------------------------------------------

fn charDisplayWidth(cp: u21) i32 {
    // Control characters
    if (cp < 0x20 or cp == 0x7F) return 0;
    // ASCII printable
    if (cp < 0x80) return 1;

    // Wide CJK ranges (simplified — covers the common cases used in tests)
    // This mirrors the wcwidth rules for the CJK Unified Ideographs and
    // surrounding blocks.
    if ((cp >= 0x1100 and cp <= 0x115F) or // Hangul Jamo
        (cp >= 0x2E80 and cp <= 0x303E) or // CJK Radicals
        (cp >= 0x3041 and cp <= 0x33BF) or // Hiragana / Katakana / CJK compat
        (cp >= 0x3400 and cp <= 0x4DBF) or // CJK Ext A
        (cp >= 0x4E00 and cp <= 0x9FFF) or // CJK Unified
        (cp >= 0xAC00 and cp <= 0xD7AF) or // Hangul
        (cp >= 0xF900 and cp <= 0xFAFF) or // CJK Compat Ideographs
        (cp >= 0xFE30 and cp <= 0xFE6F) or // CJK Compat Forms
        (cp >= 0xFF01 and cp <= 0xFF60) or // Full-width Forms
        (cp >= 0xFFE0 and cp <= 0xFFE6) or // Full-width Signs
        (cp >= 0x1F300 and cp <= 0x1F9FF)) // Misc Symbols + Emoji
    {
        return 2;
    }

    return 1;
}

fn findWrapPoint(text: []const u8, max_display_width: i32) usize {
    if (max_display_width <= 0) return 1;

    var bytes_used: usize = 0;
    var display_width: i32 = 0;
    var i: usize = 0;

    while (i < text.len) {
        // Determine byte length and codepoint of next UTF-8 sequence
        const b0 = text[i];
        var char_bytes: usize = 0;
        var cp: u21 = 0;

        if (b0 < 0x80) {
            char_bytes = 1;
            cp = b0;
        } else if (b0 < 0xE0) {
            if (i + 1 >= text.len) { bytes_used += 1; break; }
            char_bytes = 2;
            cp = (@as(u21, b0 & 0x1F) << 6) | (text[i + 1] & 0x3F);
        } else if (b0 < 0xF0) {
            if (i + 2 >= text.len) { bytes_used += 1; break; }
            char_bytes = 3;
            cp = (@as(u21, b0 & 0x0F) << 12) |
                (@as(u21, text[i + 1] & 0x3F) << 6) |
                (text[i + 2] & 0x3F);
        } else {
            if (i + 3 >= text.len) { bytes_used += 1; break; }
            char_bytes = 4;
            cp = (@as(u21, b0 & 0x07) << 18) |
                (@as(u21, text[i + 1] & 0x3F) << 12) |
                (@as(u21, text[i + 2] & 0x3F) << 6) |
                (text[i + 3] & 0x3F);
        }

        const w = charDisplayWidth(cp);
        if (display_width + w > max_display_width) break;

        bytes_used += char_bytes;
        display_width += w;
        i += char_bytes;
    }

    return if (bytes_used > 0) bytes_used else 1;
}

// ---------------------------------------------------------------------------
// ASCII wrapping tests
// ---------------------------------------------------------------------------

test "text wrap: ASCII width=5 fits 'Hello'" {
    const text = "Hello World";
    try std.testing.expectEqual(@as(usize, 5), findWrapPoint(text, 5));
}

test "text wrap: ASCII width=11 fits entire string" {
    const text = "Hello World";
    try std.testing.expectEqual(@as(usize, 11), findWrapPoint(text, 11));
}

test "text wrap: ASCII width=20 fits entire string (more space than needed)" {
    const text = "Hello World";
    try std.testing.expectEqual(@as(usize, 11), findWrapPoint(text, 20));
}

test "text wrap: ASCII width=1 fits 1 char" {
    const text = "Hello World";
    try std.testing.expectEqual(@as(usize, 1), findWrapPoint(text, 1));
}

test "text wrap: ASCII width=0 returns 1 for progress" {
    const text = "Hello World";
    try std.testing.expectEqual(@as(usize, 1), findWrapPoint(text, 0));
}

test "text wrap: ASCII width=-5 returns 1 for progress" {
    const text = "Hello World";
    try std.testing.expectEqual(@as(usize, 1), findWrapPoint(text, -5));
}

// ---------------------------------------------------------------------------
// UTF-8 multi-byte wrapping tests
// ---------------------------------------------------------------------------

test "text wrap: UTF-8 'café' (5 bytes) — width=4 returns all 5 bytes" {
    // "café" = c a f + C3 A9 (é) = 5 bytes, 4 display columns
    const cafe = "caf\xC3\xA9";
    try std.testing.expectEqual(@as(usize, 5), findWrapPoint(cafe, 4));
}

test "text wrap: UTF-8 'café' — width=3 returns 3 bytes ('caf')" {
    const cafe = "caf\xC3\xA9";
    try std.testing.expectEqual(@as(usize, 3), findWrapPoint(cafe, 3));
}

test "text wrap: UTF-8 'café' — width=2 returns 2 bytes ('ca')" {
    const cafe = "caf\xC3\xA9";
    try std.testing.expectEqual(@as(usize, 2), findWrapPoint(cafe, 2));
}

test "text wrap: Japanese '日本語' — width=6 returns all 9 bytes" {
    // 日 = E6 97 A5, 本 = E6 9C AC, 語 = E8 AA 9E — each 3 bytes / 2 display cols
    const jp = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
    try std.testing.expectEqual(@as(usize, 9), findWrapPoint(jp, 6));
}

test "text wrap: Japanese '日本語' — width=4 returns 6 bytes ('日本')" {
    const jp = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
    try std.testing.expectEqual(@as(usize, 6), findWrapPoint(jp, 4));
}

test "text wrap: Japanese '日本語' — width=2 returns 3 bytes ('日')" {
    const jp = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
    try std.testing.expectEqual(@as(usize, 3), findWrapPoint(jp, 2));
}

test "text wrap: Japanese — width=1 can't fit 2-col char, returns 1 for progress" {
    const jp = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
    const result = findWrapPoint(jp, 1);
    // Either 1 (minimum progress) or 3 (first complete char even though wide) are acceptable
    try std.testing.expect(result == 1 or result == 3);
}

// ---------------------------------------------------------------------------
// Mixed ASCII + UTF-8 wrapping tests
// ---------------------------------------------------------------------------

test "text wrap: 'Hello 日本' — width=10 returns all 12 bytes" {
    // "Hello " = 6 bytes/cols, 日 = 3 bytes/2 cols, 本 = 3 bytes/2 cols → total 12 bytes, 10 cols
    const mixed = "Hello \xE6\x97\xA5\xE6\x9C\xAC";
    try std.testing.expectEqual(@as(usize, 12), findWrapPoint(mixed, 10));
}

test "text wrap: 'Hello 日本' — width=8 returns 9 bytes ('Hello 日')" {
    const mixed = "Hello \xE6\x97\xA5\xE6\x9C\xAC";
    try std.testing.expectEqual(@as(usize, 9), findWrapPoint(mixed, 8));
}

test "text wrap: 'Hello 日本' — width=6 returns 6 bytes ('Hello ')" {
    const mixed = "Hello \xE6\x97\xA5\xE6\x9C\xAC";
    try std.testing.expectEqual(@as(usize, 6), findWrapPoint(mixed, 6));
}

test "text wrap: 'Hello 日本' — width=7 returns 6 bytes (can't fit 2-col char)" {
    const mixed = "Hello \xE6\x97\xA5\xE6\x9C\xAC";
    try std.testing.expectEqual(@as(usize, 6), findWrapPoint(mixed, 7));
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

test "text wrap: empty string returns 1 for progress" {
    const result = findWrapPoint("", 10);
    try std.testing.expectEqual(@as(usize, 1), result);
}

test "text wrap: single char fits in width=10" {
    try std.testing.expectEqual(@as(usize, 1), findWrapPoint("X", 10));
}

test "text wrap: single char fits in width=1" {
    try std.testing.expectEqual(@as(usize, 1), findWrapPoint("X", 1));
}

test "text wrap: long ASCII text — width=20" {
    const text = "This is a very long line of text that should be wrapped at various points";
    try std.testing.expectEqual(@as(usize, 20), findWrapPoint(text, 20));
}

test "text wrap: long ASCII text — width=40" {
    const text = "This is a very long line of text that should be wrapped at various points";
    try std.testing.expectEqual(@as(usize, 40), findWrapPoint(text, 40));
}

test "text wrap: long ASCII text — width=100 fits entire string" {
    const text = "This is a very long line of text that should be wrapped at various points";
    try std.testing.expectEqual(text.len, findWrapPoint(text, 100));
}

test "text wrap: string with embedded newline — width=5 includes newline (0-width)" {
    // '\n' has display width 0 so it doesn't consume a column.
    // width=5 in "Line1\nLine2": L+i+n+e+1 = 5 display cols, then '\n' adds 0 cols
    // and is consumed → result is 6 bytes ("Line1\n").
    const text = "Line1\nLine2";
    try std.testing.expectEqual(@as(usize, 6), findWrapPoint(text, 5));
}

// ---------------------------------------------------------------------------
// Border string byte-length sanity check
// ---------------------------------------------------------------------------

test "text wrap: box-drawing '│ ' border is 4 bytes" {
    // │ = U+2502 = E2 94 82 (3 bytes) + space (1 byte) = 4 bytes total
    const border = "\xE2\x94\x82 ";
    try std.testing.expectEqual(@as(usize, 4), border.len);
}

test "text wrap: 78 ASCII chars fit in width=78" {
    var content: [78]u8 = undefined;
    @memset(&content, 'a');
    try std.testing.expectEqual(@as(usize, 78), findWrapPoint(&content, 78));
}
