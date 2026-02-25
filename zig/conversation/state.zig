//! conversation/state.zig — ConversationState lifecycle management
//!
//! Zig port of src/conversation/conversation_state.c.
//!
//! Key improvements over C:
//! - No static MAX_MESSAGES array — uses `std.ArrayList` which grows dynamically
//! - No mutex needed for single-threaded access; callers requiring thread safety
//!   should wrap with `std.Thread.Mutex` at a higher level
//! - Memory management via explicit allocator threading (no hidden `free` calls)
//! - `Message.deinit` recursively frees all owned content blocks

const std = @import("std");
const content_types = @import("content_types.zig");

pub const ContentBlock = content_types.ContentBlock;
pub const Role = content_types.Role;

// ---------------------------------------------------------------------------
// Message
// ---------------------------------------------------------------------------

/// A single conversation turn.  Owns its `content` slice and all strings
/// within each `ContentBlock`.
pub const Message = struct {
    role: Role,
    /// Owned array of content blocks.
    content: []ContentBlock,

    pub fn deinit(self: *Message, allocator: std.mem.Allocator) void {
        for (self.content) |*blk| {
            blk.deinit(allocator);
        }
        allocator.free(self.content);
    }

    /// Deep-clone this message (used when duplicating conversation history).
    pub fn dupe(self: Message, allocator: std.mem.Allocator) !Message {
        const content_copy = try allocator.alloc(ContentBlock, self.content.len);
        errdefer allocator.free(content_copy);
        for (self.content, 0..) |blk, i| {
            content_copy[i] = try blk.dupe(allocator);
        }
        return Message{ .role = self.role, .content = content_copy };
    }
};

// ---------------------------------------------------------------------------
// ConversationState
// ---------------------------------------------------------------------------

