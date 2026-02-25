//! providers/anthropic.zig — Anthropic Messages API provider
//!
//! Zig port of src/anthropic_provider.c.
//!
//! Key differences from OpenAI:
//!   - Different JSON schema (system is top-level, not a message)
//!   - `content` is always an array of typed blocks
//!   - Tool use / result blocks have different field names
//!   - Prompt-caching via `cache_control: {"type":"ephemeral"}` blocks
//!   - Different SSE event types: `content_block_delta`, `message_delta`, etc.
//!
//! ## Phase 5 note
//! HTTP is not implemented; `sendRequest` returns `error.NotImplemented`.

const std = @import("std");

pub const default_url = "https://api.anthropic.com/v1/messages";
pub const anthropic_version = "2023-06-01";

// ---------------------------------------------------------------------------
// Request types (Anthropic schema)
// ---------------------------------------------------------------------------

/// A content block in the Anthropic format.
pub const ContentBlock = union(enum) {
    text: TextBlock,
    image: ImageBlock,
    tool_use: ToolUseBlock,
    tool_result: ToolResultBlock,

    pub const TextBlock = struct {
        text: []const u8,
        /// When true, wrap with cache_control: {type: "ephemeral"}.
        cacheable: bool = false,
    };

    pub const ImageBlock = struct {
        /// Must be "base64".
        source_type: []const u8 = "base64",
        media_type: []const u8,
        data: []const u8,
    };

    pub const ToolUseBlock = struct {
        id: []const u8,
        name: []const u8,
        /// Already-serialized JSON object string.
        input_json: []const u8,
    };

    pub const ToolResultBlock = struct {
        tool_use_id: []const u8,
        content: []const u8,
        is_error: bool = false,
    };
};

pub const Role = enum {
    user,
    assistant,

    pub fn toString(self: Role) []const u8 {
        return switch (self) {
            .user => "user",
            .assistant => "assistant",
        };
    }
};

pub const Message = struct {
    role: Role,
    content: []const ContentBlock,
};

pub const ToolDefinition = struct {
    name: []const u8,
    description: []const u8,
    /// JSON Schema for input parameters (already-serialized).
    input_schema_json: []const u8,
    /// If true, add cache_control to this tool definition.
    cacheable: bool = false,
};

pub const Request = struct {
    model: []const u8,
    messages: []const Message,
    system_prompt: ?[]const u8 = null,
    tools: []const ToolDefinition = &.{},
    max_tokens: u32 = 16384,
    temperature: ?f64 = null,
    stream: bool = false,
};

// ---------------------------------------------------------------------------
// Response types
// ---------------------------------------------------------------------------

pub const Response = struct {
    allocator: std.mem.Allocator,
    id: []const u8,
    model: []const u8,
    stop_reason: []const u8,
    content: []const u8,
    tool_uses: []const ToolUse,
    usage: ?Usage,

    pub const ToolUse = struct {
        id: []const u8,
        name: []const u8,
        /// JSON-encoded input object.
        input_json: []const u8,
    };

    pub const Usage = struct {
        input_tokens: u32,
        output_tokens: u32,
        cache_creation_input_tokens: u32 = 0,
        cache_read_input_tokens: u32 = 0,
    };

    pub fn deinit(self: *Response) void {
        const a = self.allocator;
        a.free(self.id);
        a.free(self.model);
        a.free(self.stop_reason);
        a.free(self.content);
        for (self.tool_uses) |tu| {
            a.free(tu.id);
            a.free(tu.name);
            a.free(tu.input_json);
        }
        a.free(self.tool_uses);
    }
};

// ---------------------------------------------------------------------------
// AnthropicProvider
// ---------------------------------------------------------------------------

