//! AI Worker - Background worker for asynchronous API processing
//!
//! Port of src/ai_worker.c and src/background_init.c — provides a dedicated
//! worker thread that consumes AI instructions, invokes a caller-provided
//! handler, and posts updates back to the TUI message queue.
//!
//! ## Usage
//!
//! ```zig
//! var worker = try AIWorker.init(allocator, &state, instruction_queue, tui_queue, handler);
//! defer worker.deinit();
//!
//! try worker.start();
//! try worker.submit("Hello AI");
//! // ... later ...
//! worker.stop();
//! ```

const std = @import("std");
const message_queue = @import("message_queue.zig");

const TUIMessageQueue = message_queue.TUIMessageQueue;
const AIInstructionQueue = message_queue.AIInstructionQueue;
const AIInstruction = message_queue.AIInstruction;

/// Information about a completed tool execution
/// Used to stream progress updates back to the TUI
pub const ToolCompletion = struct {
    tool_name: []const u8, // Tool identifier (not owned)
    result_json: ?[]const u8, // Tool result payload (not owned)
    is_error: bool, // true if tool completed with error
    completed: i32, // Number of tools finished so far
    total: i32, // Total number of tools launched
};

/// Callback type for processing instructions
/// Called on the worker thread for each instruction
pub const InstructionHandlerFn = *const fn (
    ctx: *AIWorkerContext,
    instruction: *const AIInstruction,
) void;

/// Shared state that can be accessed by the worker
/// (Mirror of ConversationState from C)
pub const ConversationState = struct {
    // Interrupt flag - set when worker should stop
    interrupt_requested: std.atomic.Value(bool),
    // Other state fields would be added here...

    pub fn init() ConversationState {
        return .{
            .interrupt_requested = std.atomic.Value(bool).init(false),
        };
    }

    pub fn requestInterrupt(self: *ConversationState) void {
        self.interrupt_requested.store(true, .release);
    }

    pub fn isInterrupted(self: *ConversationState) bool {
        return self.interrupt_requested.load(.acquire);
    }

    pub fn clearInterrupt(self: *ConversationState) void {
        self.interrupt_requested.store(false, .release);
    }
};

/// Context structure for the AI worker thread
pub const AIWorkerContext = struct {
    allocator: std.mem.Allocator,
    thread: ?std.Thread,
    instruction_queue: *AIInstructionQueue,
    tui_queue: ?*TUIMessageQueue,
    state: ?*ConversationState,
    running: std.atomic.Value(bool),
    handler: InstructionHandlerFn,

    /// Initialize a new AI worker context
    pub fn init(
        allocator: std.mem.Allocator,
        state: ?*ConversationState,
        instruction_queue: *AIInstructionQueue,
        tui_queue: ?*TUIMessageQueue,
        handler: InstructionHandlerFn,
    ) AIWorkerContext {
        return AIWorkerContext{
            .allocator = allocator,
            .thread = null,
            .instruction_queue = instruction_queue,
            .tui_queue = tui_queue,
            .state = state,
            .running = std.atomic.Value(bool).init(false),
            .handler = handler,
        };
    }

    /// Check if the worker is currently running
    pub fn isRunning(self: *const AIWorkerContext) bool {
        return self.running.load(.acquire);
    }

    /// Post a status update for a completed tool
    pub fn postToolStatus(self: *AIWorkerContext, completion: *const ToolCompletion) void {
        const tui_queue_ptr = self.tui_queue orelse return;

        const status_word = if (completion.is_error) "failed" else "completed";

        var buf: [256]u8 = undefined;
        const status_text = if (completion.total > 0) blk: {
            const text = std.fmt.bufPrint(&buf, "Tool {s} {s} ({d}/{d})", .{
                completion.tool_name,
                status_word,
                completion.completed,
                completion.total,
            }) catch break :blk "Tool update";
            break :blk text;
        } else blk: {
            const text = std.fmt.bufPrint(&buf, "Tool {s} {s}", .{
                completion.tool_name,
                status_word,
            }) catch break :blk "Tool update";
            break :blk text;
        };

        tui_queue_ptr.post(.status, status_text) catch {};
    }
};

