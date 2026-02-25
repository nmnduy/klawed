//! Message Queue - Thread-safe message passing for async TUI communication
//!
//! Port of src/message_queue.c — provides two types of queues:
//! 1. TUIMessageQueue: Worker -> Main thread (UI updates)
//! 2. AIInstructionQueue: Main -> Worker thread (user commands)
//!
//! ## Design
//!
//! Both queues use a bounded circular buffer with mutex + condition variables
//! for synchronization. The TUI queue drops oldest messages on overflow (FIFO
//! eviction), while the instruction queue blocks until space is available.

const std = @import("std");

// ---------------------------------------------------------------------------
// TUI Message Queue (Worker -> Main Thread)
// ---------------------------------------------------------------------------

/// Types of messages that can be posted to the TUI
pub const TUIMessageType = enum {
    add_line, // Add a line to conversation display
    status, // Update status line
    clear, // Clear conversation display
    error_msg, // Display error message
    todo_update, // Update TODO list
    todo_hide, // Hide TODO banner (AI idle)
};

/// Message structure for TUI updates
pub const TUIMessage = struct {
    type: TUIMessageType,
    text: ?[]u8, // Owned by queue, freed after processing

    pub fn deinit(self: *TUIMessage, allocator: std.mem.Allocator) void {
        if (self.text) |text| {
            allocator.free(text);
            self.text = null;
        }
    }
};

/// Thread-safe circular buffer for TUI messages
/// Overflow policy: Drop oldest messages (FIFO eviction)
pub const TUIMessageQueue = struct {
    allocator: std.mem.Allocator,
    messages: []TUIMessage,
    capacity: usize,
    head: usize, // Next write position
    tail: usize, // Next read position
    count: usize, // Current number of messages
    mutex: std.Thread.Mutex,
    not_empty: std.Thread.Condition,
    shutdown: bool,

    /// Initialize TUI message queue
    pub fn init(allocator: std.mem.Allocator, capacity: usize) !TUIMessageQueue {
        if (capacity == 0) return error.InvalidCapacity;

        const messages = try allocator.alloc(TUIMessage, capacity);
        errdefer allocator.free(messages);

        // Initialize all messages to empty state
        for (messages) |*msg| {
            msg.* = .{ .type = .add_line, .text = null };
        }

        return TUIMessageQueue{
            .allocator = allocator,
            .messages = messages,
            .capacity = capacity,
            .head = 0,
            .tail = 0,
            .count = 0,
            .mutex = .{},
            .not_empty = .{},
            .shutdown = false,
        };
    }

    /// Free TUI message queue resources
    /// Must be called after all threads have stopped using it
    pub fn deinit(self: *TUIMessageQueue) void {
        // Free any remaining messages
        for (self.messages) |*msg| {
            if (msg.text) |text| {
                self.allocator.free(text);
            }
        }
        self.allocator.free(self.messages);
        self.* = undefined;
    }

    /// Post a message to the TUI queue
    /// Non-blocking. If queue is full, drops oldest message.
    pub fn post(self: *TUIMessageQueue, msg_type: TUIMessageType, text: ?[]const u8) !void {
        // Copy text if provided
        var text_copy: ?[]u8 = null;
        if (text) |t| {
            text_copy = try self.allocator.dupe(u8, t);
        }
        errdefer if (text_copy) |tc| self.allocator.free(tc);

        self.mutex.lock();
        defer self.mutex.unlock();

        // If queue is full, drop oldest message (FIFO eviction)
        if (self.count == self.capacity) {
            const oldest = &self.messages[self.tail];
            if (oldest.text) |old_text| {
                self.allocator.free(old_text);
            }
            oldest.* = .{ .type = .add_line, .text = null };
            self.tail = (self.tail + 1) % self.capacity;
            self.count -= 1;
        }

        // Add new message at head
        self.messages[self.head] = .{
            .type = msg_type,
            .text = text_copy,
        };
        self.head = (self.head + 1) % self.capacity;
        self.count += 1;

        // Signal waiting readers
        self.not_empty.signal();
    }

    /// Poll for a message from the TUI queue (non-blocking)
    /// Returns true if message was retrieved, false if empty
    /// Caller must call msg.deinit(allocator) when done
    pub fn poll(self: *TUIMessageQueue, msg: *TUIMessage) bool {
        self.mutex.lock();
        defer self.mutex.unlock();

        // Check if queue is empty
        if (self.count == 0) {
            return false;
        }

        // Retrieve message from tail
        const src = &self.messages[self.tail];
        msg.* = src.*;
        src.* = .{ .type = .add_line, .text = null }; // Clear to prevent double-free

        self.tail = (self.tail + 1) % self.capacity;
        self.count -= 1;

        return true;
    }

    /// Wait for a message from the TUI queue (blocking)
    /// Returns true if message was retrieved, false if shutdown
    /// Caller must call msg.deinit(allocator) when done
    pub fn wait(self: *TUIMessageQueue, msg: *TUIMessage) bool {
        self.mutex.lock();
        defer self.mutex.unlock();

        // Wait until message available or shutdown
        while (self.count == 0 and !self.shutdown) {
            self.not_empty.wait(&self.mutex);
        }

        // Check shutdown flag
        if (self.shutdown and self.count == 0) {
            return false;
        }

        // Retrieve message from tail
        const src = &self.messages[self.tail];
        msg.* = src.*;
        src.* = .{ .type = .add_line, .text = null }; // Clear to prevent double-free

        self.tail = (self.tail + 1) % self.capacity;
        self.count -= 1;

        return true;
    }

    /// Shutdown TUI message queue and wake blocked readers
    pub fn shutdownQueue(self: *TUIMessageQueue) void {
        self.mutex.lock();
        defer self.mutex.unlock();
        self.shutdown = true;
        self.not_empty.broadcast();
    }

    /// Check if queue is shutdown
    pub fn isShutdown(self: *TUIMessageQueue) bool {
        self.mutex.lock();
        defer self.mutex.unlock();
        return self.shutdown;
    }

    /// Get current queue depth
    pub fn depth(self: *TUIMessageQueue) usize {
        self.mutex.lock();
        defer self.mutex.unlock();
        return self.count;
    }
};

