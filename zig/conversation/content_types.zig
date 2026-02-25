//! conversation/content_types.zig — Vendor-agnostic content block types
//!
//! Zig port of src/conversation/content_types.c + src/klawed_internal.h (InternalContent).
//!
//! The key improvement over the C version is replacing the stringly-typed
//! `InternalContentType` enum + flat struct with a proper `union(enum)` that
//! makes impossible states unrepresentable at compile time.
//!
//! ## Content block variants
//!
//! | Variant       | C equivalent              | Description                          |
//! |---------------|---------------------------|--------------------------------------|
//! | `text`        | `INTERNAL_TEXT`           | Plain text content                   |
//! | `tool_use`    | `INTERNAL_TOOL_CALL`      | Agent requesting a tool execution    |
//! | `tool_result` | `INTERNAL_TOOL_RESPONSE`  | Result returned from tool execution  |
//! | `image`       | `INTERNAL_IMAGE`          | Base64-encoded image data            |
//! | `thinking`    | (reasoning_content field) | Extended thinking/reasoning block    |
//!
//! ## JSON serialization
//!
//! Each variant has `toOpenAIJson` and `toAnthropicJson` helpers that write
//! the provider-specific representation to a `std.json.WriteStream`.

const std = @import("std");

// ---------------------------------------------------------------------------
// CacheControl
// ---------------------------------------------------------------------------

/// Prompt-caching marker, used with the `text` variant when prompt caching
/// is enabled.  Anthropic requires `{"type":"ephemeral"}` on the last content
/// block of the system prompt to mark a cache breakpoint.
pub const CacheControl = struct {
    type: []const u8 = "ephemeral",

    pub fn writeJson(self: CacheControl, jw: anytype) !void {
        try jw.beginObject();
        try jw.objectField("type");
        try jw.write(self.type);
        try jw.endObject();
    }
};

// ---------------------------------------------------------------------------
// ContentBlock — the central type
// ---------------------------------------------------------------------------

