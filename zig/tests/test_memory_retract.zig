//! tests/test_memory_retract.zig — Zig port of tests/test_memory_retract.c
//!
//! Tests that relation .retracts properly removes memories from recall
//! and search results.

const std = @import("std");
const memory_db = @import("../memory_db.zig");

const MemoryDb = memory_db.MemoryDb;
const MemoryCard = memory_db.MemoryCard;
const MemoryKind = memory_db.MemoryKind;
const MemoryRelation = memory_db.MemoryRelation;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

fn openMemDb(allocator: std.mem.Allocator) !MemoryDb {
    return MemoryDb.init(allocator, ":memory:");
}

// ---------------------------------------------------------------------------
// Retract makes memory unavailable for recall
// ---------------------------------------------------------------------------

test "memory_retract: retract makes memory unavailable for recall" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    // Step 1: store
    const id1 = try db.store("user", "coding_style", "prefers_tabs", .preference, .sets);
    try std.testing.expect(id1 > 0);

    // Step 2: verify recall works
    var card = (try db.recall("user", "coding_style")).?;
    try std.testing.expectEqualStrings("prefers_tabs", card.value);
    try std.testing.expectEqual(MemoryRelation.sets, card.relation);
    card.deinit(alloc);

    // Step 3: retract via MemoryDb.retract helper
    const id2 = try db.retract("user", "coding_style");
    try std.testing.expect(id2 > id1);

    // Step 4: recall should now return null
    const result = try db.recall("user", "coding_style");
    try std.testing.expectEqual(@as(?MemoryCard, null), result);
}

// ---------------------------------------------------------------------------
// Retract via explicit store with .retracts relation
// ---------------------------------------------------------------------------

test "memory_retract: explicit store with .retracts hides memory from recall" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("user", "workflow", "discuss_before_coding", .preference, .sets);

    var before = (try db.recall("user", "workflow")).?;
    try std.testing.expectEqualStrings("discuss_before_coding", before.value);
    before.deinit(alloc);

    // Retract by storing a retracts record
    _ = try db.store("user", "workflow", "discuss_before_coding", .preference, .retracts);

    const after = try db.recall("user", "workflow");
    try std.testing.expectEqual(@as(?MemoryCard, null), after);
}

// ---------------------------------------------------------------------------
// Retracted memories do not appear in search results
// ---------------------------------------------------------------------------

test "memory_retract: retracted memory is excluded from search results" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("user", "workflow", "discuss_before_coding", .preference, .sets);

    // Retract
    _ = try db.retract("user", "workflow");

    // Search — the retracted value must not appear
    const results = try db.search(alloc, "discuss", 10);
    defer MemoryDb.freeCards(alloc, results);

    for (results) |card| {
        // If we do find the slot, the value must not be the retracted one
        if (std.mem.eql(u8, card.slot, "workflow")) {
            try std.testing.expect(!std.mem.eql(u8, card.value, "discuss_before_coding"));
        }
    }
}

// ---------------------------------------------------------------------------
// Store new value after retraction
// ---------------------------------------------------------------------------

test "memory_retract: can store new value after retraction" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    // Store initial
    _ = try db.store("user", "editor", "vim", .preference, .sets);

    // Retract
    _ = try db.retract("user", "editor");

    // Verify retracted
    const mid = try db.recall("user", "editor");
    try std.testing.expectEqual(@as(?MemoryCard, null), mid);

    // Store new value
    _ = try db.store("user", "editor", "emacs", .preference, .sets);

    // Verify new value is returned
    var card = (try db.recall("user", "editor")).?;
    defer card.deinit(alloc);
    try std.testing.expectEqualStrings("emacs", card.value);
    try std.testing.expectEqual(MemoryRelation.sets, card.relation);
}

// ---------------------------------------------------------------------------
// Retract non-existent memory: no crash, recall still returns null
// ---------------------------------------------------------------------------

test "memory_retract: retracting non-existent memory returns null on recall" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    // Retract something that was never stored — should succeed
    const id = try db.store("user", "never_stored", "some_value", .fact, .retracts);
    try std.testing.expect(id > 0);

    // Recall must return null
    const result = try db.recall("user", "never_stored");
    try std.testing.expectEqual(@as(?MemoryCard, null), result);
}

// ---------------------------------------------------------------------------
// getEntityMemories excludes retracted slots (current-value view)
// ---------------------------------------------------------------------------

test "memory_retract: getEntityMemories excludes retracted slots" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("user", "slot1", "value1", .fact, .sets);
    _ = try db.store("user", "slot2", "value2", .fact, .sets);

    // Retract slot1
    _ = try db.retract("user", "slot1");

    const cards = try db.getEntityMemories(alloc, "user");
    defer MemoryDb.freeCards(alloc, cards);

    // slot2 should be present, slot1 should not (its last record is retracts)
    var found_slot2 = false;
    for (cards) |card| {
        // The implementation filters out rows whose last record is retracts,
        // so slot1 must not appear here.
        try std.testing.expect(!std.mem.eql(u8, card.slot, "slot1"));
        if (std.mem.eql(u8, card.slot, "slot2")) found_slot2 = true;
    }
    try std.testing.expect(found_slot2);
}

// ---------------------------------------------------------------------------
// Multiple sequential retracts on same slot: idempotent
// ---------------------------------------------------------------------------

test "memory_retract: multiple retracts on same slot remain retracted" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("user", "pref", "val", .preference, .sets);
    _ = try db.retract("user", "pref");
    _ = try db.retract("user", "pref");
    _ = try db.retract("user", "pref");

    const result = try db.recall("user", "pref");
    try std.testing.expectEqual(@as(?MemoryCard, null), result);
}

// ---------------------------------------------------------------------------
// Retract only affects the target slot, not sibling slots
// ---------------------------------------------------------------------------

test "memory_retract: retracting one slot does not affect sibling slots" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("user", "language", "Zig", .preference, .sets);
    _ = try db.store("user", "editor", "helix", .preference, .sets);

    _ = try db.retract("user", "language");

    // editor should still be accessible
    var card = (try db.recall("user", "editor")).?;
    defer card.deinit(alloc);
    try std.testing.expectEqualStrings("helix", card.value);

    // language should be gone
    const lang_result = try db.recall("user", "language");
    try std.testing.expectEqual(@as(?MemoryCard, null), lang_result);
}
