//! conversation/message.zig — Message building and parsing
//!
//! Zig port of src/conversation/message_builder.c and
//! src/conversation/message_parser.c.
//!
//! ## MessageBuilder
//!
//! Fluent API for constructing `Message` values from raw strings, tool calls,
//! and tool results.  The builder accumulates `ContentBlock` items and then
//! produces a single `Message` when `build()` is called.
//!
//! ## MessageParser
//!
//! Parses an OpenAI-format `message` JSON object (as `std.json.Value`) into
//! an owned `Message`.  This mirrors `add_assistant_message_openai` in C.

const std = @import("std");
const content_types = @import("content_types.zig");

pub const ContentBlock = content_types.ContentBlock;
pub const Role = content_types.Role;
pub const Message = @import("state.zig").Message;

// ---------------------------------------------------------------------------
// MessageBuilder
// ---------------------------------------------------------------------------

/// Fluent builder for constructing a `Message` with multiple content blocks.
///
/// ## Usage
/// ```zig
/// var builder = MessageBuilder.init(allocator, .assistant);
/// try builder.addText("Here is the result:");
/// try builder.addToolUse("call_001", "Bash", "{\"command\":\"ls\"}");
/// const msg = try builder.build();
/// // msg is owned by caller; call msg.deinit(allocator) when done
/// ```
pub const MessageBuilder = struct {
    allocator: std.mem.Allocator,
    role: Role,
    blocks: std.ArrayList(ContentBlock),

    pub fn init(allocator: std.mem.Allocator, role: Role) MessageBuilder {
        return .{
            .allocator = allocator,
            .role = role,
            .blocks = std.ArrayList(ContentBlock).init(allocator),
        };
    }

    /// Free all accumulated blocks and the builder itself.
    /// Call this instead of `build()` if you decide to abort construction.
    pub fn deinit(self: *MessageBuilder) void {
        for (self.blocks.items) |*blk| {
            blk.deinit(self.allocator);
        }
        self.blocks.deinit();
    }

    // -----------------------------------------------------------------------
    // Block adders
    // -----------------------------------------------------------------------

    /// Append a plain text block.
    pub fn addText(self: *MessageBuilder, text: []const u8) !void {
        const text_copy = try self.allocator.dupe(u8, text);
        errdefer self.allocator.free(text_copy);
        try self.blocks.append(ContentBlock{ .text = .{ .text = text_copy } });
    }

    /// Append a text block with a prompt-caching marker.
    pub fn addTextWithCache(self: *MessageBuilder, text: []const u8) !void {
        const text_copy = try self.allocator.dupe(u8, text);
        errdefer self.allocator.free(text_copy);
        try self.blocks.append(ContentBlock{ .text = .{
            .text = text_copy,
            .cache_control = .{},
        } });
    }

    /// Append a tool-use (tool call) block.
    pub fn addToolUse(
        self: *MessageBuilder,
        id: []const u8,
        name: []const u8,
        arguments_json: []const u8,
    ) !void {
        const id_copy = try self.allocator.dupe(u8, id);
        errdefer self.allocator.free(id_copy);
        const name_copy = try self.allocator.dupe(u8, name);
        errdefer self.allocator.free(name_copy);
        const args_copy = try self.allocator.dupe(u8, arguments_json);
        errdefer self.allocator.free(args_copy);

        try self.blocks.append(ContentBlock{ .tool_use = .{
            .id = id_copy,
            .name = name_copy,
            .arguments_json = args_copy,
        } });
    }

    /// Append a tool-result block.
    pub fn addToolResult(
        self: *MessageBuilder,
        tool_use_id: []const u8,
        content: []const u8,
        is_error: bool,
    ) !void {
        const tid_copy = try self.allocator.dupe(u8, tool_use_id);
        errdefer self.allocator.free(tid_copy);
        const content_copy = try self.allocator.dupe(u8, content);
        errdefer self.allocator.free(content_copy);

        try self.blocks.append(ContentBlock{ .tool_result = .{
            .tool_use_id = tid_copy,
            .content = content_copy,
            .is_error = is_error,
        } });
    }

    /// Append an image block.
    pub fn addImage(
        self: *MessageBuilder,
        media_type: []const u8,
        base64_data: []const u8,
    ) !void {
        const mt_copy = try self.allocator.dupe(u8, media_type);
        errdefer self.allocator.free(mt_copy);
        const data_copy = try self.allocator.dupe(u8, base64_data);
        errdefer self.allocator.free(data_copy);

        try self.blocks.append(ContentBlock{ .image = .{
            .media_type = mt_copy,
            .data = data_copy,
        } });
    }

    // -----------------------------------------------------------------------
    // Finalizer
    // -----------------------------------------------------------------------

    /// Consume the builder and produce an owned `Message`.
    /// After calling `build()` the builder must not be used.
    pub fn build(self: *MessageBuilder) !Message {
        const content = try self.blocks.toOwnedSlice();
        return Message{ .role = self.role, .content = content };
    }
};

