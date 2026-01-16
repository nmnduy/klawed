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
#include "logger.h"
#include "window_manager.h"
#include <ncurses.h>
#include <signal.h>
#include <string.h>

#define INPUT_WIN_MIN_HEIGHT 2  // Min height for input window (content lines, no borders)
#define INPUT_WIN_MAX_HEIGHT_PERCENT 20  // Max height as percentage of viewport

// Global flag to detect terminal resize
static volatile sig_atomic_t g_resize_flag = 0;

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
    int current_line = 0;

    for (int i = 0; i < buffer_len; i++) {
        if (buffer[i] == '\n') {
            lines++;
            current_line++;
            current_col = 0;  // Newlines don't have prompt
        } else {
            current_col++;
            // All lines have full window width
            if (current_col >= win_width) {
                lines++;
                current_line++;
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

// Handle terminal resize
void tui_handle_resize(TUIState *tui) {
    if (!tui || !tui->is_initialized) return;

    // Temporarily save scroll position and reset to 0 to avoid accessing
    // invalid pad coordinates during rebuild
    int saved_scroll_offset = tui->wm.conv_scroll_offset;
    tui->wm.conv_scroll_offset = 0;

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

    // Rebuild pad content from stored entries (ensures wrapping updates with new width)
    werase(tui->wm.conv_pad);
    int pad_height, pad_width;
    getmaxyx(tui->wm.conv_pad, pad_height, pad_width);

    for (int i = 0; i < tui->entries_count; i++) {
        ConversationEntry *entry = &tui->entries[i];

        // Safety check: ensure we're not writing beyond pad capacity
        int cur_y, cur_x;
        getyx(tui->wm.conv_pad, cur_y, cur_x);
        (void)cur_x;

        if (cur_y >= pad_height - 1) {
            LOG_WARN("[TUI] Pad capacity exceeded during resize rebuild at entry %d/%d (cur_y=%d, pad_height=%d)",
                     i, tui->entries_count, cur_y, pad_height);
            // Expand pad capacity on-the-fly
            int new_capacity = pad_height * 2;
            if (window_manager_ensure_pad_capacity(&tui->wm, new_capacity) != 0) {
                LOG_ERROR("[TUI] Failed to expand pad during rebuild");
                break;
            }
            // Refresh pad dimensions after expansion
            getmaxyx(tui->wm.conv_pad, pad_height, pad_width);
        }

        int mapped_pair = NCURSES_PAIR_FOREGROUND;
        switch (entry->color_pair) {
            case COLOR_PAIR_DEFAULT:
            case COLOR_PAIR_FOREGROUND:
                mapped_pair = NCURSES_PAIR_FOREGROUND;
                break;
            case COLOR_PAIR_USER:
                mapped_pair = NCURSES_PAIR_USER;
                break;
            case COLOR_PAIR_ASSISTANT:
                mapped_pair = NCURSES_PAIR_ASSISTANT;
                break;
            case COLOR_PAIR_TOOL:
            case COLOR_PAIR_STATUS:
                mapped_pair = NCURSES_PAIR_STATUS;
                break;
            case COLOR_PAIR_ERROR:
                mapped_pair = NCURSES_PAIR_ERROR;
                break;
            case COLOR_PAIR_PROMPT:
                mapped_pair = NCURSES_PAIR_PROMPT;
                break;
            case COLOR_PAIR_TODO_COMPLETED:
                mapped_pair = NCURSES_PAIR_TODO_COMPLETED;
                break;
            case COLOR_PAIR_TODO_IN_PROGRESS:
                mapped_pair = NCURSES_PAIR_TODO_IN_PROGRESS;
                break;
            case COLOR_PAIR_TODO_PENDING:
                mapped_pair = NCURSES_PAIR_TODO_PENDING;
                break;
            case COLOR_PAIR_SEARCH:
                mapped_pair = NCURSES_PAIR_SEARCH;
                break;
            default:
                /* Keep default mapped_pair (foreground) */
                break;
        }

        // Check if this is a [User] or [Assistant] message to apply new styling
        int is_user_message = (entry->prefix && strcmp(entry->prefix, "[User]") == 0);
        int is_assistant_message = (entry->prefix && strcmp(entry->prefix, "[Assistant]") == 0);

        // For user messages, add padding line before and draw full-width background
        if (is_user_message) {
            // Add one blank line for top padding
            waddch(tui->wm.conv_pad, '\n');

            // Get updated position after adding blank line
            int msg_y, msg_x;
            getyx(tui->wm.conv_pad, msg_y, msg_x);
            getmaxyx(tui->wm.conv_pad, pad_height, pad_width);
            (void)msg_x;

            // Calculate how many lines the user message will need
            // The prefix " > " takes 3 characters
            int prefix_len = 3;
            int text_len = entry->text ? (int)strlen(entry->text) : 0;
            int needed_lines = tui_window_calculate_needed_lines(entry->text, text_len, pad_width, prefix_len);

            // Draw full-width background for ALL lines the message will occupy
            if (has_colors()) {
                wattron(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_USER_MSG_BG));
            }
            int bg_start_y = msg_y;
            for (int line = 0; line < needed_lines; line++) {
                wmove(tui->wm.conv_pad, bg_start_y + line, 0);
                for (int col = 0; col < pad_width; col++) {
                    waddch(tui->wm.conv_pad, ' ');
                }
            }
            // Keep color active for text rendering

            // Move back to start of first line to draw content on top
            wmove(tui->wm.conv_pad, msg_y, 0);

            // Render prefix '>' with bold user color on top of background
            if (has_colors()) {
                wattron(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_USER) | A_BOLD);
            }
            waddstr(tui->wm.conv_pad, " > ");
            if (has_colors()) {
                wattroff(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_USER) | A_BOLD);
                // Re-enable background color for text
                wattron(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_USER_MSG_BG));
            }
        } else {
            // Write prefix with special handling for Assistant and other messages
            if (entry->prefix && entry->prefix[0] != '\0') {
                if (is_assistant_message) {
                    // Assistant message: '>>>' without background
                    if (has_colors()) {
                        wattron(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
                    }
                    waddstr(tui->wm.conv_pad, ">>>");
                    waddch(tui->wm.conv_pad, ' ');
                    if (has_colors()) {
                        wattroff(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
                    }
                } else {
                    // Other messages: keep original behavior
                    if (has_colors()) {
                        wattron(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
                    }
                    waddstr(tui->wm.conv_pad, entry->prefix);
                    waddch(tui->wm.conv_pad, ' ');
                    if (has_colors()) {
                        wattroff(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
                    }
                }
            }
        }

        if (entry->text && entry->text[0] != '\0') {
            int text_pair;
            if (is_user_message) {
                // User message: text is already on background, just render it
                text_pair = NCURSES_PAIR_USER_MSG_BG;  // Keep background active
                waddstr(tui->wm.conv_pad, entry->text);
                if (has_colors()) {
                    wattroff(tui->wm.conv_pad, COLOR_PAIR(text_pair));
                }

                // Move to end of line and add padding
                int msg_y, msg_x;
                getyx(tui->wm.conv_pad, msg_y, msg_x);
                getmaxyx(tui->wm.conv_pad, pad_height, pad_width);
                (void)msg_x;

                wmove(tui->wm.conv_pad, msg_y, pad_width - 1);
                waddch(tui->wm.conv_pad, '\n');
                // Add one blank line for bottom padding
                waddch(tui->wm.conv_pad, '\n');

                // Skip the regular newline below for user messages
                continue;
            } else if (entry->prefix && entry->prefix[0] != '\0') {
                // Other messages with prefix use foreground
                text_pair = NCURSES_PAIR_FOREGROUND;
                if (has_colors()) {
                    wattron(tui->wm.conv_pad, COLOR_PAIR(text_pair));
                }
                waddstr(tui->wm.conv_pad, entry->text);
                if (has_colors()) {
                    wattroff(tui->wm.conv_pad, COLOR_PAIR(text_pair));
                }
            } else {
                // No prefix: use the mapped pair
                text_pair = mapped_pair;
                if (has_colors()) {
                    wattron(tui->wm.conv_pad, COLOR_PAIR(text_pair));
                }
                waddstr(tui->wm.conv_pad, entry->text);
                if (has_colors()) {
                    wattroff(tui->wm.conv_pad, COLOR_PAIR(text_pair));
                }
            }
        }

        waddch(tui->wm.conv_pad, '\n');
    }

    // Get final cursor position to determine actual content lines
    int cur_y, cur_x;
    getyx(tui->wm.conv_pad, cur_y, cur_x);
    (void)cur_x;
    window_manager_set_content_lines(&tui->wm, cur_y);

    // Restore scroll position (clamped to valid range by window manager)
    tui->wm.conv_scroll_offset = saved_scroll_offset;
    int max_scroll = cur_y - tui->wm.conv_viewport_height;
    if (max_scroll < 0) max_scroll = 0;
    if (tui->wm.conv_scroll_offset > max_scroll) {
        tui->wm.conv_scroll_offset = max_scroll;
    }

    validate_tui_windows(tui);
    window_manager_refresh_all(&tui->wm);
    LOG_DEBUG("[TUI] Resize handled via WM (screen=%dx%d, conv_h=%d, status_h=%d, input_h=%d, scroll=%d/%d)",
              tui->wm.screen_width, tui->wm.screen_height, tui->wm.conv_viewport_height,
              tui->wm.status_height, tui->wm.input_height,
              tui->wm.conv_scroll_offset, max_scroll);
}
