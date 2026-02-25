//! TUI Search module
//!
//! Idiomatic Zig replacement for src/tui_search.c and src/tui_search.h
//!
//! Provides search functionality for the TUI conversation.

const std = @import("std");

// Re-export from modes module for search functionality
pub const ModeState = @import("modes.zig").ModeState;
