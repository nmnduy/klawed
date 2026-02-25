//! tests/test_tool_message_ordering.zig — Zig port of tests/test_tool_message_ordering.c
//!
//! Tests that tool results are inserted at the correct position in the
//! conversation — immediately after the assistant message that issued the
//! tool call, not appended at the end.
//!
//! Maps to ConversationState.addToolResults() in conversation/state.zig.

const std = @import("std");
const state_mod = @import("../conversation/state.zig");
const ct = @import("../conversation/content_types.zig");

const ConversationState = state_mod.ConversationState;
const Message = state_mod.Message;
const ContentBlock = ct.ContentBlock;

// ============================================================================
// Helper: build an assistant message with one tool_use block
// ============================================================================

fn makeAssistantWithToolCall(
    alloc: std.mem.Allocator,
    tool_id: []const u8,
    tool_name: []const u8,
) !Message {
    const blocks = try alloc.alloc(ContentBlock, 1);
    blocks[0] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, tool_id),
        .name = try alloc.dupe(u8, tool_name),
        .arguments_json = try alloc.dupe(u8, "{}"),
    } };
    return Message{ .role = .assistant, .content = blocks };
}

// ============================================================================
// Helper: build a tool_result message for a given tool_id
// ============================================================================

fn makeToolResult(
    alloc: std.mem.Allocator,
    tool_id: []const u8,
    output: []const u8,
) ![]ContentBlock {
    const blocks = try alloc.alloc(ContentBlock, 1);
    blocks[0] = ContentBlock{ .tool_result = .{
        .tool_use_id = try alloc.dupe(u8, tool_id),
        .content = try alloc.dupe(u8, output),
        .is_error = false,
    } };
    return blocks;
}

// ============================================================================
// Test: tool result is inserted immediately after the assistant message,
// even when a subsequent user message was added first.
// ============================================================================

test "tool results inserted after assistant, before later user messages" {
    // Scenario (from C test test_tool_results_inserted_after_assistant):
    //   1. Assistant sends tool call (call_123 / Bash)
    //   2. User sends a plain text message "Cancel that"
    //   3. Tool result arrives for call_123
    //
    // Expected ordering: [Asst(tool_call), User(tool_result), User("Cancel that")]

    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    // Step 1: assistant message with tool call
    try state.addMessage(try makeAssistantWithToolCall(alloc, "call_123", "Bash"));

    // Step 2: user sends a follow-up message
    try state.addUserMessage("Cancel that");

    // Step 3: tool result arrives — should be inserted at position 1
    const result_blocks = try makeToolResult(alloc, "call_123", "command output");
    try state.addToolResults(result_blocks, "call_123");

    // Verify: 3 messages total
    try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);

    // [0] assistant with tool call
    try std.testing.expect(state.messages.items[0].role == .assistant);
    try std.testing.expect(state.messages.items[0].content[0] == .tool_use);

    // [1] tool result (inserted at position 1)
    try std.testing.expect(state.messages.items[1].role == .user);
    try std.testing.expect(state.messages.items[1].content[0] == .tool_result);
    try std.testing.expectEqualStrings("call_123", state.messages.items[1].content[0].tool_result.tool_use_id);

    // [2] the subsequent user message
    try std.testing.expect(state.messages.items[2].role == .user);
    try std.testing.expect(state.messages.items[2].content[0] == .text);
    try std.testing.expectEqualStrings("Cancel that", state.messages.items[2].content[0].text.text);
}

// ============================================================================
// Test: correct pre-existing order is left unchanged
// ============================================================================

test "existing correct ordering is not disturbed" {
    // When tool results are already in the right place, no extra messages appear.

    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    // Assistant sends tool call
    try state.addMessage(try makeAssistantWithToolCall(alloc, "call_1", "Read"));

    // Tool result immediately follows (correct ordering already)
    const result_blocks = try makeToolResult(alloc, "call_1", "file content");
    try state.addToolResults(result_blocks, "call_1");

    // User follows
    try state.addUserMessage("User message");

    // Verify ordering is [Asst, User(tool_result), User(text)]
    try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .assistant);
    try std.testing.expect(state.messages.items[1].content[0] == .tool_result);
    try std.testing.expect(state.messages.items[2].content[0] == .text);
}

// ============================================================================
// Test: tool result appended at end when no matching assistant message found
// ============================================================================

test "tool result appended at end when no matching assistant found" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addUserMessage("Hello");

    // Tool result with an unknown id — falls back to append
    const blocks = try alloc.alloc(ContentBlock, 1);
    blocks[0] = ContentBlock{ .tool_result = .{
        .tool_use_id = try alloc.dupe(u8, "unknown_id"),
        .content = try alloc.dupe(u8, "result"),
        .is_error = false,
    } };
    try state.addToolResults(blocks, "unknown_id");

    try std.testing.expectEqual(@as(usize, 2), state.messages.items.len);
    try std.testing.expect(state.messages.items[1].content[0] == .tool_result);
}

