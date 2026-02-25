//! conversation/processor.zig — Conversation processing stubs
//!
//! Zig port of src/conversation/conversation_processor.c.
//!
//! The C processor manages the full agentic loop: call API → receive response
//! → execute tool calls → add tool results → loop.  It also handles parallel
//! tool execution via pthreads.
//!
//! ## Phase 6 scope
//!
//! This module defines the types and structures for conversation processing.
//! The actual API call and tool execution are wired in later phases:
//!
//! - **Phase 7** wires in tool execution (`execute_tool`).
//! - **Phase 8** wires in the `call_api_with_retries` entry point.
//!
//! Until those phases are complete, `processResponse` and `processUserInstruction`
//! return `error.NotImplemented`.
//!
//! ## What is fully implemented here
//!
//! - `ExecutionMode` enum (serial vs parallel)
//! - `OutputFormat` enum
//! - `ProcessingContext` struct with all callback fields
//! - `processingContextInit` default initializer
//! - Token estimation bridge to `state.totalTokenEstimate`

const std = @import("std");
const state_mod = @import("state.zig");

pub const ConversationState = state_mod.ConversationState;
pub const Message = state_mod.Message;
pub const ContentBlock = state_mod.ContentBlock;

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

/// Whether tool calls are executed serially or in parallel threads.
pub const ExecutionMode = enum {
    serial,
    parallel,
};

/// Output format for display callbacks.
pub const OutputFormat = enum {
    plain,
    json,
    markdown,
};

// ---------------------------------------------------------------------------
// ProcessingContext
// ---------------------------------------------------------------------------

/// Callback table + configuration for driving the agentic loop.
///
/// This is the Zig equivalent of the C `ProcessingContext` struct.
/// All callback fields are optional; set only the ones you need.
pub const ProcessingContext = struct {
    /// How to execute multiple tool calls in the same turn.
    execution_mode: ExecutionMode = .serial,

    /// Display format for status messages.
    output_format: OutputFormat = .plain,

    /// Maximum number of API→tool→API iterations before giving up.
    max_iterations: u32 = 2000,

    /// Opaque user data passed to all callbacks.
    user_data: ?*anyopaque = null,

    // -----------------------------------------------------------------------
    // Callbacks (all optional)
    // -----------------------------------------------------------------------

    /// Called when the assistant emits a text response segment.
    on_assistant_text: ?*const fn (text: []const u8, user_data: ?*anyopaque) void = null,

    /// Called at the start of a tool execution.
    on_tool_start: ?*const fn (
        tool_name: []const u8,
        tool_details: []const u8,
        user_data: ?*anyopaque,
    ) void = null,

    /// Called when a tool execution completes.
    on_tool_complete: ?*const fn (
        tool_name: []const u8,
        is_error: bool,
        user_data: ?*anyopaque,
    ) void = null,

    /// Called on recoverable errors (API errors, tool errors).
    on_error: ?*const fn (message: []const u8, user_data: ?*anyopaque) void = null,

    /// Called with status updates (e.g., "Calling AI…", "Processing tool results…").
    on_status_update: ?*const fn (message: []const u8, user_data: ?*anyopaque) void = null,

    /// Returns `true` if the caller wants to interrupt the current operation.
    should_interrupt: ?*const fn (user_data: ?*anyopaque) bool = null,

    /// Called after all tool results for a turn have been added to state,
    /// before the next API call.  Callers can inject user messages here.
    on_after_tool_results: ?*const fn (
        state: *ConversationState,
        user_data: ?*anyopaque,
    ) void = null,
};

/// Initialize a `ProcessingContext` with default values.
pub fn processingContextInit() ProcessingContext {
    return ProcessingContext{};
}

// ---------------------------------------------------------------------------
// Processor stubs (Phase 8 wires these up)
// ---------------------------------------------------------------------------

/// Process a single API response, executing any tool calls and looping.
///
/// **Status**: stub — returns `error.NotImplemented` until Phase 8.
/// The full implementation needs `call_api_with_retries` and `execute_tool`
/// from Phases 7–8.
pub fn processResponse(
    s: *ConversationState,
    /// Raw JSON response body from the API.
    response_body: []const u8,
    ctx: *const ProcessingContext,
) !void {
    _ = s;
    _ = response_body;
    _ = ctx;
    return error.NotImplemented;
}

/// Add a user message and drive the full API→tool loop.
///
/// **Status**: stub — returns `error.NotImplemented` until Phase 8.
pub fn processUserInstruction(
    s: *ConversationState,
    user_input: []const u8,
    ctx: *const ProcessingContext,
) !void {
    _ = s;
    _ = user_input;
    _ = ctx;
    return error.NotImplemented;
}

// ---------------------------------------------------------------------------
// Token estimation (delegated to ConversationState)
// ---------------------------------------------------------------------------

/// Estimate the total token count for a conversation.
/// Mirrors `compaction_update_token_count` heuristic in C.
pub fn estimateTokens(s: *const ConversationState) usize {
    return s.totalTokenEstimate();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "processingContextInit defaults" {
    const ctx = processingContextInit();
    try std.testing.expect(ctx.execution_mode == .serial);
    try std.testing.expect(ctx.output_format == .plain);
    try std.testing.expectEqual(@as(u32, 2000), ctx.max_iterations);
    try std.testing.expect(ctx.on_assistant_text == null);
    try std.testing.expect(ctx.on_error == null);
}

test "processResponse returns NotImplemented" {
    const allocator = std.testing.allocator;
    var s = ConversationState.init(allocator);
    defer s.deinit();

    const ctx = processingContextInit();
    const result = processResponse(&s, "{}", &ctx);
    try std.testing.expectError(error.NotImplemented, result);
}

test "processUserInstruction returns NotImplemented" {
    const allocator = std.testing.allocator;
    var s = ConversationState.init(allocator);
    defer s.deinit();

    const ctx = processingContextInit();
    const result = processUserInstruction(&s, "hello", &ctx);
    try std.testing.expectError(error.NotImplemented, result);
}

test "estimateTokens non-zero for non-empty state" {
    const allocator = std.testing.allocator;
    var s = ConversationState.init(allocator);
    defer s.deinit();
    try s.addUserMessage("Tell me something interesting.");

    const tokens = estimateTokens(&s);
    try std.testing.expect(tokens > 0);
}

test "ExecutionMode and OutputFormat enums" {
    const em = ExecutionMode.parallel;
    try std.testing.expect(em == .parallel);
    const of = OutputFormat.json;
    try std.testing.expect(of == .json);
}
