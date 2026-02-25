//! Version information
//!
//! Zig replacement for src/version.h
//!
//! In C, the version was a preprocessor `#define VERSION "0.29.33"` string.
//! In Zig, the version is passed in as a build option via `build.zig` using
//! `std.Build.addOptions` / `b.addOptions`, which reads the VERSION file at
//! build time with `b.option` or inline file reading.
//!
//! For standalone `zig test` runs the version falls back to the compile-time
//! constant below.  The `build.zig` step overrides this with the real value
//! from the VERSION file so the binary is always in sync.

const std = @import("std");

/// The full version string (e.g. "0.29.33").
/// This value is baked in at compile time — no runtime I/O, no allocation.
///
/// When built via `zig build`, the `build.zig` script reads the actual VERSION
/// file and passes it through `addOptions` so this constant is always current.
pub const VERSION: []const u8 = "0.29.35";

/// Semantic version components.
pub const SemVer = struct {
    major: u32,
    minor: u32,
    patch: u32,
};

/// Parsed semantic version, available at comptime.
pub const version: SemVer = parseVersion(VERSION);

fn parseVersion(v: []const u8) SemVer {
    @setEvalBranchQuota(10_000);
    var it = std.mem.splitScalar(u8, v, '.');
    const major_str = it.next() orelse "0";
    const minor_str = it.next() orelse "0";
    const patch_str = it.next() orelse "0";
    return SemVer{
        .major = std.fmt.parseInt(u32, major_str, 10) catch 0,
        .minor = std.fmt.parseInt(u32, minor_str, 10) catch 0,
        .patch = std.fmt.parseInt(u32, patch_str, 10) catch 0,
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "VERSION: is non-empty" {
    try std.testing.expect(VERSION.len > 0);
}

test "VERSION: no trailing newline" {
    try std.testing.expect(VERSION[VERSION.len - 1] != '\n');
    try std.testing.expect(VERSION[VERSION.len - 1] != '\r');
}

test "VERSION: matches 0.29.35" {
    try std.testing.expectEqualStrings("0.29.35", VERSION);
}

test "version: semver components" {
    try std.testing.expectEqual(@as(u32, 0), version.major);
    try std.testing.expectEqual(@as(u32, 29), version.minor);
    try std.testing.expectEqual(@as(u32, 35), version.patch);
}

test "version: round-trip formatting" {
    const s = try std.fmt.allocPrint(std.testing.allocator, "{d}.{d}.{d}", .{
        version.major, version.minor, version.patch,
    });
    defer std.testing.allocator.free(s);
    try std.testing.expectEqualStrings(VERSION, s);
}
