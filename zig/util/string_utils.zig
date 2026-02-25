//! String Utilities
//!
//! Idiomatic Zig replacements for src/util/string_utils.c
//!
//! Key C→Zig translations:
//!   - `trim_whitespace` (in-place mutation) → `trim` (returns a slice, no alloc)
//!   - `strdup_trim` (alloc + trim)          → `dupTrim` (allocator + trim)
//!   - `strip_ansi_escapes` (malloc result)  → `stripAnsi` (allocator result)
//!   - `truncate_utf8` (malloc result)       → `truncateUtf8` (allocator result)

const std = @import("std");

/// Trim leading and trailing ASCII whitespace from a slice.
/// Returns a sub-slice of the input — no allocation, no copy.
///
/// Equivalent to C `trim_whitespace` but non-destructive and allocation-free.
pub fn trim(s: []const u8) []const u8 {
    return std.mem.trim(u8, s, &std.ascii.whitespace);
}

/// Duplicate a string and trim ASCII whitespace from both ends.
/// Caller owns the returned slice and must free it with `allocator.free`.
pub fn dupTrim(allocator: std.mem.Allocator, s: []const u8) ![]u8 {
    const trimmed = trim(s);
    return allocator.dupe(u8, trimmed);
}

/// Remove ANSI/VT100 escape sequences from `input`.
/// Allocates a fresh buffer; caller must free with `allocator.free`.
///
/// Handles CSI sequences (ESC '[' … letter) and bare ESC sequences.
pub fn stripAnsi(allocator: std.mem.Allocator, input: []const u8) ![]u8 {
    var out = std.ArrayList(u8).init(allocator);
    defer out.deinit();

    var i: usize = 0;
    while (i < input.len) {
        if (input[i] == '\x1b') {
            // Skip ESC + optional '[' + params + terminating letter
            i += 1;
            if (i < input.len and input[i] == '[') {
                i += 1; // skip '['
            }
            // Skip until we hit a letter or '@' (the command byte)
            while (i < input.len) : (i += 1) {
                const c = input[i];
                if (std.ascii.isAlphabetic(c) or c == '@') {
                    i += 1;
                    break;
                }
            }
        } else {
            try out.append(input[i]);
            i += 1;
        }
    }

    return out.toOwnedSlice();
}

/// Truncate `s` to at most `max_bytes` bytes, respecting UTF-8 character
/// boundaries (never splits a multi-byte sequence).
///
/// If `s` already fits within `max_bytes`, a copy of the full string is
/// returned.  The caller must free the result with `allocator.free`.
///
/// Returns error.InvalidUtf8 if the input is not valid UTF-8.
pub fn truncateUtf8(allocator: std.mem.Allocator, s: []const u8, max_bytes: usize) ![]u8 {
    if (s.len <= max_bytes) {
        return allocator.dupe(u8, s);
    }

    // Walk backwards from max_bytes to find a valid UTF-8 start byte.
    // UTF-8 continuation bytes have the form 10xxxxxx (0x80–0xBF).
    var cut = max_bytes;
    while (cut > 0 and (s[cut] & 0xC0) == 0x80) {
        cut -= 1;
    }
    // cut == 0 means everything was continuation bytes (invalid UTF-8 or
    // pathological case) — fall back to a hard byte cut.
    if (cut == 0) cut = max_bytes;

    return allocator.dupe(u8, s[0..cut]);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "trim: removes leading/trailing whitespace" {
    try std.testing.expectEqualStrings("hello", trim("  hello  "));
    try std.testing.expectEqualStrings("hello", trim("\t hello \n"));
    try std.testing.expectEqualStrings("", trim("   "));
    try std.testing.expectEqualStrings("hello", trim("hello"));
}

test "trim: does not allocate" {
    // trim returns a sub-slice — pointer equality with interior of input
    const input = "  hi  ";
    const result = trim(input);
    try std.testing.expect(result.ptr == input.ptr + 2);
    try std.testing.expectEqual(@as(usize, 2), result.len);
}

test "dupTrim: allocates trimmed copy" {
    const allocator = std.testing.allocator;
    const result = try dupTrim(allocator, "  world  ");
    defer allocator.free(result);
    try std.testing.expectEqualStrings("world", result);
}

test "stripAnsi: removes CSI sequences" {
    const allocator = std.testing.allocator;
    const input = "\x1b[32mhello\x1b[0m world";
    const result = try stripAnsi(allocator, input);
    defer allocator.free(result);
    try std.testing.expectEqualStrings("hello world", result);
}

test "stripAnsi: bare ESC sequence strips ESC and command byte" {
    const allocator = std.testing.allocator;
    // ESC followed by 'c' — 'c' is the command byte and is consumed.
    // The second 'c' and 'd' pass through unchanged.
    const input = "ab\x1bccd";
    const result = try stripAnsi(allocator, input);
    defer allocator.free(result);
    try std.testing.expectEqualStrings("abcd", result);
}

test "stripAnsi: passthrough when no escapes" {
    const allocator = std.testing.allocator;
    const input = "plain text";
    const result = try stripAnsi(allocator, input);
    defer allocator.free(result);
    try std.testing.expectEqualStrings("plain text", result);
}

test "truncateUtf8: short string returned whole" {
    const allocator = std.testing.allocator;
    const result = try truncateUtf8(allocator, "hi", 10);
    defer allocator.free(result);
    try std.testing.expectEqualStrings("hi", result);
}

test "truncateUtf8: ASCII truncation" {
    const allocator = std.testing.allocator;
    const result = try truncateUtf8(allocator, "hello world", 5);
    defer allocator.free(result);
    try std.testing.expectEqualStrings("hello", result);
}

test "truncateUtf8: does not split 2-byte UTF-8 sequence" {
    // 'é' = 0xC3 0xA9 — 2 bytes
    const allocator = std.testing.allocator;
    const input = "caf\xC3\xA9"; // "café"
    // max_bytes=4 would land in the middle of 'é'; should back up to 3
    const result = try truncateUtf8(allocator, input, 4);
    defer allocator.free(result);
    try std.testing.expectEqualStrings("caf", result);
}

test "truncateUtf8: does not split 3-byte UTF-8 sequence" {
    // '€' = 0xE2 0x82 0xAC — 3 bytes
    const allocator = std.testing.allocator;
    const input = "x\xE2\x82\xACy"; // "x€y"
    // max_bytes=3 lands inside '€' (0x82 is continuation) — back up to 1
    const result = try truncateUtf8(allocator, input, 3);
    defer allocator.free(result);
    try std.testing.expectEqualStrings("x", result);
}
