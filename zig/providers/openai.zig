//! providers/openai.zig — OpenAI-compatible API provider
//!
//! Zig port of src/openai_provider.c, src/openai_messages.c,
//! src/openai_responses.c.
//!
//! ## Phase 5 note
//! HTTP calls are **not** implemented here; `sendRequest` returns
//! `error.NotImplemented`. The HTTP layer will be wired in Phase 5.
//!
//! ## What this module provides
//! - `OpenAIProvider` struct and constructor
//! - Message serialization to OpenAI JSON format
//! - Tool-call request body building
//! - Response/delta deserialization (non-streaming and SSE streaming)
//! - Prompt-caching header helpers

const std = @import("std");

// ---------------------------------------------------------------------------
// Types shared across providers
// ---------------------------------------------------------------------------

/// How the provider handles extended reasoning / thinking content.
pub const ReasoningMode = enum {
    /// Discard reasoning_content from responses (DeepSeek behaviour).
    discard,
    /// Preserve reasoning_content and re-send it in subsequent turns (Moonshot).
    preserve,
    /// Do not handle reasoning content at all (standard OpenAI).
    none,
};

/// A single content block in a message.
pub const ContentBlock = union(enum) {
    text: []const u8,
    image_url: ImageUrl,
    tool_use: ToolUseBlock,
    tool_result: ToolResultBlock,

    pub const ImageUrl = struct {
        url: []const u8,
        detail: []const u8 = "auto",
    };

    pub const ToolUseBlock = struct {
        id: []const u8,
        name: []const u8,
        /// JSON-encoded arguments string.
        arguments: []const u8,
    };

    pub const ToolResultBlock = struct {
        tool_call_id: []const u8,
        content: []const u8,
    };
};

/// Role of a conversation turn.
pub const Role = enum {
    system,
    user,
    assistant,
    tool,

    pub fn toString(self: Role) []const u8 {
        return switch (self) {
            .system => "system",
            .user => "user",
            .assistant => "assistant",
            .tool => "tool",
        };
    }
};

/// A single message in the conversation.
pub const Message = struct {
    role: Role,
    content: []const ContentBlock,
    /// Optional tool_call_id (for tool result messages).
    tool_call_id: ?[]const u8 = null,
    /// Optional name (for tool messages).
    name: ?[]const u8 = null,
};

/// A tool definition.
pub const ToolDefinition = struct {
    name: []const u8,
    description: []const u8,
    /// JSON Schema for the tool's parameters (already-serialized JSON string).
    parameters_json: []const u8,
};

/// Top-level request to build.
pub const Request = struct {
    model: []const u8,
    messages: []const Message,
    tools: []const ToolDefinition = &.{},
    max_tokens: u32 = 16384,
    temperature: ?f64 = null,
    stream: bool = false,
    system_prompt: ?[]const u8 = null,
};

/// Non-streaming API response.
pub const Response = struct {
    allocator: std.mem.Allocator,
    id: []const u8,
    model: []const u8,
    /// The assistant's text reply (empty if tool calls were made).
    content: []const u8,
    /// Reasoning/thinking content (if model supports it and mode != .discard).
    reasoning_content: ?[]const u8,
    finish_reason: []const u8,
    tool_calls: []const ToolCall,
    usage: ?Usage,

    pub const ToolCall = struct {
        id: []const u8,
        name: []const u8,
        /// JSON-encoded arguments.
        arguments: []const u8,
    };

    pub const Usage = struct {
        prompt_tokens: u32,
        completion_tokens: u32,
        total_tokens: u32,
    };

    pub fn deinit(self: *Response) void {
        const a = self.allocator;
        a.free(self.id);
        a.free(self.model);
        a.free(self.content);
        if (self.reasoning_content) |rc| a.free(rc);
        a.free(self.finish_reason);
        for (self.tool_calls) |tc| {
            a.free(tc.id);
            a.free(tc.name);
            a.free(tc.arguments);
        }
        a.free(self.tool_calls);
        // usage is value type, no free needed
        _ = self.usage;
    }
};

// ---------------------------------------------------------------------------
// OpenAIProvider
// ---------------------------------------------------------------------------

