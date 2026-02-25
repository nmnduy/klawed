//! memory_db.zig — SQLite FTS5 persistent memory store
//!
//! Zig port of src/memory_db.c.
//!
//! Stores named memory "cards" (entity + slot + value) in SQLite with FTS5
//! full-text search.  The global singleton from the C version is omitted here;
//! callers create explicit `MemoryDb` instances.

const std = @import("std");
const migrations = @import("migrations.zig");
const sqlite = @import("sqlite.zig");
const c = sqlite.c;

// ---------------------------------------------------------------------------
// Public enums
// ---------------------------------------------------------------------------

pub const MemoryKind = enum(i32) {
    fact = 0,
    preference = 1,
    event = 2,
    profile = 3,
    relationship = 4,
    goal = 5,

    pub fn toString(self: MemoryKind) []const u8 {
        return switch (self) {
            .fact => "fact",
            .preference => "preference",
            .event => "event",
            .profile => "profile",
            .relationship => "relationship",
            .goal => "goal",
        };
    }

    pub fn fromString(s: []const u8) MemoryKind {
        if (std.mem.eql(u8, s, "preference")) return .preference;
        if (std.mem.eql(u8, s, "event")) return .event;
        if (std.mem.eql(u8, s, "profile")) return .profile;
        if (std.mem.eql(u8, s, "relationship")) return .relationship;
        if (std.mem.eql(u8, s, "goal")) return .goal;
        return .fact;
    }
};

pub const MemoryRelation = enum(i32) {
    sets = 0,
    updates = 1,
    extends = 2,
    retracts = 3,

    pub fn toString(self: MemoryRelation) []const u8 {
        return switch (self) {
            .sets => "sets",
            .updates => "updates",
            .extends => "extends",
            .retracts => "retracts",
        };
    }

    pub fn fromString(s: []const u8) MemoryRelation {
        if (std.mem.eql(u8, s, "updates")) return .updates;
        if (std.mem.eql(u8, s, "extends")) return .extends;
        if (std.mem.eql(u8, s, "retracts")) return .retracts;
        return .sets;
    }
};

// ---------------------------------------------------------------------------
// MemoryCard
// ---------------------------------------------------------------------------

pub const MemoryCard = struct {
    id: i64,
    entity: []const u8, // owned
    slot: []const u8, // owned
    value: []const u8, // owned
    kind: MemoryKind,
    relation: MemoryRelation,
    timestamp: []const u8, // owned
    score: f64 = 0.0,

    pub fn deinit(self: *MemoryCard, allocator: std.mem.Allocator) void {
        allocator.free(self.entity);
        allocator.free(self.slot);
        allocator.free(self.value);
        allocator.free(self.timestamp);
    }
};

// ---------------------------------------------------------------------------
// SQL schema
// ---------------------------------------------------------------------------

const BASE_SCHEMA: [:0]const u8 =
    \\CREATE TABLE IF NOT EXISTS memories (
    \\    id INTEGER PRIMARY KEY AUTOINCREMENT,
    \\    entity TEXT NOT NULL,
    \\    slot TEXT NOT NULL,
    \\    value TEXT NOT NULL,
    \\    kind INTEGER NOT NULL DEFAULT 0,
    \\    relation INTEGER NOT NULL DEFAULT 0,
    \\    timestamp TEXT NOT NULL,
    \\    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
    \\);
    \\CREATE INDEX IF NOT EXISTS idx_memories_entity ON memories(entity);
    \\CREATE INDEX IF NOT EXISTS idx_memories_entity_slot ON memories(entity, slot);
    \\CREATE INDEX IF NOT EXISTS idx_memories_timestamp ON memories(timestamp);
    \\CREATE TABLE IF NOT EXISTS memory_metadata (
    \\    key TEXT PRIMARY KEY,
    \\    value TEXT NOT NULL
    \\);
;

