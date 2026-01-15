/*
 * TUI Input History Management
 *
 * Handles input history navigation and persistence.
 */

// Define feature test macros before any includes
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "tui_history.h"
#include "tui.h"
#include "tui_input.h"
#include "tui_window.h"
#include "history_search.h"
#include "history_file.h"
#include "logger.h"
#include "array_resize.h"
#include <stdlib.h>
#include <bsd/stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <ncurses.h>

/*
 * Handle Ctrl+R: Start history search popup
 */
int tui_history_start_search(TUIState *tui) {
    if (!tui) {
        return -1;
    }

    LOG_DEBUG("[TUI] Ctrl+R pressed - starting history search (history entries: %p, count: %d)",
              (void *)tui->input_history, tui->input_history_count);
    
    if (history_search_start(&tui->history_search,
                             tui->wm.screen_height,
                             tui->wm.screen_width,
                             tui->input_history,
                             tui->input_history_count) == 0) {
        tui->mode = TUI_MODE_HISTORY_SEARCH;
        history_search_render(&tui->history_search);
        LOG_DEBUG("[TUI] History search started successfully, mode changed to TUI_MODE_HISTORY_SEARCH");
        return 0;
    } else {
        LOG_ERROR("[TUI] Failed to start history search");
        beep();
        return -1;
    }
}

/*
 * Handle history search mode key processing
 */
int tui_history_process_search_key(TUIState *tui, int ch, const char *prompt) {
    if (!tui) {
        return 0;
    }

    LOG_DEBUG("[TUI] Processing key %d in history search mode", ch);
    int result = history_search_process_key(&tui->history_search, ch);
    
    if (result == 1) {
        // Selection made - insert command into input buffer
        const char *selected = history_search_get_selected(&tui->history_search);
        if (selected) {
            tui_input_insert_string(tui->input_buffer, selected);
            LOG_DEBUG("[TUI] Inserted history command: %s", selected);
        }
        history_search_stop(&tui->history_search);
        tui->mode = TUI_MODE_INSERT;
        // Refresh all windows to restore display
        window_manager_refresh_all(&tui->wm);
        tui_redraw_input(tui, prompt);
    } else if (result == -1) {
        // Cancelled
        history_search_stop(&tui->history_search);
        tui->mode = TUI_MODE_INSERT;
        // Refresh all windows to restore display
        window_manager_refresh_all(&tui->wm);
        tui_redraw_input(tui, prompt);
    } else {
        // Continue - just render the popup
        history_search_render(&tui->history_search);
    }
    
    return 0;
}

/*
 * Handle Ctrl+P: Navigate to previous history entry
 */
int tui_history_navigate_prev(TUIState *tui, const char *prompt) {
    if (!tui || !tui->input_buffer) {
        return -1;
    }

    if (tui->input_history_count > 0) {
        if (tui->input_history_index == -1) {
            free(tui->input_saved_before_history);
            tui->input_saved_before_history = strdup(tui->input_buffer->buffer);
            tui->input_history_index = tui->input_history_count;  // one past last
        }
        if (tui->input_history_index > 0) {
            tui->input_history_index--;
            const char *hist = tui->input_history[tui->input_history_index];
            if (hist) {
                size_t len = strlen(hist);

                // Dynamically resize input buffer if history entry is too large
                if (len >= (size_t)tui->input_buffer->capacity) {
                    size_t new_capacity = len + 1024;  // Add some extra space
                    void *buf_ptr = (void *)tui->input_buffer->buffer;
                    if (buffer_reserve(&buf_ptr, &tui->input_buffer->capacity, new_capacity) == 0) {
                        tui->input_buffer->buffer = (char *)buf_ptr;
                        LOG_DEBUG("[TUI] Expanded input buffer to %zu bytes for history entry", new_capacity);
                    } else {
                        // If resize fails, truncate to current capacity
                        LOG_WARN("[TUI] Failed to expand input buffer, truncating history entry");
                        len = (size_t)tui->input_buffer->capacity - 1;
                    }
                }

                memcpy(tui->input_buffer->buffer, hist, len);
                tui->input_buffer->buffer[len] = '\0';
                tui->input_buffer->length = (int)len;
                tui->input_buffer->cursor = (int)len;
                tui->input_buffer->view_offset = 0;
                tui->input_buffer->line_scroll_offset = 0;
                tui_redraw_input(tui, prompt);
            }
        }
    }
    
    return 0;
}

