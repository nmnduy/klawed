/*
 * Window Manager - Implementation
 */

#include "window_manager.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

// Default configuration values
const WindowManagerConfig DEFAULT_WINDOW_CONFIG = {
    .min_conv_height = 5,
    .min_input_height = 3,  // 1 line + 2 borders
    .max_input_height = 5,  // 3 lines + 2 borders
    .status_height = 1,
    // No gap between status and input by default
    .padding = 0,
    .conv_h_padding = 1,    // 1 column horizontal padding for conversation
    .initial_pad_capacity = 1000
};

// ============================================================================
// Private Helper Functions
// ============================================================================

// Calculate layout dimensions based on screen size and configuration
static void calculate_layout(WindowManager *wm) {
    if (!wm) return;

    int screen_height = wm->screen_height;
    if (screen_height < 0) screen_height = 0;

    // Ensure input height fits on very small screens. We prefer to always
    // leave at least 1 row for conversation viewport if possible.
    int min_conv_forced = 1; // absolute minimum to avoid invalid prefresh()
    if (screen_height <= 0) {
        wm->input_height = 0;
    } else if (wm->input_height >= screen_height) {
        wm->input_height = screen_height > min_conv_forced
            ? screen_height - min_conv_forced
            : screen_height; // may be 0 or 1
        if (wm->input_height < 0) wm->input_height = 0;
    }

    // Determine if we have space for status window
    int available_height = screen_height - wm->input_height - wm->config.padding;
    if (available_height < 0) available_height = 0;

    // Determine if we have space for status window
    if (available_height < wm->config.min_conv_height + wm->config.status_height) {
        wm->status_height = 0;
        LOG_DEBUG("[WM] No space for status window (screen_h=%d, input_h=%d)",
                  screen_height, wm->input_height);
    } else {
        wm->status_height = wm->config.status_height;
    }

    // Calculate conversation viewport height; be robust to tiny screens
    wm->conv_viewport_height = available_height - wm->status_height;
    if (wm->conv_viewport_height < min_conv_forced) {
        wm->conv_viewport_height = (available_height > 0) ? 1 : 0;
    }

    LOG_DEBUG("[WM] Layout: screen=%dx%d, conv_viewport=%d, status=%d, input=%d, pad=%d",
              wm->screen_width, wm->screen_height,
              wm->conv_viewport_height, wm->status_height,
              wm->input_height, wm->config.padding);
}

// Clear any gap lines between status and input to avoid artifacts
static void clear_gap_between_status_and_input(WindowManager *wm) {
    if (!wm) return;
    if (wm->config.padding <= 0) return;

    int y_start = wm->conv_viewport_height + wm->status_height;
    int y_end = wm->screen_height - wm->input_height - 1;
    if (y_start < 0) y_start = 0;
    if (y_end >= wm->screen_height) y_end = wm->screen_height - 1;
    if (y_start > y_end) return;

    for (int y = y_start; y <= y_end; y++) {
        move(y, 0);
        clrtoeol();
    }
}

// Copy content from old pad to new pad (for capacity expansion)
static int copy_pad_content(WINDOW *old_pad, WINDOW *new_pad,
                           int lines_to_copy, int width) {
    if (!old_pad || !new_pad || lines_to_copy <= 0 || width <= 0) {
        return 0;
    }

    // Get actual dimensions of both pads to avoid out-of-bounds access
    int old_h, old_w, new_h, new_w;
    getmaxyx(old_pad, old_h, old_w);
    getmaxyx(new_pad, new_h, new_w);

    // Clamp copy dimensions to actual pad sizes
    if (lines_to_copy > old_h) lines_to_copy = old_h;
    if (lines_to_copy > new_h) lines_to_copy = new_h;
    if (width > old_w) width = old_w;
    if (width > new_w) width = new_w;

    LOG_DEBUG("[WM] Copying %d lines from old pad to new pad (width=%d, old=%dx%d, new=%dx%d)",
              lines_to_copy, width, old_w, old_h, new_w, new_h);

    for (int y = 0; y < lines_to_copy; y++) {
        for (int x = 0; x < width; x++) {
            chtype ch = mvwinch(old_pad, y, x);
            mvwaddch(new_pad, y, x, ch);
        }
    }

    return 0;
}

