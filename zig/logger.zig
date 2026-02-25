//! Logger — Thread-safe file logging
//!
//! Idiomatic Zig replacement for src/logger.c
//!
//! Key C→Zig translations:
//!   - `pthread_mutex_t`  → `std.Thread.Mutex`
//!   - `va_list` varargs  → comptime `fmt` + `anytype args` (std.fmt.format)
//!   - `rotate_log`       → `Logger.rotate` (same logic, explicit ownership)
//!   - Global state       → `Logger` struct (explicit, thread-safe via Mutex)
//!   - Macros (LOG_INFO)  → `logger.log(.info, ...)` (no macros needed)
//!
//! ## Usage
//!
//! ```zig
//! var logger = try Logger.init(std.heap.page_allocator, null); // null = auto-detect path
//! defer logger.deinit();
//! logger.log(.info, "started PID={d}", .{pid});
//! logger.setLevel(.warn); // suppress debug/info
//! ```
//!
//! The default log path follows the same priority as the C implementation:
//!   1. `$KLAWED_LOG_PATH`
//!   2. `$KLAWED_LOG_DIR/klawed.log`
//!   3. `./.klawed/logs/klawed.log`
//!   4. `~/.local/share/klawed/logs/klawed.log`
//!   5. `/tmp/klawed.log`

const std = @import("std");

// ---------------------------------------------------------------------------
// Log level
// ---------------------------------------------------------------------------

pub const Level = enum(u8) {
    fine = 0,
    debug = 1,
    info = 2,
    warn = 3,
    err = 4,

    pub fn name(self: Level) []const u8 {
        return switch (self) {
            .fine => "FINE ",
            .debug => "DEBUG",
            .info => "INFO ",
            .warn => "WARN ",
            .err => "ERROR",
        };
    }
};

// ---------------------------------------------------------------------------
// Logger struct
// ---------------------------------------------------------------------------

