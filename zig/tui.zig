//! TUI (Terminal User Interface) module
//!
//! This is the main TUI module that re-exports all TUI submodules.
//! It provides a full-screen ncurses-based interface for klawed.
//!
//! Module structure:
//! - colorscheme: Color theme management and ANSI code generation
//! - builtin_themes: Built-in color theme definitions
//! - theme_explorer: Interactive theme selection UI
//! - input: ncurses-based input handling with keyboard support
//! - core: TUI initialization, cleanup, and core state
//! - render: Rendering functions for TUI components
//! - conversation: Conversation display and management
//! - modes: TUI mode handling (normal, insert, command, search)
//! - window_manager: Window layout and management
//! - file_search: Fuzzy file finder
//! - history_search: Command history search
//! - help_modal: Keyboard shortcuts help overlay
//! - indicators: Status indicators and spinners
//! - banner: Startup banner and version display

const std = @import("std");

// Re-export all TUI modules
pub const colorscheme = @import("tui/colorscheme.zig");
pub const builtin_themes = @import("tui/builtin_themes.zig");
pub const theme_explorer = @import("tui/theme_explorer.zig");
pub const input = @import("tui/input.zig");
pub const core = @import("tui/core.zig");
pub const render = @import("tui/render.zig");
pub const conversation = @import("tui/conversation.zig");
pub const modes = @import("tui/modes.zig");
pub const window_manager = @import("tui/window_manager.zig");
pub const file_search = @import("tui/file_search.zig");
pub const history_search = @import("tui/history_search.zig");
pub const help_modal = @import("tui/help_modal.zig");
pub const indicators = @import("tui/indicators.zig");
pub const banner = @import("tui/banner.zig");

// UI utilities submodule
pub const ui = @import("ui.zig");

// Re-export commonly used types
pub const ColorschemeManager = colorscheme.ColorschemeManager;
pub const Theme = colorscheme.Theme;
pub const Rgb = colorscheme.Rgb;
pub const ColorschemeElement = colorscheme.ColorschemeElement;

pub const TUIMode = core.TUIMode;
pub const TUIInputBoxStyle = core.TUIInputBoxStyle;
pub const TUIResponseStyle = core.TUIResponseStyle;

pub const NCursesInput = input.NCursesInput;
pub const CompletionResult = input.CompletionResult;
pub const CompletionFn = input.CompletionFn;

pub const RenderState = render.RenderState;
pub const ConversationState = conversation.ConversationState;
pub const ConversationEntry = conversation.ConversationEntry;
pub const ModeState = modes.ModeState;
pub const WindowManager = window_manager.WindowManager;
pub const WindowConfig = window_manager.WindowConfig;
pub const FileSearchState = file_search.FileSearchState;
pub const HistorySearchState = history_search.HistorySearchState;
pub const HelpModalState = help_modal.HelpModalState;
pub const SpinnerState = indicators.SpinnerState;
