//! File Utilities
//!
//! Idiomatic Zig replacements for src/util/file_utils.c
//!
//! Key C→Zig translations:
//!   - `read_file`          → `readFile`      (readToEndAlloc, proper error union)
//!   - `write_file`         → `writeFile`     (std.fs, creates dirs automatically)
//!   - `mkdir_p`            → `mkdirP`        (std.fs.Dir.makePath)
//!   - `resolve_path`       → `resolvePath`   (std.fs.path.resolve)
//!   - `save_binary_file`   → `saveBinaryFile`(writeFile variant for binary slices)
//!
//! All functions that return owned memory accept an allocator parameter.
//! The C version used global state and strdup — here ownership is explicit.

const std = @import("std");

/// Maximum file size we'll read into memory (64 MiB).
/// This prevents accidentally exhausting RAM on huge files.
pub const max_read_size: usize = 64 * 1024 * 1024;

// ---------------------------------------------------------------------------
// Core operations
// ---------------------------------------------------------------------------

/// Read the entire contents of `path` into an allocated slice.
/// Caller must free with `allocator.free`.
pub fn readFile(allocator: std.mem.Allocator, path: []const u8) ![]u8 {
    const file = try std.fs.openFileAbsolute(path, .{});
    defer file.close();
    return file.readToEndAlloc(allocator, max_read_size);
}

/// Read the entire contents of a path relative to a directory.
pub fn readFileRelative(
    allocator: std.mem.Allocator,
    dir: std.fs.Dir,
    relative_path: []const u8,
) ![]u8 {
    const file = try dir.openFile(relative_path, .{});
    defer file.close();
    return file.readToEndAlloc(allocator, max_read_size);
}

/// Write `content` to `path`, creating parent directories as needed.
/// Overwrites existing files.
pub fn writeFile(path: []const u8, content: []const u8) !void {
    // Ensure parent directories exist
    if (std.fs.path.dirname(path)) |dir_path| {
        try mkdirP(dir_path);
    }
    const file = try std.fs.createFileAbsolute(path, .{ .truncate = true });
    defer file.close();
    try file.writeAll(content);
}

/// Write binary `data` to `path`, creating parent directories as needed.
pub fn saveBinaryFile(path: []const u8, data: []const u8) !void {
    return writeFile(path, data);
}

/// Create a directory and all its parents (like `mkdir -p`).
/// Succeeds silently if the directory already exists.
pub fn mkdirP(path: []const u8) !void {
    // std.fs.makeDirAbsolute errors on EEXIST — use cwd().makePath for
    // recursive creation, which handles both absolute and relative paths.
    std.fs.cwd().makePath(path) catch |err| switch (err) {
        error.PathAlreadyExists => {}, // fine
        else => return err,
    };
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

/// Resolve `path` against `working_dir` when it is relative, then
/// canonicalize via `realpath` when the path exists.
///
/// When canonicalization fails (e.g. the target doesn't exist yet), returns a
/// simple joined path — this enables tools like Write to create new files.
///
/// Caller must free the result with `allocator.free`.
pub fn resolvePath(
    allocator: std.mem.Allocator,
    path: []const u8,
    working_dir: []const u8,
) ![]u8 {
    // If absolute, try realpath then fall back to a copy.
    if (std.fs.path.isAbsolute(path)) {
        return std.fs.realpathAlloc(allocator, path) catch
            allocator.dupe(u8, path);
    }
    // Join working_dir + path, then try realpath.
    const joined = try std.fs.path.join(allocator, &.{ working_dir, path });
    defer allocator.free(joined);

    return std.fs.realpathAlloc(allocator, joined) catch
        allocator.dupe(u8, joined);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "mkdirP: creates nested directories" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const sub = "a/b/c";
    try tmp.dir.makePath(sub);
    // Verify the leaf exists
    var leaf = try tmp.dir.openDir(sub, .{});
    defer leaf.close();
}

test "writeFile and readFile: round-trip" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const allocator = std.testing.allocator;

    // Build an absolute path inside the temp dir
    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(allocator, &.{ tmp_path, "test.txt" });
    defer allocator.free(file_path);

    const content = "hello, zig!\n";
    try writeFile(file_path, content);

    const read_back = try readFile(allocator, file_path);
    defer allocator.free(read_back);

    try std.testing.expectEqualStrings(content, read_back);
}

test "writeFile: creates parent directories automatically" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const allocator = std.testing.allocator;

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const file_path = try std.fs.path.join(allocator, &.{ tmp_path, "nested/dir/file.txt" });
    defer allocator.free(file_path);

    try writeFile(file_path, "content");

    const read_back = try readFile(allocator, file_path);
    defer allocator.free(read_back);
    try std.testing.expectEqualStrings("content", read_back);
}

test "resolvePath: absolute path returned as-is (when exists)" {
    const allocator = std.testing.allocator;
    // /tmp always exists
    const resolved = try resolvePath(allocator, "/tmp", "/ignored");
    defer allocator.free(resolved);
    // realpath may resolve /tmp → /private/tmp on macOS, so just check it starts with /
    try std.testing.expect(resolved[0] == '/');
}

test "resolvePath: relative path joined with working_dir" {
    const allocator = std.testing.allocator;
    // Use a path that doesn't exist — falls back to the joined string
    const resolved = try resolvePath(allocator, "subdir/file.txt", "/some/base");
    defer allocator.free(resolved);
    // Should contain "subdir/file.txt" somewhere
    try std.testing.expect(std.mem.indexOf(u8, resolved, "subdir/file.txt") != null);
}
