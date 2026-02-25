//! tests/test_base64.zig — Zig port of tests/test_base64.c
//!
//! Covers the base64 encode/decode functionality from zig/base64.zig.

const std = @import("std");
const base64 = @import("../base64.zig");

// ---------------------------------------------------------------------------
// Encoding tests
// ---------------------------------------------------------------------------

test "base64 encode empty input" {
    const a = std.testing.allocator;
    const enc = try base64.encode(a, "");
    defer a.free(enc);
    try std.testing.expectEqualStrings("", enc);
}

test "base64 encode basic: Hello, World!" {
    const a = std.testing.allocator;
    const enc = try base64.encode(a, "Hello, World!");
    defer a.free(enc);
    try std.testing.expectEqualStrings("SGVsbG8sIFdvcmxkIQ==", enc);
}

test "base64 encode no padding: Man" {
    const a = std.testing.allocator;
    const enc = try base64.encode(a, "Man");
    defer a.free(enc);
    try std.testing.expectEqualStrings("TWFu", enc);
}

test "base64 encode one padding char: Ma" {
    const a = std.testing.allocator;
    const enc = try base64.encode(a, "Ma");
    defer a.free(enc);
    try std.testing.expectEqualStrings("TWE=", enc);
}

test "base64 encode two padding chars: M" {
    const a = std.testing.allocator;
    const enc = try base64.encode(a, "M");
    defer a.free(enc);
    try std.testing.expectEqualStrings("TQ==", enc);
}

// ---------------------------------------------------------------------------
// Decoding tests
// ---------------------------------------------------------------------------

test "base64 decode empty input" {
    const a = std.testing.allocator;
    const dec = try base64.decode(a, "");
    defer a.free(dec);
    try std.testing.expectEqualStrings("", dec);
}

test "base64 decode basic: Hello, World!" {
    const a = std.testing.allocator;
    const dec = try base64.decode(a, "SGVsbG8sIFdvcmxkIQ==");
    defer a.free(dec);
    try std.testing.expectEqualStrings("Hello, World!", dec);
}

test "base64 decode no padding: Man" {
    const a = std.testing.allocator;
    const dec = try base64.decode(a, "TWFu");
    defer a.free(dec);
    try std.testing.expectEqualStrings("Man", dec);
}

test "base64 decode one padding: Ma" {
    const a = std.testing.allocator;
    const dec = try base64.decode(a, "TWE=");
    defer a.free(dec);
    try std.testing.expectEqualStrings("Ma", dec);
}

test "base64 decode two padding: M" {
    const a = std.testing.allocator;
    const dec = try base64.decode(a, "TQ==");
    defer a.free(dec);
    try std.testing.expectEqualStrings("M", dec);
}

// ---------------------------------------------------------------------------
// Round-trip tests
// ---------------------------------------------------------------------------

test "base64 round-trip: complex string" {
    const a = std.testing.allocator;
    const original = "Test roundtrip with various characters: !@#$%^&*()_+-=[]{}|;:,.<>?/`~";
    const enc = try base64.encode(a, original);
    defer a.free(enc);
    const dec = try base64.decode(a, enc);
    defer a.free(dec);
    try std.testing.expectEqualStrings(original, dec);
}

test "base64 round-trip: binary data (all byte values)" {
    const a = std.testing.allocator;
    var original: [256]u8 = undefined;
    for (&original, 0..) |*b, i| b.* = @intCast(i);
    const enc = try base64.encode(a, &original);
    defer a.free(enc);
    const dec = try base64.decode(a, enc);
    defer a.free(dec);
    try std.testing.expectEqualSlices(u8, &original, dec);
}

// ---------------------------------------------------------------------------
// Length / size tests (mirroring C test_base64_length_calculation)
// ---------------------------------------------------------------------------

test "base64 encoded length calculation" {
    // For standard base64, encoded_len = ceil(n/3)*4
    const a = std.testing.allocator;
    const cases = [_]struct { input_len: usize, expected_enc_len: usize }{
        .{ .input_len = 0, .expected_enc_len = 0 },
        .{ .input_len = 1, .expected_enc_len = 4 },
        .{ .input_len = 2, .expected_enc_len = 4 },
        .{ .input_len = 3, .expected_enc_len = 4 },
        .{ .input_len = 4, .expected_enc_len = 8 },
        .{ .input_len = 6, .expected_enc_len = 8 },
        .{ .input_len = 9, .expected_enc_len = 12 },
    };
    for (cases) |tc| {
        const data = try a.alloc(u8, tc.input_len);
        defer a.free(data);
        @memset(data, 'A');
        const enc = try base64.encode(a, data);
        defer a.free(enc);
        try std.testing.expectEqual(tc.expected_enc_len, enc.len);
    }
}

// ---------------------------------------------------------------------------
// URL-safe variant
// ---------------------------------------------------------------------------

test "base64 url-safe round-trip" {
    const a = std.testing.allocator;
    const original = "hello url-safe world!";
    const enc = try base64.encodeUrlSafe(a, original);
    defer a.free(enc);
    const dec = try base64.decodeUrlSafe(a, enc);
    defer a.free(dec);
    try std.testing.expectEqualStrings(original, dec);
}

// ---------------------------------------------------------------------------
// Invalid input
// ---------------------------------------------------------------------------

test "base64 decode invalid characters returns error" {
    const a = std.testing.allocator;
    // '!' is not in the standard base64 alphabet
    const result = base64.decode(a, "!@#$");
    try std.testing.expectError(error.InvalidCharacter, result);
}
