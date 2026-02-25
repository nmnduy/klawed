//! token_usage_db.zig — Dedicated SQLite token-usage tracking database
//!
//! Zig port of src/token_usage_db.c.
//!
//! Tracks per-session token consumption in a separate `.klawed/token_usage.db`
//! SQLite file.  The mutex is `std.Thread.Mutex` instead of pthread.

const std = @import("std");
const migrations = @import("migrations.zig");
const sqlite = @import("sqlite.zig");
const c = sqlite.c;

// ---------------------------------------------------------------------------
// SQL schema
// ---------------------------------------------------------------------------

const SCHEMA_SQL: [:0]const u8 =
    \\CREATE TABLE IF NOT EXISTS token_usage (
    \\    id INTEGER PRIMARY KEY AUTOINCREMENT,
    \\    api_call_id INTEGER,
    \\    session_id TEXT,
    \\    prompt_tokens INTEGER DEFAULT 0,
    \\    completion_tokens INTEGER DEFAULT 0,
    \\    total_tokens INTEGER DEFAULT 0,
    \\    cached_tokens INTEGER DEFAULT 0,
    \\    prompt_cache_hit_tokens INTEGER DEFAULT 0,
    \\    prompt_cache_miss_tokens INTEGER DEFAULT 0,
    \\    created_at INTEGER NOT NULL
    \\);
;

const INDEX_SQL: [:0]const u8 =
    \\CREATE INDEX IF NOT EXISTS idx_token_usage_api_call_id ON token_usage(api_call_id);
    \\CREATE INDEX IF NOT EXISTS idx_token_usage_session_id ON token_usage(session_id);
    \\CREATE INDEX IF NOT EXISTS idx_token_usage_created_at ON token_usage(created_at);
;

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

pub const TokenUsageRecord = struct {
    prompt_tokens: i32 = 0,
    completion_tokens: i32 = 0,
    total_tokens: i32 = 0,
    cached_tokens: i32 = 0,
    prompt_cache_hit_tokens: i32 = 0,
    prompt_cache_miss_tokens: i32 = 0,
};

