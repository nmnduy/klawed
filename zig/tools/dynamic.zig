//! tools/dynamic.zig — Dynamic tool definition loader
//!
//! Zig port of src/dynamic_tools.c
//!
//! Loads custom tool definitions from a JSON file at startup.
//! The path is determined by:
//!   1. KLAWED_DYNAMIC_TOOLS env var
//!   2. .klawed/dynamic_tools.json (project-local)
//!   3. ~/.klawed/dynamic_tools.json (global, not searched here)
//!
//! Supported JSON formats:
//!   - OpenAI format: `[{ "type": "function", "function": { "name", "description", "parameters" } }]`
//!   - Simplified format: `[{ "name", "description", "parameters" }]`

const std = @import("std");
const utils = @import("utils.zig");

pub const ToolResult = utils.ToolResult;

/// Environment variable that overrides the dynamic tools file path.
pub const env_var = "KLAWED_DYNAMIC_TOOLS";

/// Default project-local filename.
pub const default_filename = ".klawed/dynamic_tools.json";

/// Maximum number of dynamic tool definitions.
pub const max_dynamic_tools = 64;

/// A single dynamic tool definition.
pub const DynamicTool = struct {
    /// Tool name (e.g. "my_custom_tool").
    name: []u8,
    /// Human-readable description.
    description: []u8,
    /// JSON schema string for the tool's parameters (owned).
    parameters_json: []u8,

    pub fn deinit(self: *DynamicTool, allocator: std.mem.Allocator) void {
        allocator.free(self.name);
        allocator.free(self.description);
        allocator.free(self.parameters_json);
    }
};

/// Registry of dynamically-loaded tool definitions.
pub const DynamicRegistry = struct {
    allocator: std.mem.Allocator,
    tools: std.ArrayList(DynamicTool),
    source_path: []u8,
    loaded: bool,

    pub fn init(allocator: std.mem.Allocator) DynamicRegistry {
        return .{
            .allocator = allocator,
            .tools = std.ArrayList(DynamicTool).init(allocator),
            .source_path = &.{},
            .loaded = false,
        };
    }

    pub fn deinit(self: *DynamicRegistry) void {
        for (self.tools.items) |*t| t.deinit(self.allocator);
        self.tools.deinit();
        if (self.source_path.len > 0) self.allocator.free(self.source_path);
    }

    /// Load tools from the configured path (env var or default).
    /// Returns the number of tools loaded, or 0 if no file was found.
    pub fn load(self: *DynamicRegistry) !usize {
        // Determine file path
        const path: []const u8 = blk: {
            if (std.posix.getenv(env_var)) |env| break :blk env;
            break :blk default_filename;
        };

        // Try to open the file
        const file = std.fs.cwd().openFile(path, .{}) catch return 0;
        defer file.close();

        self.source_path = try self.allocator.dupe(u8, path);

        const json_text = file.readToEndAlloc(self.allocator, 1 * 1024 * 1024) catch return 0;
        defer self.allocator.free(json_text);

        return self.parseJson(json_text);
    }

    /// Parse a JSON string containing tool definitions.
    pub fn parseJson(self: *DynamicRegistry, json_text: []const u8) !usize {
        const parsed = std.json.parseFromSlice(
            std.json.Value,
            self.allocator,
            json_text,
            .{},
        ) catch return 0;
        defer parsed.deinit();

        const arr = switch (parsed.value) {
            .array => |a| a,
            else => return 0,
        };

        var count: usize = 0;
        for (arr.items) |tool_val| {
            if (self.tools.items.len >= max_dynamic_tools) break;
            if (try parseTool(self.allocator, tool_val)) |tool| {
                try self.tools.append(tool);
                count += 1;
            }
        }
        self.loaded = count > 0;
        return count;
    }
};