pub const AnthropicProvider = struct {
    allocator: std.mem.Allocator,
    api_key: []const u8,
    base_url: []const u8,
    enable_prompt_caching: bool,

    pub fn init(
        allocator: std.mem.Allocator,
        api_key: []const u8,
        base_url: []const u8,
        enable_prompt_caching: bool,
    ) !AnthropicProvider {
        return AnthropicProvider{
            .allocator = allocator,
            .api_key = try allocator.dupe(u8, api_key),
            .base_url = try allocator.dupe(u8, if (base_url.len > 0) base_url else default_url),
            .enable_prompt_caching = enable_prompt_caching,
        };
    }

    pub fn deinit(self: *AnthropicProvider) void {
        self.allocator.free(self.api_key);
        self.allocator.free(self.base_url);
    }

    /// Build the Anthropic Messages API request body JSON.
    pub fn buildRequestBody(self: *const AnthropicProvider, req: Request) ![]u8 {
        var buf = std.ArrayList(u8).init(self.allocator);
        errdefer buf.deinit();
        try serializeRequest(buf.writer(), req, self.enable_prompt_caching);
        return buf.toOwnedSlice();
    }

    /// HTTP stub — Phase 5 will implement this.
    pub fn sendRequest(
        self: *AnthropicProvider,
        allocator: std.mem.Allocator,
        body: []const u8,
    ) ![]u8 {
        _ = self;
        _ = allocator;
        _ = body;
        return error.NotImplemented;
    }

    /// Parse a non-streaming JSON response.
    pub fn parseResponse(self: *const AnthropicProvider, json_body: []const u8) !Response {
        return deserializeResponse(self.allocator, json_body);
    }
};

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

pub fn serializeRequest(writer: anytype, req: Request, enable_caching: bool) !void {
    _ = enable_caching; // future: use to emit cache_control blocks
    var jw = std.json.writeStream(writer, .{});

    try jw.beginObject();

    try jw.objectField("model");
    try jw.write(req.model);

    try jw.objectField("max_tokens");
    try jw.write(req.max_tokens);

    if (req.temperature) |t| {
        try jw.objectField("temperature");
        try jw.write(t);
    }

    if (req.stream) {
        try jw.objectField("stream");
        try jw.write(true);
    }

    // System prompt (top-level, not a message)
    if (req.system_prompt) |sp| {
        try jw.objectField("system");
        try jw.beginArray();
        try jw.beginObject();
        try jw.objectField("type");
        try jw.write("text");
        try jw.objectField("text");
        try jw.write(sp);
        try jw.endObject();
        try jw.endArray();
    }

    // Tools
    if (req.tools.len > 0) {
        try jw.objectField("tools");
        try jw.beginArray();
        for (req.tools) |tool| {
            try serializeToolDefinition(&jw, tool);
        }
        try jw.endArray();

        try jw.objectField("tool_choice");
        try jw.beginObject();
        try jw.objectField("type");
        try jw.write("auto");
        try jw.endObject();
    }

    // Messages
    try jw.objectField("messages");
    try jw.beginArray();
    for (req.messages) |msg| {
        try serializeMessage(&jw, msg);
    }
    try jw.endArray();

    try jw.endObject();
}

fn serializeMessage(jw: anytype, msg: Message) !void {
    try jw.beginObject();
    try jw.objectField("role");
    try jw.write(msg.role.toString());
    try jw.objectField("content");
    try jw.beginArray();
    for (msg.content) |blk| {
        try serializeContentBlock(jw, blk);
    }
    try jw.endArray();
    try jw.endObject();
}

fn serializeContentBlock(jw: anytype, blk: ContentBlock) !void {
    switch (blk) {
        .text => |t| {
            try jw.beginObject();
            try jw.objectField("type");
            try jw.write("text");
            try jw.objectField("text");
            try jw.write(t.text);
            if (t.cacheable) {
                try jw.objectField("cache_control");
                try jw.beginObject();
                try jw.objectField("type");
                try jw.write("ephemeral");
                try jw.endObject();
            }
            try jw.endObject();
        },
        .image => |img| {
            try jw.beginObject();
            try jw.objectField("type");
            try jw.write("image");
            try jw.objectField("source");
            try jw.beginObject();
            try jw.objectField("type");
            try jw.write(img.source_type);
            try jw.objectField("media_type");
            try jw.write(img.media_type);
            try jw.objectField("data");
            try jw.write(img.data);
            try jw.endObject();
            try jw.endObject();
        },
        .tool_use => |tu| {
            try jw.beginObject();
            try jw.objectField("type");
            try jw.write("tool_use");
            try jw.objectField("id");
            try jw.write(tu.id);
            try jw.objectField("name");
            try jw.write(tu.name);
            try jw.objectField("input");
            try jw.print("{s}", .{tu.input_json});
            try jw.endObject();
        },
        .tool_result => |tr| {
            try jw.beginObject();
            try jw.objectField("type");
            try jw.write("tool_result");
            try jw.objectField("tool_use_id");
            try jw.write(tr.tool_use_id);
            if (tr.is_error) {
                try jw.objectField("is_error");
                try jw.write(true);
            }
            try jw.objectField("content");
            try jw.beginArray();
            try jw.beginObject();
            try jw.objectField("type");
            try jw.write("text");
            try jw.objectField("text");
            try jw.write(tr.content);
            try jw.endObject();
            try jw.endArray();
            try jw.endObject();
        },
    }
}

