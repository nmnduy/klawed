//! tools/bash.zig — Bash command execution tool
//!
//! Zig port of src/tools/tool_bash.c
//!
//! Executes shell commands with:
//!   - Configurable timeout (parameter > env var > default 30s)
//!   - Combined stdout+stderr capture (via `2>&1` shell wrapper)
//!   - Output truncation at bash_output_max bytes
//!   - ANSI escape stripping on output
//!
//! Timeout mechanism: a background thread sends SIGKILL to the child's
//! process group after the deadline, then sets a flag so the parent knows
//! the child was killed.

const std = @import("std");
const utils = @import("utils.zig");

pub const ToolResult = utils.ToolResult;

/// Maximum captured output bytes before truncation.
pub const bash_output_max: usize = 12_228;

/// Default timeout in seconds.
pub const default_timeout_s: u32 = 30;

// ---------------------------------------------------------------------------
// Inline ANSI stripping (avoids cross-directory import issues in standalone tests)
// ---------------------------------------------------------------------------

/// Remove ANSI/VT100 escape sequences from a byte slice.
/// Returns an allocated copy. Caller must free.
fn stripAnsi(allocator: std.mem.Allocator, input: []const u8) ![]u8 {
    var out = std.ArrayList(u8).init(allocator);
    defer out.deinit();
    var i: usize = 0;
    while (i < input.len) {
        if (input[i] == 0x1B) {
            i += 1;
            if (i < input.len and input[i] == '[') {
                // CSI sequence: skip until a byte in range 0x40–0x7E
                i += 1;
                while (i < input.len and (input[i] < 0x40 or input[i] > 0x7E)) i += 1;
                if (i < input.len) i += 1;
            } else {
                if (i < input.len) i += 1;
            }
        } else {
            try out.append(input[i]);
            i += 1;
        }
    }
    return out.toOwnedSlice();
}

// ---------------------------------------------------------------------------
// Timeout-killer thread
// ---------------------------------------------------------------------------

const KillerArgs = struct {
    pid: std.posix.pid_t,
    timeout_ns: u64,
    timed_out: *std.atomic.Value(bool),
};

fn killerThread(args: *KillerArgs) void {
    std.time.sleep(args.timeout_ns);
    // Send SIGKILL to the process group
    const pgrp = -args.pid;
    _ = std.os.linux.kill(pgrp, std.posix.SIG.KILL);
    args.timed_out.store(true, .seq_cst);
}

// ---------------------------------------------------------------------------
// Command result
// ---------------------------------------------------------------------------

/// Result of running a subprocess.
pub const CommandResult = struct {
    output: []u8, // owned; caller must free
    exit_code: u8,
    timed_out: bool,
};

/// Run `cmd` via `/bin/sh -c` with a timeout.
/// The shell command should already include `2>&1` to merge streams.
pub fn runCommand(
    allocator: std.mem.Allocator,
    cmd: []const u8,
    timeout_s: u32,
    max_output: usize,
) !CommandResult {
    const argv = [_][]const u8{ "/bin/sh", "-c", cmd };

    var child = std.process.Child.init(&argv, allocator);
    child.stdout_behavior = .Pipe;
    child.stderr_behavior = .Ignore; // stderr already merged via 2>&1 in cmd
    child.stdin_behavior = .Close;

    try child.spawn();

    // Launch a killer thread that sends SIGKILL after timeout
    var timed_out_flag = std.atomic.Value(bool).init(false);
    var killer_args = KillerArgs{
        .pid = child.id,
        .timeout_ns = @as(u64, timeout_s) * std.time.ns_per_s,
        .timed_out = &timed_out_flag,
    };
    const killer = try std.Thread.spawn(.{}, killerThread, .{&killer_args});
    // Detach: we don't need to join — it will exit on its own after sleeping
    killer.detach();

    // Read output
    var stdout_buf = std.ArrayList(u8).init(allocator);
    defer stdout_buf.deinit();

    if (child.stdout) |stdout_pipe| {
        var buf: [4096]u8 = undefined;
        while (true) {
            const n = stdout_pipe.read(&buf) catch break;
            if (n == 0) break;
            const to_append = @min(n, max_output -| stdout_buf.items.len);
            if (to_append > 0) try stdout_buf.appendSlice(buf[0..to_append]);
        }
    }

    const term = child.wait() catch std.process.Child.Term{ .Exited = 1 };
    const exit_code: u8 = switch (term) {
        .Exited => |code| @truncate(code),
        else => 1,
    };

    const was_timed_out = timed_out_flag.load(.seq_cst);

    return CommandResult{
        .output = try stdout_buf.toOwnedSlice(),
        .exit_code = exit_code,
        .timed_out = was_timed_out,
    };
}

// ---------------------------------------------------------------------------
// JSON string writer
// ---------------------------------------------------------------------------

