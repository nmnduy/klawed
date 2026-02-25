//! ArrayList Usage Guide
//!
//! **No port needed**: this module documents how to replace `src/array_resize.c`
//! (manual `realloc`-based dynamic arrays) with `std.ArrayList`.
//!
//! ## Why
//!
//! `src/array_resize.c` implemented a generic grow-on-append pattern using raw
//! `realloc` with manual size/capacity tracking and `reallocarray` for overflow
//! safety.  Zig's `std.ArrayList(T)` provides the same semantics built-in, with
//! compile-time type safety, iterator support, and no manual memory math.
//!
//! ## Comparison: C array_resize vs std.ArrayList
//!
//! | C (array_resize.c)                          | std.ArrayList(T)                    |
//! |---------------------------------------------|-------------------------------------|
//! | `array_init(&arr, sizeof(T), initial_cap)`  | `ArrayList(T).init(allocator)`      |
//! | `array_append(&arr, &item)`                 | `list.append(item)`                 |
//! | `array_get(&arr, i)`                        | `list.items[i]`                     |
//! | `array_len(&arr)`                           | `list.items.len`                    |
//! | `array_free(&arr)`                          | `list.deinit()`                     |
//! | Manual `reallocarray` growth                | Automatic doubling, safe by default |
//!
//! ## Basic string-builder pattern
//!
//! ```zig
//! var buf = std.ArrayList(u8).init(allocator);
//! defer buf.deinit();
//! try buf.appendSlice("hello");
//! try buf.append(' ');
//! try buf.appendSlice("world");
//! const result = try buf.toOwnedSlice(); // caller frees
//! ```
//!
//! ## Generic typed list
//!
//! ```zig
//! var items = std.ArrayList(u32).init(allocator);
//! defer items.deinit();
//! try items.append(1);
//! try items.append(2);
//! try items.append(3);
//! for (items.items) |v| {
//!     std.debug.print("{d}\n", .{v});
//! }
//! ```
//!
//! ## Pre-allocating capacity (like C `initial_cap`)
//!
//! ```zig
//! var list = std.ArrayList(u8).init(allocator);
//! defer list.deinit();
//! try list.ensureTotalCapacity(256); // reserve without initialising
//! ```

const std = @import("std");

/// Convenience re-export so callers can write `array_list.ArrayList(T)`.
pub fn ArrayList(comptime T: type) type {
    return std.ArrayList(T);
}

// ---------------------------------------------------------------------------
// Tests (demonstrate the usage patterns compile and work correctly)
// ---------------------------------------------------------------------------

test "ArrayList(u8): string builder" {
    var buf = std.ArrayList(u8).init(std.testing.allocator);
    defer buf.deinit();

    try buf.appendSlice("hello");
    try buf.append(' ');
    try buf.appendSlice("world");

    try std.testing.expectEqualStrings("hello world", buf.items);
}

test "ArrayList(u8): toOwnedSlice transfers ownership" {
    var buf = std.ArrayList(u8).init(std.testing.allocator);
    // No defer buf.deinit() — toOwnedSlice consumes the list
    try buf.appendSlice("owned");
    const s = try buf.toOwnedSlice();
    defer std.testing.allocator.free(s);
    try std.testing.expectEqualStrings("owned", s);
}

test "ArrayList(u32): typed list" {
    var list = ArrayList(u32).init(std.testing.allocator);
    defer list.deinit();

    try list.append(10);
    try list.append(20);
    try list.append(30);

    try std.testing.expectEqual(@as(usize, 3), list.items.len);
    try std.testing.expectEqual(@as(u32, 20), list.items[1]);
}

test "ArrayList: ensureTotalCapacity pre-allocates" {
    var list = std.ArrayList(u8).init(std.testing.allocator);
    defer list.deinit();

    try list.ensureTotalCapacity(256);
    try std.testing.expect(list.capacity >= 256);
    try std.testing.expectEqual(@as(usize, 0), list.items.len);
}