fn serializeToolDefinition(jw: anytype, tool: ToolDefinition) !void {
    try jw.beginObject();
    try jw.objectField("name");
    try jw.write(tool.name);
    try jw.objectField("description");
    try jw.write(tool.description);
    try jw.objectField("input_schema");
    try jw.print("{s}", .{tool.input_schema_json});
    if (tool.cacheable) {
        try jw.objectField("cache_control");
        try jw.beginObject();
        try jw.objectField("type");
        try jw.write("ephemeral");
        try jw.endObject();
    }
    try jw.endObject();
}

// ---------------------------------------------------------------------------
// Response deserialization
// ---------------------------------------------------------------------------

fn deserializeResponse(allocator: std.mem.Allocator, json_body: []const u8) !Response {
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_body, .{});
    defer parsed.deinit();

    const root = parsed.value;
    if (root != .object) return error.InvalidResponseFormat;

    const id = blk: {
        const v = root.object.get("id") orelse break :blk "";
        break :blk if (v == .string) v.string else "";
    };
    const model = blk: {
        const v = root.object.get("model") orelse break :blk "";
        break :blk if (v == .string) v.string else "";
    };
    const stop_reason = blk: {
        const v = root.object.get("stop_reason") orelse break :blk "end_turn";
        break :blk if (v == .string) v.string else "end_turn";
    };

    var text_buf = std.ArrayList(u8).init(allocator);
    errdefer text_buf.deinit();
    var tool_uses = std.ArrayList(Response.ToolUse).init(allocator);
    errdefer tool_uses.deinit();
    var usage_val: ?Response.Usage = null;

    if (root.object.get("content")) |content_v| {
        if (content_v == .array) {
            for (content_v.array.items) |blk| {
                if (blk != .object) continue;
                const type_v = blk.object.get("type") orelse continue;
                if (type_v != .string) continue;

                if (std.mem.eql(u8, type_v.string, "text")) {
                    if (blk.object.get("text")) |tv| {
                        if (tv == .string) try text_buf.appendSlice(tv.string);
                    }
                } else if (std.mem.eql(u8, type_v.string, "tool_use")) {
                    const tu_id = blk: {
                        const v = blk.object.get("id") orelse break :blk "";
                        break :blk if (v == .string) v.string else "";
                    };
                    const tu_name = blk: {
                        const v = blk.object.get("name") orelse break :blk "";
                        break :blk if (v == .string) v.string else "";
                    };
                    var input_buf = std.ArrayList(u8).init(allocator);
                    errdefer input_buf.deinit();
                    if (blk.object.get("input")) |iv| {
                        try std.json.stringify(iv, .{}, input_buf.writer());
                    } else {
                        try input_buf.appendSlice("{}");
                    }
                    try tool_uses.append(Response.ToolUse{
                        .id = try allocator.dupe(u8, tu_id),
                        .name = try allocator.dupe(u8, tu_name),
                        .input_json = try input_buf.toOwnedSlice(),
                    });
                }
            }
        }
    }

    if (root.object.get("usage")) |usage_v| {
        if (usage_v == .object) {
            const inp = getU32(usage_v, "input_tokens");
            const outp = getU32(usage_v, "output_tokens");
            const cache_create = getU32(usage_v, "cache_creation_input_tokens");
            const cache_read = getU32(usage_v, "cache_read_input_tokens");
            usage_val = Response.Usage{
                .input_tokens = inp,
                .output_tokens = outp,
                .cache_creation_input_tokens = cache_create,
                .cache_read_input_tokens = cache_read,
            };
        }
    }

    return Response{
        .allocator = allocator,
        .id = try allocator.dupe(u8, id),
        .model = try allocator.dupe(u8, model),
        .stop_reason = try allocator.dupe(u8, stop_reason),
        .content = try text_buf.toOwnedSlice(),
        .tool_uses = try tool_uses.toOwnedSlice(),
        .usage = usage_val,
    };
}

