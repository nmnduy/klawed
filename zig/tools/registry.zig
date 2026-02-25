//! tools/registry.zig — Tool registry, executor, and definitions
//!
//! Zig port of src/tools/tool_definitions.c + tool_executor.c + tool_registry.c
//!
//! Provides:
//!   - `ToolDef` — a tool definition with name, description, schema, and handler
//!   - `registry` — the global list of built-in tool definitions
//!   - `findTool` — look up a tool by name
//!   - `dispatch` — execute a tool by name with JSON input

const std = @import("std");
const utils = @import("utils.zig");
const bash = @import("bash.zig");
const filesystem = @import("filesystem.zig");
const search = @import("search.zig");
const todo = @import("todo.zig");
const subagent = @import("subagent.zig");
const image = @import("image.zig");
const sleep_tool = @import("sleep.zig");

pub const ToolResult = utils.ToolResult;

// ---------------------------------------------------------------------------
// Tool definition type
// ---------------------------------------------------------------------------

/// A built-in tool definition.
pub const ToolDef = struct {
    /// Tool name as sent to the API (e.g. "Bash", "Read").
    name: []const u8,
    /// Short human-readable description.
    description: []const u8,
    /// JSON schema for the input parameters (a JSON string literal).
    input_schema: []const u8,
    /// Execute function pointer.
    execute: *const fn (std.mem.Allocator, std.json.Value) anyerror!ToolResult,
};

// ---------------------------------------------------------------------------
// JSON schemas (abbreviated — full schemas live in the API definition layer)
// ---------------------------------------------------------------------------

const BASH_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "command": {"type": "string", "description": "The command to execute"},
    \\    "timeout": {"type": "integer", "description": "Timeout in seconds (default 30)"}
    \\  },
    \\  "required": ["command"]
    \\}
;

const READ_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "file_path": {"type": "string"},
    \\    "start_line": {"type": "integer"},
    \\    "end_line": {"type": "integer"}
    \\  },
    \\  "required": ["file_path"]
    \\}
;

const WRITE_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "file_path": {"type": "string"},
    \\    "content": {"type": "string"}
    \\  },
    \\  "required": ["file_path", "content"]
    \\}
;

const EDIT_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "file_path": {"type": "string"},
    \\    "old_string": {"type": "string"},
    \\    "new_string": {"type": "string"}
    \\  },
    \\  "required": ["file_path", "old_string", "new_string"]
    \\}
;

const MULTIEDIT_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "file_path": {"type": "string"},
    \\    "edits": {
    \\      "type": "array",
    \\      "items": {
    \\        "type": "object",
    \\        "properties": {
    \\          "old_string": {"type": "string"},
    \\          "new_string": {"type": "string"}
    \\        },
    \\        "required": ["old_string", "new_string"]
    \\      }
    \\    }
    \\  },
    \\  "required": ["file_path", "edits"]
    \\}
;

const GLOB_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "pattern": {"type": "string"},
    \\    "path": {"type": "string"}
    \\  },
    \\  "required": ["pattern"]
    \\}
;

const GREP_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "pattern": {"type": "string"},
    \\    "path": {"type": "string"},
    \\    "max_results": {"type": "integer"}
    \\  },
    \\  "required": ["pattern"]
    \\}
;

const TODO_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "todos": {
    \\      "type": "array",
    \\      "items": {
    \\        "type": "object",
    \\        "properties": {
    \\          "content": {"type": "string"},
    \\          "activeForm": {"type": "string"},
    \\          "status": {"type": "string", "enum": ["pending", "in_progress", "completed"]}
    \\        },
    \\        "required": ["content", "activeForm", "status"]
    \\      }
    \\    }
    \\  },
    \\  "required": ["todos"]
    \\}
;

const SUBAGENT_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "prompt": {"type": "string"},
    \\    "timeout": {"type": "integer"},
    \\    "provider": {"type": "string"},
    \\    "working_dir": {"type": "string"}
    \\  },
    \\  "required": ["prompt"]
    \\}
;

const CHECK_SUBAGENT_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "log_file": {"type": "string"},
    \\    "tail_lines": {"type": "integer"}
    \\  },
    \\  "required": ["log_file"]
    \\}
;

const INTERRUPT_SUBAGENT_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "pid": {"type": "integer"}
    \\  },
    \\  "required": ["pid"]
    \\}
;

const IMAGE_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "file_path": {"type": "string"}
    \\  },
    \\  "required": ["file_path"]
    \\}
;

const SLEEP_SCHEMA =
    \\{
    \\  "type": "object",
    \\  "properties": {
    \\    "duration": {"type": "integer", "description": "Duration in seconds"}
    \\  },
    \\  "required": ["duration"]
    \\}
;

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