/// Holds the full conversation history and metadata.
///
/// Zig equivalent of the C `ConversationState` struct, but without the mutex,
/// provider pointer, or TUI pointer — those belong at a higher layer.
///
/// ## Memory ownership
///
/// `ConversationState` owns all `Message` objects appended via `addMessage`,
/// `addUserMessage`, etc.  Call `deinit` to free everything.
pub const ConversationState = struct {
    messages: std.ArrayList(Message),
    allocator: std.mem.Allocator,

    pub fn init(allocator: std.mem.Allocator) ConversationState {
        return .{
            .messages = std.ArrayList(Message).init(allocator),
            .allocator = allocator,
        };
    }

    pub fn deinit(self: *ConversationState) void {
        for (self.messages.items) |*msg| {
            msg.deinit(self.allocator);
        }
        self.messages.deinit();
    }

    // -----------------------------------------------------------------------
    // Message builders
    // -----------------------------------------------------------------------

    /// Append a user text message to the conversation.
    pub fn addUserMessage(self: *ConversationState, text: []const u8) !void {
        const text_copy = try self.allocator.dupe(u8, text);
        errdefer self.allocator.free(text_copy);

        const content = try self.allocator.alloc(ContentBlock, 1);
        errdefer self.allocator.free(content);
        content[0] = ContentBlock{ .text = .{ .text = text_copy } };

        try self.messages.append(Message{ .role = .user, .content = content });
    }

    /// Append a system message (position 0, typically the system prompt).
    pub fn addSystemMessage(self: *ConversationState, text: []const u8) !void {
        const text_copy = try self.allocator.dupe(u8, text);
        errdefer self.allocator.free(text_copy);

        const content = try self.allocator.alloc(ContentBlock, 1);
        errdefer self.allocator.free(content);
        content[0] = ContentBlock{ .text = .{ .text = text_copy } };

        try self.messages.append(Message{ .role = .system, .content = content });
    }

    /// Append an assistant message with one or more pre-built content blocks.
    /// Takes ownership of `blocks` (caller must not free them afterwards).
    pub fn addAssistantMessage(self: *ConversationState, blocks: []ContentBlock) !void {
        try self.messages.append(Message{ .role = .assistant, .content = blocks });
    }

    /// Append a pre-built `Message` (takes ownership).
    pub fn addMessage(self: *ConversationState, msg: Message) !void {
        try self.messages.append(msg);
    }

    /// Insert a tool-results message immediately after the assistant message
    /// that contains the corresponding tool call(s).  Falls back to appending
    /// at the end if no matching assistant message is found.
    ///
    /// Takes ownership of `blocks`.
    pub fn addToolResults(
        self: *ConversationState,
        blocks: []ContentBlock,
        first_tool_id: ?[]const u8,
    ) !void {
        var insert_pos: usize = self.messages.items.len; // default: append

        if (first_tool_id) |tid| {
            // Search backwards for the assistant message with this tool call
            var i: usize = self.messages.items.len;
            while (i > 0) {
                i -= 1;
                const msg = &self.messages.items[i];
                if (msg.role == .assistant) {
                    for (msg.content) |blk| {
                        if (blk == .tool_use and std.mem.eql(u8, blk.tool_use.id, tid)) {
                            insert_pos = i + 1;
                            break;
                        }
                    }
                    if (insert_pos != self.messages.items.len) break;
                }
            }
        }

        const new_msg = Message{ .role = .user, .content = blocks };

        if (insert_pos == self.messages.items.len) {
            try self.messages.append(new_msg);
        } else {
            // Insert at specific position — shift elements right
            try self.messages.append(undefined); // grow by 1
            var j: usize = self.messages.items.len - 1;
            while (j > insert_pos) {
                self.messages.items[j] = self.messages.items[j - 1];
                j -= 1;
            }
            self.messages.items[insert_pos] = new_msg;
        }
    }

    // -----------------------------------------------------------------------
    // System prompt management
    // -----------------------------------------------------------------------

    /// Replace the text in the system message (position 0) with `new_text`.
    /// Returns `error.NoSystemMessage` if no system message is present.
    pub fn replaceSystemPrompt(self: *ConversationState, new_text: []const u8) !void {
        if (self.messages.items.len == 0 or self.messages.items[0].role != .system) {
            return error.NoSystemMessage;
        }
        const sys = &self.messages.items[0];
        if (sys.content.len == 0 or sys.content[0] != .text) {
            return error.NoSystemMessage;
        }

        const old_text = sys.content[0].text.text;
        sys.content[0].text.text = try self.allocator.dupe(u8, new_text);
        self.allocator.free(old_text);
    }

    /// Return a slice to the system prompt text, or null if not present.
    pub fn systemPromptText(self: *const ConversationState) ?[]const u8 {
        if (self.messages.items.len == 0) return null;
        const sys = &self.messages.items[0];
        if (sys.role != .system) return null;
        if (sys.content.len == 0 or sys.content[0] != .text) return null;
        return sys.content[0].text.text;
    }

    // -----------------------------------------------------------------------
    // History management
    // -----------------------------------------------------------------------

    /// Remove all messages except the system message at position 0.
    pub fn clearHistory(self: *ConversationState) void {
        const preserve_count: usize = if (self.messages.items.len > 0 and
            (self.messages.items[0].role == .system or
             self.messages.items[0].role == .auto_compaction)) 1 else 0;

        var i: usize = preserve_count;
        while (i < self.messages.items.len) {
            self.messages.items[i].deinit(self.allocator);
            i += 1;
        }
        self.messages.shrinkRetainingCapacity(preserve_count);
    }

    /// Return the last `n` messages (or all messages if fewer than `n` exist).
    pub fn lastMessages(self: *const ConversationState, n: usize) []const Message {
        const total = self.messages.items.len;
        if (total <= n) return self.messages.items;
        return self.messages.items[total - n ..];
    }

    // -----------------------------------------------------------------------
    // Token estimation
    // -----------------------------------------------------------------------

    /// Rough token estimate for the entire conversation.
    /// Uses the same ~4 chars/token heuristic as the C code.
    pub fn totalTokenEstimate(self: *const ConversationState) usize {
        var total: usize = 100; // request overhead
        for (self.messages.items) |msg| {
            total += 3; // role overhead
            for (msg.content) |blk| {
                total += estimateBlockTokens(blk);
            }
            total += 10; // message formatting overhead
        }
        return total;
    }
};

// ---------------------------------------------------------------------------
// Token estimation helper
// ---------------------------------------------------------------------------