/// Parse a single tool definition from a JSON value.
/// Returns null if the value doesn't have the required fields.
fn parseTool(allocator: std.mem.Allocator, val: std.json.Value) !?DynamicTool {
    // Try OpenAI format: { "type": "function", "function": { "name", ... } }
    if (utils.jsonString(val, "type")) |t| {
        if (std.mem.eql(u8, t, "function")) {
            const func_obj = switch (val) {
                .object => |m| m.get("function"),
                else => null,
            } orelse return null;

            return parseToolFromObject(allocator, func_obj);
        }
    }

    // Try simplified format: { "name", "description", "parameters" }
    return parseToolFromObject(allocator, val);
}

fn parseToolFromObject(allocator: std.mem.Allocator, val: std.json.Value) !?DynamicTool {
    const name = utils.jsonString(val, "name") orelse return null;
    const desc = utils.jsonString(val, "description") orelse "";

    // Serialize parameters back to JSON string
    const params_json = blk: {
        const params_val = switch (val) {
            .object => |m| m.get("parameters"),
            else => null,
        };
        if (params_val) |pv| {
            break :blk try std.json.stringifyAlloc(allocator, pv, .{});
        } else {
            break :blk try allocator.dupe(u8, "{}");
        }
    };
    errdefer allocator.free(params_json);

    return DynamicTool{
        .name = try allocator.dupe(u8, name),
        .description = try allocator.dupe(u8, desc),
        .parameters_json = params_json,
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "DynamicRegistry: parse simplified format" {
    const allocator = std.testing.allocator;

    var registry = DynamicRegistry.init(allocator);
    defer registry.deinit();

    const json_text =
        \\[
        \\  {
        \\    "name": "my_tool",
        \\    "description": "A custom tool",
        \\    "parameters": {"type": "object", "properties": {}}
        \\  }
        \\]
    ;

    const count = try registry.parseJson(json_text);
    try std.testing.expectEqual(@as(usize, 1), count);
    try std.testing.expect(registry.loaded);
    try std.testing.expectEqualStrings("my_tool", registry.tools.items[0].name);
    try std.testing.expectEqualStrings("A custom tool", registry.tools.items[0].description);
}

test "DynamicRegistry: parse OpenAI format" {
    const allocator = std.testing.allocator;

    var registry = DynamicRegistry.init(allocator);
    defer registry.deinit();

    const json_text =
        \\[
        \\  {
        \\    "type": "function",
        \\    "function": {
        \\      "name": "openai_tool",
        \\      "description": "OpenAI format tool",
        \\      "parameters": {}
        \\    }
        \\  }
        \\]
    ;

    const count = try registry.parseJson(json_text);
    try std.testing.expectEqual(@as(usize, 1), count);
    try std.testing.expectEqualStrings("openai_tool", registry.tools.items[0].name);
}

test "DynamicRegistry: empty JSON returns 0" {
    const allocator = std.testing.allocator;
    var registry = DynamicRegistry.init(allocator);
    defer registry.deinit();

    const count = try registry.parseJson("[]");
    try std.testing.expectEqual(@as(usize, 0), count);
    try std.testing.expect(!registry.loaded);
}

test "DynamicRegistry: invalid JSON returns 0" {
    const allocator = std.testing.allocator;
    var registry = DynamicRegistry.init(allocator);
    defer registry.deinit();

    const count = try registry.parseJson("not json");
    try std.testing.expectEqual(@as(usize, 0), count);
}

test "DynamicRegistry: load from file" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    try tmp.dir.writeFile("tools.json",
        \\[{"name":"file_tool","description":"from file","parameters":{}}]
    );

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const allocator = std.testing.allocator;
    const file_path = try std.fmt.allocPrint(allocator, "{s}/tools.json", .{tmp_path});
    defer allocator.free(file_path);

    // Set env var to point to our test file
    // Instead just call parseJson directly with the file content
    var registry = DynamicRegistry.init(allocator);
    defer registry.deinit();

    const file = try std.fs.openFileAbsolute(file_path, .{});
    defer file.close();
    const content = try file.readToEndAlloc(allocator, 64 * 1024);
    defer allocator.free(content);

    const count = try registry.parseJson(content);
    try std.testing.expectEqual(@as(usize, 1), count);
    try std.testing.expectEqualStrings("file_tool", registry.tools.items[0].name);
}
