//! tests/test_tool_results_regression.zig — Zig port of tests/test_tool_results_regression.c
//!
//! Regression tests for the bug introduced in commit 414fbe8 where:
//! - Tool results could be freed before being recorded in conversation state
//! - API calls with tool_calls but missing tool_results would cause 400 errors
//! - checkTodoWriteExecuted logic order matters for correctness
//!
//! These tests verify:
//! 1. checkTodoWriteExecuted inspects content before it is moved/freed
//! 2. addToolResults correctly transfers ownership of content blocks
//! 3. Tool result content is accessible after addToolResults

const std = @import("std");
const state_mod = @import("../conversation/state.zig");
const ct = @import("../conversation/content_types.zig");

const ConversationState = state_mod.ConversationState;
const Message = state_mod.Message;
const ContentBlock = ct.ContentBlock;

// ============================================================================
// Helper: build a slice of tool_result ContentBlocks
// ============================================================================

fn createToolResults(
    alloc: std.mem.Allocator,
    count: usize,
    tool_id: []const u8,
    tool_name: []const u8,
) ![]ContentBlock {
    const blocks = try alloc.alloc(ContentBlock, count);
    errdefer alloc.free(blocks);

    for (blocks) |*b| {
        b.* = ContentBlock{ .tool_result = .{
            .tool_use_id = try alloc.dupe(u8, tool_id),
            .content = try alloc.dupe(u8, "command output"),
            .is_error = false,
        } };
        _ = tool_name; // tool name is part of the assistant side, not the result
    }
    return blocks;
}

// ============================================================================
// Test 1: checkTodoWriteExecuted works correctly before addToolResults
// (The "correct order" that avoids the bug)
// ============================================================================

test "checkTodoWriteExecuted: detects TodoWrite before results are stored" {
    const alloc = std.testing.allocator;

    // Create a set of tool_result blocks including one for TodoWrite.
    // In the correct fix, we check for TodoWrite BEFORE calling addToolResults.
    const blocks = try alloc.alloc(ContentBlock, 3);
    defer {
        for (blocks) |*b| b.deinit(alloc);
        alloc.free(blocks);
    }

    blocks[0] = ContentBlock{ .tool_result = .{
        .tool_use_id = try alloc.dupe(u8, "call_1"),
        .content = try alloc.dupe(u8, "result 1"),
        .is_error = false,
    } };
    blocks[1] = ContentBlock{ .tool_result = .{
        .tool_use_id = try alloc.dupe(u8, "call_2"),
        .content = try alloc.dupe(u8, "result 2"),
        .is_error = false,
    } };
    blocks[2] = ContentBlock{ .tool_result = .{
        .tool_use_id = try alloc.dupe(u8, "call_todo"),
        .content = try alloc.dupe(u8, "todo result"),
        .is_error = false,
    } };

    // checkTodoWriteExecuted checks for tool_use blocks with name "TodoWrite".
    // tool_result blocks don't match — this confirms it looks at the right side.
    const found = ct.checkTodoWriteExecuted(blocks);
    try std.testing.expect(!found); // Results are tool_result, not tool_use
}

test "checkTodoWriteExecuted: detects TodoWrite tool_use block" {
    const alloc = std.testing.allocator;

    // The assistant's message contains tool_use blocks (calls, not results)
    const blocks = try alloc.alloc(ContentBlock, 2);
    defer {
        for (blocks) |*b| b.deinit(alloc);
        alloc.free(blocks);
    }

    blocks[0] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_bash"),
        .name = try alloc.dupe(u8, "Bash"),
        .arguments_json = try alloc.dupe(u8, "{\"command\":\"ls\"}"),
    } };
    blocks[1] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_todo"),
        .name = try alloc.dupe(u8, "TodoWrite"),
        .arguments_json = try alloc.dupe(u8, "{\"todos\":[]}"),
    } };

    const found = ct.checkTodoWriteExecuted(blocks);
    try std.testing.expect(found);
}

test "checkTodoWriteExecuted: returns false when TodoWrite absent" {
    const alloc = std.testing.allocator;

    const blocks = try alloc.alloc(ContentBlock, 2);
    defer {
        for (blocks) |*b| b.deinit(alloc);
        alloc.free(blocks);
    }

    blocks[0] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_bash"),
        .name = try alloc.dupe(u8, "Bash"),
        .arguments_json = try alloc.dupe(u8, "{}"),
    } };
    blocks[1] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_read"),
        .name = try alloc.dupe(u8, "Read"),
        .arguments_json = try alloc.dupe(u8, "{}"),
    } };

    const found = ct.checkTodoWriteExecuted(blocks);
    try std.testing.expect(!found);
}

// ============================================================================
// Test 2: After addToolResults, tool results are in the conversation state
// (verifies ownership transfer works — no use-after-free)
// ============================================================================

