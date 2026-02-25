//! tests/test_openai_format.zig — Zig port of tests/test_openai_format.c
//!
//! Tests OpenAI message format validation:
//! - Tool calls must have corresponding tool responses
//! - Tool messages must have role="tool" and tool_call_id
//! - Message ordering must be correct

const std = @import("std");

// ---------------------------------------------------------------------------
// Helpers — pure-Zig equivalents of the C validate_* helpers
// ---------------------------------------------------------------------------

/// Represents a parsed message for format-validation purposes.
const Msg = struct {
    role: []const u8,
    tool_calls: ?[]const []const u8 = null, // list of tool_call_ids emitted
    tool_call_id: ?[]const u8 = null, // for role="tool" messages
    content: ?[]const u8 = null,
};

/// Check that every tool_call in an assistant message has a matching
/// role="tool" response before the next assistant/user message.
///
/// Returns true if valid, false if a tool_call is missing its response.
fn validateToolCallResponses(messages: []const Msg) bool {
    for (messages, 0..) |msg, i| {
        if (!std.mem.eql(u8, msg.role, "assistant")) continue;
        const calls = msg.tool_calls orelse continue;

        // For each call_id, look for a subsequent tool message with that id.
        for (calls) |call_id| {
            var found = false;
            for (messages[i + 1 ..]) |next| {
                // Stop searching at the next assistant or user turn.
                if (std.mem.eql(u8, next.role, "assistant") or
                    std.mem.eql(u8, next.role, "user"))
                {
                    break;
                }
                if (std.mem.eql(u8, next.role, "tool")) {
                    if (next.tool_call_id) |tcid| {
                        if (std.mem.eql(u8, tcid, call_id)) {
                            found = true;
                            break;
                        }
                    }
                }
            }
            if (!found) return false;
        }
    }
    return true;
}

/// Returns true iff `msg` is a valid tool message (has tool_call_id and content).
/// Non-tool messages always pass.
fn validateToolMessageFormat(msg: Msg) bool {
    if (!std.mem.eql(u8, msg.role, "tool")) return true;
    if (msg.tool_call_id == null) return false;
    if (msg.content == null) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "openai format: valid tool call response pairing" {
    const messages = [_]Msg{
        .{ .role = "user", .content = "Hello" },
        .{ .role = "assistant", .tool_calls = &.{"call_1"} },
        .{ .role = "tool", .tool_call_id = "call_1", .content = "result" },
    };
    try std.testing.expect(validateToolCallResponses(&messages));
}

test "openai format: multiple tool calls all responded" {
    const calls = [_][]const u8{ "call_1", "call_2" };
    const messages = [_]Msg{
        .{ .role = "assistant", .tool_calls = &calls },
        .{ .role = "tool", .tool_call_id = "call_1", .content = "result1" },
        .{ .role = "tool", .tool_call_id = "call_2", .content = "result2" },
    };
    try std.testing.expect(validateToolCallResponses(&messages));
}

test "openai format: missing tool response fails validation" {
    const calls = [_][]const u8{ "call_1", "call_2" };
    const messages = [_]Msg{
        .{ .role = "assistant", .tool_calls = &calls },
        // Only call_1 responded — call_2 is missing.
        .{ .role = "tool", .tool_call_id = "call_1", .content = "result1" },
    };
    try std.testing.expect(!validateToolCallResponses(&messages));
}

test "openai format: tool message requires tool_call_id" {
    // A tool message without tool_call_id is invalid.
    const msg = Msg{ .role = "tool", .content = "result" };
    try std.testing.expect(!validateToolMessageFormat(msg));
}

test "openai format: tool message requires content" {
    // A tool message without content is invalid.
    const msg = Msg{ .role = "tool", .tool_call_id = "call_1" };
    try std.testing.expect(!validateToolMessageFormat(msg));
}

test "openai format: valid tool message" {
    const msg = Msg{ .role = "tool", .tool_call_id = "call_1", .content = "result" };
    try std.testing.expect(validateToolMessageFormat(msg));
}

test "openai format: assistant with tool_calls may have null content" {
    // The assistant message has no content — that is fine for tool-call turns.
    const messages = [_]Msg{
        .{ .role = "assistant", .content = null, .tool_calls = &.{"call_1"} },
        .{ .role = "tool", .tool_call_id = "call_1", .content = "result" },
    };
    try std.testing.expect(validateToolCallResponses(&messages));
}

test "openai format: error response is still a valid tool message" {
    const msg = Msg{
        .role = "tool",
        .tool_call_id = "call_1",
        .content = "{\"error\": \"Tool call missing 'function' object\"}",
    };
    try std.testing.expect(validateToolMessageFormat(msg));

    const messages = [_]Msg{
        .{ .role = "assistant", .tool_calls = &.{"call_1"} },
        msg,
    };
    try std.testing.expect(validateToolCallResponses(&messages));
}

test "openai format: multi-turn conversation is valid" {
    const messages = [_]Msg{
        .{ .role = "user", .content = "Run ls" },
        .{ .role = "assistant", .tool_calls = &.{"call_1"} },
        .{ .role = "tool", .tool_call_id = "call_1", .content = "file1.txt" },
        .{ .role = "assistant", .content = "Found file1.txt" },
        .{ .role = "user", .content = "Read it" },
        .{ .role = "assistant", .tool_calls = &.{"call_2"} },
        .{ .role = "tool", .tool_call_id = "call_2", .content = "contents" },
    };
    try std.testing.expect(validateToolCallResponses(&messages));
}

test "openai format: non-tool message always passes validateToolMessageFormat" {
    // User and assistant messages are not subject to tool-message validation.
    const user_msg = Msg{ .role = "user", .content = "hello" };
    const asst_msg = Msg{ .role = "assistant", .content = "hi" };
    try std.testing.expect(validateToolMessageFormat(user_msg));
    try std.testing.expect(validateToolMessageFormat(asst_msg));
}

test "openai format: tool call response stops at next assistant message" {
    // call_2 has no response — it appears after another assistant message boundary.
    const calls1 = [_][]const u8{"call_1"};
    const calls2 = [_][]const u8{"call_2"};
    const messages = [_]Msg{
        .{ .role = "assistant", .tool_calls = &calls1 },
        .{ .role = "tool", .tool_call_id = "call_1", .content = "result1" },
        .{ .role = "assistant", .tool_calls = &calls2 },
        // No response for call_2 — should fail
    };
    try std.testing.expect(!validateToolCallResponses(&messages));
}

test "openai format: empty message list is valid" {
    const messages = [_]Msg{};
    try std.testing.expect(validateToolCallResponses(&messages));
}

test "openai format: JSON parsing — valid tool call JSON structure" {
    const alloc = std.testing.allocator;
    // Replicate the C test's JSON parsing using std.json.
    const json =
        \\[
        \\  {"role": "user", "content": "Hello"},
        \\  {"role": "assistant", "content": null, "tool_calls": [
        \\    {"id": "call_1", "type": "function", "function": {"name": "bash", "arguments": "{}"}}
        \\  ]},
        \\  {"role": "tool", "tool_call_id": "call_1", "content": "result"}
        \\]
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();

    // The top-level value should be an array.
    try std.testing.expect(parsed.value == .array);
    try std.testing.expectEqual(@as(usize, 3), parsed.value.array.items.len);

    // The second message (index 1) should have tool_calls.
    const asst = parsed.value.array.items[1];
    try std.testing.expect(asst == .object);
    const tool_calls = asst.object.get("tool_calls").?;
    try std.testing.expect(tool_calls == .array);
    try std.testing.expectEqual(@as(usize, 1), tool_calls.array.items.len);
}
