//! websocket.zig — Pure-Zig RFC 6455 WebSocket server primitives
//!
//! Implements the minimal subset of the WebSocket protocol needed for
//! klawed's daemon mode: server-side handshake, frame parsing, and frame
//! serialization.  Everything is ephemeral — no SQLite, no files.
//!
//! ## What is covered
//!   - HTTP/1.1 Upgrade handshake (server side only)
//!   - Frame parser: text, binary, ping, pong, close frames
//!   - Frame writer: text and control frames with masking stripped
//!   - UTF-8 payload passthrough (no validation — callers own that)
//!   - Message fragmentation reassembly (for large inbound messages)
//!
//! ## What is NOT covered
//!   - Client-side handshake
//!   - Per-message deflate / extensions
//!   - TLS — callers should front with a TLS terminator if needed

const std = @import("std");
const base64 = @import("base64.zig");

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

pub const WsError = error{
    InvalidHandshake,
    InvalidFrame,
    FrameTooLarge,
    ConnectionClosed,
    MaskRequired,
    OutOfMemory,
    Io,
};

// ---------------------------------------------------------------------------
// Frame opcodes  (RFC 6455 section 5.2)
// ---------------------------------------------------------------------------

pub const Opcode = enum(u4) {
    continuation = 0x0,
    text = 0x1,
    binary = 0x2,
    close = 0x8,
    ping = 0x9,
    pong = 0xA,
    _,
};

// ---------------------------------------------------------------------------
// Parsed frame (header + unmasked payload slice)
// ---------------------------------------------------------------------------

pub const Frame = struct {
    fin: bool,
    opcode: Opcode,
    /// Unmasked payload — owned by the caller / arena, not by Frame.
    payload: []u8,
};

// ---------------------------------------------------------------------------
// Handshake
// ---------------------------------------------------------------------------

/// Perform the server-side WebSocket upgrade handshake on `stream`.
///
/// Reads the HTTP/1.1 Upgrade request, validates the required headers, and
/// writes the 101 Switching Protocols response.
///
/// `allocator` is used for temporary line-buffering only.
pub fn serverHandshake(stream: std.net.Stream, allocator: std.mem.Allocator) !void {
    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    var found_upgrade = false;
    var found_connection = false;
    var key_buf: [128]u8 = undefined;
    var key_len: usize = 0;
    var ws_key_set = false;

    const reader = stream.reader();

    while (true) {
        buf.clearRetainingCapacity();
        try reader.readUntilDelimiterArrayList(&buf, '\n', 8192);
        // Strip trailing \r if present.
        if (buf.items.len > 0 and buf.items[buf.items.len - 1] == '\r') {
            buf.items.len -= 1;
        }
        const line = buf.items;
        if (line.len == 0) break;

        if (asciiIndexOf(line, ':')) |colon| {
            const name = std.mem.trim(u8, line[0..colon], " \t");
            const value = std.mem.trim(u8, line[colon + 1 ..], " \t");

            if (std.ascii.eqlIgnoreCase(name, "upgrade")) {
                if (std.ascii.eqlIgnoreCase(value, "websocket")) found_upgrade = true;
            } else if (std.ascii.eqlIgnoreCase(name, "connection")) {
                if (std.ascii.indexOfIgnoreCase(value, "upgrade") != null) found_connection = true;
            } else if (std.ascii.eqlIgnoreCase(name, "sec-websocket-key")) {
                const trimmed = std.mem.trim(u8, value, " \t");
                if (trimmed.len <= key_buf.len) {
                    @memcpy(key_buf[0..trimmed.len], trimmed);
                    key_len = trimmed.len;
                    ws_key_set = true;
                }
            }
        }
    }

    if (!found_upgrade or !found_connection or !ws_key_set) {
        return WsError.InvalidHandshake;
    }

    // Compute Sec-WebSocket-Accept = base64(sha1(key + GUID)).
    const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    var hasher = std.crypto.hash.Sha1.init(.{});
    hasher.update(key_buf[0..key_len]);
    hasher.update(GUID);
    var digest: [20]u8 = undefined;
    hasher.final(&digest);

    const accept = try base64.encode(allocator, &digest);
    defer allocator.free(accept);

    const writer = stream.writer();
    try writer.print(
        "HTTP/1.1 101 Switching Protocols\r\n" ++
            "Upgrade: websocket\r\n" ++
            "Connection: Upgrade\r\n" ++
            "Sec-WebSocket-Accept: {s}\r\n" ++
            "\r\n",
        .{accept},
    );
}

// ---------------------------------------------------------------------------
// Frame reader
// ---------------------------------------------------------------------------

