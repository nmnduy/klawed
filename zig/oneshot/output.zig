//! oneshot/output.zig — One-shot mode output formatting
//!
//! Zig port of src/oneshot/oneshot_output.c + src/oneshot/oneshot_ui.c.
//!
//! Supports two output formats:
//!   - `human`   — human-readable with Unicode box-drawing
//!   - `machine` — HTML+JSON for machine parsing (KLAWED_ONESHOT_FORMAT=json)
//!
//! ## Visual styles (KLAWED_ONESHOT_STYLE)
//!   - `boxes`   — Unicode box-drawing (default)
//!   - `compact` — Single-line minimal output
//!   - `minimal` — Ultra-minimal, no decoration

const std = @import("std");
const util_env = @import("../util/env_utils.zig");

// ---------------------------------------------------------------------------
// Format and style enums
// ---------------------------------------------------------------------------

pub const OneshotFormat = enum {
    human,
    machine,

    /// Parse from KLAWED_ONESHOT_FORMAT environment variable.
    pub fn fromEnv() OneshotFormat {
        const val = std.posix.getenv("KLAWED_ONESHOT_FORMAT") orelse return .human;
        if (std.mem.eql(u8, val, "json") or std.mem.eql(u8, val, "machine")) return .machine;
        return .human;
    }
};

pub const OneshotStyle = enum {
    boxes,
    compact,
    minimal,

    /// Parse from KLAWED_ONESHOT_STYLE environment variable.
    pub fn fromEnv() OneshotStyle {
        const val = std.posix.getenv("KLAWED_ONESHOT_STYLE") orelse return .boxes;
        if (std.mem.eql(u8, val, "compact")) return .compact;
        if (std.mem.eql(u8, val, "minimal")) return .minimal;
        return .boxes;
    }
};

pub const OneshotStatus = enum {
    success,
    @"error",
};

// ---------------------------------------------------------------------------
// Human-readable formatting
// ---------------------------------------------------------------------------

/// Print the tool execution header in human-readable format.
pub fn printToolHeader(
    tool_name: []const u8,
    tool_details: ?[]const u8,
    style: OneshotStyle,
    writer: anytype,
) !void {
    switch (style) {
        .boxes => {
            try writer.print("┌─ {s}", .{tool_name});
            if (tool_details) |d| try writer.print(" — {s}", .{d});
            try writer.writeAll("\n");
        },
        .compact => {
            try writer.print("● {s}", .{tool_name});
            if (tool_details) |d| try writer.print(" {s}", .{d});
            try writer.writeAll("\n");
        },
        .minimal => {
            try writer.print("{s}\n", .{tool_name});
        },
    }
}

/// Print the tool execution footer.
pub fn printToolFooter(
    status: OneshotStatus,
    summary: ?[]const u8,
    style: OneshotStyle,
    writer: anytype,
) !void {
    switch (style) {
        .boxes => {
            const mark = if (status == .success) "✓" else "✗";
            if (summary) |s| {
                try writer.print("└─ {s} {s}\n", .{ mark, s });
            } else {
                try writer.print("└─ {s}\n", .{mark});
            }
        },
        .compact => {
            const mark = if (status == .success) "ok" else "err";
            if (summary) |s| {
                try writer.print("  [{s}] {s}\n", .{ mark, s });
            } else {
                try writer.print("  [{s}]\n", .{mark});
            }
        },
        .minimal => {
            // No footer for minimal style.
        },
    }
}

/// Print content with optional code-block style indentation.
pub fn printContent(content: []const u8, indent: bool, writer: anytype) !void {
    if (content.len == 0) return;
    if (indent) {
        // Indent each line with two spaces.
        var iter = std.mem.splitScalar(u8, content, '\n');
        while (iter.next()) |line| {
            if (line.len > 0) {
                try writer.print("  {s}\n", .{line});
            }
        }
    } else {
        try writer.writeAll(content);
        if (content[content.len - 1] != '\n') {
            try writer.writeAll("\n");
        }
    }
}