/// A single content block in a conversation message.
///
/// Replaces the flat `InternalContent` C struct with a tagged union,
/// eliminating the need for callers to check `.type` before accessing fields.
pub const ContentBlock = union(enum) {
    text: TextBlock,
    tool_use: ToolUseBlock,
    tool_result: ToolResultBlock,
    image: ImageBlock,
    thinking: ThinkingBlock,

    // -----------------------------------------------------------------------
    // Variant payload types
    // -----------------------------------------------------------------------

    /// Plain text content (INTERNAL_TEXT).
    /// Owns its `text` slice.
    pub const TextBlock = struct {
        text: []const u8,
        cache_control: ?CacheControl = null,
    };

    /// A tool call initiated by the assistant (INTERNAL_TOOL_CALL).
    /// `arguments_json` is a JSON-encoded string of the tool's parameters.
    pub const ToolUseBlock = struct {
        id: []const u8,
        name: []const u8,
        /// JSON-encoded arguments string (e.g. `{"command":"ls"}`).
        arguments_json: []const u8,
    };

    /// The result returned by a tool execution (INTERNAL_TOOL_RESPONSE).
    pub const ToolResultBlock = struct {
        tool_use_id: []const u8,
        content: []const u8,
        is_error: bool = false,
    };

    /// A base64-encoded image (INTERNAL_IMAGE).
    pub const ImageBlock = struct {
        media_type: []const u8, // "image/png", "image/jpeg", etc.
        data: []const u8,       // base64-encoded bytes
    };

    /// Extended reasoning/thinking from thinking models (DeepSeek, Moonshot/Kimi).
    pub const ThinkingBlock = struct {
        thinking: []const u8,
        signature: []const u8 = "",
    };

    // -----------------------------------------------------------------------
    // Tag helpers
    // -----------------------------------------------------------------------

    /// Return the type tag string used by the Anthropic API.
    pub fn anthropicType(self: ContentBlock) []const u8 {
        return switch (self) {
            .text => "text",
            .tool_use => "tool_use",
            .tool_result => "tool_result",
            .image => "image",
            .thinking => "thinking",
        };
    }

    /// Return the type tag string used by the OpenAI API.
    pub fn openaiType(self: ContentBlock) []const u8 {
        return switch (self) {
            .text => "text",
            .tool_use => "tool_calls",      // OpenAI wraps these differently
            .tool_result => "tool",
            .image => "image_url",
            .thinking => "text",            // expose as text in OpenAI format
        };
    }

    // -----------------------------------------------------------------------
    // JSON serialization helpers
    // -----------------------------------------------------------------------

    /// Write this content block to `jw` in Anthropic API format.
    pub fn writeAnthropicJson(self: ContentBlock, jw: anytype) !void {
        try jw.beginObject();
        try jw.objectField("type");
        try jw.write(self.anthropicType());

        switch (self) {
            .text => |blk| {
                try jw.objectField("text");
                try jw.write(blk.text);
                if (blk.cache_control) |cc| {
                    try jw.objectField("cache_control");
                    try cc.writeJson(jw);
                }
            },
            .tool_use => |blk| {
                try jw.objectField("id");
                try jw.write(blk.id);
                try jw.objectField("name");
                try jw.write(blk.name);
                try jw.objectField("input");
                // arguments_json is raw JSON — inject without re-encoding
                try jw.print("{s}", .{blk.arguments_json});
            },
            .tool_result => |blk| {
                try jw.objectField("tool_use_id");
                try jw.write(blk.tool_use_id);
                try jw.objectField("content");
                try jw.write(blk.content);
                if (blk.is_error) {
                    try jw.objectField("is_error");
                    try jw.write(true);
                }
            },
            .image => |blk| {
                try jw.objectField("source");
                try jw.beginObject();
                try jw.objectField("type");
                try jw.write("base64");
                try jw.objectField("media_type");
                try jw.write(blk.media_type);
                try jw.objectField("data");
                try jw.write(blk.data);
                try jw.endObject();
            },
            .thinking => |blk| {
                try jw.objectField("thinking");
                try jw.write(blk.thinking);
                if (blk.signature.len > 0) {
                    try jw.objectField("signature");
                    try jw.write(blk.signature);
                }
            },
        }

        try jw.endObject();
    }

    /// Write this content block to `jw` in OpenAI API format.
    ///
    /// Note: OpenAI tool_use blocks must be written as a top-level
    /// `tool_calls` array, not inline in the content array.  Callers
    /// that need to handle this special case should check `self == .tool_use`
    /// before writing.
    pub fn writeOpenAIJson(self: ContentBlock, jw: anytype) !void {
        try jw.beginObject();

        switch (self) {
            .text => |blk| {
                try jw.objectField("type");
                try jw.write("text");
                try jw.objectField("text");
                try jw.write(blk.text);
            },
            .tool_use => |blk| {
                // Individual tool call entry within tool_calls array
                try jw.objectField("id");
                try jw.write(blk.id);
                try jw.objectField("type");
                try jw.write("function");
                try jw.objectField("function");
                try jw.beginObject();
                try jw.objectField("name");
                try jw.write(blk.name);
                try jw.objectField("arguments");
                try jw.write(blk.arguments_json);
                try jw.endObject();
            },
            .tool_result => |blk| {
                try jw.objectField("type");
                try jw.write("tool_result");
                try jw.objectField("tool_call_id");
                try jw.write(blk.tool_use_id);
                try jw.objectField("content");
                try jw.write(blk.content);
            },
            .image => |blk| {
                try jw.objectField("type");
                try jw.write("image_url");
                try jw.objectField("image_url");
                try jw.beginObject();
                try jw.objectField("url");
                const data_url = try std.fmt.allocPrint(
                    std.heap.page_allocator,
                    "data:{s};base64,{s}",
                    .{ blk.media_type, blk.data },
                );
                defer std.heap.page_allocator.free(data_url);
                try jw.write(data_url);
                try jw.endObject();
            },
            .thinking => |blk| {
                // Expose thinking as plain text in OpenAI format
                try jw.objectField("type");
                try jw.write("text");
                try jw.objectField("text");
                try jw.write(blk.thinking);
            },
        }

        try jw.endObject();
    }

    // -----------------------------------------------------------------------
    // Memory management
    // -----------------------------------------------------------------------

    /// Free all heap-allocated strings owned by this content block.
    pub fn deinit(self: *ContentBlock, allocator: std.mem.Allocator) void {
        switch (self.*) {
            .text => |blk| allocator.free(blk.text),
            .tool_use => |blk| {
                allocator.free(blk.id);
                allocator.free(blk.name);
                allocator.free(blk.arguments_json);
            },
            .tool_result => |blk| {
                allocator.free(blk.tool_use_id);
                allocator.free(blk.content);
            },
            .image => |blk| {
                allocator.free(blk.media_type);
                allocator.free(blk.data);
            },
            .thinking => |blk| {
                allocator.free(blk.thinking);
                if (blk.signature.len > 0) allocator.free(blk.signature);
            },
        }
    }

    /// Deep-clone this content block using the given allocator.
    pub fn dupe(self: ContentBlock, allocator: std.mem.Allocator) !ContentBlock {
        return switch (self) {
            .text => |blk| ContentBlock{
                .text = .{
                    .text = try allocator.dupe(u8, blk.text),
                    .cache_control = blk.cache_control,
                },
            },
            .tool_use => |blk| ContentBlock{
                .tool_use = .{
                    .id = try allocator.dupe(u8, blk.id),
                    .name = try allocator.dupe(u8, blk.name),
                    .arguments_json = try allocator.dupe(u8, blk.arguments_json),
                },
            },
            .tool_result => |blk| ContentBlock{
                .tool_result = .{
                    .tool_use_id = try allocator.dupe(u8, blk.tool_use_id),
                    .content = try allocator.dupe(u8, blk.content),
                    .is_error = blk.is_error,
                },
            },
            .image => |blk| ContentBlock{
                .image = .{
                    .media_type = try allocator.dupe(u8, blk.media_type),
                    .data = try allocator.dupe(u8, blk.data),
                },
            },
            .thinking => |blk| ContentBlock{
                .thinking = .{
                    .thinking = try allocator.dupe(u8, blk.thinking),
                    .signature = try allocator.dupe(u8, blk.signature),
                },
            },
        };
    }
};

