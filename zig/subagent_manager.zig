//! subagent_manager.zig — Subagent process lifecycle management
//!
//! Zig port of src/subagent_manager.c
//!
//! Manages a list of running subagent processes:
//!   - Tracks PIDs, log files, prompts, timeouts
//!   - Thread-safe (Mutex-protected)
//!   - Timeout enforcement via periodic `waitpid` checks

const std = @import("std");

// ---------------------------------------------------------------------------
// Data types
// ---------------------------------------------------------------------------

/// Current state of a tracked subagent process.
pub const SubagentState = enum {
    running,
    completed,
    timed_out,
    interrupted,
};

/// A single tracked subagent process.
pub const SubagentProcess = struct {
    pid: std.posix.pid_t,
    log_file: []u8, // owned
    prompt: []u8, // owned (truncated for display)
    state: SubagentState,
    started_at: i64, // UNIX timestamp
    timeout_s: u32,
    exit_code: ?u8,

    pub fn deinit(self: *SubagentProcess, allocator: std.mem.Allocator) void {
        allocator.free(self.log_file);
        allocator.free(self.prompt);
    }
};

/// Thread-safe manager for a list of subagent processes.
pub const SubagentManager = struct {
    allocator: std.mem.Allocator,
    processes: std.ArrayList(*SubagentProcess),
    mutex: std.Thread.Mutex,

    pub fn init(allocator: std.mem.Allocator) SubagentManager {
        return .{
            .allocator = allocator,
            .processes = std.ArrayList(*SubagentProcess).init(allocator),
            .mutex = .{},
        };
    }

    pub fn deinit(self: *SubagentManager) void {
        self.mutex.lock();
        defer self.mutex.unlock();

        for (self.processes.items) |p| {
            p.deinit(self.allocator);
            self.allocator.destroy(p);
        }
        self.processes.deinit();
    }

    /// Register a new subagent process.
    pub fn add(
        self: *SubagentManager,
        pid: std.posix.pid_t,
        log_file: []const u8,
        prompt: []const u8,
        timeout_s: u32,
    ) !void {
        const proc = try self.allocator.create(SubagentProcess);
        errdefer self.allocator.destroy(proc);

        proc.* = .{
            .pid = pid,
            .log_file = try self.allocator.dupe(u8, log_file),
            .prompt = try self.allocator.dupe(u8, prompt[0..@min(prompt.len, 256)]),
            .state = .running,
            .started_at = std.time.timestamp(),
            .timeout_s = timeout_s,
            .exit_code = null,
        };

        self.mutex.lock();
        defer self.mutex.unlock();
        try self.processes.append(proc);
    }

    /// Poll all running processes: reap completed ones, kill timed-out ones.
    /// Returns the number of processes still running.
    pub fn poll(self: *SubagentManager) usize {
        self.mutex.lock();
        defer self.mutex.unlock();

        const now = std.time.timestamp();
        var running: usize = 0;

        for (self.processes.items) |p| {
            if (p.state != .running) continue;

            // Check if timed out
            if (p.timeout_s > 0 and now - p.started_at > p.timeout_s) {
                _ = std.os.linux.kill(-p.pid, std.posix.SIG.KILL);
                _ = std.os.linux.kill(p.pid, std.posix.SIG.KILL);
                p.state = .timed_out;
                continue;
            }

            // Non-blocking waitpid
            const result = std.posix.waitpid(p.pid, std.posix.W.NOHANG);
            if (result.pid == p.pid) {
                // Process has exited
                p.exit_code = @truncate(result.status >> 8);
                p.state = .completed;
            } else {
                running += 1;
            }
        }
        return running;
    }

    /// Terminate all running subagents. Waits up to `timeout_ms` for them to exit.
    /// Returns the number of processes terminated.
    pub fn terminateAll(self: *SubagentManager, timeout_ms: u64) usize {
        self.mutex.lock();

        var terminated: usize = 0;
        for (self.processes.items) |p| {
            if (p.state == .running) {
                _ = std.os.linux.kill(-p.pid, std.posix.SIG.KILL);
                _ = std.os.linux.kill(p.pid, std.posix.SIG.KILL);
                p.state = .interrupted;
                terminated += 1;
            }
        }
        self.mutex.unlock();

        // Wait a bit for processes to be reaped
        if (terminated > 0) {
            std.time.sleep(timeout_ms * std.time.ns_per_ms);
        }
        return terminated;
    }

    /// Get count of processes in a given state.
    pub fn countByState(self: *SubagentManager, state: SubagentState) usize {
        self.mutex.lock();
        defer self.mutex.unlock();

        var n: usize = 0;
        for (self.processes.items) |p| {
            if (p.state == state) n += 1;
        }
        return n;
    }

    /// Find a process by PID.
    pub fn findByPid(self: *SubagentManager, pid: std.posix.pid_t) ?*SubagentProcess {
        self.mutex.lock();
        defer self.mutex.unlock();

        for (self.processes.items) |p| {
            if (p.pid == pid) return p;
        }
        return null;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "SubagentManager: init and deinit" {
    const allocator = std.testing.allocator;
    var mgr = SubagentManager.init(allocator);
    defer mgr.deinit();
    try std.testing.expectEqual(@as(usize, 0), mgr.processes.items.len);
}

test "SubagentManager: add process" {
    const allocator = std.testing.allocator;
    var mgr = SubagentManager.init(allocator);
    defer mgr.deinit();

    // Add a fake pid (1 = init, always exists but we won't actually wait for it)
    // Use a very large timeout so it won't be killed
    try mgr.add(99999, "/tmp/test.log", "do something", 300);

    try std.testing.expectEqual(@as(usize, 1), mgr.processes.items.len);
    try std.testing.expectEqual(@as(usize, 0), mgr.countByState(.completed));
    try std.testing.expectEqual(@as(usize, 1), mgr.countByState(.running));
}

test "SubagentManager: findByPid" {
    const allocator = std.testing.allocator;
    var mgr = SubagentManager.init(allocator);
    defer mgr.deinit();

    try mgr.add(12345, "/tmp/agent.log", "hello", 60);

    const found = mgr.findByPid(12345);
    try std.testing.expect(found != null);
    try std.testing.expectEqual(@as(std.posix.pid_t, 12345), found.?.pid);

    try std.testing.expect(mgr.findByPid(99999) == null);
}

test "SubagentManager: terminateAll" {
    const allocator = std.testing.allocator;
    var mgr = SubagentManager.init(allocator);
    defer mgr.deinit();

    // Add fake entries (pid won't actually be running, kill will fail silently)
    try mgr.add(99998, "/tmp/a.log", "task a", 300);
    try mgr.add(99997, "/tmp/b.log", "task b", 300);

    const n = mgr.terminateAll(10); // 10ms timeout
    try std.testing.expectEqual(@as(usize, 2), n);
    try std.testing.expectEqual(@as(usize, 2), mgr.countByState(.interrupted));
}