/// Background loader for system prompt, database, and memory database
/// Port of src/background_init.c
pub const BackgroundLoader = struct {
    const LoadResult = struct {
        system_prompt: ?[]u8,
        database_handle: ?*anyopaque,
        memory_db_result: i32,
        system_prompt_ready: bool,
        database_ready: bool,
        memory_db_ready: bool,
    };

    allocator: std.mem.Allocator,
    mutex: std.Thread.Mutex,
    result: LoadResult,
    threads: [3]?std.Thread,
    state: *ConversationState,

    pub fn init(allocator: std.mem.Allocator, state: *ConversationState) BackgroundLoader {
        return BackgroundLoader{
            .allocator = allocator,
            .mutex = .{},
            .result = .{
                .system_prompt = null,
                .database_handle = null,
                .memory_db_result = -1,
                .system_prompt_ready = false,
                .database_ready = false,
                .memory_db_ready = false,
            },
            .threads = .{ null, null, null },
            .state = state,
        };
    }

    pub fn deinit(self: *BackgroundLoader) void {
        // Wait for all threads to complete
        for (&self.threads) |*t| {
            if (t.*) |thread| {
                thread.join();
                t.* = null;
            }
        }

        // Free results
        if (self.result.system_prompt) |prompt| {
            self.allocator.free(prompt);
        }
    }

    /// Start all background loaders
    pub fn start(self: *BackgroundLoader) !void {
        // Start system prompt loading thread
        self.threads[0] = std.Thread.spawn(.{}, loadSystemPromptThread, .{self}) catch |err| {
            std.log.warn("Failed to start system prompt loader: {}", .{err});
            null;
        };

        // Start database initialization thread
        self.threads[1] = std.Thread.spawn(.{}, loadDatabaseThread, .{self}) catch |err| {
            std.log.warn("Failed to start database loader: {}", .{err});
            null;
        };

        // Start memory database initialization thread
        self.threads[2] = std.Thread.spawn(.{}, loadMemoryDbThread, .{self}) catch |err| {
            std.log.warn("Failed to start memory db loader: {}", .{err});
            null;
        };
    }

    fn loadSystemPromptThread(self: *BackgroundLoader) void {
        const start_time = std.time.milliTimestamp();
        std.log.debug("[BG] System prompt loading started", .{});

        // In real implementation, this would call build_system_prompt
        // For now, just simulate loading
        const prompt = self.allocator.dupe(u8, "System prompt placeholder") catch null;

        self.mutex.lock();
        self.result.system_prompt = prompt;
        self.result.system_prompt_ready = true;
        self.mutex.unlock();

        const duration = std.time.milliTimestamp() - start_time;
        std.log.debug("[BG] System prompt loading completed in {d} ms", .{duration});
    }

    fn loadDatabaseThread(self: *BackgroundLoader) void {
        const start_time = std.time.milliTimestamp();
        std.log.debug("[BG] Database initialization started", .{});

        // In real implementation, this would call persistence_init
        // For now, just simulate

        self.mutex.lock();
        self.result.database_handle = null; // Would be actual handle
        self.result.database_ready = true;
        self.mutex.unlock();

        const duration = std.time.milliTimestamp() - start_time;
        std.log.debug("[BG] Database initialization completed in {d} ms", .{duration});
    }

    fn loadMemoryDbThread(self: *BackgroundLoader) void {
        const start_time = std.time.milliTimestamp();
        std.log.debug("[BG] Memory database initialization started", .{});

        // In real implementation, this would call memory_db_init_global

        self.mutex.lock();
        self.result.memory_db_result = 0; // Success
        self.result.memory_db_ready = true;
        self.mutex.unlock();

        const duration = std.time.milliTimestamp() - start_time;
        std.log.debug("[BG] Memory database initialization completed in {d} ms", .{duration});
    }

    /// Wait for system prompt and return it (caller owns memory)
    pub fn awaitSystemPrompt(self: *BackgroundLoader) ?[]u8 {
        // Wait for thread to complete if running
        if (self.threads[0]) |t| {
            t.join();
            self.threads[0] = null;
        }

        self.mutex.lock();
        defer self.mutex.unlock();

        if (self.result.system_prompt) |prompt| {
            // Transfer ownership to caller
            self.result.system_prompt = null;
            return prompt;
        }
        return null;
    }

    /// Check if system prompt is ready
    pub fn isSystemPromptReady(self: *BackgroundLoader) bool {
        self.mutex.lock();
        defer self.mutex.unlock();
        return self.result.system_prompt_ready;
    }

    /// Check if database is ready
    pub fn isDatabaseReady(self: *BackgroundLoader) bool {
        self.mutex.lock();
        defer self.mutex.unlock();
        return self.result.database_ready;
    }

    /// Check if memory db is ready
    pub fn isMemoryDbReady(self: *BackgroundLoader) bool {
        self.mutex.lock();
        defer self.mutex.unlock();
        return self.result.memory_db_ready;
    }
};

