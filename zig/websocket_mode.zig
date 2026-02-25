//! websocket_mode.zig — WebSocket daemon mode for klawed
//!
//! Like the SQLite queue mode but fully ephemeral: no database, no files.
//! klawed binds a TCP port, performs the WebSocket handshake, then exchanges
//! JSON messages with the client using the same message format as the
//! SQLite queue mode (TEXT, TOOL, TOOL_RESULT, API_CALL, END_AI_TURN, ERROR,
//! AUTO_COMPACTION, TRIGGER_COMPACT, INTERRUPT).
//!
//! ## Key differences vs SQLite queue mode
//!   - No storage — all messages exist only in memory / in-flight
//!   - Push instead of poll — messages are sent immediately over the socket
//!   - Single client at a time (accept then process then close then repeat)
//!   - Interrupt support: client sends {"messageType":"INTERRUPT"} to abort
//!     the current AI turn mid-execution
//!
//! ## Lifecycle
//!   1. Bind TCP socket on host:port (default 0.0.0.0:9999)
//!   2. Accept one client connection at a time
//!   3. Perform WebSocket handshake
//!   4. Spawn a sender thread for outbound frames
//!   5. Main thread reads inbound WS frames and dispatches to process_fn
//!   6. process_fn posts outbound messages via WsOutChannel
//!   7. On INTERRUPT or client disconnect, cancel in-flight work and loop back
//!
//! ## Configuration (environment variables)
//!
//!   KLAWED_WS_HOST          Bind host           (default: 0.0.0.0)
//!   KLAWED_WS_PORT          Bind port           (default: 9999)
//!   KLAWED_WS_SENDER        Sender name in JSON (default: klawed)
//!   KLAWED_WS_MAX_MSG_SIZE  Max message bytes   (default: 4194304 = 4 MiB)
//!   KLAWED_WS_MAX_QUEUE     Outbound queue cap  (default: 1000)
//!
//! ## Message format
//!
//! Identical to the SQLite queue mode (see docs/sqlite-queue.md).
//! Additional inbound message type:
//!   INTERRUPT  — abort the current AI turn immediately

const std = @import("std");
const ws = @import("websocket.zig");

// ---------------------------------------------------------------------------
// Internal helpers (declared first so struct methods can reference them)
// ---------------------------------------------------------------------------

