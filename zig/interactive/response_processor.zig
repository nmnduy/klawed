//! interactive/response_processor.zig — API response processing for the agent loop
//!
//! Zig port of src/interactive/response_processor.c (core logic, no ncurses).
//!
//! Handles the inner agent loop:
//!   1. Receive an API response.
//!   2. Print any text content to stdout.
//!   3. If the response contains tool calls, execute each one.
//!   4. Add tool results to conversation state.
//!   5. If any tool calls were executed, make a follow-up API call.
//!   6. Repeat until no more tool calls.
//!
//! In Phase 8 the tool execution and API calls are represented by function
//! pointers / callbacks so the module is testable without a live network.
//! Phase 9 will wire in the real tool registry and API client.

const std = @import("std");

// ---------------------------------------------------------------------------
// Tool call / result types (minimal for Phase 8)
// ---------------------------------------------------------------------------

pub const ToolCall = struct {
    id: []const u8,
    name: []const u8,
    /// JSON-encoded parameters string.
    params_json: []const u8,
};

pub const ToolResult = struct {
    tool_id: []const u8,
    tool_name: []const u8,
    /// JSON-encoded result string.
    result_json: []const u8,
    is_error: bool,
};

// ---------------------------------------------------------------------------
// Response representation
// ---------------------------------------------------------------------------

pub const AssistantResponse = struct {
    /// Text content from the model (may be empty).
    text: []const u8,
    /// Tool calls from the model (may be empty).
    tool_calls: []const ToolCall,
};

// ---------------------------------------------------------------------------
// Callbacks for the agent loop
// ---------------------------------------------------------------------------

/// Execute a single tool call.  Returns an owned `ToolResult`.
/// The caller frees `result.result_json` and string fields when done.
pub const ToolExecutorFn = *const fn (
    allocator: std.mem.Allocator,
    call: ToolCall,
    ctx: ?*anyopaque,
) anyerror!ToolResult;

/// Make a follow-up API call and return the next response.
/// Returns null if the call failed or was interrupted.
pub const FollowupFn = *const fn (
    allocator: std.mem.Allocator,
    ctx: ?*anyopaque,
) anyerror!?AssistantResponse;

// ---------------------------------------------------------------------------
// ProcessOptions
// ---------------------------------------------------------------------------

pub const ProcessOptions = struct {
    /// Print text responses to stdout.
    print_responses: bool = true,
    /// Print tool call names before executing them.
    print_tool_calls: bool = true,
    /// Maximum recursion depth (tool calls → follow-up API calls).
    max_depth: usize = 64,
};

// ---------------------------------------------------------------------------
// Response processor
// ---------------------------------------------------------------------------

