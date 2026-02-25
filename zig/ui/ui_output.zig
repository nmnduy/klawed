//! UI Output module
//!
//! Idiomatic Zig replacement for src/ui/ui_output.c and src/ui/ui_output.h
//!
//! Provides high-level UI output functions.

const std = @import("std");
const logger = @import("../logger.zig");

/// Output level for UI messages
pub const OutputLevel = enum {
    debug,
    info,
    success,
    warning,
    err,
};

/// Output options
pub const OutputOptions = struct {
    timestamp: bool = false,
    prefix: ?[]const u8 = null,
};

/// Print a UI message with appropriate formatting
pub fn printMessage(
    writer: anytype,
    level: OutputLevel,
    message: []const u8,
    options: OutputOptions,
) !void {
    // Print timestamp if requested
    if (options.timestamp) {
        const now = std.time.timestamp();
        const secs = @divFloor(now, 1);
        const hours = @mod(@divFloor(secs, 3600), 24);
        const mins = @mod(@divFloor(secs, 60), 60);
        const secs_only = @mod(secs, 60);

        try writer.print("[{d:0>2}:{d:0>2}:{d:0>2}] ", .{ hours, mins, secs_only });
    }

    // Print prefix if provided
    if (options.prefix) |prefix| {
        try writer.writeAll(prefix);
        try writer.writeAll(" ");
    }

    // Print level indicator
    switch (level) {
        .debug => try writer.writeAll("[DEBUG] "),
        .info => {},
        .success => try writer.writeAll("\x1b[32m[✓]\x1b[0m "),
        .warning => try writer.writeAll("\x1b[33m[!]\x1b[0m "),
        .err => try writer.writeAll("\x1b[31m[✗]\x1b[0m "),
    }

    // Print message
    try writer.writeAll(message);
    try writer.writeByte('\n');
}

/// Print a success message
pub fn printSuccess(writer: anytype, message: []const u8) !void {
    try printMessage(writer, .success, message, .{});
}

/// Print an error message
pub fn printError(writer: anytype, message: []const u8) !void {
    try printMessage(writer, .err, message, .{});
}

/// Print a warning message
pub fn printWarning(writer: anytype, message: []const u8) !void {
    try printMessage(writer, .warning, message, .{});
}

/// Print an info message
pub fn printInfo(writer: anytype, message: []const u8) !void {
    try printMessage(writer, .info, message, .{});
}

/// Print a debug message
pub fn printDebug(writer: anytype, message: []const u8) !void {
    try printMessage(writer, .debug, message, .{});
}

/// Print a section header
pub fn printSectionHeader(writer: anytype, title: []const u8) !void {
    try writer.writeAll("\n┌─ ");
    try writer.writeAll(title);
    try writer.writeAll(" ─\n");
}

/// Print a section footer
pub fn printSectionFooter(writer: anytype) !void {
    try writer.writeAll("└─────────────────────\n\n");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "printMessage: all levels" {
    var buf: [256]u8 = undefined;

    // Test success
    {
        var fbs = std.io.fixedBufferStream(&buf);
        try printSuccess(fbs.writer(), "Operation completed");
        try std.testing.expect(std.mem.indexOf(u8, fbs.getWritten(), "✓") != null);
    }

    // Test error
    {
        var fbs = std.io.fixedBufferStream(&buf);
        try printError(fbs.writer(), "Something failed");
        try std.testing.expect(std.mem.indexOf(u8, fbs.getWritten(), "✗") != null);
    }

    // Test warning
    {
        var fbs = std.io.fixedBufferStream(&buf);
        try printWarning(fbs.writer(), "Be careful");
        try std.testing.expect(std.mem.indexOf(u8, fbs.getWritten(), "!") != null);
    }
}

test "printSectionHeader: formats correctly" {
    var buf: [100]u8 = undefined;
    var fbs = std.io.fixedBufferStream(&buf);

    try printSectionHeader(fbs.writer(), "Test Section");

    const result = fbs.getWritten();
    try std.testing.expect(std.mem.startsWith(u8, result, "\n┌─ "));
    try std.testing.expect(std.mem.indexOf(u8, result, "Test Section") != null);
}
