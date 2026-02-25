//! tests/test_memory_null_fix.zig — Zig port of tests/test_memory_null_fix.c
//!
//! The original C test verified that pointers are set to NULL after freeing
//! in clear_conversation() / conversation_free() to prevent double-free bugs.
//!
//! In Zig this class of bug doesn't apply the same way:
//!   - Zig's memory model uses slices (ptr + len), not raw C pointers.
//!   - `deinit` patterns leave the struct in an undefined / zeroed state via
//!     `self.* = undefined`, making double-deinit an immediate safety panic.
//!   - `defer` ensures cleanup without manual null-check chains.
//!
//! These tests verify the MemoryCard / MemoryDb lifecycle safety properties
//! that the C null-fix was trying to guarantee, expressed idiomatically in Zig.

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
// Card lifecycle: deinit clears fields
// ---------------------------------------------------------------------------

test "memory_null_fix: MemoryCard.deinit frees owned fields without crash" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("user", "slot", "value", .fact, .sets);

    var card = (try db.recall("user", "slot")).?;
    // deinit must not crash and must release all owned allocations
    card.deinit(alloc);
    // (no assertion needed — the test allocator will catch any leak)
}

test "memory_null_fix: multiple cards can be individually deinited" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("user", "slot_a", "value_a", .preference, .sets);
    _ = try db.store("user", "slot_b", "value_b", .event, .sets);
    _ = try db.store("user", "slot_c", "value_c", .goal, .sets);

    var c1 = (try db.recall("user", "slot_a")).?;
    var c2 = (try db.recall("user", "slot_b")).?;
    var c3 = (try db.recall("user", "slot_c")).?;

    c1.deinit(alloc);
    c2.deinit(alloc);
    c3.deinit(alloc);
}

// ---------------------------------------------------------------------------
// freeCards cleans up a search-result slice
// ---------------------------------------------------------------------------

test "memory_null_fix: freeCards releases all cards in a search result" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("entity", "s1", "alpha", .fact, .sets);
    _ = try db.store("entity", "s2", "beta", .fact, .sets);
    _ = try db.store("entity", "s3", "gamma", .fact, .sets);

    const cards = try db.getEntityMemories(alloc, "entity");
    // freeCards must not crash and must release all allocations
    MemoryDb.freeCards(alloc, cards);
}

test "memory_null_fix: freeCards on empty slice is a no-op" {
    const alloc = std.testing.allocator;
    const empty: []memory_db.MemoryCard = &[_]memory_db.MemoryCard{};
    MemoryDb.freeCards(alloc, empty);
}

// ---------------------------------------------------------------------------
// Db deinit is safe after use
// ---------------------------------------------------------------------------

test "memory_null_fix: MemoryDb.deinit after store and recall does not crash" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);

    _ = try db.store("user", "employer", "Acme Corp", .fact, .sets);

    var card = (try db.recall("user", "employer")).?;
    defer card.deinit(alloc);

    db.deinit();
    // Attempting to use `db` after deinit would be undefined behaviour
    // (Zig debug mode traps `undefined`). The test just verifies clean exit.
}

// ---------------------------------------------------------------------------
// Retract + recall: no dangling data
// ---------------------------------------------------------------------------

test "memory_null_fix: retract followed by recall returns null, no leak" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("user", "coding_style", "prefers_tabs", .preference, .sets);
    _ = try db.retract("user", "coding_style");

    const result = try db.recall("user", "coding_style");
    try std.testing.expectEqual(@as(?MemoryCard, null), result);
}

// ---------------------------------------------------------------------------
// Repeated store/recall does not leak
// ---------------------------------------------------------------------------

test "memory_null_fix: repeated store and recall in a loop does not leak" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    var i: usize = 0;
    while (i < 20) : (i += 1) {
        const val = try std.fmt.allocPrint(alloc, "value_{d}", .{i});
        defer alloc.free(val);

        _ = try db.store("entity", "slot", val, .fact, .sets);

        var card = (try db.recall("entity", "slot")).?;
        defer card.deinit(alloc);

        try std.testing.expectEqualStrings(val, card.value);
    }
}

// ---------------------------------------------------------------------------
// NULL-equivalent: recall on fresh db returns null
// ---------------------------------------------------------------------------

test "memory_null_fix: recall on empty db returns null for any slot" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    const result = try db.recall("entity", "slot");
    try std.testing.expectEqual(@as(?MemoryCard, null), result);
}

// ---------------------------------------------------------------------------
// Card fields are correctly initialised (no garbage from C malloc)
// ---------------------------------------------------------------------------

test "memory_null_fix: recalled card fields are non-empty strings" {
    const alloc = std.testing.allocator;
    var db = try openMemDb(alloc);
    defer db.deinit();

    _ = try db.store("my_entity", "my_slot", "my_value", .profile, .sets);

    var card = (try db.recall("my_entity", "my_slot")).?;
    defer card.deinit(alloc);

    try std.testing.expect(card.entity.len > 0);
    try std.testing.expect(card.slot.len > 0);
    try std.testing.expect(card.value.len > 0);
    try std.testing.expect(card.timestamp.len > 0);
    try std.testing.expectEqualStrings("my_entity", card.entity);
    try std.testing.expectEqualStrings("my_slot", card.slot);
    try std.testing.expectEqualStrings("my_value", card.value);
}
