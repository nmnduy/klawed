//! tests/test_file_search.zig — Zig port of tests/test_file_search.c
//!
//! Tests the FileSearchState from zig/tui/file_search.zig.
//!
//! The C test exercised internal C functions (fuzzy_score, compare_results)
//! that are not exposed in the Zig module.  Instead this port tests the
//! public FileSearchState API — activation, query management, filtering,
//! selection navigation, and edge cases — all headless.

const std = @import("std");
const fs_mod = @import("../tui/file_search.zig");
const FileSearchState = fs_mod.FileSearchState;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Populate a FileSearchState with the given file list and return it.
/// Caller owns the returned state and must call deinit().
fn makeState(alloc: std.mem.Allocator, files: []const []const u8) !FileSearchState {
    var state = FileSearchState.init(alloc);
    errdefer state.deinit();

    for (files) |name| {
        const copy = try alloc.dupe(u8, name);
        try state.files.append(copy);
    }

    // Initial filter: use clearQuery to trigger the private filterFiles path.
    state.clearQuery();
    return state;
}

/// Trigger a filter pass via the public clearQuery route (clears query → filters).
fn runFilter(state: *FileSearchState) void {
    state.clearQuery();
}

// ---------------------------------------------------------------------------
// Activation / deactivation
// ---------------------------------------------------------------------------

test "file search: activate sets active flag and resets state" {
    var state = FileSearchState.init(std.testing.allocator);
    defer state.deinit();

    try std.testing.expect(!state.isActive());
    state.activate();
    try std.testing.expect(state.isActive());

    state.selected_index = 5;
    state.scroll_offset = 10;
    state.activate(); // second activate should reset
    try std.testing.expectEqual(@as(usize, 0), state.selected_index);
    try std.testing.expectEqual(@as(usize, 0), state.scroll_offset);
    try std.testing.expectEqual(@as(usize, 0), state.query.items.len);
}

test "file search: deactivate clears active flag" {
    var state = FileSearchState.init(std.testing.allocator);
    defer state.deinit();

    state.activate();
    state.deactivate();
    try std.testing.expect(!state.isActive());
}

// ---------------------------------------------------------------------------
// Query management
// ---------------------------------------------------------------------------

test "file search: addQueryChar appends characters" {
    var state = FileSearchState.init(std.testing.allocator);
    defer state.deinit();

    try state.addQueryChar('t');
    try state.addQueryChar('e');
    try state.addQueryChar('s');
    try std.testing.expectEqualStrings("tes", state.query.items);
}

test "file search: backspaceQuery removes last character" {
    var state = FileSearchState.init(std.testing.allocator);
    defer state.deinit();

    try state.addQueryChar('t');
    try state.addQueryChar('e');
    state.backspaceQuery();
    try std.testing.expectEqualStrings("t", state.query.items);
}

test "file search: backspaceQuery on empty query is a no-op" {
    var state = FileSearchState.init(std.testing.allocator);
    defer state.deinit();

    state.backspaceQuery(); // must not crash
    try std.testing.expectEqual(@as(usize, 0), state.query.items.len);
}

test "file search: clearQuery empties the query" {
    var state = FileSearchState.init(std.testing.allocator);
    defer state.deinit();

    try state.addQueryChar('h');
    try state.addQueryChar('i');
    state.clearQuery();
    try std.testing.expectEqualStrings("", state.query.items);
}

// ---------------------------------------------------------------------------
// Filtering
// ---------------------------------------------------------------------------

test "file search: empty query shows all files" {
    const files = [_][]const u8{ "main.c", "util.c", "README.md" };
    var state = try makeState(std.testing.allocator, &files);
    defer state.deinit();

    try std.testing.expectEqual(@as(usize, 3), state.filtered.items.len);
}

test "file search: query narrows results" {
    const files = [_][]const u8{ "main.c", "main.h", "util.c", "README.md" };
    var state = try makeState(std.testing.allocator, &files);
    defer state.deinit();

    try state.addQueryChar('m');
    try state.addQueryChar('a');
    try state.addQueryChar('i');
    try state.addQueryChar('n');
    // "main.c" and "main.h" contain "main"
    try std.testing.expectEqual(@as(usize, 2), state.filtered.items.len);
}

test "file search: query with no matches returns empty filtered list" {
    const files = [_][]const u8{ "main.c", "util.c" };
    var state = try makeState(std.testing.allocator, &files);
    defer state.deinit();

    try state.addQueryChar('x');
    try state.addQueryChar('y');
    try state.addQueryChar('z');
    try std.testing.expectEqual(@as(usize, 0), state.filtered.items.len);
}

test "file search: case-insensitive substring match" {
    const files = [_][]const u8{ "Makefile", "README.md", "src/main.c" };
    var state = try makeState(std.testing.allocator, &files);
    defer state.deinit();

    // "make" should match "Makefile" case-insensitively
    try state.addQueryChar('m');
    try state.addQueryChar('a');
    try state.addQueryChar('k');
    try state.addQueryChar('e');
    try std.testing.expect(state.filtered.items.len >= 1);
}

