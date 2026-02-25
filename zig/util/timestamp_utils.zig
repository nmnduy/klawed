//! Timestamp Utilities
//!
//! Idiomatic Zig replacements for src/util/timestamp_utils.c
//!
//! Key C→Zig translations:
//!   - `get_current_timestamp` (snprintf into caller buffer) → `currentTimestamp` (allocPrint)
//!   - `get_current_date`      (malloc)                      → `currentDate` (allocPrint)
//!   - `generate_session_id`   (malloc + arc4random)         → `generateSessionId` (allocPrint + std.crypto.random)
//!   - `generate_timestamped_filename` (snprintf)            → `timestampedFilename` (allocPrint)

const std = @import("std");

/// Return the current local time as a `std.time.epoch.EpochSeconds`-compatible
/// struct broken out into calendar fields.
///
/// We use the POSIX C localtime_r internally because Zig 0.12.x std.time does
/// not yet expose timezone-aware calendar decomposition.
fn localtime() struct {
    year: u16,
    month: u8,
    day: u8,
    hour: u8,
    minute: u8,
    second: u8,
} {
    const c = @cImport({
        @cInclude("time.h");
    });
    var ts: c.time_t = c.time(null);
    var tm_buf: c.struct_tm = undefined;
    _ = c.localtime_r(&ts, &tm_buf);
    return .{
        .year = @intCast(tm_buf.tm_year + 1900),
        .month = @intCast(tm_buf.tm_mon + 1),
        .day = @intCast(tm_buf.tm_mday),
        .hour = @intCast(tm_buf.tm_hour),
        .minute = @intCast(tm_buf.tm_min),
        .second = @intCast(tm_buf.tm_sec),
    };
}

/// Return the current local time as "YYYY-MM-DD HH:MM:SS".
/// Caller must free the result with `allocator.free`.
pub fn currentTimestamp(allocator: std.mem.Allocator) ![]u8 {
    const t = localtime();
    return std.fmt.allocPrint(allocator, "{d:0>4}-{d:0>2}-{d:0>2} {d:0>2}:{d:0>2}:{d:0>2}", .{
        t.year, t.month, t.day, t.hour, t.minute, t.second,
    });
}

/// Return the current local date as "YYYY-MM-DD".
/// Caller must free the result with `allocator.free`.
pub fn currentDate(allocator: std.mem.Allocator) ![]u8 {
    const t = localtime();
    return std.fmt.allocPrint(allocator, "{d:0>4}-{d:0>2}-{d:0>2}", .{
        t.year, t.month, t.day,
    });
}

/// Return a cryptographically-random session ID in the form
/// `sess_<unix_seconds>_<8-hex-digits>`.
/// Caller must free the result with `allocator.free`.
pub fn generateSessionId(allocator: std.mem.Allocator) ![]u8 {
    const unix_secs = std.time.timestamp();
    const rand_part = std.crypto.random.int(u32);
    return std.fmt.allocPrint(allocator, "sess_{d}_{x:0>8}", .{ unix_secs, rand_part });
}

/// Map a MIME type string to a short file extension.
fn mimeToExtension(mime_type: ?[]const u8) []const u8 {
    const m = mime_type orelse return "bin";
    if (std.mem.eql(u8, m, "image/png")) return "png";
    if (std.mem.eql(u8, m, "image/jpeg") or std.mem.eql(u8, m, "image/jpg")) return "jpg";
    if (std.mem.eql(u8, m, "image/gif")) return "gif";
    if (std.mem.eql(u8, m, "image/webp")) return "webp";
    if (std.mem.startsWith(u8, m, "image/")) return "img";
    return "bin";
}

/// Generate a timestamped filename: `<prefix>_YYYYMMDD_HHMMSS.<ext>`.
/// `prefix` defaults to `"file"` when null/empty.
/// `mime_type` is used to pick the extension; defaults to `"bin"`.
/// Caller must free the result with `allocator.free`.
pub fn timestampedFilename(
    allocator: std.mem.Allocator,
    prefix: ?[]const u8,
    mime_type: ?[]const u8,
) ![]u8 {
    const t = localtime();
    const pfx = if (prefix) |p| (if (p.len > 0) p else "file") else "file";
    const ext = mimeToExtension(mime_type);
    return std.fmt.allocPrint(
        allocator,
        "{s}_{d:0>4}{d:0>2}{d:0>2}_{d:0>2}{d:0>2}{d:0>2}.{s}",
        .{ pfx, t.year, t.month, t.day, t.hour, t.minute, t.second, ext },
    );
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "currentTimestamp: format is YYYY-MM-DD HH:MM:SS" {
    const allocator = std.testing.allocator;
    const ts = try currentTimestamp(allocator);
    defer allocator.free(ts);
    // Basic length check: "2024-01-02 03:04:05" = 19 chars
    try std.testing.expectEqual(@as(usize, 19), ts.len);
    try std.testing.expectEqual(@as(u8, '-'), ts[4]);
    try std.testing.expectEqual(@as(u8, '-'), ts[7]);
    try std.testing.expectEqual(@as(u8, ' '), ts[10]);
    try std.testing.expectEqual(@as(u8, ':'), ts[13]);
    try std.testing.expectEqual(@as(u8, ':'), ts[16]);
}

test "currentDate: format is YYYY-MM-DD" {
    const allocator = std.testing.allocator;
    const d = try currentDate(allocator);
    defer allocator.free(d);
    try std.testing.expectEqual(@as(usize, 10), d.len);
    try std.testing.expectEqual(@as(u8, '-'), d[4]);
    try std.testing.expectEqual(@as(u8, '-'), d[7]);
}

test "generateSessionId: starts with sess_ and has correct structure" {
    const allocator = std.testing.allocator;
    const id = try generateSessionId(allocator);
    defer allocator.free(id);
    try std.testing.expect(std.mem.startsWith(u8, id, "sess_"));
    // Must contain exactly two underscores (sess_<ts>_<hex>)
    var underscore_count: usize = 0;
    for (id) |c| {
        if (c == '_') underscore_count += 1;
    }
    try std.testing.expectEqual(@as(usize, 2), underscore_count);
}

test "mimeToExtension: known types" {
    try std.testing.expectEqualStrings("png", mimeToExtension("image/png"));
    try std.testing.expectEqualStrings("jpg", mimeToExtension("image/jpeg"));
    try std.testing.expectEqualStrings("jpg", mimeToExtension("image/jpg"));
    try std.testing.expectEqualStrings("gif", mimeToExtension("image/gif"));
    try std.testing.expectEqualStrings("webp", mimeToExtension("image/webp"));
    try std.testing.expectEqualStrings("img", mimeToExtension("image/avif"));
    try std.testing.expectEqualStrings("bin", mimeToExtension("application/octet-stream"));
    try std.testing.expectEqualStrings("bin", mimeToExtension(null));
}

test "timestampedFilename: default prefix and png extension" {
    const allocator = std.testing.allocator;
    const name = try timestampedFilename(allocator, null, "image/png");
    defer allocator.free(name);
    try std.testing.expect(std.mem.startsWith(u8, name, "file_"));
    try std.testing.expect(std.mem.endsWith(u8, name, ".png"));
}

test "timestampedFilename: custom prefix and bin extension" {
    const allocator = std.testing.allocator;
    const name = try timestampedFilename(allocator, "upload", null);
    defer allocator.free(name);
    try std.testing.expect(std.mem.startsWith(u8, name, "upload_"));
    try std.testing.expect(std.mem.endsWith(u8, name, ".bin"));
}