pub const Logger = struct {
    allocator: std.mem.Allocator,
    file: ?std.fs.File,
    path: []u8,
    min_level: Level,
    mutex: std.Thread.Mutex,
    max_size_bytes: u64,
    max_backups: u32,
    session_id: [64]u8,
    session_id_len: usize,
    always_flush: bool,

    /// Open (or create) a log file.
    /// Pass `null` for `log_path` to auto-detect using environment variables
    /// and standard fallback directories.
    pub fn init(allocator: std.mem.Allocator, log_path: ?[]const u8) !Logger {
        const path = if (log_path) |p|
            try allocator.dupe(u8, p)
        else
            try detectLogPath(allocator);

        // Ensure parent directory exists
        if (std.fs.path.dirname(path)) |dir| {
            std.fs.cwd().makePath(dir) catch {}; // best effort
        }

        const file = std.fs.createFileAbsolute(path, .{ .truncate = false }) catch |err| {
            std.debug.print("logger: failed to open {s}: {}\n", .{ path, err });
            return err;
        };
        // Seek to end for append behaviour
        try file.seekFromEnd(0);

        return Logger{
            .allocator = allocator,
            .file = file,
            .path = path,
            .min_level = .info,
            .mutex = .{},
            .max_size_bytes = 10 * 1024 * 1024, // 10 MiB
            .max_backups = 5,
            .session_id = undefined,
            .session_id_len = 0,
            .always_flush = false,
        };
    }

    /// Close the log file and free internal memory.
    pub fn deinit(self: *Logger) void {
        if (self.file) |f| {
            self.writeMarker("Log ended") catch {};
            f.close();
            self.file = null;
        }
        self.allocator.free(self.path);
    }

    /// Set the minimum log level.  Messages below this level are ignored.
    pub fn setLevel(self: *Logger, level: Level) void {
        self.mutex.lock();
        defer self.mutex.unlock();
        self.min_level = level;
    }

    /// Set the session ID included in every log line.
    pub fn setSessionId(self: *Logger, id: []const u8) void {
        self.mutex.lock();
        defer self.mutex.unlock();
        const n = @min(id.len, self.session_id.len - 1);
        @memcpy(self.session_id[0..n], id[0..n]);
        self.session_id[n] = 0;
        self.session_id_len = n;
    }

    /// Configure log rotation.
    pub fn setRotation(self: *Logger, max_size_mb: u32, max_backups: u32) void {
        self.mutex.lock();
        defer self.mutex.unlock();
        self.max_size_bytes = @as(u64, max_size_mb) * 1024 * 1024;
        self.max_backups = max_backups;
    }

    /// Set whether to flush after every write.
    pub fn setAlwaysFlush(self: *Logger, always: bool) void {
        self.mutex.lock();
        defer self.mutex.unlock();
        self.always_flush = always;
    }

    /// Log a message at `level` using `std.fmt` formatting.
    pub fn log(
        self: *Logger,
        level: Level,
        comptime fmt: []const u8,
        args: anytype,
    ) void {
        if (@intFromEnum(level) < @intFromEnum(self.min_level)) return;

        self.mutex.lock();
        defer self.mutex.unlock();

        const f = self.file orelse return;

        self.maybeRotate() catch {};

        const writer = f.writer();

        // Timestamp
        const ts = timestampNow();
        writer.print("[{s}] ", .{&ts}) catch return;

        // Optional session ID
        if (self.session_id_len > 0) {
            writer.print("[{s}] ", .{self.session_id[0..self.session_id_len]}) catch return;
        }

        // Level + message
        writer.print("{s} ", .{level.name()}) catch return;
        writer.print(fmt ++ "\n", args) catch return;

        if (self.always_flush or @intFromEnum(level) >= @intFromEnum(Level.warn)) {
            f.sync() catch {};
        }
    }

    /// Flush buffered data to disk.
    pub fn flush(self: *Logger) void {
        self.mutex.lock();
        defer self.mutex.unlock();
        if (self.file) |f| f.sync() catch {};
    }

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

    fn writeMarker(self: *Logger, msg: []const u8) !void {
        const f = self.file orelse return;
        const ts = timestampNow();
        try f.writer().print("=== {s}: {s} ===\n", .{ msg, &ts });
    }

    fn maybeRotate(self: *Logger) !void {
        const f = self.file orelse return;
        const size = (f.getPos() catch 0);
        if (size < self.max_size_bytes) return;

        // Close current file
        f.close();
        self.file = null;

        // Delete oldest backup
        const oldest = try std.fmt.allocPrint(
            self.allocator,
            "{s}.{d}",
            .{ self.path, self.max_backups },
        );
        defer self.allocator.free(oldest);
        std.fs.deleteFileAbsolute(oldest) catch {};

        // Rotate .N-1 → .N
        var i: u32 = self.max_backups;
        while (i > 1) : (i -= 1) {
            const old = try std.fmt.allocPrint(self.allocator, "{s}.{d}", .{ self.path, i - 1 });
            defer self.allocator.free(old);
            const new = try std.fmt.allocPrint(self.allocator, "{s}.{d}", .{ self.path, i });
            defer self.allocator.free(new);
            std.fs.renameAbsolute(old, new) catch {};
        }

        // Rotate current → .1
        const dot1 = try std.fmt.allocPrint(self.allocator, "{s}.1", .{self.path});
        defer self.allocator.free(dot1);
        std.fs.renameAbsolute(self.path, dot1) catch {};

        // Reopen
        const new_file = try std.fs.createFileAbsolute(self.path, .{ .truncate = true });
        self.file = new_file;

        const ts = timestampNow();
        new_file.writer().print("=== Log rotated: {s} ===\n", .{&ts}) catch {};
    }
};

// ---------------------------------------------------------------------------
// Timestamp helper
// ---------------------------------------------------------------------------

/// Return a 19-char "YYYY-MM-DD HH:MM:SS" timestamp without allocation.
fn timestampNow() [19]u8 {
    const c = @cImport(@cInclude("time.h"));
    var ts: c.time_t = c.time(null);
    var tm_buf: c.struct_tm = undefined;
    _ = c.localtime_r(&ts, &tm_buf);
    var buf: [19]u8 = undefined;
    _ = std.fmt.bufPrint(&buf, "{d:0>4}-{d:0>2}-{d:0>2} {d:0>2}:{d:0>2}:{d:0>2}", .{
        @as(u32, @intCast(tm_buf.tm_year + 1900)),
        @as(u32, @intCast(tm_buf.tm_mon + 1)),
        @as(u32, @intCast(tm_buf.tm_mday)),
        @as(u32, @intCast(tm_buf.tm_hour)),
        @as(u32, @intCast(tm_buf.tm_min)),
        @as(u32, @intCast(tm_buf.tm_sec)),
    }) catch unreachable;
    return buf;
}

// ---------------------------------------------------------------------------
// Log path detection
// ---------------------------------------------------------------------------