pub const TokenUsageDb = struct {
    db: *c.sqlite3,
    db_path: []const u8, // owned
    mutex: std.Thread.Mutex,
    allocator: std.mem.Allocator,

    /// Open (or create) the token-usage database.
    ///
    /// If `db_path` is null/empty, the path is derived from:
    ///   `$KLAWED_TOKEN_USAGE_DB_PATH` → data dir / token_usage.db → ./token_usage.db
    pub fn init(allocator: std.mem.Allocator, db_path: ?[]const u8) !TokenUsageDb {
        const resolved = try resolveDbPath(allocator, db_path);
        errdefer allocator.free(resolved);

        // Ensure parent directory.
        if (std.fs.path.dirname(resolved)) |dir| {
            std.fs.cwd().makePath(dir) catch {};
        }

        const path_z = try allocator.dupeZ(u8, resolved);
        defer allocator.free(path_z);

        var handle: ?*c.sqlite3 = null;
        if (c.sqlite3_open(path_z.ptr, &handle) != c.SQLITE_OK) {
            if (handle) |h| _ = c.sqlite3_close(h);
            return migrations.SqliteError.SqliteOpen;
        }

        var self = TokenUsageDb{
            .db = handle.?,
            .db_path = resolved,
            .mutex = .{},
            .allocator = allocator,
        };

        // Configure for performance.
        self.execRaw("PRAGMA journal_mode=WAL;") catch {};
        self.execRaw("PRAGMA synchronous=NORMAL;") catch {};
        _ = c.sqlite3_busy_timeout(self.db, 5000);

        // Create schema.
        try self.execRaw(SCHEMA_SQL);
        self.execRaw(INDEX_SQL) catch {};

        // Apply migrations (currently a no-op).
        var mdb = migrations.Db{ .handle = self.db };
        try migrations.applyTokenUsageMigrations(&mdb);

        // Auto-rotate on startup.
        self.autoRotate() catch {};

        return self;
    }

    /// Close and free the database.
    pub fn deinit(self: *TokenUsageDb) void {
        _ = c.sqlite3_close(self.db);
        self.allocator.free(self.db_path);
        self.* = undefined;
    }

    // -----------------------------------------------------------------------
    // Write
    // -----------------------------------------------------------------------

    /// Log a token-usage record.  `api_call_id` may be 0 for no association.
    pub fn log(
        self: *TokenUsageDb,
        api_call_id: i64,
        session_id: ?[]const u8,
        rec: TokenUsageRecord,
    ) !void {
        const sql: [:0]const u8 =
            \\INSERT INTO token_usage
            \\  (api_call_id, session_id, prompt_tokens, completion_tokens,
            \\   total_tokens, cached_tokens, prompt_cache_hit_tokens,
            \\   prompt_cache_miss_tokens, created_at)
            \\  VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
        ;

        self.mutex.lock();
        defer self.mutex.unlock();

        const stmt = try self.prepareRaw(sql);
        defer _ = c.sqlite3_finalize(stmt);

        if (api_call_id > 0) {
            _ = c.sqlite3_bind_int64(stmt, 1, api_call_id);
        } else {
            _ = c.sqlite3_bind_null(stmt, 1);
        }

        if (session_id) |sid| {
            _ = c.sqlite3_bind_text(stmt, 2, sid.ptr, @intCast(sid.len), c.SQLITE_TRANSIENT);
        } else {
            _ = c.sqlite3_bind_null(stmt, 2);
        }

        _ = c.sqlite3_bind_int(stmt, 3, rec.prompt_tokens);
        _ = c.sqlite3_bind_int(stmt, 4, rec.completion_tokens);
        _ = c.sqlite3_bind_int(stmt, 5, rec.total_tokens);
        _ = c.sqlite3_bind_int(stmt, 6, rec.cached_tokens);
        _ = c.sqlite3_bind_int(stmt, 7, rec.prompt_cache_hit_tokens);
        _ = c.sqlite3_bind_int(stmt, 8, rec.prompt_cache_miss_tokens);
        _ = c.sqlite3_bind_int64(stmt, 9, std.time.timestamp());

        if (c.sqlite3_step(stmt) != c.SQLITE_DONE) return migrations.SqliteError.SqliteStep;
    }

    // -----------------------------------------------------------------------
    // Read
    // -----------------------------------------------------------------------

    /// Get token counts for the most-recent record in `session_id`.
    pub fn getSessionUsage(
        self: *TokenUsageDb,
        session_id: ?[]const u8,
    ) !struct { prompt: i32, completion: i32, cached: i32 } {
        const sql_session: [:0]const u8 =
            \\SELECT prompt_tokens, completion_tokens, cached_tokens
            \\FROM token_usage WHERE session_id = ?
            \\ORDER BY created_at DESC, id DESC LIMIT 1;
        ;
        const sql_all: [:0]const u8 =
            \\SELECT prompt_tokens, completion_tokens, cached_tokens
            \\FROM token_usage ORDER BY created_at DESC, id DESC LIMIT 1;
        ;

        self.mutex.lock();
        defer self.mutex.unlock();

        const stmt = try self.prepareRaw(if (session_id != null) sql_session else sql_all);
        defer _ = c.sqlite3_finalize(stmt);

        if (session_id) |sid| {
            _ = c.sqlite3_bind_text(stmt, 1, sid.ptr, @intCast(sid.len), c.SQLITE_TRANSIENT);
        }

        if (c.sqlite3_step(stmt) != c.SQLITE_ROW) {
            return .{ .prompt = 0, .completion = 0, .cached = 0 };
        }
        return .{
            .prompt = c.sqlite3_column_int(stmt, 0),
            .completion = c.sqlite3_column_int(stmt, 1),
            .cached = c.sqlite3_column_int(stmt, 2),
        };
    }

    /// Get the most-recent prompt token count for a session.
    pub fn getLastPromptTokens(self: *TokenUsageDb, session_id: ?[]const u8) !i32 {
        const sql_session: [:0]const u8 =
            \\SELECT prompt_tokens FROM token_usage
            \\WHERE session_id = ? ORDER BY created_at DESC, id DESC LIMIT 1;
        ;
        const sql_all: [:0]const u8 =
            \\SELECT prompt_tokens FROM token_usage
            \\ORDER BY created_at DESC, id DESC LIMIT 1;
        ;

        self.mutex.lock();
        defer self.mutex.unlock();

        const stmt = try self.prepareRaw(if (session_id != null) sql_session else sql_all);
        defer _ = c.sqlite3_finalize(stmt);
        if (session_id) |sid| {
            _ = c.sqlite3_bind_text(stmt, 1, sid.ptr, @intCast(sid.len), c.SQLITE_TRANSIENT);
        }
        if (c.sqlite3_step(stmt) != c.SQLITE_ROW) return 0;
        return c.sqlite3_column_int(stmt, 0);
    }

    /// Get the most-recent cached token count for a session.
    pub fn getLastCachedTokens(self: *TokenUsageDb, session_id: ?[]const u8) !i32 {
        const sql_session: [:0]const u8 =
            \\SELECT cached_tokens, prompt_cache_hit_tokens FROM token_usage
            \\WHERE session_id = ? ORDER BY created_at DESC, id DESC LIMIT 1;
        ;
        const sql_all: [:0]const u8 =
            \\SELECT cached_tokens, prompt_cache_hit_tokens FROM token_usage
            \\ORDER BY created_at DESC, id DESC LIMIT 1;
        ;

        self.mutex.lock();
        defer self.mutex.unlock();

        const stmt = try self.prepareRaw(if (session_id != null) sql_session else sql_all);
        defer _ = c.sqlite3_finalize(stmt);
        if (session_id) |sid| {
            _ = c.sqlite3_bind_text(stmt, 1, sid.ptr, @intCast(sid.len), c.SQLITE_TRANSIENT);
        }
        if (c.sqlite3_step(stmt) != c.SQLITE_ROW) return 0;
        var cached = c.sqlite3_column_int(stmt, 0);
        if (cached == 0) cached = c.sqlite3_column_int(stmt, 1);
        return cached;
    }

    // -----------------------------------------------------------------------
    // Rotation
    // -----------------------------------------------------------------------

    pub fn rotateByAge(self: *TokenUsageDb, days: i32) !i32 {
        if (days == 0) return 0;
        const cutoff = std.time.timestamp() - @as(i64, days) * 86400;

        self.mutex.lock();
        defer self.mutex.unlock();

        const stmt = try self.prepareRaw("DELETE FROM token_usage WHERE created_at < ?;");
        defer _ = c.sqlite3_finalize(stmt);
        _ = c.sqlite3_bind_int64(stmt, 1, cutoff);
        if (c.sqlite3_step(stmt) != c.SQLITE_DONE) return migrations.SqliteError.SqliteStep;
        return c.sqlite3_changes(self.db);
    }

    pub fn rotateByCount(self: *TokenUsageDb, max_records: i32) !i32 {
        if (max_records == 0) return 0;

        self.mutex.lock();
        defer self.mutex.unlock();

        // Count total records.
        const count_stmt = try self.prepareRaw("SELECT COUNT(*) FROM token_usage;");
        defer _ = c.sqlite3_finalize(count_stmt);
        if (c.sqlite3_step(count_stmt) != c.SQLITE_ROW) return 0;
        const total = c.sqlite3_column_int(count_stmt, 0);
        if (total <= max_records) return 0;

        const del_stmt = try self.prepareRaw(
            "DELETE FROM token_usage WHERE id NOT IN " ++
                "(SELECT id FROM token_usage ORDER BY created_at DESC LIMIT ?);",
        );
        defer _ = c.sqlite3_finalize(del_stmt);
        _ = c.sqlite3_bind_int(del_stmt, 1, max_records);
        if (c.sqlite3_step(del_stmt) != c.SQLITE_DONE) return migrations.SqliteError.SqliteStep;
        return c.sqlite3_changes(self.db);
    }

    pub fn vacuum(self: *TokenUsageDb) !void {
        self.mutex.lock();
        defer self.mutex.unlock();
        try self.execRaw("VACUUM;");
    }

    pub fn autoRotate(self: *TokenUsageDb) !void {
        const auto = std.posix.getenv("KLAWED_DB_AUTO_ROTATE");
        if (auto != null and std.mem.eql(u8, auto.?, "0")) return;

        const max_days = envInt("KLAWED_TOKEN_USAGE_DB_MAX_DAYS", 30);
        const max_recs = envInt("KLAWED_TOKEN_USAGE_DB_MAX_RECORDS", 5000);

        var need_vacuum = false;
        if (max_days > 0 and (try self.rotateByAge(@intCast(max_days))) > 0) need_vacuum = true;
        if (max_recs > 0 and (try self.rotateByCount(@intCast(max_recs))) > 0) need_vacuum = true;
        if (need_vacuum) try self.vacuum();
    }

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    fn execRaw(self: *TokenUsageDb, sql: [:0]const u8) !void {
        var errmsg: ?[*:0]u8 = null;
        const rc = c.sqlite3_exec(self.db, sql.ptr, null, null, &errmsg);
        if (rc != c.SQLITE_OK) {
            if (errmsg) |msg| c.sqlite3_free(msg);
            return migrations.SqliteError.SqliteExec;
        }
    }

    fn prepareRaw(self: *TokenUsageDb, sql: [:0]const u8) !*c.sqlite3_stmt {
        var stmt: ?*c.sqlite3_stmt = null;
        if (c.sqlite3_prepare_v2(self.db, sql.ptr, -1, &stmt, null) != c.SQLITE_OK) {
            return migrations.SqliteError.SqlitePrepare;
        }
        return stmt.?;
    }
};

