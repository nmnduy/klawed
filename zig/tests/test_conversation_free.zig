//! tests/test_conversation_free.zig — Zig port of tests/test_conversation_free.c
//!
//! Tests conversation memory management in the Zig API:
//! - ConversationState init creates empty state
//! - Messages are owned and freed correctly by deinit
//! - Double-free is prevented (deinit is idempotent via defer)
//! - Messages with text, tool_use, tool_result, and mixed content
//! - clearHistory keeps system message, frees the rest
//! - Session resume scenario: clear and rebuild without crashes

const std = @import("std");
const state_mod = @import("../conversation/state.zig");
const ct = @import("../conversation/content_types.zig");

const ConversationState = state_mod.ConversationState;
const Message = state_mod.Message;
const ContentBlock = ct.ContentBlock;

// ============================================================================
// Basic init / deinit
// ============================================================================

test "ConversationState: init creates empty state" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try std.testing.expectEqual(@as(usize, 0), state.messages.items.len);
}

// ============================================================================
// Free message with text content
// ============================================================================

test "ConversationState: add and free text message" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addUserMessage("Hello, world!");

    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .user);
    try std.testing.expectEqualStrings("Hello, world!", state.messages.items[0].content[0].text.text);
    // deinit via defer — no crash = pass
}

// ============================================================================
// Free message with tool_use content
// ============================================================================

test "ConversationState: add and free tool_use message" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    const blocks = try alloc.alloc(ContentBlock, 1);
    blocks[0] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_abc"),
        .name = try alloc.dupe(u8, "Bash"),
        .arguments_json = try alloc.dupe(u8, "{\"command\":\"ls\"}"),
    } };
    try state.addAssistantMessage(blocks);

    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .assistant);
    try std.testing.expect(state.messages.items[0].content[0] == .tool_use);
    try std.testing.expectEqualStrings("call_abc", state.messages.items[0].content[0].tool_use.id);
    try std.testing.expectEqualStrings("Bash", state.messages.items[0].content[0].tool_use.name);
    // deinit via defer frees all content
}

// ============================================================================
// Free message with tool_result content
// ============================================================================

test "ConversationState: add and free tool_result message" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    const blocks = try alloc.alloc(ContentBlock, 1);
    blocks[0] = ContentBlock{ .tool_result = .{
        .tool_use_id = try alloc.dupe(u8, "call_abc"),
        .content = try alloc.dupe(u8, "exit 0"),
        .is_error = false,
    } };
    try state.addMessage(Message{ .role = .user, .content = blocks });

    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].content[0] == .tool_result);
}

// ============================================================================
// Free message with mixed content (text + tool_use)
// ============================================================================

test "ConversationState: add and free mixed-content message" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    const blocks = try alloc.alloc(ContentBlock, 2);
    blocks[0] = ContentBlock{ .text = .{
        .text = try alloc.dupe(u8, "Let me check that file."),
    } };
    blocks[1] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_123"),
        .name = try alloc.dupe(u8, "Read"),
        .arguments_json = try alloc.dupe(u8, "{\"file_path\":\"/test/file.txt\"}"),
    } };
    try state.addAssistantMessage(blocks);

    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
    try std.testing.expectEqual(@as(usize, 2), state.messages.items[0].content.len);
    try std.testing.expect(state.messages.items[0].content[0] == .text);
    try std.testing.expect(state.messages.items[0].content[1] == .tool_use);
}

// ============================================================================
// "Double-free prevention" — deinit cleans up safely, multiple calls to
// clearHistory followed by deinit should not crash.
// ============================================================================

test "ConversationState: clearHistory then deinit is safe" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addSystemMessage("You are a helpful assistant.");
    try state.addUserMessage("User message");
    try state.addUserMessage("Another message");

    try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);

    state.clearHistory();
    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .system);

    // Second clearHistory is safe
    state.clearHistory();
    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
}

// ============================================================================
// clearHistory keeps system message, frees the rest
// ============================================================================

test "ConversationState: clearHistory preserves system message" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addSystemMessage("System prompt");
    try state.addUserMessage("User input");

    const blocks = try alloc.alloc(ContentBlock, 1);
    blocks[0] = ContentBlock{ .text = .{
        .text = try alloc.dupe(u8, "Assistant response"),
    } };
    try state.addAssistantMessage(blocks);

    try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);

    state.clearHistory();

    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .system);
    try std.testing.expectEqualStrings("System prompt", state.messages.items[0].content[0].text.text);
}