pub const OpenAIProvider = struct {
    allocator: std.mem.Allocator,
    api_key: []const u8,
    base_url: []const u8,
    reasoning_mode: ReasoningMode,

    pub fn init(
        allocator: std.mem.Allocator,
        api_key: []const u8,
        base_url: []const u8,
        reasoning_mode: ReasoningMode,
    ) !OpenAIProvider {
        return OpenAIProvider{
            .allocator = allocator,
            .api_key = try allocator.dupe(u8, api_key),
            .base_url = try allocator.dupe(u8, base_url),
            .reasoning_mode = reasoning_mode,
        };
    }

    pub fn deinit(self: *OpenAIProvider) void {
        self.allocator.free(self.api_key);
        self.allocator.free(self.base_url);
    }

    // ------------------------------------------------------------------
    // Request building
    // ------------------------------------------------------------------

    /// Serialize `req` to the OpenAI Chat Completions JSON format.
    /// Returns an owned slice the caller must free.
    pub fn buildRequestBody(self: *const OpenAIProvider, req: Request) ![]u8 {
        var buf = std.ArrayList(u8).init(self.allocator);
        errdefer buf.deinit();
        try serializeRequest(buf.writer(), req, self.reasoning_mode);
        return buf.toOwnedSlice();
    }

    // ------------------------------------------------------------------
    // HTTP stub (Phase 5)
    // ------------------------------------------------------------------

    /// Send the serialized `body` to the API.
    /// **Phase 5 will replace this stub with real HTTP logic.**
    pub fn sendRequest(
        self: *OpenAIProvider,
        allocator: std.mem.Allocator,
        body: []const u8,
    ) ![]u8 {
        _ = self;
        _ = allocator;
        _ = body;
        return error.NotImplemented;
    }

    // ------------------------------------------------------------------
    // Response parsing
    // ------------------------------------------------------------------

    /// Parse a non-streaming JSON response body into a `Response`.
    /// Caller must call `response.deinit()` when done.
    pub fn parseResponse(self: *const OpenAIProvider, json_body: []const u8) !Response {
        return deserializeResponse(self.allocator, json_body, self.reasoning_mode);
    }
};

// ---------------------------------------------------------------------------
// Serialization helpers
// ---------------------------------------------------------------------------

/// Write the full OpenAI Chat Completions request JSON to `writer`.
pub fn serializeRequest(writer: anytype, req: Request, reasoning_mode: ReasoningMode) !void {
    _ = reasoning_mode; // future: strip reasoning_content from assistant messages in .discard mode
    var jw = std.json.writeStream(writer, .{});

    try jw.beginObject();

    try jw.objectField("model");
    try jw.write(req.model);

    // max_completion_tokens (new name) / max_tokens (legacy name) — emit both for compat
    try jw.objectField("max_completion_tokens");
    try jw.write(req.max_tokens);

    if (req.temperature) |t| {
        try jw.objectField("temperature");
        try jw.write(t);
    }

    if (req.stream) {
        try jw.objectField("stream");
        try jw.write(true);
    }

    // System prompt goes into messages[0] with role=system if present
    try jw.objectField("messages");
    try jw.beginArray();

    if (req.system_prompt) |sp| {
        try jw.beginObject();
        try jw.objectField("role");
        try jw.write("system");
        try jw.objectField("content");
        try jw.write(sp);
        try jw.endObject();
    }

    for (req.messages) |msg| {
        try serializeMessage(&jw, msg);
    }

    try jw.endArray();

    // Tools
    if (req.tools.len > 0) {
        try jw.objectField("tools");
        try jw.beginArray();
        for (req.tools) |tool| {
            try serializeToolDefinition(&jw, tool);
        }
        try jw.endArray();

        try jw.objectField("tool_choice");
        try jw.write("auto");
    }

    try jw.endObject();
}

