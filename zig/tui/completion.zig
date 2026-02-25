//! TUI Completion module
//!
//! Idiomatic Zig replacement for src/tui_completion.c and src/tui_completion.h
//!
//! Provides tab completion support for the TUI input.

const std = @import("std");

// Re-export from input module
pub const CompletionResult = @import("input.zig").CompletionResult;
pub const CompletionFn = @import("input.zig").CompletionFn;
