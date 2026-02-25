//! tools/todo.zig — TodoWrite tool implementation
//!
//! Zig port of src/tools/tool_todo.c + src/todo.c
//!
//! Manages a structured todo list with three states:
//!   - pending    (○)
//!   - in_progress (⋯)
//!   - completed  (✓)
//!
//! The TUI rendering is deferred to Phase 9.  This module focuses on:
//!   - TodoItem / TodoList data structures
//!   - JSON parsing of the `todos` parameter
//!   - Plain-text rendering for the tool result

const std = @import("std");
const utils = @import("utils.zig");

pub const ToolResult = utils.ToolResult;

// ---------------------------------------------------------------------------
// Data types
// ---------------------------------------------------------------------------

pub const TodoStatus = enum {
    pending,
    in_progress,
    completed,

    /// Parse from string representation.
    pub fn fromString(s: []const u8) ?TodoStatus {
        if (std.mem.eql(u8, s, "pending")) return .pending;
        if (std.mem.eql(u8, s, "in_progress")) return .in_progress;
        if (std.mem.eql(u8, s, "completed")) return .completed;
        return null;
    }

    pub fn toString(self: TodoStatus) []const u8 {
        return switch (self) {
            .pending => "pending",
            .in_progress => "in_progress",
            .completed => "completed",
        };
    }

    /// Unicode indicator character for plain-text rendering.
    pub fn indicator(self: TodoStatus) []const u8 {
        return switch (self) {
            .pending => "○",
            .in_progress => "⋯",
            .completed => "✓",
        };
    }
};

/// A single todo item.  All strings are owned by the containing `TodoList`.
pub const TodoItem = struct {
    content: []u8,
    active_form: []u8,
    status: TodoStatus,
};

/// A dynamic list of todo items.
pub const TodoList = struct {
    allocator: std.mem.Allocator,
    items: std.ArrayList(TodoItem),

    pub fn init(allocator: std.mem.Allocator) TodoList {
        return .{
            .allocator = allocator,
            .items = std.ArrayList(TodoItem).init(allocator),
        };
    }

    pub fn deinit(self: *TodoList) void {
        for (self.items.items) |item| {
            self.allocator.free(item.content);
            self.allocator.free(item.active_form);
        }
        self.items.deinit();
    }

    pub fn clear(self: *TodoList) void {
        for (self.items.items) |item| {
            self.allocator.free(item.content);
            self.allocator.free(item.active_form);
        }
        self.items.clearRetainingCapacity();
    }

    /// Add a new item.  Strings are duplicated into the list's allocator.
    pub fn add(
        self: *TodoList,
        content: []const u8,
        active_form: []const u8,
        status: TodoStatus,
    ) !void {
        const c = try self.allocator.dupe(u8, content);
        errdefer self.allocator.free(c);
        const a = try self.allocator.dupe(u8, active_form);
        errdefer self.allocator.free(a);
        try self.items.append(.{ .content = c, .active_form = a, .status = status });
    }

    /// Count items with a given status.
    pub fn countByStatus(self: TodoList, status: TodoStatus) usize {
        var n: usize = 0;
        for (self.items.items) |item| {
            if (item.status == status) n += 1;
        }
        return n;
    }

    /// Render to a plain-text string (no ANSI colors).
    /// Caller must free the returned slice.
    pub fn renderToString(self: TodoList, allocator: std.mem.Allocator) ![]u8 {
        var out = std.ArrayList(u8).init(allocator);
        defer out.deinit();
        const w = out.writer();

        for (self.items.items) |item| {
            try w.print("  {s} {s}\n", .{ item.status.indicator(), item.content });
        }
        return out.toOwnedSlice();
    }
};

// ---------------------------------------------------------------------------
// Execute (TodoWrite tool)
// ---------------------------------------------------------------------------

/// Execute the TodoWrite tool.
///
/// Input:
/// ```json
/// {
///   "todos": [
///     { "content": "Run tests", "activeForm": "Running tests", "status": "pending" },
///     ...
///   ]
/// }
/// ```
///
/// Returns the rendered todo list as a JSON result.
pub fn execute(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const todos_val = switch (input) {
        .object => |m| m.get("todos"),
        else => null,
    } orelse return utils.errLit("Missing or invalid 'todos' parameter (must be array)");

    const todos_arr = switch (todos_val) {
        .array => |a| a,
        else => return utils.errLit("Missing or invalid 'todos' parameter (must be array)"),
    };

    var list = TodoList.init(allocator);
    defer list.deinit();

    var added: usize = 0;
    const total = todos_arr.items.len;

    for (todos_arr.items) |todo_val| {
        const content = utils.jsonString(todo_val, "content") orelse continue;
        const active_form = utils.jsonString(todo_val, "activeForm") orelse continue;
        const status_str = utils.jsonString(todo_val, "status") orelse continue;
        const status = TodoStatus.fromString(status_str) orelse continue;

        list.add(content, active_form, status) catch continue;
        added += 1;
    }

    // Render the list
    const rendered = try list.renderToString(allocator);
    defer allocator.free(rendered);

    // Build JSON result
    var out = std.ArrayList(u8).init(allocator);
    defer out.deinit();
    const w = out.writer();

    try w.writeAll("{\"status\":\"success\"");
    try std.fmt.format(w, ",\"added\":{d},\"total\":{d}", .{ added, total });
    try w.writeAll(",\"rendered\":");
    try writeJsonString(w, rendered);
    try w.writeByte('}');

    return utils.okOwned(try out.toOwnedSlice());
}

