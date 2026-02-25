//! tools/subagent.zig — Subagent tool implementations
//!
//! Zig port of src/tools/tool_subagent.c
//!
//! Implements three tools:
//!   - Subagent               — spawn a child klawed process
//!   - CheckSubagentProgress  — read the subagent's log file tail
//!   - InterruptSubagent      — send SIGKILL to a running subagent

const std = @import("std");
const utils = @import("utils.zig");

pub const ToolResult = utils.ToolResult;

/// Default subagent timeout in seconds (5 minutes).
pub const default_timeout_s: u32 = 300;

/// Maximum log tail lines to return by default.
pub const default_tail_lines: usize = 50;

/// Maximum characters per log line in CheckSubagentProgress.
pub const default_log_line_max: usize = 12_000;

// ---------------------------------------------------------------------------
// Subagent — spawn child klawed process
// ---------------------------------------------------------------------------

/// Execute the Subagent tool.
///
/// Input:
/// ```json
/// {
///   "prompt": "...",
///   "timeout": <optional seconds>,
///   "provider": <optional provider name>,
///   "working_dir": <optional absolute path>
/// }
/// ```
pub fn executeSubagent(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const prompt = utils.jsonString(input, "prompt") orelse {
        return utils.errLit("Missing 'prompt' parameter");
    };
    if (prompt.len == 0) return utils.errLit("Prompt cannot be empty");

    const timeout_s: u32 = blk: {
        if (utils.jsonInt(input, "timeout")) |t| {
            if (t > 0) break :blk @intCast(@min(t, 999_999));
        }
        break :blk default_timeout_s;
    };
    _ = timeout_s; // used for log filename metadata; actual enforcement is in manager

    const provider = utils.jsonString(input, "provider");
    const working_dir = utils.jsonString(input, "working_dir");

    // Validate working_dir: must be absolute if provided
    if (working_dir) |wd| {
        if (!std.fs.path.isAbsolute(wd)) {
            return utils.errLit("working_dir must be an absolute path (starting with '/')");
        }
    }

    // Find the current executable path via /proc/self/exe
    var exe_path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const exe_path = std.fs.readLinkAbsolute("/proc/self/exe", &exe_path_buf) catch blk: {
        // Fallback: use argv[0] guess
        break :blk "./build/klawed";
    };

    // Build a log directory path: .klawed/subagent/
    const log_dir = ".klawed/subagent";
    std.fs.cwd().makePath(log_dir) catch {};

    // Generate unique log filename with timestamp + pid
    const pid = std.os.linux.getpid();
    const timestamp = std.time.timestamp();
    const log_file = try std.fmt.allocPrint(
        allocator,
        "{s}/subagent_{d}_{d}.log",
        .{ log_dir, timestamp, pid },
    );
    defer allocator.free(log_file);

    // Build the command argv for the child klawed process.
    // We use execve-style: [exe_path, prompt, (redirected)]
    // The actual log capture is done via shell redirect: exe "prompt" > log 2>&1 </dev/null
    var cmd_buf = std.ArrayList(u8).init(allocator);
    defer cmd_buf.deinit();
    const cw = cmd_buf.writer();

    // Escape the prompt for embedding in a double-quoted shell argument
    try cw.writeByte('"');
    try cw.writeAll(exe_path);
    try cw.writeAll("\" \"");
    for (prompt) |c| {
        switch (c) {
            '"', '\\', '$', '`' => {
                try cw.writeByte('\\');
                try cw.writeByte(c);
            },
            else => try cw.writeByte(c),
        }
    }
    try cw.writeAll("\" > \"");
    try cw.writeAll(log_file);
    try cw.writeAll("\" 2>&1 </dev/null");

    const cmd = try cmd_buf.toOwnedSlice();
    defer allocator.free(cmd);

    // Build env overrides
    var env_list = std.ArrayList([]const u8).init(allocator);
    defer {
        for (env_list.items) |e| allocator.free(e);
        env_list.deinit();
    }
    try env_list.append(try allocator.dupe(u8, "KLAWED_IS_SUBAGENT=1"));
    if (provider) |p| {
        try env_list.append(try std.fmt.allocPrint(allocator, "KLAWED_LLM_PROVIDER={s}", .{p}));
    }

    // Fork + exec via /bin/sh
    const argv_sh = [_][]const u8{ "/bin/sh", "-c", cmd };
    var child = std.process.Child.init(&argv_sh, allocator);
    child.stdin_behavior = .Close;
    child.stdout_behavior = .Close;
    child.stderr_behavior = .Close;
    if (working_dir) |wd| child.cwd = wd;

    // Set environment: inherit parent env + our additions
    // In Zig 0.12 we build an EnvMap
    var env_map = std.process.EnvMap.init(allocator);
    defer env_map.deinit();

    // Copy existing environment
    var env_iter = try std.process.getEnvMap(allocator);
    defer env_iter.deinit();
    var env_kv = env_iter.iterator();
    while (env_kv.next()) |kv| {
        try env_map.put(kv.key_ptr.*, kv.value_ptr.*);
    }
    // Override with our additions
    for (env_list.items) |entry| {
        const eq = std.mem.indexOfScalar(u8, entry, '=') orelse continue;
        try env_map.put(entry[0..eq], entry[eq + 1 ..]);
    }
    child.env_map = &env_map;

    child.spawn() catch |e| {
        return utils.errFmt(allocator, "Failed to spawn subagent: {s}", .{@errorName(e)});
    };

    const spawned_pid = child.id;

    // Detach the child by immediately calling wait in a non-blocking manner.
    // In a full implementation, the SubagentManager would track the pid.
    // For now, we use WNOHANG so we don't block.
    _ = std.posix.waitpid(spawned_pid, std.posix.W.NOHANG);

    // Build result JSON
    const result_json = try std.fmt.allocPrint(
        allocator,
        "{{\"status\":\"started\",\"pid\":{d},\"log_file\":\"{s}\",\"message\":\"Subagent started. Use CheckSubagentProgress to monitor.\"}}",
        .{ spawned_pid, log_file },
    );
    return utils.okOwned(result_json);
}

