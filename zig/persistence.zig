//! persistence.zig — API call history database
//!
//! Zig port of src/persistence.c.
//!
//! Stores every LLM API call in `api_calls.db` for auditing and session
//! resume.  The mutex is `std.Thread.Mutex` instead of pthread.

const std = @import("std");
const migrations = @import("migrations.zig");
const token_usage_db = @import("token_usage_db.zig");
const sqlite = @import("sqlite.zig");
const c = sqlite.c;

// ---------------------------------------------------------------------------
// SQL schema
// ---------------------------------------------------------------------------

const SCHEMA_SQL: [:0]const u8 =
    \\CREATE TABLE IF NOT EXISTS api_calls (
    \\    id INTEGER PRIMARY KEY AUTOINCREMENT,
    \\    timestamp TEXT NOT NULL,
    \\    session_id TEXT,
    \\    api_base_url TEXT NOT NULL,
    \\    request_json TEXT NOT NULL,
    \\    headers_json TEXT,
    \\    response_json TEXT,
    \\    model TEXT NOT NULL,
    \\    status TEXT NOT NULL,
    \\    http_status INTEGER,
    \\    error_message TEXT,
    \\    duration_ms INTEGER,
    \\    tool_count INTEGER DEFAULT 0,
    \\    created_at INTEGER NOT NULL
    \\);
;

const INDEX_SQL: [:0]const u8 =
    \\CREATE INDEX IF NOT EXISTS idx_api_calls_timestamp ON api_calls(timestamp);
    \\CREATE INDEX IF NOT EXISTS idx_api_calls_session_id ON api_calls(session_id);
;

// ---------------------------------------------------------------------------
// ApiCallRecord
// ---------------------------------------------------------------------------

pub const ApiCallRecord = struct {
    session_id: ?[]const u8 = null,
    api_base_url: []const u8,
    request_json: []const u8,
    headers_json: ?[]const u8 = null,
    response_json: ?[]const u8 = null,
    model: []const u8,
    status: []const u8,
    http_status: i32 = 0,
    error_message: ?[]const u8 = null,
    duration_ms: i64 = 0,
    tool_count: i32 = 0,
};

// ---------------------------------------------------------------------------
// PersistenceDb
// ---------------------------------------------------------------------------

