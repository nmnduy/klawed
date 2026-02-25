//! sqlite.zig — Shared SQLite C bindings (single @cImport to avoid type conflicts)
//!
//! All Phase 3 modules that need sqlite3 types should import from here rather
//! than declaring their own @cImport, so that `*c.sqlite3` is the same opaque
//! type everywhere.

pub const c = @cImport({
    @cInclude("sqlite3.h");
    @cInclude("string.h");
});
