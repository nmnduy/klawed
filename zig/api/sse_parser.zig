//! api/sse_parser.zig — Server-Sent Events (SSE) line parser
//!
//! Parses raw SSE text (as received from Anthropic or OpenAI streaming
//! endpoints) into typed `StreamEvent` values.
//!
//! ## SSE wire format (RFC 8607-ish)
//! ```
//! event: content_block_delta
//! data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"Hi"}}
//!
//! data: [DONE]
//! ```
//!
//! Each *event* consists of one or more field lines followed by a blank line.
//! This module processes **individual lines** through `Parser.feedLine()` and
//! fires `onEvent` once a blank line terminates an event.
//!
//! ## Provider support
//! - **Anthropic**: explicit `event:` field maps to `StreamEventKind`
//! - **OpenAI**: no `event:` field; events are classified as `openai_chunk`
//!   or `openai_done` based on the `data:` value

const std = @import("std");

// ---------------------------------------------------------------------------
// StreamEvent types
// ---------------------------------------------------------------------------

pub const StreamEventKind = enum {
    // Anthropic Messages API events
    message_start,
    content_block_start,
    content_block_delta,
    content_block_stop,
    message_delta,
    message_stop,
    error_event,
    ping,

    // OpenAI Chat Completions API events
    openai_chunk,
    openai_done,

    // Fallback
    unknown,
};

/// A parsed SSE event.
///
/// All slice fields point into memory owned by the `Parser`'s arena; they are
/// valid until the next `feedLine` / `reset` call or `deinit`.
pub const StreamEvent = struct {
    kind: StreamEventKind,
    /// Raw event name from the `event:` field (empty string if absent).
    event_name: []const u8,
    /// Raw `data:` payload (empty string if absent; "[DONE]" for OpenAI done).
    raw_data: []const u8,
    /// Parsed JSON value if `raw_data` is valid JSON and not "[DONE]".
    /// Lifetime: valid until next `feedLine` call.
    json: ?std.json.Value,
    /// Backing parsed JSON memory (must be deinit'd after use).
    _json_parsed: ?std.json.Parsed(std.json.Value),
};

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

/// Callback invoked each time a complete SSE event is assembled.
/// Return `true` to abort the stream.
pub const EventCallback = *const fn (event: StreamEvent, userdata: ?*anyopaque) bool;

/// Stateful SSE parser.  Feed raw lines one at a time via `feedLine`.
pub const Parser = struct {
    allocator: std.mem.Allocator,
    /// Accumulated `event:` value for the current event.
    event_name_buf: std.ArrayList(u8),
    /// Accumulated `data:` value for the current event.
    data_buf: std.ArrayList(u8),
    /// User callback.
    callback: EventCallback,
    /// Passed through to `callback`.
    userdata: ?*anyopaque,
    /// Set to true if callback returned true (abort signal).
    abort_requested: bool,

    pub fn init(
        allocator: std.mem.Allocator,
        callback: EventCallback,
        userdata: ?*anyopaque,
    ) Parser {
        return Parser{
            .allocator = allocator,
            .event_name_buf = std.ArrayList(u8).init(allocator),
            .data_buf = std.ArrayList(u8).init(allocator),
            .callback = callback,
            .userdata = userdata,
            .abort_requested = false,
        };
    }

    pub fn deinit(self: *Parser) void {
        self.event_name_buf.deinit();
        self.data_buf.deinit();
    }

    /// Reset the buffers for the current (in-progress) event without
    /// discarding them — called after dispatching.
    fn resetEvent(self: *Parser) void {
        self.event_name_buf.clearRetainingCapacity();
        self.data_buf.clearRetainingCapacity();
    }

    /// Process one raw line (without the trailing newline character).
    ///
    /// Returns `error.StreamAborted` if the callback requested an abort.
    pub fn feedLine(self: *Parser, line: []const u8) error{StreamAborted}!void {
        if (self.abort_requested) return error.StreamAborted;

        // Blank line → dispatch current event (if any)
        if (line.len == 0) {
            if (self.data_buf.items.len > 0 or self.event_name_buf.items.len > 0) {
                try self.dispatchEvent();
                self.resetEvent();
            }
            return;
        }

        // Comments start with ':'
        if (line[0] == ':') return;

        // Find field/value split at first ':'
        if (std.mem.indexOfScalar(u8, line, ':')) |colon| {
            const field = line[0..colon];
            // Skip the optional single space after the colon
            const value_start: usize = if (colon + 1 < line.len and line[colon + 1] == ' ')
                colon + 2
            else
                colon + 1;
            const value = if (value_start <= line.len) line[value_start..] else "";

            if (std.mem.eql(u8, field, "event")) {
                self.event_name_buf.clearRetainingCapacity();
                self.event_name_buf.appendSlice(value) catch {};
            } else if (std.mem.eql(u8, field, "data")) {
                self.data_buf.appendSlice(value) catch {};
            }
            // Ignore: id, retry, and unknown fields
        } else {
            // Line with no colon — treat entire line as data
            self.data_buf.appendSlice(line) catch {};
        }
    }

    // ------------------------------------------------------------------
    // Private
    // ------------------------------------------------------------------

    fn dispatchEvent(self: *Parser) error{StreamAborted}!void {
        const event_name = self.event_name_buf.items;
        const raw_data = self.data_buf.items;

        const kind = classifyEvent(event_name, raw_data);

        // Try to parse JSON (skip for "[DONE]" marker)
        var json_parsed: ?std.json.Parsed(std.json.Value) = null;
        var json_val: ?std.json.Value = null;

        if (raw_data.len > 0 and !std.mem.eql(u8, raw_data, "[DONE]")) {
            if (std.json.parseFromSlice(std.json.Value, self.allocator, raw_data, .{})) |p| {
                json_parsed = p;
                json_val = p.value;
            } else |_| {
                // Non-JSON data — leave json_val null
            }
        }

        defer if (json_parsed) |*p| p.deinit();

        const event = StreamEvent{
            .kind = kind,
            .event_name = event_name,
            .raw_data = raw_data,
            .json = json_val,
            ._json_parsed = null, // json_parsed freed in defer above
        };

        if (self.callback(event, self.userdata)) {
            self.abort_requested = true;
            return error.StreamAborted;
        }
    }
};

