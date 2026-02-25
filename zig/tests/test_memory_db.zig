//! tests/test_memory_db.zig — Zig port of tests/test_memory_db.c
//!
//! Tests the MemoryDb API: kind/relation constants, string conversions,
//! store, recall, search, and entity-memory queries.

const std = @import("std");
const memory_db = @import("../memory_db.zig");

const MemoryDb = memory_db.MemoryDb;
const MemoryKind = memory_db.MemoryKind;
const MemoryRelation = memory_db.MemoryRelation;

// ---------------------------------------------------------------------------
// Helper: open an in-memory MemoryDb
// ---------------------------------------------------------------------------

fn openMemDb(allocator: std.mem.Allocator) !MemoryDb {
    return MemoryDb.init(allocator, ":memory:");
}

// ---------------------------------------------------------------------------
// Kind constant values
// ---------------------------------------------------------------------------

test "memory_db: MemoryKind enum values are correct" {
    try std.testing.expectEqual(@as(i32, 0), @intFromEnum(MemoryKind.fact));
    try std.testing.expectEqual(@as(i32, 1), @intFromEnum(MemoryKind.preference));
    try std.testing.expectEqual(@as(i32, 2), @intFromEnum(MemoryKind.event));
    try std.testing.expectEqual(@as(i32, 3), @intFromEnum(MemoryKind.profile));
    try std.testing.expectEqual(@as(i32, 4), @intFromEnum(MemoryKind.relationship));
    try std.testing.expectEqual(@as(i32, 5), @intFromEnum(MemoryKind.goal));
}

// ---------------------------------------------------------------------------
// Relation constant values
// ---------------------------------------------------------------------------

test "memory_db: MemoryRelation enum values are correct" {
    try std.testing.expectEqual(@as(i32, 0), @intFromEnum(MemoryRelation.sets));
    try std.testing.expectEqual(@as(i32, 1), @intFromEnum(MemoryRelation.updates));
    try std.testing.expectEqual(@as(i32, 2), @intFromEnum(MemoryRelation.extends));
    try std.testing.expectEqual(@as(i32, 3), @intFromEnum(MemoryRelation.retracts));
}

// ---------------------------------------------------------------------------
// Kind ↔ string conversions
// ---------------------------------------------------------------------------

test "memory_db: MemoryKind.toString returns correct strings" {
    try std.testing.expectEqualStrings("fact", MemoryKind.fact.toString());
    try std.testing.expectEqualStrings("preference", MemoryKind.preference.toString());
    try std.testing.expectEqualStrings("event", MemoryKind.event.toString());
    try std.testing.expectEqualStrings("profile", MemoryKind.profile.toString());
    try std.testing.expectEqualStrings("relationship", MemoryKind.relationship.toString());
    try std.testing.expectEqualStrings("goal", MemoryKind.goal.toString());
}

test "memory_db: MemoryKind.fromString returns correct values" {
    try std.testing.expectEqual(MemoryKind.fact, MemoryKind.fromString("fact"));
    try std.testing.expectEqual(MemoryKind.preference, MemoryKind.fromString("preference"));
    try std.testing.expectEqual(MemoryKind.event, MemoryKind.fromString("event"));
    try std.testing.expectEqual(MemoryKind.profile, MemoryKind.fromString("profile"));
    try std.testing.expectEqual(MemoryKind.relationship, MemoryKind.fromString("relationship"));
    try std.testing.expectEqual(MemoryKind.goal, MemoryKind.fromString("goal"));
    // Unknown → fact (default)
    try std.testing.expectEqual(MemoryKind.fact, MemoryKind.fromString("unknown"));
    try std.testing.expectEqual(MemoryKind.fact, MemoryKind.fromString(""));
}

// ---------------------------------------------------------------------------
// Relation ↔ string conversions
// ---------------------------------------------------------------------------

test "memory_db: MemoryRelation.toString returns correct strings" {
    try std.testing.expectEqualStrings("sets", MemoryRelation.sets.toString());
    try std.testing.expectEqualStrings("updates", MemoryRelation.updates.toString());
    try std.testing.expectEqualStrings("extends", MemoryRelation.extends.toString());
    try std.testing.expectEqualStrings("retracts", MemoryRelation.retracts.toString());
}

test "memory_db: MemoryRelation.fromString returns correct values" {
    try std.testing.expectEqual(MemoryRelation.sets, MemoryRelation.fromString("sets"));
    try std.testing.expectEqual(MemoryRelation.updates, MemoryRelation.fromString("updates"));
    try std.testing.expectEqual(MemoryRelation.extends, MemoryRelation.fromString("extends"));
    try std.testing.expectEqual(MemoryRelation.retracts, MemoryRelation.fromString("retracts"));
    // Unknown → sets (default)
    try std.testing.expectEqual(MemoryRelation.sets, MemoryRelation.fromString("unknown"));
    try std.testing.expectEqual(MemoryRelation.sets, MemoryRelation.fromString(""));
}

// ---------------------------------------------------------------------------
// Init and deinit
// ---------------------------------------------------------------------------

test "memory_db: init and deinit with in-memory database" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();
}

// ---------------------------------------------------------------------------
// Store and recall
// ---------------------------------------------------------------------------