fn envInt(name: []const u8, default_val: i64) i64 {
    const val = std.posix.getenv(name) orelse return default_val;
    return std.fmt.parseInt(i64, val, 10) catch default_val;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

pub const WsConfig = struct {
    host: []const u8 = "0.0.0.0",
    port: u16 = 9999,
    sender_name: []const u8 = "klawed",
    max_msg_size: usize = 4 * 1024 * 1024,
    max_queue: usize = 1000,

    pub fn fromEnv() WsConfig {
        return .{
            .host = std.posix.getenv("KLAWED_WS_HOST") orelse "0.0.0.0",
            .port = @intCast(envInt("KLAWED_WS_PORT", 9999)),
            .sender_name = std.posix.getenv("KLAWED_WS_SENDER") orelse "klawed",
            .max_msg_size = @intCast(envInt("KLAWED_WS_MAX_MSG_SIZE", 4 * 1024 * 1024)),
            .max_queue = @intCast(envInt("KLAWED_WS_MAX_QUEUE", 1000)),
        };
    }
};

// ---------------------------------------------------------------------------
// Message types
// ---------------------------------------------------------------------------

pub const MessageType = enum {
    TEXT,
    TOOL,
    TOOL_RESULT,
    API_CALL,
    END_AI_TURN,
    ERROR,
    AUTO_COMPACTION,
    TRIGGER_COMPACT,
    INTERRUPT,
};

/// Parse the `messageType` field from a JSON object.
/// Returns null if not found or unrecognised.
pub fn parseMessageType(json: []const u8) ?MessageType {
    const key = "\"messageType\":";
    const pos = std.mem.indexOf(u8, json, key) orelse return null;
    const after = json[pos + key.len ..];

    const q1 = std.mem.indexOf(u8, after, "\"") orelse return null;
    const rest = after[q1 + 1 ..];
    const q2 = std.mem.indexOf(u8, rest, "\"") orelse return null;
    const type_str = rest[0..q2];

    if (std.mem.eql(u8, type_str, "TEXT")) return .TEXT;
    if (std.mem.eql(u8, type_str, "TOOL")) return .TOOL;
    if (std.mem.eql(u8, type_str, "TOOL_RESULT")) return .TOOL_RESULT;
    if (std.mem.eql(u8, type_str, "API_CALL")) return .API_CALL;
    if (std.mem.eql(u8, type_str, "END_AI_TURN")) return .END_AI_TURN;
    if (std.mem.eql(u8, type_str, "ERROR")) return .ERROR;
    if (std.mem.eql(u8, type_str, "AUTO_COMPACTION")) return .AUTO_COMPACTION;
    if (std.mem.eql(u8, type_str, "TRIGGER_COMPACT")) return .TRIGGER_COMPACT;
    if (std.mem.eql(u8, type_str, "INTERRUPT")) return .INTERRUPT;
    return null;
}

// ---------------------------------------------------------------------------
// Outbound message channel
// ---------------------------------------------------------------------------

pub const WsOutChannel = struct {
    allocator: std.mem.Allocator,
    items: [][]const u8,
    cap: usize,
    head: usize,
    tail: usize,
    count: usize,
    mutex: std.Thread.Mutex,
    not_empty: std.Thread.Condition,
    not_full: std.Thread.Condition,
    shutdown: bool,

    pub fn init(allocator: std.mem.Allocator, capacity: usize) !WsOutChannel {
        const items = try allocator.alloc([]const u8, capacity);
        for (items) |*s| s.* = "";
        return .{
            .allocator = allocator,
            .items = items,
            .cap = capacity,
            .head = 0,
            .tail = 0,
            .count = 0,
            .mutex = .{},
            .not_empty = .{},
            .not_full = .{},
            .shutdown = false,
        };
    }

    pub fn deinit(self: *WsOutChannel) void {
        self.mutex.lock();
        var i: usize = 0;
        while (i < self.count) : (i += 1) {
            const idx = (self.tail + i) % self.cap;
            if (self.items[idx].len > 0) self.allocator.free(self.items[idx]);
        }
        self.mutex.unlock();
        self.allocator.free(self.items);
        self.* = undefined;
    }

    /// Push a message (copy taken).  Blocks up to 5 s if full.
    pub fn push(self: *WsOutChannel, msg: []const u8) !void {
        const copy = try self.allocator.dupe(u8, msg);
        errdefer self.allocator.free(copy);

        self.mutex.lock();
        defer self.mutex.unlock();

        var waited_ms: u64 = 0;
        while (self.count == self.cap and !self.shutdown) {
            if (waited_ms >= 5000) return error.ChannelFull;
            self.mutex.unlock();
            std.time.sleep(10 * std.time.ns_per_ms);
            self.mutex.lock();
            waited_ms += 10;
        }
        if (self.shutdown) {
            self.allocator.free(copy);
            return error.ChannelShutdown;
        }

        self.items[self.head] = copy;
        self.head = (self.head + 1) % self.cap;
        self.count += 1;
        self.not_empty.signal();
    }

    /// Blocking pop — returns null on shutdown.  Caller owns and must free.
    pub fn pop(self: *WsOutChannel) ?[]const u8 {
        self.mutex.lock();
        defer self.mutex.unlock();

        while (self.count == 0 and !self.shutdown) {
            self.not_empty.wait(&self.mutex);
        }
        if (self.count == 0) return null;

        const item = self.items[self.tail];
        self.items[self.tail] = "";
        self.tail = (self.tail + 1) % self.cap;
        self.count -= 1;
        self.not_full.signal();
        return item;
    }

    /// Non-blocking pop.  Returns null if empty.
    pub fn tryPop(self: *WsOutChannel) ?[]const u8 {
        self.mutex.lock();
        defer self.mutex.unlock();
        if (self.count == 0) return null;
        const item = self.items[self.tail];
        self.items[self.tail] = "";
        self.tail = (self.tail + 1) % self.cap;
        self.count -= 1;
        self.not_full.signal();
        return item;
    }

    pub fn shutdownChannel(self: *WsOutChannel) void {
        self.mutex.lock();
        defer self.mutex.unlock();
        self.shutdown = true;
        self.not_empty.broadcast();
        self.not_full.broadcast();
    }

    pub fn isShutdown(self: *WsOutChannel) bool {
        self.mutex.lock();
        defer self.mutex.unlock();
        return self.shutdown;
    }
};

// ---------------------------------------------------------------------------
// Interrupt flag
// ---------------------------------------------------------------------------

/// Shared atomic interrupt flag.
/// The main connection thread sets it when an INTERRUPT message arrives;
/// the AI worker polls it to abort the current turn gracefully.
pub const InterruptFlag = struct {
    value: std.atomic.Value(bool),

    pub fn init() InterruptFlag {
        return .{ .value = std.atomic.Value(bool).init(false) };
    }

    pub fn set(self: *InterruptFlag) void {
        self.value.store(true, .release);
    }

    pub fn clear(self: *InterruptFlag) void {
        self.value.store(false, .release);
    }

    pub fn isSet(self: *InterruptFlag) bool {
        return self.value.load(.acquire);
    }
};

// ---------------------------------------------------------------------------
// ProcessMessageFn — callback signature for the daemon caller
// ---------------------------------------------------------------------------

/// Called by the WS daemon for each complete TEXT or TRIGGER_COMPACT message.
///
/// Parameters:
///   allocator       — arena/gpa for this call's allocations
///   json            — raw JSON string received from the client
///   out             — push response frames here (will be sent to client)
///   interrupt_flag  — poll this; abort early if set
///   ctx             — arbitrary caller-supplied context pointer
pub const ProcessMessageFn = *const fn (
    allocator: std.mem.Allocator,
    json: []const u8,
    out: *WsOutChannel,
    interrupt_flag: *InterruptFlag,
    ctx: ?*anyopaque,
) void;

// ---------------------------------------------------------------------------
// Connection context (per-accepted client)
// ---------------------------------------------------------------------------

const ConnContext = struct {
    allocator: std.mem.Allocator,
    stream: std.net.Stream,
    out_channel: *WsOutChannel,
    interrupt_flag: *InterruptFlag,
    process_fn: ProcessMessageFn,
    process_ctx: ?*anyopaque,
    cfg: *const WsConfig,
    shutdown: std.atomic.Value(bool),
};

// ---------------------------------------------------------------------------
// Sender thread
// ---------------------------------------------------------------------------

fn senderThread(ctx: *ConnContext) void {
    while (true) {
        const msg = ctx.out_channel.pop() orelse break;
        defer ctx.allocator.free(msg);

        ws.sendText(ctx.stream, msg) catch |err| {
            std.log.warn("ws: send error: {}", .{err});
            ctx.shutdown.store(true, .release);
            break;
        };
    }
}

// ---------------------------------------------------------------------------
// Handle one WebSocket connection
// ---------------------------------------------------------------------------

fn handleConnection(ctx: *ConnContext) void {
    const allocator = ctx.allocator;
    var msg_buf = std.ArrayList(u8).init(allocator);
    defer msg_buf.deinit();

    ws.serverHandshake(ctx.stream, allocator) catch |err| {
        std.log.warn("ws: handshake failed: {}", .{err});
        return;
    };

    std.log.info("ws: client connected", .{});

    const sender = std.Thread.spawn(.{}, senderThread, .{ctx}) catch |err| {
        std.log.err("ws: failed to spawn sender thread: {}", .{err});
        return;
    };
    defer {
        ctx.out_channel.shutdownChannel();
        sender.join();
    }

    while (!ctx.shutdown.load(.acquire)) {
        msg_buf.clearRetainingCapacity();

        const opcode = ws.readMessage(ctx.stream, &msg_buf, true) catch |err| {
            switch (err) {
                ws.WsError.ConnectionClosed => std.log.info("ws: client disconnected (close frame)", .{}),
                else => std.log.warn("ws: read error: {}", .{err}),
            }
            break;
        };

        if (opcode != .text and opcode != .binary) continue;

        const json = msg_buf.items;
        const msg_type = parseMessageType(json) orelse {
            std.log.warn("ws: unknown messageType in: {s}", .{json[0..@min(json.len, 120)]});
            continue;
        };

        switch (msg_type) {
            .INTERRUPT => {
                std.log.info("ws: INTERRUPT received", .{});
                ctx.interrupt_flag.set();
            },
            .TEXT, .TRIGGER_COMPACT => {
                ctx.interrupt_flag.clear();
                ctx.process_fn(
                    allocator,
                    json,
                    ctx.out_channel,
                    ctx.interrupt_flag,
                    ctx.process_ctx,
                );
            },
            else => {
                std.log.warn("ws: unexpected inbound message type: {}", .{msg_type});
            },
        }
    }

    ws.sendClose(ctx.stream, 1000, "bye") catch {};
    std.log.info("ws: connection closed", .{});
}

// ---------------------------------------------------------------------------
// Main daemon loop
// ---------------------------------------------------------------------------

/// Run the WebSocket daemon.
///
/// Binds `cfg.host:cfg.port`, accepts clients one at a time, and calls
/// `process_fn` for each inbound TEXT or TRIGGER_COMPACT message.
/// Blocks until `shutdown_flag` is set.
pub fn runWsDaemon(
    allocator: std.mem.Allocator,
    cfg: WsConfig,
    process_fn: ProcessMessageFn,
    process_ctx: ?*anyopaque,
    shutdown_flag: *std.atomic.Value(bool),
) !void {
    const addr = try std.net.Address.parseIp(cfg.host, cfg.port);
    var server = try addr.listen(.{
        .reuse_address = true,
        .reuse_port = true,
    });
    defer server.deinit();

    std.log.info("ws: listening on {s}:{d}", .{ cfg.host, cfg.port });

    while (!shutdown_flag.load(.acquire)) {
        const conn = server.accept() catch |err| {
            if (err == error.WouldBlock) {
                std.time.sleep(50 * std.time.ns_per_ms);
                continue;
            }
            std.log.err("ws: accept error: {}", .{err});
            continue;
        };

        var out_channel = WsOutChannel.init(allocator, cfg.max_queue) catch |err| {
            std.log.err("ws: failed to init out channel: {}", .{err});
            conn.stream.close();
            continue;
        };
        var interrupt_flag = InterruptFlag.init();

        var conn_ctx = ConnContext{
            .allocator = allocator,
            .stream = conn.stream,
            .out_channel = &out_channel,
            .interrupt_flag = &interrupt_flag,
            .process_fn = process_fn,
            .process_ctx = process_ctx,
            .cfg = &cfg,
            .shutdown = std.atomic.Value(bool).init(false),
        };

        handleConnection(&conn_ctx);

        out_channel.deinit();
        conn.stream.close();
    }

    std.log.info("ws: daemon shutdown", .{});
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

/// Extract the string value of "content" from a JSON object.
/// Returns a slice into `json` (not owned).
pub fn extractContent(json: []const u8) ?[]const u8 {
    const key = "\"content\":";
    const pos = std.mem.indexOf(u8, json, key) orelse return null;
    const after = json[pos + key.len ..];
    const q1 = std.mem.indexOf(u8, after, "\"") orelse return null;
    const rest = after[q1 + 1 ..];
    var i: usize = 0;
    while (i < rest.len) : (i += 1) {
        if (rest[i] == '\\') {
            i += 1;
        } else if (rest[i] == '"') {
            return rest[0..i];
        }
    }
    return null;
}

/// Build a {"messageType":"...","content":"..."} JSON string.
/// Caller must free with `allocator.free`.
pub fn buildTextMessage(
    allocator: std.mem.Allocator,
    msg_type: []const u8,
    content: []const u8,
) ![]const u8 {
    return std.fmt.allocPrint(
        allocator,
        "{{\"messageType\":\"{s}\",\"content\":\"{s}\"}}",
        .{ msg_type, content },
    );
}

/// Build a {"messageType":"END_AI_TURN"} message.
pub fn buildEndTurnMessage(allocator: std.mem.Allocator) ![]const u8 {
    return allocator.dupe(u8, "{\"messageType\":\"END_AI_TURN\"}");
}

/// Build a {"messageType":"ERROR","content":"..."} message.
pub fn buildErrorMessage(allocator: std.mem.Allocator, err_text: []const u8) ![]const u8 {
    return buildTextMessage(allocator, "ERROR", err_text);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "parseMessageType: known types" {
    const t = std.testing;
    try t.expectEqual(MessageType.TEXT, parseMessageType("{\"messageType\":\"TEXT\",\"content\":\"hello\"}").?);
    try t.expectEqual(MessageType.INTERRUPT, parseMessageType("{\"messageType\":\"INTERRUPT\"}").?);
    try t.expectEqual(MessageType.END_AI_TURN, parseMessageType("{\"messageType\":\"END_AI_TURN\"}").?);
    try t.expectEqual(MessageType.TRIGGER_COMPACT, parseMessageType("{\"messageType\":\"TRIGGER_COMPACT\"}").?);
    try t.expectEqual(MessageType.TOOL, parseMessageType("{\"messageType\":\"TOOL\",\"toolName\":\"Read\"}").?);
    try t.expectEqual(MessageType.ERROR, parseMessageType("{\"messageType\":\"ERROR\",\"content\":\"oops\"}").?);
    try t.expectEqual(MessageType.API_CALL, parseMessageType("{\"messageType\":\"API_CALL\"}").?);
    try t.expectEqual(MessageType.AUTO_COMPACTION, parseMessageType("{\"messageType\":\"AUTO_COMPACTION\"}").?);
    try t.expectEqual(MessageType.TOOL_RESULT, parseMessageType("{\"messageType\":\"TOOL_RESULT\"}").?);
}

test "parseMessageType: unknown returns null" {
    const t = std.testing;
    try t.expectEqual(@as(?MessageType, null), parseMessageType("{\"messageType\":\"BOGUS\"}"));
    try t.expectEqual(@as(?MessageType, null), parseMessageType("{}"));
    try t.expectEqual(@as(?MessageType, null), parseMessageType(""));
}

test "extractContent: basic" {
    const t = std.testing;
    const json = "{\"messageType\":\"TEXT\",\"content\":\"hello world\"}";
    const content = extractContent(json);
    try t.expect(content != null);
    try t.expectEqualStrings("hello world", content.?);
}

test "extractContent: missing" {
    try std.testing.expectEqual(@as(?[]const u8, null), extractContent("{\"messageType\":\"END_AI_TURN\"}"));
}

test "WsOutChannel: push and pop" {
    const allocator = std.testing.allocator;
    var ch = try WsOutChannel.init(allocator, 4);
    defer ch.deinit();

    try ch.push("hello");
    try ch.push("world");

    const a = ch.tryPop().?;
    defer allocator.free(a);
    const b = ch.tryPop().?;
    defer allocator.free(b);

    try std.testing.expectEqualStrings("hello", a);
    try std.testing.expectEqualStrings("world", b);
    try std.testing.expectEqual(@as(?[]const u8, null), ch.tryPop());
}

test "WsOutChannel: shutdown wakes pop" {
    const allocator = std.testing.allocator;
    var ch = try WsOutChannel.init(allocator, 4);
    defer ch.deinit();

    ch.shutdownChannel();
    try std.testing.expectEqual(@as(?[]const u8, null), ch.pop());
}

test "InterruptFlag: set and clear" {
    var f = InterruptFlag.init();
    try std.testing.expect(!f.isSet());
    f.set();
    try std.testing.expect(f.isSet());
    f.clear();
    try std.testing.expect(!f.isSet());
}

test "buildTextMessage and buildEndTurnMessage" {
    const allocator = std.testing.allocator;

    const text_msg = try buildTextMessage(allocator, "TEXT", "hello");
    defer allocator.free(text_msg);
    try std.testing.expect(std.mem.indexOf(u8, text_msg, "TEXT") != null);
    try std.testing.expect(std.mem.indexOf(u8, text_msg, "hello") != null);

    const end_msg = try buildEndTurnMessage(allocator);
    defer allocator.free(end_msg);
    try std.testing.expect(std.mem.indexOf(u8, end_msg, "END_AI_TURN") != null);
}

test "buildErrorMessage" {
    const allocator = std.testing.allocator;
    const err_msg = try buildErrorMessage(allocator, "something went wrong");
    defer allocator.free(err_msg);
    try std.testing.expect(std.mem.indexOf(u8, err_msg, "ERROR") != null);
    try std.testing.expect(std.mem.indexOf(u8, err_msg, "something went wrong") != null);
}

test "WsConfig fromEnv defaults" {
    const cfg = WsConfig.fromEnv();
    try std.testing.expectEqual(@as(u16, 9999), cfg.port);
    try std.testing.expectEqualStrings("klawed", cfg.sender_name);
}
