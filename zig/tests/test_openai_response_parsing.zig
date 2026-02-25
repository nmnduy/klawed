//! tests/test_openai_response_parsing.zig — Zig port of tests/test_openai_response_parsing.c
//!
//! Tests OpenAI response parsing via providers/openai.zig:
//! - Valid text-only responses
//! - Valid tool-call responses (single and multiple)
//! - Mixed text + tool-call responses
//! - Edge cases: missing/empty choices, null content, invalid JSON
//! - reasoning_content support (Moonshot/Kimi thinking models)

const std = @import("std");
const openai = @import("../providers/openai.zig");

// Convenience: parse using the module-level deserializeResponse wrapper
// via OpenAIProvider.parseResponse.
fn parseWith(alloc: std.mem.Allocator, json_body: []const u8, mode: openai.ReasoningMode) !openai.Response {
    const provider = try openai.OpenAIProvider.init(alloc, "sk-test", "https://api.openai.com/v1/chat/completions", mode);
    var p = provider;
    defer p.deinit();
    return p.parseResponse(json_body);
}

// ---------------------------------------------------------------------------
// Edge cases — malformed / incomplete responses
// ---------------------------------------------------------------------------

test "openai response parsing: invalid JSON returns error" {
    const alloc = std.testing.allocator;
    const result = parseWith(alloc, "{this is not valid json", .none);
    // Zig 0.12 JSON parser returns SyntaxError for invalid JSON
    try std.testing.expect(std.meta.isError(result));
}

test "openai response parsing: missing choices returns empty content" {
    const alloc = std.testing.allocator;
    const json =
        \\{"message":{"role":"assistant","content":"No choices here"}}
    ;
    var resp = try parseWith(alloc, json, .none);
    defer resp.deinit();
    // No choices → empty content and no tool calls
    try std.testing.expectEqualStrings("", resp.content);
    try std.testing.expectEqual(@as(usize, 0), resp.tool_calls.len);
}

test "openai response parsing: empty choices array returns empty content" {
    const alloc = std.testing.allocator;
    const json =
        \\{"choices":[]}
    ;
    var resp = try parseWith(alloc, json, .none);
    defer resp.deinit();
    try std.testing.expectEqualStrings("", resp.content);
    try std.testing.expectEqual(@as(usize, 0), resp.tool_calls.len);
}

test "openai response parsing: missing message object returns empty content" {
    const alloc = std.testing.allocator;
    const json =
        \\{"choices":[{"status":"completed"}]}
    ;
    var resp = try parseWith(alloc, json, .none);
    defer resp.deinit();
    try std.testing.expectEqualStrings("", resp.content);
}

test "openai response parsing: null content returns empty content" {
    const alloc = std.testing.allocator;
    const json =
        \\{"choices":[{"message":{"role":"assistant","content":null}}]}
    ;
    var resp = try parseWith(alloc, json, .none);
    defer resp.deinit();
    try std.testing.expectEqualStrings("", resp.content);
    try std.testing.expectEqual(@as(usize, 0), resp.tool_calls.len);
}

// ---------------------------------------------------------------------------
// Valid parsing — text-only response
// ---------------------------------------------------------------------------

test "openai response parsing: valid text-only response" {
    const alloc = std.testing.allocator;
    const json =
        \\{"choices":[{"message":{"role":"assistant","content":"Hello, world!"}}]}
    ;
    var resp = try parseWith(alloc, json, .none);
    defer resp.deinit();
    try std.testing.expect(resp.content.len > 0);
    try std.testing.expect(std.mem.indexOf(u8, resp.content, "Hello") != null);
    try std.testing.expectEqual(@as(usize, 0), resp.tool_calls.len);
}

test "openai response parsing: text response with usage" {
    const alloc = std.testing.allocator;
    const json =
        \\{"id":"chatcmpl-abc","choices":[{"message":{"role":"assistant","content":"Hello!"}}],"usage":{"prompt_tokens":10,"completion_tokens":5,"total_tokens":15}}
    ;
    var resp = try parseWith(alloc, json, .none);
    defer resp.deinit();
    try std.testing.expect(std.mem.indexOf(u8, resp.content, "Hello") != null);
    try std.testing.expect(resp.usage != null);
    try std.testing.expectEqual(@as(u32, 10), resp.usage.?.prompt_tokens);
    try std.testing.expectEqual(@as(u32, 5), resp.usage.?.completion_tokens);
    try std.testing.expectEqual(@as(u32, 15), resp.usage.?.total_tokens);
}

// ---------------------------------------------------------------------------
// Valid parsing — tool-call response
// ---------------------------------------------------------------------------

test "openai response parsing: single tool call" {
    const alloc = std.testing.allocator;
    const json =
        \\{"choices":[{"message":{"role":"assistant","content":null,"tool_calls":[{"id":"call_123","type":"function","function":{"name":"Bash","arguments":"{\"command\":\"ls -la\"}"}}]}}]}
    ;
    var resp = try parseWith(alloc, json, .none);
    defer resp.deinit();
    try std.testing.expectEqual(@as(usize, 1), resp.tool_calls.len);
    try std.testing.expectEqualStrings("call_123", resp.tool_calls[0].id);
    try std.testing.expectEqualStrings("Bash", resp.tool_calls[0].name);
    try std.testing.expect(std.mem.indexOf(u8, resp.tool_calls[0].arguments, "ls -la") != null);
}

