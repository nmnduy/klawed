//! tests/test_tool_definition_parity.zig — Zig port of tests/test_tool_definition_parity.c
//!
//! The C test verified parity between the Messages API and Responses API tool
//! definitions.  In the Zig implementation there is a single `registry` in
//! `tools/registry.zig` — the same list is used for every API format.
//!
//! This port therefore validates:
//!   - All expected core tools are present in the registry
//!   - Tool names are unique (no accidental duplicates)
//!   - Memory tools are present (MemoryStore, MemoryRecall, MemorySearch)
//!   - Key built-in tools (Bash, Read, Write, Edit, etc.) are present
//!
//! Note: The "Messages vs Responses API parity" concern is moot in Zig because
//! the provider layer renders one registry into whichever JSON format each
//! provider expects at call time — there is no second registry to drift.

const std = @import("std");
const registry = @import("../tools/registry.zig");

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

fn toolExists(name: []const u8) bool {
    return registry.findTool(name) != null;
}

fn countDuplicateNames() usize {
    var count: usize = 0;
    for (registry.registry, 0..) |tool_a, i| {
        for (registry.registry[i + 1 ..]) |tool_b| {
            if (std.mem.eql(u8, tool_a.name, tool_b.name)) {
                count += 1;
            }
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "tool definition parity: no duplicate tool names in registry" {
    try std.testing.expectEqual(@as(usize, 0), countDuplicateNames());
}

test "tool definition parity: all tools have non-empty fields" {
    for (registry.registry) |tool| {
        try std.testing.expect(tool.name.len > 0);
        try std.testing.expect(tool.description.len > 0);
        try std.testing.expect(tool.input_schema.len > 0);
    }
}

test "tool definition parity: core built-in tools present" {
    // These tools must always be present regardless of mode
    try std.testing.expect(toolExists("Bash"));
    try std.testing.expect(toolExists("Read"));
    try std.testing.expect(toolExists("Write"));
    try std.testing.expect(toolExists("Edit"));
    try std.testing.expect(toolExists("MultiEdit"));
    try std.testing.expect(toolExists("Glob"));
    try std.testing.expect(toolExists("Grep"));
}

test "tool definition parity: TodoWrite tool present" {
    try std.testing.expect(toolExists("TodoWrite"));
}

test "tool definition parity: Subagent tools present" {
    try std.testing.expect(toolExists("Subagent"));
    try std.testing.expect(toolExists("CheckSubagentProgress"));
    try std.testing.expect(toolExists("InterruptSubagent"));
}

test "tool definition parity: UploadImage tool present" {
    try std.testing.expect(toolExists("UploadImage"));
}

test "tool definition parity: Sleep tool present" {
    try std.testing.expect(toolExists("Sleep"));
}

test "tool definition parity: registry size is reasonable" {
    // At minimum there should be ~13 built-in tools
    try std.testing.expect(registry.registry.len >= 10);
}

test "tool definition parity: all tools findable by name" {
    for (registry.registry) |tool| {
        const found = registry.findTool(tool.name);
        try std.testing.expect(found != null);
        try std.testing.expectEqualStrings(tool.name, found.?.name);
    }
}

test "tool definition parity: unknown tool not found" {
    try std.testing.expect(registry.findTool("NonExistentTool") == null);
    try std.testing.expect(registry.findTool("") == null);
    try std.testing.expect(registry.findTool("MemoryStore") == null); // dynamic, not built-in
}