fn serializeMessage(jw: anytype, msg: Message) !void {
    try jw.beginObject();
    try jw.objectField("role");
    try jw.write(msg.role.toString());

    if (msg.tool_call_id) |tid| {
        try jw.objectField("tool_call_id");
        try jw.write(tid);
    }

    // Content: if single text block we can use a plain string;
    // otherwise use an array of content objects.
    if (msg.content.len == 1 and msg.content[0] == .text) {
        try jw.objectField("content");
        try jw.write(msg.content[0].text);
    } else if (msg.content.len == 0) {
        // Tool-call messages with no text content
        try jw.objectField("content");
        try jw.write(@as(?[]const u8, null));
    } else {
        // Check if message has tool_use blocks — they go as top-level tool_calls
        var has_tool_use = false;
        for (msg.content) |blk| {
            if (blk == .tool_use) { has_tool_use = true; break; }
        }

        // Text/image blocks → content array
        var text_blocks: usize = 0;
        for (msg.content) |blk| {
            if (blk != .tool_use) text_blocks += 1;
        }
        if (text_blocks > 0) {
            try jw.objectField("content");
            try jw.beginArray();
            for (msg.content) |blk| {
                switch (blk) {
                    .text => |t| {
                        try jw.beginObject();
                        try jw.objectField("type");
                        try jw.write("text");
                        try jw.objectField("text");
                        try jw.write(t);
                        try jw.endObject();
                    },
                    .image_url => |iu| {
                        try jw.beginObject();
                        try jw.objectField("type");
                        try jw.write("image_url");
                        try jw.objectField("image_url");
                        try jw.beginObject();
                        try jw.objectField("url");
                        try jw.write(iu.url);
                        try jw.objectField("detail");
                        try jw.write(iu.detail);
                        try jw.endObject();
                        try jw.endObject();
                    },
                    .tool_result => |tr| {
                        try jw.beginObject();
                        try jw.objectField("type");
                        try jw.write("tool_result");
                        try jw.objectField("tool_call_id");
                        try jw.write(tr.tool_call_id);
                        try jw.objectField("content");
                        try jw.write(tr.content);
                        try jw.endObject();
                    },
                    .tool_use => {}, // handled below
                }
            }
            try jw.endArray();
        } else {
            try jw.objectField("content");
            try jw.write(@as(?[]const u8, null));
        }

        // Tool calls → separate top-level field
        if (has_tool_use) {
            try jw.objectField("tool_calls");
            try jw.beginArray();
            for (msg.content) |blk| {
                if (blk == .tool_use) {
                    const tu = blk.tool_use;
                    try jw.beginObject();
                    try jw.objectField("id");
                    try jw.write(tu.id);
                    try jw.objectField("type");
                    try jw.write("function");
                    try jw.objectField("function");
                    try jw.beginObject();
                    try jw.objectField("name");
                    try jw.write(tu.name);
                    try jw.objectField("arguments");
                    try jw.write(tu.arguments);
                    try jw.endObject();
                    try jw.endObject();
                }
            }
            try jw.endArray();
        }
    }

    try jw.endObject();
}

fn serializeToolDefinition(jw: anytype, tool: ToolDefinition) !void {
    try jw.beginObject();
    try jw.objectField("type");
    try jw.write("function");
    try jw.objectField("function");
    try jw.beginObject();
    try jw.objectField("name");
    try jw.write(tool.name);
    try jw.objectField("description");
    try jw.write(tool.description);
    try jw.objectField("parameters");
    // parameters_json is already valid JSON — write raw
    try jw.print("{s}", .{tool.parameters_json});
    try jw.endObject();
    try jw.endObject();
}

// ---------------------------------------------------------------------------
// Response deserialization
// ---------------------------------------------------------------------------

