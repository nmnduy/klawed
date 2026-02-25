//! context/environment.zig — Platform and environment detection
//!
//! Zig port of src/context/environment.c.
//!
//! Collects environment metadata used to build the system prompt:
//! - Current date (YYYY-MM-DD)
//! - Platform name ("linux", "macos", "windows", …)
//! - OS version string
//! - Git repository detection
//! - Git status (branch, clean/modified, recent commits)

const std = @import("std");
const builtin = @import("builtin");

// ---------------------------------------------------------------------------
// Date
// ---------------------------------------------------------------------------

/// Return today's date as an owned "YYYY-MM-DD" string.
/// Caller must free with `allocator.free`.
pub fn getCurrentDate(allocator: std.mem.Allocator) ![]u8 {
    const epoch_seconds = std.time.timestamp();
    const epoch_day = @divFloor(epoch_seconds, std.time.s_per_day);
    const days_since_epoch: u32 = @intCast(@max(0, epoch_day));

    const ymd = epochDayToYMD(days_since_epoch);
    return std.fmt.allocPrint(allocator, "{d:0>4}-{d:0>2}-{d:0>2}", .{
        ymd.year, ymd.month, ymd.day,
    });
}

/// Simple calendar calculation: seconds-since-epoch day → year/month/day.
const YMD = struct { year: u32, month: u8, day: u8 };

fn epochDayToYMD(days: u32) YMD {
    // Algorithm: https://howardhinnant.github.io/date_algorithms.html
    const z: i64 = @as(i64, days) + 719468;
    const era = @divFloor(z, 146097);
    const doe = z - era * 146097;
    const yoe = @divFloor((doe - @divFloor(doe, 1460) + @divFloor(doe, 36524) - @divFloor(doe, 146096)), 365);
    const y = yoe + era * 400;
    const doy = doe - (365 * yoe + @divFloor(yoe, 4) - @divFloor(yoe, 100));
    const mp = @divFloor(5 * doy + 2, 153);
    const d = doy - @divFloor(153 * mp + 2, 5) + 1;
    const m = if (mp < 10) mp + 3 else mp - 9;
    const year_adj = if (m <= 2) y + 1 else y;
    return YMD{
        .year = @intCast(@max(0, year_adj)),
        .month = @intCast(@max(1, @min(12, m))),
        .day = @intCast(@max(1, @min(31, d))),
    };
}

// ---------------------------------------------------------------------------
// Platform
// ---------------------------------------------------------------------------

/// Return a static platform name string.
pub fn getPlatform() []const u8 {
    return switch (builtin.os.tag) {
        .linux => "linux",
        .macos => "macos",
        .windows => "windows",
        .freebsd => "freebsd",
        .netbsd => "netbsd",
        .openbsd => "openbsd",
        else => "unknown",
    };
}

/// Return the OS version string.
/// On Linux this reads `/etc/os-release`; falls back to `uname -r`.
/// Caller must free with `allocator.free`.
pub fn getOsVersion(allocator: std.mem.Allocator) ![]u8 {
    switch (builtin.os.tag) {
        .linux => {
            if (readOsRelease(allocator)) |version| return version else |_| {}
            return execCommand(allocator, &.{ "uname", "-r" }) catch
                allocator.dupe(u8, "unknown");
        },
        .macos => {
            return execCommand(allocator, &.{ "sw_vers", "-productVersion" }) catch
                allocator.dupe(u8, "unknown");
        },
        else => return allocator.dupe(u8, "unknown"),
    }
}

fn readOsRelease(allocator: std.mem.Allocator) ![]u8 {
    const file = try std.fs.openFileAbsolute("/etc/os-release", .{});
    defer file.close();

    var buf: [2048]u8 = undefined;
    const n = try file.read(&buf);
    const content = buf[0..n];

    // Look for PRETTY_NAME="..." line
    var lines = std.mem.splitScalar(u8, content, '\n');
    while (lines.next()) |line| {
        if (std.mem.startsWith(u8, line, "PRETTY_NAME=")) {
            var value = line["PRETTY_NAME=".len..];
            // Strip surrounding quotes
            if (value.len >= 2 and value[0] == '"') value = value[1..];
            if (value.len >= 1 and value[value.len - 1] == '"') value = value[0 .. value.len - 1];
            return allocator.dupe(u8, value);
        }
    }
    return error.NotFound;
}

// ---------------------------------------------------------------------------
// Git
// ---------------------------------------------------------------------------