// ============================================================================
// Test: multiple tool calls in separate assistant messages, each gets result
// after its own assistant message
// ============================================================================

test "two separate tool calls each get result after their own assistant" {
    // Sequence: Asst(call_1) → User(text) → Asst(call_2)
    // Then:
    //   addToolResults for call_1 → inserts at position 1
    //   addToolResults for call_2 → inserts at the new end (after Asst(call_2))
    //
    // Expected final: [Asst(call_1), User(result_1), User(text), Asst(call_2), User(result_2)]

    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addMessage(try makeAssistantWithToolCall(alloc, "call_1", "Read"));
    try state.addUserMessage("User message 1");
    try state.addMessage(try makeAssistantWithToolCall(alloc, "call_2", "Bash"));

    // Insert result for call_1 (should go to position 1)
    const r1 = try makeToolResult(alloc, "call_1", "read output");
    try state.addToolResults(r1, "call_1");

    // Now: [Asst(call_1), User(result_1), User(text), Asst(call_2)]
    try std.testing.expectEqual(@as(usize, 4), state.messages.items.len);
    try std.testing.expectEqualStrings("call_1", state.messages.items[1].content[0].tool_result.tool_use_id);
    try std.testing.expect(state.messages.items[2].content[0] == .text);
    try std.testing.expect(state.messages.items[3].content[0] == .tool_use);

    // Insert result for call_2 (should go after Asst(call_2) at position 4)
    const r2 = try makeToolResult(alloc, "call_2", "bash output");
    try state.addToolResults(r2, "call_2");

    // Now: [Asst(call_1), User(result_1), User(text), Asst(call_2), User(result_2)]
    try std.testing.expectEqual(@as(usize, 5), state.messages.items.len);
    try std.testing.expectEqualStrings("call_2", state.messages.items[4].content[0].tool_result.tool_use_id);
}

// ============================================================================
// Test: multiple tool_use blocks in a single assistant message —
// all results go in a single user message inserted after the assistant.
// ============================================================================

test "multiple tool calls in one assistant message — results grouped together" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    // One assistant message with two tool calls
    const asst_blocks = try alloc.alloc(ContentBlock, 2);
    asst_blocks[0] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_a"),
        .name = try alloc.dupe(u8, "Read"),
        .arguments_json = try alloc.dupe(u8, "{}"),
    } };
    asst_blocks[1] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_b"),
        .name = try alloc.dupe(u8, "Bash"),
        .arguments_json = try alloc.dupe(u8, "{}"),
    } };
    try state.addAssistantMessage(asst_blocks);

    // Follow-up user message
    try state.addUserMessage("User follow-up");

    // Add both results in a single multi-block user message
    // (addToolResults uses first_tool_id to locate the assistant)
    const result_blocks = try alloc.alloc(ContentBlock, 2);
    result_blocks[0] = ContentBlock{ .tool_result = .{
        .tool_use_id = try alloc.dupe(u8, "call_a"),
        .content = try alloc.dupe(u8, "read result"),
        .is_error = false,
    } };
    result_blocks[1] = ContentBlock{ .tool_result = .{
        .tool_use_id = try alloc.dupe(u8, "call_b"),
        .content = try alloc.dupe(u8, "bash result"),
        .is_error = false,
    } };
    // Use "call_a" as the first_tool_id to locate the assistant
    try state.addToolResults(result_blocks, "call_a");

    // Expected: [Asst(call_a,call_b), User(result_a,result_b), User("User follow-up")]
    try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);

    // [0] assistant
    try std.testing.expect(state.messages.items[0].role == .assistant);
    try std.testing.expectEqual(@as(usize, 2), state.messages.items[0].content.len);

    // [1] combined tool results inserted at position 1
    try std.testing.expect(state.messages.items[1].role == .user);
    try std.testing.expectEqual(@as(usize, 2), state.messages.items[1].content.len);
    try std.testing.expect(state.messages.items[1].content[0] == .tool_result);
    try std.testing.expect(state.messages.items[1].content[1] == .tool_result);

    // [2] the follow-up user message
    try std.testing.expect(state.messages.items[2].role == .user);
    try std.testing.expect(state.messages.items[2].content[0] == .text);
    try std.testing.expectEqualStrings("User follow-up", state.messages.items[2].content[0].text.text);
}

// ============================================================================
// Test: addToolResults with null first_tool_id — just appends
// ============================================================================

test "addToolResults with null first_tool_id appends at end" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addUserMessage("Start");

    const blocks = try alloc.alloc(ContentBlock, 1);
    blocks[0] = ContentBlock{ .tool_result = .{
        .tool_use_id = try alloc.dupe(u8, "call_x"),
        .content = try alloc.dupe(u8, "output"),
        .is_error = false,
    } };
    try state.addToolResults(blocks, null);

    try std.testing.expectEqual(@as(usize, 2), state.messages.items.len);
    try std.testing.expect(state.messages.items[1].role == .user);
    try std.testing.expect(state.messages.items[1].content[0] == .tool_result);
}
