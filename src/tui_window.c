/*
 * TUI Window Management
 *
 * Handles window layout, sizing, and viewport control.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "tui_window.h"
#include "tui.h"
#include "tui_input.h"
#include "tui_conversation.h"
#include "logger.h"
#include "window_manager.h"
#include <ncurses.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <wchar.h>
#include <locale.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <termios.h>

#define INPUT_WIN_MIN_HEIGHT 2  // Min height for input window (content lines, no borders)
#define INPUT_WIN_MAX_HEIGHT_PERCENT 20  // Max height as percentage of viewport

// Global flag to detect terminal resize
static volatile sig_atomic_t g_resize_flag = 0;

// Terminal size of the last SUCCESSFUL resize rebuild (-1 = never rebuilt).
// Used to skip the redundant second handling (SIGWINCH flag + KEY_RESIZE fire
// for the same resize event) without dropping genuine size changes.
static int g_last_handled_width = -1;
static int g_last_handled_height = -1;

// Signal handler for window resize
#ifdef SIGWINCH
static void handle_resize(int sig) {
    (void)sig;
    g_resize_flag = 1;
}
#endif

// Validate TUI window state (debug builds)
static void validate_tui_windows(TUIState *tui) {
#ifdef DEBUG
    if (!tui) return;
    window_manager_validate(&tui->wm);
#else
    (void)tui;
#endif
}

// Calculate how many visual lines are needed for input buffer
int tui_window_calculate_needed_lines(const char *buffer, int buffer_len, int win_width, int prompt_len) {
    if (buffer_len == 0) return 1;

    int lines = 1;
    int current_col = prompt_len;  // First line starts after prompt

    for (int i = 0; i < buffer_len; i++) {
        if (buffer[i] == '\n') {
            lines++;
            current_col = 0;  // Newlines don't have prompt
        } else {
            current_col++;
            // All lines have full window width
            if (current_col >= win_width) {
                lines++;
                current_col = 0;
            }
        }
    }

    return lines;
}

// Resize input window dynamically based on content
int tui_window_resize_input(TUIState *tui, int desired_lines) {
    if (!tui || !tui->is_initialized) return -1;

    if (window_manager_resize_input(&tui->wm, desired_lines) != 0) {
        LOG_ERROR("Failed to resize input window via WindowManager");
        return -1;
    }

    // Update input buffer to new window geometry
    if (tui->input_buffer && tui->wm.input_win) {
        int h, w;
        getmaxyx(tui->wm.input_win, h, w);
        tui->input_buffer->win = tui->wm.input_win;
        tui->input_buffer->win_width = w;  // No borders
        tui->input_buffer->win_height = h;
    }

    // Ensure content lines are up to date before refresh
    window_manager_refresh_all(&tui->wm);
    return 0;
}

// Show TODO banner window (wraps window_manager with input buffer sync)
int tui_window_show_todo_banner(TUIState *tui, int height) {
    if (!tui || !tui->is_initialized) return -1;

    if (window_manager_show_todo_window(&tui->wm, height) != 0) {
        return -1;
    }

    // Sync input buffer to the recreated input window
    if (tui->input_buffer && tui->wm.input_win) {
        int h, w;
        getmaxyx(tui->wm.input_win, h, w);
        tui->input_buffer->win = tui->wm.input_win;
        tui->input_buffer->win_width = w;
        tui->input_buffer->win_height = h;
    }

    return 0;
}

// Hide TODO banner window (wraps window_manager with input buffer sync)
void tui_window_hide_todo_banner(TUIState *tui) {
    if (!tui || !tui->is_initialized) return;

    window_manager_hide_todo_window(&tui->wm);

    // Sync input buffer to the recreated input window
    if (tui->input_buffer && tui->wm.input_win) {
        int h, w;
        getmaxyx(tui->wm.input_win, h, w);
        tui->input_buffer->win = tui->wm.input_win;
        tui->input_buffer->win_width = w;
        tui->input_buffer->win_height = h;
    }
}

// Refresh conversation window viewport (using pad)
void tui_window_refresh_conversation_viewport(TUIState *tui) {
    if (!tui) return;
    window_manager_refresh_conversation(&tui->wm);
}

// Validate TUI window state (debug builds only)
void tui_window_validate(TUIState *tui) {
    validate_tui_windows(tui);
}

// Clear the global resize flag
void tui_window_clear_resize_flag(void) {
    g_resize_flag = 0;
}

// Check if terminal resize is pending
int tui_window_resize_pending(void) {
    return g_resize_flag != 0;
}

// Install resize signal handler
int tui_window_install_resize_handler(void) {
#ifdef SIGWINCH
    signal(SIGWINCH, handle_resize);
    return 0;
#else
    return -1;  // SIGWINCH not available on this platform
#endif
}

// Find which entry index is visible at the top of the viewport (scroll_offset).
// Uses the exact pad_start_line recorded for each entry — no estimation.
// Returns the index of the first entry whose start line <= scroll_offset and
// whose next entry's start line > scroll_offset.
// If scroll_offset is 0 or below first entry, returns 0.
static int find_anchor_entry(TUIState *tui, int scroll_offset) {
    if (!tui || tui->entries_count == 0) return 0;
    if (scroll_offset <= 0) return 0;

    for (int i = 0; i < tui->entries_count; i++) {
        int entry_start = tui->entries[i].pad_start_line;
        int next_start = (i + 1 < tui->entries_count)
                             ? tui->entries[i + 1].pad_start_line
                             : INT_MAX;
        if (entry_start >= 0 && entry_start <= scroll_offset && scroll_offset < next_start) {
            return i;
        }
    }
    /* scroll_offset is past all content: anchor to last entry */
    return tui->entries_count - 1;
}

