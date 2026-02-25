//! TUI Window module
//!
//! Idiomatic Zig replacement for src/tui_window.c and src/tui_window.h
//!
//! Provides window management utilities for the TUI.

const std = @import("std");

// Re-export from window_manager module
pub const WindowManager = @import("window_manager.zig").WindowManager;
pub const WindowConfig = @import("window_manager.zig").WindowConfig;