/// Read one WebSocket frame from `reader`.
///
/// `payload_buf` is written with the unmasked payload.
/// Returns `WsError.FrameTooLarge` if payload exceeds `payload_buf.len`.
/// Per RFC 6455 clients MUST mask frames; `WsError.MaskRequired` is returned
/// if an unmasked client frame is received (`is_client = true`).
pub fn readFrame(
    reader: anytype,
    payload_buf: []u8,
    is_client: bool,
) !Frame {
    const b0 = try reader.readByte();
    const fin = (b0 & 0x80) != 0;
    const opcode: Opcode = @enumFromInt(@as(u4, @truncate(b0 & 0x0F)));

    const b1 = try reader.readByte();
    const masked = (b1 & 0x80) != 0;
    var payload_len: u64 = b1 & 0x7F;

    if (is_client and !masked) return WsError.MaskRequired;

    if (payload_len == 126) {
        var ext: [2]u8 = undefined;
        try reader.readNoEof(&ext);
        payload_len = std.mem.readInt(u16, &ext, .big);
    } else if (payload_len == 127) {
        var ext: [8]u8 = undefined;
        try reader.readNoEof(&ext);
        payload_len = std.mem.readInt(u64, &ext, .big);
    }

    if (payload_len > payload_buf.len) return WsError.FrameTooLarge;

    var mask: [4]u8 = .{ 0, 0, 0, 0 };
    if (masked) try reader.readNoEof(&mask);

    const plen = @as(usize, @intCast(payload_len));
    try reader.readNoEof(payload_buf[0..plen]);

    if (masked) {
        for (payload_buf[0..plen], 0..) |*b, i| {
            b.* ^= mask[i % 4];
        }
    }

    return Frame{
        .fin = fin,
        .opcode = opcode,
        .payload = payload_buf[0..plen],
    };
}

// ---------------------------------------------------------------------------
// Message reader (handles fragmentation)
// ---------------------------------------------------------------------------

/// Read a complete (possibly fragmented) WebSocket message into `out`.
///
/// Fragments are reassembled transparently.  Returns the opcode of the first
/// frame (.text or .binary).  Control frames (ping/pong/close) received
/// during fragmented messages are handled inline.
pub fn readMessage(
    stream: std.net.Stream,
    out: *std.ArrayList(u8),
    is_client: bool,
) !Opcode {
    var frame_buf: [4 * 1024 * 1024]u8 = undefined;
    var msg_opcode: ?Opcode = null;
    const reader = stream.reader();

    while (true) {
        const frame = try readFrame(reader, &frame_buf, is_client);

        switch (frame.opcode) {
            .ping => {
                try writeFrame(stream, .pong, frame.payload, false);
                continue;
            },
            .pong => continue,
            .close => return WsError.ConnectionClosed,
            .continuation => {
                if (msg_opcode == null) return WsError.InvalidFrame;
                try out.appendSlice(frame.payload);
            },
            else => {
                msg_opcode = frame.opcode;
                out.clearRetainingCapacity();
                try out.appendSlice(frame.payload);
            },
        }

        if (frame.fin) break;
    }

    return msg_opcode orelse WsError.InvalidFrame;
}

// ---------------------------------------------------------------------------
// Frame writer
// ---------------------------------------------------------------------------

/// Write a single unfragmented WebSocket frame to `stream`.
///
/// `mask_payload` should be true for client-originated frames; klawed
/// always acts as server so it passes false.
pub fn writeFrame(
    stream: std.net.Stream,
    opcode: Opcode,
    payload: []const u8,
    mask_payload: bool,
) !void {
    const writer = stream.writer();

    try writer.writeByte(0x80 | @as(u8, @intFromEnum(opcode)));

    const plen = payload.len;
    const mask_bit: u8 = if (mask_payload) 0x80 else 0x00;

    if (plen < 126) {
        try writer.writeByte(mask_bit | @as(u8, @intCast(plen)));
    } else if (plen <= 0xFFFF) {
        try writer.writeByte(mask_bit | 126);
        var ext: [2]u8 = undefined;
        std.mem.writeInt(u16, &ext, @as(u16, @intCast(plen)), .big);
        try writer.writeAll(&ext);
    } else {
        try writer.writeByte(mask_bit | 127);
        var ext: [8]u8 = undefined;
        std.mem.writeInt(u64, &ext, plen, .big);
        try writer.writeAll(&ext);
    }

    if (mask_payload) {
        var mask: [4]u8 = undefined;
        std.crypto.random.bytes(&mask);
        try writer.writeAll(&mask);
        for (payload, 0..) |b, i| {
            try writer.writeByte(b ^ mask[i % 4]);
        }
    } else {
        try writer.writeAll(payload);
    }
}