// ---------------------------------------------------------------------------
// JSON string helper
// ---------------------------------------------------------------------------

fn writeJsonString(writer: anytype, s: []const u8) !void {
    try writer.writeByte('"');
    for (s) |c| {
        switch (c) {
            '"' => try writer.writeAll("\\\""),
            '\\' => try writer.writeAll("\\\\"),
            '\n' => try writer.writeAll("\\n"),
            '\r' => try writer.writeAll("\\r"),
            '\t' => try writer.writeAll("\\t"),
            0x00...0x08, 0x0B...0x0C, 0x0E...0x1F => try std.fmt.format(writer, "\\u{X:0>4}", .{c}),
            else => try writer.writeByte(c),
        }
    }
    try writer.writeByte('"');
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "TodoStatus.fromString" {
    try std.testing.expect(TodoStatus.fromString("pending") == .pending);
    try std.testing.expect(TodoStatus.fromString("in_progress") == .in_progress);
    try std.testing.expect(TodoStatus.fromString("completed") == .completed);
    try std.testing.expect(TodoStatus.fromString("unknown") == null);
}

test "TodoList: add and clear" {
    const allocator = std.testing.allocator;
    var list = TodoList.init(allocator);
    defer list.deinit();

    try list.add("Run tests", "Running tests", .pending);
    try list.add("Deploy", "Deploying", .in_progress);
    try std.testing.expectEqual(@as(usize, 2), list.items.items.len);

    list.clear();
    try std.testing.expectEqual(@as(usize, 0), list.items.items.len);
}

test "TodoList: countByStatus" {
    const allocator = std.testing.allocator;
    var list = TodoList.init(allocator);
    defer list.deinit();

    try list.add("a", "a", .pending);
    try list.add("b", "b", .pending);
    try list.add("c", "c", .completed);

    try std.testing.expectEqual(@as(usize, 2), list.countByStatus(.pending));
    try std.testing.expectEqual(@as(usize, 1), list.countByStatus(.completed));
    try std.testing.expectEqual(@as(usize, 0), list.countByStatus(.in_progress));
}

test "TodoList: renderToString" {
    const allocator = std.testing.allocator;
    var list = TodoList.init(allocator);
    defer list.deinit();

    try list.add("Run tests", "Running tests", .pending);
    try list.add("Deploy", "Deploying", .completed);

    const rendered = try list.renderToString(allocator);
    defer allocator.free(rendered);

    try std.testing.expect(std.mem.indexOf(u8, rendered, "Run tests") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered, "Deploy") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered, "✓") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered, "○") != null);
}

test "execute TodoWrite: parses todos and renders" {
    const allocator = std.testing.allocator;
    const json_text =
        \\{
        \\  "todos": [
        \\    {"content": "Write code", "activeForm": "Writing code", "status": "in_progress"},
        \\    {"content": "Run tests",  "activeForm": "Running tests", "status": "pending"},
        \\    {"content": "Deploy",     "activeForm": "Deploying",    "status": "completed"}
        \\  ]
        \\}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try execute(allocator, parsed.value);
    defer result.deinit(allocator);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "success") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "added") != null);
    // rendered field should contain todo content
    try std.testing.expect(std.mem.indexOf(u8, result.content, "Write code") != null);
}

test "execute TodoWrite: missing todos returns error" {
    const allocator = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{}", .{});
    defer parsed.deinit();

    const result = try execute(allocator, parsed.value);
    try std.testing.expect(result.is_error);
}

test "execute TodoWrite: invalid status items are skipped" {
    const allocator = std.testing.allocator;
    const json_text =
        \\{"todos":[
        \\  {"content":"a","activeForm":"a","status":"invalid"},
        \\  {"content":"b","activeForm":"b","status":"pending"}
        \\]}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try execute(allocator, parsed.value);
    defer result.deinit(allocator);

    try std.testing.expect(!result.is_error);
    // Only 1 valid item added out of 2 total
    try std.testing.expect(std.mem.indexOf(u8, result.content, "\"added\":1") != null);
}
