//! migrations.zig — Schema migration system for SQLite databases
//!
//! Zig port of src/migrations.c and src/token_usage_db_migrations.c.
//!
//! Wraps `sqlite3` with a thin error-union API and applies sequential
//! schema migrations tracked in a `schema_version` table.

const std = @import("std");
const sqlite = @import("sqlite.zig");
const c = sqlite.c;

// ---------------------------------------------------------------------------
// Public error set
// ---------------------------------------------------------------------------

pub const SqliteError = error{
    SqliteOpen,
    SqliteExec,
    SqlitePrepare,
    SqliteStep,
    SqliteBind,
    SqliteNotFound,
    OutOfMemory,
};

// ---------------------------------------------------------------------------
// Db — thin wrapper around sqlite3*
// ---------------------------------------------------------------------------

pub const Db = struct {
    handle: *c.sqlite3,

    /// Open a database file, creating it if necessary.
    pub fn open(path: [:0]const u8) !Db {
        var handle: ?*c.sqlite3 = null;
        const rc = c.sqlite3_open(path.ptr, &handle);
        if (rc != c.SQLITE_OK) {
            if (handle) |h| _ = c.sqlite3_close(h);
            return SqliteError.SqliteOpen;
        }
        return Db{ .handle = handle.? };
    }

    /// Close the database.
    pub fn close(self: *Db) void {
        _ = c.sqlite3_close(self.handle);
    }

    /// Execute a SQL statement (no result rows expected).
    pub fn exec(self: *Db, sql: [:0]const u8) !void {
        var errmsg: ?[*:0]u8 = null;
        const rc = c.sqlite3_exec(self.handle, sql.ptr, null, null, &errmsg);
        if (rc != c.SQLITE_OK) {
            if (errmsg) |msg| c.sqlite3_free(msg);
            return SqliteError.SqliteExec;
        }
    }

    /// Execute a SQL statement, tolerating "duplicate column name" errors.
    /// Used for idempotent `ALTER TABLE ... ADD COLUMN` migrations.
    pub fn execIdempotentAlter(self: *Db, sql: [:0]const u8) !void {
        var errmsg: ?[*:0]u8 = null;
        const rc = c.sqlite3_exec(self.handle, sql.ptr, null, null, &errmsg);
        if (rc != c.SQLITE_OK) {
            if (errmsg) |msg| {
                const is_dup = c.strstr(msg, "duplicate column name") != null;
                c.sqlite3_free(msg);
                if (is_dup) return; // already applied — not an error
            }
            return SqliteError.SqliteExec;
        }
    }

    /// Prepare a SQL statement and return the raw statement handle.
    /// Caller must call `c.sqlite3_finalize(stmt)`.
    pub fn prepare(self: *Db, sql: [:0]const u8) !*c.sqlite3_stmt {
        var stmt: ?*c.sqlite3_stmt = null;
        const rc = c.sqlite3_prepare_v2(self.handle, sql.ptr, -1, &stmt, null);
        if (rc != c.SQLITE_OK) return SqliteError.SqlitePrepare;
        return stmt.?;
    }

    /// Return the last SQLite error message.
    pub fn lastError(self: *const Db) []const u8 {
        return std.mem.sliceTo(c.sqlite3_errmsg(self.handle), 0);
    }
};

// ---------------------------------------------------------------------------
// Migration descriptor
// ---------------------------------------------------------------------------

pub const MigrationFn = *const fn (db: *Db) anyerror!void;

pub const Migration = struct {
    version: i32,
    description: []const u8,
    up: MigrationFn,
};

// ---------------------------------------------------------------------------
// api_calls.db migrations
// ---------------------------------------------------------------------------

/// Migration 001 — add `session_id` column to `api_calls`.
fn migration001AddSessionId(db: *Db) anyerror!void {
    try db.execIdempotentAlter(
        "ALTER TABLE api_calls ADD COLUMN session_id TEXT;",
    );
    // Non-fatal if index already exists.
    db.exec(
        "CREATE INDEX IF NOT EXISTS idx_api_calls_session_id ON api_calls(session_id);",
    ) catch {};
}