test "file search: clearing query restores all files" {
    const files = [_][]const u8{ "alpha.c", "beta.c", "gamma.c" };
    var state = try makeState(std.testing.allocator, &files);
    defer state.deinit();

    try state.addQueryChar('a');
    try state.addQueryChar('l');
    // only "alpha.c" should match
    try std.testing.expectEqual(@as(usize, 1), state.filtered.items.len);

    state.clearQuery();
    try std.testing.expectEqual(@as(usize, 3), state.filtered.items.len);
}

// ---------------------------------------------------------------------------
// Selection navigation
// ---------------------------------------------------------------------------

test "file search: moveDown increments selected_index" {
    const files = [_][]const u8{ "a.c", "b.c", "c.c" };
    var state = try makeState(std.testing.allocator, &files);
    defer state.deinit();

    try std.testing.expectEqual(@as(usize, 0), state.selected_index);
    state.moveDown();
    try std.testing.expectEqual(@as(usize, 1), state.selected_index);
    state.moveDown();
    try std.testing.expectEqual(@as(usize, 2), state.selected_index);
}

test "file search: moveDown stops at last item" {
    const files = [_][]const u8{ "a.c", "b.c" };
    var state = try makeState(std.testing.allocator, &files);
    defer state.deinit();

    state.moveDown();
    state.moveDown(); // should not go past index 1
    state.moveDown();
    try std.testing.expectEqual(@as(usize, 1), state.selected_index);
}

test "file search: moveUp decrements selected_index" {
    const files = [_][]const u8{ "a.c", "b.c", "c.c" };
    var state = try makeState(std.testing.allocator, &files);
    defer state.deinit();

    state.moveDown();
    state.moveDown();
    state.moveUp();
    try std.testing.expectEqual(@as(usize, 1), state.selected_index);
}

test "file search: moveUp stops at first item" {
    const files = [_][]const u8{ "a.c", "b.c" };
    var state = try makeState(std.testing.allocator, &files);
    defer state.deinit();

    state.moveUp(); // already at 0, should not underflow
    try std.testing.expectEqual(@as(usize, 0), state.selected_index);
}

// ---------------------------------------------------------------------------
// getSelected
// ---------------------------------------------------------------------------

test "file search: getSelected returns currently highlighted file" {
    const files = [_][]const u8{ "first.c", "second.c", "third.c" };
    var state = try makeState(std.testing.allocator, &files);
    defer state.deinit();

    const sel0 = state.getSelected();
    try std.testing.expect(sel0 != null);
    try std.testing.expectEqualStrings("first.c", sel0.?);

    state.moveDown();
    const sel1 = state.getSelected();
    try std.testing.expect(sel1 != null);
    try std.testing.expectEqualStrings("second.c", sel1.?);
}

test "file search: getSelected returns null when filtered list is empty" {
    var state = FileSearchState.init(std.testing.allocator);
    defer state.deinit();

    // No files loaded, no filter run
    try std.testing.expect(state.getSelected() == null);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

test "file search: empty file list works without crashing" {
    var state = FileSearchState.init(std.testing.allocator);
    defer state.deinit();

    runFilter(&state);
    try std.testing.expectEqual(@as(usize, 0), state.filtered.items.len);
    try std.testing.expect(state.getSelected() == null);
}

test "file search: single file returns correct result" {
    const files = [_][]const u8{"only_file.zig"};
    var state = try makeState(std.testing.allocator, &files);
    defer state.deinit();

    try std.testing.expectEqual(@as(usize, 1), state.filtered.items.len);
    const sel = state.getSelected();
    try std.testing.expect(sel != null);
    try std.testing.expectEqualStrings("only_file.zig", sel.?);
}

test "file search: real-world file patterns are found" {
    const files = [_][]const u8{
        "src/main.c",
        "include/header.h",
        "Makefile",
        "README.md",
        "src_utils.c",
        "test-unit.c",
    };
    var state = try makeState(std.testing.allocator, &files);
    defer state.deinit();

    // "main" should match "src/main.c"
    try state.addQueryChar('m');
    try state.addQueryChar('a');
    try state.addQueryChar('i');
    try state.addQueryChar('n');
    try std.testing.expect(state.filtered.items.len >= 1);
    state.clearQuery();

    // "head" should match "include/header.h"
    try state.addQueryChar('h');
    try state.addQueryChar('e');
    try state.addQueryChar('a');
    try state.addQueryChar('d');
    try std.testing.expect(state.filtered.items.len >= 1);
    state.clearQuery();

    // "unit" should match "test-unit.c"
    try state.addQueryChar('u');
    try state.addQueryChar('n');
    try state.addQueryChar('i');
    try state.addQueryChar('t');
    try std.testing.expect(state.filtered.items.len >= 1);
}
