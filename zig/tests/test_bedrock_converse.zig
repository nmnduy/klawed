//! tests/test_bedrock_converse.zig — Zig port of tests/test_bedrock_converse.c
//!
//! Tests Bedrock Converse API request serialization:
//! - Normal conversation (user message first) is serialized correctly
//! - Request body is valid JSON
//! - System prompt is extracted into the "system" field
//! - Tool definitions are placed in "toolConfig"
//! - Inference config (maxTokens, temperature) is included
//! - Text, tool_use, and tool_result content blocks serialize correctly

const std = @import("std");
const bedrock = @import("../providers/bedrock.zig");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Parse a Bedrock Converse request body (JSON) and return the first message's
/// role, or null if not present.
fn getFirstMessageRole(alloc: std.mem.Allocator, json: []const u8) !?[]const u8 {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();

    const root = parsed.value;
    if (root != .object) return null;
    const msgs = root.object.get("messages") orelse return null;
    if (msgs != .array or msgs.array.items.len == 0) return null;
    const first = msgs.array.items[0];
    if (first != .object) return null;
    const role = first.object.get("role") orelse return null;
    if (role != .string) return null;
    return try alloc.dupe(u8, role.string);
}

/// Count messages in a serialized Bedrock request.
fn countMessages(alloc: std.mem.Allocator, json: []const u8) !usize {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();
    const root = parsed.value;
    if (root != .object) return 0;
    const msgs = root.object.get("messages") orelse return 0;
    if (msgs != .array) return 0;
    return msgs.array.items.len;
}

// ---------------------------------------------------------------------------
// Tests — request serialization
// ---------------------------------------------------------------------------

test "bedrock converse: minimal request serializes to valid JSON" {
    const alloc = std.testing.allocator;
    var provider = try bedrock.BedrockProvider.init(alloc, "us-east-1", "anthropic.claude-3-sonnet");
    defer provider.deinit();

    const req = bedrock.Request{
        .model_id = "anthropic.claude-3-sonnet",
        .messages = &.{
            .{
                .role = .user,
                .content = &.{.{ .text = "Hello" }},
            },
        },
    };

    const body = try provider.buildRequestBody(req);
    defer alloc.free(body);

    // Must be valid JSON.
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, body, .{});
    defer parsed.deinit();
    try std.testing.expect(parsed.value == .object);
}

test "bedrock converse: user message is serialized correctly" {
    const alloc = std.testing.allocator;
    var provider = try bedrock.BedrockProvider.init(alloc, "us-east-1", "test-model");
    defer provider.deinit();

    const req = bedrock.Request{
        .model_id = "test-model",
        .messages = &.{
            .{
                .role = .user,
                .content = &.{.{ .text = "Hello, world!" }},
            },
        },
    };

    const body = try provider.buildRequestBody(req);
    defer alloc.free(body);

    const role = try getFirstMessageRole(alloc, body);
    defer if (role) |r| alloc.free(r);
    try std.testing.expect(role != null);
    try std.testing.expectEqualStrings("user", role.?);
}

test "bedrock converse: assistant message role is serialized correctly" {
    const alloc = std.testing.allocator;
    var provider = try bedrock.BedrockProvider.init(alloc, "us-east-1", "test-model");
    defer provider.deinit();

    const req = bedrock.Request{
        .model_id = "test-model",
        .messages = &.{
            .{
                .role = .user,
                .content = &.{.{ .text = "Hello" }},
            },
            .{
                .role = .assistant,
                .content = &.{.{ .text = "Hi there!" }},
            },
        },
    };

    const body = try provider.buildRequestBody(req);
    defer alloc.free(body);

    try std.testing.expectEqual(@as(usize, 2), try countMessages(alloc, body));
}

test "bedrock converse: system prompt is extracted into 'system' field" {
    const alloc = std.testing.allocator;
    var provider = try bedrock.BedrockProvider.init(alloc, "us-east-1", "test-model");
    defer provider.deinit();

    const req = bedrock.Request{
        .model_id = "test-model",
        .messages = &.{
            .{
                .role = .user,
                .content = &.{.{ .text = "Hello" }},
            },
        },
        .system_prompt = "You are a helpful assistant",
    };

    const body = try provider.buildRequestBody(req);
    defer alloc.free(body);

    // "system" key must be present in the root object.
    try std.testing.expect(std.mem.indexOf(u8, body, "\"system\"") != null);
    // The system text must appear in the serialized output.
    try std.testing.expect(std.mem.indexOf(u8, body, "helpful assistant") != null);

    // Parse and verify structure.
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, body, .{});
    defer parsed.deinit();
    const root = parsed.value;
    const system_v = root.object.get("system").?;
    try std.testing.expect(system_v == .array);
    try std.testing.expect(system_v.array.items.len > 0);
}

test "bedrock converse: no system prompt omits 'system' field" {
    const alloc = std.testing.allocator;
    var provider = try bedrock.BedrockProvider.init(alloc, "us-east-1", "test-model");
    defer provider.deinit();

    const req = bedrock.Request{
        .model_id = "test-model",
        .messages = &.{
            .{ .role = .user, .content = &.{.{ .text = "Hi" }} },
        },
    };

    const body = try provider.buildRequestBody(req);
    defer alloc.free(body);

    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, body, .{});
    defer parsed.deinit();
    try std.testing.expect(parsed.value.object.get("system") == null);
}

