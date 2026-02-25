//! tests/test_dump_utils.zig — Zig port of tests/test_dump_utils.c
//!
//! Tests dump_utils.zig: dumpConversationMarkdown, dumpApiCallMarkdown,
//! dumpApiCallJson, DumpRole labels.

const std = @import("std");
const dump = @import("../dump_utils.zig");

const DumpMessage = dump.DumpMessage;
const DumpRole = dump.DumpRole;
const ApiCallRecord = dump.ApiCallRecord;

// ---------------------------------------------------------------------------
// DumpRole labels
// ---------------------------------------------------------------------------

test "DumpRole labels: correct strings" {
    try std.testing.expectEqualStrings("USER", DumpRole.user.label());
    try std.testing.expectEqualStrings("ASSISTANT", DumpRole.assistant.label());
    try std.testing.expectEqualStrings("SYSTEM", DumpRole.system.label());
    try std.testing.expectEqualStrings("TOOL_CALL", DumpRole.tool_call.label());
    try std.testing.expectEqualStrings("TOOL_RESULT", DumpRole.tool_result.label());
    try std.testing.expectEqualStrings("AUTO_COMPACTION", DumpRole.compaction.label());
    try std.testing.expectEqualStrings("UNKNOWN", DumpRole.unknown.label());
}

// ---------------------------------------------------------------------------
// dumpConversationMarkdown
// ---------------------------------------------------------------------------

test "dumpConversationMarkdown: empty conversation" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    try dump.dumpConversationMarkdown(&.{}, "ses-001", "2024-01-01 12:00:00", buf.writer());

    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "ses-001") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "No messages") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "Conversation Dump") != null);
}

test "dumpConversationMarkdown: user and assistant messages" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    const msgs = [_]DumpMessage{
        .{ .role = .user, .text = "Hello world" },
        .{ .role = .assistant, .text = "Hi there!" },
    };

    try dump.dumpConversationMarkdown(&msgs, null, null, buf.writer());

    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "USER") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "ASSISTANT") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "Hello world") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "Hi there!") != null);
}

test "dumpConversationMarkdown: tool call and result" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    const msgs = [_]DumpMessage{
        .{ .role = .tool_call, .tool_name = "Bash", .tool_id = "tc1" },
        .{ .role = .tool_result, .tool_id = "tc1", .text = "exit 0" },
    };

    try dump.dumpConversationMarkdown(&msgs, null, null, buf.writer());

    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "TOOL CALL: Bash") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "tc1") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "TOOL RESULT") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "exit 0") != null);
}

test "dumpConversationMarkdown: session ID and timestamp appear in header" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    try dump.dumpConversationMarkdown(&.{}, "session-xyz", "2026-01-01 00:00:00", buf.writer());

    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "session-xyz") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "2026-01-01 00:00:00") != null);
}

// ---------------------------------------------------------------------------
// dumpApiCallMarkdown
// ---------------------------------------------------------------------------

test "dumpApiCallMarkdown: basic record fields present" {
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

    try dump.dumpApiCallMarkdown(record, buf.writer());
    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "gpt-4o") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "success") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "2024-01-01 12:00:00") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "{\"messages\":[]}") != null);
}

test "dumpApiCallMarkdown: with error message" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    const record = ApiCallRecord{
        .call_num = 2,
        .model = "claude-3",
        .status = "error",
        .error_msg = "Rate limit exceeded",
    };

    try dump.dumpApiCallMarkdown(record, buf.writer());
    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "Rate limit exceeded") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "error") != null);
}

// ---------------------------------------------------------------------------
// dumpApiCallJson
// ---------------------------------------------------------------------------

test "dumpApiCallJson: basic record produces valid-looking JSON" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    const record = ApiCallRecord{
        .call_num = 1,
        .timestamp = "2026-01-07 10:30:00",
        .model = "claude-3-opus",
        .status = "success",
        .request_json = "{\"messages\":[{\"role\":\"user\",\"content\":\"Hello\"}]}",
        .response_json = "{\"content\":[{\"type\":\"text\",\"text\":\"Hi there!\"}]}",
    };

    try dump.dumpApiCallJson(record, buf.writer());
    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "\"timestamp\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "2026-01-07 10:30:00") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "\"model\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "claude-3-opus") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "\"status\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "success") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "\"request\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "\"response\"") != null);
}

test "dumpApiCallJson: with error message" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    const record = ApiCallRecord{
        .call_num = 1,
        .model = "gpt-4",
        .status = "error",
        .error_msg = "Rate limit exceeded",
    };

    try dump.dumpApiCallJson(record, buf.writer());
    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "Rate limit exceeded") != null);
}

test "dumpApiCallJson: starts and ends with braces" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    const record = ApiCallRecord{ .call_num = 1, .model = "test-model" };

    try dump.dumpApiCallJson(record, buf.writer());
    const output = buf.items;
    try std.testing.expect(output.len > 0);
    try std.testing.expect(output[0] == '{');
    // Last non-whitespace char should be '}'
    var last: usize = output.len;
    while (last > 0 and (output[last - 1] == '\n' or output[last - 1] == ' ')) last -= 1;
    try std.testing.expect(last > 0 and output[last - 1] == '}');
}
