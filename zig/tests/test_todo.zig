//! tests/test_todo.zig — Zig port of tests/test_todo.c
//!
//! Tests the TodoList / TodoStatus / TodoWrite tool implementation.

const std = @import("std");
const todo_mod = @import("../tools/todo.zig");

const TodoList = todo_mod.TodoList;
const TodoStatus = todo_mod.TodoStatus;

// ---------------------------------------------------------------------------
// TodoStatus
// ---------------------------------------------------------------------------

test "TodoStatus fromString: valid statuses" {
    try std.testing.expectEqual(TodoStatus.pending, TodoStatus.fromString("pending").?);
    try std.testing.expectEqual(TodoStatus.in_progress, TodoStatus.fromString("in_progress").?);
    try std.testing.expectEqual(TodoStatus.completed, TodoStatus.fromString("completed").?);
}

test "TodoStatus fromString: unknown returns null" {
    try std.testing.expect(TodoStatus.fromString("unknown") == null);
    try std.testing.expect(TodoStatus.fromString("") == null);
    try std.testing.expect(TodoStatus.fromString("PENDING") == null);
}

test "TodoStatus toString round-trip" {
    try std.testing.expectEqualStrings("pending", TodoStatus.pending.toString());
    try std.testing.expectEqualStrings("in_progress", TodoStatus.in_progress.toString());
    try std.testing.expectEqualStrings("completed", TodoStatus.completed.toString());
}

test "TodoStatus indicator symbols" {
    try std.testing.expectEqualStrings("○", TodoStatus.pending.indicator());
    try std.testing.expectEqualStrings("⋯", TodoStatus.in_progress.indicator());
    try std.testing.expectEqualStrings("✓", TodoStatus.completed.indicator());
}

// ---------------------------------------------------------------------------
// TodoList init and deinit
// ---------------------------------------------------------------------------

test "TodoList: init creates empty list" {
    const alloc = std.testing.allocator;
    var list = TodoList.init(alloc);
    defer list.deinit();

    try std.testing.expectEqual(@as(usize, 0), list.items.items.len);
}

// ---------------------------------------------------------------------------
// Adding items
// ---------------------------------------------------------------------------

test "TodoList: add single item" {
    const alloc = std.testing.allocator;
    var list = TodoList.init(alloc);
    defer list.deinit();

    try list.add("Run tests", "Running tests", .pending);

    try std.testing.expectEqual(@as(usize, 1), list.items.items.len);
    try std.testing.expectEqualStrings("Run tests", list.items.items[0].content);
    try std.testing.expectEqualStrings("Running tests", list.items.items[0].active_form);
    try std.testing.expectEqual(TodoStatus.pending, list.items.items[0].status);
}

test "TodoList: add multiple items preserves order" {
    const alloc = std.testing.allocator;
    var list = TodoList.init(alloc);
    defer list.deinit();

    try list.add("Task 1", "Doing task 1", .pending);
    try list.add("Task 2", "Doing task 2", .in_progress);
    try list.add("Task 3", "Doing task 3", .completed);

    try std.testing.expectEqual(@as(usize, 3), list.items.items.len);
    try std.testing.expectEqualStrings("Task 1", list.items.items[0].content);
    try std.testing.expectEqual(TodoStatus.in_progress, list.items.items[1].status);
    try std.testing.expectEqualStrings("Task 3", list.items.items[2].content);
}

// ---------------------------------------------------------------------------
// countByStatus (mirrors test_count_by_status)
// ---------------------------------------------------------------------------

test "TodoList: countByStatus" {
    const alloc = std.testing.allocator;
    var list = TodoList.init(alloc);
    defer list.deinit();

    try list.add("T1", "D1", .pending);
    try list.add("T2", "D2", .pending);
    try list.add("T3", "D3", .in_progress);
    try list.add("T4", "D4", .completed);
    try list.add("T5", "D5", .completed);
    try list.add("T6", "D6", .completed);

    try std.testing.expectEqual(@as(usize, 2), list.countByStatus(.pending));
    try std.testing.expectEqual(@as(usize, 1), list.countByStatus(.in_progress));
    try std.testing.expectEqual(@as(usize, 3), list.countByStatus(.completed));
}