// ---------------------------------------------------------------------------
// Role
// ---------------------------------------------------------------------------

/// Role of a conversation message.
pub const Role = enum {
    user,
    assistant,
    system,
    /// Auto-compaction notice injected by the compaction subsystem.
    auto_compaction,

    pub fn toString(self: Role) []const u8 {
        return switch (self) {
            .user => "user",
            .assistant => "assistant",
            .system => "system",
            .auto_compaction => "user", // sent as user message to API
        };
    }
};

// ---------------------------------------------------------------------------
// Helpers — check for TodoWrite in a results slice (mirrors C `check_todo_write_executed`)
// ---------------------------------------------------------------------------

/// Returns `true` if any block in `results` is a `tool_use` block for `TodoWrite`.
/// Mirrors the C function `check_todo_write_executed`.
pub fn checkTodoWriteExecuted(results: []const ContentBlock) bool {
    for (results) |blk| {
        if (blk == .tool_use) {
            if (std.mem.eql(u8, blk.tool_use.name, "TodoWrite")) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "ContentBlock.text round-trip Anthropic JSON" {
    const allocator = std.testing.allocator;

    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();
    var jw = std.json.writeStream(buf.writer(), .{});

    const blk = ContentBlock{ .text = .{ .text = "hello world" } };
    try blk.writeAnthropicJson(&jw);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, buf.items, .{});
    defer parsed.deinit();

    try std.testing.expectEqualStrings("text", parsed.value.object.get("type").?.string);
    try std.testing.expectEqualStrings("hello world", parsed.value.object.get("text").?.string);
}

test "ContentBlock.tool_use round-trip OpenAI JSON" {
    const allocator = std.testing.allocator;

    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();
    var jw = std.json.writeStream(buf.writer(), .{});

    const blk = ContentBlock{ .tool_use = .{
        .id = "call_001",
        .name = "Bash",
        .arguments_json = "{\"command\":\"ls\"}",
    } };
    try blk.writeOpenAIJson(&jw);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, buf.items, .{});
    defer parsed.deinit();

    try std.testing.expectEqualStrings("call_001", parsed.value.object.get("id").?.string);
    const fn_obj = parsed.value.object.get("function").?;
    try std.testing.expectEqualStrings("Bash", fn_obj.object.get("name").?.string);
}

test "ContentBlock.tool_result Anthropic JSON" {
    const allocator = std.testing.allocator;

    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();
    var jw = std.json.writeStream(buf.writer(), .{});

    const blk = ContentBlock{ .tool_result = .{
        .tool_use_id = "call_001",
        .content = "file not found",
        .is_error = true,
    } };
    try blk.writeAnthropicJson(&jw);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, buf.items, .{});
    defer parsed.deinit();

    try std.testing.expectEqualStrings("tool_result", parsed.value.object.get("type").?.string);
    try std.testing.expect(parsed.value.object.get("is_error").?.bool);
}

