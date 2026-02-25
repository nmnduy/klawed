//! oneshot/processor.zig — One-shot mode response processor
//!
//! Zig port of src/oneshot/oneshot_processor.c.
//!
//! Runs the agent loop for one-shot (single-prompt) execution.
//! Recursively processes tool calls until the model returns a final
//! text-only response.

const std = @import("std");
const output_mod = @import("output.zig");
const response_proc = @import("../interactive/response_processor.zig");

pub const OneshotFormat = output_mod.OneshotFormat;
pub const OneshotStyle = output_mod.OneshotStyle;
pub const AssistantResponse = response_proc.AssistantResponse;
pub const ToolCall = response_proc.ToolCall;
pub const ToolResult = response_proc.ToolResult;
pub const ToolExecutorFn = response_proc.ToolExecutorFn;
pub const FollowupFn = response_proc.FollowupFn;

// ---------------------------------------------------------------------------
// OneshotProcessor
// ---------------------------------------------------------------------------

pub const OneshotOptions = struct {
    format: OneshotFormat = .human,
    style: OneshotStyle = .boxes,
    /// Maximum number of tool-call rounds.
    max_rounds: usize = 64,
};

/// Process a one-shot API response.
///
/// Executes tool calls (if any), formats output, and recurses until the
/// model produces a final text-only response.
///
/// Returns the exit code (0 for success, 1 for error).
pub fn processOneshotResponse(
    allocator: std.mem.Allocator,
    response: AssistantResponse,
    executor: ToolExecutorFn,
    follow_up: FollowupFn,
    exec_ctx: ?*anyopaque,
    opts: OneshotOptions,
) !i32 {
    const stdout = std.io.getStdOut().writer();
    return processOneshotResponseWriter(
        allocator,
        response,
        executor,
        follow_up,
        exec_ctx,
        opts,
        stdout.any(),
    );
}

/// Like `processOneshotResponse` but accepts an explicit `AnyWriter` for
/// output.  Tests pass an in-memory buffer writer to avoid writing to stdout.
pub fn processOneshotResponseWriter(
    allocator: std.mem.Allocator,
    response: AssistantResponse,
    executor: ToolExecutorFn,
    follow_up: FollowupFn,
    exec_ctx: ?*anyopaque,
    opts: OneshotOptions,
    writer: std.io.AnyWriter,
) !i32 {
    // Print text content.
    if (response.text.len > 0) {
        try writer.writeAll(response.text);
        if (response.text[response.text.len - 1] != '\n') {
            try writer.writeAll("\n");
        }
    }

    if (response.tool_calls.len == 0) return 0; // Done.

    var results_buf = std.ArrayList(ToolResult).init(allocator);
    defer {
        for (results_buf.items) |r| {
            allocator.free(r.result_json);
        }
        results_buf.deinit();
    }

    // Execute all tool calls and format output.
    for (response.tool_calls) |call| {
        const result = try executor(allocator, call, exec_ctx);
        try results_buf.append(result);

        if (opts.format == .machine) {
            try output_mod.printMachineFormat(
                allocator,
                call.name,
                call.params_json,
                result.result_json,
                writer,
            );
        } else {
            const has_error = result.is_error;
            try output_mod.printToolHeader(call.name, call.params_json, opts.style, writer);
            try output_mod.printContent(result.result_json, true, writer);
            try output_mod.printToolFooter(
                if (has_error) .@"error" else .success,
                null,
                opts.style,
                writer,
            );
        }
    }

    // Follow-up API call.
    const next_response = try follow_up(allocator, exec_ctx) orelse {
        return 1; // API call failed.
    };

    // Recurse.
    if (opts.max_rounds == 0) return 0;

    const next_opts = OneshotOptions{
        .format = opts.format,
        .style = opts.style,
        .max_rounds = opts.max_rounds - 1,
    };

    return processOneshotResponseWriter(
        allocator,
        next_response,
        executor,
        follow_up,
        exec_ctx,
        next_opts,
        writer,
    );
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "processOneshotResponse: text-only response exits 0" {
    const alloc = std.testing.allocator;

    const response = AssistantResponse{
        .text = "Task complete.",
        .tool_calls = &.{},
    };

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
    const exit_code = try processOneshotResponseWriter(
        alloc,
        response,
        exec,
        followup,
        null,
        .{ .format = .human, .style = .minimal },
        buf.writer().any(),
    );

    try std.testing.expectEqual(@as(i32, 0), exit_code);
}

test "processOneshotResponse: one tool call then done" {
    const alloc = std.testing.allocator;

    const tool_calls = [_]ToolCall{
        .{ .id = "tc1", .name = "Bash", .params_json = "{\"command\":\"ls\"}" },
    };

    const first = AssistantResponse{
        .text = "",
        .tool_calls = &tool_calls,
    };

    const exec: ToolExecutorFn = struct {
        fn e(a: std.mem.Allocator, call: ToolCall, _: ?*anyopaque) !ToolResult {
            return ToolResult{
                .tool_id = call.id,
                .tool_name = call.name,
                .result_json = try a.dupe(u8, "{\"output\":\"file.txt\"}"),
                .is_error = false,
            };
        }
    }.e;

    var round: usize = 0;
    const State = struct { r: *usize };
    var s = State{ .r = &round };

    const followup: FollowupFn = struct {
        fn f(_: std.mem.Allocator, ctx: ?*anyopaque) !?AssistantResponse {
            const st: *State = @ptrCast(@alignCast(ctx));
            st.r.* += 1;
            return AssistantResponse{ .text = "All done.", .tool_calls = &.{} };
        }
    }.f;

    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();
    const exit_code = try processOneshotResponseWriter(
        alloc,
        first,
        exec,
        followup,
        &s,
        .{ .format = .human, .style = .minimal },
        buf.writer().any(),
    );

    try std.testing.expectEqual(@as(i32, 0), exit_code);
    try std.testing.expectEqual(@as(usize, 1), round);
}

test "processOneshotResponse: machine format" {
    const alloc = std.testing.allocator;

    const tool_calls = [_]ToolCall{
        .{ .id = "tc2", .name = "Read", .params_json = "file.txt" },
    };

    const first = AssistantResponse{
        .text = "",
        .tool_calls = &tool_calls,
    };

    const exec: ToolExecutorFn = struct {
        fn e(a: std.mem.Allocator, call: ToolCall, _: ?*anyopaque) !ToolResult {
            return ToolResult{
                .tool_id = call.id,
                .tool_name = call.name,
                .result_json = try a.dupe(u8, "{\"content\":\"hello\"}"),
                .is_error = false,
            };
        }
    }.e;

    const followup: FollowupFn = struct {
        fn f(_: std.mem.Allocator, _: ?*anyopaque) !?AssistantResponse {
            return AssistantResponse{ .text = "done", .tool_calls = &.{} };
        }
    }.f;

    var buf = std.ArrayList(u8).init(alloc);
    defer buf.deinit();
    const exit_code = try processOneshotResponseWriter(
        alloc,
        first,
        exec,
        followup,
        null,
        .{ .format = .machine, .style = .minimal },
        buf.writer().any(),
    );

    try std.testing.expectEqual(@as(i32, 0), exit_code);
}