/// The global built-in tool registry.
pub const registry: []const ToolDef = &.{
    .{
        .name = "Sleep",
        .description = "Pause execution for a specified duration",
        .input_schema = SLEEP_SCHEMA,
        .execute = sleep_tool.execute,
    },
    .{
        .name = "Bash",
        .description = "Execute bash commands with timeout protection",
        .input_schema = BASH_SCHEMA,
        .execute = bash.execute,
    },
    .{
        .name = "Read",
        .description = "Read a file from the filesystem with optional line range support",
        .input_schema = READ_SCHEMA,
        .execute = filesystem.executeRead,
    },
    .{
        .name = "Write",
        .description = "Write content to a file",
        .input_schema = WRITE_SCHEMA,
        .execute = filesystem.executeWrite,
    },
    .{
        .name = "Edit",
        .description = "Replace the first occurrence of a string in a file",
        .input_schema = EDIT_SCHEMA,
        .execute = filesystem.executeEdit,
    },
    .{
        .name = "MultiEdit",
        .description = "Apply multiple string replacements to a file",
        .input_schema = MULTIEDIT_SCHEMA,
        .execute = filesystem.executeMultiEdit,
    },
    .{
        .name = "Glob",
        .description = "Find files matching a glob pattern",
        .input_schema = GLOB_SCHEMA,
        .execute = filesystem.executeGlob,
    },
    .{
        .name = "Grep",
        .description = "Search for patterns in files",
        .input_schema = GREP_SCHEMA,
        .execute = search.execute,
    },
    .{
        .name = "TodoWrite",
        .description = "Create and update a task list to track progress",
        .input_schema = TODO_SCHEMA,
        .execute = todo.execute,
    },
    .{
        .name = "Subagent",
        .description = "Delegate a task to a fresh klawed process with its own context",
        .input_schema = SUBAGENT_SCHEMA,
        .execute = subagent.executeSubagent,
    },
    .{
        .name = "CheckSubagentProgress",
        .description = "Check the progress of a running subagent by reading its log file",
        .input_schema = CHECK_SUBAGENT_SCHEMA,
        .execute = subagent.executeCheckProgress,
    },
    .{
        .name = "InterruptSubagent",
        .description = "Interrupt a running subagent process",
        .input_schema = INTERRUPT_SUBAGENT_SCHEMA,
        .execute = subagent.executeInterrupt,
    },
    .{
        .name = "UploadImage",
        .description = "Upload an image file to include in the conversation context",
        .input_schema = IMAGE_SCHEMA,
        .execute = image.execute,
    },
};

// ---------------------------------------------------------------------------
// Lookup and dispatch
// ---------------------------------------------------------------------------

/// Find a tool definition by name.  Returns null if not found.
pub fn findTool(name: []const u8) ?*const ToolDef {
    for (registry) |*tool| {
        if (std.mem.eql(u8, tool.name, name)) return tool;
    }
    return null;
}

/// Dispatch a tool call by name, parsing JSON input from `input_json`.
/// Returns a ToolResult; caller must call `deinit` when done.
pub fn dispatch(
    allocator: std.mem.Allocator,
    name: []const u8,
    input_json: []const u8,
) !ToolResult {
    const tool = findTool(name) orelse {
        return utils.errFmt(allocator, "Unknown tool: {s}", .{name});
    };

    const parsed = std.json.parseFromSlice(std.json.Value, allocator, input_json, .{}) catch {
        return utils.errFmt(allocator, "Invalid JSON input for tool '{s}'", .{name});
    };
    defer parsed.deinit();

    return tool.execute(allocator, parsed.value) catch |e| {
        return utils.errFmt(allocator, "Tool '{s}' failed: {s}", .{ name, @errorName(e) });
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "registry: all tools have non-empty names" {
    for (registry) |tool| {
        try std.testing.expect(tool.name.len > 0);
        try std.testing.expect(tool.description.len > 0);
        try std.testing.expect(tool.input_schema.len > 0);
    }
}

test "findTool: finds known tools" {
    const bash_def = findTool("Bash");
    try std.testing.expect(bash_def != null);
    try std.testing.expectEqualStrings("Bash", bash_def.?.name);

    const read_def = findTool("Read");
    try std.testing.expect(read_def != null);

    const todo_def = findTool("TodoWrite");
    try std.testing.expect(todo_def != null);
}

test "findTool: unknown tool returns null" {
    try std.testing.expect(findTool("NonExistentTool") == null);
    try std.testing.expect(findTool("") == null);
}

test "findTool: all registry tools are findable" {
    for (registry) |tool| {
        const found = findTool(tool.name);
        try std.testing.expect(found != null);
    }
}

test "dispatch: unknown tool returns error result" {
    const allocator = std.testing.allocator;
    var result = try dispatch(allocator, "NoSuchTool", "{}");
    defer result.deinit(allocator);
    try std.testing.expect(result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "Unknown tool") != null);
}

test "dispatch: Sleep tool executes successfully" {
    const allocator = std.testing.allocator;
    var result = try dispatch(allocator, "Sleep", "{\"duration\":0}");
    defer result.deinit(allocator);
    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "success") != null);
}

test "dispatch: TodoWrite tool works" {
    const allocator = std.testing.allocator;
    const input =
        \\{"todos":[{"content":"test","activeForm":"testing","status":"pending"}]}
    ;
    var result = try dispatch(allocator, "TodoWrite", input);
    defer result.deinit(allocator);
    try std.testing.expect(!result.is_error);
}

test "dispatch: invalid JSON returns error" {
    const allocator = std.testing.allocator;
    var result = try dispatch(allocator, "Bash", "not json");
    defer result.deinit(allocator);
    try std.testing.expect(result.is_error);
}
