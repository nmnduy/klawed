//! Banner module
//!
//! Idiomatic Zig replacement for src/vltrn_banner.c and src/vltrn_banner.h
//!
//! Provides startup banner and version display.

const std = @import("std");

// ---------------------------------------------------------------------------
// Banner Content
// ---------------------------------------------------------------------------

pub const BANNER_TEXT =
    \\     ██╗  ██╗██╗      █████╗ ██╗    ██╗███████╗██████╗
    \\     ██║ ██╔╝██║     ██╔══██╗██║    ██║██╔════╝██╔══██╗
    \\     █████╔╝ ██║     ███████║██║ █╗ ██║█████╗  ██║  ██║
    \\     ██╔═██╗ ██║     ██╔══██║██║███╗██║██╔══╝  ██║  ██║
    \\     ██║  ██╗███████╗██║  ██║╚███╔███╔╝███████╗██████╔╝
    \\     ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝ ╚══╝╚══╝ ╚══════╝╚═════╝
;

pub const MASCOT =
    \\       /\_/\
    \\      ( o.o )
    \\       > ^ <
;

// ---------------------------------------------------------------------------
// Banner Functions
// ---------------------------------------------------------------------------

/// Get the full banner with version info
pub fn getFullBanner(allocator: std.mem.Allocator, version: []const u8, model: []const u8, working_dir: []const u8) ![]const u8 {
    return std.fmt.allocPrint(allocator, "{s}\n    Version: {s}\n    Model: {s}\n    Working Directory: {s}\n", .{
        BANNER_TEXT,
        version,
        model,
        working_dir,
    });
}

/// Get compact banner (just mascot)
pub fn getCompactBanner() []const u8 {
    return MASCOT;
}

/// Get simple text banner
pub fn getSimpleBanner(version: []const u8) []const u8 {
    // Return a const string, caller should not free
    _ = version;
    return "klawed - AI Coding Assistant";
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "Banner: content exists" {
    try std.testing.expect(BANNER_TEXT.len > 0);
    try std.testing.expect(MASCOT.len > 0);
}

test "getFullBanner: generates banner with info" {
    const banner = try getFullBanner(std.testing.allocator, "1.0.0", "gpt-4", "/home/user");
    defer std.testing.allocator.free(banner);

    try std.testing.expect(std.mem.indexOf(u8, banner, "1.0.0") != null);
    try std.testing.expect(std.mem.indexOf(u8, banner, "gpt-4") != null);
    try std.testing.expect(std.mem.indexOf(u8, banner, "/home/user") != null);
}

test "getCompactBanner: returns mascot" {
    const banner = getCompactBanner();
    try std.testing.expect(std.mem.indexOf(u8, banner, "/\\_/\\") != null);
}
