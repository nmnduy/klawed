//! tests/test_json_parsing.zig — Zig port of tests/test_json_parsing.c
//!
//! Tests the JSON parsing resilience pattern used throughout klawed:
//! - Valid JSON parses successfully
//! - Invalid JSON falls back gracefully (empty object / null handling)
//! - Nested field access with null safety
//! - std.json.parseFromSlice round-trip

const std = @import("std");

// ---------------------------------------------------------------------------
// Helpers — mirrors the C "fallback to empty object" pattern
// ---------------------------------------------------------------------------

/// Parse a JSON string; on failure return an empty object value.
/// This mirrors the C idiom: if (!cJSON_Parse(s)) result = cJSON_CreateObject()
fn parseOrEmpty(alloc: std.mem.Allocator, s: ?[]const u8) !std.json.Parsed(std.json.Value) {
    const input = s orelse return std.json.parseFromSlice(std.json.Value, alloc, "{}", .{});
    return std.json.parseFromSlice(std.json.Value, alloc, input, .{}) catch
        std.json.parseFromSlice(std.json.Value, alloc, "{}", .{});
}

// ---------------------------------------------------------------------------
// Test 1: Valid JSON parses successfully
// ---------------------------------------------------------------------------

test "json parsing: valid JSON object parses successfully" {
    const alloc = std.testing.allocator;
    const json = "{\"param1\": \"value1\", \"param2\": 42}";
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();

    try std.testing.expect(parsed.value == .object);
    const p1 = parsed.value.object.get("param1").?;
    try std.testing.expect(p1 == .string);
    try std.testing.expectEqualStrings("value1", p1.string);

    const p2 = parsed.value.object.get("param2").?;
    try std.testing.expect(p2 == .integer);
    try std.testing.expectEqual(@as(i64, 42), p2.integer);
}

// ---------------------------------------------------------------------------
// Test 2: Invalid JSON → fallback to empty object
// ---------------------------------------------------------------------------

test "json parsing: invalid JSON falls back to empty object" {
    const alloc = std.testing.allocator;
    // Missing closing brace → parse fails
    const invalid = "{\"param1\": \"value1\", \"param2\": 42";
    const result = std.json.parseFromSlice(std.json.Value, alloc, invalid, .{});
    try std.testing.expectError(error.UnexpectedEndOfInput, result);

    // Fallback pattern: use empty object
    const fallback = try std.json.parseFromSlice(std.json.Value, alloc, "{}", .{});
    defer fallback.deinit();
    try std.testing.expect(fallback.value == .object);
}

test "json parsing: parseOrEmpty handles invalid JSON" {
    const alloc = std.testing.allocator;
    const invalid = "{\"param1\": \"value1\", \"param2\": 42"; // missing }
    var result = try parseOrEmpty(alloc, invalid);
    defer result.deinit();

    // Should have gotten an empty object fallback
    try std.testing.expect(result.value == .object);
}

// ---------------------------------------------------------------------------
// Test 3: NULL/missing input → fallback to empty object
// ---------------------------------------------------------------------------

test "json parsing: null input falls back to empty object" {
    const alloc = std.testing.allocator;
    var result = try parseOrEmpty(alloc, null);
    defer result.deinit();

    try std.testing.expect(result.value == .object);
}

test "json parsing: empty string input fails gracefully" {
    const alloc = std.testing.allocator;
    const result = std.json.parseFromSlice(std.json.Value, alloc, "", .{});
    // Empty string is not valid JSON
    try std.testing.expect(result == error.UnexpectedEndOfInput or result == error.SyntaxError or
        @as(anyerror!std.json.Parsed(std.json.Value), result) == error.UnexpectedEndOfInput);
    _ = result catch {}; // consume the error
}

// ---------------------------------------------------------------------------
// Test 4: Nested object access with null safety
// ---------------------------------------------------------------------------

test "json parsing: nested field access" {
    const alloc = std.testing.allocator;
    const json =
        \\{"level1": {"level2": {"value": "deep"}}}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();

    const l1 = parsed.value.object.get("level1").?;
    try std.testing.expect(l1 == .object);
    const l2 = l1.object.get("level2").?;
    try std.testing.expect(l2 == .object);
    const val = l2.object.get("value").?;
    try std.testing.expectEqualStrings("deep", val.string);
}