test "ContentBlock.dupe and deinit" {
    const allocator = std.testing.allocator;

    const original = ContentBlock{ .tool_use = .{
        .id = "call_abc",
        .name = "Read",
        .arguments_json = "{\"file_path\":\"/tmp/x\"}",
    } };

    var copy = try original.dupe(allocator);
    defer copy.deinit(allocator);

    try std.testing.expectEqualStrings("call_abc", copy.tool_use.id);
    try std.testing.expectEqualStrings("Read", copy.tool_use.name);
}

test "checkTodoWriteExecuted" {
    const blocks = [_]ContentBlock{
        ContentBlock{ .text = .{ .text = "done" } },
        ContentBlock{ .tool_use = .{
            .id = "t1",
            .name = "TodoWrite",
            .arguments_json = "{}",
        } },
    };
    try std.testing.expect(checkTodoWriteExecuted(&blocks));

    const no_todo = [_]ContentBlock{
        ContentBlock{ .text = .{ .text = "nothing" } },
    };
    try std.testing.expect(!checkTodoWriteExecuted(&no_todo));
}

test "ContentBlock.image Anthropic JSON" {
    const allocator = std.testing.allocator;

    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();
    var jw = std.json.writeStream(buf.writer(), .{});

    const blk = ContentBlock{ .image = .{
        .media_type = "image/png",
        .data = "abc123base64==",
    } };
    try blk.writeAnthropicJson(&jw);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, buf.items, .{});
    defer parsed.deinit();

    try std.testing.expectEqualStrings("image", parsed.value.object.get("type").?.string);
    const source = parsed.value.object.get("source").?;
    try std.testing.expectEqualStrings("base64", source.object.get("type").?.string);
    try std.testing.expectEqualStrings("image/png", source.object.get("media_type").?.string);
}

test "CacheControl JSON" {
    const allocator = std.testing.allocator;

    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();
    var jw = std.json.writeStream(buf.writer(), .{});

    const cc = CacheControl{};
    try cc.writeJson(&jw);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, buf.items, .{});
    defer parsed.deinit();
    try std.testing.expectEqualStrings("ephemeral", parsed.value.object.get("type").?.string);
}

test "Role.toString" {
    try std.testing.expectEqualStrings("user", Role.user.toString());
    try std.testing.expectEqualStrings("assistant", Role.assistant.toString());
    try std.testing.expectEqualStrings("system", Role.system.toString());
    // auto_compaction is sent as "user" to the API
    try std.testing.expectEqualStrings("user", Role.auto_compaction.toString());
}