/// FTS5 virtual table + triggers for automatic indexing.
const FTS_SCHEMA: [:0]const u8 =
    \\CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5(
    \\    content,
    \\    entity,
    \\    slot,
    \\    content_rowid=rowid
    \\);
    \\CREATE TRIGGER IF NOT EXISTS memories_fts_insert AFTER INSERT ON memories BEGIN
    \\    INSERT INTO memories_fts(rowid, content, entity, slot)
    \\    VALUES (new.id, new.value, new.entity, new.slot);
    \\END;
    \\CREATE TRIGGER IF NOT EXISTS memories_fts_delete AFTER DELETE ON memories BEGIN
    \\    INSERT INTO memories_fts(memories_fts, rowid, content, entity, slot)
    \\    VALUES ('delete', old.id, old.value, old.entity, old.slot);
    \\END;
;

// ---------------------------------------------------------------------------
// MemoryDb
// ---------------------------------------------------------------------------

pub const MemoryDb = struct {
    db: *c.sqlite3,
    db_path: []const u8, // owned
    has_fts: bool,
    allocator: std.mem.Allocator,

    /// Open (or create) a memory database.
    pub fn init(allocator: std.mem.Allocator, db_path: []const u8) !MemoryDb {
        if (std.fs.path.dirname(db_path)) |dir| {
            std.fs.cwd().makePath(dir) catch {};
        }

        const path_z = try allocator.dupeZ(u8, db_path);
        defer allocator.free(path_z);

        var handle: ?*c.sqlite3 = null;
        if (c.sqlite3_open(path_z.ptr, &handle) != c.SQLITE_OK) {
            if (handle) |h| _ = c.sqlite3_close(h);
            return migrations.SqliteError.SqliteOpen;
        }

        var self = MemoryDb{
            .db = handle.?,
            .db_path = try allocator.dupe(u8, db_path),
            .has_fts = false,
            .allocator = allocator,
        };

        _ = c.sqlite3_exec(self.db, "PRAGMA foreign_keys = ON;", null, null, null);

        // Base schema.
        try self.execRaw(BASE_SCHEMA);

        // FTS5 schema (optional — not all SQLite builds have it).
        self.has_fts = blk: {
            var errmsg: ?[*:0]u8 = null;
            const rc = c.sqlite3_exec(self.db, FTS_SCHEMA.ptr, null, null, &errmsg);
            if (rc != c.SQLITE_OK) {
                if (errmsg) |msg| c.sqlite3_free(msg);
                break :blk false;
            }
            break :blk true;
        };

        // Record schema version.
        self.execRaw(
            "INSERT OR REPLACE INTO memory_metadata (key, value) VALUES ('schema_version', '1');",
        ) catch {};

        return self;
    }

    pub fn deinit(self: *MemoryDb) void {
        _ = c.sqlite3_close(self.db);
        self.allocator.free(self.db_path);
        self.* = undefined;
    }

    // -----------------------------------------------------------------------
    // Store
    // -----------------------------------------------------------------------

    /// Insert a memory card.  Returns the new row id.
    pub fn store(
        self: *MemoryDb,
        entity: []const u8,
        slot: []const u8,
        value: []const u8,
        kind: MemoryKind,
        relation: MemoryRelation,
    ) !i64 {
        const sql: [:0]const u8 =
            \\INSERT INTO memories (entity, slot, value, kind, relation, timestamp)
            \\  VALUES (?, ?, ?, ?, ?, ?);
        ;

        const stmt = try self.prepareRaw(sql);
        defer _ = c.sqlite3_finalize(stmt);

        bindText(stmt, 1, entity);
        bindText(stmt, 2, slot);
        bindText(stmt, 3, value);
        _ = c.sqlite3_bind_int(stmt, 4, @intFromEnum(kind));
        _ = c.sqlite3_bind_int(stmt, 5, @intFromEnum(relation));

        var ts_buf: [32]u8 = undefined;
        const ts = isoTimestampZ(&ts_buf);
        _ = c.sqlite3_bind_text(stmt, 6, ts.ptr, @intCast(ts.len), c.SQLITE_TRANSIENT);

        if (c.sqlite3_step(stmt) != c.SQLITE_DONE) return migrations.SqliteError.SqliteStep;
        return c.sqlite3_last_insert_rowid(self.db);
    }

    // -----------------------------------------------------------------------
    // Recall
    // -----------------------------------------------------------------------

    /// Get the most-recent non-retracted value for entity:slot.
    /// Returns null when not found.  Caller owns the returned card.
    pub fn recall(
        self: *MemoryDb,
        entity: []const u8,
        slot: []const u8,
    ) !?MemoryCard {
        const sql: [:0]const u8 =
            \\SELECT id, entity, slot, value, kind, relation, timestamp
            \\  FROM memories WHERE entity = ? AND slot = ?
            \\  ORDER BY id DESC LIMIT 1;
        ;

        const stmt = try self.prepareRaw(sql);
        defer _ = c.sqlite3_finalize(stmt);

        bindText(stmt, 1, entity);
        bindText(stmt, 2, slot);

        if (c.sqlite3_step(stmt) != c.SQLITE_ROW) return null;

        const relation: MemoryRelation = @enumFromInt(c.sqlite3_column_int(stmt, 5));
        if (relation == .retracts) return null; // retracted

        return try rowToCard(self.allocator, stmt);
    }

    // -----------------------------------------------------------------------
    // Retract
    // -----------------------------------------------------------------------

    /// Retract (logically delete) a memory slot by inserting a `retracts`
    /// record.  Returns the new row id of the retraction record.
    pub fn retract(
        self: *MemoryDb,
        entity: []const u8,
        slot: []const u8,
    ) !i64 {
        return self.store(entity, slot, "(retracted)", .fact, .retracts);
    }

    // -----------------------------------------------------------------------
    // Search
    // -----------------------------------------------------------------------

    /// Search memories using FTS5 (or a LIKE fallback).
    /// Caller owns the returned slice; free with `freeCards`.
    pub fn search(
        self: *MemoryDb,
        allocator: std.mem.Allocator,
        query: []const u8,
        top_k: u32,
    ) ![]MemoryCard {
        if (self.has_fts) {
            return self.ftsSearch(allocator, query, top_k);
        } else {
            return self.likeSearch(allocator, query, top_k);
        }
    }

    fn ftsSearch(
        self: *MemoryDb,
        allocator: std.mem.Allocator,
        query: []const u8,
        top_k: u32,
    ) ![]MemoryCard {
        const sql: [:0]const u8 =
            \\SELECT m.id, m.entity, m.slot, m.value, m.kind, m.relation, m.timestamp,
            \\       rank as score
            \\  FROM memories_fts
            \\  JOIN memories m ON memories_fts.rowid = m.id
            \\  WHERE memories_fts MATCH ?
            \\    AND m.relation != 3
            \\    AND NOT EXISTS (
            \\        SELECT 1 FROM memories m2
            \\        WHERE m2.entity = m.entity AND m2.slot = m.slot
            \\          AND m2.relation = 3 AND m2.id > m.id)
            \\  ORDER BY rank LIMIT ?;
        ;

        const stmt = try self.prepareRaw(sql);
        defer _ = c.sqlite3_finalize(stmt);

        // Enhance query with prefix wildcards.
        const enhanced = try enhanceFts5Query(allocator, query);
        defer allocator.free(enhanced);
        const enhanced_z = try allocator.dupeZ(u8, enhanced);
        defer allocator.free(enhanced_z);

        _ = c.sqlite3_bind_text(stmt, 1, enhanced_z.ptr, -1, c.SQLITE_STATIC);
        _ = c.sqlite3_bind_int(stmt, 2, @intCast(top_k));

        return collectCardRows(allocator, stmt, true);
    }

    fn likeSearch(
        self: *MemoryDb,
        allocator: std.mem.Allocator,
        query: []const u8,
        top_k: u32,
    ) ![]MemoryCard {
        const sql: [:0]const u8 =
            \\SELECT id, entity, slot, value, kind, relation, timestamp, 0.0 as score
            \\  FROM memories m
            \\  WHERE (value LIKE ? OR entity LIKE ? OR slot LIKE ?)
            \\    AND relation != 3
            \\    AND NOT EXISTS (
            \\        SELECT 1 FROM memories m2
            \\        WHERE m2.entity = m.entity AND m2.slot = m.slot
            \\          AND m2.relation = 3 AND m2.id > m.id)
            \\  ORDER BY id DESC LIMIT ?;
        ;

        const stmt = try self.prepareRaw(sql);
        defer _ = c.sqlite3_finalize(stmt);

        const pattern = try std.fmt.allocPrint(allocator, "%{s}%", .{query});
        defer allocator.free(pattern);
        const pattern_z = try allocator.dupeZ(u8, pattern);
        defer allocator.free(pattern_z);

        _ = c.sqlite3_bind_text(stmt, 1, pattern_z.ptr, -1, c.SQLITE_STATIC);
        _ = c.sqlite3_bind_text(stmt, 2, pattern_z.ptr, -1, c.SQLITE_STATIC);
        _ = c.sqlite3_bind_text(stmt, 3, pattern_z.ptr, -1, c.SQLITE_STATIC);
        _ = c.sqlite3_bind_int(stmt, 4, @intCast(top_k));

        return collectCardRows(allocator, stmt, true);
    }

    // -----------------------------------------------------------------------
    // Entity memories
    // -----------------------------------------------------------------------

    pub fn getEntityMemories(
        self: *MemoryDb,
        allocator: std.mem.Allocator,
        entity: []const u8,
    ) ![]MemoryCard {
        const sql: [:0]const u8 =
            \\SELECT m.id, m.entity, m.slot, m.value, m.kind, m.relation, m.timestamp
            \\  FROM memories m
            \\  WHERE m.entity = ? AND m.relation != 3
            \\    AND NOT EXISTS (
            \\        SELECT 1 FROM memories m2
            \\        WHERE m2.entity = m.entity AND m2.slot = m.slot
            \\          AND m2.relation = 3 AND m2.id > m.id)
            \\  ORDER BY m.id DESC;
        ;

        const stmt = try self.prepareRaw(sql);
        defer _ = c.sqlite3_finalize(stmt);

        bindText(stmt, 1, entity);
        return collectCardRows(allocator, stmt, false);
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    pub fn freeCards(allocator: std.mem.Allocator, cards: []MemoryCard) void {
        for (cards) |*card| card.deinit(allocator);
        allocator.free(cards);
    }

    fn execRaw(self: *MemoryDb, sql: [:0]const u8) !void {
        var errmsg: ?[*:0]u8 = null;
        if (c.sqlite3_exec(self.db, sql.ptr, null, null, &errmsg) != c.SQLITE_OK) {
            if (errmsg) |msg| c.sqlite3_free(msg);
            return migrations.SqliteError.SqliteExec;
        }
    }

    fn prepareRaw(self: *MemoryDb, sql: [:0]const u8) !*c.sqlite3_stmt {
        var stmt: ?*c.sqlite3_stmt = null;
        if (c.sqlite3_prepare_v2(self.db, sql.ptr, -1, &stmt, null) != c.SQLITE_OK) {
            return migrations.SqliteError.SqlitePrepare;
        }
        return stmt.?;
    }
};

// ---------------------------------------------------------------------------
// Private free-standing helpers
// ---------------------------------------------------------------------------

fn bindText(stmt: *c.sqlite3_stmt, idx: c_int, s: []const u8) void {
    _ = c.sqlite3_bind_text(stmt, idx, s.ptr, @intCast(s.len), c.SQLITE_TRANSIENT);
}

fn columnSlice(stmt: *c.sqlite3_stmt, col: c_int) []const u8 {
    const ptr = c.sqlite3_column_text(stmt, col);
    if (ptr == null) return "";
    return std.mem.sliceTo(ptr, 0);
}

fn rowToCard(allocator: std.mem.Allocator, stmt: *c.sqlite3_stmt) !MemoryCard {
    return MemoryCard{
        .id = c.sqlite3_column_int64(stmt, 0),
        .entity = try allocator.dupe(u8, columnSlice(stmt, 1)),
        .slot = try allocator.dupe(u8, columnSlice(stmt, 2)),
        .value = try allocator.dupe(u8, columnSlice(stmt, 3)),
        .kind = @enumFromInt(c.sqlite3_column_int(stmt, 4)),
        .relation = @enumFromInt(c.sqlite3_column_int(stmt, 5)),
        .timestamp = try allocator.dupe(u8, columnSlice(stmt, 6)),
        .score = 0.0,
    };
}

fn collectCardRows(
    allocator: std.mem.Allocator,
    stmt: *c.sqlite3_stmt,
    has_score: bool,
) ![]MemoryCard {
    var cards = std.ArrayList(MemoryCard).init(allocator);
    errdefer {
        for (cards.items) |*card| card.deinit(allocator);
        cards.deinit();
    }

    var rc = c.sqlite3_step(stmt);
    while (rc == c.SQLITE_ROW) {
        var card = try rowToCard(allocator, stmt);
        if (has_score) {
            card.score = c.sqlite3_column_double(stmt, 7);
        }
        try cards.append(card);
        rc = c.sqlite3_step(stmt);
    }

    return cards.toOwnedSlice();
}

/// Add `*` suffix to alphanumeric FTS5 query terms for prefix matching.
fn enhanceFts5Query(allocator: std.mem.Allocator, query: []const u8) ![]u8 {
    var buf = std.ArrayList(u8).init(allocator);
    errdefer buf.deinit();

    var in_quotes = false;
    var term_start: ?usize = null;
    var i: usize = 0;

    while (i <= query.len) : (i += 1) {
        const ch: u8 = if (i < query.len) query[i] else 0;

        if (ch == '"') {
            in_quotes = !in_quotes;
            if (term_start != null) {
                try buf.appendSlice(query[term_start.?..i]);
                term_start = null;
            }
            try buf.append(ch);
            continue;
        }

        if (in_quotes) {
            if (term_start == null) term_start = i;
            continue;
        }

        if (ch == ' ' or ch == '\t' or ch == '\n' or ch == 0) {
            if (term_start) |ts| {
                const term = query[ts..i];
                // Check for FTS5 operators.
                const has_op = std.mem.indexOfAny(u8, term, ":*^") != null or
                    std.ascii.eqlIgnoreCase(term, "and") or
                    std.ascii.eqlIgnoreCase(term, "or") or
                    std.ascii.eqlIgnoreCase(term, "not");

                try buf.appendSlice(term);
                if (!has_op and term.len > 0 and std.ascii.isAlphanumeric(term[term.len - 1])) {
                    try buf.append('*');
                }
                term_start = null;
            }
            if (ch != 0) try buf.append(ch);
        } else {
            if (term_start == null) term_start = i;
        }
    }

    return buf.toOwnedSlice();
}

fn isoTimestampZ(buf: *[32]u8) [:0]const u8 {
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
    buf[len + 1 + ml + 1 + dl] = 'T';
    const base = len + 1 + ml + 1 + dl + 1;
    const hl = std.fmt.formatIntBuf(buf[base .. base + 2], dt.getHoursIntoDay(), 10, .lower, .{ .width = 2, .fill = '0' });
    buf[base + hl] = ':';
    const minl = std.fmt.formatIntBuf(buf[base + hl + 1 .. base + hl + 3], dt.getMinutesIntoHour(), 10, .lower, .{ .width = 2, .fill = '0' });
    buf[base + hl + 1 + minl] = ':';
    const sl = std.fmt.formatIntBuf(buf[base + hl + 1 + minl + 1 ..][0..2], dt.getSecondsIntoMinute(), 10, .lower, .{ .width = 2, .fill = '0' });
    buf[base + hl + 1 + minl + 1 + sl] = 'Z';
    buf[base + hl + 1 + minl + 1 + sl + 1] = 0;
    return buf[0 .. base + hl + 1 + minl + 1 + sl + 1 :0];
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "MemoryDb init and deinit" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "memory.db" });
    defer allocator.free(p);

    var mdb = try MemoryDb.init(allocator, p);
    defer mdb.deinit();
}

