//! history_file.zig — Flat-file input history (one entry per line)
//!
//! Zig port of src/history_file.c.
//!
//! Each entry is stored on a single physical line with `\n` characters
//! escaped as the two-character sequence `\n` (backslash + 'n').
//! This lets multi-line editor inputs survive a round-trip through the file.

const std = @import("std");
const data_dir = @import("data_dir.zig");

/// Escape `\n` → `\n` (two chars) for safe single-line storage.
/// Returns a newly-allocated string; caller must free.
pub fn escapeNewlines(allocator: std.mem.Allocator, text: []const u8) ![]u8 {
    // Count newlines to pre-size the buffer.
    var count: usize = 0;
    for (text) |ch| if (ch == '\n') { count += 1; };

    if (count == 0) return allocator.dupe(u8, text);

    var buf = try allocator.alloc(u8, text.len + count);
    var out: usize = 0;
    for (text) |ch| {
        if (ch == '\n') {
            buf[out] = '\\';
            out += 1;
            buf[out] = 'n';
        } else {
            buf[out] = ch;
        }
        out += 1;
    }
    return buf[0..out];
}

/// Unescape `\n` (two chars) → `\n` when loading from storage.
/// Returns a newly-allocated string; caller must free.
pub fn unescapeNewlines(allocator: std.mem.Allocator, text: []const u8) ![]u8 {
    // Count `\n` sequences.
    var count: usize = 0;
    var i: usize = 0;
    while (i < text.len) : (i += 1) {
        if (text[i] == '\\' and i + 1 < text.len and text[i + 1] == 'n') {
            count += 1;
            i += 1; // skip 'n'
        }
    }

    if (count == 0) return allocator.dupe(u8, text);

    var buf = try allocator.alloc(u8, text.len - count);
    var out: usize = 0;
    i = 0;
    while (i < text.len) : (i += 1) {
        if (text[i] == '\\' and i + 1 < text.len and text[i + 1] == 'n') {
            buf[out] = '\n';
            out += 1;
            i += 1;
        } else {
            buf[out] = text[i];
            out += 1;
        }
    }
    return buf[0..out];
}

/// Resolve the default history file path.
///
/// Priority: `$KLAWED_HISTORY_FILE_PATH` → data_dir/input_history.txt
/// → `$XDG_DATA_HOME/klawed/input_history.txt`
/// → `$HOME/.local/share/klawed/input_history.txt`
/// → `./input_history.txt`
///
/// Returns a caller-owned string.
pub fn defaultPath(allocator: std.mem.Allocator) ![]const u8 {
    // 1. Explicit env override.
    if (std.process.getEnvVarOwned(allocator, "KLAWED_HISTORY_FILE_PATH")) |p| {
        if (p.len > 0) return p;
        allocator.free(p);
    } else |_| {}

    // 2. Project-local data directory.
    if (data_dir.buildPath(allocator, "input_history.txt")) |p| {
        return p;
    } else |_| {}

    // 3. XDG_DATA_HOME.
    if (std.process.getEnvVarOwned(allocator, "XDG_DATA_HOME")) |xdg| {
        defer allocator.free(xdg);
        if (xdg.len > 0) {
            return std.fs.path.join(allocator, &.{ xdg, "klawed", "input_history.txt" });
        }
    } else |_| {}

    // 4. HOME fallback.
    if (std.process.getEnvVarOwned(allocator, "HOME")) |home| {
        defer allocator.free(home);
        if (home.len > 0) {
            return std.fs.path.join(
                allocator,
                &.{ home, ".local", "share", "klawed", "input_history.txt" },
            );
        }
    } else |_| {}

    // 5. CWD fallback.
    return allocator.dupe(u8, "./input_history.txt");
}