// ---------------------------------------------------------------------------
// MessageParser
// ---------------------------------------------------------------------------

/// Parses a raw `std.json.Value` (an OpenAI `message` object) into an owned
/// `Message`.  Caller must call `msg.deinit(allocator)` when done.
///
/// Mirrors the C function `add_assistant_message_openai`.
pub const MessageParser = struct {
    allocator: std.mem.Allocator,

    pub fn init(allocator: std.mem.Allocator) MessageParser {
        return .{ .allocator = allocator };
    }

    /// Parse an OpenAI `message` JSON value into a `Message`.
    ///
    /// Handles:
    /// - `content` string (plain text)
    /// - `tool_calls` array (tool_use blocks)
    /// - `reasoning_content` string (stored as ThinkingBlock)
    pub fn parseOpenAI(self: MessageParser, msg_value: std.json.Value) !Message {
        if (msg_value != .object) return error.InvalidMessageFormat;
        const obj = msg_value.object;

        var builder = MessageBuilder.init(self.allocator, .assistant);
        errdefer builder.deinit();

        // Determine role
        const role: Role = blk: {
            const role_v = obj.get("role") orelse break :blk .assistant;
            if (role_v == .string) {
                if (std.mem.eql(u8, role_v.string, "user")) break :blk .user;
                if (std.mem.eql(u8, role_v.string, "system")) break :blk .system;
            }
            break :blk .assistant;
        };
        builder.role = role;

        // Extract reasoning_content (stored once, on the first content block)
        var reasoning_text: ?[]const u8 = null;
        if (obj.get("reasoning_content")) |rc_v| {
            if (rc_v == .string and rc_v.string.len > 0) {
                reasoning_text = rc_v.string;
            }
        }

        // Text content
        var added_reasoning = false;
        if (obj.get("content")) |content_v| {
            if (content_v == .string and content_v.string.len > 0) {
                // Trim leading/trailing whitespace (mirrors strdup_trim in C)
                const trimmed = std.mem.trim(u8, content_v.string, " \t\n\r");
                if (trimmed.len > 0) {
                    try builder.addText(trimmed);
                    added_reasoning = true;
                }
            }
        }

        // Store reasoning as a ThinkingBlock if not yet consumed
        if (reasoning_text) |rt| {
            if (!added_reasoning or true) { // always add if present
                const rt_copy = try self.allocator.dupe(u8, rt);
                errdefer self.allocator.free(rt_copy);
                const sig_copy = try self.allocator.dupe(u8, "");
                errdefer self.allocator.free(sig_copy);
                try builder.blocks.append(ContentBlock{ .thinking = .{
                    .thinking = rt_copy,
                    .signature = sig_copy,
                } });
            }
        }

        // Tool calls
        if (obj.get("tool_calls")) |tc_arr| {
            if (tc_arr == .array) {
                for (tc_arr.array.items) |tc| {
                    if (tc != .object) continue;
                    const tc_obj = tc.object;

                    const id_v = tc_obj.get("id") orelse continue;
                    if (id_v != .string) continue;

                    const fn_v = tc_obj.get("function") orelse continue;
                    if (fn_v != .object) continue;
                    const fn_obj = fn_v.object;

                    const name_v = fn_obj.get("name") orelse continue;
                    if (name_v != .string) continue;

                    const args_str: []const u8 = blk: {
                        const av = fn_obj.get("arguments") orelse break :blk "{}";
                        break :blk if (av == .string) av.string else "{}";
                    };

                    try builder.addToolUse(id_v.string, name_v.string, args_str);
                }
            }
        }

        // Validate we got at least something
        if (builder.blocks.items.len == 0) {
            return error.EmptyMessage;
        }

        return builder.build();
    }

    /// Parse an Anthropic-format `content` array (array of content block objects)
    /// into a slice of `ContentBlock`.  Caller owns the returned slice.
    pub fn parseAnthropicContent(
        self: MessageParser,
        content_array: std.json.Value,
    ) ![]ContentBlock {
        if (content_array != .array) return error.InvalidContentFormat;

        var blocks = std.ArrayList(ContentBlock).init(self.allocator);
        errdefer {
            for (blocks.items) |*b| b.deinit(self.allocator);
            blocks.deinit();
        }

        for (content_array.array.items) |item| {
            if (item != .object) continue;
            const item_obj = item.object;

            const type_v = item_obj.get("type") orelse continue;
            if (type_v != .string) continue;
            const type_str = type_v.string;

            if (std.mem.eql(u8, type_str, "text")) {
                const text_v = item_obj.get("text") orelse continue;
                if (text_v != .string) continue;
                try blocks.append(ContentBlock{ .text = .{
                    .text = try self.allocator.dupe(u8, text_v.string),
                } });
            } else if (std.mem.eql(u8, type_str, "tool_use")) {
                const id_v = item_obj.get("id") orelse continue;
                const name_v = item_obj.get("name") orelse continue;
                const input_v = item_obj.get("input") orelse continue;
                if (id_v != .string or name_v != .string) continue;

                // Serialize input back to JSON string
                var args_buf = std.ArrayList(u8).init(self.allocator);
                defer args_buf.deinit();
                try std.json.stringify(input_v, .{}, args_buf.writer());

                try blocks.append(ContentBlock{ .tool_use = .{
                    .id = try self.allocator.dupe(u8, id_v.string),
                    .name = try self.allocator.dupe(u8, name_v.string),
                    .arguments_json = try args_buf.toOwnedSlice(),
                } });
            } else if (std.mem.eql(u8, type_str, "tool_result")) {
                const tid_v = item_obj.get("tool_use_id") orelse continue;
                if (tid_v != .string) continue;
                const content_v = item_obj.get("content") orelse continue;
                const content_str = if (content_v == .string) content_v.string else "";
                const is_error = blk: {
                    const ev = item_obj.get("is_error") orelse break :blk false;
                    break :blk ev == .bool and ev.bool;
                };
                try blocks.append(ContentBlock{ .tool_result = .{
                    .tool_use_id = try self.allocator.dupe(u8, tid_v.string),
                    .content = try self.allocator.dupe(u8, content_str),
                    .is_error = is_error,
                } });
            } else if (std.mem.eql(u8, type_str, "thinking")) {
                const thinking_v = item_obj.get("thinking") orelse continue;
                if (thinking_v != .string) continue;
                const sig: []const u8 = blk: {
                    const sv = item_obj.get("signature") orelse break :blk "";
                    break :blk if (sv == .string) sv.string else "";
                };
                try blocks.append(ContentBlock{ .thinking = .{
                    .thinking = try self.allocator.dupe(u8, thinking_v.string),
                    .signature = try self.allocator.dupe(u8, sig),
                } });
            }
        }

        return blocks.toOwnedSlice();
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "MessageBuilder.addText and build" {
    const allocator = std.testing.allocator;
    var builder = MessageBuilder.init(allocator, .user);
    try builder.addText("Hello, world!");
    var msg = try builder.build();
    defer msg.deinit(allocator);

    try std.testing.expect(msg.role == .user);
    try std.testing.expectEqual(@as(usize, 1), msg.content.len);
    try std.testing.expectEqualStrings("Hello, world!", msg.content[0].text.text);
}

test "MessageBuilder.addToolUse and addText combined" {
    const allocator = std.testing.allocator;
    var builder = MessageBuilder.init(allocator, .assistant);
    try builder.addText("I will run a command.");
    try builder.addToolUse("call_99", "Bash", "{\"command\":\"echo hi\"}");
    var msg = try builder.build();
    defer msg.deinit(allocator);

    try std.testing.expectEqual(@as(usize, 2), msg.content.len);
    try std.testing.expect(msg.content[0] == .text);
    try std.testing.expect(msg.content[1] == .tool_use);
    try std.testing.expectEqualStrings("Bash", msg.content[1].tool_use.name);
}

test "MessageBuilder.addToolResult" {
    const allocator = std.testing.allocator;
    var builder = MessageBuilder.init(allocator, .user);
    try builder.addToolResult("call_99", "hello from bash", false);
    var msg = try builder.build();
    defer msg.deinit(allocator);

    try std.testing.expectEqual(@as(usize, 1), msg.content.len);
    try std.testing.expect(msg.content[0] == .tool_result);
    try std.testing.expect(!msg.content[0].tool_result.is_error);
}

test "MessageParser.parseOpenAI text only" {
    const allocator = std.testing.allocator;
    const json_str =
        \\{"role":"assistant","content":"Hello!"}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_str, .{});
    defer parsed.deinit();

    const parser = MessageParser.init(allocator);
    var msg = try parser.parseOpenAI(parsed.value);
    defer msg.deinit(allocator);

    try std.testing.expect(msg.role == .assistant);
    try std.testing.expect(msg.content[0] == .text);
    try std.testing.expectEqualStrings("Hello!", msg.content[0].text.text);
}

test "MessageParser.parseOpenAI with tool calls" {
    const allocator = std.testing.allocator;
    const json_str =
        \\{
        \\  "role": "assistant",
        \\  "content": null,
        \\  "tool_calls": [{
        \\    "id": "call_001",
        \\    "type": "function",
        \\    "function": {"name": "Bash", "arguments": "{\"command\":\"ls\"}"}
        \\  }]
        \\}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_str, .{});
    defer parsed.deinit();

    const parser = MessageParser.init(allocator);
    var msg = try parser.parseOpenAI(parsed.value);
    defer msg.deinit(allocator);

    // Find the tool_use block
    var found = false;
    for (msg.content) |blk| {
        if (blk == .tool_use) {
            try std.testing.expectEqualStrings("call_001", blk.tool_use.id);
            try std.testing.expectEqualStrings("Bash", blk.tool_use.name);
            found = true;
        }
    }
    try std.testing.expect(found);
}

