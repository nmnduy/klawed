//! sqlite_queue.zig — SQLite-backed reliable message queue
//!
//! Zig port of the core (non-interactive) parts of src/sqlite_queue.c.
//!
//! Provides a durable producer/consumer queue backed by a SQLite database,
//! so messages survive process crashes.  The `pthread` mutex and condition
//! variable are replaced by `std.Thread.Mutex` and `std.Thread.Condition`.
//!
//! The heavy interactive processing logic (TUI callbacks, conversation state
//! seeding, etc.) is left in the C code for now.  This module covers:
//!   - Database open/init/close
//!   - send / receive / acknowledge
//!   - stats / status queries
//!   - error types

const std = @import("std");
const migrations = @import("migrations.zig");
const sqlite = @import("sqlite.zig");
const c = sqlite.c;

// ---------------------------------------------------------------------------
// SQL
// ---------------------------------------------------------------------------

const CREATE_MESSAGES_TABLE: [:0]const u8 =
    \\CREATE TABLE IF NOT EXISTS messages (
    \\    id INTEGER PRIMARY KEY AUTOINCREMENT,
    \\    sender TEXT NOT NULL,
    \\    receiver TEXT NOT NULL,
    \\    message TEXT NOT NULL,
    \\    sent INTEGER DEFAULT 0,
    \\    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    \\    updated_at INTEGER DEFAULT (strftime('%s', 'now'))
    \\);
;

const CREATE_INDEX_SENDER: [:0]const u8 =
    "CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender, sent);";
const CREATE_INDEX_RECEIVER: [:0]const u8 =
    "CREATE INDEX IF NOT EXISTS idx_messages_receiver ON messages(receiver, sent);";

const INSERT_MESSAGE: [:0]const u8 =
    "INSERT INTO messages (sender, receiver, message, sent) VALUES (?, ?, ?, 0);";

const SELECT_MESSAGES: [:0]const u8 =
    "SELECT id, message FROM messages WHERE receiver = ? AND sent = 0 ORDER BY created_at ASC LIMIT ?;";

const ACK_MESSAGE: [:0]const u8 =
    "UPDATE messages SET sent = 1, updated_at = strftime('%s', 'now') WHERE id = ?;";

const COUNT_PENDING: [:0]const u8 =
    "SELECT COUNT(*) FROM messages WHERE sent = 0;";
const COUNT_TOTAL: [:0]const u8 =
    "SELECT COUNT(*) FROM messages;";
const COUNT_UNREAD: [:0]const u8 =
    "SELECT COUNT(*) FROM messages WHERE sender = ? AND sent = 0;";

// ---------------------------------------------------------------------------
// Error types
// ---------------------------------------------------------------------------

pub const QueueError = error{
    DbOpen,
    DbPrepare,
    DbExecute,
    DbBusy,
    Timeout,
    InvalidParam,
    MessageTooLong,
    NotInitialized,
    OutOfMemory,
};

// ---------------------------------------------------------------------------
// Config (read once from env at init time)
// ---------------------------------------------------------------------------

pub const QueueConfig = struct {
    poll_interval_ms: u64 = 300,
    poll_timeout_ms: u64 = 30_000,
    max_retries: u32 = 3,
    max_message_size: usize = 1024 * 1024,
    max_queue_size: u32 = 1000,

    pub fn fromEnv() QueueConfig {
        return .{
            .poll_interval_ms = @intCast(envInt("KLAWED_SQLITE_POLL_INTERVAL", 300)),
            .poll_timeout_ms = @intCast(envInt("KLAWED_SQLITE_POLL_TIMEOUT", 30_000)),
            .max_retries = @intCast(envInt("KLAWED_SQLITE_MAX_RETRIES", 3)),
            .max_message_size = @intCast(envInt("KLAWED_SQLITE_MAX_MESSAGE_SIZE", 1024 * 1024)),
            .max_queue_size = @intCast(envInt("KLAWED_SQLITE_MAX_QUEUE_SIZE", 1000)),
        };
    }
};

// ---------------------------------------------------------------------------
// SqliteQueue
// ---------------------------------------------------------------------------