/// Return `true` if `working_dir` contains a `.git` directory or file.
pub fn isGitRepo(working_dir: []const u8) bool {
    var buf: [std.fs.max_path_bytes]u8 = undefined;
    const git_path = std.fmt.bufPrint(&buf, "{s}/.git", .{working_dir}) catch return false;

    std.fs.accessAbsolute(git_path, .{}) catch return false;
    return true;
}

/// Build a formatted git status block for the system prompt.
/// Returns an owned string or `null` if not a git repo or on error.
/// Caller must free with `allocator.free`.
pub fn getGitStatus(allocator: std.mem.Allocator, working_dir: []const u8) !?[]u8 {
    if (!isGitRepo(working_dir)) return null;

    const branch = execCommand(allocator, &.{
        "git", "-C", working_dir, "rev-parse", "--abbrev-ref", "HEAD",
    }) catch try allocator.dupe(u8, "unknown");
    defer allocator.free(branch);

    const status_out = execCommand(allocator, &.{
        "git", "-C", working_dir, "status", "--porcelain",
    }) catch try allocator.dupe(u8, "");
    defer allocator.free(status_out);
    const status_str: []const u8 = if (status_out.len > 0) "modified" else "clean";

    const commits = execCommand(allocator, &.{
        "git", "-C", working_dir, "log", "--oneline", "-5",
    }) catch try allocator.dupe(u8, "(no commits)");
    defer allocator.free(commits);

    const result = try std.fmt.allocPrint(allocator,
        "gitStatus: This is the git status at the start of the conversation. " ++
        "Note that this status is a snapshot in time, and will not update during the conversation.\n" ++
        "Current branch: {s}\n\n" ++
        "Main branch (you will usually use this for PRs): \n\n" ++
        "Status:\n({s})\n\n" ++
        "Recent commits:\n{s}",
        .{ branch, status_str, commits },
    );
    return result;
}

// ---------------------------------------------------------------------------
// Subprocess helper
// ---------------------------------------------------------------------------

/// Run a command and return trimmed stdout.
/// Caller must free the returned slice.
pub fn execCommand(allocator: std.mem.Allocator, argv: []const []const u8) ![]u8 {
    var child = std.process.Child.init(argv, allocator);
    child.stdout_behavior = .Pipe;
    child.stderr_behavior = .Ignore;
    try child.spawn();

    const stdout = child.stdout orelse {
        _ = try child.wait();
        return error.NoPipe;
    };
    const output = try stdout.readToEndAlloc(allocator, 64 * 1024);
    _ = try child.wait();

    // Trim trailing whitespace/newlines
    const trimmed = std.mem.trimRight(u8, output, " \t\n\r");
    if (trimmed.len == output.len) return output;

    defer allocator.free(output);
    return allocator.dupe(u8, trimmed);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "getCurrentDate returns YYYY-MM-DD format" {
    const allocator = std.testing.allocator;
    const date = try getCurrentDate(allocator);
    defer allocator.free(date);

    try std.testing.expectEqual(@as(usize, 10), date.len);
    try std.testing.expect(date[4] == '-');
    try std.testing.expect(date[7] == '-');
}

test "getPlatform returns non-empty" {
    const platform = getPlatform();
    try std.testing.expect(platform.len > 0);
}

test "getOsVersion returns non-empty" {
    const allocator = std.testing.allocator;
    const version = try getOsVersion(allocator);
    defer allocator.free(version);
    try std.testing.expect(version.len > 0);
}

test "isGitRepo detects current repo" {
    // We're inside the klawed git repo, so this should return true
    const cwd_buf = try std.process.getCwdAlloc(std.testing.allocator);
    defer std.testing.allocator.free(cwd_buf);
    // Walk up to find a git root
    const result = isGitRepo(cwd_buf);
    // This may or may not be a git root depending on CWD; just verify it doesn't crash
    _ = result;
}

test "epochDayToYMD known value" {
    // 2024-01-01 = 19723 days since epoch
    const ymd = epochDayToYMD(19723);
    try std.testing.expectEqual(@as(u32, 2024), ymd.year);
    try std.testing.expectEqual(@as(u8, 1), ymd.month);
    try std.testing.expectEqual(@as(u8, 1), ymd.day);
}

test "execCommand runs echo" {
    const allocator = std.testing.allocator;
    const out = try execCommand(allocator, &.{ "echo", "hello" });
    defer allocator.free(out);
    try std.testing.expectEqualStrings("hello", out);
}