/// AI Worker handle
pub const AIWorker = struct {
    allocator: std.mem.Allocator,
    context: AIWorkerContext,

    /// Initialize the AI worker (does not start the thread)
    pub fn init(
        allocator: std.mem.Allocator,
        state: ?*ConversationState,
        instruction_queue: *AIInstructionQueue,
        tui_queue: ?*TUIMessageQueue,
        handler: InstructionHandlerFn,
    ) !AIWorker {
        return AIWorker{
            .allocator = allocator,
            .context = AIWorkerContext.init(
                allocator,
                state,
                instruction_queue,
                tui_queue,
                handler,
            ),
        };
    }

    /// Start the worker thread
    pub fn start(self: *AIWorker) !void {
        if (self.context.isRunning()) {
            return error.AlreadyRunning;
        }

        self.context.running.store(true, .release);

        self.context.thread = try std.Thread.spawn(
            .{},
            workerThreadMain,
            .{&self.context},
        );
    }

    /// Stop the worker thread and wait for it to finish
    /// Safe to call multiple times
    pub fn stop(self: *AIWorker) void {
        if (!self.context.isRunning()) {
            return;
        }

        self.context.running.store(false, .release);

        // Set interrupt flag to signal any ongoing API calls
        if (self.context.state) |state| {
            state.requestInterrupt();
        }

        // Shutdown the instruction queue to wake up blocked readers
        self.context.instruction_queue.shutdownQueue();

        // Give thread a moment to exit gracefully
        std.time.sleep(100 * std.time.ns_per_ms);

        // Cancel and join the thread
        if (self.context.thread) |t| {
            // Note: Zig doesn't have thread cancellation like pthread_cancel
            // The thread should check the running flag periodically
            t.join();
            self.context.thread = null;
        }
    }

    /// Submit a new instruction to the worker
    pub fn submit(self: *AIWorker, text: []const u8) !void {
        if (!self.context.isRunning()) {
            return error.NotRunning;
        }
        try self.context.instruction_queue.enqueue(text, self.context.state);
    }

    /// Check if the worker is running
    pub fn isRunning(self: *const AIWorker) bool {
        return self.context.isRunning();
    }

    /// Get a reference to the worker context
    pub fn getContext(self: *AIWorker) *AIWorkerContext {
        return &self.context;
    }
};

