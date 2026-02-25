//! tests/test_utf8_truncate.zig — Zig port of tests/test_utf8_truncate.c
//!
//! Tests the UTF-8-aware truncation function `truncateUtf8` from
//! zig/util/string_utils.zig.
//!
//! The C version tested `truncate_utf8` from src/util/string_utils.c.
//! The Zig equivalent is `truncateUtf8` in zig/util/string_utils.zig.
//!
//! Ported test cases:
//!   1. ASCII-only string truncated exactly
//!   2. UTF-8 arrow (3-byte, U+2192 →) not split at a byte inside the sequence
//!   3. Truncation to 1 byte — lands before or at a multi-byte char boundary
//!   4. String already within limit returned whole
//!   5. Empty string returns empty string
//!   6. Large buffer with a multi-byte char near the limit — valid UTF-8 result
//!   7. 4-byte emoji (😀, U+1F600) not split

const std = @import("std");
const string_utils = @import("../util/string_utils.zig");

const truncateUtf8 = string_utils.truncateUtf8;

// ---------------------------------------------------------------------------
// helper: check that a byte slice is valid UTF-8
// ---------------------------------------------------------------------------

fn isValidUtf8(s: []const u8) bool {
    const view = std.unicode.Utf8View.init(s) catch return false;
    var it = view.iterator();
    while (it.nextCodepointSlice()) |_| {}
    return true;
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

test "truncateUtf8: ASCII-only string truncated at exact byte boundary" {
    const alloc = std.testing.allocator;

    const result = try truncateUtf8(alloc, "Hello World", 5);
    defer alloc.free(result);

    try std.testing.expectEqualStrings("Hello", result);
    try std.testing.expect(isValidUtf8(result));
}

test "truncateUtf8: string already within limit is returned whole" {
    const alloc = std.testing.allocator;

    const result = try truncateUtf8(alloc, "Short", 100);
    defer alloc.free(result);

    try std.testing.expectEqualStrings("Short", result);
}

test "truncateUtf8: empty string" {
    const alloc = std.testing.allocator;

    const result = try truncateUtf8(alloc, "", 10);
    defer alloc.free(result);

    try std.testing.expectEqualStrings("", result);
}

test "truncateUtf8: 3-byte UTF-8 arrow (→, U+2192) not split" {
    // "A→B→C→D" — the arrow → is E2 86 92 (3 bytes, 1 display column)
    // Truncating at 4 bytes would land inside the arrow sequence starting at
    // byte 1 (E2); we must back up to 1 byte ("A").
    const alloc = std.testing.allocator;

    const input = "A\xE2\x86\x92B\xE2\x86\x92C"; // A→B→C
    const result = try truncateUtf8(alloc, input, 4);
    defer alloc.free(result);

    try std.testing.expect(isValidUtf8(result));
    try std.testing.expect(result.len <= 4);
    // Must contain at least 'A'
    try std.testing.expect(result.len >= 1);
}

test "truncateUtf8: truncate to 1 byte stays valid" {
    // "A→B" = A + E2 86 92 + B = 5 bytes
    const alloc = std.testing.allocator;

    const input = "A\xE2\x86\x92B";
    const result = try truncateUtf8(alloc, input, 1);
    defer alloc.free(result);

    try std.testing.expect(isValidUtf8(result));
    try std.testing.expect(result.len == 1);
    try std.testing.expectEqualStrings("A", result);
}

test "truncateUtf8: large buffer with multi-byte char near limit" {
    // Build a 12234-byte string:
    //   12230 'A' bytes + E2 86 92 (→) + 'B' + NUL
    // Truncating at bash_output_max (12228) must not split the arrow.
    const alloc = std.testing.allocator;
    const bash_output_max: usize = 12_228;

    var input_buf = try alloc.alloc(u8, 12234);
    defer alloc.free(input_buf);
    @memset(input_buf[0..12230], 'A');
    input_buf[12230] = '\xE2';
    input_buf[12231] = '\x86';
    input_buf[12232] = '\x92'; // →
    input_buf[12233] = 'B';

    const result = try truncateUtf8(alloc, input_buf, bash_output_max);
    defer alloc.free(result);

    try std.testing.expect(isValidUtf8(result));
    try std.testing.expect(result.len <= bash_output_max);
    // At least some of the initial 'A' bytes must be present (the exact count
    // depends on where the UTF-8-safe cut lands, but must be >= 12226 at minimum)
    try std.testing.expect(result.len >= 12226);
}

test "truncateUtf8: 4-byte emoji (😀, U+1F600) not split" {
    // 😀 = F0 9F 98 80 (4 bytes)
    // "A😀B😀C"
    const alloc = std.testing.allocator;

    const input = "A\xF0\x9F\x98\x80B\xF0\x9F\x98\x80C"; // A😀B😀C
    // Truncate at 5 bytes: lands inside second emoji byte → back up to "A😀"
    const result = try truncateUtf8(alloc, input, 5);
    defer alloc.free(result);

    try std.testing.expect(isValidUtf8(result));
    try std.testing.expect(result.len <= 5);
    // At minimum "A" must be present
    try std.testing.expect(result.len >= 1);
}

test "truncateUtf8: 2-byte sequence (é, U+00E9) not split" {
    // "café" = c a f é = 3 ASCII + C3 A9 = 5 bytes total
    // Truncating at 4 bytes lands on 0xA9 (continuation) → backs up to 3 ("caf")
    const alloc = std.testing.allocator;

    const input = "caf\xC3\xA9"; // "café"
    const result = try truncateUtf8(alloc, input, 4);
    defer alloc.free(result);

    try std.testing.expectEqualStrings("caf", result);
    try std.testing.expect(isValidUtf8(result));
}

test "truncateUtf8: truncate at exact multi-byte boundary returns full char" {
    // "café" at 5 bytes (full string) — no truncation needed
    const alloc = std.testing.allocator;

    const input = "caf\xC3\xA9";
    const result = try truncateUtf8(alloc, input, 5);
    defer alloc.free(result);

    try std.testing.expectEqualStrings("caf\xC3\xA9", result);
    try std.testing.expect(isValidUtf8(result));
}
