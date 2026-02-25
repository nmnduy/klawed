//! oneshot/mode.zig — One-shot mode entry point
//!
//! Zig port of src/oneshot/oneshot_mode.c.
//!
//! `executeOneshot` is the top-level function for single-command (non-interactive)
//! execution.  It:
//!   1. Adds the user prompt to conversation state.
//!   2. Calls the API (via callback).
//!   3. Processes the response (tool calls + follow-ups) via `processor.zig`.
//!   4. Prints token usage summary.
//!   5. Returns an exit code.

const std = @import("std");
const processor = @import("processor.zig");
const output_mod = @import("output.zig");

pub const OneshotFormat = output_mod.OneshotFormat;
pub const OneshotStyle = output_mod.OneshotStyle;
pub const AssistantResponse = processor.AssistantResponse;
pub const ToolCall = processor.ToolCall;
pub const ToolResult = processor.ToolResult;
pub const ToolExecutorFn = processor.ToolExecutorFn;
pub const FollowupFn = processor.FollowupFn;

// ---------------------------------------------------------------------------
// OneshotConfig
// ---------------------------------------------------------------------------

pub const OneshotConfig = struct {
    format: OneshotFormat = .human,
    style: OneshotStyle = .boxes,
    /// Maximum tool-call rounds.
    max_rounds: usize = 64,
};

/// Parse configuration from environment variables.
pub fn configFromEnv() OneshotConfig {
    return OneshotConfig{
        .format = OneshotFormat.fromEnv(),
        .style = OneshotStyle.fromEnv(),
    };
}

// ---------------------------------------------------------------------------
// API call callback type
// ---------------------------------------------------------------------------

/// Callback that performs the initial API call (takes the user prompt).
/// Returns an `AssistantResponse` or null on failure.
pub const InitialCallFn = *const fn (
    allocator: std.mem.Allocator,
    prompt: []const u8,
    ctx: ?*anyopaque,
) anyerror!?AssistantResponse;

// ---------------------------------------------------------------------------
// executeOneshot
// ---------------------------------------------------------------------------

/// Execute a single prompt in one-shot mode.
///
/// Parameters:
///   allocator    — memory allocator
///   prompt       — user prompt string
///   initial_call — callback to make the first API call with the prompt
///   executor     — callback to execute tool calls
///   follow_up    — callback to make follow-up API calls
///   api_ctx      — opaque context passed to all callbacks
///   cfg          — one-shot configuration
///
/// Returns 0 on success, 1 on failure.
pub fn executeOneshot(
    allocator: std.mem.Allocator,
    prompt: []const u8,
    initial_call: InitialCallFn,
    executor: ToolExecutorFn,
    follow_up: FollowupFn,
    api_ctx: ?*anyopaque,
    cfg: OneshotConfig,
) !i32 {
    const stdout = std.io.getStdOut().writer();
    return executeOneshotWriter(
        allocator,
        prompt,
        initial_call,
        executor,
        follow_up,
        api_ctx,
        cfg,
        stdout.any(),
    );
}

/// Like `executeOneshot` but accepts an explicit `AnyWriter` for output.
/// Use in tests to avoid writing to stdout.
pub fn executeOneshotWriter(
    allocator: std.mem.Allocator,
    prompt: []const u8,
    initial_call: InitialCallFn,
    executor: ToolExecutorFn,
    follow_up: FollowupFn,
    api_ctx: ?*anyopaque,
    cfg: OneshotConfig,
    writer: std.io.AnyWriter,
) !i32 {
    // Make the initial API call.
    const response = try initial_call(allocator, prompt, api_ctx) orelse {
        const stderr = std.io.getStdErr().writer();
        try stderr.writeAll("Error: Failed to get response from API\n");
        return 1;
    };

    // Process response (tool calls + follow-ups).
    const opts = processor.OneshotOptions{
        .format = cfg.format,
        .style = cfg.style,
        .max_rounds = cfg.max_rounds,
    };

    return processor.processOneshotResponseWriter(
        allocator,
        response,
        executor,
        follow_up,
        api_ctx,
        opts,
        writer,
    );
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "executeOneshot: simple text response" {
    const alloc = std.testing.allocator;

    const initial: InitialCallFn = struct {
        fn f(_: std.mem.Allocator, _: []const u8, _: ?*anyopaque) !?AssistantResponse {
            return AssistantResponse{
                .text = "I can help with that.",
                .tool_calls = &.{},
            };
        }
    }.f;

    const exec: ToolExecutorFn = struct {
        fn e(_: std.mem.Allocator, _: ToolCall, _: ?*anyopaque) !ToolResult {
            unreachable;
        }
    }.e;

    const followup: FollowupFn = struct {
        fn f(_: std.mem.Allocator, _: ?*anyopaque) !?AssistantResponse {
            unreachable;
        }
    }.f;

    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();
    const code = try executeOneshotWriter(
        alloc,
        "hello",
        initial,
        exec,
        followup,
        null,
        .{ .format = .human, .style = .minimal },
        buf.writer().any(),
    );

    try std.testing.expectEqual(@as(i32, 0), code);
}

test "executeOneshot: API failure returns 1" {
    const alloc = std.testing.allocator;

    const initial: InitialCallFn = struct {
        fn f(_: std.mem.Allocator, _: []const u8, _: ?*anyopaque) !?AssistantResponse {
            return null; // Simulate API failure.
        }
    }.f;

    const exec: ToolExecutorFn = struct {
        fn e(_: std.mem.Allocator, _: ToolCall, _: ?*anyopaque) !ToolResult {
            unreachable;
        }
    }.e;

    const followup: FollowupFn = struct {
        fn f(_: std.mem.Allocator, _: ?*anyopaque) !?AssistantResponse {
            unreachable;
        }
    }.f;

    const code = try executeOneshot(
        alloc,
        "do something",
        initial,
        exec,
        followup,
        null,
        .{},
    );

    try std.testing.expectEqual(@as(i32, 1), code);
}

test "configFromEnv: returns default config" {
    // Just verify it compiles and returns a config.
    const cfg = configFromEnv();
    _ = cfg.max_rounds;
}
