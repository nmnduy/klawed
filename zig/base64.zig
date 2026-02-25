//! Base64 encoding and decoding
//!
//! Idiomatic Zig replacement for src/base64.c
//!
//! Key C→Zig translations:
//!   - `base64_encode` (malloc result + out-param length) → `encode` (allocator, returns slice)
//!   - `base64_decode` (malloc result + out-param length) → `decode` (allocator, returns slice)
//!
//! Uses `std.base64.standard` (RFC 4648 standard alphabet with `=` padding).
//! For URL-safe base64 (no padding, `-`/`_` alphabet) use `std.base64.url_safe_no_pad`.

const std = @import("std");

// Re-export the standard codec objects for callers that want direct access.
pub const standard = std.base64.standard;
pub const url_safe = std.base64.url_safe;
pub const url_safe_no_pad = std.base64.url_safe_no_pad;

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

/// Encode `input` bytes as standard base64 (with `=` padding).
/// Caller must free the result with `allocator.free`.
pub fn encode(allocator: std.mem.Allocator, input: []const u8) ![]u8 {
    const encoded_len = std.base64.standard.Encoder.calcSize(input.len);
    const buf = try allocator.alloc(u8, encoded_len);
    _ = std.base64.standard.Encoder.encode(buf, input);
    return buf;
}

/// Encode `input` bytes as URL-safe base64 (no padding).
/// Caller must free the result with `allocator.free`.
pub fn encodeUrlSafe(allocator: std.mem.Allocator, input: []const u8) ![]u8 {
    const encoded_len = std.base64.url_safe_no_pad.Encoder.calcSize(input.len);
    const buf = try allocator.alloc(u8, encoded_len);
    _ = std.base64.url_safe_no_pad.Encoder.encode(buf, input);
    return buf;
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

/// Decode standard base64 (with or without `=` padding).
/// Returns an error on invalid input characters.
/// Caller must free the result with `allocator.free`.
pub fn decode(allocator: std.mem.Allocator, input: []const u8) ![]u8 {
    const decoded_len = try std.base64.standard.Decoder.calcSizeForSlice(input);
    const buf = try allocator.alloc(u8, decoded_len);
    errdefer allocator.free(buf);
    try std.base64.standard.Decoder.decode(buf, input);
    return buf;
}

/// Decode URL-safe base64 (no padding).
/// Caller must free the result with `allocator.free`.
pub fn decodeUrlSafe(allocator: std.mem.Allocator, input: []const u8) ![]u8 {
    const decoded_len = try std.base64.url_safe_no_pad.Decoder.calcSizeForSlice(input);
    const buf = try allocator.alloc(u8, decoded_len);
    errdefer allocator.free(buf);
    try std.base64.url_safe_no_pad.Decoder.decode(buf, input);
    return buf;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "encode: empty input" {
    const a = std.testing.allocator;
    const enc = try encode(a, "");
    defer a.free(enc);
    try std.testing.expectEqualStrings("", enc);
}

test "encode: 'hello'" {
    const a = std.testing.allocator;
    const enc = try encode(a, "hello");
    defer a.free(enc);
    try std.testing.expectEqualStrings("aGVsbG8=", enc);
}

test "encode: 'Man' (classic padding example)" {
    const a = std.testing.allocator;
    const enc = try encode(a, "Man");
    defer a.free(enc);
    try std.testing.expectEqualStrings("TWFu", enc);
}

test "encode: 'Ma' (one-char padding)" {
    const a = std.testing.allocator;
    const enc = try encode(a, "Ma");
    defer a.free(enc);
    try std.testing.expectEqualStrings("TWE=", enc);
}

test "decode: 'aGVsbG8=' → 'hello'" {
    const a = std.testing.allocator;
    const dec = try decode(a, "aGVsbG8=");
    defer a.free(dec);
    try std.testing.expectEqualStrings("hello", dec);
}

test "decode: 'TWFu' → 'Man'" {
    const a = std.testing.allocator;
    const dec = try decode(a, "TWFu");
    defer a.free(dec);
    try std.testing.expectEqualStrings("Man", dec);
}

test "encode/decode round-trip: binary data" {
    const a = std.testing.allocator;
    const original = [_]u8{ 0x00, 0xFF, 0x80, 0x7F, 0x01, 0xFE };
    const enc = try encode(a, &original);
    defer a.free(enc);
    const dec = try decode(a, enc);
    defer a.free(dec);
    try std.testing.expectEqualSlices(u8, &original, dec);
}

test "encode/decode round-trip: all byte values" {
    const a = std.testing.allocator;
    var original: [256]u8 = undefined;
    for (&original, 0..) |*b, i| b.* = @intCast(i);
    const enc = try encode(a, &original);
    defer a.free(enc);
    const dec = try decode(a, enc);
    defer a.free(dec);
    try std.testing.expectEqualSlices(u8, &original, dec);
}

test "encodeUrlSafe / decodeUrlSafe round-trip" {
    const a = std.testing.allocator;
    const original = "hello url-safe world!";
    const enc = try encodeUrlSafe(a, original);
    defer a.free(enc);
    const dec = try decodeUrlSafe(a, enc);
    defer a.free(dec);
    try std.testing.expectEqualStrings(original, dec);
}

test "decode: invalid base64 characters return error" {
    const a = std.testing.allocator;
    // '!' and '@' are not in the standard base64 alphabet
    const result = decode(a, "!@#$");
    try std.testing.expectError(error.InvalidCharacter, result);
}