fn deserializeResponse(
    allocator: std.mem.Allocator,
    json_body: []const u8,
    reasoning_mode: ReasoningMode,
) !Response {
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

    var content = std.ArrayList(u8).init(allocator);
    errdefer content.deinit();
    var reasoning_buf = std.ArrayList(u8).init(allocator);
    errdefer reasoning_buf.deinit();
    var finish_reason_buf: []const u8 = "stop";
    var tool_calls = std.ArrayList(Response.ToolCall).init(allocator);
    errdefer tool_calls.deinit();
    var usage_val: ?Response.Usage = null;

    // Parse choices[0]
    if (root.object.get("choices")) |choices_v| {
        if (choices_v == .array and choices_v.array.items.len > 0) {
            const choice = choices_v.array.items[0];
            if (choice == .object) {
                if (choice.object.get("finish_reason")) |fr| {
                    if (fr == .string) finish_reason_buf = fr.string;
                }
                if (choice.object.get("message")) |msg| {
                    if (msg == .object) {
                        // Text content
                        if (msg.object.get("content")) |cv| {
                            if (cv == .string and cv.string.len > 0) {
                                try content.appendSlice(cv.string);
                            }
                        }
                        // Reasoning content
                        if (reasoning_mode != .discard) {
                            if (msg.object.get("reasoning_content")) |rc| {
                                if (rc == .string and rc.string.len > 0) {
                                    try reasoning_buf.appendSlice(rc.string);
                                }
                            }
                        }
                        // Tool calls
                        if (msg.object.get("tool_calls")) |tc_arr| {
                            if (tc_arr == .array) {
                                for (tc_arr.array.items) |tc| {
                                    if (tc != .object) continue;
                                    const tc_id = blk: {
                                        const v = tc.object.get("id") orelse break :blk "";
                                        break :blk if (v == .string) v.string else "";
                                    };
                                    var fn_name: []const u8 = "";
                                    var fn_args: []const u8 = "{}";
                                    if (tc.object.get("function")) |fn_obj| {
                                        if (fn_obj == .object) {
                                            if (fn_obj.object.get("name")) |nv| {
                                                if (nv == .string) fn_name = nv.string;
                                            }
                                            if (fn_obj.object.get("arguments")) |av| {
                                                if (av == .string) fn_args = av.string;
                                            }
                                        }
                                    }
                                    try tool_calls.append(Response.ToolCall{
                                        .id = try allocator.dupe(u8, tc_id),
                                        .name = try allocator.dupe(u8, fn_name),
                                        .arguments = try allocator.dupe(u8, fn_args),
                                    });
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Parse usage
    if (root.object.get("usage")) |usage_v| {
        if (usage_v == .object) {
            const pt = blk: {
                const v = usage_v.object.get("prompt_tokens") orelse break :blk @as(u32, 0);
                break :blk if (v == .integer) @as(u32, @intCast(v.integer)) else 0;
            };
            const ct = blk: {
                const v = usage_v.object.get("completion_tokens") orelse break :blk @as(u32, 0);
                break :blk if (v == .integer) @as(u32, @intCast(v.integer)) else 0;
            };
            usage_val = Response.Usage{
                .prompt_tokens = pt,
                .completion_tokens = ct,
                .total_tokens = pt + ct,
            };
        }
    }

    return Response{
        .allocator = allocator,
        .id = try allocator.dupe(u8, id),
        .model = try allocator.dupe(u8, model),
        .content = try content.toOwnedSlice(),
        .reasoning_content = if (reasoning_buf.items.len > 0)
            try reasoning_buf.toOwnedSlice()
        else blk: {
            reasoning_buf.deinit();
            break :blk null;
        },
        .finish_reason = try allocator.dupe(u8, finish_reason_buf),
        .tool_calls = try tool_calls.toOwnedSlice(),
        .usage = usage_val,
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "serializeRequest minimal" {
    const allocator = std.testing.allocator;
    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    const req = Request{
        .model = "gpt-4o",
        .messages = &.{
            Message{
                .role = .user,
                .content = &.{ContentBlock{ .text = "Hello!" }},
            },
        },
    };
    try serializeRequest(buf.writer(), req, .none);

    // Verify it parses as valid JSON
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, buf.items, .{});
    defer parsed.deinit();
    try std.testing.expect(parsed.value == .object);

    const model_v = parsed.value.object.get("model") orelse return error.TestMissingField;
    try std.testing.expectEqualStrings("gpt-4o", model_v.string);
}

test "serializeRequest with system prompt" {
    const allocator = std.testing.allocator;
    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    const req = Request{
        .model = "gpt-4o",
        .messages = &.{},
        .system_prompt = "You are helpful.",
    };
    try serializeRequest(buf.writer(), req, .none);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, buf.items, .{});
    defer parsed.deinit();

    const messages = parsed.value.object.get("messages") orelse return error.TestMissingField;
    try std.testing.expect(messages == .array);
    try std.testing.expectEqual(@as(usize, 1), messages.array.items.len);

    const sys_msg = messages.array.items[0];
    try std.testing.expectEqualStrings("system", sys_msg.object.get("role").?.string);
    try std.testing.expectEqualStrings("You are helpful.", sys_msg.object.get("content").?.string);
}

test "serializeRequest with tools" {
    const allocator = std.testing.allocator;
    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    const req = Request{
        .model = "gpt-4o",
        .messages = &.{},
        .tools = &.{
            ToolDefinition{
                .name = "Bash",
                .description = "Run a shell command",
                .parameters_json =
                \\{"type":"object","properties":{"command":{"type":"string"}}}
                ,
            },
        },
    };
    try serializeRequest(buf.writer(), req, .none);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, buf.items, .{});
    defer parsed.deinit();

    const tools = parsed.value.object.get("tools") orelse return error.TestMissingField;
    try std.testing.expect(tools == .array);
    try std.testing.expectEqual(@as(usize, 1), tools.array.items.len);

    const tool = tools.array.items[0];
    try std.testing.expectEqualStrings("function", tool.object.get("type").?.string);
    const fn_obj = tool.object.get("function").?;
    try std.testing.expectEqualStrings("Bash", fn_obj.object.get("name").?.string);
}

test "parseResponse basic text" {
    const json_body =
        \\{
        \\  "id": "chatcmpl-abc123",
        \\  "model": "gpt-4o",
        \\  "choices": [{
        \\    "finish_reason": "stop",
        \\    "message": {
        \\      "role": "assistant",
        \\      "content": "Hello, world!"
        \\    }
        \\  }],
        \\  "usage": {
        \\    "prompt_tokens": 10,
        \\    "completion_tokens": 5
        \\  }
        \\}
    ;
    var resp = try deserializeResponse(std.testing.allocator, json_body, .none);
    defer resp.deinit();

    try std.testing.expectEqualStrings("chatcmpl-abc123", resp.id);
    try std.testing.expectEqualStrings("gpt-4o", resp.model);
    try std.testing.expectEqualStrings("Hello, world!", resp.content);
    try std.testing.expectEqualStrings("stop", resp.finish_reason);
    try std.testing.expectEqual(@as(usize, 0), resp.tool_calls.len);
    const usage = resp.usage orelse return error.TestUnexpectedNull;
    try std.testing.expectEqual(@as(u32, 10), usage.prompt_tokens);
    try std.testing.expectEqual(@as(u32, 5), usage.completion_tokens);
}

test "parseResponse with tool calls" {
    const json_body =
        \\{
        \\  "id": "chatcmpl-xyz",
        \\  "model": "gpt-4o",
        \\  "choices": [{
        \\    "finish_reason": "tool_calls",
        \\    "message": {
        \\      "role": "assistant",
        \\      "content": null,
        \\      "tool_calls": [{
        \\        "id": "call_001",
        \\        "type": "function",
        \\        "function": {
        \\          "name": "Bash",
        \\          "arguments": "{\"command\":\"ls\"}"
        \\        }
        \\      }]
        \\    }
        \\  }]
        \\}
    ;
    var resp = try deserializeResponse(std.testing.allocator, json_body, .none);
    defer resp.deinit();

    try std.testing.expectEqual(@as(usize, 1), resp.tool_calls.len);
    try std.testing.expectEqualStrings("call_001", resp.tool_calls[0].id);
    try std.testing.expectEqualStrings("Bash", resp.tool_calls[0].name);
    try std.testing.expectEqualStrings("{\"command\":\"ls\"}", resp.tool_calls[0].arguments);
}

test "OpenAIProvider sendRequest returns NotImplemented" {
    var p = try OpenAIProvider.init(std.testing.allocator, "sk-test", "https://api.openai.com/v1/chat/completions", .none);
    defer p.deinit();

    try std.testing.expectError(error.NotImplemented, p.sendRequest(std.testing.allocator, "{}"));
}

test "OpenAIProvider buildRequestBody" {
    var p = try OpenAIProvider.init(std.testing.allocator, "sk-test", "https://api.openai.com/v1/chat/completions", .none);
    defer p.deinit();

    const body = try p.buildRequestBody(Request{
        .model = "gpt-4o",
        .messages = &.{},
        .max_tokens = 8192,
    });
    defer std.testing.allocator.free(body);

    try std.testing.expect(std.mem.indexOf(u8, body, "gpt-4o") != null);
    try std.testing.expect(std.mem.indexOf(u8, body, "8192") != null);
}