/*
 * Handle Ctrl+N: Navigate to next history entry
 */
int tui_history_navigate_next(TUIState *tui, const char *prompt) {
    if (!tui || !tui->input_buffer) {
        return -1;
    }

    if (tui->input_history_index != -1) {
        tui->input_history_index++;
        if (tui->input_history_index >= tui->input_history_count) {
            // restore saved input
            const char *saved = tui->input_saved_before_history ? tui->input_saved_before_history : "";
            size_t len = strlen(saved);

            // Dynamically resize input buffer if saved input is too large
            if (len >= (size_t)tui->input_buffer->capacity) {
                size_t new_capacity = len + 1024;  // Add some extra space
                void *buf_ptr = (void *)tui->input_buffer->buffer;
                if (buffer_reserve(&buf_ptr, &tui->input_buffer->capacity, new_capacity) == 0) {
                    tui->input_buffer->buffer = (char *)buf_ptr;
                    LOG_DEBUG("[TUI] Expanded input buffer to %zu bytes for saved input", new_capacity);
                } else {
                    // If resize fails, truncate to current capacity
                    LOG_WARN("[TUI] Failed to expand input buffer, truncating saved input");
                    len = (size_t)tui->input_buffer->capacity - 1;
                }
            }

            memcpy(tui->input_buffer->buffer, saved, len);
            tui->input_buffer->buffer[len] = '\0';
            tui->input_buffer->length = (int)len;
            tui->input_buffer->cursor = (int)len;
            tui->input_buffer->view_offset = 0;
            tui->input_buffer->line_scroll_offset = 0;
            tui->input_history_index = -1;
            tui_redraw_input(tui, prompt);
        } else {
            const char *hist = tui->input_history[tui->input_history_index];
            if (hist) {
                size_t len = strlen(hist);

                // Dynamically resize input buffer if history entry is too large
                if (len >= (size_t)tui->input_buffer->capacity) {
                    size_t new_capacity = len + 1024;  // Add some extra space
                    void *buf_ptr = (void *)tui->input_buffer->buffer;
                    if (buffer_reserve(&buf_ptr, &tui->input_buffer->capacity, new_capacity) == 0) {
                        tui->input_buffer->buffer = (char *)buf_ptr;
                        LOG_DEBUG("[TUI] Expanded input buffer to %zu bytes for history entry", new_capacity);
                    } else {
                        // If resize fails, truncate to current capacity
                        LOG_WARN("[TUI] Failed to expand input buffer, truncating history entry");
                        len = (size_t)tui->input_buffer->capacity - 1;
                    }
                }

                memcpy(tui->input_buffer->buffer, hist, len);
                tui->input_buffer->buffer[len] = '\0';
                tui->input_buffer->length = (int)len;
                tui->input_buffer->cursor = (int)len;
                tui->input_buffer->view_offset = 0;
                tui->input_buffer->line_scroll_offset = 0;
                tui_redraw_input(tui, prompt);
            }
        }
    }
    
    return 0;
}

/*
 * Append input to history (both in-memory and persistent)
 */
void tui_history_append(TUIState *tui, const char *input) {
    if (!tui || !input) {
        return;
    }

    // Save to persistent history file
    if (tui->history_file) {
        history_file_append(tui->history_file, input);
    }
    
    // Append to in-memory history with simple de-dup of last entry
    if (tui->input_history_count == 0 ||
        strcmp(tui->input_history[tui->input_history_count - 1], input) != 0) {
        // Ensure capacity
        if (tui->input_history_count >= tui->input_history_capacity) {
            int new_cap = tui->input_history_capacity > 0 ? tui->input_history_capacity * 2 : 100;
            char **new_arr = reallocarray(tui->input_history, (size_t)new_cap, sizeof(char*));
            if (new_arr) {
                tui->input_history = new_arr;
                tui->input_history_capacity = new_cap;
            }
        }
        if (tui->input_history_count < tui->input_history_capacity) {
            tui->input_history[tui->input_history_count++] = strdup(input);
        }
    }
    
    // Reset history navigation state after append
    tui_history_reset_navigation(tui);
}

/*
 * Reset history navigation state
 */
void tui_history_reset_navigation(TUIState *tui) {
    if (!tui) {
        return;
    }

    free(tui->input_saved_before_history);
    tui->input_saved_before_history = NULL;
    tui->input_history_index = -1;
}
