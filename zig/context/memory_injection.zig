//! context/memory_injection.zig — Memory context injection into system prompt
//!
//! Zig port of src/context/memory_injection.c.
//!
//! Queries the `MemoryDb` for relevant memories and injects them into the
//! system prompt as a `## Background Knowledge (from memory)` section.
//!
//! The injection can be called before each API request to keep the memory
//! context fresh.  Any existing memory section is removed before a new one
//! is added (idempotent refresh).
//!
//! ## Section markers
//!
//! The injected block is wrapped with:
//!   - Start: `\n\n## Background Knowledge (from memory)\n\n`
//!   - End:   `\n<!-- END_MEMORY_CONTEXT -->\n`
//!
//! These markers allow subsequent calls to find and replace the section.

const std = @import("std");
const memory_db = @import("../memory_db.zig");
const state_mod = @import("../conversation/state.zig");

pub const ConversationState = state_mod.ConversationState;
pub const MemoryDb = memory_db.MemoryDb;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const MEMORY_START_MARKER = "\n\n## Background Knowledge (from memory)\n\n";
const MEMORY_END_MARKER = "\n<!-- END_MEMORY_CONTEXT -->\n";

// ---------------------------------------------------------------------------
// buildMemoryContext
// ---------------------------------------------------------------------------

/// Query `db` for memories relevant to `working_dir` and return a formatted
/// string.  Returns `null` if no relevant memories are found.
/// Caller must free with `allocator.free`.
pub fn buildMemoryContext(
    allocator: std.mem.Allocator,
    db: *MemoryDb,
    working_dir: ?[]const u8,
) !?[]u8 {
    var buf = std.ArrayList(u8).init(allocator);
    errdefer buf.deinit();
    const w = buf.writer();

    var has_content = false;

    // 1. User preferences
    {
        const cards = try db.getEntityMemories(allocator, "user");
        defer memory_db.MemoryDb.freeCards(allocator, cards);
        if (cards.len > 0) {
            try w.writeAll("### User Preferences\n");
            const limit = @min(cards.len, 10);
            for (cards[0..limit]) |card| {
                try w.print("- {s}: {s}\n", .{ card.slot, card.value });
                has_content = true;
            }
            try w.writeByte('\n');
        }
    }

    // 2. Active tasks/goals (FTS search)
    {
        const cards = try db.search(allocator, "task goal", 10);
        defer memory_db.MemoryDb.freeCards(allocator, cards);
        var tasks_added: usize = 0;
        const max_tasks = 5;
        for (cards) |card| {
            if (tasks_added >= max_tasks) break;
            if (std.mem.startsWith(u8, card.entity, "task:") or
                std.mem.startsWith(u8, card.entity, "goal:"))
            {
                if (tasks_added == 0) try w.writeAll("### Active Tasks\n");
                try w.print("- {s}\n", .{card.value});
                has_content = true;
                tasks_added += 1;
            }
        }
        if (tasks_added > 0) try w.writeByte('\n');
    }

    // 3. Project-specific knowledge
    if (working_dir) |wd| {
        const project_name = extractProjectName(wd);
        if (project_name.len > 0) {
            const entity = try std.fmt.allocPrint(allocator, "project.{s}", .{project_name});
            defer allocator.free(entity);

            const cards = try db.getEntityMemories(allocator, entity);
            defer memory_db.MemoryDb.freeCards(allocator, cards);
            if (cards.len > 0) {
                try w.print("### Project Knowledge ({s})\n", .{project_name});
                const limit = @min(cards.len, 10);
                for (cards[0..limit]) |card| {
                    try w.print("- {s}: {s}\n", .{ card.slot, card.value });
                    has_content = true;
                }
                try w.writeByte('\n');
            }
        }
    }

    if (!has_content) {
        return null;
    }

    const result = try buf.toOwnedSlice();
    return result;
}

/// Extract the last path component as the project name.
fn extractProjectName(path: []const u8) []const u8 {
    if (path.len == 0) return "";
    // Trim trailing slash
    const trimmed = if (path[path.len - 1] == '/') path[0 .. path.len - 1] else path;
    if (std.mem.lastIndexOfScalar(u8, trimmed, '/')) |idx| {
        return trimmed[idx + 1 ..];
    }
    return trimmed;
}

// ---------------------------------------------------------------------------
// Remove existing memory section
// ---------------------------------------------------------------------------

/// Return the system prompt with any existing memory section removed.
/// Returns `null` if no memory section was found (input is unchanged).
/// If a section was found, returns an owned slice; caller must free.
pub fn removeMemoryContext(
    allocator: std.mem.Allocator,
    prompt: []const u8,
) !?[]u8 {
    const start_idx = std.mem.indexOf(u8, prompt, MEMORY_START_MARKER) orelse return null;

    var end_idx: usize = prompt.len;
    if (std.mem.indexOf(u8, prompt[start_idx..], MEMORY_END_MARKER)) |rel| {
        end_idx = start_idx + rel + MEMORY_END_MARKER.len;
    }

    const before = prompt[0..start_idx];
    const after = if (end_idx < prompt.len) prompt[end_idx..] else "";

    const result = try std.mem.concat(allocator, u8, &.{ before, after });
    return result;
}