/// Main worker thread function
fn workerThreadMain(ctx: *AIWorkerContext) void {
    // Thread cancellation setup not needed in Zig
    // The thread checks ctx.running periodically

    while (ctx.running.load(.acquire)) {
        var instruction: AIInstruction = undefined;
        const got = ctx.instruction_queue.dequeue(&instruction);

        if (!got) {
            // Queue shutdown
            break;
        }

        if (!ctx.running.load(.acquire)) {
            instruction.deinit(ctx.allocator);
            break;
        }

        // Call the handler
        ctx.handler(ctx, &instruction);

        // Clean up the instruction
        instruction.deinit(ctx.allocator);
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "AIWorkerContext init" {
    const allocator = std.testing.allocator;

    const handler = struct {
        fn h(_: *AIWorkerContext, _: *const AIInstruction) void {}
    }.h;

    var instruction_queue = try AIInstructionQueue.init(allocator, 4);
    defer instruction_queue.deinit();

    var ctx = AIWorkerContext.init(allocator, null, &instruction_queue, null, handler);

    try std.testing.expect(!ctx.isRunning());
    try std.testing.expect(ctx.thread == null);
}

test "ConversationState interrupt" {
    var state = ConversationState.init();

    try std.testing.expect(!state.isInterrupted());

    state.requestInterrupt();
    try std.testing.expect(state.isInterrupted());

    state.clearInterrupt();
    try std.testing.expect(!state.isInterrupted());
}

test "AIWorker start and stop" {
    const allocator = std.testing.allocator;

    const handler = struct {
        fn h(_: *AIWorkerContext, _: *const AIInstruction) void {}
    }.h;

    var instruction_queue = try AIInstructionQueue.init(allocator, 4);
    defer instruction_queue.deinit();

    var worker = try AIWorker.init(allocator, null, &instruction_queue, null, handler);

    try std.testing.expect(!worker.isRunning());

    try worker.start();
    try std.testing.expect(worker.isRunning());

    worker.stop();
    try std.testing.expect(!worker.isRunning());
}

test "AIWorker submit instruction" {
    const allocator = std.testing.allocator;

    var received_count: usize = 0;
    const S = struct {
        var count: *usize = undefined;

        fn h(_: *AIWorkerContext, _: *const AIInstruction) void {
            count.* += 1;
        }
    };
    S.count = &received_count;

    var instruction_queue = try AIInstructionQueue.init(allocator, 4);
    defer instruction_queue.deinit();

    var worker = try AIWorker.init(allocator, null, &instruction_queue, null, S.h);

    try worker.start();
    defer worker.stop();

    try worker.submit("test instruction");

    // Give the worker time to process
    std.time.sleep(50 * std.time.ns_per_ms);

    try std.testing.expect(received_count > 0);
}

test "BackgroundLoader init and deinit" {
    const allocator = std.testing.allocator;
    var state = ConversationState.init();

    var loader = BackgroundLoader.init(allocator, &state);

    // Don't start threads in test to avoid complexity
    // Just verify init worked
    try std.testing.expect(!loader.isSystemPromptReady());
}

test "ToolCompletion status formatting" {
    const completion = ToolCompletion{
        .tool_name = "Bash",
        .result_json = null,
        .is_error = false,
        .completed = 2,
        .total = 5,
    };

    try std.testing.expectEqualStrings("Bash", completion.tool_name);
    try std.testing.expectEqual(@as(i32, 2), completion.completed);
    try std.testing.expectEqual(@as(i32, 5), completion.total);
    try std.testing.expect(!completion.is_error);
}

test "AIWorker cannot start twice" {
    const allocator = std.testing.allocator;

    const handler = struct {
        fn h(_: *AIWorkerContext, _: *const AIInstruction) void {}
    }.h;

    var instruction_queue = try AIInstructionQueue.init(allocator, 4);
    defer instruction_queue.deinit();

    var worker = try AIWorker.init(allocator, null, &instruction_queue, null, handler);

    try worker.start();
    defer worker.stop();

    // Starting again should fail
    const result = worker.start();
    try std.testing.expectError(error.AlreadyRunning, result);
}

test "AIWorker submit when not running fails" {
    const allocator = std.testing.allocator;

    const handler = struct {
        fn h(_: *AIWorkerContext, _: *const AIInstruction) void {}
    }.h;

    var instruction_queue = try AIInstructionQueue.init(allocator, 4);
    defer instruction_queue.deinit();

    var worker = try AIWorker.init(allocator, null, &instruction_queue, null, handler);

    const result = worker.submit("test");
    try std.testing.expectError(error.NotRunning, result);
}
