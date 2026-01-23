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
#include <stdlib.h>
#include <wchar.h>
#include <locale.h>

#define INPUT_WIN_MIN_HEIGHT 2  // Min height for input window (content lines, no borders)
#define INPUT_WIN_MAX_HEIGHT_PERCENT 20  // Max height as percentage of viewport

// Calculate display width of a UTF-8 string
static int utf8_display_width(const char *str) {
    if (!str || !*str) {
        return 0;
    }

    // Save current locale
    char *old_locale = setlocale(LC_ALL, NULL);
    if (old_locale) {
        old_locale = strdup(old_locale);
    }

    // Set to UTF-8 locale for mbstowcs
    setlocale(LC_ALL, "C.UTF-8");

    // Convert to wide characters
    size_t len = mbstowcs(NULL, str, 0);
    if (len == (size_t)-1) {
        // Conversion failed, fall back to strlen (assume ASCII)
        if (old_locale) {
            setlocale(LC_ALL, old_locale);
            free(old_locale);
        }
        return (int)strlen(str);
    }

    wchar_t *wstr = malloc((len + 1) * sizeof(wchar_t));
    if (!wstr) {
        if (old_locale) {
            setlocale(LC_ALL, old_locale);
            free(old_locale);
        }
        return (int)strlen(str);  // Fall back
    }

    mbstowcs(wstr, str, len + 1);

    // Calculate display width using wcswidth
    int width = wcswidth(wstr, len);
    if (width < 0) {
        // wcswidth returns -1 if string contains non-printable wide chars
        width = (int)len;  // Fall back to character count
    }

    free(wstr);

    // Restore locale
    if (old_locale) {
        setlocale(LC_ALL, old_locale);
        free(old_locale);
    }

    return width;
}

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
    (void)pad_width;  // May be used in getmaxyx refresh call later

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

        // For user messages, add padding line before
        if (is_user_message) {
            // Add one blank line for top padding
            waddch(tui->wm.conv_pad, '\n');

            // Render prefix '❯❯❯' with bold user color (3 carets for visibility)
            if (has_colors()) {
                wattron(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_USER) | A_BOLD);
            }
            waddstr(tui->wm.conv_pad, "❯❯❯ ");
            if (has_colors()) {
                wattroff(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_USER) | A_BOLD);
            }
        } else if (is_assistant_message) {
            // Assistant message: use left border decoration (│) on each line
            // Render text with left border and background color filling the box

            // Get pad width for background fill
            int local_pad_height, local_pad_width;
            getmaxyx(tui->wm.conv_pad, local_pad_height, local_pad_width);
            (void)local_pad_height;

            // Border display width (│ = 1 char + space = 2)
            int border_display_width = utf8_display_width("│ ");

            // Helper macro to render a padding line (border + background fill)
            #define RENDER_PADDING_LINE() do { \
                if (has_colors()) { \
                    wattron(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD); \
                } \
                waddstr(tui->wm.conv_pad, "│ "); \
                if (has_colors()) { \
                    wattroff(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD); \
                    wattron(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_ASSISTANT_BG)); \
                } \
                int pad_remaining = local_pad_width - border_display_width; \
                for (int pi = 0; pi < pad_remaining; pi++) { \
                    waddch(tui->wm.conv_pad, ' '); \
                } \
                if (has_colors()) { \
                    wattroff(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_ASSISTANT_BG)); \
                } \
                { \
                    int pad_y, pad_x; \
                    getyx(tui->wm.conv_pad, pad_y, pad_x); \
                    (void)pad_y; \
                    if (pad_x > 0) { \
                        waddch(tui->wm.conv_pad, '\n'); \
                    } \
                } \
            } while(0)

            // Add top padding line
            RENDER_PADDING_LINE();

            if (entry->text && entry->text[0] != '\0') {
                const char *line_start = entry->text;
                const char *p = entry->text;

                while (*p) {
                    // Find end of current line
                    while (*p && *p != '\n') {
                        p++;
                    }

                    // Calculate line length
                    size_t line_len = (size_t)(p - line_start);

                    // Render border (with border color)
                    if (has_colors()) {
                        wattron(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
                    }
                    waddstr(tui->wm.conv_pad, "│ ");
                    if (has_colors()) {
                        wattroff(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
                    }

                    // Calculate text display width
                    int text_display_width = 0;
                    if (line_len > 0) {
                        char *tmp = malloc(line_len + 1);
                        if (tmp) {
                            memcpy(tmp, line_start, line_len);
                            tmp[line_len] = '\0';
                            text_display_width = utf8_display_width(tmp);
                            free(tmp);
                        } else {
                            text_display_width = (int)line_len;  // Fallback
                        }
                    }

                    // Render text content with background
                    if (has_colors()) {
                        wattron(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_ASSISTANT_BG));
                    }
                    waddnstr(tui->wm.conv_pad, line_start, (int)line_len);

                    // Fill remaining line width with background color
                    int used_width = border_display_width + text_display_width;
                    int remaining = local_pad_width - used_width;
                    if (remaining > 0) {
                        for (int j = 0; j < remaining; j++) {
                            waddch(tui->wm.conv_pad, ' ');
                        }
                    }

                    if (has_colors()) {
                        wattroff(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_ASSISTANT_BG));
                    }

                    if (*p == '\n') {
                        // Check if cursor has already wrapped to next line after filling
                        // If we filled to the right edge, ncurses auto-wraps and cursor is at x=0
                        int wrap_y, wrap_x;
                        getyx(tui->wm.conv_pad, wrap_y, wrap_x);
                        (void)wrap_y;
                        if (wrap_x > 0) {
                            // Cursor hasn't wrapped yet, need explicit newline
                            waddch(tui->wm.conv_pad, '\n');
                        }
                        p++;
                        line_start = p;
                    }
                }
            }

            // Add bottom padding line
            RENDER_PADDING_LINE();

            #undef RENDER_PADDING_LINE

            continue;  // Skip regular text/newline handling below
        } else {
            // Write prefix for other (non-user, non-assistant) messages
            if (entry->prefix && entry->prefix[0] != '\0') {
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

        if (entry->text && entry->text[0] != '\0') {
            int text_pair;
            if (is_user_message) {
                // User message: use foreground color (no background)
                text_pair = NCURSES_PAIR_FOREGROUND;
                if (has_colors()) {
                    wattron(tui->wm.conv_pad, COLOR_PAIR(text_pair));
                }
                waddstr(tui->wm.conv_pad, entry->text);
                if (has_colors()) {
                    wattroff(tui->wm.conv_pad, COLOR_PAIR(text_pair));
                }

                // Add newline and one blank line for bottom padding
                waddch(tui->wm.conv_pad, '\n');
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
