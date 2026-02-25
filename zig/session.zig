//! session.zig — Session management and persistence
//!
//! Zig port of src/session.c and src/session/session_persistence.c.
//!
//! A "session" is a group of API calls sharing the same `session_id` string.
//! This module provides helpers to:
//!   - list sessions stored in the api_calls database
//!   - retrieve basic metadata (start time, model, call count) for a session
//!
//! The heavy conversational state reconstruction (`session_load_from_db`)
//! depends on JSON parsing of full request/response bodies and will be
//! completed in Phase 6 once the conversation layer is ported.
//! Here we provide a lightweight version that returns raw JSON rows.

const std = @import("std");
const persistence = @import("persistence.zig");
const migrations = @import("migrations.zig");
const sqlite = @import("sqlite.zig");
const c = sqlite.c;

// ---------------------------------------------------------------------------
// SessionInfo
// ---------------------------------------------------------------------------

pub const SessionInfo = struct {
    session_id: []const u8, // owned
    started_at: ?[]const u8, // ISO timestamp — owned, may be null
    model: ?[]const u8, // owned, may be null
    call_count: i32,

    pub fn deinit(self: *SessionInfo, allocator: std.mem.Allocator) void {
        allocator.free(self.session_id);
        if (self.started_at) |t| allocator.free(t);
        if (self.model) |m| allocator.free(m);
    }
};

// ---------------------------------------------------------------------------
// List sessions
// ---------------------------------------------------------------------------

/// Return the `limit` most-recently-active sessions from `pdb`.
/// Pass `limit = 0` for no limit.
/// Caller owns the returned slice; free with `freeSessions`.
pub fn listSessions(
    allocator: std.mem.Allocator,
    pdb: *persistence.PersistenceDb,
    limit: u32,
) ![]SessionInfo {
    const sql_no_limit: [:0]const u8 =
        \\SELECT session_id,
        \\       MIN(timestamp) AS started_at,
        \\       model,
        \\       COUNT(*) AS call_count
        \\  FROM api_calls
        \\  WHERE session_id IS NOT NULL
        \\  GROUP BY session_id
        \\  ORDER BY MAX(created_at) DESC;
    ;
    const sql_with_limit: [:0]const u8 =
        \\SELECT session_id,
        \\       MIN(timestamp) AS started_at,
        \\       model,
        \\       COUNT(*) AS call_count
        \\  FROM api_calls
        \\  WHERE session_id IS NOT NULL
        \\  GROUP BY session_id
        \\  ORDER BY MAX(created_at) DESC
        \\  LIMIT ?;
    ;

    pdb.mutex.lock();
    defer pdb.mutex.unlock();

    const sql = if (limit > 0) sql_with_limit else sql_no_limit;
    var raw_stmt: ?*c.sqlite3_stmt = null;
    if (c.sqlite3_prepare_v2(pdb.db, sql.ptr, -1, &raw_stmt, null) != c.SQLITE_OK) {
        return migrations.SqliteError.SqlitePrepare;
    }
    const stmt = raw_stmt.?;
    defer _ = c.sqlite3_finalize(stmt);

    if (limit > 0) {
        _ = c.sqlite3_bind_int(stmt, 1, @intCast(limit));
    }

    var list = std.ArrayList(SessionInfo).init(allocator);
    errdefer {
        for (list.items) |*info| info.deinit(allocator);
        list.deinit();
    }

    while (c.sqlite3_step(stmt) == c.SQLITE_ROW) {
        const sid_raw = c.sqlite3_column_text(stmt, 0);
        if (sid_raw == null) continue;
        const sid = std.mem.sliceTo(sid_raw, 0);

        const ts_raw = c.sqlite3_column_text(stmt, 1);
        const model_raw = c.sqlite3_column_text(stmt, 2);

        const info = SessionInfo{
            .session_id = try allocator.dupe(u8, sid),
            .started_at = if (ts_raw != null)
                try allocator.dupe(u8, std.mem.sliceTo(ts_raw, 0))
            else
                null,
            .model = if (model_raw != null)
                try allocator.dupe(u8, std.mem.sliceTo(model_raw, 0))
            else
                null,
            .call_count = c.sqlite3_column_int(stmt, 3),
        };
        try list.append(info);
    }

    return list.toOwnedSlice();
}

/// Free a session list returned by `listSessions`.
pub fn freeSessions(allocator: std.mem.Allocator, sessions: []SessionInfo) void {
    for (sessions) |*info| info.deinit(allocator);
    allocator.free(sessions);
}

// ---------------------------------------------------------------------------
// Get the most-recent session id
// ---------------------------------------------------------------------------

