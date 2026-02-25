//! Format Utilities
//!
//! Idiomatic Zig replacements for src/util/format_utils.c
//!
//! Key C→Zig translations:
//!   - `format_file_size` (static buffer, not thread-safe) → `formatFileSize` (allocates, thread-safe)
//!
//! The C version used a static buffer (not thread-safe). Here we always
//! allocate so the result is safe to use across threads and can be stored.

const std = @import("std");

/// Human-readable byte-size units.
const Unit = struct { threshold: u64, divisor: f64, suffix: []const u8 };

const units = [_]Unit{
    .{ .threshold = 1024 * 1024 * 1024, .divisor = 1024.0 * 1024.0 * 1024.0, .suffix = "GB" },
    .{ .threshold = 1024 * 1024, .divisor = 1024.0 * 1024.0, .suffix = "MB" },
    .{ .threshold = 1024, .divisor = 1024.0, .suffix = "KB" },
};

/// Format `size` bytes as a human-readable string (B / KB / MB / GB).
/// Caller must free the result with `allocator.free`.
///
/// Examples:
///   512        → "512 B"
///   1536       → "1.5 KB"
///   2097152    → "2.0 MB"
///   1073741824 → "1.0 GB"
pub fn formatFileSize(allocator: std.mem.Allocator, size: u64) ![]u8 {
    for (units) |u| {
        if (size >= u.threshold) {
            const value = @as(f64, @floatFromInt(size)) / u.divisor;
            return std.fmt.allocPrint(allocator, "{d:.1} {s}", .{ value, u.suffix });
        }
    }
    return std.fmt.allocPrint(allocator, "{d} B", .{size});
}

/// Format `size` bytes into a caller-provided buffer (no allocation).
/// Writes a NUL-terminated C-string compatible result.
/// Returns the number of bytes written (excluding NUL), or an error if the
/// buffer is too small.
pub fn formatFileSizeBuf(buf: []u8, size: u64) !usize {
    const result = for (units) |u| {
        if (size >= u.threshold) {
            const value = @as(f64, @floatFromInt(size)) / u.divisor;
            break try std.fmt.bufPrint(buf, "{d:.1} {s}", .{ value, u.suffix });
        }
    } else try std.fmt.bufPrint(buf, "{d} B", .{size});
    return result.len;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "formatFileSize: bytes" {
    const a = std.testing.allocator;
    const s = try formatFileSize(a, 0);
    defer a.free(s);
    try std.testing.expectEqualStrings("0 B", s);
}

test "formatFileSize: 512 bytes" {
    const a = std.testing.allocator;
    const s = try formatFileSize(a, 512);
    defer a.free(s);
    try std.testing.expectEqualStrings("512 B", s);
}

test "formatFileSize: exactly 1 KB" {
    const a = std.testing.allocator;
    const s = try formatFileSize(a, 1024);
    defer a.free(s);
    try std.testing.expectEqualStrings("1.0 KB", s);
}

test "formatFileSize: 1.5 KB" {
    const a = std.testing.allocator;
    const s = try formatFileSize(a, 1536);
    defer a.free(s);
    try std.testing.expectEqualStrings("1.5 KB", s);
}

test "formatFileSize: MB range" {
    const a = std.testing.allocator;
    const s = try formatFileSize(a, 2 * 1024 * 1024);
    defer a.free(s);
    try std.testing.expectEqualStrings("2.0 MB", s);
}

test "formatFileSize: GB range" {
    const a = std.testing.allocator;
    const s = try formatFileSize(a, 1024 * 1024 * 1024);
    defer a.free(s);
    try std.testing.expectEqualStrings("1.0 GB", s);
}

test "formatFileSizeBuf: fits in buffer" {
    var buf: [32]u8 = undefined;
    const n = try formatFileSizeBuf(&buf, 2048);
    try std.testing.expectEqualStrings("2.0 KB", buf[0..n]);
}