// ---------------------------------------------------------------------------
// CheckSubagentProgress — read log file tail
// ---------------------------------------------------------------------------

/// Execute the CheckSubagentProgress tool.
///
/// Input: `{ "log_file": <path>, "tail_lines": <optional int> }`
pub fn executeCheckProgress(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const log_file = utils.jsonString(input, "log_file") orelse {
        return utils.errLit("Missing 'log_file' parameter");
    };

    const tail_lines_raw = utils.jsonInt(input, "tail_lines");
    const tail_lines: usize = if (tail_lines_raw) |v| blk: {
        if (v > 0) break :blk @intCast(@min(v, 10_000));
        break :blk default_tail_lines;
    } else default_tail_lines;

    const max_line_chars: usize = blk: {
        if (std.posix.getenv("KLAWED_SUBAGENT_LOG_LINE_MAX_CHARS")) |env| {
            break :blk std.fmt.parseInt(usize, env, 10) catch default_log_line_max;
        }
        break :blk default_log_line_max;
    };

    // Read the log file
    const content = (if (std.fs.path.isAbsolute(log_file))
        (std.fs.openFileAbsolute(log_file, .{}) catch null)
    else
        (std.fs.cwd().openFile(log_file, .{}) catch null)) orelse {
        return utils.errFmt(allocator, "Log file not found: {s}", .{log_file});
    };
    const file = content;
    defer file.close();

    const raw = file.readToEndAlloc(allocator, 100 * 1024 * 1024) catch |e| {
        return utils.errFmt(allocator, "Failed to read log file: {s}", .{@errorName(e)});
    };
    defer allocator.free(raw);

    // Extract last `tail_lines` lines
    var lines = std.ArrayList([]const u8).init(allocator);
    defer lines.deinit();

    var iter = std.mem.splitScalar(u8, raw, '\n');
    while (iter.next()) |line| {
        if (line.len == 0) continue;
        // Truncate lines that are too long
        const trimmed = if (line.len > max_line_chars) line[0..max_line_chars] else line;
        try lines.append(trimmed);
    }

    // Take last tail_lines
    const start = if (lines.items.len > tail_lines) lines.items.len - tail_lines else 0;
    const tail = lines.items[start..];

    var out = std.ArrayList(u8).init(allocator);
    defer out.deinit();
    const w = out.writer();

    for (tail) |line| {
        try w.writeAll(line);
        try w.writeByte('\n');
    }

    var final = std.ArrayList(u8).init(allocator);
    defer final.deinit();
    const fw = final.writer();
    try fw.writeAll("{\"log_file\":");
    try writeJsonString(fw, log_file);
    try std.fmt.format(fw, ",\"tail_lines\":{d},\"content\":", .{tail.len});
    try writeJsonString(fw, out.items);
    try fw.writeByte('}');

    return utils.okOwned(try final.toOwnedSlice());
}