test "bedrock converse: inferenceConfig contains maxTokens" {
    const alloc = std.testing.allocator;
    var provider = try bedrock.BedrockProvider.init(alloc, "us-east-1", "test-model");
    defer provider.deinit();

    const req = bedrock.Request{
        .model_id = "test-model",
        .messages = &.{
            .{ .role = .user, .content = &.{.{ .text = "Hi" }} },
        },
        .max_tokens = 2048,
    };

    const body = try provider.buildRequestBody(req);
    defer alloc.free(body);

    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, body, .{});
    defer parsed.deinit();
    const root = parsed.value;
    const ic = root.object.get("inferenceConfig").?;
    try std.testing.expect(ic == .object);
    const mt = ic.object.get("maxTokens").?;
    try std.testing.expect(mt == .integer);
    try std.testing.expectEqual(@as(i64, 2048), mt.integer);
}

test "bedrock converse: temperature is included when set" {
    const alloc = std.testing.allocator;
    var provider = try bedrock.BedrockProvider.init(alloc, "us-east-1", "test-model");
    defer provider.deinit();

    const req = bedrock.Request{
        .model_id = "test-model",
        .messages = &.{
            .{ .role = .user, .content = &.{.{ .text = "Hi" }} },
        },
        .temperature = 0.7,
    };

    const body = try provider.buildRequestBody(req);
    defer alloc.free(body);

    try std.testing.expect(std.mem.indexOf(u8, body, "\"temperature\"") != null);
}

test "bedrock converse: tool definitions produce toolConfig" {
    const alloc = std.testing.allocator;
    var provider = try bedrock.BedrockProvider.init(alloc, "us-east-1", "test-model");
    defer provider.deinit();

    const tools = [_]bedrock.ToolDefinition{
        .{
            .name = "Bash",
            .description = "Run a bash command",
            .input_schema_json = "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\"}}}",
        },
    };

    const req = bedrock.Request{
        .model_id = "test-model",
        .messages = &.{
            .{ .role = .user, .content = &.{.{ .text = "Run ls" }} },
        },
        .tools = &tools,
    };

    const body = try provider.buildRequestBody(req);
    defer alloc.free(body);

    try std.testing.expect(std.mem.indexOf(u8, body, "\"toolConfig\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, body, "\"Bash\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, body, "Run a bash command") != null);
}

test "bedrock converse: tool_use content block serializes with toolUse" {
    const alloc = std.testing.allocator;
    var provider = try bedrock.BedrockProvider.init(alloc, "us-east-1", "test-model");
    defer provider.deinit();

    const req = bedrock.Request{
        .model_id = "test-model",
        .messages = &.{
            .{
                .role = .assistant,
                .content = &.{
                    .{
                        .tool_use = .{
                            .tool_use_id = "tool_id_1",
                            .name = "Read",
                            .input_json = "{\"file_path\":\"/tmp/test.txt\"}",
                        },
                    },
                },
            },
        },
    };

    const body = try provider.buildRequestBody(req);
    defer alloc.free(body);

    try std.testing.expect(std.mem.indexOf(u8, body, "\"toolUse\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, body, "tool_id_1") != null);
    try std.testing.expect(std.mem.indexOf(u8, body, "Read") != null);
}

test "bedrock converse: tool_result content block serializes with toolResult" {
    const alloc = std.testing.allocator;
    var provider = try bedrock.BedrockProvider.init(alloc, "us-east-1", "test-model");
    defer provider.deinit();

    const req = bedrock.Request{
        .model_id = "test-model",
        .messages = &.{
            .{
                .role = .user,
                .content = &.{
                    .{
                        .tool_result = .{
                            .tool_use_id = "tool_id_1",
                            .content = "file contents here",
                            .status = "success",
                        },
                    },
                },
            },
        },
    };

    const body = try provider.buildRequestBody(req);
    defer alloc.free(body);

    try std.testing.expect(std.mem.indexOf(u8, body, "\"toolResult\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, body, "tool_id_1") != null);
    try std.testing.expect(std.mem.indexOf(u8, body, "file contents here") != null);
}

test "bedrock converse: complex conversation with tools" {
    const alloc = std.testing.allocator;
    var provider = try bedrock.BedrockProvider.init(alloc, "us-east-1", "test-model");
    defer provider.deinit();

    const tools = [_]bedrock.ToolDefinition{
        .{
            .name = "test",
            .description = "A test tool",
            .input_schema_json = "{\"type\":\"object\"}",
        },
    };

    const req = bedrock.Request{
        .model_id = "test-model",
        .messages = &.{
            .{ .role = .user, .content = &.{.{ .text = "Use the tool" }} },
            .{
                .role = .assistant,
                .content = &.{
                    .{ .text = "I'll help" },
                    .{ .tool_use = .{ .tool_use_id = "call_1", .name = "test", .input_json = "{}" } },
                },
            },
            .{
                .role = .user,
                .content = &.{
                    .{ .tool_result = .{ .tool_use_id = "call_1", .content = "result" } },
                },
            },
            .{ .role = .assistant, .content = &.{.{ .text = "Done!" }} },
        },
        .tools = &tools,
        .system_prompt = "You are helpful",
    };

    const body = try provider.buildRequestBody(req);
    defer alloc.free(body);

    // Should produce valid JSON with the correct message count.
    try std.testing.expectEqual(@as(usize, 4), try countMessages(alloc, body));

    // Verify the first message is user.
    const first_role = try getFirstMessageRole(alloc, body);
    defer if (first_role) |r| alloc.free(r);
    try std.testing.expectEqualStrings("user", first_role.?);

    // Verify tools and system present.
    try std.testing.expect(std.mem.indexOf(u8, body, "\"system\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, body, "\"toolConfig\"") != null);
}
