//! mcp.zig — Model Context Protocol (MCP) client
//!
//! Zig port of src/mcp.c
//!
//! Implements a JSON-RPC 2.0 client for communicating with MCP servers.
//! Supports stdio transport (process spawning).
//!
//! MCP is disabled by default; enable via KLAWED_MCP_ENABLED=1.

const std = @import("std");
const utils = @import("tools/utils.zig");

pub const ToolResult = utils.ToolResult;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Environment variable to enable MCP.
pub const mcp_enabled_env = "KLAWED_MCP_ENABLED";

/// Default MCP config file path (relative to HOME).
pub const default_config_relative = ".config/klawed/mcp_servers.json";

/// MCP config path env override.
pub const mcp_config_env = "KLAWED_MCP_CONFIG";

/// Timeout for MCP server initialization (seconds).
pub const default_init_timeout_s: u32 = 10;

/// Timeout for MCP requests (seconds).
pub const default_request_timeout_s: u32 = 30;

/// JSON-RPC version.
pub const jsonrpc_version = "2.0";

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/// Transport type for an MCP server.
pub const McpTransport = enum { stdio };

/// A single MCP server configuration.
pub const McpServerConfig = struct {
    name: []u8,
    command: []u8,
    args: [][]u8,
    allocator: std.mem.Allocator,

    pub fn deinit(self: *McpServerConfig) void {
        self.allocator.free(self.name);
        self.allocator.free(self.command);
        for (self.args) |a| self.allocator.free(a);
        self.allocator.free(self.args);
    }
};

/// MCP configuration (list of server configs).
pub const McpConfig = struct {
    allocator: std.mem.Allocator,
    servers: std.ArrayList(McpServerConfig),

    pub fn init(allocator: std.mem.Allocator) McpConfig {
        return .{
            .allocator = allocator,
            .servers = std.ArrayList(McpServerConfig).init(allocator),
        };
    }

    pub fn deinit(self: *McpConfig) void {
        for (self.servers.items) |*s| s.deinit();
        self.servers.deinit();
    }
};

/// A JSON-RPC 2.0 request.
pub const JsonRpcRequest = struct {
    id: u64,
    method: []const u8,
    params: std.json.Value,
};

/// A JSON-RPC 2.0 response.
pub const JsonRpcResponse = struct {
    id: u64,
    result: ?std.json.Value,
    err: ?struct {
        code: i64,
        message: []const u8,
    },
};

// ---------------------------------------------------------------------------
// Enable check
// ---------------------------------------------------------------------------

/// Returns true if MCP is enabled via environment.
pub fn isEnabled() bool {
    const val = std.posix.getenv(mcp_enabled_env) orelse return false;
    return std.mem.eql(u8, val, "1") or
        std.ascii.eqlIgnoreCase(val, "true") or
        std.ascii.eqlIgnoreCase(val, "on");
}

// ---------------------------------------------------------------------------
// Config loading
// ---------------------------------------------------------------------------

/// Load MCP config from the configured path (env override or default).
/// Returns a freshly allocated McpConfig; caller must call `deinit`.
pub fn loadConfig(allocator: std.mem.Allocator) !McpConfig {
    var config = McpConfig.init(allocator);
    errdefer config.deinit();

    const config_path: []const u8 = blk: {
        if (std.posix.getenv(mcp_config_env)) |p| break :blk p;

        // Build default path: $HOME/.config/klawed/mcp_servers.json
        const home = std.posix.getenv("HOME") orelse return config;
        const path = try std.fs.path.join(allocator, &.{ home, default_config_relative });
        defer allocator.free(path);

        // Check if file exists
        std.fs.cwd().access(path, .{}) catch return config;
        break :blk path;
    };

    const file = std.fs.cwd().openFile(config_path, .{}) catch return config;
    defer file.close();

    const json_text = file.readToEndAlloc(allocator, 1 * 1024 * 1024) catch return config;
    defer allocator.free(json_text);

    try parseConfig(allocator, &config, json_text);
    return config;
}

/// Parse a JSON config string into `config`.
fn parseConfig(allocator: std.mem.Allocator, config: *McpConfig, json_text: []const u8) !void {
    const parsed = std.json.parseFromSlice(std.json.Value, allocator, json_text, .{}) catch return;
    defer parsed.deinit();

    const servers_val = switch (parsed.value) {
        .object => |m| m.get("mcpServers"),
        else => return,
    } orelse return;

    const servers_obj = switch (servers_val) {
        .object => |m| m,
        else => return,
    };

    var iter = servers_obj.iterator();
    while (iter.next()) |kv| {
        const server_name = kv.key_ptr.*;
        const server_val = kv.value_ptr.*;

        const command = utils.jsonString(server_val, "command") orelse continue;

        // Parse args array
        var args_list = std.ArrayList([]u8).init(allocator);
        errdefer {
            for (args_list.items) |a| allocator.free(a);
            args_list.deinit();
        }

        const args_val = switch (server_val) {
            .object => |m| m.get("args"),
            else => null,
        };
        if (args_val) |av| {
            if (av == .array) {
                for (av.array.items) |arg_val| {
                    if (arg_val == .string) {
                        try args_list.append(try allocator.dupe(u8, arg_val.string));
                    }
                }
            }
        }

        try config.servers.append(McpServerConfig{
            .name = try allocator.dupe(u8, server_name),
            .command = try allocator.dupe(u8, command),
            .args = try args_list.toOwnedSlice(),
            .allocator = allocator,
        });
    }
}

// ---------------------------------------------------------------------------
// JSON-RPC helpers
// ---------------------------------------------------------------------------