fn estimateBlockTokens(blk: ContentBlock) usize {
    const chars_per_token = 4;
    return switch (blk) {
        .text => |b| (b.text.len + chars_per_token - 1) / chars_per_token,
        .tool_use => |b| 5 + (b.name.len + b.arguments_json.len) / chars_per_token,
        .tool_result => |b| 5 + (b.tool_use_id.len + b.content.len) / chars_per_token,
        .image => 500, // conservative estimate
        .thinking => |b| (b.thinking.len + chars_per_token - 1) / chars_per_token,
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "ConversationState basic add and deinit" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    try state.addSystemMessage("You are a helpful assistant.");
    try state.addUserMessage("Hello!");

    try std.testing.expectEqual(@as(usize, 2), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .system);
    try std.testing.expect(state.messages.items[1].role == .user);
    try std.testing.expectEqualStrings("Hello!", state.messages.items[1].content[0].text.text);
}

test "ConversationState.addAssistantMessage" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    const blocks = try allocator.alloc(ContentBlock, 1);
    blocks[0] = ContentBlock{ .text = .{ .text = try allocator.dupe(u8, "I can help!") } };
    try state.addAssistantMessage(blocks);

    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .assistant);
}

test "ConversationState.addToolResults inserts after assistant" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    try state.addUserMessage("run ls");

    // Assistant message with a tool call
    const assistant_blocks = try allocator.alloc(ContentBlock, 1);
    assistant_blocks[0] = ContentBlock{ .tool_use = .{
        .id = try allocator.dupe(u8, "call_001"),
        .name = try allocator.dupe(u8, "Bash"),
        .arguments_json = try allocator.dupe(u8, "{\"command\":\"ls\"}"),
    } };
    try state.addAssistantMessage(assistant_blocks);

    // Tool result blocks
    const result_blocks = try allocator.alloc(ContentBlock, 1);
    result_blocks[0] = ContentBlock{ .tool_result = .{
        .tool_use_id = try allocator.dupe(u8, "call_001"),
        .content = try allocator.dupe(u8, "file1.txt\nfile2.txt"),
        .is_error = false,
    } };
    try state.addToolResults(result_blocks, "call_001");

    // Should be: [user, assistant, user(tool_result)]
    try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);
    try std.testing.expect(state.messages.items[2].role == .user);
    try std.testing.expect(state.messages.items[2].content[0] == .tool_result);
}

test "ConversationState.clearHistory keeps system message" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    try state.addSystemMessage("sys");
    try state.addUserMessage("msg1");
    try state.addUserMessage("msg2");

    try std.testing.expectEqual(@as(usize, 3), state.messages.items.len);
    state.clearHistory();
    try std.testing.expectEqual(@as(usize, 1), state.messages.items.len);
    try std.testing.expect(state.messages.items[0].role == .system);
}

test "ConversationState.replaceSystemPrompt" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    try state.addSystemMessage("original prompt");
    try state.replaceSystemPrompt("updated prompt");

    const text = state.systemPromptText() orelse return error.TestFailed;
    try std.testing.expectEqualStrings("updated prompt", text);
}

test "ConversationState.totalTokenEstimate" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    try state.addUserMessage("hello world");
    const estimate = state.totalTokenEstimate();
    // Should be > 0 and a reasonable value
    try std.testing.expect(estimate > 0);
    try std.testing.expect(estimate < 10000);
}

test "ConversationState.lastMessages" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init(allocator);
    defer state.deinit();

    try state.addUserMessage("msg1");
    try state.addUserMessage("msg2");
    try state.addUserMessage("msg3");

    const last2 = state.lastMessages(2);
    try std.testing.expectEqual(@as(usize, 2), last2.len);
    try std.testing.expectEqualStrings("msg2", last2[0].content[0].text.text);
    try std.testing.expectEqualStrings("msg3", last2[1].content[0].text.text);
}

test "Message.dupe" {
    const allocator = std.testing.allocator;

    const content = try allocator.alloc(ContentBlock, 1);
    content[0] = ContentBlock{ .text = .{ .text = try allocator.dupe(u8, "original") } };
    var original = Message{ .role = .user, .content = content };
    defer original.deinit(allocator);

    var copy = try original.dupe(allocator);
    defer copy.deinit(allocator);

    try std.testing.expectEqualStrings("original", copy.content[0].text.text);
    // Ensure it's a deep copy
    try std.testing.expect(copy.content[0].text.text.ptr != original.content[0].text.text.ptr);
}
