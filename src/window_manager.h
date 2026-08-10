/*
 * Window Manager - Centralized ncurses window lifecycle management
 *
 * This module provides robust window management for the TUI, including:
 * - Window creation, destruction, and resizing
 * - Pad management with automatic capacity expansion
 * - Scroll offset management
 * - Layout calculations
 * - Defensive validation
 */

#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <ncurses.h>

// Forward declaration for WINDOW type
typedef struct _win_st WINDOW;

// Safety margin (rows) kept free below the pad's current cursor position.
// Renderers call window_manager_ensure_cursor_room() before writing each
// visual line; keeping this margin prevents the cursor from ever reaching
// the pad's last row, which would trigger ncurses bottom-edge scrolling
// (wscrl) — a full-pad memmove per character (O(n^2) for long sessions).
#define WM_PAD_GROW_MARGIN 64

// Window manager configuration
typedef struct {
    int min_conv_height;      // Minimum conversation viewport height
    int min_input_height;     // Minimum input window height
    int max_input_height;     // Maximum input window height
    int status_height;        // Status window height (0 to disable)
    int padding;              // Padding between windows
    int conv_h_padding;       // Right padding for conversation box
    int initial_pad_capacity; // Initial pad capacity (lines)
    int max_todo_height;      // Maximum TODO window height (default 5)
} WindowManagerConfig;

// Window manager state
typedef struct {
    // Screen dimensions
    int screen_width;
    int screen_height;

    // Conversation pad (virtual scrollable window)
    WINDOW *conv_pad;
    int conv_pad_capacity;      // Total pad height (allocated)
    int conv_pad_content_lines; // Actual content lines (used)
    int conv_viewport_height;   // Visible area height
    int conv_scroll_offset;     // Current vertical scroll position (0 = top)
    int conv_scroll_x;          // Current horizontal scroll position (0 = left, for no-wrap mode)

    // Status window
    WINDOW *status_win;
    int status_height;          // Actual status height (may be 0)

    // Input window
    WINDOW *input_win;
    int input_height;           // Current input window height

    // TODO window (shown when there are incomplete todos)
    WINDOW *todo_win;
    int todo_height;            // Current TODO window height (0 when hidden)

    // Configuration
    WindowManagerConfig config;

    // State flags
    int is_initialized;
} WindowManager;

// Default configuration
extern const WindowManagerConfig DEFAULT_WINDOW_CONFIG;

// ============================================================================
// Lifecycle Management
// ============================================================================

// Initialize window manager and create all windows
// Returns: 0 on success, -1 on failure
int window_manager_init(WindowManager *wm, const WindowManagerConfig *config);

// Destroy all windows and cleanup
void window_manager_destroy(WindowManager *wm);

// ============================================================================
// Window Operations
// ============================================================================

// Handle terminal resize (recreate all windows with new dimensions)
// Returns: 0 on success, -1 on failure
int window_manager_resize_screen(WindowManager *wm);

// Ensure conversation pad has at least the specified capacity
// Automatically expands pad and copies content if needed
// Returns: 0 on success, -1 on failure
int window_manager_ensure_pad_capacity(WindowManager *wm, int needed_lines);

// Ensure the pad has at least `margin` free rows below the current cursor
// position, expanding capacity (and preserving the cursor) if needed.
// Prevents ncurses bottom-edge scrolling during rendering.
// Returns: 0 on success, -1 on failure
int window_manager_ensure_cursor_room(WindowManager *wm, int margin);

// Ensure the pad has room for `text` written at the current cursor position
// with ncurses line wrapping.  Grows the pad IN PLACE (wresize) so the
// cursor and content survive — the cursor must never reach the pad's last
// row, or ncurses bottom-edge scrolling (wscrl, a full-pad memmove per
// character) turns rendering into O(n^2).  The row estimate is
// conservative: one row per newline, <=2 bytes per column, and 8 columns
// per tab (ncurses expands tabs to tab stops).
// Returns: 0 on success, -1 on failure
int window_manager_ensure_room_for_text(WindowManager *wm, const char *text);

// Change the conversation pad width (used when toggling wrap on/off)
// Recreates the pad and copies existing content
// Returns: 0 on success, -1 on failure
int window_manager_set_pad_width(WindowManager *wm, int new_width);

// Resize input window to accommodate the specified number of content lines
// Automatically adjusts conversation viewport height
// Returns: 0 on success, -1 on failure
int window_manager_resize_input(WindowManager *wm, int desired_content_lines);

// ============================================================================
// Refresh Operations
// ============================================================================

// Refresh conversation pad viewport (must be called after content changes)
void window_manager_refresh_conversation(WindowManager *wm);

// Refresh status window (must be called after status changes)
void window_manager_refresh_status(WindowManager *wm);

// Refresh input window (must be called after input changes)
void window_manager_refresh_input(WindowManager *wm);

// Refresh all windows
void window_manager_refresh_all(WindowManager *wm);

// ============================================================================
// TODO Window Operations
// ============================================================================

// Show the TODO window with specified height
// Creates and positions the TODO window above the input window
// Returns: 0 on success, -1 on failure
int window_manager_show_todo_window(WindowManager *wm, int height);

// Hide the TODO window
// Destroys the TODO window and adjusts layout
void window_manager_hide_todo_window(WindowManager *wm);

// Refresh the TODO window (must be called after content changes)
void window_manager_refresh_todo(WindowManager *wm);

// ============================================================================
// Scrolling Operations
// ============================================================================

// Scroll conversation window by delta lines (positive = down, negative = up)
// Automatically clamps to valid range
void window_manager_scroll(WindowManager *wm, int delta);

// Scroll to bottom of conversation
void window_manager_scroll_to_bottom(WindowManager *wm);

// Scroll to top of conversation
void window_manager_scroll_to_top(WindowManager *wm);

// Scroll to a specific content line (0 = first line)
// The target line will be positioned at the top of the viewport if possible
void window_manager_scroll_to_line(WindowManager *wm, int line);

// Get current scroll position (0 = top, max = bottom)
int window_manager_get_scroll_offset(WindowManager *wm);

// Get maximum scroll offset
int window_manager_get_max_scroll(WindowManager *wm);

// Horizontal scrolling (for no-wrap mode)
// Scroll horizontally by delta columns (positive = right, negative = left)
void window_manager_scroll_horizontal(WindowManager *wm, int delta);

// Set horizontal scroll position (clamped to valid range)
void window_manager_set_scroll_x(WindowManager *wm, int scroll_x);

// Get current horizontal scroll position
int window_manager_get_scroll_x(WindowManager *wm);

// Get the actual pad width (may be larger than screen width in no-wrap mode)
int window_manager_get_pad_width(WindowManager *wm);

// ============================================================================
// Content Management
// ============================================================================

// Update content line count (call after adding/removing content)
void window_manager_set_content_lines(WindowManager *wm, int lines);

// Get current content line count
int window_manager_get_content_lines(WindowManager *wm);

// ============================================================================
// Validation and Debugging
// ============================================================================

// Validate window manager state (debug builds only)
// Checks for common issues like pad vs window confusion
// Returns: 0 if valid, -1 if issues detected
int window_manager_validate(WindowManager *wm);

// Get human-readable status string for debugging
// Buffer should be at least 256 bytes
void window_manager_get_status(WindowManager *wm, char *buffer, size_t buffer_size);

#endif // WINDOW_MANAGER_H