// ============================================================================
// Lifecycle Management
// ============================================================================

int window_manager_init(WindowManager *wm, const WindowManagerConfig *config) {
    if (!wm) {
        LOG_ERROR("[WM] NULL window manager passed to init");
        return -1;
    }

    // Zero-initialize
    memset(wm, 0, sizeof(WindowManager));

    // Copy configuration (use defaults if NULL)
    if (config) {
        wm->config = *config;
    } else {
        wm->config = DEFAULT_WINDOW_CONFIG;
    }

    // Get initial screen dimensions
    getmaxyx(stdscr, wm->screen_height, wm->screen_width);

    // Set initial input height
    wm->input_height = wm->config.min_input_height;

    // Calculate layout
    calculate_layout(wm);

    LOG_INFO("[WM] Initializing window manager (screen=%dx%d)",
             wm->screen_width, wm->screen_height);

    // Create conversation pad with horizontal padding
    wm->conv_pad_capacity = wm->config.initial_pad_capacity;
    int conv_pad_width = wm->screen_width - (2 * wm->config.conv_h_padding);
    if (conv_pad_width < 1) conv_pad_width = 1; // Safety check
    wm->conv_pad = newpad(wm->conv_pad_capacity, conv_pad_width);
    if (!wm->conv_pad) {
        LOG_ERROR("[WM] Failed to create conversation pad");
        return -1;
    }
    scrollok(wm->conv_pad, TRUE);
    wm->conv_pad_content_lines = 0;
    wm->conv_scroll_offset = 0;

    LOG_DEBUG("[WM] Created conversation pad (capacity=%d, width=%d, h_padding=%d)",
              wm->conv_pad_capacity, conv_pad_width, wm->config.conv_h_padding);

    // Create status window (if enabled)
    if (wm->status_height > 0) {
        wm->status_win = newwin(wm->status_height, wm->screen_width,
                               wm->conv_viewport_height, 0);
        if (!wm->status_win) {
            LOG_WARN("[WM] Failed to create status window, continuing without it");
            wm->status_height = 0;
        } else {
            LOG_DEBUG("[WM] Created status window (h=%d, w=%d, y=%d)",
                      wm->status_height, wm->screen_width, wm->conv_viewport_height);
        }
    }

    // Create input window
    int input_y = wm->screen_height - wm->input_height;
    if (input_y < 0) input_y = 0;
    wm->input_win = newwin(wm->input_height, wm->screen_width, input_y, 0);
    if (!wm->input_win) {
        LOG_ERROR("[WM] Failed to create input window");
        if (wm->status_win) delwin(wm->status_win);
        delwin(wm->conv_pad);
        return -1;
    }
    keypad(wm->input_win, TRUE);

    LOG_DEBUG("[WM] Created input window (h=%d, w=%d, y=%d)",
              wm->input_height, wm->screen_width, input_y);

    wm->is_initialized = 1;

    // Validate initial state
    window_manager_validate(wm);

    LOG_INFO("[WM] Window manager initialized successfully");
    return 0;
}

void window_manager_destroy(WindowManager *wm) {
    if (!wm || !wm->is_initialized) {
        return;
    }

    LOG_INFO("[WM] Destroying window manager");

    if (wm->conv_pad) {
        delwin(wm->conv_pad);
        wm->conv_pad = NULL;
    }

    if (wm->status_win) {
        delwin(wm->status_win);
        wm->status_win = NULL;
    }

    if (wm->input_win) {
        delwin(wm->input_win);
        wm->input_win = NULL;
    }

    wm->is_initialized = 0;

    LOG_DEBUG("[WM] Window manager destroyed");
}

// ============================================================================
// Window Operations
// ============================================================================