/// Convenience: send a UTF-8 text frame (server to client, no masking).
pub fn sendText(stream: std.net.Stream, text: []const u8) !void {
    try writeFrame(stream, .text, text, false);
}

/// Convenience: send a close frame with status code.
pub fn sendClose(stream: std.net.Stream, code: u16, reason: []const u8) !void {
    var buf: [125]u8 = undefined;
    buf[0] = @as(u8, @intCast(code >> 8));
    buf[1] = @as(u8, @intCast(code & 0xFF));
    const reason_len = @min(reason.len, 123);
    @memcpy(buf[2..][0..reason_len], reason[0..reason_len]);
    try writeFrame(stream, .close, buf[0 .. 2 + reason_len], false);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

fn asciiIndexOf(s: []const u8, needle: u8) ?usize {
    for (s, 0..) |c, i| {
        if (c == needle) return i;
    }
    return null;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "frame round-trip: small text, no mask" {
    const allocator = std.testing.allocator;

    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    const payload = "hello websocket";

    {
        const writer = buf.writer();
        try writer.writeByte(0x81); // FIN=1, text
        try writer.writeByte(@as(u8, @intCast(payload.len)));
        try writer.writeAll(payload);
    }

    var frame_buf: [256]u8 = undefined;
    var fbs = std.io.fixedBufferStream(buf.items);
    const frame = try readFrame(fbs.reader(), &frame_buf, false);
    try std.testing.expect(frame.fin);
    try std.testing.expect(frame.opcode == .text);
    try std.testing.expectEqualStrings(payload, frame.payload);
}

test "frame round-trip: masked client frame" {
    const allocator = std.testing.allocator;

    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    const payload = "ping!";
    const mask_key = [4]u8{ 0xDE, 0xAD, 0xBE, 0xEF };

    {
        const writer = buf.writer();
        try writer.writeByte(0x81); // FIN=1, text
        try writer.writeByte(0x80 | @as(u8, @intCast(payload.len))); // MASK=1
        try writer.writeAll(&mask_key);
        for (payload, 0..) |b, i| {
            try writer.writeByte(b ^ mask_key[i % 4]);
        }
    }

    var frame_buf: [256]u8 = undefined;
    var fbs = std.io.fixedBufferStream(buf.items);
    const frame = try readFrame(fbs.reader(), &frame_buf, true);
    try std.testing.expect(frame.fin);
    try std.testing.expectEqualStrings(payload, frame.payload);
}

test "frame: 2-byte extended length" {
    const allocator = std.testing.allocator;

    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    const payload = "A" ** 200;

    {
        const writer = buf.writer();
        try writer.writeByte(0x82); // FIN=1, binary
        try writer.writeByte(126); // 2-byte extended length
        var ext: [2]u8 = undefined;
        std.mem.writeInt(u16, &ext, 200, .big);
        try writer.writeAll(&ext);
        try writer.writeAll(payload);
    }

    var frame_buf: [512]u8 = undefined;
    var fbs = std.io.fixedBufferStream(buf.items);
    const frame = try readFrame(fbs.reader(), &frame_buf, false);
    try std.testing.expectEqual(@as(usize, 200), frame.payload.len);
    try std.testing.expectEqualStrings(payload, frame.payload);
}

test "writeFrame: produces parseable output" {
    const allocator = std.testing.allocator;

    var pipe_buf = std.ArrayList(u8).init(allocator);
    defer pipe_buf.deinit();

    const msg = "test message";
    {
        const writer = pipe_buf.writer();
        try writer.writeByte(0x80 | @as(u8, @intFromEnum(Opcode.text)));
        try writer.writeByte(@as(u8, @intCast(msg.len)));
        try writer.writeAll(msg);
    }

    var frame_buf: [256]u8 = undefined;
    var fbs = std.io.fixedBufferStream(pipe_buf.items);
    const frame = try readFrame(fbs.reader(), &frame_buf, false);
    try std.testing.expectEqualStrings(msg, frame.payload);
}

test "mask required error" {
    const allocator = std.testing.allocator;

    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    const payload = "oops";
    {
        const writer = buf.writer();
        try writer.writeByte(0x81); // FIN=1, text, no mask
        try writer.writeByte(@as(u8, @intCast(payload.len)));
        try writer.writeAll(payload);
    }

    var frame_buf: [256]u8 = undefined;
    var fbs = std.io.fixedBufferStream(buf.items);
    const result = readFrame(fbs.reader(), &frame_buf, true);
    try std.testing.expectError(WsError.MaskRequired, result);
}