/// An open history file that supports appending and loading recent entries.
pub const HistoryFile = struct {
    path: []const u8, // owned
    file: std.fs.File,
    allocator: std.mem.Allocator,

    /// Open (or create) the history file at `path`.  If `path` is empty, the
    /// default path is used.  The file is opened in append mode.
    pub fn open(allocator: std.mem.Allocator, path: ?[]const u8) !HistoryFile {
        const resolved_path = if (path != null and path.?.len > 0)
            try allocator.dupe(u8, path.?)
        else
            try defaultPath(allocator);
        errdefer allocator.free(resolved_path);

        // Ensure parent directory exists.
        if (std.fs.path.dirname(resolved_path)) |dir| {
            std.fs.cwd().makePath(dir) catch {};
        }

        const file = try std.fs.cwd().createFile(resolved_path, .{
            .read = false,
            .truncate = false,
        });
        // Seek to end for append semantics.
        try file.seekFromEnd(0);

        return HistoryFile{
            .path = resolved_path,
            .file = file,
            .allocator = allocator,
        };
    }

    /// Close the history file and free resources.
    pub fn close(self: *HistoryFile) void {
        self.file.close();
        self.allocator.free(self.path);
        self.* = undefined;
    }

    /// Append a single entry.  Empty strings are silently ignored.
    pub fn append(self: *HistoryFile, text: []const u8) !void {
        if (text.len == 0) return;

        const escaped = try escapeNewlines(self.allocator, text);
        defer self.allocator.free(escaped);

        var bw = std.io.bufferedWriter(self.file.writer());
        try bw.writer().writeAll(escaped);
        try bw.writer().writeByte('\n');
        try bw.flush();
    }

    /// Load the `limit` most-recent entries.
    ///
    /// Returns a slice of caller-owned strings; use `freeLoaded` to release.
    /// Returns an empty slice when the file is empty or unreadable.
    pub fn loadRecent(
        self: *const HistoryFile,
        limit: usize,
    ) ![][]const u8 {
        if (limit == 0) return &[_][]const u8{};

        // Read the entire file.
        const file = std.fs.cwd().openFile(self.path, .{}) catch
            return &[_][]const u8{};
        defer file.close();

        const content = try file.readToEndAlloc(self.allocator, 64 * 1024 * 1024);
        defer self.allocator.free(content);

        // Split into lines.
        var lines = std.ArrayList([]const u8).init(self.allocator);
        defer {
            for (lines.items) |l| self.allocator.free(l);
            lines.deinit();
        }

        var it = std.mem.splitScalar(u8, content, '\n');
        while (it.next()) |raw_line| {
            // Trim trailing \r.
            var line = raw_line;
            if (line.len > 0 and line[line.len - 1] == '\r') {
                line = line[0 .. line.len - 1];
            }
            if (line.len == 0) continue;

            const unescaped = try unescapeNewlines(self.allocator, line);
            try lines.append(unescaped);
        }

        // Return only the last `limit` entries.
        const total = lines.items.len;
        const start = if (total > limit) total - limit else 0;
        const slice = lines.items[start..];

        var result = try self.allocator.alloc([]const u8, slice.len);
        for (slice, 0..) |l, i| {
            result[i] = l;
            // Ownership transferred — prevent the defer above from freeing.
            lines.items[start + i] = &[_]u8{};
        }
        return result;
    }

    /// Free the slice returned by `loadRecent`.
    pub fn freeLoaded(self: *const HistoryFile, entries: [][]const u8) void {
        for (entries) |e| self.allocator.free(e);
        self.allocator.free(entries);
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "escape and unescape round-trip" {
    const allocator = std.testing.allocator;

    const original = "line one\nline two\nline three";
    const escaped = try escapeNewlines(allocator, original);
    defer allocator.free(escaped);

    // Escaped should not contain literal newlines.
    try std.testing.expect(std.mem.indexOf(u8, escaped, "\n") == null);

    const restored = try unescapeNewlines(allocator, escaped);
    defer allocator.free(restored);
    try std.testing.expectEqualStrings(original, restored);
}

test "escape no-op for strings without newlines" {
    const allocator = std.testing.allocator;
    const s = "hello world";
    const escaped = try escapeNewlines(allocator, s);
    defer allocator.free(escaped);
    try std.testing.expectEqualStrings(s, escaped);
}

test "unescape no-op for strings without escape sequences" {
    const allocator = std.testing.allocator;
    const s = "hello world";
    const result = try unescapeNewlines(allocator, s);
    defer allocator.free(result);
    try std.testing.expectEqualStrings(s, result);
}

test "HistoryFile append and loadRecent" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const allocator = std.testing.allocator;

    // Build an absolute path inside the tmp dir.
    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const hist_path = try tmp.dir.realpath(".", &path_buf);
    const full_path = try std.fs.path.join(allocator, &.{ hist_path, "history.txt" });
    defer allocator.free(full_path);

    var hf = try HistoryFile.open(allocator, full_path);
    defer hf.close();

    try hf.append("first entry");
    try hf.append("second entry");
    try hf.append("multi\nline\nentry");

    const entries = try hf.loadRecent(10);
    defer hf.freeLoaded(entries);

    try std.testing.expectEqual(@as(usize, 3), entries.len);
    try std.testing.expectEqualStrings("first entry", entries[0]);
    try std.testing.expectEqualStrings("second entry", entries[1]);
    try std.testing.expectEqualStrings("multi\nline\nentry", entries[2]);
}

test "HistoryFile loadRecent respects limit" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const allocator = std.testing.allocator;

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir_path = try tmp.dir.realpath(".", &path_buf);
    const full_path = try std.fs.path.join(allocator, &.{ dir_path, "history2.txt" });
    defer allocator.free(full_path);

    var hf = try HistoryFile.open(allocator, full_path);
    defer hf.close();

    var i: usize = 0;
    while (i < 5) : (i += 1) {
        const line = try std.fmt.allocPrint(allocator, "entry {d}", .{i});
        defer allocator.free(line);
        try hf.append(line);
    }

    const entries = try hf.loadRecent(3);
    defer hf.freeLoaded(entries);

    try std.testing.expectEqual(@as(usize, 3), entries.len);
    try std.testing.expectEqualStrings("entry 2", entries[0]);
    try std.testing.expectEqualStrings("entry 3", entries[1]);
    try std.testing.expectEqualStrings("entry 4", entries[2]);
}
