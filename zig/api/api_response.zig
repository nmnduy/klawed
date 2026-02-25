//! api/api_response.zig — Unified API response types and parsing
//!
//! Provides a provider-agnostic `ApiResponse` that all providers return after
//! a successful (or failed) API call, plus provider-specific parsing helpers
//! that convert raw JSON bodies into this common structure.
//!
//! ## Design
//! Rather than mirroring the C `ApiResponse` struct verbatim (which has a
//! `cJSON*` inside it), we define a clean Zig type with:
//!  - `content: []const u8`  — concatenated assistant text
//!  - `tool_calls: []ToolCall` — structured tool invocations
//!  - `usage: ?Usage`        — token usage if available
//!  - `stop_reason: []const u8`
//!
//! ## Provider response formats
//! - **OpenAI / DeepSeek / Moonshot / Kimi**: `choices[0].message`
//! - **Anthropic**: top-level `content` array
//! - **Bedrock Converse**: `output.message.content` array

const std = @import("std");

// ---------------------------------------------------------------------------
// Shared types
// ---------------------------------------------------------------------------

pub const ToolCall = struct {
    id: []const u8,
    name: []const u8,
    /// JSON-encoded arguments / input object.
    arguments: []const u8,
};

pub const Usage = struct {
    /// Prompt / input tokens (includes cached tokens for Anthropic).
    prompt_tokens: u32 = 0,
    /// Completion / output tokens.
    completion_tokens: u32 = 0,
    /// Cache creation tokens (Anthropic-specific, zero for other providers).
    cache_creation_tokens: u32 = 0,
    /// Cache read tokens (Anthropic-specific, zero for other providers).
    cache_read_tokens: u32 = 0,
};

/// Provider-agnostic result of a single (non-streaming) API call.
///
/// All slice fields are owned by the struct.  Call `deinit(allocator)` to free.
pub const ApiResponse = struct {
    allocator: std.mem.Allocator,
    /// Assistant text reply (may be empty if tool_calls is non-empty).
    content: []const u8,
    /// Optional reasoning/thinking text (DeepSeek, Moonshot, Kimi).
    reasoning_content: ?[]const u8,
    /// Structured tool invocations requested by the model.
    tool_calls: []const ToolCall,
    /// Why the model stopped generating.
    stop_reason: []const u8,
    /// Token usage statistics.
    usage: ?Usage,
    /// True if the response represents an error (non-2xx or JSON error object).
    is_error: bool = false,
    /// Human-readable error message when `is_error` is true.
    error_message: ?[]const u8 = null,

    pub fn deinit(self: *ApiResponse) void {
        const a = self.allocator;
        a.free(self.content);
        if (self.reasoning_content) |rc| a.free(rc);
        for (self.tool_calls) |tc| {
            a.free(tc.id);
            a.free(tc.name);
            a.free(tc.arguments);
        }
        a.free(self.tool_calls);
        a.free(self.stop_reason);
        if (self.error_message) |em| a.free(em);
    }
};

// ---------------------------------------------------------------------------
// Provider format — determines which JSON schema to use
// ---------------------------------------------------------------------------

pub const ProviderFormat = enum {
    /// OpenAI Chat Completions (also DeepSeek, Moonshot, Kimi, etc.)
    openai,
    /// Anthropic Messages API
    anthropic,
    /// AWS Bedrock Converse API
    bedrock,
};

// ---------------------------------------------------------------------------
// Public parse entry point
// ---------------------------------------------------------------------------

/// Parse a raw JSON response body according to `format` and return an
/// `ApiResponse`.  Caller owns the result and must call `deinit()`.
pub fn parseResponse(
    allocator: std.mem.Allocator,
    json_body: []const u8,
    format: ProviderFormat,
) !ApiResponse {
    return switch (format) {
        .openai => parseOpenAI(allocator, json_body),
        .anthropic => parseAnthropic(allocator, json_body),
        .bedrock => parseBedrock(allocator, json_body),
    };
}

// ---------------------------------------------------------------------------
// OpenAI format parser
// ---------------------------------------------------------------------------