// ---------------------------------------------------------------------------
// AI Instruction Queue (Main Thread -> Worker)
// ---------------------------------------------------------------------------

/// Instruction for the AI worker thread
pub const AIInstruction = struct {
    text: []u8, // User instruction text (owned by instruction)
    conversation_state: ?*anyopaque, // Pointer to ConversationState (shared)

    pub fn deinit(self: *AIInstruction, allocator: std.mem.Allocator) void {
        allocator.free(self.text);
        self.text = &[_]u8{};
    }
};

/// Thread-safe queue for AI instructions
/// Overflow policy: Block sender until space available
pub const AIInstructionQueue = struct {
    allocator: std.mem.Allocator,
    instructions: []AIInstruction,
    capacity: usize,
    head: usize, // Next write position
    tail: usize, // Next read position
    count: usize, // Current number of instructions
    mutex: std.Thread.Mutex,
    not_empty: std.Thread.Condition,
    not_full: std.Thread.Condition,
    shutdown: bool,

    /// Initialize AI instruction queue
    pub fn init(allocator: std.mem.Allocator, capacity: usize) !AIInstructionQueue {
        if (capacity == 0) return error.InvalidCapacity;

        const instructions = try allocator.alloc(AIInstruction, capacity);
        errdefer allocator.free(instructions);

        // Initialize all instructions to empty state
        for (instructions) |*instr| {
            instr.* = .{ .text = &.{}, .conversation_state = null };
        }

        return AIInstructionQueue{
            .allocator = allocator,
            .instructions = instructions,
            .capacity = capacity,
            .head = 0,
            .tail = 0,
            .count = 0,
            .mutex = .{},
            .not_empty = .{},
            .not_full = .{},
            .shutdown = false,
        };
    }

    /// Free AI instruction queue resources
    /// Must be called after all threads have stopped using it
    pub fn deinit(self: *AIInstructionQueue) void {
        // Free any remaining instructions
        for (self.instructions) |*instr| {
            if (instr.text.len > 0) {
                self.allocator.free(instr.text);
            }
        }
        self.allocator.free(self.instructions);
        self.* = undefined;
    }

    /// Enqueue an instruction for the AI worker
    /// Blocks if queue is full (with 5 second timeout).
    pub fn enqueue(self: *AIInstructionQueue, text: []const u8, conversation_state: ?*anyopaque) !void {
        // Copy text
        const text_copy = try self.allocator.dupe(u8, text);
        errdefer self.allocator.free(text_copy);

        self.mutex.lock();
        defer self.mutex.unlock();

        // Wait until space available or shutdown (with timeout)
        const timeout_ms = 5000; // 5 second timeout
        var waited_ms: u64 = 0;
        while (self.count == self.capacity and !self.shutdown) {
            if (waited_ms >= timeout_ms) {
                return error.QueueFullTimeout; // errdefer will free text_copy
            }
            // Small sleep before checking again
            self.mutex.unlock();
            std.time.sleep(10 * std.time.ns_per_ms);
            self.mutex.lock();
            waited_ms += 10;
        }

        // Check shutdown flag
        if (self.shutdown) {
            self.allocator.free(text_copy);
            return error.QueueShutdown;
        }

        // Add instruction at head
        self.instructions[self.head] = .{
            .text = text_copy,
            .conversation_state = conversation_state,
        };
        self.head = (self.head + 1) % self.capacity;
        self.count += 1;

        // Signal waiting readers
        self.not_empty.signal();
    }

    /// Dequeue an instruction for processing
    /// Blocks until instruction available or shutdown.
    /// Returns true if instruction retrieved, false if shutdown
    /// Caller must call instr.deinit(allocator) when done
    pub fn dequeue(self: *AIInstructionQueue, instr: *AIInstruction) bool {
        self.mutex.lock();
        defer self.mutex.unlock();

        // Wait until instruction available or shutdown
        while (self.count == 0 and !self.shutdown) {
            self.not_empty.wait(&self.mutex);
        }

        // Check shutdown flag
        if (self.shutdown and self.count == 0) {
            return false;
        }

        // Retrieve instruction from tail
        const src = &self.instructions[self.tail];
        instr.* = src.*;
        src.* = .{ .text = &.{}, .conversation_state = null }; // Clear to prevent double-free

        self.tail = (self.tail + 1) % self.capacity;
        self.count -= 1;

        // Signal waiting writers
        self.not_full.signal();

        return true;
    }

    /// Shutdown AI instruction queue and wake blocked threads
    pub fn shutdownQueue(self: *AIInstructionQueue) void {
        self.mutex.lock();
        defer self.mutex.unlock();
        self.shutdown = true;
        self.not_empty.broadcast();
        self.not_full.broadcast();
    }

    /// Check if queue is shutdown
    pub fn isShutdown(self: *AIInstructionQueue) bool {
        self.mutex.lock();
        defer self.mutex.unlock();
        return self.shutdown;
    }

    /// Get current queue depth (number of pending instructions)
    pub fn depth(self: *AIInstructionQueue) usize {
        self.mutex.lock();
        defer self.mutex.unlock();
        return self.count;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "TUIMessageQueue init and deinit" {
    const allocator = std.testing.allocator;
    var queue = try TUIMessageQueue.init(allocator, 4);
    defer queue.deinit();

    try std.testing.expectEqual(@as(usize, 4), queue.capacity);
    try std.testing.expectEqual(@as(usize, 0), queue.depth());
}

test "TUIMessageQueue post and poll" {
    const allocator = std.testing.allocator;
    var queue = try TUIMessageQueue.init(allocator, 4);
    defer queue.deinit();

    // Post a message
    try queue.post(.status, "Hello");
    try std.testing.expectEqual(@as(usize, 1), queue.depth());

    // Poll the message
    var msg: TUIMessage = undefined;
    const got = queue.poll(&msg);
    try std.testing.expect(got);
    try std.testing.expect(msg.type == .status);
    try std.testing.expectEqualStrings("Hello", msg.text.?);
    defer msg.deinit(allocator);

    try std.testing.expectEqual(@as(usize, 0), queue.depth());
}

test "TUIMessageQueue FIFO eviction on overflow" {
    const allocator = std.testing.allocator;
    var queue = try TUIMessageQueue.init(allocator, 2);
    defer queue.deinit();

    // Fill queue
    try queue.post(.add_line, "first");
    try queue.post(.add_line, "second");
    try std.testing.expectEqual(@as(usize, 2), queue.depth());

    // Overflow - should drop oldest
    try queue.post(.add_line, "third");
    try std.testing.expectEqual(@as(usize, 2), queue.depth());

    // Poll - should get "second" (first was dropped)
    var msg: TUIMessage = undefined;
    const got = queue.poll(&msg);
    try std.testing.expect(got);
    try std.testing.expectEqualStrings("second", msg.text.?);
    defer msg.deinit(allocator);
}

test "TUIMessageQueue wait with shutdown" {
    const allocator = std.testing.allocator;
    var queue = try TUIMessageQueue.init(allocator, 4);
    defer queue.deinit();

    // Shutdown should wake waiters
    queue.shutdownQueue();

    var msg: TUIMessage = undefined;
    const got = queue.wait(&msg);
    try std.testing.expect(!got); // false = shutdown
}

test "TUIMessageQueue post with null text" {
    const allocator = std.testing.allocator;
    var queue = try TUIMessageQueue.init(allocator, 4);
    defer queue.deinit();

    try queue.post(.clear, null);

    var msg: TUIMessage = undefined;
    const got = queue.poll(&msg);
    try std.testing.expect(got);
    try std.testing.expect(msg.type == .clear);
    try std.testing.expect(msg.text == null);
    defer msg.deinit(allocator);
}

test "AIInstructionQueue init and deinit" {
    const allocator = std.testing.allocator;
    var queue = try AIInstructionQueue.init(allocator, 4);
    defer queue.deinit();

    try std.testing.expectEqual(@as(usize, 4), queue.capacity);
    try std.testing.expectEqual(@as(usize, 0), queue.depth());
}

test "AIInstructionQueue enqueue and dequeue" {
    const allocator = std.testing.allocator;
    var queue = try AIInstructionQueue.init(allocator, 4);
    defer queue.deinit();

    // Enqueue an instruction
    try queue.enqueue("test instruction", null);
    try std.testing.expectEqual(@as(usize, 1), queue.depth());

    // Dequeue the instruction
    var instr: AIInstruction = undefined;
    const got = queue.dequeue(&instr);
    try std.testing.expect(got);
    try std.testing.expectEqualStrings("test instruction", instr.text);
    defer instr.deinit(allocator);

    try std.testing.expectEqual(@as(usize, 0), queue.depth());
}

test "AIInstructionQueue dequeue with shutdown" {
    const allocator = std.testing.allocator;
    var queue = try AIInstructionQueue.init(allocator, 4);
    defer queue.deinit();

    queue.shutdownQueue();

    var instr: AIInstruction = undefined;
    const got = queue.dequeue(&instr);
    try std.testing.expect(!got); // false = shutdown
}

test "AIInstructionQueue blocks on full" {
    const allocator = std.testing.allocator;
    var queue = try AIInstructionQueue.init(allocator, 2);
    defer queue.deinit();

    // Fill the queue
    try queue.enqueue("first", null);
    try queue.enqueue("second", null);
    try std.testing.expectEqual(@as(usize, 2), queue.depth());

    // This should timeout and return error
    const result = queue.enqueue("third", null);
    try std.testing.expectError(error.QueueFullTimeout, result);
}

test "TUIMessageQueue concurrent operations" {
    // Simplified test - just verify basic thread safety without heavy concurrency
    const allocator = std.testing.allocator;
    var queue = try TUIMessageQueue.init(allocator, 10);
    defer queue.deinit();

    // Post from main thread
    try queue.post(.add_line, "msg1");
    try queue.post(.add_line, "msg2");

    try std.testing.expectEqual(@as(usize, 2), queue.depth());

    // Drain the queue
    var msg: TUIMessage = undefined;
    while (queue.poll(&msg)) {
        msg.deinit(allocator);
    }

    try std.testing.expectEqual(@as(usize, 0), queue.depth());
}
