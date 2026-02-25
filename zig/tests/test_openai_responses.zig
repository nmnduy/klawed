//! tests/test_openai_responses.zig — Zig port of tests/test_openai_responses.c
//!
//! Tests parsing of the OpenAI /v1/responses endpoint format, including:
//! - Text-only responses (message items with output_text content)
//! - Tool call responses (function_call items in output array)
//! - Mixed responses (both text and tool calls)
//! - Reasoning items (should be ignored)
//!
//! The C test implemented its own parser over cJSON; here we replicate the
//! parsing logic using std.json and verify the same invariants.

const std = @import("std");

// ---------------------------------------------------------------------------
// Parsed result type (mirrors C's ParsedResponse)
// ---------------------------------------------------------------------------

const ParsedResponse = struct {
    allocator: std.mem.Allocator,
    text: ?[]u8,
    tool_ids: [][]u8,
    tool_names: [][]u8,
    tool_arguments: [][]u8,
    tool_count: usize,

    fn deinit(self: *ParsedResponse) void {
        const a = self.allocator;
        if (self.text) |t| a.free(t);
        for (0..self.tool_count) |i| {
            a.free(self.tool_ids[i]);
            a.free(self.tool_names[i]);
            a.free(self.tool_arguments[i]);
        }
        a.free(self.tool_ids);
        a.free(self.tool_names);
        a.free(self.tool_arguments);
    }
};

// ---------------------------------------------------------------------------
// Parser — extract text and function_call items from a Responses API output
// ---------------------------------------------------------------------------

fn parseResponsesApi(alloc: std.mem.Allocator, json_str: []const u8) !ParsedResponse {
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json_str, .{});
    defer parsed.deinit();

    const root = parsed.value;
    if (root != .object) return error.InvalidFormat;

    const output_v = root.object.get("output") orelse return ParsedResponse{
        .allocator = alloc,
        .text = null,
        .tool_ids = try alloc.alloc([]u8, 0),
        .tool_names = try alloc.alloc([]u8, 0),
        .tool_arguments = try alloc.alloc([]u8, 0),
        .tool_count = 0,
    };
    if (output_v != .array) return error.InvalidFormat;
    const output = output_v.array.items;

    // --- Extract text from message items ---
    var text_buf = std.ArrayList(u8).init(alloc);
    defer text_buf.deinit();

    for (output) |item| {
        if (item != .object) continue;
        const type_v = item.object.get("type") orelse continue;
        if (type_v != .string) continue;
        if (!std.mem.eql(u8, type_v.string, "message")) continue;

        // Extract output_text content blocks
        const content_v = item.object.get("content") orelse continue;
        if (content_v != .array) continue;
        for (content_v.array.items) |cblk| {
            if (cblk != .object) continue;
            const ct = cblk.object.get("type") orelse continue;
            if (ct != .string or !std.mem.eql(u8, ct.string, "output_text")) continue;
            const tv = cblk.object.get("text") orelse continue;
            if (tv != .string) continue;
            try text_buf.appendSlice(tv.string);
        }
    }

    // --- Count and extract function_call items ---
    var tool_count: usize = 0;
    for (output) |item| {
        if (item != .object) continue;
        const type_v = item.object.get("type") orelse continue;
        if (type_v == .string and std.mem.eql(u8, type_v.string, "function_call"))
            tool_count += 1;
    }

    var ids = try alloc.alloc([]u8, tool_count);
    errdefer alloc.free(ids);
    var names = try alloc.alloc([]u8, tool_count);
    errdefer alloc.free(names);
    var args = try alloc.alloc([]u8, tool_count);
    errdefer alloc.free(args);

    var idx: usize = 0;
    for (output) |item| {
        if (item != .object) continue;
        const type_v = item.object.get("type") orelse continue;
        if (type_v != .string or !std.mem.eql(u8, type_v.string, "function_call")) continue;

        const id_s = if (item.object.get("call_id")) |v| if (v == .string) v.string else "" else "";
        const nm_s = if (item.object.get("name")) |v| if (v == .string) v.string else "" else "";
        const ar_s = if (item.object.get("arguments")) |v| if (v == .string) v.string else "{}" else "{}";

        ids[idx] = try alloc.dupe(u8, id_s);
        names[idx] = try alloc.dupe(u8, nm_s);
        args[idx] = try alloc.dupe(u8, ar_s);
        idx += 1;
    }

    return ParsedResponse{
        .allocator = alloc,
        .text = if (text_buf.items.len > 0) try text_buf.toOwnedSlice() else null,
        .tool_ids = ids,
        .tool_names = names,
        .tool_arguments = args,
        .tool_count = tool_count,
    };
}

// ---------------------------------------------------------------------------
// Test fixtures (matching the C test JSON strings)
// ---------------------------------------------------------------------------

const response_text_only =
    \\{"id":"resp_text_only","object":"response","status":"completed",
    \\"output":[{"id":"msg_001","type":"message","status":"completed",
    \\"content":[{"type":"output_text","text":"Hello! How can I help you today?"}],
    \\"role":"assistant"}]}
;

const response_tool_call_only =
    \\{"id":"resp_tool_only","object":"response","status":"completed",
    \\"output":[{"id":"rs_001","type":"reasoning","summary":[]},
    \\{"id":"fc_001","type":"function_call","status":"completed",
    \\"arguments":"{\"command\":\"ls -la\"}","call_id":"call_001","name":"Bash"}]}
;