test "TodoList: countByStatus empty list returns zero" {
    const alloc = std.testing.allocator;
    var list = TodoList.init(alloc);
    defer list.deinit();

    try std.testing.expectEqual(@as(usize, 0), list.countByStatus(.pending));
    try std.testing.expectEqual(@as(usize, 0), list.countByStatus(.in_progress));
    try std.testing.expectEqual(@as(usize, 0), list.countByStatus(.completed));
}

// ---------------------------------------------------------------------------
// clear (mirrors test_clear_todos)
// ---------------------------------------------------------------------------

test "TodoList: clear removes all items" {
    const alloc = std.testing.allocator;
    var list = TodoList.init(alloc);
    defer list.deinit();

    try list.add("T1", "D1", .pending);
    try list.add("T2", "D2", .in_progress);
    try list.add("T3", "D3", .completed);
    try std.testing.expectEqual(@as(usize, 3), list.items.items.len);

    list.clear();
    try std.testing.expectEqual(@as(usize, 0), list.items.items.len);
}

test "TodoList: can add items after clear" {
    const alloc = std.testing.allocator;
    var list = TodoList.init(alloc);
    defer list.deinit();

    try list.add("T1", "D1", .pending);
    list.clear();
    try list.add("T2", "D2", .completed);

    try std.testing.expectEqual(@as(usize, 1), list.items.items.len);
    try std.testing.expectEqualStrings("T2", list.items.items[0].content);
}

// ---------------------------------------------------------------------------
// renderToString (mirrors test_render_visual)
// ---------------------------------------------------------------------------

test "TodoList: renderToString contains all items" {
    const alloc = std.testing.allocator;
    var list = TodoList.init(alloc);
    defer list.deinit();

    try list.add("Initialize project structure", "Initializing", .completed);
    try list.add("Implement core functionality", "Implementing", .in_progress);
    try list.add("Write unit tests", "Writing tests", .pending);

    const rendered = try list.renderToString(alloc);
    defer alloc.free(rendered);

    try std.testing.expect(std.mem.indexOf(u8, rendered, "Initialize project structure") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered, "Implement core functionality") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered, "Write unit tests") != null);
    // Status indicators present
    try std.testing.expect(std.mem.indexOf(u8, rendered, "✓") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered, "⋯") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered, "○") != null);
}

test "TodoList: renderToString empty list is empty" {
    const alloc = std.testing.allocator;
    var list = TodoList.init(alloc);
    defer list.deinit();

    const rendered = try list.renderToString(alloc);
    defer alloc.free(rendered);
    try std.testing.expectEqualStrings("", rendered);
}

// ---------------------------------------------------------------------------
// execute (TodoWrite JSON tool)
// ---------------------------------------------------------------------------

test "TodoWrite execute: parses valid todos array" {
    const alloc = std.testing.allocator;
    const json_text =
        \\{
        \\  "todos": [
        \\    {"content": "Task A", "activeForm": "Doing A", "status": "pending"},
        \\    {"content": "Task B", "activeForm": "Doing B", "status": "in_progress"},
        \\    {"content": "Task C", "activeForm": "Doing C", "status": "completed"}
        \\  ]
        \\}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json_text, .{});
    defer parsed.deinit();

    var result = try todo_mod.execute(alloc, parsed.value);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "success") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "Task A") != null);
}

test "TodoWrite execute: missing todos parameter returns error" {
    const alloc = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, "{}", .{});
    defer parsed.deinit();

    const result = try todo_mod.execute(alloc, parsed.value);
    try std.testing.expect(result.is_error);
}

test "TodoWrite execute: empty todos array succeeds with zero items" {
    const alloc = std.testing.allocator;
    const json_text = "{\"todos\":[]}";
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json_text, .{});
    defer parsed.deinit();

    var result = try todo_mod.execute(alloc, parsed.value);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "\"added\":0") != null);
}

test "TodoWrite execute: items with invalid status are skipped" {
    const alloc = std.testing.allocator;
    const json_text =
        \\{"todos":[
        \\  {"content":"Good", "activeForm":"Doing good", "status":"pending"},
        \\  {"content":"Bad",  "activeForm":"Doing bad",  "status":"invalid_status"}
        \\]}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json_text, .{});
    defer parsed.deinit();

    var result = try todo_mod.execute(alloc, parsed.value);
    defer result.deinit(alloc);

    try std.testing.expect(!result.is_error);
    // Only 1 item added out of 2 total
    try std.testing.expect(std.mem.indexOf(u8, result.content, "\"added\":1") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "\"total\":2") != null);
}
