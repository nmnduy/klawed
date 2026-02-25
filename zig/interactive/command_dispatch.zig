//! interactive/command_dispatch.zig — Vim-style command dispatch
//!
//! Zig port of src/interactive/command_dispatch.c.
//!
//! Handles `:q`, `:quit`, `:clear`, `:help`, `:!<cmd>` (shell escape),
//! `:re !<cmd>` (read command output into input buffer), `:vim`, `:git`.
//!
//! In Phase 8, TUI functions are stubbed out.  Phase 9 will wire in the
//! ncurses TUI.

const std = @import("std");

// ---------------------------------------------------------------------------
// DispatchResult
// ---------------------------------------------------------------------------

pub const VimCommandResult = enum {
    /// Continue running.
    @"continue",
    /// Exit the application.
    exit,
    /// Command inserted output into the input buffer.
    insert_into_input,
};

// ---------------------------------------------------------------------------
// Vim-command handler
// ---------------------------------------------------------------------------

/// Dispatch a vim-style colon command (input starts with ':').
///
/// Returns `VimCommandResult` indicating what the caller should do.
/// `output_buf` (if non-null) receives the output for `:re !<cmd>`.
pub fn handleVimCommand(
    allocator: std.mem.Allocator,
    command: []const u8,
    output_buf: ?*std.ArrayList(u8),
) !VimCommandResult {
    if (command.len == 0 or command[0] != ':') return .@"continue";

    const cmd = std.mem.trimRight(u8, command[1..], " \t\r\n");

    // Quit commands.
    if (std.mem.eql(u8, cmd, "q") or
        std.mem.eql(u8, cmd, "quit") or
        std.mem.eql(u8, cmd, "wq"))
    {
        return .exit;
    }

    // Clear command.
    if (std.mem.eql(u8, cmd, "clear")) {
        const stdout = std.io.getStdOut().writer();
        try stdout.writeAll("[Status] Conversation cleared\n");
        return .@"continue";
    }

    // Help command.
    if (std.mem.eql(u8, cmd, "help")) {
        const stdout = std.io.getStdOut().writer();
        try stdout.writeAll("Vim-style commands:\n");
        try stdout.writeAll("  :q, :quit, :wq  - Exit\n");
        try stdout.writeAll("  :clear          - Clear conversation\n");
        try stdout.writeAll("  :!<cmd>         - Execute shell command\n");
        try stdout.writeAll("  :re !<cmd>      - Read command output into input\n");
        try stdout.writeAll("  :vim            - Open vim\n");
        try stdout.writeAll("  :help           - Show this help\n");
        return .@"continue";
    }

    // Shell escape: :!<cmd>
    if (cmd.len > 0 and cmd[0] == '!') {
        const shell_cmd = std.mem.trimLeft(u8, cmd[1..], " \t");
        if (shell_cmd.len == 0) {
            const stderr = std.io.getStdErr().writer();
            try stderr.writeAll("[Error] No command specified after :!\n");
            return .@"continue";
        }

        const result = try std.process.Child.run(.{
            .allocator = allocator,
            .argv = &.{ "/bin/sh", "-c", shell_cmd },
        });
        defer allocator.free(result.stdout);
        defer allocator.free(result.stderr);

        const stdout = std.io.getStdOut().writer();
        if (result.stdout.len > 0) try stdout.writeAll(result.stdout);
        if (result.stderr.len > 0) try stdout.writeAll(result.stderr);
        try stdout.writeAll("\nPress ENTER to continue...");
        // In non-TUI mode, just continue.
        return .@"continue";
    }

    // Read output into input: :re !<cmd>
    if (std.mem.startsWith(u8, cmd, "re !")) {
        const shell_cmd = std.mem.trimLeft(u8, cmd[4..], " \t");
        if (shell_cmd.len == 0) return .@"continue";

        const result = try std.process.Child.run(.{
            .allocator = allocator,
            .argv = &.{ "/bin/sh", "-c", shell_cmd },
        });
        defer allocator.free(result.stdout);
        defer allocator.free(result.stderr);

        if (output_buf) |buf| {
            var out = result.stdout;
            // Strip trailing newline.
            if (out.len > 0 and out[out.len - 1] == '\n') out = out[0 .. out.len - 1];
            try buf.appendSlice(out);
        }

        return .insert_into_input;
    }

    // :vim shortcut.
    if (std.mem.eql(u8, cmd, "vim")) {
        _ = std.process.Child.run(.{
            .allocator = allocator,
            .argv = &.{"vim"},
        }) catch {};
        return .@"continue";
    }

    // Unknown command.
    const stderr = std.io.getStdErr().writer();
    try stderr.print("[Error] Unknown vim command: {s}\n", .{cmd});
    return .@"continue";
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "handleVimCommand: :q returns exit" {
    const alloc = std.testing.allocator;
    const result = try handleVimCommand(alloc, ":q", null);
    try std.testing.expectEqual(VimCommandResult.exit, result);
}

test "handleVimCommand: :quit returns exit" {
    const alloc = std.testing.allocator;
    const result = try handleVimCommand(alloc, ":quit", null);
    try std.testing.expectEqual(VimCommandResult.exit, result);
}

test "handleVimCommand: :wq returns exit" {
    const alloc = std.testing.allocator;
    const result = try handleVimCommand(alloc, ":wq", null);
    try std.testing.expectEqual(VimCommandResult.exit, result);
}

test "handleVimCommand: :clear returns continue" {
    const alloc = std.testing.allocator;
    const result = try handleVimCommand(alloc, ":clear", null);
    try std.testing.expectEqual(VimCommandResult.@"continue", result);
}

test "handleVimCommand: :help returns continue" {
    const alloc = std.testing.allocator;
    const result = try handleVimCommand(alloc, ":help", null);
    try std.testing.expectEqual(VimCommandResult.@"continue", result);
}

test "handleVimCommand: :! echo hello" {
    const alloc = std.testing.allocator;
    const result = try handleVimCommand(alloc, ":!echo hello", null);
    try std.testing.expectEqual(VimCommandResult.@"continue", result);
}

test "handleVimCommand: :re !echo captures output" {
    const alloc = std.testing.allocator;
    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();

    const result = try handleVimCommand(alloc, ":re !echo captured", &buf);
    try std.testing.expectEqual(VimCommandResult.insert_into_input, result);
    try std.testing.expect(std.mem.indexOf(u8, buf.items, "captured") != null);
}

test "handleVimCommand: unknown command returns continue" {
    const alloc = std.testing.allocator;
    const result = try handleVimCommand(alloc, ":zzznope", null);
    try std.testing.expectEqual(VimCommandResult.@"continue", result);
}

test "handleVimCommand: empty colon returns continue" {
    const alloc = std.testing.allocator;
    const result = try handleVimCommand(alloc, ":", null);
    try std.testing.expectEqual(VimCommandResult.@"continue", result);
}
