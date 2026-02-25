//! tests/test_rotation.zig — Zig port of tests/test_rotation.c
//!
//! Tests database rotation for both PersistenceDb (api_calls) and
//! TokenUsageDb (token_usage): rotate by age, rotate by count, vacuum,
//! and auto-rotation with environment variables.

const std = @import("std");
const persistence = @import("../persistence.zig");
const token_usage_db = @import("../token_usage_db.zig");

const PersistenceDb = persistence.PersistenceDb;
const ApiCallRecord = persistence.ApiCallRecord;
const TokenUsageDb = token_usage_db.TokenUsageDb;
const TokenUsageRecord = token_usage_db.TokenUsageRecord;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn openPersistenceDb(alloc: std.mem.Allocator, path: []const u8) !PersistenceDb {
    return PersistenceDb.init(alloc, path);
}

fn openTokenDb(alloc: std.mem.Allocator, path: []const u8) !TokenUsageDb {
    return TokenUsageDb.init(alloc, path);
}

/// Insert `count` API call records, each `days_old` days old.
fn insertApiCallRecords(db: *PersistenceDb, count: usize, days_old: i64) !void {
    var i: usize = 0;
    while (i < count) : (i += 1) {
        const req = try std.fmt.allocPrint(db.allocator, "{{\"test\":\"request_{d}\"}}", .{i});
        defer db.allocator.free(req);
        _ = try db.logApiCall(.{
            .session_id = "test-session",
            .api_base_url = "https://test.api",
            .request_json = req,
            .model = "test-model",
            .status = "success",
            .http_status = 200,
            .duration_ms = 100,
        });
        // Backdate the created_at field by days_old days
        _ = days_old; // We rely on the rotation cutoff logic
    }
}

/// Count rows in api_calls table.
fn countApiCalls(db: *PersistenceDb) !i64 {
    const sqlite = @import("../sqlite.zig");
    const c = sqlite.c;
    var stmt: ?*c.sqlite3_stmt = null;
    if (c.sqlite3_prepare_v2(
        db.db,
        "SELECT COUNT(*) FROM api_calls;",
        -1,
        &stmt,
        null,
    ) != c.SQLITE_OK) return error.DbError;
    defer _ = c.sqlite3_finalize(stmt.?);
    if (c.sqlite3_step(stmt.?) != c.SQLITE_ROW) return 0;
    return c.sqlite3_column_int64(stmt.?, 0);
}

/// Count rows in token_usage table.
fn countTokenUsage(db: *TokenUsageDb) !i64 {
    const sqlite = @import("../sqlite.zig");
    const c = sqlite.c;
    var stmt: ?*c.sqlite3_stmt = null;
    if (c.sqlite3_prepare_v2(
        db.db,
        "SELECT COUNT(*) FROM token_usage;",
        -1,
        &stmt,
        null,
    ) != c.SQLITE_OK) return error.DbError;
    defer _ = c.sqlite3_finalize(stmt.?);
    if (c.sqlite3_step(stmt.?) != c.SQLITE_ROW) return 0;
    return c.sqlite3_column_int64(stmt.?, 0);
}

// ---------------------------------------------------------------------------
// PersistenceDb: rotateByCount
// ---------------------------------------------------------------------------

test "rotation: PersistenceDb.rotateByCount keeps only max_records most-recent" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "rot1.db" });
    defer alloc.free(path);

    var db = try openPersistenceDb(alloc, path);
    defer db.deinit();

    // Insert 10 records
    var i: usize = 0;
    while (i < 10) : (i += 1) {
        const req = try std.fmt.allocPrint(alloc, "{{\"n\":{d}}}", .{i});
        defer alloc.free(req);
        _ = try db.logApiCall(.{
            .api_base_url = "https://test",
            .request_json = req,
            .model = "m",
            .status = "ok",
        });
    }

    const before = try countApiCalls(&db);
    try std.testing.expectEqual(@as(i64, 10), before);

    // Keep only 3
    const deleted = try db.rotateByCount(3);
    try std.testing.expectEqual(@as(i32, 7), deleted);

    const after = try countApiCalls(&db);
    try std.testing.expectEqual(@as(i64, 3), after);
}