test "json parsing: missing key returns null" {
    const alloc = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, "{}", .{});
    defer parsed.deinit();

    try std.testing.expect(parsed.value.object.get("nonexistent") == null);
}

// ---------------------------------------------------------------------------
// Test 5: Array parsing
// ---------------------------------------------------------------------------

test "json parsing: JSON array parsed correctly" {
    const alloc = std.testing.allocator;
    const json = "[1, 2, 3, \"four\"]";
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();

    try std.testing.expect(parsed.value == .array);
    try std.testing.expectEqual(@as(usize, 4), parsed.value.array.items.len);
    try std.testing.expectEqual(@as(i64, 1), parsed.value.array.items[0].integer);
    try std.testing.expectEqualStrings("four", parsed.value.array.items[3].string);
}

// ---------------------------------------------------------------------------
// Test 6: Null value in JSON
// ---------------------------------------------------------------------------

test "json parsing: JSON null value is parsed as .null" {
    const alloc = std.testing.allocator;
    const json = "{\"key\": null}";
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();

    const v = parsed.value.object.get("key").?;
    try std.testing.expect(v == .null);
}

// ---------------------------------------------------------------------------
// Test 7: Tool arguments pattern — parse or empty object fallback
// ---------------------------------------------------------------------------

test "json parsing: tool arguments pattern — valid args" {
    const alloc = std.testing.allocator;
    const valid_args = "{\"command\": \"ls -la\", \"timeout\": 30}";

    var result = try parseOrEmpty(alloc, valid_args);
    defer result.deinit();

    try std.testing.expect(result.value == .object);
    const cmd = result.value.object.get("command").?;
    try std.testing.expectEqualStrings("ls -la", cmd.string);
}

test "json parsing: tool arguments pattern — invalid args fall back to empty object" {
    const alloc = std.testing.allocator;
    const invalid_args = "{\"command\": \"ls -la\", \"timeout\""; // truncated

    var result = try parseOrEmpty(alloc, invalid_args);
    defer result.deinit();

    try std.testing.expect(result.value == .object);
    // Empty fallback — no "command" key
    try std.testing.expect(result.value.object.get("command") == null);
}

// ---------------------------------------------------------------------------
// Test 8: Round-trip serialization
// ---------------------------------------------------------------------------

test "json parsing: round-trip via stringify and re-parse" {
    const alloc = std.testing.allocator;
    const original_json = "{\"model\":\"gpt-4\",\"max_tokens\":1024}";
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, original_json, .{});
    defer parsed.deinit();

    // Re-serialize
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();
    try std.json.stringify(parsed.value, .{}, buf.writer());

    // Re-parse the serialized form
    const reparsed = try std.json.parseFromSlice(std.json.Value, alloc, buf.items, .{});
    defer reparsed.deinit();

    try std.testing.expect(reparsed.value == .object);
    try std.testing.expectEqualStrings("gpt-4", reparsed.value.object.get("model").?.string);
    try std.testing.expectEqual(@as(i64, 1024), reparsed.value.object.get("max_tokens").?.integer);
}

// ---------------------------------------------------------------------------
// Test 9: Integer type handling
// ---------------------------------------------------------------------------

test "json parsing: integer values are accessible" {
    const alloc = std.testing.allocator;
    const json = "{\"count\": 42, \"negative\": -5, \"zero\": 0}";
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();

    try std.testing.expectEqual(@as(i64, 42), parsed.value.object.get("count").?.integer);
    try std.testing.expectEqual(@as(i64, -5), parsed.value.object.get("negative").?.integer);
    try std.testing.expectEqual(@as(i64, 0), parsed.value.object.get("zero").?.integer);
}

// ---------------------------------------------------------------------------
// Test 10: Boolean values
// ---------------------------------------------------------------------------

test "json parsing: boolean values" {
    const alloc = std.testing.allocator;
    const json = "{\"flag_true\": true, \"flag_false\": false}";
    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, json, .{});
    defer parsed.deinit();

    const t = parsed.value.object.get("flag_true").?;
    const f = parsed.value.object.get("flag_false").?;
    try std.testing.expect(t == .bool and t.bool == true);
    try std.testing.expect(f == .bool and f.bool == false);
}
