//! TUI Core module
//!
//! Core types, constants, and definitions shared across TUI submodules.
//!
//! Zig port of tui.h core definitions:
//!   - TUIMode enum (normal/insert/command/search/file_search/history_search)
//!   - TUIInputBoxStyle enum
//!   - TUIResponseStyle enum
//!   - NCURSES_PAIR_* color pair constants

// ---------------------------------------------------------------------------
// TUI Modes
// ---------------------------------------------------------------------------

/// TUI input mode
pub const TUIMode = enum {
    normal,
    insert,
    command,
    search,
    file_search,
    history_search,
};

// ---------------------------------------------------------------------------
// Visual Styles
// ---------------------------------------------------------------------------

/// Input box visual style
pub const TUIInputBoxStyle = enum {
    background, // Background color + left border
    border, // Full border with no background
    horizontal, // Top and bottom border only
    bland, // Just '>>>' caret, no padding (default)
};

/// Response visual style for assistant messages
pub const TUIResponseStyle = enum {
    border, // Left border '│ ' on each line (default)
    caret, // Leading '>>> ' caret, no wrapping borders
};

// ---------------------------------------------------------------------------
// ncurses color pair numbers
// ---------------------------------------------------------------------------

/// Type alias for ncurses color pair numbers (c_short)
pub const ColorPairNum = c_short;

pub const NCURSES_PAIR_FOREGROUND: ColorPairNum = 1;
pub const NCURSES_PAIR_USER: ColorPairNum = 2;
pub const NCURSES_PAIR_ASSISTANT: ColorPairNum = 3;
pub const NCURSES_PAIR_STATUS: ColorPairNum = 4;
pub const NCURSES_PAIR_ERROR: ColorPairNum = 5;
pub const NCURSES_PAIR_PROMPT: ColorPairNum = 6;
pub const NCURSES_PAIR_TODO_COMPLETED: ColorPairNum = 7;
pub const NCURSES_PAIR_TODO_IN_PROGRESS: ColorPairNum = 8;
pub const NCURSES_PAIR_TODO_PENDING: ColorPairNum = 9;
pub const NCURSES_PAIR_TOOL: ColorPairNum = 10;
pub const NCURSES_PAIR_SEARCH: ColorPairNum = 11;
pub const NCURSES_PAIR_INPUT_BG: ColorPairNum = 12;
pub const NCURSES_PAIR_INPUT_BORDER: ColorPairNum = 13;
pub const NCURSES_PAIR_USER_MSG_BG: ColorPairNum = 14;
pub const NCURSES_PAIR_ASSISTANT_BG: ColorPairNum = 15;
pub const NCURSES_PAIR_ASSISTANT_BORDER_BG: ColorPairNum = 16;
pub const NCURSES_PAIR_TOOL_DIM: ColorPairNum = 17;
pub const NCURSES_PAIR_DIFF_CONTEXT: ColorPairNum = 18;

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

const std = @import("std");

test "TUIMode values" {
    try std.testing.expectEqual(TUIMode.normal, TUIMode.normal);
    try std.testing.expectEqual(TUIMode.insert, TUIMode.insert);
    try std.testing.expectEqual(TUIMode.command, TUIMode.command);
}

test "TUIInputBoxStyle values" {
    try std.testing.expectEqual(TUIInputBoxStyle.bland, TUIInputBoxStyle.bland);
    try std.testing.expectEqual(TUIInputBoxStyle.background, TUIInputBoxStyle.background);
}

test "TUIResponseStyle values" {
    try std.testing.expectEqual(TUIResponseStyle.border, TUIResponseStyle.border);
    try std.testing.expectEqual(TUIResponseStyle.caret, TUIResponseStyle.caret);
}

test "NCURSES_PAIR constants" {
    try std.testing.expectEqual(@as(ColorPairNum, 1), NCURSES_PAIR_FOREGROUND);
    try std.testing.expectEqual(@as(ColorPairNum, 2), NCURSES_PAIR_USER);
    try std.testing.expectEqual(@as(ColorPairNum, 3), NCURSES_PAIR_ASSISTANT);
    try std.testing.expectEqual(@as(ColorPairNum, 10), NCURSES_PAIR_TOOL);
    try std.testing.expectEqual(@as(ColorPairNum, 11), NCURSES_PAIR_SEARCH);
}
