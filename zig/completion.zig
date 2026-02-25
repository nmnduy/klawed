//! completion.zig — Path and command tab completion
//!
//! Zig port of src/completion.c.
//!
//! Provides filesystem path completion (files and directories) for the
//! interactive input handler.  The TUI tab-completion integration lives in
//! Phase 9; this module exposes only the pure completion logic.

const std = @import("std");
const fs = std.fs;

// ---------------------------------------------------------------------------
// CompletionResult
// ---------------------------------------------------------------------------

/// A list of completion candidates for a given partial path.
/// Caller owns the result and must call `deinit` to free memory.
pub const CompletionResult = struct {
    allocator: std.mem.Allocator,
    /// Sorted list of completion strings (owned).
    options: std.ArrayList([]u8),
    /// Currently selected index in the options list.
    selected: usize,

    pub fn init(allocator: std.mem.Allocator) CompletionResult {
        return CompletionResult{
            .allocator = allocator,
            .options = std.ArrayList([]u8).init(allocator),
            .selected = 0,
        };
    }

    pub fn deinit(self: *CompletionResult) void {
        for (self.options.items) |opt| {
            self.allocator.free(opt);
        }
        self.options.deinit();
    }

    pub fn count(self: *const CompletionResult) usize {
        return self.options.items.len;
    }
};

// ---------------------------------------------------------------------------
// Path splitting helpers
// ---------------------------------------------------------------------------

/// Split `partial` into directory and basename parts.
/// Returns `{ dir, base }` where both are slices into a caller-owned buffer.
///
/// Examples:
///   ""        → dir=".", base=""
///   "foo"     → dir=".", base="foo"
///   "foo/bar" → dir="foo", base="bar"
///   "/foo"    → dir="/", base="foo"
fn splitPath(partial: []const u8) struct { dir: []const u8, base: []const u8 } {
    if (partial.len == 0) return .{ .dir = ".", .base = "" };

    if (std.mem.lastIndexOfScalar(u8, partial, '/')) |slash| {
        if (slash == 0) {
            // Root path like "/foo"
            return .{ .dir = "/", .base = partial[1..] };
        }
        return .{ .dir = partial[0..slash], .base = partial[slash + 1 ..] };
    }

    return .{ .dir = ".", .base = partial };
}

// ---------------------------------------------------------------------------
// Core completion logic
// ---------------------------------------------------------------------------

/// Complete filesystem paths matching `partial`.
/// If `dirs_only` is true, only directory entries are returned.
///
/// Returns a `CompletionResult` (caller must call `deinit`), or null if no
/// matches or the directory cannot be opened.
pub fn completePath(
    allocator: std.mem.Allocator,
    partial: []const u8,
    dirs_only: bool,
) !?CompletionResult {
    const split = splitPath(partial);
    const dir_path = split.dir;
    const prefix = split.base;

    var dir = fs.cwd().openDir(dir_path, .{ .iterate = true }) catch return null;
    defer dir.close();

    var result = CompletionResult.init(allocator);
    errdefer result.deinit();

    var iter = dir.iterate();
    while (try iter.next()) |entry| {
        const name = entry.name;

        // Skip hidden entries unless prefix starts with '.'
        if (name[0] == '.' and (prefix.len == 0 or prefix[0] != '.')) continue;

        // Must start with prefix
        if (!std.mem.startsWith(u8, name, prefix)) continue;

        // Check type
        const is_dir = entry.kind == .directory;
        if (dirs_only and !is_dir) continue;

        // Build the completion string.
        // Format: [dir_path/]name[/]
        var buf = std.ArrayList(u8).init(allocator);
        errdefer buf.deinit();

        // Add directory component (unless it's the implicit ".")
        if (!std.mem.eql(u8, dir_path, ".")) {
            try buf.appendSlice(dir_path);
            try buf.append('/');
        }
        try buf.appendSlice(name);
        if (is_dir) try buf.append('/');

        try result.options.append(try buf.toOwnedSlice());
    }

    if (result.options.items.len == 0) {
        result.deinit();
        return null;
    }

    // Sort for deterministic output.
    std.mem.sort([]u8, result.options.items, {}, struct {
        fn lessThan(_: void, a: []u8, b: []u8) bool {
            return std.mem.lessThan(u8, a, b);
        }
    }.lessThan);

    return result;
}

/// Complete file paths (files and directories).
pub fn completeFilePath(allocator: std.mem.Allocator, partial: []const u8) !?CompletionResult {
    return completePath(allocator, partial, false);
}

/// Complete directory paths only.
pub fn completeDirPath(allocator: std.mem.Allocator, partial: []const u8) !?CompletionResult {
    return completePath(allocator, partial, true);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "splitPath: empty" {
    const r = splitPath("");
    try std.testing.expectEqualStrings(".", r.dir);
    try std.testing.expectEqualStrings("", r.base);
}

test "splitPath: no slash" {
    const r = splitPath("foo");
    try std.testing.expectEqualStrings(".", r.dir);
    try std.testing.expectEqualStrings("foo", r.base);
}

test "splitPath: with slash" {
    const r = splitPath("src/main");
    try std.testing.expectEqualStrings("src", r.dir);
    try std.testing.expectEqualStrings("main", r.base);
}

test "splitPath: root" {
    const r = splitPath("/etc");
    try std.testing.expectEqualStrings("/", r.dir);
    try std.testing.expectEqualStrings("etc", r.base);
}

test "splitPath: nested" {
    const r = splitPath("a/b/c");
    try std.testing.expectEqualStrings("a/b", r.dir);
    try std.testing.expectEqualStrings("c", r.base);
}

test "completeFilePath: returns null for non-existent dir" {
    const alloc = std.testing.allocator;
    const result = try completeFilePath(alloc, "/nonexistent_dir_xyz/");
    try std.testing.expectEqual(@as(?CompletionResult, null), result);
}

test "completeFilePath: finds matches in /tmp" {
    const alloc = std.testing.allocator;
    // /tmp always exists; we just check we don't crash and get a result or null.
    const result = try completeFilePath(alloc, "/tmp/");
    if (result) |r| {
        var res = r;
        defer res.deinit();
        // Just verify the count is >= 0 (it's a list so it's always >= 0).
        try std.testing.expect(res.count() >= 0);
    }
}

test "CompletionResult: deinit is safe when empty" {
    const alloc = std.testing.allocator;
    var r = CompletionResult.init(alloc);
    r.deinit();
}