int window_manager_resize_screen(WindowManager *wm) {
    if (!wm || !wm->is_initialized) {
        LOG_ERROR("[WM] Cannot resize uninitialized window manager");
        return -1;
    }

    LOG_INFO("[WM] Handling screen resize");

    // Properly handle ncurses resize
    endwin();
    refresh();
    clear();

    // Get new screen dimensions
    int old_width = wm->screen_width;
    int old_height = wm->screen_height;
    getmaxyx(stdscr, wm->screen_height, wm->screen_width);

    LOG_INFO("[WM] Screen resized from %dx%d to %dx%d",
             old_width, old_height, wm->screen_width, wm->screen_height);

    // Recalculate layout
    calculate_layout(wm);

    // Save current content for restoration
    int old_content_lines = wm->conv_pad_content_lines;
    int old_scroll_offset = wm->conv_scroll_offset;
    int old_capacity = wm->conv_pad_capacity;

    // Recreate conversation pad with new width (accounting for horizontal padding)
    WINDOW *old_pad = wm->conv_pad;
    int conv_pad_width = wm->screen_width - (2 * wm->config.conv_h_padding);
    if (conv_pad_width < 1) conv_pad_width = 1; // Safety check
    wm->conv_pad = newpad(wm->conv_pad_capacity, conv_pad_width);
    if (!wm->conv_pad) {
        LOG_ERROR("[WM] Failed to recreate conversation pad");
        wm->conv_pad = old_pad;  // Restore old pad
        return -1;
    }
    scrollok(wm->conv_pad, TRUE);

    // Copy content from old pad (but content will be re-rendered by caller)
    // We just need to preserve the pad structure
    // Be very defensive about bounds: don't copy more than either pad can hold
    int lines_to_copy = old_content_lines;
    if (lines_to_copy > old_capacity) lines_to_copy = old_capacity;
    if (lines_to_copy > wm->conv_pad_capacity) lines_to_copy = wm->conv_pad_capacity;

    int old_conv_width = old_width - (2 * wm->config.conv_h_padding);
    if (old_conv_width < 1) old_conv_width = 1;
    int width_to_copy = old_conv_width < conv_pad_width ? old_conv_width : conv_pad_width;

    // Additional safety: verify old_pad is still valid before copying
    if (old_pad && lines_to_copy > 0 && width_to_copy > 0) {
        copy_pad_content(old_pad, wm->conv_pad, lines_to_copy, width_to_copy);
    }

    // Delete old pad after copy is complete
    if (old_pad) {
        delwin(old_pad);
        old_pad = NULL;
    }

    LOG_DEBUG("[WM] Recreated conversation pad (capacity=%d, width=%d, h_padding=%d)",
              wm->conv_pad_capacity, conv_pad_width, wm->config.conv_h_padding);

    // Recreate status window (if enabled)
    if (wm->status_win) {
        delwin(wm->status_win);
        wm->status_win = NULL;
    }

    if (wm->status_height > 0) {
        wm->status_win = newwin(wm->status_height, wm->screen_width,
                               wm->conv_viewport_height, 0);
        if (!wm->status_win) {
            LOG_WARN("[WM] Failed to recreate status window");
            wm->status_height = 0;
        } else {
            LOG_DEBUG("[WM] Recreated status window");
        }
    }

    // Recreate input window
    if (wm->input_win) {
        delwin(wm->input_win);
    }

    int input_y = wm->screen_height - wm->input_height;
    if (input_y < 0) input_y = 0;
    wm->input_win = newwin(wm->input_height, wm->screen_width, input_y, 0);
    if (!wm->input_win) {
        LOG_ERROR("[WM] Failed to recreate input window");
        return -1;
    }
    keypad(wm->input_win, TRUE);

    LOG_DEBUG("[WM] Recreated input window");

    // Restore scroll offset (clamped to new valid range)
    wm->conv_scroll_offset = old_scroll_offset;
    int max_scroll = wm->conv_pad_content_lines - wm->conv_viewport_height;
    if (max_scroll < 0) max_scroll = 0;
    if (wm->conv_scroll_offset > max_scroll) {
        wm->conv_scroll_offset = max_scroll;
    }

    // Validate after resize
    window_manager_validate(wm);

    LOG_INFO("[WM] Screen resize complete");
    return 0;
}