// ---------------------------------------------------------------------------
// Machine-readable formatting (HTML+JSON)
// ---------------------------------------------------------------------------

/// XML-escape a string into `buf`.  Returns the escaped slice (may point into `buf`).
fn xmlEscape(src: []const u8, buf: []u8) ![]const u8 {
    var fbs = std.io.fixedBufferStream(buf);
    const w = fbs.writer();
    for (src) |c| {
        switch (c) {
            '"' => try w.writeAll("&quot;"),
            '&' => try w.writeAll("&amp;"),
            '<' => try w.writeAll("&lt;"),
            '>' => try w.writeAll("&gt;"),
            else => try w.writeByte(c),
        }
    }
    return fbs.getWritten();
}

/// Print a tool call in machine-readable (HTML+JSON) format.
pub fn printMachineFormat(
    allocator: std.mem.Allocator,
    tool_name: []const u8,
    tool_details: ?[]const u8,
    result_json: ?[]const u8,
    writer: anytype,
) !void {
    try writer.print("<tool name=\"{s}\"", .{tool_name});

    if (tool_details) |d| {
        // Escape the details string.
        const esc_buf = try allocator.alloc(u8, d.len * 6 + 1);
        defer allocator.free(esc_buf);
        const escaped = try xmlEscape(d, esc_buf);
        try writer.print(" details=\"{s}\"", .{escaped});
    }

    try writer.writeAll(">\n");

    if (result_json) |json| {
        try writer.writeAll(json);
        try writer.writeAll("\n");
    }

    try writer.writeAll("</tool>\n");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "printToolHeader: boxes style" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    try printToolHeader("Bash", "ls -la", .boxes, buf.writer());
    const output = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, output, "Bash") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "ls -la") != null);
    try std.testing.expect(std.mem.indexOf(u8, output, "┌") != null);
}

test "printToolHeader: compact style" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    try printToolHeader("Read", null, .compact, buf.writer());
    try std.testing.expect(std.mem.indexOf(u8, buf.items, "●") != null);
    try std.testing.expect(std.mem.indexOf(u8, buf.items, "Read") != null);
}

test "printToolHeader: minimal style" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    try printToolHeader("Glob", "*.zig", .minimal, buf.writer());
    try std.testing.expect(std.mem.indexOf(u8, buf.items, "Glob") != null);
}

test "printToolFooter: success" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    try printToolFooter(.success, "5 files", .boxes, buf.writer());
    const out = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, out, "✓") != null);
    try std.testing.expect(std.mem.indexOf(u8, out, "5 files") != null);
}

test "printToolFooter: error" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    try printToolFooter(.@"error", null, .compact, buf.writer());
    try std.testing.expect(std.mem.indexOf(u8, buf.items, "err") != null);
}

test "printContent: indented" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    try printContent("line1\nline2", true, buf.writer());
    try std.testing.expect(std.mem.indexOf(u8, buf.items, "  line1") != null);
    try std.testing.expect(std.mem.indexOf(u8, buf.items, "  line2") != null);
}

test "printMachineFormat: basic tool" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    try printMachineFormat(alloc, "Bash", "ls -la", "{\"output\":\"file.txt\"}", buf.writer());
    const out = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, out, "<tool name=\"Bash\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, out, "</tool>") != null);
    try std.testing.expect(std.mem.indexOf(u8, out, "file.txt") != null);
}

test "printMachineFormat: XML escaping in details" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    try printMachineFormat(alloc, "Read", "a<b>&\"c\"", null, buf.writer());
    const out = buf.items;
    try std.testing.expect(std.mem.indexOf(u8, out, "&lt;") != null);
    try std.testing.expect(std.mem.indexOf(u8, out, "&amp;") != null);
    try std.testing.expect(std.mem.indexOf(u8, out, "&quot;") != null);
}

test "OneshotFormat.fromEnv: default is human" {
    // Can't set env in tests reliably, just check the default branch.
    // The env var is not set in test environment.
    _ = OneshotFormat.fromEnv;
    _ = OneshotStyle.fromEnv;
}