// ============================================================================
// Session resume scenario: load messages, clear, rebuild, no crash
// ============================================================================

test "ConversationState: session clear and reuse scenario" {
    const alloc = std.testing.allocator;

    // First session
    {
        var state = ConversationState.init(alloc);
        defer state.deinit();

        try state.addSystemMessage("You are helpful.");
        try state.addUserMessage("First message");
        try state.addUserMessage("Second message");

        try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);

        state.clearHistory();
        try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);

        // Add new messages after clearing
        try state.addUserMessage("New session message");
        try std.testing.expectEqual(@as(usize, 2), state.messages.items.len);
    }
    // No crash on deinit = pass
}

// ============================================================================
// Multiple messages freed in sequence
// ============================================================================

test "ConversationState: free multiple messages in sequence" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addUserMessage("Message 1");

    // tool_use message
    const tool_blocks = try alloc.alloc(ContentBlock, 1);
    tool_blocks[0] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_1"),
        .name = try alloc.dupe(u8, "Grep"),
        .arguments_json = try alloc.dupe(u8, "{\"pattern\":\"TODO\"}"),
    } };
    try state.addAssistantMessage(tool_blocks);

    // mixed content message
    const mixed_blocks = try alloc.alloc(ContentBlock, 2);
    mixed_blocks[0] = ContentBlock{ .text = .{
        .text = try alloc.dupe(u8, "Let me check that file."),
    } };
    mixed_blocks[1] = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_2"),
        .name = try alloc.dupe(u8, "Read"),
        .arguments_json = try alloc.dupe(u8, "{\"file_path\":\"/test\"}"),
    } };
    try state.addAssistantMessage(mixed_blocks);

    try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);
    // deinit via defer frees all — no crash = pass
}

// ============================================================================
// replaceSystemPrompt — verifies pointer is updated and old allocation freed
// ============================================================================

test "ConversationState: replaceSystemPrompt frees old text" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addSystemMessage("Original system prompt");
    try state.replaceSystemPrompt("Updated system prompt");

    const text = state.systemPromptText().?;
    try std.testing.expectEqualStrings("Updated system prompt", text);
}

test "ConversationState: replaceSystemPrompt on missing system returns error" {
    const alloc = std.testing.allocator;
    var state = ConversationState.init(alloc);
    defer state.deinit();

    try state.addUserMessage("No system message yet");

    const result = state.replaceSystemPrompt("New prompt");
    try std.testing.expectError(error.NoSystemMessage, result);
}

// ============================================================================
// All pointers in ContentBlock are freed (no leaks per leak detector)
// ============================================================================

test "ContentBlock: all fields freed by deinit" {
    const alloc = std.testing.allocator;

    // A tool_use block owns id, name, arguments_json
    var blk = ContentBlock{ .tool_use = .{
        .id = try alloc.dupe(u8, "call_abc"),
        .name = try alloc.dupe(u8, "Read"),
        .arguments_json = try alloc.dupe(u8, "{\"file_path\":\"/tmp/x\"}"),
    } };
    blk.deinit(alloc);
    // Leak detector will catch any unfreed allocations

    // A tool_result block owns tool_use_id and content
    var blk2 = ContentBlock{ .tool_result = .{
        .tool_use_id = try alloc.dupe(u8, "call_abc"),
        .content = try alloc.dupe(u8, "output text"),
        .is_error = false,
    } };
    blk2.deinit(alloc);

    // A text block owns text
    var blk3 = ContentBlock{ .text = .{
        .text = try alloc.dupe(u8, "some text"),
    } };
    blk3.deinit(alloc);
}

// ============================================================================
// Message.dupe — ensures deep copy and independent lifecycle
// ============================================================================

test "Message: dupe creates independent copy" {
    const alloc = std.testing.allocator;

    const content = try alloc.alloc(ContentBlock, 1);
    content[0] = ContentBlock{ .text = .{
        .text = try alloc.dupe(u8, "original text"),
    } };
    var original = Message{ .role = .user, .content = content };
    defer original.deinit(alloc);

    var copy = try original.dupe(alloc);
    defer copy.deinit(alloc);

    try std.testing.expectEqualStrings("original text", copy.content[0].text.text);
    // Verify deep copy — different pointer
    try std.testing.expect(copy.content[0].text.text.ptr != original.content[0].text.text.ptr);
}