test "memory_db: store returns positive row id" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    const id = try db.store("test_user", "favorite_color", "blue", .preference, .sets);
    try std.testing.expect(id > 0);
}

test "memory_db: recall returns stored value" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("test_user", "favorite_color", "blue", .preference, .sets);

    var card = (try db.recall("test_user", "favorite_color")).?;
    defer card.deinit(alloc);

    try std.testing.expectEqualStrings("blue", card.value);
    try std.testing.expectEqualStrings("test_user", card.entity);
    try std.testing.expectEqualStrings("favorite_color", card.slot);
    try std.testing.expectEqual(MemoryKind.preference, card.kind);
    try std.testing.expectEqual(MemoryRelation.sets, card.relation);
}

test "memory_db: recall non-existent slot returns null" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    const result = try db.recall("nobody", "nothing");
    try std.testing.expectEqual(@as(?memory_db.MemoryCard, null), result);
}

test "memory_db: store multiple kinds and recall preserves kind" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("user", "language", "Python", .preference, .sets);
    _ = try db.store("user", "event1", "joined meeting", .event, .sets);
    _ = try db.store("user", "name", "Alice", .profile, .sets);

    var c1 = (try db.recall("user", "language")).?;
    defer c1.deinit(alloc);
    try std.testing.expectEqual(MemoryKind.preference, c1.kind);
    try std.testing.expectEqualStrings("Python", c1.value);

    var c2 = (try db.recall("user", "event1")).?;
    defer c2.deinit(alloc);
    try std.testing.expectEqual(MemoryKind.event, c2.kind);

    var c3 = (try db.recall("user", "name")).?;
    defer c3.deinit(alloc);
    try std.testing.expectEqual(MemoryKind.profile, c3.kind);
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

test "memory_db: search finds stored values" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("user1", "language", "Python", .preference, .sets);
    _ = try db.store("user2", "language", "JavaScript", .preference, .sets);
    _ = try db.store("user1", "editor", "vim", .preference, .sets);

    // Search for Python — should find at least 1 result
    const results = try db.search(alloc, "Python", 10);
    defer MemoryDb.freeCards(alloc, results);

    try std.testing.expect(results.len >= 1);

    // Verify Python appears in results
    var found = false;
    for (results) |card| {
        if (std.mem.eql(u8, card.value, "Python")) {
            found = true;
            break;
        }
    }
    try std.testing.expect(found);
}

test "memory_db: search respects top_k limit" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    // Store many values containing "test"
    var i: usize = 0;
    while (i < 20) : (i += 1) {
        const val = try std.fmt.allocPrint(alloc, "test_value_{d}", .{i});
        defer alloc.free(val);
        const slot = try std.fmt.allocPrint(alloc, "slot_{d}", .{i});
        defer alloc.free(slot);
        _ = try db.store("entity", slot, val, .fact, .sets);
    }

    const results = try db.search(alloc, "test", 5);
    defer MemoryDb.freeCards(alloc, results);

    try std.testing.expect(results.len <= 5);
}

// ---------------------------------------------------------------------------
// Entity memories
// ---------------------------------------------------------------------------

test "memory_db: getEntityMemories returns all memories for entity" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("test_entity", "slot1", "value1", .fact, .sets);
    _ = try db.store("test_entity", "slot2", "value2", .preference, .sets);
    _ = try db.store("other_entity", "slot3", "value3", .fact, .sets);

    const cards = try db.getEntityMemories(alloc, "test_entity");
    defer MemoryDb.freeCards(alloc, cards);

    try std.testing.expectEqual(@as(usize, 2), cards.len);

    // All returned cards should belong to "test_entity"
    for (cards) |card| {
        try std.testing.expectEqualStrings("test_entity", card.entity);
    }
}

test "memory_db: getEntityMemories empty entity returns empty slice" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    const cards = try db.getEntityMemories(alloc, "nonexistent");
    defer MemoryDb.freeCards(alloc, cards);

    try std.testing.expectEqual(@as(usize, 0), cards.len);
}

// ---------------------------------------------------------------------------
// Recall returns most-recent value when multiple records exist for same slot
// ---------------------------------------------------------------------------

test "memory_db: recall returns most-recent value for slot" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("user", "editor", "vim", .preference, .sets);
    _ = try db.store("user", "editor", "emacs", .preference, .updates);

    var card = (try db.recall("user", "editor")).?;
    defer card.deinit(alloc);

    try std.testing.expectEqualStrings("emacs", card.value);
}

// ---------------------------------------------------------------------------
// MemoryCard.id is positive
// ---------------------------------------------------------------------------

test "memory_db: stored card has positive id" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    const row_id = try db.store("user", "x", "y", .fact, .sets);
    try std.testing.expect(row_id > 0);

    var card = (try db.recall("user", "x")).?;
    defer card.deinit(alloc);
    try std.testing.expect(card.id > 0);
}

// ---------------------------------------------------------------------------
// Timestamp is non-empty
// ---------------------------------------------------------------------------

test "memory_db: stored card has non-empty timestamp" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("user", "ts_slot", "ts_value", .fact, .sets);

    var card = (try db.recall("user", "ts_slot")).?;
    defer card.deinit(alloc);
    try std.testing.expect(card.timestamp.len > 0);
}