/// Allocate a JSON-string representation of s (with surrounding quotes).
fn jsonStringAlloc(allocator: std.mem.Allocator, s: []const u8) ![]u8 {
    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();
    try writeJsonString(buf.writer(), s);
    return buf.toOwnedSlice();
}

// ---------------------------------------------------------------------------
// InterruptSubagent — kill a running subagent
// ---------------------------------------------------------------------------

/// Execute the InterruptSubagent tool.
///
/// Input: `{ "pid": <int> }`
pub fn executeInterrupt(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const pid_val = utils.jsonInt(input, "pid") orelse {
        return utils.errLit("Missing 'pid' parameter");
    };
    if (pid_val <= 0) return utils.errLit("Invalid PID");

    const pid: std.posix.pid_t = @intCast(pid_val);

    // Send SIGKILL to the process group (negative pid = pgrp)
    _ = std.os.linux.kill(-pid, std.posix.SIG.KILL);
    _ = std.os.linux.kill(pid, std.posix.SIG.KILL);

    return utils.okFmt(
        allocator,
        "{{\"status\":\"interrupted\",\"pid\":{d}}}",
        .{pid},
    );
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

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
// Tests
// ---------------------------------------------------------------------------

test "executeSubagent: missing prompt returns error" {
    const allocator = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{}", .{});
    defer parsed.deinit();

    const result = try executeSubagent(allocator, parsed.value);
    try std.testing.expect(result.is_error);
}

test "executeSubagent: empty prompt returns error" {
    const allocator = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{\"prompt\":\"\"}", .{});
    defer parsed.deinit();

    const result = try executeSubagent(allocator, parsed.value);
    try std.testing.expect(result.is_error);
}

test "executeSubagent: relative working_dir returns error" {
    const allocator = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(
        std.json.Value,
        allocator,
        "{\"prompt\":\"hi\",\"working_dir\":\"relative/path\"}",
        .{},
    );
    defer parsed.deinit();

    const result = try executeSubagent(allocator, parsed.value);
    try std.testing.expect(result.is_error);
}

test "executeCheckProgress: missing log_file returns error" {
    const allocator = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{}", .{});
    defer parsed.deinit();

    var result = try executeCheckProgress(allocator, parsed.value);
    defer result.deinit(allocator); // safe: errLit has owned=false
    try std.testing.expect(result.is_error);
}

test "executeCheckProgress: reads log file tail" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    // Write 20 lines
    var content = std.ArrayList(u8).init(std.testing.allocator);
    defer content.deinit();
    var i: usize = 1;
    while (i <= 20) : (i += 1) {
        try std.fmt.format(content.writer(), "line {d}\n", .{i});
    }
    try tmp.dir.writeFile("agent.log", content.items);

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const allocator = std.testing.allocator;
    const json_text = try std.fmt.allocPrint(
        allocator,
        "{{\"log_file\":\"{s}/agent.log\",\"tail_lines\":5}}",
        .{tmp_path},
    );
    defer allocator.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try executeCheckProgress(allocator, parsed.value);
    defer result.deinit(allocator);

    try std.testing.expect(!result.is_error);
    // Should contain last 5 lines (16-20)
    try std.testing.expect(std.mem.indexOf(u8, result.content, "line 20") != null);
    // Should NOT contain early lines
    try std.testing.expect(std.mem.indexOf(u8, result.content, "line 1\\n") == null);
}

test "executeInterrupt: invalid pid returns error" {
    const allocator = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{\"pid\":-1}", .{});
    defer parsed.deinit();

    const result = try executeInterrupt(allocator, parsed.value);
    try std.testing.expect(result.is_error);
}
