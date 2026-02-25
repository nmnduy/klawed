//! Arena Allocator — Zig Usage Guide
//!
//! **No port needed**: this module documents how to replace `src/arena.h`
//! (a hand-rolled ~767-line C arena) with `std.heap.ArenaAllocator`.
//!
//! ## Why
//!
//! The C arena was introduced specifically to work around the pain of tracking
//! individual `malloc`/`free` pairs.  Zig's `std.heap.ArenaAllocator` gives
//! the same semantics with zero boilerplate and no custom code to maintain.
//!
//! ## Basic usage
//!
//! ```zig
//! var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
//! defer arena.deinit(); // frees ALL allocations at once
//! const allocator = arena.allocator();
//!
//! const s = try allocator.dupe(u8, "hello");
//! const n = try allocator.alloc(u8, 1024);
//! // No need to free `s` or `n` individually — arena.deinit() handles it
//! ```
//!
//! ## Pattern: arena per request/tool-call
//!
//! ```zig
//! fn handleToolCall(gpa: std.mem.Allocator, input: []const u8) ![]u8 {
//!     var arena = std.heap.ArenaAllocator.init(gpa);
//!     defer arena.deinit();
//!     const a = arena.allocator();
//!
//!     // All intermediate allocations use `a`; they disappear at the
//!     // end of this function automatically.
//!     const parsed = try parseInput(a, input);
//!     const result = try buildResponse(a, parsed);
//!
//!     // Return a copy owned by the caller's allocator
//!     return gpa.dupe(u8, result);
//! }
//! ```
//!
//! ## Comparison: C arena.h vs Zig ArenaAllocator
//!
//! | C arena.h                    | std.heap.ArenaAllocator          |
//! |------------------------------|----------------------------------|
//! | `arena_init(&a, 4096)`       | `var a = ArenaAllocator.init(…)` |
//! | `arena_alloc(&a, size)`      | `a.allocator().alloc(T, n)`      |
//! | `arena_strdup(&a, s)`        | `a.allocator().dupe(u8, s)`      |
//! | `arena_free_all(&a)` + reuse | `_ = a.reset(.retain_capacity)`  |
//! | `arena_destroy(&a)`          | `a.deinit()`                     |

const std = @import("std");

/// Re-export for convenience: `Arena` is just `std.heap.ArenaAllocator`.
pub const Arena = std.heap.ArenaAllocator;

// ---------------------------------------------------------------------------
// Tests (demonstrate the usage patterns above compile and work correctly)
// ---------------------------------------------------------------------------

test "Arena: basic alloc and deinit" {
    var arena = Arena.init(std.testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();

    const s = try a.dupe(u8, "hello, arena");
    try std.testing.expectEqualStrings("hello, arena", s);
    // No explicit free needed — arena.deinit() handles it
}

test "Arena: reset retains capacity for reuse" {
    var arena = Arena.init(std.testing.allocator);
    defer arena.deinit();

    {
        const a = arena.allocator();
        const buf = try a.alloc(u8, 64);
        @memset(buf, 'x');
    }

    // Reset the arena without freeing backing memory (pattern for loops)
    _ = arena.reset(.retain_capacity);

    const a2 = arena.allocator();
    const buf2 = try a2.alloc(u8, 32);
    try std.testing.expectEqual(@as(usize, 32), buf2.len);
}

test "Arena: multiple allocations freed together" {
    var arena = Arena.init(std.testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();

    var items = std.ArrayList([]u8).init(a);
    for (0..10) |i| {
        const s = try std.fmt.allocPrint(a, "item_{d}", .{i});
        try items.append(s);
    }
    try std.testing.expectEqual(@as(usize, 10), items.items.len);
    // All freed by arena.deinit()
}