int window_manager_ensure_pad_capacity(WindowManager *wm, int needed_lines) {
    if (!wm || !wm->is_initialized) {
        LOG_ERROR("[WM] Cannot expand pad on uninitialized window manager");
        return -1;
    }

    // Validate input
    if (needed_lines <= 0) {
        LOG_ERROR("[WM] Invalid needed_lines: %d", needed_lines);
        return -1;
    }

    // Already have enough capacity?
    if (needed_lines <= wm->conv_pad_capacity) {
        return 0;
    }

    // Calculate new capacity (double until it fits) with overflow checking
    int new_capacity = wm->conv_pad_capacity;
    if (new_capacity == 0) {
        new_capacity = 1;  // Start with at least 1
    }

    while (new_capacity < needed_lines) {
        // Check for integer overflow before doubling
        if (new_capacity > INT_MAX / 2) {
            LOG_ERROR("[WM] Pad capacity doubling would overflow (current=%d, INT_MAX=%d)",
                     new_capacity, INT_MAX);
            return -1;
        }
        new_capacity *= 2;
    }

    LOG_INFO("[WM] Expanding pad capacity from %d to %d lines",
             wm->conv_pad_capacity, new_capacity);

    // Create new larger pad (with horizontal padding)
    int conv_pad_width = wm->screen_width - (2 * wm->config.conv_h_padding);
    if (conv_pad_width < 1) conv_pad_width = 1; // Safety check
    WINDOW *new_pad = newpad(new_capacity, conv_pad_width);
    if (!new_pad) {
        LOG_ERROR("[WM] Failed to create expanded pad");
        return -1;
    }
    scrollok(new_pad, TRUE);

    // Copy existing content
    copy_pad_content(wm->conv_pad, new_pad,
                    wm->conv_pad_content_lines, conv_pad_width);

    // Replace old pad
    delwin(wm->conv_pad);
    wm->conv_pad = new_pad;
    wm->conv_pad_capacity = new_capacity;

    LOG_DEBUG("[WM] Pad expansion complete (new_capacity=%d)", new_capacity);

    return 0;
}

int window_manager_resize_input(WindowManager *wm, int desired_content_lines) {
    if (!wm || !wm->is_initialized) {
        LOG_ERROR("[WM] Cannot resize input on uninitialized window manager");
        return -1;
    }

    // Calculate desired window height (content + borders)
    int new_height = desired_content_lines + 2;  // +2 for borders

    // Clamp to min/max
    if (new_height < wm->config.min_input_height) {
        new_height = wm->config.min_input_height;
    } else if (new_height > wm->config.max_input_height) {
        new_height = wm->config.max_input_height;
    }

    // No change needed?
    if (new_height == wm->input_height) {
        return 0;
    }

    LOG_DEBUG("[WM] Resizing input window from %d to %d lines",
              wm->input_height, new_height);

    wm->input_height = new_height;

    // Recalculate layout
    calculate_layout(wm);

    // Recreate status window if needed
    if (wm->status_win) {
        delwin(wm->status_win);
        wm->status_win = NULL;
    }

    if (wm->status_height > 0) {
        wm->status_win = newwin(wm->status_height, wm->screen_width,
                               wm->conv_viewport_height, 0);
        if (!wm->status_win) {
            LOG_WARN("[WM] Failed to recreate status window after input resize");
            wm->status_height = 0;
        }
    }

    // Recreate input window
    delwin(wm->input_win);
    int input_y = wm->screen_height - wm->input_height;
    wm->input_win = newwin(wm->input_height, wm->screen_width, input_y, 0);
    if (!wm->input_win) {
        LOG_ERROR("[WM] Failed to recreate input window");
        return -1;
    }
    keypad(wm->input_win, TRUE);

    // Adjust scroll offset if viewport changed
    int max_scroll = wm->conv_pad_content_lines - wm->conv_viewport_height;
    if (max_scroll < 0) max_scroll = 0;
    if (wm->conv_scroll_offset > max_scroll) {
        wm->conv_scroll_offset = max_scroll;
    }

    LOG_DEBUG("[WM] Input window resized (new_h=%d, conv_viewport=%d)",
              wm->input_height, wm->conv_viewport_height);

    return 0;
}