/// Return the session_id of the most-recent API call.
/// Caller owns the returned string.
pub fn getMostRecentSessionId(
    allocator: std.mem.Allocator,
    pdb: *persistence.PersistenceDb,
) !?[]const u8 {
    const sql: [:0]const u8 =
        \\SELECT session_id FROM api_calls
        \\WHERE session_id IS NOT NULL
        \\ORDER BY created_at DESC LIMIT 1;
    ;

    pdb.mutex.lock();
    defer pdb.mutex.unlock();

    var raw_stmt: ?*c.sqlite3_stmt = null;
    if (c.sqlite3_prepare_v2(pdb.db, sql.ptr, -1, &raw_stmt, null) != c.SQLITE_OK) {
        return migrations.SqliteError.SqlitePrepare;
    }
    const stmt = raw_stmt.?;
    defer _ = c.sqlite3_finalize(stmt);

    if (c.sqlite3_step(stmt) != c.SQLITE_ROW) return null;

    const raw = c.sqlite3_column_text(stmt, 0);
    if (raw == null) return null;
    return try allocator.dupe(u8, std.mem.sliceTo(raw, 0));
}

// ---------------------------------------------------------------------------
// Print session list (like the C version's session_list_sessions)
// ---------------------------------------------------------------------------

/// Print an ASCII table of available sessions to stdout.
pub fn printSessionList(
    allocator: std.mem.Allocator,
    pdb: *persistence.PersistenceDb,
    limit: u32,
) !void {
    const writer = std.io.getStdOut().writer();

    const sessions = try listSessions(allocator, pdb, limit);
    defer freeSessions(allocator, sessions);

    if (sessions.len == 0) {
        try writer.writeAll("No sessions found in database.\n");
        return;
    }

    try writer.writeAll("\n");
    try writer.writeAll("=================================================================\n");
    try writer.writeAll("                    AVAILABLE SESSIONS\n");
    try writer.writeAll("=================================================================\n");
    try writer.print("{s:<40} {s:<20} {s:<15} {s}\n", .{ "Session ID", "Started", "Model", "Calls" });
    try writer.writeAll("-----------------------------------------------------------------\n");

    for (sessions) |info| {
        const sid_display = if (info.session_id.len > 40)
            info.session_id[0..37]
        else
            info.session_id;

        try writer.print("{s:<40} {s:<20} {s:<15} {d}\n", .{
            sid_display,
            info.started_at orelse "unknown",
            info.model orelse "unknown",
            info.call_count,
        });
    }

    try writer.writeAll("-----------------------------------------------------------------\n");
    try writer.print("Total: {d} session(s)\n", .{sessions.len});
    try writer.writeAll("\nTo resume a session, use: klawed --resume <session_id>\n");
    try writer.writeAll("=================================================================\n\n");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "listSessions empty database" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "session_test.db" });
    defer allocator.free(p);

    var pdb = try persistence.PersistenceDb.init(allocator, p);
    defer pdb.deinit();

    const sessions = try listSessions(allocator, &pdb, 0);
    defer freeSessions(allocator, sessions);

    try std.testing.expectEqual(@as(usize, 0), sessions.len);
}

test "listSessions with data" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "session_test2.db" });
    defer allocator.free(p);

    var pdb = try persistence.PersistenceDb.init(allocator, p);
    defer pdb.deinit();

    _ = try pdb.logApiCall(.{
        .session_id = "sess-alpha",
        .api_base_url = "https://api.openai.com",
        .request_json = "{}",
        .model = "gpt-4",
        .status = "success",
    });
    _ = try pdb.logApiCall(.{
        .session_id = "sess-beta",
        .api_base_url = "https://api.openai.com",
        .request_json = "{}",
        .model = "gpt-3.5-turbo",
        .status = "success",
    });

    const sessions = try listSessions(allocator, &pdb, 0);
    defer freeSessions(allocator, sessions);

    try std.testing.expect(sessions.len >= 2);
}

test "getMostRecentSessionId" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "session_test3.db" });
    defer allocator.free(p);

    var pdb = try persistence.PersistenceDb.init(allocator, p);
    defer pdb.deinit();

    // Empty — should return null.
    const before = try getMostRecentSessionId(allocator, &pdb);
    try std.testing.expectEqual(@as(?[]const u8, null), before);

    _ = try pdb.logApiCall(.{
        .session_id = "sess-most-recent",
        .api_base_url = "https://api.openai.com",
        .request_json = "{}",
        .model = "gpt-4",
        .status = "success",
    });

    const after = try getMostRecentSessionId(allocator, &pdb);
    defer if (after) |s| allocator.free(s);
    try std.testing.expectEqualStrings("sess-most-recent", after.?);
}

test "listSessions respects limit" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "session_test4.db" });
    defer allocator.free(p);

    var pdb = try persistence.PersistenceDb.init(allocator, p);
    defer pdb.deinit();

    var i: usize = 0;
    while (i < 5) : (i += 1) {
        const sid = try std.fmt.allocPrint(allocator, "sess-{d}", .{i});
        defer allocator.free(sid);
        _ = try pdb.logApiCall(.{
            .session_id = sid,
            .api_base_url = "https://api.openai.com",
            .request_json = "{}",
            .model = "gpt-4",
            .status = "success",
        });
    }

    const sessions = try listSessions(allocator, &pdb, 3);
    defer freeSessions(allocator, sessions);
    try std.testing.expectEqual(@as(usize, 3), sessions.len);
}