test "openai response parsing: multiple tool calls" {
    const alloc = std.testing.allocator;
    const json =
        \\{"choices":[{"message":{"role":"assistant","content":null,"tool_calls":[
        \\  {"id":"tc1","type":"function","function":{"name":"Read","arguments":"{}"}},
        \\  {"id":"tc2","type":"function","function":{"name":"Grep","arguments":"{\"pattern\":\"TODO\"}"}}
        \\]}}]}
    ;
    var resp = try parseWith(alloc, json, .none);
    defer resp.deinit();
    try std.testing.expectEqual(@as(usize, 2), resp.tool_calls.len);
    try std.testing.expectEqualStrings("tc1", resp.tool_calls[0].id);
    try std.testing.expectEqualStrings("tc2", resp.tool_calls[1].id);
    try std.testing.expectEqualStrings("Read", resp.tool_calls[0].name);
    try std.testing.expectEqualStrings("Grep", resp.tool_calls[1].name);
}

test "openai response parsing: text AND tool calls" {
    const alloc = std.testing.allocator;
    const json =
        \\{"choices":[{"message":{"role":"assistant","content":"Let me check that for you.","tool_calls":[{"id":"call_456","type":"function","function":{"name":"Bash","arguments":"{\"command\":\"pwd\"}"}}]}}]}
    ;
    var resp = try parseWith(alloc, json, .none);
    defer resp.deinit();
    try std.testing.expect(std.mem.indexOf(u8, resp.content, "check") != null);
    try std.testing.expectEqual(@as(usize, 1), resp.tool_calls.len);
    try std.testing.expectEqualStrings("call_456", resp.tool_calls[0].id);
}

// ---------------------------------------------------------------------------
// reasoning_content support (Moonshot/Kimi thinking models)
// ---------------------------------------------------------------------------

test "openai response parsing: reasoning_content with text (preserve mode)" {
    const alloc = std.testing.allocator;
    const json =
        \\{"choices":[{"message":{"role":"assistant","content":"The answer is 42.","reasoning_content":"Let me think step by step..."}}]}
    ;
    var resp = try parseWith(alloc, json, .preserve);
    defer resp.deinit();
    try std.testing.expect(std.mem.indexOf(u8, resp.content, "42") != null);
    try std.testing.expect(resp.reasoning_content != null);
    try std.testing.expect(std.mem.indexOf(u8, resp.reasoning_content.?, "step by step") != null);
}

test "openai response parsing: reasoning_content discarded in discard mode" {
    const alloc = std.testing.allocator;
    const json =
        \\{"choices":[{"message":{"role":"assistant","content":"The answer is 42.","reasoning_content":"Let me think step by step..."}}]}
    ;
    var resp = try parseWith(alloc, json, .discard);
    defer resp.deinit();
    try std.testing.expect(std.mem.indexOf(u8, resp.content, "42") != null);
    // In .discard mode, reasoning_content is not populated.
    try std.testing.expect(resp.reasoning_content == null);
}

test "openai response parsing: reasoning_content absent in none mode" {
    const alloc = std.testing.allocator;
    const json =
        \\{"choices":[{"message":{"role":"assistant","content":"The answer is 42.","reasoning_content":"Let me think step by step..."}}]}
    ;
    var resp = try parseWith(alloc, json, .none);
    defer resp.deinit();
    // In .none mode, reasoning_content is not populated.
    try std.testing.expect(resp.reasoning_content == null);
}

// ---------------------------------------------------------------------------
// deinit safety — double-free / null safety
// ---------------------------------------------------------------------------

test "openai response parsing: deinit is safe on empty response" {
    const alloc = std.testing.allocator;
    const json =
        \\{"choices":[]}
    ;
    var resp = try parseWith(alloc, json, .none);
    resp.deinit(); // Must not crash
}

test "openai response parsing: session loading scenario" {
    // Simulate parsing multiple sequential responses (like session_load_from_db).
    const alloc = std.testing.allocator;
    const responses = [_][]const u8{
        \\{"choices":[{"message":{"role":"assistant","content":"Step 1"}}]}
        ,
        \\{"choices":[{"message":{"role":"assistant","content":"Step 2"}}]}
        ,
    };

    for (responses) |json| {
        var resp = try parseWith(alloc, json, .none);
        defer resp.deinit();
        try std.testing.expect(resp.content.len > 0);
    }
}

// ---------------------------------------------------------------------------
// finish_reason field
// ---------------------------------------------------------------------------

test "openai response parsing: finish_reason is captured" {
    const alloc = std.testing.allocator;
    const json =
        \\{"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","content":null,"tool_calls":[{"id":"c1","type":"function","function":{"name":"Bash","arguments":"{}"}}]}}]}
    ;
    var resp = try parseWith(alloc, json, .none);
    defer resp.deinit();
    try std.testing.expectEqualStrings("tool_calls", resp.finish_reason);
}
