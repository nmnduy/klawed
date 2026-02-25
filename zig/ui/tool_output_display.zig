//! Tool Output Display module
//!
//! Idiomatic Zig replacement for src/ui/tool_output_display.c and src/ui/tool_output_display.h
//!
//! Provides formatted display of tool execution output.

const std = @import("std");
const logger = @import("../logger.zig");
const print_helpers = @import("print_helpers.zig");

/// Tool output type
pub const ToolOutputType = enum {
    text,
    json,
    diff,
    err,
    info,
};

/// Tool output display options
pub const ToolOutputOptions = struct {
    max_lines: usize = 100,
    show_line_numbers: bool = false,

    pub fn getTruncateIndicator() []const u8 {
        return "\n... (truncated)";
    }
};

/// Display tool output with formatting
pub fn displayToolOutput(
    allocator: std.mem.Allocator,
    writer: anytype,
    tool_name: []const u8,
    output: []const u8,
    output_type: ToolOutputType,
    options: ToolOutputOptions,
) !void {
    // Print tool header
    try writer.writeAll("\n┌─ ");
    try writer.writeAll(tool_name);
    try writer.writeAll(" ─");

    // Fill to width (assume 80 for now)
    const header_len = tool_name.len + 6;
    const fill_len = if (header_len < 78) 78 - header_len else 0;
    var i: usize = 0;
    while (i < fill_len) : (i += 1) {
        try writer.writeByte('─');
    }
    try writer.writeAll("\n");

    // Print output based on type
    switch (output_type) {
        .text => try displayTextOutput(writer, output, options),
        .json => try displayJsonOutput(writer, output, options),
        .diff => try displayDiffOutput(writer, output, options),
        .err => try displayErrorOutput(writer, output, options),
        .info => try displayInfoOutput(writer, output, options),
    }

    // Print footer
    try writer.writeAll("└");
    i = 0;
    while (i < 78) : (i += 1) {
        try writer.writeByte('─');
    }
    try writer.writeAll("\n");
}

/// Display plain text output
fn displayTextOutput(writer: anytype, output: []const u8, options: ToolOutputOptions) !void {
    _ = options;
    try writer.writeAll("│ ");
    // Indent each line
    var lines = std.mem.splitScalar(u8, output, '\n');
    var first = true;
    while (lines.next()) |line| {
        if (!first) try writer.writeAll("\n│ ");
        try writer.writeAll(line);
        first = false;
    }
    try writer.writeAll("\n");
}

/// Display JSON output with formatting
fn displayJsonOutput(writer: anytype, output: []const u8, options: ToolOutputOptions) !void {
    _ = options;
    // For now, just display as text
    // TODO: Add syntax highlighting
    try displayTextOutput(writer, output, options);
}

/// Display diff output with colors
fn displayDiffOutput(writer: anytype, output: []const u8, options: ToolOutputOptions) !void {
    _ = options;
    var lines = std.mem.splitScalar(u8, output, '\n');
    while (lines.next()) |line| {
        if (line.len == 0) continue;

        const prefix = line[0];
        switch (prefix) {
            '+' => {
                // Addition - green
                try writer.writeAll("│ \x1b[32m");
                try writer.writeAll(line);
                try writer.writeAll("\x1b[0m\n");
            },
            '-' => {
                // Deletion - red
                try writer.writeAll("│ \x1b[31m");
                try writer.writeAll(line);
                try writer.writeAll("\x1b[0m\n");
            },
            '@' => {
                // Header - cyan
                try writer.writeAll("│ \x1b[36m");
                try writer.writeAll(line);
                try writer.writeAll("\x1b[0m\n");
            },
            else => {
                try writer.writeAll("│ ");
                try writer.writeAll(line);
                try writer.writeAll("\n");
            },
        }
    }
}

/// Display error output
fn displayErrorOutput(writer: anytype, output: []const u8, options: ToolOutputOptions) !void {
    _ = options;
    try writer.writeAll("│ \x1b[31m");
    try displayTextOutput(writer, output, options);
    try writer.writeAll("\x1b[0m");
}

/// Display info output
fn displayInfoOutput(writer: anytype, output: []const u8, options: ToolOutputOptions) !void {
    _ = options;
    try writer.writeAll("│ \x1b[36m");
    try displayTextOutput(writer, output, options);
    try writer.writeAll("\x1b[0m");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "displayToolOutput: basic text output" {
    const output = "Line 1\nLine 2\nLine 3";
    var buf: [1024]u8 = undefined;
    var fbs = std.io.fixedBufferStream(&buf);

    try displayToolOutput(
        std.testing.allocator,
        fbs.writer(),
        "Bash",
        output,
        .text,
        .{},
    );

    const result = fbs.getWritten();
    try std.testing.expect(std.mem.indexOf(u8, result, "Bash") != null);
    try std.testing.expect(std.mem.indexOf(u8, result, "Line 1") != null);
}