pub const SqliteQueue = struct {
    db: *c.sqlite3,
    db_path: []const u8, // owned
    sender_name: []const u8, // owned
    config: QueueConfig,
    mutex: std.Thread.Mutex,
    cond: std.Thread.Condition,
    allocator: std.mem.Allocator,

    /// Create a new queue context and initialise the database schema.
    pub fn init(
        allocator: std.mem.Allocator,
        db_path: []const u8,
        sender_name: []const u8,
    ) !SqliteQueue {
        const path_z = try allocator.dupeZ(u8, db_path);
        defer allocator.free(path_z);

        var handle: ?*c.sqlite3 = null;
        if (c.sqlite3_open(path_z.ptr, &handle) != c.SQLITE_OK) {
            if (handle) |h| _ = c.sqlite3_close(h);
            return QueueError.DbOpen;
        }

        var self = SqliteQueue{
            .db = handle.?,
            .db_path = try allocator.dupe(u8, db_path),
            .sender_name = try allocator.dupe(u8, sender_name),
            .config = QueueConfig.fromEnv(),
            .mutex = .{},
            .cond = .{},
            .allocator = allocator,
        };
        errdefer {
            allocator.free(self.db_path);
            allocator.free(self.sender_name);
        }

        // Configure SQLite.
        self.execRaw("PRAGMA journal_mode=WAL;") catch {};
        self.execRaw("PRAGMA synchronous=NORMAL;") catch {};
        _ = c.sqlite3_busy_timeout(self.db, 5000);
        self.execRaw("PRAGMA cache_size=-2000;") catch {};
        self.execRaw("PRAGMA temp_store=MEMORY;") catch {};

        // Create schema.
        try self.execRaw(CREATE_MESSAGES_TABLE);
        self.execRaw(CREATE_INDEX_SENDER) catch {};
        self.execRaw(CREATE_INDEX_RECEIVER) catch {};

        return self;
    }

    pub fn deinit(self: *SqliteQueue) void {
        _ = c.sqlite3_close(self.db);
        self.allocator.free(self.db_path);
        self.allocator.free(self.sender_name);
        self.* = undefined;
    }

    // -----------------------------------------------------------------------
    // Send
    // -----------------------------------------------------------------------

    pub fn send(self: *SqliteQueue, receiver: []const u8, message: []const u8) !void {
        if (message.len > self.config.max_message_size) return QueueError.MessageTooLong;

        const stmt = try self.prepareRaw(INSERT_MESSAGE);
        defer _ = c.sqlite3_finalize(stmt);

        _ = c.sqlite3_bind_text(stmt, 1, self.sender_name.ptr, @intCast(self.sender_name.len), c.SQLITE_STATIC);
        _ = c.sqlite3_bind_text(stmt, 2, receiver.ptr, @intCast(receiver.len), c.SQLITE_STATIC);
        _ = c.sqlite3_bind_text(stmt, 3, message.ptr, @intCast(message.len), c.SQLITE_STATIC);

        if (c.sqlite3_step(stmt) != c.SQLITE_DONE) return QueueError.DbExecute;

        self.mutex.lock();
        self.cond.signal();
        self.mutex.unlock();
    }

    // -----------------------------------------------------------------------
    // Receive (polling with timeout)
    // -----------------------------------------------------------------------

    pub const ReceivedMessages = struct {
        messages: [][]const u8, // each slice is allocator-owned
        ids: []i64,
        allocator: std.mem.Allocator,

        pub fn deinit(self: *ReceivedMessages) void {
            for (self.messages) |m| self.allocator.free(m);
            self.allocator.free(self.messages);
            self.allocator.free(self.ids);
        }
    };

    /// Poll for up to `max_messages` messages addressed to our `sender_name`.
    /// Blocks until a message arrives or `timeout_ms` elapses.
    /// Returns `QueueError.Timeout` on timeout (normal condition).
    pub fn receive(
        self: *SqliteQueue,
        max_messages: u32,
        timeout_ms: u64,
    ) !ReceivedMessages {
        const limit: u32 = if (max_messages == 0) 100 else max_messages;
        const deadline_ns = std.time.nanoTimestamp() + @as(i128, timeout_ms) * std.time.ns_per_ms;

        while (true) {
            // Try to fetch.
            if (try self.tryFetch(limit)) |result| return result;

            // Check deadline.
            if (std.time.nanoTimestamp() >= deadline_ns) return QueueError.Timeout;

            // Short sleep before retry.
            std.time.sleep(50 * std.time.ns_per_ms);
        }
    }

    fn tryFetch(self: *SqliteQueue, limit: u32) !?ReceivedMessages {
        const stmt = try self.prepareRaw(SELECT_MESSAGES);
        defer _ = c.sqlite3_finalize(stmt);

        _ = c.sqlite3_bind_text(stmt, 1, self.sender_name.ptr, @intCast(self.sender_name.len), c.SQLITE_STATIC);
        _ = c.sqlite3_bind_int(stmt, 2, @intCast(limit));

        var msgs = std.ArrayList([]const u8).init(self.allocator);
        var ids = std.ArrayList(i64).init(self.allocator);
        errdefer {
            for (msgs.items) |m| self.allocator.free(m);
            msgs.deinit();
            ids.deinit();
        }

        var rc = c.sqlite3_step(stmt);
        while (rc == c.SQLITE_ROW) {
            const id = c.sqlite3_column_int64(stmt, 0);
            const text_ptr = c.sqlite3_column_text(stmt, 1);
            if (text_ptr != null) {
                const text = std.mem.sliceTo(text_ptr, 0);
                try msgs.append(try self.allocator.dupe(u8, text));
                try ids.append(id);
            }
            rc = c.sqlite3_step(stmt);
        }

        if (msgs.items.len == 0) {
            msgs.deinit();
            ids.deinit();
            return null;
        }

        return ReceivedMessages{
            .messages = try msgs.toOwnedSlice(),
            .ids = try ids.toOwnedSlice(),
            .allocator = self.allocator,
        };
    }

    // -----------------------------------------------------------------------
    // Acknowledge
    // -----------------------------------------------------------------------

    pub fn acknowledge(self: *SqliteQueue, message_id: i64) !void {
        const stmt = try self.prepareRaw(ACK_MESSAGE);
        defer _ = c.sqlite3_finalize(stmt);

        _ = c.sqlite3_bind_int64(stmt, 1, message_id);
        if (c.sqlite3_step(stmt) != c.SQLITE_DONE) return QueueError.DbExecute;
    }

    // -----------------------------------------------------------------------
    // Stats
    // -----------------------------------------------------------------------

    pub const Stats = struct {
        pending: i32,
        total: i32,
        unread: i32,
    };

    pub fn getStats(self: *SqliteQueue) !Stats {
        var stats = Stats{ .pending = 0, .total = 0, .unread = 0 };

        {
            const stmt = try self.prepareRaw(COUNT_PENDING);
            defer _ = c.sqlite3_finalize(stmt);
            if (c.sqlite3_step(stmt) == c.SQLITE_ROW) {
                stats.pending = c.sqlite3_column_int(stmt, 0);
            }
        }
        {
            const stmt = try self.prepareRaw(COUNT_TOTAL);
            defer _ = c.sqlite3_finalize(stmt);
            if (c.sqlite3_step(stmt) == c.SQLITE_ROW) {
                stats.total = c.sqlite3_column_int(stmt, 0);
            }
        }
        {
            const stmt = try self.prepareRaw(COUNT_UNREAD);
            defer _ = c.sqlite3_finalize(stmt);
            _ = c.sqlite3_bind_text(stmt, 1, self.sender_name.ptr, @intCast(self.sender_name.len), c.SQLITE_STATIC);
            if (c.sqlite3_step(stmt) == c.SQLITE_ROW) {
                stats.unread = c.sqlite3_column_int(stmt, 0);
            }
        }
        return stats;
    }

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    fn execRaw(self: *SqliteQueue, sql: [:0]const u8) !void {
        var errmsg: ?[*:0]u8 = null;
        if (c.sqlite3_exec(self.db, sql.ptr, null, null, &errmsg) != c.SQLITE_OK) {
            if (errmsg) |msg| c.sqlite3_free(msg);
            return QueueError.DbExecute;
        }
    }

    fn prepareRaw(self: *SqliteQueue, sql: [:0]const u8) !*c.sqlite3_stmt {
        var stmt: ?*c.sqlite3_stmt = null;
        if (c.sqlite3_prepare_v2(self.db, sql.ptr, -1, &stmt, null) != c.SQLITE_OK) {
            return QueueError.DbPrepare;
        }
        return stmt.?;
    }
};

