//! interactive/interactive_loop.zig — Interactive REPL loop (non-TUI)
//!
//! Zig port of the core logic from src/interactive/interactive_loop.c and
//! src/interactive/input_handler.c.
//!
//! In Phase 8, this is a simple readline loop without ncurses.
//! Phase 9 will replace the stdin reader with the TUI event loop.
//!
//! Flow per turn:
//!   1. Read a line from stdin (prompt displayed).
//!   2. If the line starts with ':' → vim-style command dispatch.
//!   3. If the line starts with '/' → slash command dispatch.
//!   4. Otherwise → add as user message and call the AI callback.

const std = @import("std");
const input_handler = @import("input_handler.zig");
const command_dispatch = @import("command_dispatch.zig");
const commands_mod = @import("../commands.zig");

pub const InputResult = input_handler.InputResult;
pub const ReadLineResult = input_handler.ReadLineResult;

// ---------------------------------------------------------------------------
// Loop context
// ---------------------------------------------------------------------------

/// Callback invoked when the user submits a non-command message.
/// Return `false` to stop the loop.
pub const SubmitFn = *const fn (
    allocator: std.mem.Allocator,
    text: []const u8,
    ctx: ?*anyopaque,
) anyerror!bool;

pub const LoopConfig = struct {
    /// Prompt string printed before each input line.
    prompt: []const u8 = ">>> ",
    /// Whether to print "[User] <line>" echo.
    echo_input: bool = true,
    /// Command registry for slash commands.
    command_registry: ?*const commands_mod.CommandRegistry = null,
};

// ---------------------------------------------------------------------------
// Run the interactive loop
// ---------------------------------------------------------------------------

/// Run the interactive REPL loop until EOF or the user exits.
///
/// `submit_fn` is called for every non-command message submitted by the user.
/// Returns when the loop exits (EOF, Ctrl+D, `/exit`, `:q`).
pub fn runInteractiveLoop(
    allocator: std.mem.Allocator,
    cfg: LoopConfig,
    submit_fn: SubmitFn,
    submit_ctx: ?*anyopaque,
) !void {
    while (true) {
        const rr = try input_handler.readLine(allocator, cfg.prompt);
        switch (rr.kind) {
            .eof => break,
            .interrupt => continue,
            .input => {
                const text = rr.text orelse continue;
                defer allocator.free(text);

                const trimmed = std.mem.trim(u8, text, " \t\r\n");
                if (trimmed.len == 0) continue;

                // Vim-style colon command.
                if (trimmed[0] == ':') {
                    const result = try command_dispatch.handleVimCommand(
                        allocator,
                        trimmed,
                        null,
                    );
                    if (result == .exit) break;
                    continue;
                }

                // Slash command.
                if (trimmed[0] == '/') {
                    if (cfg.command_registry) |reg| {
                        const ctx = commands_mod.CommandContext{};
                        const result = reg.execute(ctx, trimmed);
                        if (result.kind == .exit) break;
                        if (result.message) |msg| {
                            const stdout = std.io.getStdOut().writer();
                            try stdout.print("[Status] {s}\n", .{msg});
                        }
                    }
                    continue;
                }

                // Normal user message.
                if (cfg.echo_input) {
                    const stdout = std.io.getStdOut().writer();
                    try stdout.print("[User] {s}\n", .{trimmed});
                }

                const should_continue = try submit_fn(allocator, trimmed, submit_ctx);
                if (!should_continue) break;
            },
        }
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "interactive loop: empty registry dispatch" {
    // Just compile-check the types.
    const alloc = std.testing.allocator;
    _ = alloc;

    // Verify LoopConfig defaults work.
    const cfg = LoopConfig{};
    try std.testing.expectEqualStrings(">>> ", cfg.prompt);
    try std.testing.expect(cfg.echo_input);
    try std.testing.expectEqual(@as(?*const commands_mod.CommandRegistry, null), cfg.command_registry);
}

test "interactive loop: SubmitFn type check" {
    // Verify we can construct a valid SubmitFn.
    const noop: SubmitFn = struct {
        fn f(_: std.mem.Allocator, _: []const u8, _: ?*anyopaque) !bool {
            return true;
        }
    }.f;
    _ = noop;
}