// ---------------------------------------------------------------------------
// Classification helper
// ---------------------------------------------------------------------------

fn classifyEvent(event_name: []const u8, raw_data: []const u8) StreamEventKind {
    // Anthropic: explicit event names
    if (event_name.len > 0) {
        if (std.mem.eql(u8, event_name, "message_start")) return .message_start;
        if (std.mem.eql(u8, event_name, "content_block_start")) return .content_block_start;
        if (std.mem.eql(u8, event_name, "content_block_delta")) return .content_block_delta;
        if (std.mem.eql(u8, event_name, "content_block_stop")) return .content_block_stop;
        if (std.mem.eql(u8, event_name, "message_delta")) return .message_delta;
        if (std.mem.eql(u8, event_name, "message_stop")) return .message_stop;
        if (std.mem.eql(u8, event_name, "error")) return .error_event;
        if (std.mem.eql(u8, event_name, "ping")) return .ping;
        return .unknown;
    }

    // OpenAI: no explicit event field — classify by data content
    if (std.mem.eql(u8, raw_data, "[DONE]")) return .openai_done;
    if (raw_data.len > 0) return .openai_chunk;

    return .ping; // empty event treated as keepalive
}

// ---------------------------------------------------------------------------
// Convenience: parse a complete SSE document in one call
// ---------------------------------------------------------------------------

/// A collected SSE event with owned string memory.
/// Caller must call `deinit(allocator)` on each event in the returned list.
pub const OwnedEvent = struct {
    kind: StreamEventKind,
    /// Owned copy of the event name.
    event_name: []const u8,
    /// Owned copy of the raw data.
    raw_data: []const u8,

    pub fn deinit(self: *OwnedEvent, allocator: std.mem.Allocator) void {
        allocator.free(self.event_name);
        allocator.free(self.raw_data);
    }
};

/// Parse a full SSE response body (newline-separated lines) and collect all
/// events into a `std.ArrayList(OwnedEvent)`.
///
/// Each event in the returned list owns its `event_name` and `raw_data`
/// strings (duped from the parser's internal buffers).  Call
/// `event.deinit(allocator)` on each entry before freeing the list.
pub fn parseDocument(
    allocator: std.mem.Allocator,
    document: []const u8,
    out_events: *std.ArrayList(OwnedEvent),
) !void {
    const Collector = struct {
        alloc: std.mem.Allocator,
        events: *std.ArrayList(OwnedEvent),

        fn callback(event: StreamEvent, userdata: ?*anyopaque) bool {
            const self = @as(*@This(), @ptrCast(@alignCast(userdata.?)));
            // Dupe the strings so they survive after the Parser resets its buffers.
            const owned_name = self.alloc.dupe(u8, event.event_name) catch return true;
            const owned_data = self.alloc.dupe(u8, event.raw_data) catch {
                self.alloc.free(owned_name);
                return true;
            };
            self.events.append(OwnedEvent{
                .kind = event.kind,
                .event_name = owned_name,
                .raw_data = owned_data,
            }) catch {
                self.alloc.free(owned_name);
                self.alloc.free(owned_data);
                return true;
            };
            return false;
        }
    };

    var collector = Collector{
        .alloc = allocator,
        .events = out_events,
    };

    var parser = Parser.init(allocator, Collector.callback, &collector);
    defer parser.deinit();

    var it = std.mem.splitScalar(u8, document, '\n');
    while (it.next()) |raw_line| {
        // Strip trailing \r (Windows CRLF line endings)
        const line = if (raw_line.len > 0 and raw_line[raw_line.len - 1] == '\r')
            raw_line[0 .. raw_line.len - 1]
        else
            raw_line;
        parser.feedLine(line) catch break;
    }
    // Feed a final blank line to flush any trailing event
    parser.feedLine("") catch {};
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "classifyEvent — Anthropic events" {
    try std.testing.expectEqual(StreamEventKind.message_start, classifyEvent("message_start", "{}"));
    try std.testing.expectEqual(StreamEventKind.content_block_delta, classifyEvent("content_block_delta", "{}"));
    try std.testing.expectEqual(StreamEventKind.message_stop, classifyEvent("message_stop", "{}"));
    try std.testing.expectEqual(StreamEventKind.error_event, classifyEvent("error", "{}"));
    try std.testing.expectEqual(StreamEventKind.ping, classifyEvent("ping", "{}"));
    try std.testing.expectEqual(StreamEventKind.unknown, classifyEvent("custom_unknown", "{}"));
}

test "classifyEvent — OpenAI events" {
    try std.testing.expectEqual(StreamEventKind.openai_done, classifyEvent("", "[DONE]"));
    try std.testing.expectEqual(StreamEventKind.openai_chunk, classifyEvent("", "{\"choices\":[]}"));
}

test "classifyEvent — empty event is ping" {
    try std.testing.expectEqual(StreamEventKind.ping, classifyEvent("", ""));
}

test "parseDocument — Anthropic text_delta" {
    const sse =
        \\event: content_block_delta
        \\data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}}
        \\
        \\event: message_stop
        \\data: {"type":"message_stop"}
        \\
        \\
    ;
    var events = std.ArrayList(OwnedEvent).init(std.testing.allocator);
    defer {
        for (events.items) |*ev| ev.deinit(std.testing.allocator);
        events.deinit();
    }

    try parseDocument(std.testing.allocator, sse, &events);

    try std.testing.expectEqual(@as(usize, 2), events.items.len);
    try std.testing.expectEqual(StreamEventKind.content_block_delta, events.items[0].kind);
    try std.testing.expectEqual(StreamEventKind.message_stop, events.items[1].kind);
    try std.testing.expectEqualStrings("content_block_delta", events.items[0].event_name);
}