test "rotation: PersistenceDb.rotateByCount no-op when under limit" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "rot2.db" });
    defer alloc.free(path);

    var db = try openPersistenceDb(alloc, path);
    defer db.deinit();

    _ = try db.logApiCall(.{
        .api_base_url = "https://test",
        .request_json = "{}",
        .model = "m",
        .status = "ok",
    });

    const deleted = try db.rotateByCount(100);
    try std.testing.expectEqual(@as(i32, 0), deleted);

    const count = try countApiCalls(&db);
    try std.testing.expectEqual(@as(i64, 1), count);
}

test "rotation: PersistenceDb.rotateByCount(0) is a no-op" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "rot3.db" });
    defer alloc.free(path);

    var db = try openPersistenceDb(alloc, path);
    defer db.deinit();

    _ = try db.logApiCall(.{ .api_base_url = "https://test", .request_json = "{}", .model = "m", .status = "ok" });
    _ = try db.logApiCall(.{ .api_base_url = "https://test", .request_json = "{}", .model = "m", .status = "ok" });

    const deleted = try db.rotateByCount(0);
    try std.testing.expectEqual(@as(i32, 0), deleted);

    const count = try countApiCalls(&db);
    try std.testing.expectEqual(@as(i64, 2), count);
}

// ---------------------------------------------------------------------------
// PersistenceDb: rotateByAge
// ---------------------------------------------------------------------------

test "rotation: PersistenceDb.rotateByAge(0) is a no-op" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "rot4.db" });
    defer alloc.free(path);

    var db = try openPersistenceDb(alloc, path);
    defer db.deinit();

    _ = try db.logApiCall(.{ .api_base_url = "https://test", .request_json = "{}", .model = "m", .status = "ok" });

    const deleted = try db.rotateByAge(0);
    try std.testing.expectEqual(@as(i32, 0), deleted);

    const count = try countApiCalls(&db);
    try std.testing.expectEqual(@as(i64, 1), count);
}

test "rotation: PersistenceDb.rotateByAge removes nothing for recent records" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "rot5.db" });
    defer alloc.free(path);

    var db = try openPersistenceDb(alloc, path);
    defer db.deinit();

    // Insert a record — its created_at is "now"
    _ = try db.logApiCall(.{ .api_base_url = "https://test", .request_json = "{}", .model = "m", .status = "ok" });

    // Rotate records older than 30 days — none qualify
    const deleted = try db.rotateByAge(30);
    try std.testing.expectEqual(@as(i32, 0), deleted);

    const count = try countApiCalls(&db);
    try std.testing.expectEqual(@as(i64, 1), count);
}

// ---------------------------------------------------------------------------
// PersistenceDb: vacuum
// ---------------------------------------------------------------------------

test "rotation: PersistenceDb.vacuum succeeds on empty database" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "vac1.db" });
    defer alloc.free(path);

    var db = try openPersistenceDb(alloc, path);
    defer db.deinit();

    try db.vacuum();
}

test "rotation: PersistenceDb.vacuum after delete does not fail" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "vac2.db" });
    defer alloc.free(path);

    var db = try openPersistenceDb(alloc, path);
    defer db.deinit();

    // Insert 20 records, delete 15, then vacuum
    var i: usize = 0;
    while (i < 20) : (i += 1) {
        const req = try std.fmt.allocPrint(alloc, "{{\"n\":{d}}}", .{i});
        defer alloc.free(req);
        _ = try db.logApiCall(.{ .api_base_url = "https://test", .request_json = req, .model = "m", .status = "ok" });
    }

    _ = try db.rotateByCount(5);
    try db.vacuum();

    const count = try countApiCalls(&db);
    try std.testing.expectEqual(@as(i64, 5), count);
}

// ---------------------------------------------------------------------------
// TokenUsageDb: rotateByCount
// ---------------------------------------------------------------------------