/// Process one assistant response (and any resulting tool calls recursively).
///
/// `executor`  — function to execute a single tool call
/// `follow_up` — function to call the API after adding tool results
/// `exec_ctx`  — opaque context passed to both callbacks
///
/// Returns the number of tool call rounds executed.
pub fn processResponse(
    allocator: std.mem.Allocator,
    response: AssistantResponse,
    /// Caller-supplied buffer to accumulate tool results before follow-up call.
    results_buf: *std.ArrayList(ToolResult),
    executor: ToolExecutorFn,
    follow_up: FollowupFn,
    exec_ctx: ?*anyopaque,
    opts: ProcessOptions,
    depth: usize,
) !usize {
    // Print text content.
    if (opts.print_responses and response.text.len > 0) {
        const stdout = std.io.getStdOut().writer();
        try stdout.writeAll(response.text);
        if (response.text[response.text.len - 1] != '\n') {
            try stdout.writeAll("\n");
        }
    }

    if (response.tool_calls.len == 0) return 0;
    if (depth >= opts.max_depth) {
        const stderr = std.io.getStdErr().writer();
        try stderr.writeAll("[Warning] Max tool-call recursion depth reached\n");
        return 0;
    }

    var total_rounds: usize = 0;

    // Execute all tool calls.
    for (response.tool_calls) |call| {
        if (opts.print_tool_calls) {
            const stdout = std.io.getStdOut().writer();
            try stdout.print("● {s}\n", .{call.name});
        }

        const result = try executor(allocator, call, exec_ctx);
        try results_buf.append(result);
    }
    total_rounds += 1;

    // Make a follow-up API call with the tool results.
    const next_response = try follow_up(allocator, exec_ctx) orelse return total_rounds;

    // Recurse to handle any further tool calls.
    total_rounds += try processResponse(
        allocator,
        next_response,
        results_buf,
        executor,
        follow_up,
        exec_ctx,
        opts,
        depth + 1,
    );

    return total_rounds;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "processResponse: text-only response" {
    const alloc = std.testing.allocator;

    const response = AssistantResponse{
        .text = "Hello, world!",
        .tool_calls = &.{},
    };

    var results = std.ArrayList(ToolResult).init(alloc);
    defer results.deinit();

    const executor: ToolExecutorFn = struct {
        fn exec(_: std.mem.Allocator, _: ToolCall, _: ?*anyopaque) !ToolResult {
            unreachable;
        }
    }.exec;

    const follow_up: FollowupFn = struct {
        fn fu(_: std.mem.Allocator, _: ?*anyopaque) !?AssistantResponse {
            unreachable;
        }
    }.fu;

    const rounds = try processResponse(
        alloc,
        response,
        &results,
        executor,
        follow_up,
        null,
        .{ .print_responses = false },
        0,
    );

    try std.testing.expectEqual(@as(usize, 0), rounds);
    try std.testing.expectEqual(@as(usize, 0), results.items.len);
}

test "processResponse: one tool call then done" {
    const alloc = std.testing.allocator;

    const tool_calls = [_]ToolCall{
        .{ .id = "tc1", .name = "Bash", .params_json = "{\"command\":\"echo hi\"}" },
    };

    const first_response = AssistantResponse{
        .text = "",
        .tool_calls = &tool_calls,
    };

    var results = std.ArrayList(ToolResult).init(alloc);
    defer {
        for (results.items) |r| {
            alloc.free(r.result_json);
        }
        results.deinit();
    }

    const TestState = struct {
        call_count: usize = 0,
    };
    var state = TestState{};

    const executor: ToolExecutorFn = struct {
        fn exec(a: std.mem.Allocator, call: ToolCall, _: ?*anyopaque) !ToolResult {
            return ToolResult{
                .tool_id = call.id,
                .tool_name = call.name,
                .result_json = try a.dupe(u8, "{\"output\":\"hi\"}"),
                .is_error = false,
            };
        }
    }.exec;

    const follow_up: FollowupFn = struct {
        fn fu(_: std.mem.Allocator, ctx: ?*anyopaque) !?AssistantResponse {
            const s: *TestState = @ptrCast(@alignCast(ctx));
            s.call_count += 1;
            // Return a text-only response (no more tool calls).
            return AssistantResponse{ .text = "Done!", .tool_calls = &.{} };
        }
    }.fu;

    const rounds = try processResponse(
        alloc,
        first_response,
        &results,
        executor,
        follow_up,
        &state,
        .{ .print_responses = false, .print_tool_calls = false },
        0,
    );

    try std.testing.expectEqual(@as(usize, 1), rounds);
    try std.testing.expectEqual(@as(usize, 1), results.items.len);
    try std.testing.expectEqualStrings("tc1", results.items[0].tool_id);
    try std.testing.expectEqual(@as(usize, 1), state.call_count);
}

test "processResponse: max_depth prevents infinite recursion" {
    const alloc = std.testing.allocator;

    const tool_calls = [_]ToolCall{
        .{ .id = "tc1", .name = "Bash", .params_json = "{}" },
    };

    const response = AssistantResponse{
        .text = "",
        .tool_calls = &tool_calls,
    };

    var results = std.ArrayList(ToolResult).init(alloc);
    defer {
        for (results.items) |r| alloc.free(r.result_json);
        results.deinit();
    }

    // Always returns another tool call to simulate infinite loop.
    const always_tool: FollowupFn = struct {
        fn fu(a: std.mem.Allocator, _: ?*anyopaque) !?AssistantResponse {
            const calls = try a.alloc(ToolCall, 1);
            calls[0] = ToolCall{ .id = "tc1", .name = "Bash", .params_json = "{}" };
            // We need a stable slice — use a static here (safe for tests).
            a.free(calls);
            const static_calls = [_]ToolCall{
                .{ .id = "tcX", .name = "Bash", .params_json = "{}" },
            };
            return AssistantResponse{ .text = "", .tool_calls = &static_calls };
        }
    }.fu;

    const executor: ToolExecutorFn = struct {
        fn exec(a: std.mem.Allocator, call: ToolCall, _: ?*anyopaque) !ToolResult {
            return ToolResult{
                .tool_id = call.id,
                .tool_name = call.name,
                .result_json = try a.dupe(u8, "{}"),
                .is_error = false,
            };
        }
    }.exec;

    // With max_depth=2, recursion is capped.
    const rounds = try processResponse(
        alloc,
        response,
        &results,
        executor,
        always_tool,
        null,
        .{ .print_responses = false, .print_tool_calls = false, .max_depth = 2 },
        0,
    );

    // Should have stopped at depth 2.
    try std.testing.expect(rounds <= 3);
}