/// Detect the default log path using the same priority as the C logger.
/// Caller must free the returned path with `allocator.free`.
fn detectLogPath(allocator: std.mem.Allocator) ![]u8 {
    // 1. $KLAWED_LOG_PATH
    if (std.process.getEnvVarOwned(allocator, "KLAWED_LOG_PATH") catch null) |p| {
        return p;
    }

    // 2. $KLAWED_LOG_DIR/klawed.log
    if (std.process.getEnvVarOwned(allocator, "KLAWED_LOG_DIR") catch null) |dir| {
        defer allocator.free(dir);
        std.fs.cwd().makePath(dir) catch {};
        return std.fs.path.join(allocator, &.{ dir, "klawed.log" });
    }

    // 3. ./.klawed/logs/klawed.log
    {
        const local = ".klawed/logs";
        if (std.fs.cwd().makePath(local)) |_| {
            const cwd_path = try std.fs.realpathAlloc(allocator, ".");
            defer allocator.free(cwd_path);
            return std.fs.path.join(allocator, &.{ cwd_path, ".klawed/logs/klawed.log" });
        } else |_| {}
    }

    // 4. ~/.local/share/klawed/logs/klawed.log
    if (std.process.getEnvVarOwned(allocator, "HOME") catch null) |home| {
        defer allocator.free(home);
        const dir = try std.fs.path.join(allocator, &.{ home, ".local/share/klawed/logs" });
        defer allocator.free(dir);
        if (std.fs.cwd().makePath(dir)) |_| {
            return std.fs.path.join(allocator, &.{ dir, "klawed.log" });
        } else |_| {}
    }

    // 5. /tmp/klawed.log
    return allocator.dupe(u8, "/tmp/klawed.log");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "Logger: init, log, deinit" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const log_path = try std.fs.path.join(std.testing.allocator, &.{ tmp_path, "test.log" });
    defer std.testing.allocator.free(log_path);

    var logger = try Logger.init(std.testing.allocator, log_path);
    defer logger.deinit();

    logger.log(.info, "hello {s}", .{"world"});
    logger.log(.warn, "warning {d}", .{42});
    logger.flush();

    // Verify the log file has content
    const contents = try std.fs.File.readToEndAlloc(
        try std.fs.openFileAbsolute(log_path, .{}),
        std.testing.allocator,
        1024 * 1024,
    );
    defer std.testing.allocator.free(contents);

    try std.testing.expect(std.mem.indexOf(u8, contents, "hello world") != null);
    try std.testing.expect(std.mem.indexOf(u8, contents, "warning 42") != null);
}

test "Logger: level filtering" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const log_path = try std.fs.path.join(std.testing.allocator, &.{ tmp_path, "filter.log" });
    defer std.testing.allocator.free(log_path);

    var logger = try Logger.init(std.testing.allocator, log_path);
    defer logger.deinit();

    logger.setLevel(.err); // only errors
    logger.log(.info, "this should NOT appear", .{});
    logger.log(.err, "this SHOULD appear", .{});
    logger.flush();

    const contents = try std.fs.File.readToEndAlloc(
        try std.fs.openFileAbsolute(log_path, .{}),
        std.testing.allocator,
        1024 * 1024,
    );
    defer std.testing.allocator.free(contents);

    try std.testing.expect(std.mem.indexOf(u8, contents, "should NOT appear") == null);
    try std.testing.expect(std.mem.indexOf(u8, contents, "this SHOULD appear") != null);
}

test "Logger: session ID included in output" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const log_path = try std.fs.path.join(std.testing.allocator, &.{ tmp_path, "sess.log" });
    defer std.testing.allocator.free(log_path);

    var logger = try Logger.init(std.testing.allocator, log_path);
    defer logger.deinit();

    logger.setSessionId("sess_abc123");
    logger.log(.info, "with session", .{});
    logger.flush();

    const contents = try std.fs.File.readToEndAlloc(
        try std.fs.openFileAbsolute(log_path, .{}),
        std.testing.allocator,
        1024 * 1024,
    );
    defer std.testing.allocator.free(contents);

    try std.testing.expect(std.mem.indexOf(u8, contents, "sess_abc123") != null);
}

test "Level.name: returns correct strings" {
    try std.testing.expectEqualStrings("INFO ", Level.info.name());
    try std.testing.expectEqualStrings("WARN ", Level.warn.name());
    try std.testing.expectEqualStrings("ERROR", Level.err.name());
    try std.testing.expectEqualStrings("DEBUG", Level.debug.name());
    try std.testing.expectEqualStrings("FINE ", Level.fine.name());
}