// Handle terminal resize
void tui_handle_resize(TUIState *tui) {
    if (!tui || !tui->is_initialized) return;

    // Skip redundant resize handling. A single terminal resize event reaches
    // this function twice: once via the SIGWINCH flag (event loop) and once
    // via ncurses' KEY_RESIZE key (wgetch still reports the same resize as
    // pending even after endwin()/refresh() handled it). Each call used to
    // run the full endwin()/refresh() cycle + conversation pad rebuild, so a
    // resize event was rebuilt TWICE — during window drags (many events/sec)
    // this doubled the rebuild load and made the TUI appear frozen.
    //
    // getmaxyx(stdscr) is ncurses-cached and still reports the OLD size until
    // endwin()/refresh(), so compare against the REAL terminal size via
    // TIOCGWINSZ (uncached). g_last_handled_width/height hold the size of the
    // last successful rebuild; if they match the real terminal size, nothing
    // changed and the expensive rebuild can be skipped safely.
    {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
            g_last_handled_width > 0 && g_last_handled_height > 0 &&
            ws.ws_col == (unsigned short)g_last_handled_width &&
            ws.ws_row == (unsigned short)g_last_handled_height) {
            LOG_DEBUG("[TUI] Resize requested but terminal size unchanged "
                      "(%dx%d) — skipping redundant rebuild",
                      g_last_handled_width, g_last_handled_height);
            return;
        }
    }

    // Temporarily save scroll position and reset to 0 to avoid accessing
    // invalid pad coordinates during rebuild
    int saved_scroll_offset = tui->wm.conv_scroll_offset;
    tui->wm.conv_scroll_offset = 0;

    // Find the anchor entry (the entry visible at the top of the viewport)
    // before the pad is destroyed, using exact start lines tracked per entry.
    int anchor_entry_idx = find_anchor_entry(tui, saved_scroll_offset);
    LOG_DEBUG("[TUI] Resize anchor: entry %d/%d (old scroll=%d)",
              anchor_entry_idx, tui->entries_count, saved_scroll_offset);

    // Get new screen dimensions to recalculate max input height
    int screen_height, screen_width;
    getmaxyx(stdscr, screen_height, screen_width);
    (void)screen_width;  // Unused

    // Recalculate max input height as 20% of screen height
    int calculated_max_height = (screen_height * INPUT_WIN_MAX_HEIGHT_PERCENT) / 100;
    if (calculated_max_height < INPUT_WIN_MIN_HEIGHT) {
        calculated_max_height = INPUT_WIN_MIN_HEIGHT;
    }

    // Update window manager config with new max height
    tui->wm.config.max_input_height = calculated_max_height;

    // Handle screen resize via WindowManager
    if (window_manager_resize_screen(&tui->wm) != 0) {
        LOG_ERROR("[TUI] WindowManager screen resize failed");
        return;
    }

    // Verify pad was successfully recreated
    if (!tui->wm.conv_pad) {
        LOG_ERROR("[TUI] Conversation pad is NULL after resize");
        return;
    }

    // Update input buffer to point to the new input window (critical for normal mode)
    if (tui->input_buffer && tui->wm.input_win) {
        int h, w;
        getmaxyx(tui->wm.input_win, h, w);
        tui->input_buffer->win = tui->wm.input_win;
        tui->input_buffer->win_width = w;  // No borders
        tui->input_buffer->win_height = h;
        LOG_DEBUG("[TUI] Updated input buffer window pointer after resize");
    }

    // Estimate needed capacity for all entries (conservative: 2 lines per entry minimum)
    int estimated_lines = (tui->entries_count * 2) + 100;
    if (window_manager_ensure_pad_capacity(&tui->wm, estimated_lines) != 0) {
        LOG_ERROR("[TUI] Failed to ensure pad capacity before rebuild");
        return;
    }

    // Rebuild pad content via unified rendering path.
    // Resize creates a fresh pad whose width may differ — all entries must
    // be re-rendered to reflow text at the new width. Mark all dirty so
    // redraw_conversation triggers a full (not incremental) rebuild.
    tui_mark_all_entries_dirty(tui);
    redraw_conversation(tui);

    // Restore scroll position anchored to the entry that was at the top of
    // the viewport before the resize. This preserves the user's reading
    // position even when text reflows due to a width change.
    // pad_start_line is set per-entry by redraw_conversation → render_entry_to_pad.
    int content_lines = window_manager_get_content_lines(&tui->wm);
    int new_scroll = saved_scroll_offset;  // fallback
    if (anchor_entry_idx >= 0 && anchor_entry_idx < tui->entries_count) {
        new_scroll = tui->entries[anchor_entry_idx].pad_start_line;
        LOG_DEBUG("[TUI] Resize: anchor entry %d starts at new line %d",
                  anchor_entry_idx, new_scroll);
    }

    int max_scroll = content_lines - tui->wm.conv_viewport_height;
    if (max_scroll < 0) max_scroll = 0;
    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > max_scroll) new_scroll = max_scroll;
    tui->wm.conv_scroll_offset = new_scroll;

    validate_tui_windows(tui);
    window_manager_refresh_all(&tui->wm);

    // Record the size we successfully rebuilt at, so a duplicate resize event
    // (KEY_RESIZE for the same resize) can be skipped on the next call.
    g_last_handled_width = tui->wm.screen_width;
    g_last_handled_height = tui->wm.screen_height;

    LOG_DEBUG("[TUI] Resize handled via WM (screen=%dx%d, conv_h=%d, status_h=%d, input_h=%d, scroll=%d/%d)",
              tui->wm.screen_width, tui->wm.screen_height, tui->wm.conv_viewport_height,
              tui->wm.status_height, tui->wm.input_height,
              tui->wm.conv_scroll_offset, max_scroll);
}