// ---------------------------------------------------------------------------
// injectMemoryContext
// ---------------------------------------------------------------------------

/// Inject fresh memory context into `state`'s system prompt.
///
/// 1. Removes any existing memory section from the system prompt.
/// 2. Queries `db` for relevant memories.
/// 3. Appends the new section (wrapped in markers) to the prompt.
///
/// Returns `true` if context was injected, `false` if nothing to inject.
pub fn injectMemoryContext(
    allocator: std.mem.Allocator,
    state: *ConversationState,
    db: *MemoryDb,
    working_dir: ?[]const u8,
) !bool {
    const current_prompt = state.systemPromptText() orelse return false;

    // Remove existing memory section
    const maybe_cleaned = try removeMemoryContext(allocator, current_prompt);
    const base_prompt: []const u8 = maybe_cleaned orelse current_prompt;
    defer if (maybe_cleaned) |c| allocator.free(c);

    // Build fresh memory context
    const memory_context = try buildMemoryContext(allocator, db, working_dir) orelse {
        // No memories — if we cleaned the prompt, update it
        if (maybe_cleaned != null) {
            try state.replaceSystemPrompt(base_prompt);
        }
        return false;
    };
    defer allocator.free(memory_context);

    // Assemble new prompt
    const new_prompt = try std.mem.concat(allocator, u8, &.{
        base_prompt,
        MEMORY_START_MARKER,
        memory_context,
        MEMORY_END_MARKER,
    });
    defer allocator.free(new_prompt);

    try state.replaceSystemPrompt(new_prompt);
    return true;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "extractProjectName" {
    try std.testing.expectEqualStrings("myproject", extractProjectName("/home/user/myproject"));
    try std.testing.expectEqualStrings("myproject", extractProjectName("/home/user/myproject/"));
    try std.testing.expectEqualStrings("project", extractProjectName("project"));
    try std.testing.expectEqualStrings("", extractProjectName(""));
}

test "removeMemoryContext — no section returns null" {
    const allocator = std.testing.allocator;
    const prompt = "system prompt without memory";
    const result = try removeMemoryContext(allocator, prompt);
    try std.testing.expect(result == null);
}

test "removeMemoryContext — section is removed" {
    const allocator = std.testing.allocator;
    const prompt = "Before section" ++
        MEMORY_START_MARKER ++
        "### User Preferences\n- foo: bar\n" ++
        MEMORY_END_MARKER ++
        "After section";

    const result = try removeMemoryContext(allocator, prompt) orelse
        return error.TestFailed;
    defer allocator.free(result);

    try std.testing.expectEqualStrings("Before sectionAfter section", result);
    try std.testing.expect(std.mem.indexOf(u8, result, "MEMORY") == null);
}

test "removeMemoryContext — partial (no end marker)" {
    const allocator = std.testing.allocator;
    const prompt = "Before" ++ MEMORY_START_MARKER ++ "some memory";

    const result = try removeMemoryContext(allocator, prompt) orelse
        return error.TestFailed;
    defer allocator.free(result);

    try std.testing.expectEqualStrings("Before", result);
}

test "buildMemoryContext with empty db returns null" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const db_path = try std.fmt.allocPrint(allocator, "{s}/test_memory.db", .{tmp_path});
    defer allocator.free(db_path);

    var db = try MemoryDb.init(allocator, db_path);
    defer db.deinit();

    const result = try buildMemoryContext(allocator, &db, "/tmp/myproject");
    try std.testing.expect(result == null);
}

test "buildMemoryContext with user preferences" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const db_path = try std.fmt.allocPrint(allocator, "{s}/test_memory2.db", .{tmp_path});
    defer allocator.free(db_path);

    var db = try MemoryDb.init(allocator, db_path);
    defer db.deinit();

    _ = try db.store(
        "user",
        "preferred_language",
        "Zig",
        .preference,
        .sets,
    );

    const result = try buildMemoryContext(allocator, &db, "/tmp/test");
    defer if (result) |r| allocator.free(r);

    try std.testing.expect(result != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "preferred_language") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.?, "Zig") != null);
}

test "injectMemoryContext integrates into state" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    try state.addSystemMessage("Base system prompt.");

    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);
    const db_path = try std.fmt.allocPrint(allocator, "{s}/test_memory3.db", .{tmp_path});
    defer allocator.free(db_path);

    var db = try MemoryDb.init(allocator, db_path);
    defer db.deinit();

    _ = try db.store("user", "coding_style", "minimal", .preference, .sets);

    const injected = try injectMemoryContext(allocator, &state, &db, null);
    try std.testing.expect(injected);

    const sys_text = state.systemPromptText() orelse return error.TestFailed;
    try std.testing.expect(std.mem.indexOf(u8, sys_text, "Background Knowledge") != null);
    try std.testing.expect(std.mem.indexOf(u8, sys_text, "coding_style") != null);
}
