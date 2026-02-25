//! tests/test_sqlite_queue.zig — Zig port of tests/test_sqlite_queue.c
//!
//! Tests the SqliteQueue API: init/deinit, send, receive, acknowledge,
//! statistics, message-size limits, and timeout behaviour.

const std = @import("std");
const sqlite_queue = @import("../sqlite_queue.zig");

const SqliteQueue = sqlite_queue.SqliteQueue;
const QueueError = sqlite_queue.QueueError;

// ---------------------------------------------------------------------------
// Helper: open a queue backed by a temp-dir file
// ---------------------------------------------------------------------------

fn openQueue(
    alloc: std.mem.Allocator,
    tmp: std.testing.TmpDir,
    filename: []const u8,
    sender_name: []const u8,
) !SqliteQueue {
    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, filename });
    defer alloc.free(path);
    return SqliteQueue.init(alloc, path, sender_name);
}

// ---------------------------------------------------------------------------
// Init and deinit
// ---------------------------------------------------------------------------

test "sqlite_queue: init and deinit" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var q = try openQueue(alloc, tmp, "q1.db", "sender");
    defer q.deinit();
}

// ---------------------------------------------------------------------------
// Init with :memory: path
// ---------------------------------------------------------------------------

test "sqlite_queue: init with in-memory path" {
    const alloc = std.testing.allocator;
    var q = try SqliteQueue.init(alloc, ":memory:", "test");
    defer q.deinit();
}

// ---------------------------------------------------------------------------
// Send and receive
// ---------------------------------------------------------------------------

test "sqlite_queue: send and receive a single message" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    // Share the same DB file between sender and receiver
    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "shared.db" });
    defer alloc.free(path);

    var sender = try SqliteQueue.init(alloc, path, "sender");
    defer sender.deinit();

    var receiver = try SqliteQueue.init(alloc, path, "receiver");
    defer receiver.deinit();

    try sender.send("receiver", "Hello, World!");

    var msgs = try receiver.receive(10, 2000);
    defer msgs.deinit();

    try std.testing.expectEqual(@as(usize, 1), msgs.messages.len);
    try std.testing.expect(std.mem.indexOf(u8, msgs.messages[0], "Hello, World!") != null);
}

test "sqlite_queue: receive message content is preserved exactly" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "exact.db" });
    defer alloc.free(path);

    var q = try SqliteQueue.init(alloc, path, "self");
    defer q.deinit();

    const payload = "{\"messageType\": \"TEXT\", \"content\": \"Test payload\"}";
    try q.send("self", payload);

    var msgs = try q.receive(10, 2000);
    defer msgs.deinit();

    try std.testing.expectEqual(@as(usize, 1), msgs.messages.len);
    try std.testing.expectEqualStrings(payload, msgs.messages[0]);
}

// ---------------------------------------------------------------------------
// Acknowledge
// ---------------------------------------------------------------------------

test "sqlite_queue: acknowledge marks message as sent" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "ack.db" });
    defer alloc.free(path);

    var q = try SqliteQueue.init(alloc, path, "tester");
    defer q.deinit();

    try q.send("tester", "ack-me");

    var msgs = try q.receive(10, 2000);
    defer msgs.deinit();

    try std.testing.expectEqual(@as(usize, 1), msgs.messages.len);
    const msg_id = msgs.ids[0];

    try q.acknowledge(msg_id);

    // After acknowledge, pending count should be 0
    const stats = try q.getStats();
    try std.testing.expectEqual(@as(i32, 0), stats.pending);
}

test "sqlite_queue: acknowledged messages are not re-delivered" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "redelivery.db" });
    defer alloc.free(path);

    var q = try SqliteQueue.init(alloc, path, "consumer");
    defer q.deinit();

    try q.send("consumer", "one-shot");

    // First receive
    var msgs = try q.receive(10, 2000);
    defer msgs.deinit();
    try q.acknowledge(msgs.ids[0]);

    // Second receive should timeout (no pending messages)
    const result = q.receive(10, 150);
    try std.testing.expectError(QueueError.Timeout, result);
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

