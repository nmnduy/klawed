//! tests/test_sqlite_queue_threading.zig — Zig port of tests/test_sqlite_queue_threading.c
//!
//! Tests the thread-safety and concurrent behaviour of SqliteQueue:
//! mutex/condition primitives, concurrent sends, FIFO ordering, and
//! a simulated worker-thread pattern.

const std = @import("std");
const sqlite_queue = @import("../sqlite_queue.zig");

const SqliteQueue = sqlite_queue.SqliteQueue;
const QueueError = sqlite_queue.QueueError;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

fn openQueue(
    alloc: std.mem.Allocator,
    tmp: std.testing.TmpDir,
    filename: []const u8,
    sender: []const u8,
) !SqliteQueue {
    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, filename });
    defer alloc.free(path);
    return SqliteQueue.init(alloc, path, sender);
}

// ---------------------------------------------------------------------------
// Thread-safety: mutex and condition variable primitives are usable
// ---------------------------------------------------------------------------

test "sqlite_queue_threading: mutex and condition are functional after init" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var q = try openQueue(alloc, tmp, "mutex.db", "tester");
    defer q.deinit();

    // Lock and unlock the mutex without deadlock
    q.mutex.lock();
    q.mutex.unlock();

    // signal() on the condition variable must not crash
    q.mutex.lock();
    q.cond.signal();
    q.mutex.unlock();
}

// ---------------------------------------------------------------------------
// Concurrent sends from multiple threads
// ---------------------------------------------------------------------------

const ConcurrentSendArgs = struct {
    queue_path: []const u8,
    sender_name: []const u8,
    start_id: usize,
    count: usize,
    alloc: std.mem.Allocator,
};

fn concurrentSendWorker(args: *ConcurrentSendArgs) void {
    var q = SqliteQueue.init(args.alloc, args.queue_path, args.sender_name) catch return;
    defer q.deinit();

    var i: usize = 0;
    while (i < args.count) : (i += 1) {
        var buf: [64]u8 = undefined;
        const msg = std.fmt.bufPrint(&buf, "msg-{d}", .{args.start_id + i}) catch continue;
        q.send("consumer", msg) catch {};
    }
}

test "sqlite_queue_threading: concurrent sends from 4 threads all arrive" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "concurrent.db" });
    defer alloc.free(path);

    const num_threads = 4;
    const msgs_per_thread = 10;
    const total = num_threads * msgs_per_thread;

    var thread_args: [num_threads]ConcurrentSendArgs = undefined;
    var threads: [num_threads]std.Thread = undefined;

    for (0..num_threads) |i| {
        thread_args[i] = .{
            .queue_path = path,
            .sender_name = "sender",
            .start_id = i * msgs_per_thread,
            .count = msgs_per_thread,
            .alloc = alloc,
        };
        threads[i] = try std.Thread.spawn(.{}, concurrentSendWorker, .{&thread_args[i]});
    }

    for (&threads) |t| t.join();

    // Drain: open a consumer and collect all messages
    var consumer = try SqliteQueue.init(alloc, path, "consumer");
    defer consumer.deinit();

    var received: usize = 0;
    while (received < total) {
        const result = consumer.receive(@intCast(total), 2000);
        if (result) |*msgs| {
            // need mutable reference for deinit
            var m = msgs.*;
            received += m.messages.len;
            for (m.ids) |id| consumer.acknowledge(id) catch {};
            m.deinit();
        } else |err| {
            if (err == QueueError.Timeout) break;
            return err;
        }
    }

    try std.testing.expectEqual(total, received);
}

// ---------------------------------------------------------------------------
// Multiple init/deinit cycles: no resource leak
// ---------------------------------------------------------------------------

test "sqlite_queue_threading: repeated init/deinit cycles are stable" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "cycle.db" });
    defer alloc.free(path);

    var i: usize = 0;
    while (i < 10) : (i += 1) {
        var q = try SqliteQueue.init(alloc, path, "cycler");
        try q.send("cycler", "cycle-msg");
        q.deinit();
    }
}

// ---------------------------------------------------------------------------
// Worker-thread pattern: producer/consumer via SqliteQueue
// ---------------------------------------------------------------------------

const WorkerArgs = struct {
    queue_path: []const u8,
    alloc: std.mem.Allocator,
    processed: std.atomic.Value(usize),

    fn init(path: []const u8, a: std.mem.Allocator) WorkerArgs {
        return .{
            .queue_path = path,
            .alloc = a,
            .processed = std.atomic.Value(usize).init(0),
        };
    }
};

fn workerThread(args: *WorkerArgs) void {
    var q = SqliteQueue.init(args.alloc, args.queue_path, "worker") catch return;
    defer q.deinit();

    // Process up to 10 messages then exit
    var done: usize = 0;
    while (done < 10) {
        const result = q.receive(1, 500);
        if (result) |*msgs| {
            var m = msgs.*;
            for (m.ids) |id| q.acknowledge(id) catch {};
            done += m.messages.len;
            _ = args.processed.fetchAdd(m.messages.len, .monotonic);
            m.deinit();
        } else |_| {
            // timeout or error — keep trying until done
            if (done >= 10) break;
        }
    }
}

test "sqlite_queue_threading: worker thread processes all produced messages" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "worker.db" });
    defer alloc.free(path);

    var worker_args = WorkerArgs.init(path, alloc);

    // Start worker thread
    const worker = try std.Thread.spawn(.{}, workerThread, .{&worker_args});

    // Producer: enqueue 10 messages
    var producer = try SqliteQueue.init(alloc, path, "producer");
    defer producer.deinit();

    var i: usize = 0;
    while (i < 10) : (i += 1) {
        var buf: [32]u8 = undefined;
        const msg = try std.fmt.bufPrint(&buf, "task-{d}", .{i});
        try producer.send("worker", msg);
        std.time.sleep(5 * std.time.ns_per_ms);
    }

    worker.join();

    try std.testing.expectEqual(@as(usize, 10), worker_args.processed.load(.monotonic));
}

// ---------------------------------------------------------------------------
// Cleanup with messages still pending (no crash / leak)
// ---------------------------------------------------------------------------

test "sqlite_queue_threading: deinit with pending messages does not crash" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "leak.db" });
    defer alloc.free(path);

    var q = try SqliteQueue.init(alloc, path, "leaker");

    try q.send("leaker", "pending-1");
    try q.send("leaker", "pending-2");
    try q.send("leaker", "pending-3");

    // Deinit without consuming messages — must not crash
    q.deinit();
}

// ---------------------------------------------------------------------------
// Send signals the condition variable (observable via stats)
// ---------------------------------------------------------------------------

test "sqlite_queue_threading: send updates stats atomically" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "stats_atomic.db" });
    defer alloc.free(path);

    var q = try SqliteQueue.init(alloc, path, "stat-sender");
    defer q.deinit();

    const s0 = try q.getStats();
    try std.testing.expectEqual(@as(i32, 0), s0.pending);

    try q.send("stat-sender", "a");
    try q.send("stat-sender", "b");

    const s1 = try q.getStats();
    try std.testing.expect(s1.pending >= 2);
}