const response_text_and_tools =
    \\{"id":"resp_mixed","object":"response","status":"completed",
    \\"output":[{"id":"msg_002","type":"message","status":"completed",
    \\"content":[{"type":"output_text","text":"Let me check the directory for you:"}],
    \\"role":"assistant"},
    \\{"id":"fc_002","type":"function_call","status":"completed",
    \\"arguments":"{\"command\":\"ls -la\",\"timeout\":30}","call_id":"call_002","name":"Bash"}]}
;

const response_multiple_tools =
    \\{"id":"resp_multi_tool","object":"response","status":"completed",
    \\"output":[
    \\{"id":"fc_003","type":"function_call","status":"completed","arguments":"{}","call_id":"call_tool1","name":"Read"},
    \\{"id":"fc_004","type":"function_call","status":"completed","arguments":"{\"pattern\":\"*.c\"}","call_id":"call_tool2","name":"Glob"},
    \\{"id":"fc_005","type":"function_call","status":"completed","arguments":"{\"pattern\":\"TODO\"}","call_id":"call_tool3","name":"Grep"}]}
;

const response_null_content =
    \\{"id":"resp_null_content","object":"response","status":"completed",
    \\"output":[{"id":"msg_004","type":"message","status":"completed",
    \\"content":null,"role":"assistant"}]}
;

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "openai responses: text-only response" {
    const alloc = std.testing.allocator;
    var resp = try parseResponsesApi(alloc, response_text_only);
    defer resp.deinit();

    try std.testing.expect(resp.text != null);
    try std.testing.expect(std.mem.indexOf(u8, resp.text.?, "Hello!") != null);
    try std.testing.expectEqual(@as(usize, 0), resp.tool_count);
}

test "openai responses: tool-call-only response (reasoning item ignored)" {
    const alloc = std.testing.allocator;
    var resp = try parseResponsesApi(alloc, response_tool_call_only);
    defer resp.deinit();

    try std.testing.expect(resp.text == null);
    try std.testing.expectEqual(@as(usize, 1), resp.tool_count);
    try std.testing.expectEqualStrings("Bash", resp.tool_names[0]);
    try std.testing.expect(std.mem.indexOf(u8, resp.tool_arguments[0], "ls -la") != null);
    try std.testing.expectEqualStrings("call_001", resp.tool_ids[0]);
}

test "openai responses: text and tools mixed response" {
    const alloc = std.testing.allocator;
    var resp = try parseResponsesApi(alloc, response_text_and_tools);
    defer resp.deinit();

    try std.testing.expect(resp.text != null);
    try std.testing.expect(std.mem.indexOf(u8, resp.text.?, "directory") != null);
    try std.testing.expectEqual(@as(usize, 1), resp.tool_count);
    try std.testing.expectEqualStrings("Bash", resp.tool_names[0]);
    try std.testing.expect(std.mem.indexOf(u8, resp.tool_arguments[0], "timeout") != null);
}

test "openai responses: multiple tool calls" {
    const alloc = std.testing.allocator;
    var resp = try parseResponsesApi(alloc, response_multiple_tools);
    defer resp.deinit();

    try std.testing.expect(resp.text == null);
    try std.testing.expectEqual(@as(usize, 3), resp.tool_count);
    try std.testing.expectEqualStrings("Read", resp.tool_names[0]);
    try std.testing.expectEqualStrings("Glob", resp.tool_names[1]);
    try std.testing.expectEqualStrings("Grep", resp.tool_names[2]);
}

test "openai responses: null content in message" {
    const alloc = std.testing.allocator;
    var resp = try parseResponsesApi(alloc, response_null_content);
    defer resp.deinit();

    try std.testing.expect(resp.text == null);
    try std.testing.expectEqual(@as(usize, 0), resp.tool_count);
}

test "openai responses: db response with reasoning item" {
    const alloc = std.testing.allocator;
    const db_response =
        \\{"id":"resp_from_db","object":"response","status":"completed",
        \\"output":[{"id":"rs_reasoning","type":"reasoning","summary":[]},
        \\{"id":"fc_db","type":"function_call","status":"completed",
        \\"arguments":"{\"command\":\"git status --short --branch\",\"timeout\":120}",
        \\"call_id":"call_git","name":"Bash"}]}
    ;
    var resp = try parseResponsesApi(alloc, db_response);
    defer resp.deinit();

    try std.testing.expect(resp.text == null);
    try std.testing.expectEqual(@as(usize, 1), resp.tool_count);
    try std.testing.expectEqualStrings("Bash", resp.tool_names[0]);
    try std.testing.expect(std.mem.indexOf(u8, resp.tool_arguments[0], "git status") != null);
    try std.testing.expect(std.mem.indexOf(u8, resp.tool_arguments[0], "timeout") != null);
    try std.testing.expectEqualStrings("call_git", resp.tool_ids[0]);
}

test "openai responses: empty output array" {
    const alloc = std.testing.allocator;
    const json =
        \\{"id":"empty","object":"response","status":"completed","output":[]}
    ;
    var resp = try parseResponsesApi(alloc, json);
    defer resp.deinit();

    try std.testing.expect(resp.text == null);
    try std.testing.expectEqual(@as(usize, 0), resp.tool_count);
}

test "openai responses: reasoning items are skipped" {
    // A response containing only a reasoning item should produce no text and no tools.
    const alloc = std.testing.allocator;
    const json =
        \\{"id":"only_reasoning","object":"response","status":"completed",
        \\"output":[{"id":"rs_01","type":"reasoning","summary":[{"type":"summary_text","text":"thinking..."}]}]}
    ;
    var resp = try parseResponsesApi(alloc, json);
    defer resp.deinit();

    try std.testing.expect(resp.text == null);
    try std.testing.expectEqual(@as(usize, 0), resp.tool_count);
}
