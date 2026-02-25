//! dump_utils.zig — Conversation state dump utilities
//!
//! Zig port of src/dump_utils.c.
//!
//! Provides functions for dumping conversation state to Markdown or JSON files
//! for debugging / export.  Mirrors the C API: `dumpConversationToFile`,
//! `dumpApiCallJson`, `dumpApiCallMarkdown`.

const std = @import("std");

// ---------------------------------------------------------------------------
// Output format types
// ---------------------------------------------------------------------------

pub const DumpFormat = enum {
    markdown,
    json,
};

// ---------------------------------------------------------------------------
// API call record (for dump_api_call_*)
// ---------------------------------------------------------------------------

pub const ApiCallRecord = struct {
    timestamp: ?[]const u8 = null,
    model: ?[]const u8 = null,
    status: ?[]const u8 = null,
    error_msg: ?[]const u8 = null,
    request_json: ?[]const u8 = null,
    response_json: ?[]const u8 = null,
    call_num: usize = 0,
};

// ---------------------------------------------------------------------------
// Message role tag for dump output
// ---------------------------------------------------------------------------

pub const DumpRole = enum {
    user,
    assistant,
    system,
    tool_call,
    tool_result,
    compaction,
    unknown,

    pub fn label(self: DumpRole) []const u8 {
        return switch (self) {
            .user => "USER",
            .assistant => "ASSISTANT",
            .system => "SYSTEM",
            .tool_call => "TOOL_CALL",
            .tool_result => "TOOL_RESULT",
            .compaction => "AUTO_COMPACTION",
            .unknown => "UNKNOWN",
        };
    }
};

// ---------------------------------------------------------------------------
// Dump message record
// ---------------------------------------------------------------------------

pub const DumpMessage = struct {
    role: DumpRole,
    text: ?[]const u8 = null,
    tool_name: ?[]const u8 = null,
    tool_id: ?[]const u8 = null,
};

// ---------------------------------------------------------------------------
// Dump conversation to Markdown
// ---------------------------------------------------------------------------

/// Write a conversation dump in Markdown format to `writer`.
pub fn dumpConversationMarkdown(
    messages: []const DumpMessage,
    session_id: ?[]const u8,
    timestamp: ?[]const u8,
    writer: anytype,
) !void {
    try writer.writeAll("# Conversation Dump\n\n");
    try writer.print("**Session ID:** {s}\n", .{session_id orelse "unknown"});
    try writer.print("**Timestamp:** {s}\n\n", .{timestamp orelse "unknown"});

    if (messages.len == 0) {
        try writer.writeAll("*No messages in conversation.*\n\n");
        return;
    }

    for (messages, 0..) |msg, i| {
        try writer.print("## Message {} - {s}\n\n", .{ i + 1, msg.role.label() });

        switch (msg.role) {
            .user, .assistant, .system, .compaction => {
                if (msg.text) |t| {
                    try writer.writeAll(t);
                    try writer.writeAll("\n\n");
                } else {
                    try writer.writeAll("*(empty)*\n\n");
                }
            },
            .tool_call => {
                try writer.print("**[TOOL CALL: {s}", .{msg.tool_name orelse "unknown"});
                if (msg.tool_id) |id| try writer.print(" (id: {s})", .{id});
                try writer.writeAll("]**\n\n");
            },
            .tool_result => {
                try writer.writeAll("**[TOOL RESULT");
                if (msg.tool_id) |id| try writer.print(" for {s}", .{id});
                try writer.writeAll("]**\n\n");
                if (msg.text) |t| {
                    try writer.writeAll(t);
                    try writer.writeAll("\n\n");
                }
            },
            .unknown => {
                try writer.writeAll("**[UNKNOWN CONTENT TYPE]**\n\n");
            },
        }

        try writer.writeAll("---\n\n");
    }
}

// ---------------------------------------------------------------------------
// Dump API call to Markdown
// ---------------------------------------------------------------------------