/// Migration 002 — add `headers_json` column to `api_calls`.
fn migration002AddHeadersJson(db: *Db) anyerror!void {
    try db.execIdempotentAlter(
        "ALTER TABLE api_calls ADD COLUMN headers_json TEXT;",
    );
}

/// Migration 003 — add `session_id` to `token_usage` (legacy; no-op for new
/// databases where token_usage has moved to a separate file).
fn migration003AddSessionIdToTokenUsage(db: *Db) anyerror!void {
    // Check if the token_usage table exists in this database (legacy only).
    const stmt = db.prepare(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='token_usage';",
    ) catch return; // cannot prepare → treat as no-op
    defer _ = c.sqlite3_finalize(stmt);

    if (c.sqlite3_step(stmt) != c.SQLITE_ROW) return; // table absent — skip

    // Table present in legacy database — try to add the column.
    db.execIdempotentAlter(
        "ALTER TABLE token_usage ADD COLUMN session_id TEXT;",
    ) catch {};
    db.exec(
        "CREATE INDEX IF NOT EXISTS idx_token_usage_session_id ON token_usage(session_id);",
    ) catch {};
}

/// All api_calls.db migrations in order.
const API_CALLS_MIGRATIONS = [_]Migration{
    .{
        .version = 1,
        .description = "Add session_id column to api_calls table",
        .up = migration001AddSessionId,
    },
    .{
        .version = 2,
        .description = "Add headers_json column to api_calls table",
        .up = migration002AddHeadersJson,
    },
    .{
        .version = 3,
        .description = "Add session_id column to token_usage table",
        .up = migration003AddSessionIdToTokenUsage,
    },
};

// ---------------------------------------------------------------------------
// Version management helpers
// ---------------------------------------------------------------------------

/// Return the current schema version from `schema_version` table.
/// Returns 0 if the table does not exist or is empty.
pub fn getVersion(db: *Db, version_table: [:0]const u8) i32 {
    // Check the table exists.
    var check_buf: [256]u8 = undefined;
    const check_sql = std.fmt.bufPrintZ(
        &check_buf,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='{s}';",
        .{version_table},
    ) catch return 0;

    const check_stmt = db.prepare(check_sql) catch return 0;
    defer _ = c.sqlite3_finalize(check_stmt);
    if (c.sqlite3_step(check_stmt) != c.SQLITE_ROW) return 0;

    // Table exists — read max version.
    var ver_buf: [256]u8 = undefined;
    const ver_sql = std.fmt.bufPrintZ(
        &ver_buf,
        "SELECT version FROM {s} ORDER BY version DESC LIMIT 1;",
        .{version_table},
    ) catch return 0;

    const ver_stmt = db.prepare(ver_sql) catch return 0;
    defer _ = c.sqlite3_finalize(ver_stmt);
    if (c.sqlite3_step(ver_stmt) != c.SQLITE_ROW) return 0;
    return c.sqlite3_column_int(ver_stmt, 0);
}

/// Ensure the version table exists and insert a version record.
fn setVersion(
    db: *Db,
    version_table: [:0]const u8,
    version: i32,
    description: []const u8,
) !void {
    // Create table.
    var create_buf: [512]u8 = undefined;
    const create_sql = try std.fmt.bufPrintZ(
        &create_buf,
        "CREATE TABLE IF NOT EXISTS {s} (" ++
            "version INTEGER PRIMARY KEY," ++
            "description TEXT NOT NULL," ++
            "applied_at INTEGER NOT NULL" ++
            ");",
        .{version_table},
    );
    try db.exec(create_sql);

    // Insert/replace version record.
    var insert_buf: [512]u8 = undefined;
    const insert_sql = try std.fmt.bufPrintZ(
        &insert_buf,
        "INSERT OR REPLACE INTO {s} (version, description, applied_at) VALUES (?, ?, ?);",
        .{version_table},
    );

    const stmt = try db.prepare(insert_sql);
    defer _ = c.sqlite3_finalize(stmt);

    _ = c.sqlite3_bind_int(stmt, 1, version);
    _ = c.sqlite3_bind_text(stmt, 2, description.ptr, @intCast(description.len), c.SQLITE_TRANSIENT);
    _ = c.sqlite3_bind_int64(stmt, 3, std.time.timestamp());

    if (c.sqlite3_step(stmt) != c.SQLITE_DONE) return SqliteError.SqliteStep;
}