test "sqlite_queue: getStats reflects send and acknowledge" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "stats.db" });
    defer alloc.free(path);

    var q = try SqliteQueue.init(alloc, path, "stats-test");
    defer q.deinit();

    // Initially empty
    const s0 = try q.getStats();
    try std.testing.expectEqual(@as(i32, 0), s0.pending);
    try std.testing.expectEqual(@as(i32, 0), s0.total);

    try q.send("stats-test", "msg1");
    try q.send("stats-test", "msg2");

    const s1 = try q.getStats();
    try std.testing.expect(s1.total >= 2);
    try std.testing.expect(s1.pending >= 2);
}

test "sqlite_queue: getStats total does not decrease after acknowledge" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "total.db" });
    defer alloc.free(path);

    var q = try SqliteQueue.init(alloc, path, "totaler");
    defer q.deinit();

    try q.send("totaler", "m1");
    try q.send("totaler", "m2");

    var msgs = try q.receive(10, 2000);
    defer msgs.deinit();
    for (msgs.ids) |id| try q.acknowledge(id);

    const stats = try q.getStats();
    // total should still count the acknowledged messages; pending should be 0
    try std.testing.expect(stats.total >= 2);
    try std.testing.expectEqual(@as(i32, 0), stats.pending);
}

// ---------------------------------------------------------------------------
// Timeout
// ---------------------------------------------------------------------------

test "sqlite_queue: receive times out when no messages are pending" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "timeout.db" });
    defer alloc.free(path);

    var q = try SqliteQueue.init(alloc, path, "waiter");
    defer q.deinit();

    const result = q.receive(10, 100); // 100 ms timeout
    try std.testing.expectError(QueueError.Timeout, result);
}

// ---------------------------------------------------------------------------
// Message size limit
// ---------------------------------------------------------------------------

test "sqlite_queue: send rejects message larger than max_message_size" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "sizelimit.db" });
    defer alloc.free(path);

    var q = try SqliteQueue.init(alloc, path, "size-test");
    defer q.deinit();

    // Build a message larger than the default 1 MiB limit
    const huge_size: usize = 2 * 1024 * 1024; // 2 MiB
    const huge = try alloc.alloc(u8, huge_size);
    defer alloc.free(huge);
    @memset(huge, 'A');

    const result = q.send("receiver", huge);
    try std.testing.expectError(QueueError.MessageTooLong, result);
}

// ---------------------------------------------------------------------------
// Multiple messages, multiple receivers
// ---------------------------------------------------------------------------

test "sqlite_queue: messages are delivered only to intended receiver" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "routing.db" });
    defer alloc.free(path);

    var q_a = try SqliteQueue.init(alloc, path, "alice");
    defer q_a.deinit();
    var q_b = try SqliteQueue.init(alloc, path, "bob");
    defer q_b.deinit();

    // Send to alice
    try q_b.send("alice", "for-alice");

    // Bob tries to receive — should timeout (no messages for bob)
    const bob_result = q_b.receive(10, 150);
    try std.testing.expectError(QueueError.Timeout, bob_result);

    // Alice can receive
    var alice_msgs = try q_a.receive(10, 2000);
    defer alice_msgs.deinit();
    try std.testing.expectEqual(@as(usize, 1), alice_msgs.messages.len);
    try std.testing.expectEqualStrings("for-alice", alice_msgs.messages[0]);
}

// ---------------------------------------------------------------------------
// Send multiple messages in sequence
// ---------------------------------------------------------------------------

test "sqlite_queue: multiple messages are delivered in order" {
    const alloc = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const path = try std.fs.path.join(alloc, &.{ dir, "order.db" });
    defer alloc.free(path);

    var q = try SqliteQueue.init(alloc, path, "orderer");
    defer q.deinit();

    try q.send("orderer", "msg-1");
    try q.send("orderer", "msg-2");
    try q.send("orderer", "msg-3");

    var msgs = try q.receive(10, 2000);
    defer msgs.deinit();

    try std.testing.expectEqual(@as(usize, 3), msgs.messages.len);
    // ORDER BY created_at ASC — first sent should come first
    try std.testing.expectEqualStrings("msg-1", msgs.messages[0]);
    try std.testing.expectEqualStrings("msg-2", msgs.messages[1]);
    try std.testing.expectEqualStrings("msg-3", msgs.messages[2]);
}
