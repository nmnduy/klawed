//! context/klawed_md.zig — Read KLAWED.md from the project directory
//!
//! Zig port of src/context/klawed_md.c.
//!
//! Reads the `KLAWED.md` file from the given working directory and returns
//! its contents as an owned string.  Returns `null` if the file does not exist.

const std = @import("std");

/// Read `KLAWED.md` from `working_dir`.
/// Returns an owned string (caller must free), or `null` if the file does not exist.
pub fn readKlawedMd(allocator: std.mem.Allocator, working_dir: []const u8) !?[]u8 {
    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const path = std.fmt.bufPrint(&path_buf, "{s}/KLAWED.md", .{working_dir}) catch
        return null;

    const file = std.fs.openFileAbsolute(path, .{}) catch |err| switch (err) {
        error.FileNotFound => return null,
        else => return err,
    };
    defer file.close();

    const stat = try file.stat();
    const content = try allocator.alloc(u8, stat.size);
    errdefer allocator.free(content);

    const n = try file.readAll(content);
    if (n != stat.size) {
        // Partial read — return what we got
        const trimmed = try allocator.realloc(content, n);
        return trimmed;
    }
    return content;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "readKlawedMd returns null for non-existent directory" {
    const allocator = std.testing.allocator;
    const result = try readKlawedMd(allocator, "/nonexistent_dir_abc123");
    try std.testing.expect(result == null);
}

test "readKlawedMd reads existing KLAWED.md" {
    const allocator = std.testing.allocator;

    // Create a temporary directory with a KLAWED.md
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const content_str = "# KLAWED.md\n\nTest content.\n";
    const file = try tmp.dir.createFile("KLAWED.md", .{});
    defer file.close();
    try file.writeAll(content_str);

    // Get absolute path of the temp dir
    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const result = try readKlawedMd(allocator, tmp_path);
    defer if (result) |r| allocator.free(r);

    try std.testing.expect(result != null);
    try std.testing.expectEqualStrings(content_str, result.?);
}

test "readKlawedMd returns null when no KLAWED.md present" {
    const allocator = std.testing.allocator;

    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const result = try readKlawedMd(allocator, tmp_path);
    try std.testing.expect(result == null);
}