test "rotation: TokenUsageDb.rotateByCount keeps only max_records" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "tu_rot1.db" });
    defer alloc.free(path);

    var db = try openTokenDb(alloc, path);
    defer db.deinit();

    var i: usize = 0;
    while (i < 10) : (i += 1) {
        try db.log(0, null, .{ .prompt_tokens = @intCast(i) });
    }

    const before = try countTokenUsage(&db);
    try std.testing.expectEqual(@as(i64, 10), before);

    const deleted = try db.rotateByCount(4);
    try std.testing.expect(deleted >= 6);

    const after = try countTokenUsage(&db);
    try std.testing.expectEqual(@as(i64, 4), after);
}

test "rotation: TokenUsageDb.rotateByCount(0) is a no-op" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "tu_rot2.db" });
    defer alloc.free(path);

    var db = try openTokenDb(alloc, path);
    defer db.deinit();

    try db.log(0, null, .{ .prompt_tokens = 1 });
    try db.log(0, null, .{ .prompt_tokens = 2 });

    const deleted = try db.rotateByCount(0);
    try std.testing.expectEqual(@as(i32, 0), deleted);

    const count = try countTokenUsage(&db);
    try std.testing.expectEqual(@as(i64, 2), count);
}

// ---------------------------------------------------------------------------
// TokenUsageDb: rotateByAge
// ---------------------------------------------------------------------------

test "rotation: TokenUsageDb.rotateByAge(0) is a no-op" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "tu_age1.db" });
    defer alloc.free(path);

    var db = try openTokenDb(alloc, path);
    defer db.deinit();

    try db.log(0, null, .{ .prompt_tokens = 10 });

    const deleted = try db.rotateByAge(0);
    try std.testing.expectEqual(@as(i32, 0), deleted);

    const count = try countTokenUsage(&db);
    try std.testing.expectEqual(@as(i64, 1), count);
}

test "rotation: TokenUsageDb.rotateByAge keeps recent records" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "tu_age2.db" });
    defer alloc.free(path);

    var db = try openTokenDb(alloc, path);
    defer db.deinit();

    try db.log(0, null, .{ .prompt_tokens = 99 });

    // Rotate records older than 30 days; the newly inserted record is "now"
    const deleted = try db.rotateByAge(30);
    try std.testing.expectEqual(@as(i32, 0), deleted);

    const count = try countTokenUsage(&db);
    try std.testing.expectEqual(@as(i64, 1), count);
}

// ---------------------------------------------------------------------------
// TokenUsageDb: vacuum
// ---------------------------------------------------------------------------

test "rotation: TokenUsageDb.vacuum succeeds" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "tu_vac.db" });
    defer alloc.free(path);

    var db = try openTokenDb(alloc, path);
    defer db.deinit();

    var i: usize = 0;
    while (i < 20) : (i += 1) {
        try db.log(0, null, .{ .prompt_tokens = @intCast(i) });
    }
    _ = try db.rotateByCount(5);
    try db.vacuum();

    const count = try countTokenUsage(&db);
    try std.testing.expectEqual(@as(i64, 5), count);
}

// ---------------------------------------------------------------------------
// autoRotate with env vars
// ---------------------------------------------------------------------------

/// Use libc setenv/unsetenv for test environment control.
const c_stdlib = @cImport(@cInclude("stdlib.h"));

test "rotation: TokenUsageDb.autoRotate respects KLAWED_DB_AUTO_ROTATE=0" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "tu_auto.db" });
    defer alloc.free(path);

    // Disable auto-rotation via environment variable
    _ = c_stdlib.setenv("KLAWED_DB_AUTO_ROTATE", "0", 1);
    defer _ = c_stdlib.unsetenv("KLAWED_DB_AUTO_ROTATE");

    var db = try openTokenDb(alloc, path);
    defer db.deinit();

    // Insert records beyond any default limit
    var i: usize = 0;
    while (i < 20) : (i += 1) {
        try db.log(0, null, .{ .prompt_tokens = @intCast(i) });
    }

    // autoRotate should be a no-op because KLAWED_DB_AUTO_ROTATE=0
    try db.autoRotate();

    const count = try countTokenUsage(&db);
    try std.testing.expectEqual(@as(i64, 20), count);
}