// ---------------------------------------------------------------------------
// Path resolution helpers
// ---------------------------------------------------------------------------

fn resolveDbPath(allocator: std.mem.Allocator, explicit: ?[]const u8) ![]const u8 {
    if (explicit) |p| {
        if (p.len > 0) return allocator.dupe(u8, p);
    }

    if (std.process.getEnvVarOwned(allocator, "KLAWED_TOKEN_USAGE_DB_PATH")) |env_path| {
        if (env_path.len > 0) return env_path;
        allocator.free(env_path);
    } else |_| {}

    // Try data dir.
    const data_dir = @import("data_dir.zig");
    if (data_dir.buildPath(allocator, "token_usage.db")) |p| {
        return p;
    } else |_| {}

    return allocator.dupe(u8, "./token_usage.db");
}

fn envInt(name: []const u8, default_val: i64) i64 {
    const val = std.posix.getenv(name) orelse return default_val;
    return std.fmt.parseInt(i64, val, 10) catch default_val;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "TokenUsageDb init and deinit" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "token_usage.db" });
    defer allocator.free(p);

    var tdb = try TokenUsageDb.init(allocator, p);
    defer tdb.deinit();
}

test "TokenUsageDb log and retrieve" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "token_usage2.db" });
    defer allocator.free(p);

    var tdb = try TokenUsageDb.init(allocator, p);
    defer tdb.deinit();

    try tdb.log(1, "sess-abc", .{
        .prompt_tokens = 100,
        .completion_tokens = 50,
        .total_tokens = 150,
        .cached_tokens = 20,
        .prompt_cache_hit_tokens = 20,
        .prompt_cache_miss_tokens = 80,
    });

    const usage = try tdb.getSessionUsage("sess-abc");
    try std.testing.expectEqual(@as(i32, 100), usage.prompt);
    try std.testing.expectEqual(@as(i32, 50), usage.completion);
    try std.testing.expectEqual(@as(i32, 20), usage.cached);
}

test "TokenUsageDb getLastPromptTokens" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "token_usage3.db" });
    defer allocator.free(p);

    var tdb = try TokenUsageDb.init(allocator, p);
    defer tdb.deinit();

    // Empty database.
    const before = try tdb.getLastPromptTokens("sess-x");
    try std.testing.expectEqual(@as(i32, 0), before);

    try tdb.log(0, "sess-x", .{ .prompt_tokens = 777, .completion_tokens = 0, .total_tokens = 0 });

    const after = try tdb.getLastPromptTokens("sess-x");
    try std.testing.expectEqual(@as(i32, 777), after);
}

test "TokenUsageDb rotate by count" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "token_usage4.db" });
    defer allocator.free(p);

    var tdb = try TokenUsageDb.init(allocator, p);
    defer tdb.deinit();

    var i: usize = 0;
    while (i < 5) : (i += 1) {
        try tdb.log(0, null, .{ .prompt_tokens = @intCast(i) });
    }

    const deleted = try tdb.rotateByCount(3);
    try std.testing.expect(deleted >= 2);
}