pub const PersistenceDb = struct {
    db: *c.sqlite3,
    db_path: []const u8, // owned
    mutex: std.Thread.Mutex,
    allocator: std.mem.Allocator,
    token_db: ?token_usage_db.TokenUsageDb,

    /// Open (or create) the api_calls database.
    ///
    /// If `db_path` is null/empty the path is derived from:
    ///   `$KLAWED_DB_PATH` → data_dir/api_calls.db → XDG → HOME → ./api_calls.db
    pub fn init(allocator: std.mem.Allocator, db_path: ?[]const u8) !PersistenceDb {
        const resolved = try resolveDbPath(allocator, db_path);
        errdefer allocator.free(resolved);

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

        var self = PersistenceDb{
            .db = handle.?,
            .db_path = resolved,
            .mutex = .{},
            .allocator = allocator,
            .token_db = null,
        };

        // Configure for performance + reliability.
        self.execRaw("PRAGMA journal_mode=WAL;") catch {};
        self.execRaw("PRAGMA synchronous=NORMAL;") catch {};
        _ = c.sqlite3_busy_timeout(self.db, 5000);

        // Create schema.
        try self.execRaw(SCHEMA_SQL);
        self.execRaw(INDEX_SQL) catch {};

        // Apply schema migrations.
        var mdb = migrations.Db{ .handle = self.db };
        try migrations.applyApiCallsMigrations(&mdb);

        // Open the token-usage side-car database.
        self.token_db = token_usage_db.TokenUsageDb.init(allocator, null) catch null;

        // Auto-rotate on startup.
        self.autoRotate() catch {};

        return self;
    }

    pub fn deinit(self: *PersistenceDb) void {
        if (self.token_db) |*tdb| tdb.deinit();
        _ = c.sqlite3_close(self.db);
        self.allocator.free(self.db_path);
        self.* = undefined;
    }

    // -----------------------------------------------------------------------
    // Insert
    // -----------------------------------------------------------------------

    /// Log one API call.  Returns the inserted row id.
    pub fn logApiCall(self: *PersistenceDb, rec: ApiCallRecord) !i64 {
        const sql: [:0]const u8 =
            \\INSERT INTO api_calls
            \\  (timestamp, session_id, api_base_url, request_json, headers_json,
            \\   response_json, model, status, http_status, error_message,
            \\   duration_ms, tool_count, created_at)
            \\  VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        ;

        // Build ISO timestamp outside the lock.
        var ts_buf: [32]u8 = undefined;
        const ts = isoTimestamp(&ts_buf);

        self.mutex.lock();
        defer self.mutex.unlock();

        const stmt = try self.prepareRaw(sql);
        defer _ = c.sqlite3_finalize(stmt);

        bindText(stmt, 1, ts);
        bindOptText(stmt, 2, rec.session_id);
        bindText(stmt, 3, rec.api_base_url);
        bindText(stmt, 4, rec.request_json);
        bindOptText(stmt, 5, rec.headers_json);
        bindOptText(stmt, 6, rec.response_json);
        bindText(stmt, 7, rec.model);
        bindText(stmt, 8, rec.status);
        _ = c.sqlite3_bind_int(stmt, 9, rec.http_status);
        bindOptText(stmt, 10, rec.error_message);
        _ = c.sqlite3_bind_int64(stmt, 11, rec.duration_ms);
        _ = c.sqlite3_bind_int(stmt, 12, rec.tool_count);
        _ = c.sqlite3_bind_int64(stmt, 13, std.time.timestamp());

        if (c.sqlite3_step(stmt) != c.SQLITE_DONE) return migrations.SqliteError.SqliteStep;
        return c.sqlite3_last_insert_rowid(self.db);
    }

    // -----------------------------------------------------------------------
    // Token-usage delegation
    // -----------------------------------------------------------------------

    pub fn getSessionTokenUsage(
        self: *PersistenceDb,
        session_id: ?[]const u8,
    ) !struct { prompt: i32, completion: i32, cached: i32 } {
        if (self.token_db) |*tdb| {
            return tdb.getSessionUsage(session_id);
        }
        return .{ .prompt = 0, .completion = 0, .cached = 0 };
    }

    pub fn getLastPromptTokens(self: *PersistenceDb, session_id: ?[]const u8) !i32 {
        if (self.token_db) |*tdb| return tdb.getLastPromptTokens(session_id);
        return 0;
    }

    pub fn getLastCachedTokens(self: *PersistenceDb, session_id: ?[]const u8) !i32 {
        if (self.token_db) |*tdb| return tdb.getLastCachedTokens(session_id);
        return 0;
    }

    // -----------------------------------------------------------------------
    // Rotation
    // -----------------------------------------------------------------------

    pub fn rotateByAge(self: *PersistenceDb, days: i32) !i32 {
        if (days == 0) return 0;
        const cutoff = std.time.timestamp() - @as(i64, days) * 86400;

        self.mutex.lock();
        defer self.mutex.unlock();

        const stmt = try self.prepareRaw("DELETE FROM api_calls WHERE created_at < ?;");
        defer _ = c.sqlite3_finalize(stmt);
        _ = c.sqlite3_bind_int64(stmt, 1, cutoff);
        if (c.sqlite3_step(stmt) != c.SQLITE_DONE) return migrations.SqliteError.SqliteStep;
        return c.sqlite3_changes(self.db);
    }

    pub fn rotateByCount(self: *PersistenceDb, max_records: i32) !i32 {
        if (max_records == 0) return 0;

        self.mutex.lock();
        defer self.mutex.unlock();

        const count_stmt = try self.prepareRaw("SELECT COUNT(*) FROM api_calls;");
        defer _ = c.sqlite3_finalize(count_stmt);
        if (c.sqlite3_step(count_stmt) != c.SQLITE_ROW) return 0;
        const total = c.sqlite3_column_int(count_stmt, 0);
        if (total <= max_records) return 0;

        const del_stmt = try self.prepareRaw(
            "DELETE FROM api_calls WHERE id NOT IN " ++
                "(SELECT id FROM api_calls ORDER BY created_at DESC LIMIT ?);",
        );
        defer _ = c.sqlite3_finalize(del_stmt);
        _ = c.sqlite3_bind_int(del_stmt, 1, max_records);
        if (c.sqlite3_step(del_stmt) != c.SQLITE_DONE) return migrations.SqliteError.SqliteStep;
        return c.sqlite3_changes(self.db);
    }

    pub fn vacuum(self: *PersistenceDb) !void {
        self.mutex.lock();
        defer self.mutex.unlock();
        try self.execRaw("VACUUM;");
    }

    pub fn autoRotate(self: *PersistenceDb) !void {
        const auto_env = std.posix.getenv("KLAWED_DB_AUTO_ROTATE");
        if (auto_env != null and std.mem.eql(u8, auto_env.?, "0")) return;

        const max_days = envInt("KLAWED_DB_MAX_DAYS", 30);
        const max_recs = envInt("KLAWED_DB_MAX_RECORDS", 1000);

        var need_vacuum = false;
        if (max_days > 0 and (try self.rotateByAge(@intCast(max_days))) > 0) need_vacuum = true;
        if (max_recs > 0 and (try self.rotateByCount(@intCast(max_recs))) > 0) need_vacuum = true;
        if (need_vacuum) try self.vacuum();
    }

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    fn execRaw(self: *PersistenceDb, sql: [:0]const u8) !void {
        var errmsg: ?[*:0]u8 = null;
        const rc = c.sqlite3_exec(self.db, sql.ptr, null, null, &errmsg);
        if (rc != c.SQLITE_OK) {
            if (errmsg) |msg| c.sqlite3_free(msg);
            return migrations.SqliteError.SqliteExec;
        }
    }

    fn prepareRaw(self: *PersistenceDb, sql: [:0]const u8) !*c.sqlite3_stmt {
        var stmt: ?*c.sqlite3_stmt = null;
        if (c.sqlite3_prepare_v2(self.db, sql.ptr, -1, &stmt, null) != c.SQLITE_OK) {
            return migrations.SqliteError.SqlitePrepare;
        }
        return stmt.?;
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn bindText(stmt: *c.sqlite3_stmt, idx: c_int, s: []const u8) void {
    _ = c.sqlite3_bind_text(stmt, idx, s.ptr, @intCast(s.len), c.SQLITE_TRANSIENT);
}

fn bindOptText(stmt: *c.sqlite3_stmt, idx: c_int, s: ?[]const u8) void {
    if (s) |v| {
        bindText(stmt, idx, v);
    } else {
        _ = c.sqlite3_bind_null(stmt, idx);
    }
}

fn isoTimestamp(buf: *[32]u8) []const u8 {
    const ts = std.time.timestamp();
    const secs: u64 = @intCast(@max(0, ts));
    const epoch = std.time.epoch.EpochSeconds{ .secs = secs };
    const day = epoch.getEpochDay();
    const yd = day.calculateYearDay();
    const md = yd.calculateMonthDay();
    const dt = epoch.getDaySeconds();
    const len = std.fmt.formatIntBuf(buf[0..4], yd.year, 10, .lower, .{ .width = 4, .fill = '0' });
    buf[len] = '-';
    const ml = std.fmt.formatIntBuf(buf[len + 1 .. len + 3], @as(u32, @intFromEnum(md.month)), 10, .lower, .{ .width = 2, .fill = '0' });
    buf[len + 1 + ml] = '-';
    const dl = std.fmt.formatIntBuf(buf[len + 1 + ml + 1 .. len + 1 + ml + 3], md.day_index + 1, 10, .lower, .{ .width = 2, .fill = '0' });
    buf[len + 1 + ml + 1 + dl] = ' ';
    const base = len + 1 + ml + 1 + dl + 1;
    const hl = std.fmt.formatIntBuf(buf[base .. base + 2], dt.getHoursIntoDay(), 10, .lower, .{ .width = 2, .fill = '0' });
    buf[base + hl] = ':';
    const minl = std.fmt.formatIntBuf(buf[base + hl + 1 .. base + hl + 3], dt.getMinutesIntoHour(), 10, .lower, .{ .width = 2, .fill = '0' });
    buf[base + hl + 1 + minl] = ':';
    const sl = std.fmt.formatIntBuf(buf[base + hl + 1 + minl + 1 ..][0..2], dt.getSecondsIntoMinute(), 10, .lower, .{ .width = 2, .fill = '0' });
    return buf[0 .. base + hl + 1 + minl + 1 + sl];
}

fn resolveDbPath(allocator: std.mem.Allocator, explicit: ?[]const u8) ![]const u8 {
    if (explicit) |p| {
        if (p.len > 0) return allocator.dupe(u8, p);
    }

    if (std.process.getEnvVarOwned(allocator, "KLAWED_DB_PATH")) |env_path| {
        if (env_path.len > 0) return env_path;
        allocator.free(env_path);
    } else |_| {}

    const data_dir_mod = @import("data_dir.zig");
    if (data_dir_mod.buildPath(allocator, "api_calls.db")) |p| {
        return p;
    } else |_| {}

    if (std.process.getEnvVarOwned(allocator, "XDG_DATA_HOME")) |xdg| {
        defer allocator.free(xdg);
        if (xdg.len > 0) {
            return std.fs.path.join(allocator, &.{ xdg, "klawed", "api_calls.db" });
        }
    } else |_| {}

    if (std.process.getEnvVarOwned(allocator, "HOME")) |home| {
        defer allocator.free(home);
        if (home.len > 0) {
            return std.fs.path.join(allocator, &.{ home, ".local", "share", "klawed", "api_calls.db" });
        }
    } else |_| {}

    return allocator.dupe(u8, "./api_calls.db");
}

fn envInt(name: []const u8, default_val: i64) i64 {
    const val = std.posix.getenv(name) orelse return default_val;
    return std.fmt.parseInt(i64, val, 10) catch default_val;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "PersistenceDb init and deinit" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "api_calls.db" });
    defer allocator.free(p);

    var pdb = try PersistenceDb.init(allocator, p);
    defer pdb.deinit();
}

test "PersistenceDb logApiCall and retrieve" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "api_calls2.db" });
    defer allocator.free(p);

    var pdb = try PersistenceDb.init(allocator, p);
    defer pdb.deinit();

    const id = try pdb.logApiCall(.{
        .session_id = "sess-1",
        .api_base_url = "https://api.openai.com",
        .request_json = "{}",
        .model = "gpt-4",
        .status = "success",
        .duration_ms = 1200,
        .tool_count = 0,
    });
    try std.testing.expect(id > 0);
}

test "PersistenceDb rotate by count" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "api_calls3.db" });
    defer allocator.free(p);

    var pdb = try PersistenceDb.init(allocator, p);
    defer pdb.deinit();

    var i: usize = 0;
    while (i < 5) : (i += 1) {
        _ = try pdb.logApiCall(.{
            .api_base_url = "https://api.openai.com",
            .request_json = "{}",
            .model = "gpt-4",
            .status = "success",
        });
    }

    const deleted = try pdb.rotateByCount(3);
    try std.testing.expect(deleted >= 2);
}