test "MemoryDb store and recall" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "memory2.db" });
    defer allocator.free(p);

    var mdb = try MemoryDb.init(allocator, p);
    defer mdb.deinit();

    const id = try mdb.store("user", "name", "Alice", .fact, .sets);
    try std.testing.expect(id > 0);

    var card = (try mdb.recall("user", "name")).?;
    defer card.deinit(allocator);
    try std.testing.expectEqualStrings("Alice", card.value);
    try std.testing.expectEqual(MemoryKind.fact, card.kind);
}

test "MemoryDb retract removes value" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "memory3.db" });
    defer allocator.free(p);

    var mdb = try MemoryDb.init(allocator, p);
    defer mdb.deinit();

    _ = try mdb.store("user", "age", "30", .fact, .sets);
    _ = try mdb.retract("user", "age");

    const result = try mdb.recall("user", "age");
    try std.testing.expectEqual(@as(?MemoryCard, null), result);
}

test "MemoryDb search" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "memory4.db" });
    defer allocator.free(p);

    var mdb = try MemoryDb.init(allocator, p);
    defer mdb.deinit();

    _ = try mdb.store("user", "language", "Zig", .preference, .sets);
    _ = try mdb.store("user", "hobby", "programming", .fact, .sets);

    const cards = try mdb.search(allocator, "Zig", 10);
    defer MemoryDb.freeCards(allocator, cards);

    try std.testing.expect(cards.len >= 1);
    try std.testing.expectEqualStrings("Zig", cards[0].value);
}