fn getU32(obj: std.json.Value, key: []const u8) u32 {
    const v = obj.object.get(key) orelse return 0;
    return switch (v) {
        .integer => @intCast(v.integer),
        .float => @intFromFloat(v.float),
        else => 0,
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "serializeRequest basic" {
    const allocator = std.testing.allocator;
    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    const req = Request{
        .model = "claude-3-5-sonnet-20240620",
        .messages = &.{
            Message{
                .role = .user,
                .content = &.{ContentBlock{ .text = .{ .text = "Hi" } }},
            },
        },
        .system_prompt = "Be helpful.",
    };
    try serializeRequest(buf.writer(), req, false);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, buf.items, .{});
    defer parsed.deinit();

    try std.testing.expect(parsed.value == .object);
    const model_v = parsed.value.object.get("model") orelse return error.TestMissingField;
    try std.testing.expectEqualStrings("claude-3-5-sonnet-20240620", model_v.string);

    // system should be an array
    const sys_v = parsed.value.object.get("system") orelse return error.TestMissingField;
    try std.testing.expect(sys_v == .array);
    try std.testing.expectEqual(@as(usize, 1), sys_v.array.items.len);
}

test "serializeRequest with tool definition" {
    const allocator = std.testing.allocator;
    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    const req = Request{
        .model = "claude-opus-4-5",
        .messages = &.{},
        .tools = &.{
            ToolDefinition{
                .name = "Read",
                .description = "Read a file",
                .input_schema_json = "{\"type\":\"object\",\"properties\":{\"file_path\":{\"type\":\"string\"}}}",
            },
        },
    };
    try serializeRequest(buf.writer(), req, false);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, buf.items, .{});
    defer parsed.deinit();

    const tools_v = parsed.value.object.get("tools") orelse return error.TestMissingField;
    try std.testing.expect(tools_v == .array);
    try std.testing.expectEqual(@as(usize, 1), tools_v.array.items.len);
    const tool = tools_v.array.items[0];
    try std.testing.expectEqualStrings("Read", tool.object.get("name").?.string);
}

test "deserializeResponse text" {
    const json_body =
        \\{
        \\  "id": "msg_01abc",
        \\  "model": "claude-3-5-sonnet-20240620",
        \\  "stop_reason": "end_turn",
        \\  "content": [{"type":"text","text":"Hello!"}],
        \\  "usage": {"input_tokens": 8, "output_tokens": 3}
        \\}
    ;
    var resp = try deserializeResponse(std.testing.allocator, json_body);
    defer resp.deinit();

    try std.testing.expectEqualStrings("msg_01abc", resp.id);
    try std.testing.expectEqualStrings("Hello!", resp.content);
    try std.testing.expectEqual(@as(usize, 0), resp.tool_uses.len);
    const usage = resp.usage orelse return error.TestUnexpectedNull;
    try std.testing.expectEqual(@as(u32, 8), usage.input_tokens);
}

test "deserializeResponse tool_use" {
    const json_body =
        \\{
        \\  "id": "msg_02xyz",
        \\  "model": "claude-3-opus-20240229",
        \\  "stop_reason": "tool_use",
        \\  "content": [{
        \\    "type": "tool_use",
        \\    "id": "toolu_01",
        \\    "name": "Bash",
        \\    "input": {"command": "ls"}
        \\  }],
        \\  "usage": {"input_tokens": 20, "output_tokens": 15}
        \\}
    ;
    var resp = try deserializeResponse(std.testing.allocator, json_body);
    defer resp.deinit();

    try std.testing.expectEqual(@as(usize, 1), resp.tool_uses.len);
    try std.testing.expectEqualStrings("toolu_01", resp.tool_uses[0].id);
    try std.testing.expectEqualStrings("Bash", resp.tool_uses[0].name);
}

test "AnthropicProvider sendRequest returns NotImplemented" {
    var p = try AnthropicProvider.init(std.testing.allocator, "sk-ant-test", "", true);
    defer p.deinit();
    try std.testing.expectError(error.NotImplemented, p.sendRequest(std.testing.allocator, "{}"));
}
