//! tests/test_insert_system_message.zig — Zig port of tests/test_insert_system_message.c
//!
//! Tests system message management in ConversationState:
//! - addSystemMessage creates a system message
//! - System message always lives at the front (position 0)
//! - replaceSystemPrompt replaces existing system message text
//! - systemPromptText returns the correct text
//! - clearHistory preserves the system message
//! - Multiple calls to add/replace system message work correctly
//! - addSystemMessage when no system message exists
//! - replaceSystemPrompt returns error when no system message
//!
//! The C test tested a C-specific `insert_system_message()` function that
//! inserts at position 0, shifting existing messages.  In Zig that logic is
//! distributed across addSystemMessage + replaceSystemPrompt; this file tests
//! the equivalent behaviour.

const std = @import("std");
const state_mod = @import("../conversation/state.zig");
const ct = @import("../conversation/content_types.zig");

const ConversationState = state_mod.ConversationState;
const Message = state_mod.Message;
const ContentBlock = ct.ContentBlock;

// ============================================================================
// Test 1: Insert into empty conversation
// ============================================================================

test "addSystemMessage into empty conversation" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addSystemMessage("System prompt");

    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .system);
    try std.testing.expectEqualStrings("System prompt", state.messages.items[0].content[0].text.text);
}

// ============================================================================
// Test 2: Replace existing system message at position 0
// ============================================================================

test "replaceSystemPrompt replaces existing system message" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addSystemMessage("Old system prompt");
    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);

    try state.replaceSystemPrompt("New system prompt");

    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .system);
    try std.testing.expectEqualStrings("New system prompt", state.messages.items[0].content[0].text.text);
}

// ============================================================================
// Test 3: System message comes before user messages
// ============================================================================

test "system message is at position 0, user messages follow" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addSystemMessage("System prompt");
    try state.addUserMessage("User message 1");
    try state.addUserMessage("User message 2");

    try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .system);
    try std.testing.expectEqualStrings("System prompt", state.messages.items[0].content[0].text.text);
    try std.testing.expect(state.messages.items[1].role == .user);
    try std.testing.expect(state.messages.items[2].role == .user);
}

// ============================================================================
// Test 4: replaceSystemPrompt updates text, count stays the same
// ============================================================================

test "replaceSystemPrompt: count stays the same" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addSystemMessage("First");
    try state.addUserMessage("User1");
    try state.addUserMessage("User2");
    try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);

    try state.replaceSystemPrompt("Second");
    try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);
    try std.testing.expectEqualStrings("Second", state.messages.items[0].content[0].text.text);
}

// ============================================================================
// Test 5: systemPromptText returns null when no system message
// ============================================================================

test "systemPromptText returns null when no system message" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try std.testing.expect(state.systemPromptText() == null);
}

// ============================================================================
// Test 6: systemPromptText returns null when first message is user (not system)
// ============================================================================

test "systemPromptText returns null when first message is user" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addUserMessage("User question");
    try std.testing.expect(state.systemPromptText() == null);
}

// ============================================================================
// Test 7: replaceSystemPrompt returns error when no system message exists
// ============================================================================

test "replaceSystemPrompt returns NoSystemMessage error when no system present" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    const result = state.replaceSystemPrompt("New prompt");
    try std.testing.expectError(error.NoSystemMessage, result);
}

test "replaceSystemPrompt returns NoSystemMessage when first message is user" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addUserMessage("User question");

    const result = state.replaceSystemPrompt("New prompt");
    try std.testing.expectError(error.NoSystemMessage, result);
}

// ============================================================================
// Test 8: Multiple insert calls — idempotent behavior
// (mirrors test_multiple_inserts in the C test)
// ============================================================================

test "multiple system message operations stay at position 0" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    // First add
    try state.addSystemMessage("First system prompt");
    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);

    // Replace should update but not add
    try state.replaceSystemPrompt("Second system prompt");
    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
    try std.testing.expectEqualStrings("Second system prompt", state.messages.items[0].content[0].text.text);
}

// ============================================================================
// Test 9: System message persists across clearHistory
// ============================================================================

test "clearHistory preserves system message at position 0" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addSystemMessage("System prompt");
    try state.addUserMessage("User message 1");
    try state.addUserMessage("User message 2");
    try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);

    state.clearHistory();

    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .system);
    try std.testing.expectEqualStrings("System prompt", state.messages.items[0].content[0].text.text);
}

// ============================================================================
// Test 10: System message with assistant message following
// (mirrors test_insert_with_assistant_at_position_0 in C)
// ============================================================================

test "system message before assistant message" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addSystemMessage("System prompt");

    const asst_blocks = try alloc.alloc(ContentBlock, 1);
    asst_blocks[0] = ContentBlock{ .text = .{
        .text = try alloc.dupe(u8, "Assistant response"),
    } };
    try state.addAssistantMessage(asst_blocks);

    try std.testing.expectEqual(@as(usize, 2), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .system);
    try std.testing.expect(state.messages.items[1].role == .assistant);
}

// ============================================================================
// Test 11: systemPromptText returns correct text
// ============================================================================

test "systemPromptText returns correct text" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addSystemMessage("You are a helpful coding assistant.");

    const text = state.systemPromptText();
    try std.testing.expect(text != null);
    try std.testing.expectEqualStrings("You are a helpful coding assistant.", text.?);
}