// ============================================================================
// Refresh Operations
// ============================================================================

void window_manager_refresh_conversation(WindowManager *wm) {
    if (!wm || !wm->is_initialized || !wm->conv_pad) {
        return;
    }

    if (wm->conv_viewport_height <= 0 || wm->screen_width <= 0) {
        return; // nothing to show or invalid geometry
    }

    // Clamp scroll offset to valid range
    int max_scroll = wm->conv_pad_content_lines - wm->conv_viewport_height;
    if (max_scroll < 0) max_scroll = 0;

    if (wm->conv_scroll_offset < 0) {
        wm->conv_scroll_offset = 0;
    } else if (wm->conv_scroll_offset > max_scroll) {
        wm->conv_scroll_offset = max_scroll;
    }

    // Refresh pad viewport with horizontal padding offset
    // prefresh(pad, pad_y, pad_x, screen_y1, screen_x1, screen_y2, screen_x2)
    int x1 = wm->config.conv_h_padding;
    int y2 = wm->conv_viewport_height - 1;
    int x2 = wm->screen_width - 1 - wm->config.conv_h_padding;
    if (y2 < 0) y2 = 0;
    if (x1 < 0) x1 = 0;
    if (x2 < x1) x2 = x1;
    prefresh(wm->conv_pad,
             wm->conv_scroll_offset, 0,  // pad position
             0, x1,                       // screen top-left (with horizontal offset)
             y2, x2);                     // screen bottom-right (with horizontal offset)
}

void window_manager_refresh_status(WindowManager *wm) {
    if (!wm || !wm->is_initialized || !wm->status_win) {
        return;
    }

    touchwin(wm->status_win);
    wrefresh(wm->status_win);
}

void window_manager_refresh_input(WindowManager *wm) {
    if (!wm || !wm->is_initialized || !wm->input_win) {
        return;
    }

    touchwin(wm->input_win);
    wrefresh(wm->input_win);
}

void window_manager_refresh_all(WindowManager *wm) {
    if (!wm || !wm->is_initialized) {
        return;
    }

    window_manager_refresh_conversation(wm);
    window_manager_refresh_status(wm);
    // Ensure the padding region (if any) stays clean
    clear_gap_between_status_and_input(wm);
    window_manager_refresh_input(wm);
    refresh();  // Refresh stdscr
    doupdate(); // Update physical screen
}

// ============================================================================
// Scrolling Operations
// ============================================================================

void window_manager_scroll(WindowManager *wm, int delta) {
    if (!wm || !wm->is_initialized) {
        return;
    }

    int old_offset = wm->conv_scroll_offset;
    wm->conv_scroll_offset += delta;

    // Clamp to valid range
    int max_scroll = wm->conv_pad_content_lines - wm->conv_viewport_height;
    if (max_scroll < 0) max_scroll = 0;

    if (wm->conv_scroll_offset < 0) {
        wm->conv_scroll_offset = 0;
    } else if (wm->conv_scroll_offset > max_scroll) {
        wm->conv_scroll_offset = max_scroll;
    }

    if (wm->conv_scroll_offset != old_offset) {
        LOG_DEBUG("[WM] Scrolled from %d to %d (delta=%d, max=%d)",
                  old_offset, wm->conv_scroll_offset, delta, max_scroll);
        window_manager_refresh_conversation(wm);
    }
}

void window_manager_scroll_to_bottom(WindowManager *wm) {
    if (!wm || !wm->is_initialized) {
        return;
    }

    int max_scroll = wm->conv_pad_content_lines - wm->conv_viewport_height;
    if (max_scroll < 0) max_scroll = 0;

    wm->conv_scroll_offset = max_scroll;
    window_manager_refresh_conversation(wm);

    LOG_DEBUG("[WM] Scrolled to bottom (offset=%d)", wm->conv_scroll_offset);
}

