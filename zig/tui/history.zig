//! TUI History module
//!
//! Idiomatic Zig replacement for src/tui_history.c and src/tui_history.h
//!
//! Provides input history navigation for the TUI.

const std = @import("std");

// Re-export from history_search module
pub const HistorySearchState = @import("history_search.zig").HistorySearchState;