fn parseOpenAI(allocator: std.mem.Allocator, json_body: []const u8) !ApiResponse {
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_body, .{});
    defer parsed.deinit();

    const root = parsed.value;
    if (root != .object) return makeErrorResponse(allocator, "Invalid JSON: expected object");

    // Check for API-level error object
    if (root.object.get("error")) |err_v| {
        if (err_v == .object) {
            const msg = getStr(err_v, "message") orelse "API error";
            return makeErrorResponse(allocator, msg);
        }
    }

    var content = std.ArrayList(u8).init(allocator);
    errdefer content.deinit();
    var reasoning_buf = std.ArrayList(u8).init(allocator);
    errdefer reasoning_buf.deinit();
    var tool_calls_list = std.ArrayList(ToolCall).init(allocator);
    errdefer {
        for (tool_calls_list.items) |tc| {
            allocator.free(tc.id);
            allocator.free(tc.name);
            allocator.free(tc.arguments);
        }
        tool_calls_list.deinit();
    }
    var stop_reason: []const u8 = "stop";
    var usage_val: ?Usage = null;

    if (root.object.get("choices")) |choices_v| {
        if (choices_v == .array and choices_v.array.items.len > 0) {
            const choice = choices_v.array.items[0];
            if (choice == .object) {
                if (choice.object.get("finish_reason")) |fr| {
                    if (fr == .string) stop_reason = fr.string;
                }
                if (choice.object.get("message")) |msg| {
                    if (msg == .object) {
                        if (msg.object.get("content")) |cv| {
                            if (cv == .string) try content.appendSlice(cv.string);
                        }
                        if (msg.object.get("reasoning_content")) |rc| {
                            if (rc == .string and rc.string.len > 0) {
                                try reasoning_buf.appendSlice(rc.string);
                            }
                        }
                        if (msg.object.get("tool_calls")) |tc_arr| {
                            if (tc_arr == .array) {
                                for (tc_arr.array.items) |tc| {
                                    if (tc != .object) continue;
                                    const id = getStr(tc, "id") orelse "";
                                    var name: []const u8 = "";
                                    var args: []const u8 = "{}";
                                    if (tc.object.get("function")) |fn_obj| {
                                        if (fn_obj == .object) {
                                            name = getStr(fn_obj, "name") orelse "";
                                            args = getStr(fn_obj, "arguments") orelse "{}";
                                        }
                                    }
                                    try tool_calls_list.append(ToolCall{
                                        .id = try allocator.dupe(u8, id),
                                        .name = try allocator.dupe(u8, name),
                                        .arguments = try allocator.dupe(u8, args),
                                    });
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (root.object.get("usage")) |uv| {
        if (uv == .object) {
            usage_val = Usage{
                .prompt_tokens = getU32(uv, "prompt_tokens"),
                .completion_tokens = getU32(uv, "completion_tokens"),
            };
        }
    }

    return ApiResponse{
        .allocator = allocator,
        .content = try content.toOwnedSlice(),
        .reasoning_content = if (reasoning_buf.items.len > 0)
            try reasoning_buf.toOwnedSlice()
        else blk: {
            reasoning_buf.deinit();
            break :blk null;
        },
        .tool_calls = try tool_calls_list.toOwnedSlice(),
        .stop_reason = try allocator.dupe(u8, stop_reason),
        .usage = usage_val,
    };
}

// ---------------------------------------------------------------------------
// Anthropic format parser
// ---------------------------------------------------------------------------

fn parseAnthropic(allocator: std.mem.Allocator, json_body: []const u8) !ApiResponse {
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_body, .{});
    defer parsed.deinit();

    const root = parsed.value;
    if (root != .object) return makeErrorResponse(allocator, "Invalid JSON: expected object");

    if (root.object.get("error")) |err_v| {
        if (err_v == .object) {
            const msg = getStr(err_v, "message") orelse "API error";
            return makeErrorResponse(allocator, msg);
        }
    }

    const stop_reason = getStr(root, "stop_reason") orelse "end_turn";

    var text_buf = std.ArrayList(u8).init(allocator);
    errdefer text_buf.deinit();
    var tool_calls_list = std.ArrayList(ToolCall).init(allocator);
    errdefer {
        for (tool_calls_list.items) |tc| {
            allocator.free(tc.id);
            allocator.free(tc.name);
            allocator.free(tc.arguments);
        }
        tool_calls_list.deinit();
    }
    var usage_val: ?Usage = null;

    if (root.object.get("content")) |content_v| {
        if (content_v == .array) {
            for (content_v.array.items) |blk| {
                if (blk != .object) continue;
                const blk_type = getStr(blk, "type") orelse continue;
                if (std.mem.eql(u8, blk_type, "text")) {
                    if (blk.object.get("text")) |tv| {
                        if (tv == .string) try text_buf.appendSlice(tv.string);
                    }
                } else if (std.mem.eql(u8, blk_type, "tool_use")) {
                    const id = getStr(blk, "id") orelse "";
                    const name = getStr(blk, "name") orelse "";
                    var input_buf = std.ArrayList(u8).init(allocator);
                    errdefer input_buf.deinit();
                    if (blk.object.get("input")) |iv| {
                        try std.json.stringify(iv, .{}, input_buf.writer());
                    } else {
                        try input_buf.appendSlice("{}");
                    }
                    try tool_calls_list.append(ToolCall{
                        .id = try allocator.dupe(u8, id),
                        .name = try allocator.dupe(u8, name),
                        .arguments = try input_buf.toOwnedSlice(),
                    });
                }
            }
        }
    }

    if (root.object.get("usage")) |uv| {
        if (uv == .object) {
            usage_val = Usage{
                .prompt_tokens = getU32(uv, "input_tokens"),
                .completion_tokens = getU32(uv, "output_tokens"),
                .cache_creation_tokens = getU32(uv, "cache_creation_input_tokens"),
                .cache_read_tokens = getU32(uv, "cache_read_input_tokens"),
            };
        }
    }

    return ApiResponse{
        .allocator = allocator,
        .content = try text_buf.toOwnedSlice(),
        .reasoning_content = null,
        .tool_calls = try tool_calls_list.toOwnedSlice(),
        .stop_reason = try allocator.dupe(u8, stop_reason),
        .usage = usage_val,
    };
}

// ---------------------------------------------------------------------------
// Bedrock Converse format parser
// ---------------------------------------------------------------------------

fn parseBedrock(allocator: std.mem.Allocator, json_body: []const u8) !ApiResponse {
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_body, .{});
    defer parsed.deinit();

    const root = parsed.value;
    if (root != .object) return makeErrorResponse(allocator, "Invalid JSON: expected object");

    // Bedrock wraps the response differently — check for error first
    if (root.object.get("message") != null and root.object.get("__type") != null) {
        const msg = getStr(root, "message") orelse "Bedrock API error";
        return makeErrorResponse(allocator, msg);
    }

    const stop_reason = getStr(root, "stopReason") orelse "end_turn";

    var text_buf = std.ArrayList(u8).init(allocator);
    errdefer text_buf.deinit();
    var tool_calls_list = std.ArrayList(ToolCall).init(allocator);
    errdefer {
        for (tool_calls_list.items) |tc| {
            allocator.free(tc.id);
            allocator.free(tc.name);
            allocator.free(tc.arguments);
        }
        tool_calls_list.deinit();
    }
    var usage_val: ?Usage = null;

    // Bedrock Converse: output.message.content[]
    if (root.object.get("output")) |output_v| {
        if (output_v == .object) {
            if (output_v.object.get("message")) |msg_v| {
                if (msg_v == .object) {
                    if (msg_v.object.get("content")) |content_v| {
                        if (content_v == .array) {
                            for (content_v.array.items) |blk| {
                                if (blk != .object) continue;
                                if (blk.object.get("text")) |tv| {
                                    if (tv == .string) try text_buf.appendSlice(tv.string);
                                }
                                if (blk.object.get("toolUse")) |tu_v| {
                                    if (tu_v == .object) {
                                        const id = getStr(tu_v, "toolUseId") orelse "";
                                        const name = getStr(tu_v, "name") orelse "";
                                        var input_buf = std.ArrayList(u8).init(allocator);
                                        errdefer input_buf.deinit();
                                        if (tu_v.object.get("input")) |iv| {
                                            try std.json.stringify(iv, .{}, input_buf.writer());
                                        } else {
                                            try input_buf.appendSlice("{}");
                                        }
                                        try tool_calls_list.append(ToolCall{
                                            .id = try allocator.dupe(u8, id),
                                            .name = try allocator.dupe(u8, name),
                                            .arguments = try input_buf.toOwnedSlice(),
                                        });
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (root.object.get("usage")) |uv| {
        if (uv == .object) {
            usage_val = Usage{
                .prompt_tokens = getU32(uv, "inputTokens"),
                .completion_tokens = getU32(uv, "outputTokens"),
            };
        }
    }

    return ApiResponse{
        .allocator = allocator,
        .content = try text_buf.toOwnedSlice(),
        .reasoning_content = null,
        .tool_calls = try tool_calls_list.toOwnedSlice(),
        .stop_reason = try allocator.dupe(u8, stop_reason),
        .usage = usage_val,
    };
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn getStr(v: std.json.Value, key: []const u8) ?[]const u8 {
    const f = v.object.get(key) orelse return null;
    return if (f == .string) f.string else null;
}

fn getU32(v: std.json.Value, key: []const u8) u32 {
    const f = v.object.get(key) orelse return 0;
    return switch (f) {
        .integer => @intCast(f.integer),
        .float => @intFromFloat(f.float),
        else => 0,
    };
}

fn makeErrorResponse(allocator: std.mem.Allocator, msg: []const u8) !ApiResponse {
    return ApiResponse{
        .allocator = allocator,
        .content = try allocator.dupe(u8, ""),
        .reasoning_content = null,
        .tool_calls = try allocator.dupe(ToolCall, &.{}),
        .stop_reason = try allocator.dupe(u8, "error"),
        .usage = null,
        .is_error = true,
        .error_message = try allocator.dupe(u8, msg),
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "parseResponse openai — basic text" {
    const json =
        \\{
        \\  "id": "chatcmpl-1",
        \\  "model": "gpt-4o",
        \\  "choices": [{
        \\    "finish_reason": "stop",
        \\    "message": {"role": "assistant", "content": "Hello!"}
        \\  }],
        \\  "usage": {"prompt_tokens": 10, "completion_tokens": 5}
        \\}
    ;
    var resp = try parseResponse(std.testing.allocator, json, .openai);
    defer resp.deinit();

    try std.testing.expectEqualStrings("Hello!", resp.content);
    try std.testing.expectEqualStrings("stop", resp.stop_reason);
    try std.testing.expectEqual(@as(usize, 0), resp.tool_calls.len);
    try std.testing.expect(!resp.is_error);
    const u = resp.usage orelse return error.TestUnexpectedNull;
    try std.testing.expectEqual(@as(u32, 10), u.prompt_tokens);
    try std.testing.expectEqual(@as(u32, 5), u.completion_tokens);
}

test "parseResponse openai — tool calls" {
    const json =
        \\{
        \\  "id": "chatcmpl-2",
        \\  "model": "gpt-4o",
        \\  "choices": [{
        \\    "finish_reason": "tool_calls",
        \\    "message": {
        \\      "role": "assistant",
        \\      "content": null,
        \\      "tool_calls": [{
        \\        "id": "call_abc",
        \\        "type": "function",
        \\        "function": {"name": "Bash", "arguments": "{\"command\":\"ls\"}"}
        \\      }]
        \\    }
        \\  }]
        \\}
    ;
    var resp = try parseResponse(std.testing.allocator, json, .openai);
    defer resp.deinit();

    try std.testing.expectEqual(@as(usize, 1), resp.tool_calls.len);
    try std.testing.expectEqualStrings("call_abc", resp.tool_calls[0].id);
    try std.testing.expectEqualStrings("Bash", resp.tool_calls[0].name);
    try std.testing.expectEqualStrings("{\"command\":\"ls\"}", resp.tool_calls[0].arguments);
}

test "parseResponse openai — error object" {
    const json =
        \\{"error": {"type": "invalid_request_error", "message": "Bad request"}}
    ;
    var resp = try parseResponse(std.testing.allocator, json, .openai);
    defer resp.deinit();

    try std.testing.expect(resp.is_error);
    try std.testing.expectEqualStrings("Bad request", resp.error_message.?);
}

test "parseResponse anthropic — text" {
    const json =
        \\{
        \\  "id": "msg_01",
        \\  "model": "claude-3-5-sonnet-20240620",
        \\  "stop_reason": "end_turn",
        \\  "content": [{"type": "text", "text": "Sure!"}],
        \\  "usage": {"input_tokens": 20, "output_tokens": 8}
        \\}
    ;
    var resp = try parseResponse(std.testing.allocator, json, .anthropic);
    defer resp.deinit();

    try std.testing.expectEqualStrings("Sure!", resp.content);
    try std.testing.expectEqualStrings("end_turn", resp.stop_reason);
    try std.testing.expect(!resp.is_error);
    const u = resp.usage orelse return error.TestUnexpectedNull;
    try std.testing.expectEqual(@as(u32, 20), u.prompt_tokens);
    try std.testing.expectEqual(@as(u32, 8), u.completion_tokens);
}

test "parseResponse anthropic — tool_use" {
    const json =
        \\{
        \\  "id": "msg_02",
        \\  "model": "claude-3-opus",
        \\  "stop_reason": "tool_use",
        \\  "content": [{
        \\    "type": "tool_use",
        \\    "id": "toolu_99",
        \\    "name": "Read",
        \\    "input": {"file_path": "/tmp/foo.txt"}
        \\  }],
        \\  "usage": {"input_tokens": 30, "output_tokens": 12}
        \\}
    ;
    var resp = try parseResponse(std.testing.allocator, json, .anthropic);
    defer resp.deinit();

    try std.testing.expectEqual(@as(usize, 1), resp.tool_calls.len);
    try std.testing.expectEqualStrings("toolu_99", resp.tool_calls[0].id);
    try std.testing.expectEqualStrings("Read", resp.tool_calls[0].name);
}

test "parseResponse anthropic — cache usage fields" {
    const json =
        \\{
        \\  "id": "msg_03",
        \\  "model": "claude-3-5-sonnet",
        \\  "stop_reason": "end_turn",
        \\  "content": [{"type": "text", "text": "cached!"}],
        \\  "usage": {
        \\    "input_tokens": 5,
        \\    "output_tokens": 3,
        \\    "cache_creation_input_tokens": 100,
        \\    "cache_read_input_tokens": 50
        \\  }
        \\}
    ;
    var resp = try parseResponse(std.testing.allocator, json, .anthropic);
    defer resp.deinit();

    const u = resp.usage orelse return error.TestUnexpectedNull;
    try std.testing.expectEqual(@as(u32, 100), u.cache_creation_tokens);
    try std.testing.expectEqual(@as(u32, 50), u.cache_read_tokens);
}

test "parseResponse bedrock — basic text" {
    const json =
        \\{
        \\  "stopReason": "end_turn",
        \\  "output": {
        \\    "message": {
        \\      "role": "assistant",
        \\      "content": [{"text": "Bedrock reply"}]
        \\    }
        \\  },
        \\  "usage": {"inputTokens": 15, "outputTokens": 7}
        \\}
    ;
    var resp = try parseResponse(std.testing.allocator, json, .bedrock);
    defer resp.deinit();

    try std.testing.expectEqualStrings("Bedrock reply", resp.content);
    try std.testing.expectEqualStrings("end_turn", resp.stop_reason);
    const u = resp.usage orelse return error.TestUnexpectedNull;
    try std.testing.expectEqual(@as(u32, 15), u.prompt_tokens);
    try std.testing.expectEqual(@as(u32, 7), u.completion_tokens);
}

test "parseResponse bedrock — tool_use" {
    const json =
        \\{
        \\  "stopReason": "tool_use",
        \\  "output": {
        \\    "message": {
        \\      "role": "assistant",
        \\      "content": [{
        \\        "toolUse": {
        \\          "toolUseId": "bedrock-tool-1",
        \\          "name": "Bash",
        \\          "input": {"command": "pwd"}
        \\        }
        \\      }]
        \\    }
        \\  },
        \\  "usage": {"inputTokens": 20, "outputTokens": 10}
        \\}
    ;
    var resp = try parseResponse(std.testing.allocator, json, .bedrock);
    defer resp.deinit();

    try std.testing.expectEqual(@as(usize, 1), resp.tool_calls.len);
    try std.testing.expectEqualStrings("bedrock-tool-1", resp.tool_calls[0].id);
    try std.testing.expectEqualStrings("Bash", resp.tool_calls[0].name);
}

test "ApiResponse deinit with tool calls" {
    // Build a response manually and verify deinit doesn't leak
    const a = std.testing.allocator;
    var tcs = try a.alloc(ToolCall, 1);
    tcs[0] = ToolCall{
        .id = try a.dupe(u8, "tc-1"),
        .name = try a.dupe(u8, "Bash"),
        .arguments = try a.dupe(u8, "{}"),
    };
    var resp = ApiResponse{
        .allocator = a,
        .content = try a.dupe(u8, "hello"),
        .reasoning_content = null,
        .tool_calls = tcs,
        .stop_reason = try a.dupe(u8, "tool_calls"),
        .usage = null,
    };
    resp.deinit();
}