/// Write `s` as a JSON-encoded string (with surrounding double-quotes).
fn writeJsonString(writer: anytype, s: []const u8) !void {
    try writer.writeByte('"');
    for (s) |c| {
        switch (c) {
            '"' => try writer.writeAll("\\\""),
            '\\' => try writer.writeAll("\\\\"),
            '\n' => try writer.writeAll("\\n"),
            '\r' => try writer.writeAll("\\r"),
            '\t' => try writer.writeAll("\\t"),
            0x00...0x08, 0x0B...0x0C, 0x0E...0x1F => try std.fmt.format(writer, "\\u{X:0>4}", .{c}),
            else => try writer.writeByte(c),
        }
    }
    try writer.writeByte('"');
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

/// Execute the Bash tool.
///
/// Expected input JSON:
/// ```json
/// { "command": "<shell command>", "timeout": <optional_seconds> }
/// ```
pub fn execute(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const command = utils.jsonString(input, "command") orelse {
        return utils.errLit("Missing 'command' parameter");
    };

    if (command.len == 0) {
        return utils.errLit("Command cannot be empty");
    }

    // Determine timeout: parameter > env > default
    const timeout_s: u32 = blk: {
        if (utils.jsonInt(input, "timeout")) |t| {
            if (t > 0) break :blk @intCast(@min(t, 999_999));
        }
        if (std.posix.getenv("KLAWED_BASH_TIMEOUT")) |env| {
            const v = std.fmt.parseInt(u32, env, 10) catch 0;
            if (v > 0) break :blk v;
        }
        break :blk default_timeout_s;
    };

    // Strip trailing whitespace (mirrors C behavior)
    const trimmed_cmd = std.mem.trimRight(u8, command, &std.ascii.whitespace);

    // Wrap to merge stderr and close stdin
    const full_cmd = try std.fmt.allocPrint(
        allocator,
        "{s} </dev/null 2>&1",
        .{trimmed_cmd},
    );
    defer allocator.free(full_cmd);

    const cmd_result = try runCommand(allocator, full_cmd, timeout_s, bash_output_max);
    defer allocator.free(cmd_result.output);

    // Strip ANSI escape sequences
    const clean = try stripAnsi(allocator, cmd_result.output);
    defer allocator.free(clean);

    const truncated = cmd_result.output.len >= bash_output_max;

    // Build JSON result
    var out = std.ArrayList(u8).init(allocator);
    defer out.deinit();
    const writer = out.writer();

    try writer.writeAll("{\"exit_code\":");
    try std.fmt.format(writer, "{d}", .{cmd_result.exit_code});
    try writer.writeAll(",\"output\":");
    try writeJsonString(writer, clean);

    if (cmd_result.timed_out) {
        const msg = try std.fmt.allocPrint(
            allocator,
            "Command timed out after {d} seconds. Use KLAWED_BASH_TIMEOUT to adjust timeout.",
            .{timeout_s},
        );
        defer allocator.free(msg);
        try writer.writeAll(",\"timeout_error\":");
        try writeJsonString(writer, msg);
    }

    if (truncated) {
        const msg = try std.fmt.allocPrint(
            allocator,
            "Command output was truncated at {d} bytes (maximum: {d} bytes).",
            .{ cmd_result.output.len, bash_output_max },
        );
        defer allocator.free(msg);
        try writer.writeAll(",\"truncation_warning\":");
        try writeJsonString(writer, msg);
    }

    try writer.writeByte('}');

    return utils.okOwned(try out.toOwnedSlice());
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "bash tool: missing command returns error" {
    const allocator = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{}", .{});
    defer parsed.deinit();

    const result = try execute(allocator, parsed.value);
    try std.testing.expect(result.is_error);
}

test "bash tool: echo command returns output" {
    const allocator = std.testing.allocator;
    const json_text =
        \\{"command": "/bin/echo hello"}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try execute(allocator, parsed.value);
    defer result.deinit(allocator);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "hello") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "exit_code") != null);
}

test "bash tool: non-zero exit code captured" {
    const allocator = std.testing.allocator;
    const json_text =
        \\{"command": "exit 42"}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try execute(allocator, parsed.value);
    defer result.deinit(allocator);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "42") != null);
}

test "bash tool: short timeout kills process" {
    const allocator = std.testing.allocator;
    const json_text =
        \\{"command": "sleep 10", "timeout": 1}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try execute(allocator, parsed.value);
    defer result.deinit(allocator);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "timeout_error") != null);
}

test "bash tool: output truncation" {
    const allocator = std.testing.allocator;
    // Generate more output than bash_output_max
    const json_text =
        \\{"command": "python3 -c \"print('A' * 20000)\""}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try execute(allocator, parsed.value);
    defer result.deinit(allocator);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "truncation_warning") != null);
}

test "writeJsonString: escapes special characters" {
    var buf = std.ArrayList(u8).init(std.testing.allocator);
    defer buf.deinit();
    try writeJsonString(buf.writer(), "a\"b\\c\nd");
    try std.testing.expectEqualStrings("\"a\\\"b\\\\c\\nd\"", buf.items);
}