void window_manager_scroll_to_top(WindowManager *wm) {
    if (!wm || !wm->is_initialized) {
        return;
    }

    wm->conv_scroll_offset = 0;
    window_manager_refresh_conversation(wm);

    LOG_DEBUG("[WM] Scrolled to top");
}

int window_manager_get_scroll_offset(WindowManager *wm) {
    if (!wm || !wm->is_initialized) {
        return 0;
    }
    return wm->conv_scroll_offset;
}

int window_manager_get_max_scroll(WindowManager *wm) {
    if (!wm || !wm->is_initialized) {
        return 0;
    }

    int max_scroll = wm->conv_pad_content_lines - wm->conv_viewport_height;
    return max_scroll < 0 ? 0 : max_scroll;
}

// ============================================================================
// Content Management
// ============================================================================

void window_manager_set_content_lines(WindowManager *wm, int lines) {
    if (!wm || !wm->is_initialized) {
        return;
    }

    wm->conv_pad_content_lines = lines;
    LOG_DEBUG("[WM] Content lines set to %d", lines);
}

int window_manager_get_content_lines(WindowManager *wm) {
    if (!wm || !wm->is_initialized) {
        return 0;
    }
    return wm->conv_pad_content_lines;
}

// ============================================================================
// Validation and Debugging
// ============================================================================

int window_manager_validate(WindowManager *wm) {
#ifdef DEBUG
    if (!wm) {
        LOG_ERROR("[WM] VALIDATION FAILED: NULL window manager");
        return -1;
    }

    if (!wm->is_initialized) {
        LOG_WARN("[WM] VALIDATION: Window manager not initialized");
        return -1;
    }

    // Check that conv_pad is actually a pad
    if (wm->conv_pad && !is_pad(wm->conv_pad)) {
        LOG_ERROR("[WM] VALIDATION FAILED: conv_pad is not a pad!");
        return -1;
    }

    // Check that status/input windows are NOT pads
    if (wm->status_win && is_pad(wm->status_win)) {
        LOG_WARN("[WM] VALIDATION: status_win is a pad (unexpected)");
    }

    if (wm->input_win && is_pad(wm->input_win)) {
        LOG_WARN("[WM] VALIDATION: input_win is a pad (unexpected)");
    }

    // Check dimensions are sane
    if (wm->conv_viewport_height <= 0 || wm->conv_viewport_height > wm->screen_height) {
        LOG_WARN("[WM] VALIDATION: conv_viewport_height=%d out of range (screen=%d)",
                wm->conv_viewport_height, wm->screen_height);
    }

    if (wm->conv_pad_content_lines > wm->conv_pad_capacity) {
        LOG_WARN("[WM] VALIDATION: content_lines=%d exceeds capacity=%d",
                wm->conv_pad_content_lines, wm->conv_pad_capacity);
    }

    if (wm->conv_scroll_offset < 0) {
        LOG_WARN("[WM] VALIDATION: scroll_offset=%d is negative",
                wm->conv_scroll_offset);
    }

    LOG_DEBUG("[WM] Validation passed");
#else
    (void)wm;  // Suppress unused parameter warning
#endif
    return 0;
}

void window_manager_get_status(WindowManager *wm, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    if (!wm) {
        snprintf(buffer, buffer_size, "[WM] NULL");
        return;
    }

    if (!wm->is_initialized) {
        snprintf(buffer, buffer_size, "[WM] Not initialized");
        return;
    }

    snprintf(buffer, buffer_size,
             "[WM] screen=%dx%d, conv_viewport=%d, content=%d/%d, scroll=%d/%d, "
             "status=%d, input=%d",
             wm->screen_width, wm->screen_height,
             wm->conv_viewport_height,
             wm->conv_pad_content_lines, wm->conv_pad_capacity,
             wm->conv_scroll_offset, window_manager_get_max_scroll(wm),
             wm->status_height, wm->input_height);
}