test "MemoryDb getEntityMemories" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "memory5.db" });
    defer allocator.free(p);

    var mdb = try MemoryDb.init(allocator, p);
    defer mdb.deinit();

    _ = try mdb.store("project.klawed", "lang", "C", .fact, .sets);
    _ = try mdb.store("project.klawed", "version", "0.29", .fact, .sets);

    const cards = try mdb.getEntityMemories(allocator, "project.klawed");
    defer MemoryDb.freeCards(allocator, cards);

    try std.testing.expect(cards.len >= 2);
}

test "MemoryKind and MemoryRelation round-trip" {
    try std.testing.expectEqualStrings("fact", MemoryKind.fact.toString());
    try std.testing.expectEqualStrings("goal", MemoryKind.goal.toString());
    try std.testing.expectEqual(MemoryKind.preference, MemoryKind.fromString("preference"));
    try std.testing.expectEqual(MemoryKind.fact, MemoryKind.fromString("unknown"));

    try std.testing.expectEqualStrings("sets", MemoryRelation.sets.toString());
    try std.testing.expectEqualStrings("retracts", MemoryRelation.retracts.toString());
    try std.testing.expectEqual(MemoryRelation.extends, MemoryRelation.fromString("extends"));
    try std.testing.expectEqual(MemoryRelation.sets, MemoryRelation.fromString("unknown"));
}

test "enhanceFts5Query adds wildcards" {
    const allocator = std.testing.allocator;
    const enhanced = try enhanceFts5Query(allocator, "hello world");
    defer allocator.free(enhanced);
    try std.testing.expect(std.mem.indexOf(u8, enhanced, "hello*") != null);
    try std.testing.expect(std.mem.indexOf(u8, enhanced, "world*") != null);
}

test "enhanceFts5Query leaves quoted phrases alone" {
    const allocator = std.testing.allocator;
    const enhanced = try enhanceFts5Query(allocator, "\"exact phrase\"");
    defer allocator.free(enhanced);
    // Quoted content should pass through without wildcards added to internal words.
    try std.testing.expect(std.mem.indexOf(u8, enhanced, "\"exact phrase\"") != null);
}