test "MessageParser.parseOpenAI with reasoning_content" {
    const allocator = std.testing.allocator;
    const json_str =
        \\{
        \\  "role": "assistant",
        \\  "content": "Answer",
        \\  "reasoning_content": "Step 1: think..."
        \\}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_str, .{});
    defer parsed.deinit();

    const parser = MessageParser.init(allocator);
    var msg = try parser.parseOpenAI(parsed.value);
    defer msg.deinit(allocator);

    var found_thinking = false;
    for (msg.content) |blk| {
        if (blk == .thinking) {
            try std.testing.expectEqualStrings("Step 1: think...", blk.thinking.thinking);
            found_thinking = true;
        }
    }
    try std.testing.expect(found_thinking);
}

test "MessageParser.parseAnthropicContent" {
    const allocator = std.testing.allocator;
    const json_str =
        \\[
        \\  {"type":"text","text":"The answer is 42"},
        \\  {"type":"tool_use","id":"t1","name":"Read","input":{"file_path":"/tmp/x"}}
        \\]
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_str, .{});
    defer parsed.deinit();

    const parser = MessageParser.init(allocator);
    const blocks = try parser.parseAnthropicContent(parsed.value);
    defer {
        for (blocks) |*b| {
            var bc = b.*;
            bc.deinit(allocator);
        }
        allocator.free(blocks);
    }

    try std.testing.expectEqual(@as(usize, 2), blocks.len);
    try std.testing.expect(blocks[0] == .text);
    try std.testing.expect(blocks[1] == .tool_use);
    try std.testing.expectEqualStrings("Read", blocks[1].tool_use.name);
}

test "MessageBuilder.deinit (abort path)" {
    const allocator = std.testing.allocator;
    var builder = MessageBuilder.init(allocator, .user);
    try builder.addText("start");
    try builder.addText("more text");
    // Abort — call deinit instead of build
    builder.deinit();
    // Memory should be freed cleanly (leak detector will catch issues)
}

test "MessageBuilder.addImage" {
    const allocator = std.testing.allocator;
    var builder = MessageBuilder.init(allocator, .user);
    try builder.addImage("image/png", "abc123==");
    var msg = try builder.build();
    defer msg.deinit(allocator);

    try std.testing.expect(msg.content[0] == .image);
    try std.testing.expectEqualStrings("image/png", msg.content[0].image.media_type);
}