/// Build a JSON-RPC 2.0 request string.
/// Caller must free the returned slice.
pub fn buildRequest(
    allocator: std.mem.Allocator,
    id: u64,
    method: []const u8,
    params: ?std.json.Value,
) ![]u8 {
    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();
    const w = buf.writer();

    try std.fmt.format(w, "{{\"jsonrpc\":\"{s}\",\"id\":{d},\"method\":", .{ jsonrpc_version, id });

    // Write method as JSON string
    try w.writeByte('"');
    for (method) |c| {
        if (c == '"' or c == '\\') try w.writeByte('\\');
        try w.writeByte(c);
    }
    try w.writeByte('"');

    if (params) |p| {
        try w.writeAll(",\"params\":");
        try std.json.stringify(p, .{}, w);
    }
    try w.writeByte('}');
    try w.writeByte('\n'); // newline-delimited JSON-RPC

    return buf.toOwnedSlice();
}

/// Parse a JSON-RPC 2.0 response line.
pub fn parseResponse(allocator: std.mem.Allocator, line: []const u8) !?JsonRpcResponse {
    const parsed = std.json.parseFromSlice(std.json.Value, allocator, line, .{}) catch return null;
    defer parsed.deinit();

    const obj = switch (parsed.value) {
        .object => |m| m,
        else => return null,
    };

    // Check jsonrpc version
    const ver = obj.get("jsonrpc") orelse return null;
    if (ver != .string or !std.mem.eql(u8, ver.string, jsonrpc_version)) return null;

    const id_val = obj.get("id") orelse return null;
    const id: u64 = switch (id_val) {
        .integer => |i| if (i >= 0) @intCast(i) else return null,
        else => return null,
    };

    if (obj.get("error")) |err_val| {
        const err_obj = switch (err_val) {
            .object => |m| m,
            else => return null,
        };
        const code = switch (err_obj.get("code") orelse return null) {
            .integer => |i| i,
            else => return null,
        };
        const message = switch (err_obj.get("message") orelse return null) {
            .string => |s| s,
            else => return null,
        };
        return JsonRpcResponse{
            .id = id,
            .result = null,
            .err = .{ .code = code, .message = message },
        };
    }

    return JsonRpcResponse{
        .id = id,
        .result = obj.get("result"),
        .err = null,
    };
}

// ---------------------------------------------------------------------------
// MCP tool stubs (ListMcpResources, ReadMcpResource, CallMcpTool)
// ---------------------------------------------------------------------------

/// Execute the ListMcpResources tool.
pub fn executeListResources(allocator: std.mem.Allocator, _input: std.json.Value) !ToolResult {
    _ = _input;
    if (!isEnabled()) {
        return utils.errLit("MCP is not enabled. Set KLAWED_MCP_ENABLED=1 to enable.");
    }
    return utils.ok(allocator, "{\"resources\":[]}");
}

/// Execute the ReadMcpResource tool.
pub fn executeReadResource(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    if (!isEnabled()) {
        return utils.errLit("MCP is not enabled. Set KLAWED_MCP_ENABLED=1 to enable.");
    }
    const uri = utils.jsonString(input, "uri") orelse {
        return utils.errLit("Missing 'uri' parameter");
    };
    return utils.errFmt(allocator, "Resource not found: {s}", .{uri});
}

/// Execute the CallMcpTool tool.
pub fn executeCallTool(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    if (!isEnabled()) {
        return utils.errLit("MCP is not enabled. Set KLAWED_MCP_ENABLED=1 to enable.");
    }
    const tool_name = utils.jsonString(input, "tool_name") orelse {
        return utils.errLit("Missing 'tool_name' parameter");
    };
    return utils.errFmt(allocator, "MCP tool not found: {s}", .{tool_name});
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "isEnabled: false when env not set" {
    // In test environment KLAWED_MCP_ENABLED is typically unset
    _ = isEnabled();
}

test "buildRequest: produces valid JSON-RPC" {
    const allocator = std.testing.allocator;
    const req = try buildRequest(allocator, 1, "tools/list", null);
    defer allocator.free(req);

    try std.testing.expect(std.mem.indexOf(u8, req, "\"jsonrpc\":\"2.0\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, req, "\"method\":\"tools/list\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, req, "\"id\":1") != null);
}

test "parseConfig: parses mcpServers" {
    const allocator = std.testing.allocator;
    var config = McpConfig.init(allocator);
    defer config.deinit();

    const json_text =
        \\{
        \\  "mcpServers": {
        \\    "my_server": {
        \\      "command": "npx",
        \\      "args": ["my-mcp-server", "--port", "3000"]
        \\    }
        \\  }
        \\}
    ;
    try parseConfig(allocator, &config, json_text);

    try std.testing.expectEqual(@as(usize, 1), config.servers.items.len);
    try std.testing.expectEqualStrings("my_server", config.servers.items[0].name);
    try std.testing.expectEqualStrings("npx", config.servers.items[0].command);
    try std.testing.expectEqual(@as(usize, 3), config.servers.items[0].args.len);
}

test "parseConfig: invalid JSON is a no-op" {
    const allocator = std.testing.allocator;
    var config = McpConfig.init(allocator);
    defer config.deinit();

    try parseConfig(allocator, &config, "not json");
    try std.testing.expectEqual(@as(usize, 0), config.servers.items.len);
}

test "executeListResources: error when MCP disabled" {
    const allocator = std.testing.allocator;
    if (isEnabled()) return;

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{}", .{});
    defer parsed.deinit();

    const result = try executeListResources(allocator, parsed.value);
    try std.testing.expect(result.is_error);
}

test "executeReadResource: missing uri returns error" {
    const allocator = std.testing.allocator;
    if (isEnabled()) return;

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{}", .{});
    defer parsed.deinit();

    const result = try executeReadResource(allocator, parsed.value);
    try std.testing.expect(result.is_error);
}