test "parseDocument — OpenAI chunk + done" {
    const sse =
        \\data: {"id":"chatcmpl-1","choices":[{"delta":{"content":"Hi"},"index":0}]}
        \\
        \\data: [DONE]
        \\
        \\
    ;
    var events = std.ArrayList(OwnedEvent).init(std.testing.allocator);
    defer {
        for (events.items) |*ev| ev.deinit(std.testing.allocator);
        events.deinit();
    }

    try parseDocument(std.testing.allocator, sse, &events);

    try std.testing.expectEqual(@as(usize, 2), events.items.len);
    try std.testing.expectEqual(StreamEventKind.openai_chunk, events.items[0].kind);
    try std.testing.expectEqual(StreamEventKind.openai_done, events.items[1].kind);
    try std.testing.expectEqualStrings("[DONE]", events.items[1].raw_data);
}

test "parseDocument — ping (comment) skipped" {
    const sse =
        \\: this is a comment
        \\
        \\data: {"hello":"world"}
        \\
        \\
    ;
    var events = std.ArrayList(OwnedEvent).init(std.testing.allocator);
    defer {
        for (events.items) |*ev| ev.deinit(std.testing.allocator);
        events.deinit();
    }

    try parseDocument(std.testing.allocator, sse, &events);

    // Comment lines should not produce events; only the data line should
    try std.testing.expectEqual(@as(usize, 1), events.items.len);
    try std.testing.expectEqual(StreamEventKind.openai_chunk, events.items[0].kind);
}

test "parseDocument — CRLF line endings" {
    // Use explicit \r\n line endings
    const sse = "event: ping\r\ndata: {}\r\n\r\n";
    var events = std.ArrayList(OwnedEvent).init(std.testing.allocator);
    defer {
        for (events.items) |*ev| ev.deinit(std.testing.allocator);
        events.deinit();
    }

    try parseDocument(std.testing.allocator, sse, &events);

    try std.testing.expectEqual(@as(usize, 1), events.items.len);
    try std.testing.expectEqual(StreamEventKind.ping, events.items[0].kind);
}

test "Parser.feedLine — abort on callback true" {
    const AbortImmediate = struct {
        fn cb(event: StreamEvent, userdata: ?*anyopaque) bool {
            _ = event;
            _ = userdata;
            return true; // abort
        }
    };

    var parser = Parser.init(std.testing.allocator, AbortImmediate.cb, null);
    defer parser.deinit();

    // Feed a complete event
    try parser.feedLine("data: hello");
    const result = parser.feedLine(""); // triggers dispatch → abort
    try std.testing.expectError(error.StreamAborted, result);
}

test "parseDocument — raw_data slice contents" {
    const sse =
        \\data: some raw payload
        \\
        \\
    ;
    var events = std.ArrayList(OwnedEvent).init(std.testing.allocator);
    defer {
        for (events.items) |*ev| ev.deinit(std.testing.allocator);
        events.deinit();
    }

    try parseDocument(std.testing.allocator, sse, &events);

    try std.testing.expectEqual(@as(usize, 1), events.items.len);
    try std.testing.expectEqualStrings("some raw payload", events.items[0].raw_data);
}
