//! tests/test_diff_colors.zig — Zig port of tests/test_diff_colors.c
//!
//! Tests diff line colorization logic from zig/util/diff_utils.zig.

const std = @import("std");
const diff_utils = @import("../util/diff_utils.zig");

const DiffColors = diff_utils.DiffColors;

// ---------------------------------------------------------------------------
// diffLineColor classification tests
// ---------------------------------------------------------------------------

test "diff colors: added line starts with + gets green color" {
    const colors = DiffColors{};
    const col = diff_utils.diffLineColor("+added content", colors);
    try std.testing.expect(col != null);
    try std.testing.expectEqualStrings(colors.add, col.?.prefix);
    try std.testing.expectEqualStrings(colors.reset, col.?.suffix);
}

test "diff colors: removed line starts with - gets red color" {
    const colors = DiffColors{};
    const col = diff_utils.diffLineColor("-removed content", colors);
    try std.testing.expect(col != null);
    try std.testing.expectEqualStrings(colors.remove, col.?.prefix);
    try std.testing.expectEqualStrings(colors.reset, col.?.suffix);
}

test "diff colors: +++ header line not colorized" {
    const colors = DiffColors{};
    try std.testing.expect(diff_utils.diffLineColor("+++file.txt", colors) == null);
    try std.testing.expect(diff_utils.diffLineColor("+++ file.txt", colors) == null);
}

test "diff colors: --- header line not colorized" {
    const colors = DiffColors{};
    try std.testing.expect(diff_utils.diffLineColor("---file.txt", colors) == null);
    try std.testing.expect(diff_utils.diffLineColor("--- file.txt", colors) == null);
}

test "diff colors: context line (space prefix) not colorized" {
    const colors = DiffColors{};
    try std.testing.expect(diff_utils.diffLineColor(" unchanged line", colors) == null);
}

test "diff colors: @@ hunk header not colorized" {
    const colors = DiffColors{};
    try std.testing.expect(diff_utils.diffLineColor("@@ -1,3 +1,3 @@", colors) == null);
}

test "diff colors: empty line not colorized" {
    const colors = DiffColors{};
    try std.testing.expect(diff_utils.diffLineColor("", colors) == null);
}

// ---------------------------------------------------------------------------
// renderDiff: full pipeline
// ---------------------------------------------------------------------------

test "diff colors: renderDiff processes a complete diff" {
    const alloc = std.testing.allocator;
    const diff_output =
        \\--- a/old.txt
        \\+++ b/new.txt
        \\@@ -1,3 +1,3 @@
        \\ Line 1: unchanged
        \\-Line 2: removed
        \\+Line 2: added
        \\ Line 3: unchanged
    ;
    const colors = DiffColors{};
    const rendered = try diff_utils.renderDiff(alloc, diff_output, colors);
    defer alloc.free(rendered);

    // Should contain added and removed markers
    try std.testing.expect(std.mem.indexOf(u8, rendered, "Line 2: removed") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered, "Line 2: added") != null);
    // Color codes should be present for the changed lines
    try std.testing.expect(std.mem.indexOf(u8, rendered, "\x1b[32m") != null); // green
    try std.testing.expect(std.mem.indexOf(u8, rendered, "\x1b[31m") != null); // red
    try std.testing.expect(std.mem.indexOf(u8, rendered, "\x1b[0m") != null); // reset
}

test "diff colors: renderDiff empty input returns empty" {
    const alloc = std.testing.allocator;
    const colors = DiffColors{};
    const rendered = try diff_utils.renderDiff(alloc, "", colors);
    defer alloc.free(rendered);
    try std.testing.expectEqualStrings("", rendered);
}

// ---------------------------------------------------------------------------
// Custom colors
// ---------------------------------------------------------------------------

test "diff colors: custom colors applied to added line" {
    const custom = DiffColors{
        .add = "[ADD]",
        .remove = "[REM]",
        .reset = "[RST]",
    };
    const col = diff_utils.diffLineColor("+new line", custom);
    try std.testing.expect(col != null);
    try std.testing.expectEqualStrings("[ADD]", col.?.prefix);
    try std.testing.expectEqualStrings("[RST]", col.?.suffix);
}

test "diff colors: custom colors applied to removed line" {
    const custom = DiffColors{
        .add = "[ADD]",
        .remove = "[REM]",
        .reset = "[RST]",
    };
    const col = diff_utils.diffLineColor("-old line", custom);
    try std.testing.expect(col != null);
    try std.testing.expectEqualStrings("[REM]", col.?.prefix);
}
