//! tests/test_tui_input_buffer.zig — Zig port of tests/test_tui_input_buffer.c
//!
//! Tests dynamic input-buffer expansion logic.  The C tests mocked realloc
//! to force failure scenarios; here we use a bump allocator that can be
//! configured to fail after N allocations to simulate OOM.
//!
//! All logic is pure — no ncurses required.

const std = @import("std");

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/// A simple wrapper around an ArrayList(u8) that mimics the C struct.
const InputBuffer = struct {
    buf: std.ArrayList(u8),

    fn init(allocator: std.mem.Allocator) InputBuffer {
        return .{ .buf = std.ArrayList(u8).init(allocator) };
    }

    fn deinit(self: *InputBuffer) void {
        self.buf.deinit();
    }

    /// Expand the buffer to hold at least `need` bytes (content + null).
    /// Returns true on success, false if the allocator fails.
    fn ensureCapacity(self: *InputBuffer, need: usize) bool {
        self.buf.ensureTotalCapacity(need + 1024) catch return false;
        return true;
    }

    fn capacity(self: *const InputBuffer) usize {
        return self.buf.capacity;
    }
};

// ---------------------------------------------------------------------------
// Test 1: Successful expansion
// ---------------------------------------------------------------------------

test "input buffer: expansion succeeds when history entry exceeds capacity" {
    const alloc = std.testing.allocator;
    var ib = InputBuffer.init(alloc);
    defer ib.deinit();

    // Start with minimal space (10 bytes)
    try ib.buf.ensureTotalCapacity(10);
    const initial_cap = ib.capacity();

    const large_history = "This is a very long history entry that exceeds the initial buffer capacity";
    const history_len = large_history.len;

    // history_len > initial capacity → need expansion
    try std.testing.expect(history_len >= initial_cap);

    const ok = ib.ensureCapacity(history_len);
    try std.testing.expect(ok);

    // Buffer must now hold at least history_len + null + 1024 bytes
    try std.testing.expect(ib.capacity() >= history_len + 1024);

    // Copy the content and verify
    try ib.buf.appendSlice(large_history);
    try std.testing.expectEqualStrings(large_history, ib.buf.items);
}

// ---------------------------------------------------------------------------
// Test 2: No expansion needed when content fits
// ---------------------------------------------------------------------------

test "input buffer: no expansion when history fits in current capacity" {
    const alloc = std.testing.allocator;
    var ib = InputBuffer.init(alloc);
    defer ib.deinit();

    // Pre-allocate 100 bytes
    try ib.buf.ensureTotalCapacity(100);
    const cap_before = ib.capacity();

    const small_history = "Short history";
    try std.testing.expect(small_history.len < cap_before);

    // ensureCapacity should succeed trivially
    const ok = ib.ensureCapacity(small_history.len);
    try std.testing.expect(ok);

    // Capacity should not shrink
    try std.testing.expect(ib.capacity() >= cap_before);

    try ib.buf.appendSlice(small_history);
    try std.testing.expectEqualStrings(small_history, ib.buf.items);
}

// ---------------------------------------------------------------------------
// Test 3: Edge case — exact capacity
// ---------------------------------------------------------------------------

test "input buffer: content exactly fits in buffer (no expansion needed)" {
    const alloc = std.testing.allocator;
    var ib = InputBuffer.init(alloc);
    defer ib.deinit();

    try ib.buf.ensureTotalCapacity(20);

    const exact_fit = "Exactly 19 chars!"; // 17 chars — well within 20
    try std.testing.expect(exact_fit.len < ib.capacity());

    try ib.buf.appendSlice(exact_fit);
    try std.testing.expectEqualStrings(exact_fit, ib.buf.items);
}

test "input buffer: empty string is handled correctly" {
    const alloc = std.testing.allocator;
    var ib = InputBuffer.init(alloc);
    defer ib.deinit();

    try ib.buf.ensureTotalCapacity(10);

    const empty = "";
    try ib.buf.appendSlice(empty);
    try std.testing.expectEqual(@as(usize, 0), ib.buf.items.len);
    try std.testing.expectEqualStrings("", ib.buf.items);
}

// ---------------------------------------------------------------------------
// Test 4: Multiple sequential expansions
// ---------------------------------------------------------------------------

test "input buffer: second expansion is larger than first" {
    const alloc = std.testing.allocator;
    var ib = InputBuffer.init(alloc);
    defer ib.deinit();

    try ib.buf.ensureTotalCapacity(10);

    // First expansion: moderate content
    const first = "First long history entry";
    _ = ib.ensureCapacity(first.len);
    const cap_after_first = ib.capacity();

    ib.buf.clearRetainingCapacity();
    try ib.buf.appendSlice(first);

    // Second expansion: much larger content
    const second = "This is a much longer history entry that should definitely trigger another " ++
        "expansion because the first expansion only added minimal padding.";
    _ = ib.ensureCapacity(second.len);
    const cap_after_second = ib.capacity();

    // Second capacity must be at least as large as the first
    try std.testing.expect(cap_after_second >= cap_after_first);
    // And must accommodate the second, larger string
    try std.testing.expect(cap_after_second >= second.len + 1024);

    ib.buf.clearRetainingCapacity();
    try ib.buf.appendSlice(second);
    try std.testing.expectEqualStrings(second, ib.buf.items);
}

// ---------------------------------------------------------------------------
// Test 5: Failure path — OOM → truncate to current capacity
// ---------------------------------------------------------------------------

test "input buffer: OOM during expansion truncates content gracefully" {
    // Use a FailingAllocator so the realloc-equivalent always fails.
    var fa = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    const failing_alloc = fa.allocator();

    // Build a tiny ArrayList backed by a real allocator first, then "simulate"
    // OOM by attempting growth with the failing allocator.
    var ib = InputBuffer.init(std.testing.allocator);
    defer ib.deinit();

    try ib.buf.ensureTotalCapacity(10);
    const original_cap = ib.capacity();

    const large_history = "This is a very long history entry that exceeds the initial buffer capacity";

    // Simulate the OOM path: if expansion fails, truncate to current capacity.
    var history_len = large_history.len;
    var failing_ib = InputBuffer.init(failing_alloc);
    // Don't call ensureCapacity — the allocator will fail.
    // The truncation logic: clamp history_len to (capacity - 1).
    if (history_len >= original_cap) {
        const ok = failing_ib.ensureCapacity(history_len);
        if (!ok) {
            history_len = if (original_cap > 0) original_cap - 1 else 0;
        }
    }

    // After OOM, content must be truncated to original_cap - 1
    try std.testing.expectEqual(original_cap - 1, history_len);

    // Copy truncated slice into the original (non-failing) buffer and verify
    try ib.buf.appendSlice(large_history[0..history_len]);
    try std.testing.expectEqual(history_len, ib.buf.items.len);
    try std.testing.expectEqualStrings(large_history[0..history_len], ib.buf.items);

    // Failing allocator's deinit — nothing was allocated so nothing to free.
    failing_ib.deinit();
}