// ---------------------------------------------------------------------------
// Generic migration runner
// ---------------------------------------------------------------------------

/// Apply all pending migrations from `migrations[]` to `db`.
///
/// Uses the given `version_table` name to track which migrations have been
/// applied.  Each migration runs inside its own transaction.
pub fn applyMigrations(
    db: *Db,
    migrations: []const Migration,
    version_table: [:0]const u8,
) !void {
    var current = getVersion(db, version_table);

    for (migrations) |*m| {
        if (m.version <= current) continue;

        try db.exec("BEGIN TRANSACTION;");
        errdefer db.exec("ROLLBACK;") catch {};

        try m.up(db);
        try setVersion(db, version_table, m.version, m.description);
        try db.exec("COMMIT;");

        current = m.version;
    }
}

// ---------------------------------------------------------------------------
// Public convenience wrappers
// ---------------------------------------------------------------------------

/// Apply all api_calls.db schema migrations.
pub fn applyApiCallsMigrations(db: *Db) !void {
    return applyMigrations(db, &API_CALLS_MIGRATIONS, "schema_version");
}

/// Apply token_usage.db schema migrations.
/// Currently a no-op (no migrations defined yet for the token_usage DB),
/// but the plumbing is in place for future additions.
pub fn applyTokenUsageMigrations(_: *Db) !void {
    // No token-usage migrations defined yet.
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "Db open/close in-memory" {
    var db = try Db.open(":memory:");
    defer db.close();
}

test "Db exec create and insert" {
    var db = try Db.open(":memory:");
    defer db.close();

    try db.exec("CREATE TABLE t (x INTEGER);");
    try db.exec("INSERT INTO t VALUES (42);");
}

test "applyApiCallsMigrations creates schema_version table" {
    var db = try Db.open(":memory:");
    defer db.close();

    // Create minimal api_calls table so migrations can run.
    try db.exec(
        "CREATE TABLE api_calls (" ++
            "id INTEGER PRIMARY KEY AUTOINCREMENT," ++
            "timestamp TEXT NOT NULL," ++
            "api_base_url TEXT NOT NULL," ++
            "request_json TEXT NOT NULL," ++
            "response_json TEXT," ++
            "model TEXT NOT NULL," ++
            "status TEXT NOT NULL," ++
            "http_status INTEGER," ++
            "error_message TEXT," ++
            "duration_ms INTEGER," ++
            "tool_count INTEGER DEFAULT 0," ++
            "created_at INTEGER NOT NULL" ++
            ");",
    );

    try applyApiCallsMigrations(&db);

    // Verify version table exists and has entries.
    const v = getVersion(&db, "schema_version");
    try std.testing.expect(v >= 3);
}

test "applyTokenUsageMigrations is a no-op" {
    var db = try Db.open(":memory:");
    defer db.close();
    // Should not error.
    try applyTokenUsageMigrations(&db);
}

test "getVersion returns 0 for fresh database" {
    var db = try Db.open(":memory:");
    defer db.close();
    const v = getVersion(&db, "schema_version");
    try std.testing.expectEqual(@as(i32, 0), v);
}

test "migrations are idempotent" {
    var db = try Db.open(":memory:");
    defer db.close();

    try db.exec(
        "CREATE TABLE api_calls (" ++
            "id INTEGER PRIMARY KEY AUTOINCREMENT," ++
            "timestamp TEXT NOT NULL," ++
            "api_base_url TEXT NOT NULL," ++
            "request_json TEXT NOT NULL," ++
            "response_json TEXT," ++
            "model TEXT NOT NULL," ++
            "status TEXT NOT NULL," ++
            "http_status INTEGER," ++
            "error_message TEXT," ++
            "duration_ms INTEGER," ++
            "tool_count INTEGER DEFAULT 0," ++
            "created_at INTEGER NOT NULL" ++
            ");",
    );

    // Run twice — should not fail.
    try applyApiCallsMigrations(&db);
    try applyApiCallsMigrations(&db);

    const v = getVersion(&db, "schema_version");
    try std.testing.expect(v >= 3);
}