/// Write a single API call record in Markdown format to `writer`.
pub fn dumpApiCallMarkdown(record: ApiCallRecord, writer: anytype) !void {
    try writer.print("## Call {} - {s}\n\n", .{
        record.call_num,
        record.timestamp orelse "unknown",
    });
    try writer.print("**Model:** {s}  \n", .{record.model orelse "unknown"});
    try writer.print("**Status:** {s}  \n\n", .{record.status orelse "unknown"});

    if (record.error_msg) |e| {
        try writer.print("**Error:** {s}  \n\n", .{e});
    }

    if (record.request_json) |req| {
        try writer.writeAll("### Request\n\n");
        try writer.writeAll("```json\n");
        try writer.writeAll(req);
        try writer.writeAll("\n```\n\n");
    }

    if (record.response_json) |resp| {
        try writer.writeAll("### Response\n\n");
        try writer.writeAll("```json\n");
        try writer.writeAll(resp);
        try writer.writeAll("\n```\n\n");
    }

    try writer.writeAll("---\n\n");
}

// ---------------------------------------------------------------------------
// Dump API call to JSON
// ---------------------------------------------------------------------------

/// Write a single API call record as JSON to `writer`.
pub fn dumpApiCallJson(record: ApiCallRecord, writer: anytype) !void {
    // We produce a minimal JSON object without a full JSON library.
    // This mirrors the C cJSON_Print() output structure.
    try writer.writeAll("{\n");
    var first = true;

    if (record.timestamp) |t| {
        if (!first) try writer.writeAll(",\n");
        try writer.print("  \"timestamp\": \"{s}\"", .{t});
        first = false;
    }
    if (record.model) |m| {
        if (!first) try writer.writeAll(",\n");
        try writer.print("  \"model\": \"{s}\"", .{m});
        first = false;
    }
    if (record.status) |s| {
        if (!first) try writer.writeAll(",\n");
        try writer.print("  \"status\": \"{s}\"", .{s});
        first = false;
    }
    if (record.error_msg) |e| {
        if (!first) try writer.writeAll(",\n");
        try writer.print("  \"error_message\": \"{s}\"", .{e});
        first = false;
    }
    if (record.request_json) |req| {
        if (!first) try writer.writeAll(",\n");
        try writer.print("  \"request\": {s}", .{req});
        first = false;
    }
    if (record.response_json) |resp| {
        if (!first) try writer.writeAll(",\n");
        try writer.print("  \"response\": {s}", .{resp});
        // _ = first; // last field
    }

    try writer.writeAll("\n}\n");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "dumpConversationMarkdown: empty conversation" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    try dumpConversationMarkdown(&.{}, "ses-001", "2024-01-01 12:00:00", buf.writer());

    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "ses-001") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "No messages") != null);
}

test "dumpConversationMarkdown: user and assistant messages" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    const msgs = [_]DumpMessage{
        .{ .role = .user, .text = "Hello world" },
        .{ .role = .assistant, .text = "Hi there!" },
        .{ .role = .tool_call, .tool_name = "Bash", .tool_id = "tc1" },
        .{ .role = .tool_result, .tool_id = "tc1", .text = "exit 0" },
    };

    try dumpConversationMarkdown(&msgs, null, null, buf.writer());

    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "USER") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "ASSISTANT") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "Hello world") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "TOOL CALL: Bash") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "TOOL RESULT") != null);
}

test "dumpApiCallMarkdown: basic record" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    const record = ApiCallRecord{
        .call_num = 1,
        .timestamp = "2024-01-01 12:00:00",
        .model = "gpt-4o",
        .status = "success",
        .request_json = "{\"messages\":[]}",
    };

    try dumpApiCallMarkdown(record, buf.writer());
    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "gpt-4o") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "success") != null);
}

test "dumpApiCallJson: basic record" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    const record = ApiCallRecord{
        .call_num = 2,
        .model = "claude-3",
        .status = "error",
        .error_msg = "context too long",
    };

    try dumpApiCallJson(record, buf.writer());
    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "claude-3") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "context too long") != null);
}

test "DumpRole labels" {
    try std.testing.expectEqualStrings("USER", DumpRole.user.label());
    try std.testing.expectEqualStrings("ASSISTANT", DumpRole.assistant.label());
    try std.testing.expectEqualStrings("SYSTEM", DumpRole.system.label());
    try std.testing.expectEqualStrings("AUTO_COMPACTION", DumpRole.compaction.label());
}