test "addToolResults: tool results accessible after adding to conversation" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    // Set up assistant with a tool call
    const asst_blocks = try alloc.alloc(ContentBlock, 1);
    asst_blocks[0] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "tool_call_123"),
        .name = try alloc.dupe(u8, "Bash"),
        .arguments_json = try alloc.dupe(u8, "{\"command\":\"ls\"}"),
    } };
    try state.addAssistantMessage(asst_blocks);

    // Add tool results — ownership is transferred to state
    const result_blocks = try createToolResults(alloc, 1, "tool_call_123", "Bash");
    try state.addToolResults(result_blocks, "tool_call_123");

    // Verify tool results are accessible in the conversation
    try std.testing.expectEqual(@as(usize, 2), state.messages.items.len);
    const result_msg = state.messages.items[1];
    try std.testing.expect(result_msg.role == .user);
    try std.testing.expect(result_msg.content[0] == .tool_result);
    try std.testing.expectEqualStrings("tool_call_123", result_msg.content[0].tool_result.tool_use_id);
}

// ============================================================================
// Test 3: Missing tool results scenario — conversation has tool calls but no results
// (In Zig, this is a logical test; actual API behavior depends on callers)
// ============================================================================

test "conversation with tool call but no result — detectable" {
    // This tests that we can identify the inconsistent state:
    // an assistant message with tool_use but no following tool_result.
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    // Only the tool call, no result
    const asst_blocks = try alloc.alloc(ContentBlock, 1);
    asst_blocks[0] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_missing"),
        .name = try alloc.dupe(u8, "Read"),
        .arguments_json = try alloc.dupe(u8, "{}"),
    } };
    try state.addAssistantMessage(asst_blocks);

    // No tool result added — state has 1 message
    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);

    // The assistant message contains a tool_use with no corresponding result
    const asst_msg = state.messages.items[0];
    var found_unpaired_call = false;
    for (asst_msg.content) |blk| {
        if (blk == .tool_use) {
            found_unpaired_call = true;
        }
    }
    try std.testing.expect(found_unpaired_call);
    // A correct implementation would add a synthetic error result before making
    // the next API call — this test documents the detectable inconsistency.
}

// ============================================================================
// Test 4: Correct order of operations — extract info before storing results
// ============================================================================

test "correct order: inspect blocks then store" {
    // Demonstrates the correct fix: extract what we need from the blocks array
    // BEFORE calling addToolResults (which takes ownership).
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    // Build assistant message with TodoWrite and another tool
    const asst_blocks = try alloc.alloc(ContentBlock, 2);
    asst_blocks[0] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_bash"),
        .name = try alloc.dupe(u8, "Bash"),
        .arguments_json = try alloc.dupe(u8, "{}"),
    } };
    asst_blocks[1] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_todo"),
        .name = try alloc.dupe(u8, "TodoWrite"),
        .arguments_json = try alloc.dupe(u8, "{\"todos\":[]}"),
    } };
    try state.addAssistantMessage(asst_blocks);

    // Build results for both tools
    const result_blocks = try alloc.alloc(ContentBlock, 2);
    result_blocks[0] = ContentBlock{ .tool_result = .{
        .tool_use_id = try alloc.dupe(u8, "call_bash"),
        .content = try alloc.dupe(u8, "bash output"),
        .is_error = false,
    } };
    result_blocks[1] = ContentBlock{ .tool_result = .{
        .tool_use_id = try alloc.dupe(u8, "call_todo"),
        .content = try alloc.dupe(u8, "todo result"),
        .is_error = false,
    } };

    // CORRECT ORDER: check for TodoWrite in the ASSISTANT blocks (not results)
    // before doing anything else
    const todo_executed = ct.checkTodoWriteExecuted(asst_blocks);
    try std.testing.expect(todo_executed);

    // Now store results (ownership transferred)
    try state.addToolResults(result_blocks, "call_bash");

    // Results are safely stored; todo_executed info extracted correctly
    try std.testing.expectEqual(@as(usize, 2), state.messages.items.len);
    try std.testing.expect(todo_executed);
}

// ============================================================================
// Test 5: Empty result slice — addToolResults handles edge case gracefully
// ============================================================================

test "addToolResults: empty result slice appends empty user message" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addUserMessage("Start");

    // Empty blocks slice — just an empty user message
    const empty_blocks = try alloc.alloc(ContentBlock, 0);
    try state.addToolResults(empty_blocks, null);

    try std.testing.expectEqual(@as(usize, 2), state.messages.items.len);
    try std.testing.expect(state.messages.items[1].role == .user);
    try std.testing.expectEqual(@as(usize, 0), state.messages.items[1].content.len);
}