fn envInt(name: []const u8, default_val: i64) i64 {
    const val = std.posix.getenv(name) orelse return default_val;
    return std.fmt.parseInt(i64, val, 10) catch default_val;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "SqliteQueue init and deinit" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "queue.db" });
    defer allocator.free(p);

    var q = try SqliteQueue.init(allocator, p, "sender-a");
    defer q.deinit();
}

test "SqliteQueue send and receive" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "queue2.db" });
    defer allocator.free(p);

    // Sender queue.
    var qsend = try SqliteQueue.init(allocator, p, "sender-a");
    defer qsend.deinit();

    // Receiver queue (same DB, different sender name).
    var qrecv = try SqliteQueue.init(allocator, p, "receiver-b");
    defer qrecv.deinit();

    try qsend.send("receiver-b", "hello queue");

    var msgs = try qrecv.receive(10, 1000);
    defer msgs.deinit();

    try std.testing.expectEqual(@as(usize, 1), msgs.messages.len);
    try std.testing.expectEqualStrings("hello queue", msgs.messages[0]);
}

test "SqliteQueue acknowledge marks as sent" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "queue3.db" });
    defer allocator.free(p);

    var q = try SqliteQueue.init(allocator, p, "tester");
    defer q.deinit();

    // Send a message to ourselves.
    try q.send("tester", "ack-me");

    var msgs = try q.receive(10, 500);
    defer msgs.deinit();

    // Acknowledge.
    try q.acknowledge(msgs.ids[0]);

    // Should be empty now.
    const stats = try q.getStats();
    try std.testing.expectEqual(@as(i32, 0), stats.pending);
}

test "SqliteQueue receive timeout" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "queue4.db" });
    defer allocator.free(p);

    var q = try SqliteQueue.init(allocator, p, "waiter");
    defer q.deinit();

    const result = q.receive(10, 100); // 100 ms timeout
    try std.testing.expectError(QueueError.Timeout, result);
}

test "SqliteQueue getStats" {
    const allocator = std.testing.allocator;
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const dir = try tmp.dir.realpath(".", &path_buf);
    const p = try std.fs.path.join(allocator, &.{ dir, "queue5.db" });
    defer allocator.free(p);

    var q = try SqliteQueue.init(allocator, p, "stats-tester");
    defer q.deinit();

    try q.send("stats-tester", "msg-1");
    try q.send("stats-tester", "msg-2");

    const stats = try q.getStats();
    try std.testing.expect(stats.total >= 2);
    try std.testing.expect(stats.pending >= 2);
}
