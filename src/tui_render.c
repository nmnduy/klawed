/*
 * TUI Rendering & Display
 *
 * Handles all rendering operations including:
 * - Status window rendering with spinner
 * - Conversation pad rendering
 * - Input window rendering
 * - Search highlighting
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "tui_render.h"
#include "tui.h"
#include "tui_input.h"
#include "tui_window.h"
#define COLORSCHEME_EXTERN
#include "colorscheme.h"
#include "fallback_colors.h"
#include "logger.h"
#include "indicators.h"
#include "window_manager.h"
#include "klawed_internal.h"
#include "persistence.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include <ncurses.h>
#include <bsd/string.h>
#include <stdlib.h>
#include <wchar.h>
#include <locale.h>

// ============================================================================
// Helper Functions
// ============================================================================

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
    free(wstr);

    // Restore locale
    if (old_locale) {
        setlocale(LC_ALL, old_locale);
        free(old_locale);
    }

    // If wcswidth returns -1 (unknown characters), fall back to character count
    if (width < 0) {
        return (int)len;
    }

    return width;
}

// ============================================================================
// Spinner Functions
// ============================================================================

static const spinner_variant_t* status_spinner_variant(void) {
    init_global_spinner_variant();
    if (GLOBAL_SPINNER_VARIANT.frames && GLOBAL_SPINNER_VARIANT.count > 0) {
        return &GLOBAL_SPINNER_VARIANT;
    }
    static const spinner_variant_t fallback_variant = { SPINNER_FRAMES, SPINNER_FRAME_COUNT };
    return &fallback_variant;
}

static uint64_t monotonic_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int status_message_wants_spinner(const char *message) {
    if (!message) {
        return 0;
    }
    if (strstr(message, "...")) {
        return 1;
    }
    if (strstr(message, "\xE2\x80\xA6")) { // Unicode ellipsis
        return 1;
    }
    return 0;
}

static void status_spinner_start(TUIState *tui) {
    if (!tui) {
        return;
    }
    if (!tui->status_spinner_active) {
        tui->status_spinner_frame = 0;
    }
    tui->status_spinner_active = 1;
    tui->status_spinner_last_update_ns = monotonic_time_ns();
}

static void status_spinner_stop(TUIState *tui) {
    if (!tui) {
        return;
    }
    tui->status_spinner_active = 0;
    tui->status_spinner_frame = 0;
    tui->status_spinner_last_update_ns = 0;
}

// ============================================================================
// Status Window Rendering
// ============================================================================

void render_status_window(TUIState *tui) {
    if (!tui || !tui->wm.status_win) {
        return;
    }

    int height, width;
    getmaxyx(tui->wm.status_win, height, width);
    (void)height;

    // Get narrow screen threshold from environment variable
    // Default is 80 characters (standard terminal width)
    const char *narrow_threshold_str = getenv("KLAWED_NARROW_SCREEN_THRESHOLD");
    int narrow_threshold = 80; // default
    if (narrow_threshold_str) {
        char *endptr;
        long val = strtol(narrow_threshold_str, &endptr, 10);
        if (endptr != narrow_threshold_str && *endptr == '\0' && val >= 0 && val <= 1000) {
            narrow_threshold = (int)val;
        }
    }

    werase(tui->wm.status_win);

    int col = 0;

    // Prepare status message string (agent status - rightmost)
    char status_str[256] = {0};
    int status_str_len = 0;  // Byte length for mvwaddnstr
    int status_display_width = 0;  // Display width for positioning
    int has_spinner = 0;
    if (tui->status_visible && tui->status_message && tui->status_message[0] != '\0') {
        if (tui->status_spinner_active) {
            const spinner_variant_t *variant = status_spinner_variant();
            int frame_count = variant->count;
            const char **frames = variant->frames;
            if (!frames || frame_count <= 0) {
                frames = SPINNER_FRAMES;
                frame_count = SPINNER_FRAME_COUNT;
            }
            const char *frame = frames[tui->status_spinner_frame % frame_count];

            // When screen is narrow, show only spinner without text
            // to make space for token count and scroll percentage
            if (width < narrow_threshold) {
                snprintf(status_str, sizeof(status_str), "%s", frame);
            } else {
                snprintf(status_str, sizeof(status_str), "%s %s ", frame, tui->status_message);
            }
            has_spinner = 1;
        } else {
            // When screen is narrow, hide status text entirely
            // to make space for token count and scroll percentage
            if (width >= narrow_threshold) {
                snprintf(status_str, sizeof(status_str), "%s ", tui->status_message);
            }
        }
        status_str_len = (int)strlen(status_str);
        status_display_width = utf8_display_width(status_str);
    }

    // Prepare plan mode indicator (if enabled) - always visible regardless of mode
    char plan_str[16] = {0};
    int plan_str_len = 0;
    int plan_mode = 0;

    // Read plan mode from conversation state with proper locking
    if (tui->conversation_state) {
        if (conversation_state_lock(tui->conversation_state) == 0) {
            plan_mode = tui->conversation_state->plan_mode;
            conversation_state_unlock(tui->conversation_state);
            LOG_FINE("[TUI] render_status_window: plan_mode=%d, width=%d", plan_mode, width);
        } else {
            LOG_WARN("[TUI] Failed to lock conversation state for plan_mode read");
        }
    } else {
        LOG_WARN("[TUI] No conversation state for plan_mode read");
    }

    int plan_display_width = 0;
    if (plan_mode) {
        snprintf(plan_str, sizeof(plan_str), " ● Plan ");
        plan_str_len = (int)strlen(plan_str);
        plan_display_width = utf8_display_width(plan_str);
        LOG_FINE("[TUI] Plan mode indicator: '%s' (len=%d, display_width=%d)", plan_str, plan_str_len, plan_display_width);
    }

    // Prepare scroll percentage in NORMAL mode
    char scroll_str[32] = {0};
    int scroll_str_len = 0;
    int scroll_display_width = 0;
    if (tui->mode == TUI_MODE_NORMAL) {
        int scroll_offset = window_manager_get_scroll_offset(&tui->wm);
        int max_scroll = window_manager_get_max_scroll(&tui->wm);
        int content_lines = window_manager_get_content_lines(&tui->wm);

        // Calculate percentage with rounding
        int percentage;
        if (content_lines == 0 || max_scroll <= 0) {
            // No content or everything fits in viewport
            percentage = 100;
        } else if (scroll_offset <= 0) {
            percentage = 0;
        } else if (scroll_offset >= max_scroll) {
            percentage = 100;
        } else {
            // Use rounding instead of truncation for better accuracy
            // (scroll_offset * 100 + max_scroll/2) / max_scroll gives proper rounding
            percentage = (scroll_offset * 100 + max_scroll / 2) / max_scroll;
            // Clamp to 0-100 just in case
            if (percentage < 0) percentage = 0;
            if (percentage > 100) percentage = 100;
        }

        snprintf(scroll_str, sizeof(scroll_str), " %d%% ", percentage);
        scroll_str_len = (int)strlen(scroll_str);
        scroll_display_width = utf8_display_width(scroll_str);
    }

    // Prepare token usage (show when non-zero, regardless of mode)
    char token_str[128] = {0};
    int token_str_len = 0;
    int token_display_width = 0;

    // Query total prompt/completion tokens and cached tokens for this session
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int cached_tokens = 0;
    if (tui->persistence_db) {
        if (persistence_get_session_token_usage(tui->persistence_db,
                                                tui->session_id,
                                                &prompt_tokens,
                                                &completion_tokens,
                                                &cached_tokens) == 0) {
            LOG_FINE("[TUI] Retrieved session token totals from DB: prompt=%d completion=%d cached=%d",
                     prompt_tokens, completion_tokens, cached_tokens);
        } else {
            LOG_FINE("[TUI] Failed to retrieve session token totals from DB");
        }
    } else {
        LOG_FINE("[TUI] No persistence database connection available");
    }

    int total_tokens = prompt_tokens + completion_tokens;

    // Show token count when non-zero, regardless of mode
    if (total_tokens > 0) {
        // Show total tokens and cached tokens in the format: "Token: X (+Y cached) "
        if (cached_tokens > 0) {
            snprintf(token_str, sizeof(token_str), "Token: %d (+%d cached) ",
                     total_tokens, cached_tokens);
        } else {
            snprintf(token_str, sizeof(token_str), "Token: %d ", total_tokens);
        }
        token_str_len = (int)strlen(token_str);
        token_display_width = utf8_display_width(token_str);
        LOG_FINE("[TUI] Rendering token display: %s (mode=%d)", token_str, tui->mode);
    }

    // Layout: keep spinner + status message on the right, float the rest on the left
    // Left side (in order): plan mode, scroll %, token usage
    // Right side: spinner + LLM status message

    int status_col = width - status_display_width;
    if (status_col < 0) status_col = 0;

    // Render status message on the right (rightmost position)
    if (status_str_len > 0 && status_str_len < width) {
        if (has_colors()) {
            wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
        } else {
            wattron(tui->wm.status_win, A_BOLD);
        }
        mvwaddnstr(tui->wm.status_win, 0, status_col, status_str, status_str_len);
        if (has_colors()) {
            wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
        } else {
            wattroff(tui->wm.status_win, A_BOLD);
        }
    }

    // Float left-aligned info, stopping before the status block starts
    int left_col = 0;
    int left_limit = status_col;

    // Plan mode indicator (always visible when enabled)
    if (plan_str_len > 0 && plan_display_width < width && left_col + plan_display_width < left_limit) {
        LOG_FINE("[TUI] Rendering plan mode at col=%d, width=%d, plan_display_width=%d, mode=%d",
                  left_col, width, plan_display_width, tui->mode);
        if (has_colors()) {
            wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
        } else {
            wattron(tui->wm.status_win, A_BOLD);
        }
        mvwaddnstr(tui->wm.status_win, 0, left_col, plan_str, plan_str_len);
        if (has_colors()) {
            wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
        } else {
            wattroff(tui->wm.status_win, A_BOLD);
        }
        left_col += plan_display_width;
    } else if (plan_str_len > 0) {
        LOG_FINE("[TUI] Plan mode indicator not rendered: plan_display_width=%d, width=%d, condition=%d",
                  plan_display_width, width, (plan_str_len > 0 && plan_display_width < width));
    }

    // Scroll percentage (NORMAL mode only)
    if (scroll_str_len > 0 && scroll_display_width < width && left_col + scroll_display_width < left_limit) {
        if (has_colors()) {
            wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }
        mvwaddnstr(tui->wm.status_win, 0, left_col, scroll_str, scroll_str_len);
        if (has_colors()) {
            wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }
        left_col += scroll_display_width;
    }

    // Token usage (NORMAL mode only)
    if (token_str_len > 0 && token_display_width < width && left_col + token_display_width < left_limit) {
        if (has_colors()) {
            wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_ASSISTANT));
        }
        mvwaddnstr(tui->wm.status_win, 0, left_col, token_str, token_str_len);
        if (has_colors()) {
            wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_ASSISTANT));
        }
        left_col += token_display_width;
    }

    (void)col;  // Suppress unused variable warning
    (void)has_spinner;  // Suppress unused variable warning

    wrefresh(tui->wm.status_win);
}

// ============================================================================
// Conversation Viewport and Rendering
// ============================================================================

void refresh_conversation_viewport(TUIState *tui) {
    if (!tui) return;
    window_manager_refresh_conversation(&tui->wm);
}

static int render_text_with_search_highlight(WINDOW *win, const char *text,
                                           int text_pair __attribute__((unused)),
                                           const char *search_pattern, int bg_pair) {
    if (!text || !text[0]) {
        return 0;
    }

    if (!search_pattern || !search_pattern[0]) {
        // No search pattern, render normally with background if provided
        if (bg_pair > 0 && has_colors()) {
            wattron(win, COLOR_PAIR(bg_pair));
        }
        waddstr(win, text);
        if (bg_pair > 0 && has_colors()) {
            wattroff(win, COLOR_PAIR(bg_pair));
        }
        return (int)strlen(text);
    }

    int rendered = 0;
    const char *current = text;
    size_t pattern_len = strlen(search_pattern);

    // Apply background color if provided
    if (bg_pair > 0 && has_colors()) {
        wattron(win, COLOR_PAIR(bg_pair));
    }

    while (*current) {
        // Check if pattern matches at current position (case-insensitive)
        if (strncasecmp(current, search_pattern, pattern_len) == 0) {
            // Render text before the match
            if (current > text) {
                size_t before_len = (size_t)(current - text);
                waddnstr(win, text, (int)before_len);
                rendered += (int)before_len;
            }

            // Render the match with highlight
            if (has_colors()) {
                wattron(win, COLOR_PAIR(NCURSES_PAIR_SEARCH) | A_BOLD);
            }
            waddnstr(win, current, (int)pattern_len);
            if (has_colors()) {
                wattroff(win, COLOR_PAIR(NCURSES_PAIR_SEARCH) | A_BOLD);
            }
            rendered += (int)pattern_len;

            // Move past the match
            current += pattern_len;
            text = current; // Update text pointer for next segment
        } else {
            // Move to next character
            current++;
        }
    }

    // Render any remaining text after last match
    if (*text) {
        waddstr(win, text);
        rendered += (int)strlen(text);
    }

    // Turn off background color if it was applied
    if (bg_pair > 0 && has_colors()) {
        wattroff(win, COLOR_PAIR(bg_pair));
    }

    return rendered;
}

// Helper to render a single visual line segment with border
// Returns bytes consumed from segment
static void render_bordered_segment(TUIState *tui, const char *segment, size_t len,
                                    int border_pair, const char *border_str, bool add_newline) {
    WINDOW *pad = tui->wm.conv_pad;
    int pad_width, pad_height;
    getmaxyx(pad, pad_height, pad_width);
    (void)pad_height;
    (void)border_pair;  // Unused, kept for API compatibility

    // Calculate border display width
    int border_display_width = utf8_display_width(border_str);

    // Render border with assistant color on assistant background
    // This ensures the entire assistant message region has consistent background
    if (has_colors()) {
        wattron(pad, COLOR_PAIR(NCURSES_PAIR_ASSISTANT_BORDER_BG) | A_BOLD);
    }
    waddstr(pad, border_str);
    if (has_colors()) {
        wattroff(pad, COLOR_PAIR(NCURSES_PAIR_ASSISTANT_BORDER_BG) | A_BOLD);
    }

    // Calculate text display width
    int text_display_width = 0;
    if (len > 0) {
        char *tmp = malloc(len + 1);
        if (tmp) {
            memcpy(tmp, segment, len);
            tmp[len] = '\0';
            text_display_width = utf8_display_width(tmp);
            free(tmp);
        } else {
            text_display_width = (int)len;  // Fallback
        }
    }

    // Render text content with background
    if (has_colors()) {
        wattron(pad, COLOR_PAIR(NCURSES_PAIR_ASSISTANT_BG));
    }

    // Render with search highlighting if active
    if (tui->last_search_pattern && tui->last_search_pattern[0] != '\0') {
        char *seg_buf = malloc(len + 1);
        if (seg_buf) {
            memcpy(seg_buf, segment, len);
            seg_buf[len] = '\0';
            render_text_with_search_highlight(pad, seg_buf, 0, tui->last_search_pattern, NCURSES_PAIR_ASSISTANT_BG);
            free(seg_buf);
        } else {
            waddnstr(pad, segment, (int)len);
        }
    } else {
        waddnstr(pad, segment, (int)len);
    }

    // Fill remaining line width with background color
    int used_width = border_display_width + text_display_width;
    int remaining = pad_width - used_width;
    if (remaining > 0) {
        // Fill with spaces to extend background to right edge
        for (int i = 0; i < remaining; i++) {
            waddch(pad, ' ');
        }
    }

    if (has_colors()) {
        wattroff(pad, COLOR_PAIR(NCURSES_PAIR_ASSISTANT_BG));
    }

    if (add_newline) {
        waddch(pad, '\n');
    }
}

// Helper to find byte position that fits within a display width
// Returns number of bytes that fit within max_display_width
static size_t find_wrap_point(const char *text, size_t text_len, int max_display_width) {
    if (max_display_width <= 0) {
        return 1;  // At least one byte to make progress
    }

    // Save current locale
    char *old_locale = setlocale(LC_ALL, NULL);
    if (old_locale) {
        old_locale = strdup(old_locale);
    }
    setlocale(LC_ALL, "C.UTF-8");

    size_t bytes_used = 0;
    int display_width = 0;
    mbstate_t state;
    memset(&state, 0, sizeof(state));

    while (bytes_used < text_len && display_width < max_display_width) {
        wchar_t wc;
        size_t char_bytes = mbrtowc(&wc, text + bytes_used, text_len - bytes_used, &state);

        if (char_bytes == 0) {
            // Null character
            break;
        } else if (char_bytes == (size_t)-1 || char_bytes == (size_t)-2) {
            // Invalid sequence or incomplete - treat as single byte
            bytes_used++;
            display_width++;
        } else {
            int char_width = wcwidth(wc);
            if (char_width < 0) char_width = 1;  // Unknown character

            if (display_width + char_width > max_display_width) {
                // This character would exceed the limit
                break;
            }
            bytes_used += char_bytes;
            display_width += char_width;
        }
    }

    // Restore locale
    if (old_locale) {
        setlocale(LC_ALL, old_locale);
        free(old_locale);
    }

    // Ensure we make progress (at least 1 byte)
    return bytes_used > 0 ? bytes_used : 1;
}

// Helper to render text with a left border for assistant messages
// Handles line wrapping by adding border at start of each new line
// Uses NCURSES_PAIR_ASSISTANT_BG for subtle background highlighting
static void render_text_with_left_border(TUIState *tui, const char *text, int text_pair,
                                         int border_pair, const char *border_str) {
    if (!text || !text[0]) return;

    WINDOW *pad = tui->wm.conv_pad;
    int pad_width;
    int pad_height;
    getmaxyx(pad, pad_height, pad_width);
    (void)pad_height;

    // Calculate border display width (for UTF-8 characters like │)
    int border_display_width = utf8_display_width(border_str);

    // Available width for text content (after border)
    int content_width = pad_width - border_display_width;
    if (content_width < 1) content_width = 1;

    const char *line_start = text;
    const char *p = text;

    while (*p) {
        // Find end of current logical line (newline or end of string)
        while (*p && *p != '\n') {
            p++;
        }

        size_t line_len = (size_t)(p - line_start);

        if (line_len == 0) {
            // Empty line - just render border and newline
            render_bordered_segment(tui, "", 0, border_pair, border_str, (*p == '\n'));
        } else {
            // Check if the line needs wrapping
            int line_display_width = 0;
            {
                char *tmp = malloc(line_len + 1);
                if (tmp) {
                    memcpy(tmp, line_start, line_len);
                    tmp[line_len] = '\0';
                    line_display_width = utf8_display_width(tmp);
                    free(tmp);
                } else {
                    line_display_width = (int)line_len;  // Fallback
                }
            }

            if (line_display_width <= content_width) {
                // Line fits - render normally
                render_bordered_segment(tui, line_start, line_len, border_pair, border_str, (*p == '\n'));
            } else {
                // Line needs wrapping - break into chunks
                const char *chunk_start = line_start;
                size_t remaining = line_len;

                while (remaining > 0) {
                    size_t chunk_bytes = find_wrap_point(chunk_start, remaining, content_width);
                    bool is_last_chunk = (chunk_bytes >= remaining);
                    bool add_nl = is_last_chunk && (*p == '\n');

                    render_bordered_segment(tui, chunk_start, chunk_bytes, border_pair, border_str, true);

                    chunk_start += chunk_bytes;
                    remaining -= chunk_bytes;

                    // Suppress unused - add_nl used for clarity but last chunk newline
                    // is handled by adding newline to all wrapped segments
                    (void)add_nl;
                }
            }
        }

        // Move past newline if present
        if (*p == '\n') {
            p++;
            line_start = p;
        }
    }

    (void)text_pair;  // Suppress unused warning (background pair used instead)
}

int render_entry_to_pad(TUIState *tui, const char *prefix, const char *text, TUIColorPair color_pair) {
    if (!tui || !tui->wm.conv_pad) {
        return -1;
    }

    // Map color pair
    int mapped_pair = NCURSES_PAIR_FOREGROUND;
    switch (color_pair) {
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
            mapped_pair = NCURSES_PAIR_TOOL;
            break;
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

    // Move to end of pad
    int start_line = window_manager_get_content_lines(&tui->wm);
    wmove(tui->wm.conv_pad, start_line, 0);

    // Check if this is a [User] or [Assistant] message to apply new styling
    int is_user_message = (prefix && strcmp(prefix, "[User]") == 0);
    int is_assistant_message = (prefix && strcmp(prefix, "[Assistant]") == 0);

    // For user messages, add padding line before
    if (is_user_message) {
        // Add one blank line for top padding
        waddch(tui->wm.conv_pad, '\n');

        // Render prefix '❯❯❯' with bold user color (3 carets for visibility)
        // User message prefix removed for cleaner look
    } else if (is_assistant_message) {
        // Assistant message: check response style
        if (tui->response_style == RESPONSE_STYLE_BORDER) {
            // Border style: use left border decoration (│ ) on each line
            int text_pair = NCURSES_PAIR_FOREGROUND;
            render_text_with_left_border(tui, text, text_pair, mapped_pair, "│ ");

            // Add final newline after bordered content
            waddch(tui->wm.conv_pad, '\n');
            goto skip_newline;
        } else {
            // Caret style: leading '>>> ' prefix with no border
            if (has_colors()) {
                wattron(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
            }
            waddstr(tui->wm.conv_pad, ">>> ");
            if (has_colors()) {
                wattroff(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
            }
            // Fall through to write text normally
        }
    } else {
        // Write prefix for other (non-user, non-assistant) messages
        if (prefix && prefix[0] != '\0') {
            if (has_colors()) {
                wattron(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
            }
            waddstr(tui->wm.conv_pad, prefix);
            waddch(tui->wm.conv_pad, ' ');
            if (has_colors()) {
                wattroff(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
            }
        }
    }

    // Write text (for user messages, caret-style assistant, and other messages)
    if (text && text[0] != '\0') {
        int text_pair;
        if (is_user_message) {
            // User message: use foreground color (no background)
            text_pair = NCURSES_PAIR_FOREGROUND;
        } else if (is_assistant_message && tui->response_style == RESPONSE_STYLE_CARET) {
            // Caret-style assistant: use foreground color
            text_pair = NCURSES_PAIR_FOREGROUND;
        } else if (prefix && prefix[0] != '\0') {
            // Other messages with prefix use foreground
            text_pair = NCURSES_PAIR_FOREGROUND;
        } else {
            // No prefix: use the mapped pair
            text_pair = mapped_pair;
        }

        if (has_colors()) {
            wattron(tui->wm.conv_pad, COLOR_PAIR(text_pair));
        }

        // Check if we have an active search pattern to highlight
        if (tui->last_search_pattern && tui->last_search_pattern[0] != '\0') {
            render_text_with_search_highlight(tui->wm.conv_pad, text, text_pair, tui->last_search_pattern, 0);
        } else {
            waddstr(tui->wm.conv_pad, text);
        }

        if (has_colors()) {
            wattroff(tui->wm.conv_pad, COLOR_PAIR(text_pair));
        }

        // For user messages, add padding line after
        if (is_user_message) {
            // Add one blank line for bottom padding
            waddch(tui->wm.conv_pad, '\n');

            // Exit early to avoid duplicate newline below
            goto skip_newline;
        }
    }

    // Add newline (for messages that didn't use goto skip_newline)
    waddch(tui->wm.conv_pad, '\n');

skip_newline:
    ; // Empty statement required after label

    // Update total lines (get actual cursor position after wrapping)
    int cur_y, cur_x;
    getyx(tui->wm.conv_pad, cur_y, cur_x);
    (void)cur_x;

    // Safety check: ensure cursor is within pad bounds
    int current_pad_height, current_pad_width;
    getmaxyx(tui->wm.conv_pad, current_pad_height, current_pad_width);
    if (cur_y >= current_pad_height) {
        LOG_ERROR("[TUI] Cursor position %d exceeds pad height %d! Expanding pad.", cur_y, current_pad_height);
        // Emergency expansion with overflow check
        int emergency_capacity = cur_y + 100;
        if (emergency_capacity < cur_y) {  // Check for integer overflow
            LOG_ERROR("[TUI] Emergency expansion would overflow! Limiting cursor.");
            cur_y = current_pad_height - 1;
        } else if (window_manager_ensure_pad_capacity(&tui->wm, emergency_capacity) != 0) {
            LOG_ERROR("[TUI] Failed to expand pad in emergency!");
            // Try to recover by limiting to current capacity
            cur_y = current_pad_height - 1;
        }
    }

    window_manager_set_content_lines(&tui->wm, cur_y);

    return 0;
}

void redraw_conversation(TUIState *tui) {
    if (!tui || !tui->is_initialized || !tui->wm.conv_pad) {
        return;
    }

    // Save current scroll position
    int saved_scroll_offset = tui->wm.conv_scroll_offset;

    // Clear the pad
    werase(tui->wm.conv_pad);
    window_manager_set_content_lines(&tui->wm, 0);

    // Re-render all entries
    for (int i = 0; i < tui->entries_count; i++) {
        ConversationEntry *entry = &tui->entries[i];
        render_entry_to_pad(tui, entry->prefix, entry->text, entry->color_pair);
    }

    // Restore scroll position
    tui->wm.conv_scroll_offset = saved_scroll_offset;

    // Refresh the conversation viewport
    window_manager_refresh_conversation(&tui->wm);
}

// ============================================================================
// Input Rendering
// ============================================================================

#define INPUT_LEFT_BORDER_WIDTH 1
#define INPUT_LEFT_PADDING 1
#define INPUT_RIGHT_PADDING 1
#define INPUT_CONTENT_START (INPUT_LEFT_BORDER_WIDTH + INPUT_LEFT_PADDING)

void input_redraw(TUIState *tui, const char *prompt) {
    if (!tui || !tui->input_buffer) {
        return;
    }

    TUIInputBuffer *input = tui->input_buffer;
    WINDOW *win = input->win;
    if (!win) {
        return;
    }

    // Hide input window in NORMAL mode
    if (tui->mode == TUI_MODE_NORMAL) {
        werase(win);
        wrefresh(win);
        return;
    }

    // For command/search mode, we show the prefix (:/? + buffer)
    // For insert mode, no prompt prefix
    int mode_prefix_len = 0;
    const char *mode_prefix = "";
    char search_prompt[260] = {0};

    if (tui->mode == TUI_MODE_COMMAND && tui->command_buffer) {
        mode_prefix = tui->command_buffer;
        mode_prefix_len = (int)strlen(mode_prefix);
    } else if (tui->mode == TUI_MODE_SEARCH && tui->search_buffer) {
        if (tui->search_direction == 1) {
            snprintf(search_prompt, sizeof(search_prompt), "/%s", tui->search_buffer);
        } else {
            snprintf(search_prompt, sizeof(search_prompt), "?%s", tui->search_buffer);
        }
        mode_prefix = search_prompt;
        mode_prefix_len = (int)strlen(mode_prefix);
    }

    // Calculate available width for text content
    // Layout depends on style:
    // - BACKGROUND: border (1) + left padding (1) + content + right padding (1)
    // - BORDER: box border (1) + left padding (1) + content + right padding (1) + box border (1)
    // - BLAND: caret '❯ ' (2 display cols) + content (no padding, no borders)
    int content_start_col;
    int right_margin;

    if (tui->input_box_style == INPUT_STYLE_BACKGROUND) {
        content_start_col = INPUT_CONTENT_START;  // border (1) + padding (1) = 2
        right_margin = INPUT_RIGHT_PADDING;       // padding (1) = 1
    } else if (tui->input_box_style == INPUT_STYLE_BORDER) {
        // BORDER style: box border on left + padding
        content_start_col = INPUT_LEFT_BORDER_WIDTH + INPUT_LEFT_PADDING;  // 1 + 1 = 2
        right_margin = INPUT_RIGHT_PADDING + INPUT_LEFT_BORDER_WIDTH;      // padding + right border = 2
    } else if (tui->input_box_style == INPUT_STYLE_HORIZONTAL) {
        // HORIZONTAL style: only top and bottom borders, caret '❯ ' but no left/right border
        // In COMMAND/SEARCH mode, the mode prefix starts at column 0 (no caret)
        // In INSERT mode, the caret '❯ ' starts at column 0
        if (tui->mode == TUI_MODE_COMMAND || tui->mode == TUI_MODE_SEARCH) {
            content_start_col = 0;  // Mode prefix (: or /) starts at beginning
        } else {
            content_start_col = 2;  // '❯ ' = 2 display columns in INSERT mode
        }
        right_margin = INPUT_RIGHT_PADDING;      // just padding (1)
    } else {
        // BLAND style: just '❯ ' prefix (2 display cols), no padding
        // In COMMAND/SEARCH mode, the mode prefix starts at column 0 (no caret)
        // In INSERT mode, the caret '❯ ' starts at column 0
        if (tui->mode == TUI_MODE_COMMAND || tui->mode == TUI_MODE_SEARCH) {
            content_start_col = 0;  // Mode prefix (: or /) starts at beginning
        } else {
            content_start_col = 2;  // '❯ ' = 2 display columns in INSERT mode
        }
        right_margin = 0;       // no right padding
    }

    int content_width = input->win_width - content_start_col - right_margin;
    if (content_width < 10) content_width = 10;

    // For command/search mode, calculate needed lines with mode prefix
    // For insert mode, no prefix
    int effective_prefix_len = (tui->mode == TUI_MODE_INSERT) ? 0 : mode_prefix_len;
    int needed_lines = tui_window_calculate_needed_lines(input->buffer, input->length,
                                              content_width, effective_prefix_len);

    // Request window resize (this will be a no-op if size hasn't changed)
    // For BORDER style, we need extra height for top and bottom borders
    // For HORIZONTAL style, we need extra height for caret row + top and bottom borders
    // For BACKGROUND style, we add one line of top padding and one line of bottom padding
    // For BLAND style, no extra height needed
    int window_height_needed = needed_lines;
    if (tui->input_box_style == INPUT_STYLE_BORDER) {
        window_height_needed += 2;  // +2 for top and bottom borders
    } else if (tui->input_box_style == INPUT_STYLE_HORIZONTAL) {
        window_height_needed += 2;  // +2 for top and bottom borders
    } else if (tui->input_box_style == INPUT_STYLE_BACKGROUND) {
        window_height_needed += 2;  // +2 for top and bottom padding
    }
    // BLAND style: no extra height
    tui_window_resize_input(tui, window_height_needed);
    input = tui->input_buffer;
    win = input->win;
    if (!win) {
        return;
    }

    // Recalculate content width after potential resize
    if (tui->input_box_style == INPUT_STYLE_BACKGROUND) {
        content_start_col = INPUT_CONTENT_START;
        right_margin = INPUT_RIGHT_PADDING;
    } else if (tui->input_box_style == INPUT_STYLE_BORDER) {
        content_start_col = INPUT_LEFT_BORDER_WIDTH + INPUT_LEFT_PADDING;
        right_margin = INPUT_RIGHT_PADDING + INPUT_LEFT_BORDER_WIDTH;
    } else if (tui->input_box_style == INPUT_STYLE_HORIZONTAL) {
        // In INSERT mode, '❯ ' = 2 display columns (same as BLAND style)
        // In COMMAND/SEARCH mode, mode prefix starts at column 0
        if (tui->mode == TUI_MODE_COMMAND || tui->mode == TUI_MODE_SEARCH) {
            content_start_col = 0;
        } else {
            content_start_col = 2;
        }
        right_margin = INPUT_RIGHT_PADDING;
    } else {
        // BLAND style: In COMMAND/SEARCH mode, mode prefix starts at column 0
        // In INSERT mode, '❯ ' = 2 display columns
        if (tui->mode == TUI_MODE_COMMAND || tui->mode == TUI_MODE_SEARCH) {
            content_start_col = 0;
        } else {
            content_start_col = 2;
        }
        right_margin = 0;
    }
    content_width = input->win_width - content_start_col - right_margin;
    if (content_width < 10) content_width = 10;

    // Calculate cursor line position
    int cursor_line = 0;
    int cursor_col = effective_prefix_len;
    for (int i = 0; i < input->cursor; i++) {
        if (input->buffer[i] == '\n') {
            cursor_line++;
            cursor_col = 0;
        } else {
            cursor_col++;
            if (cursor_col >= content_width) {
                cursor_line++;
                cursor_col = 0;
            }
        }
    }

    // Adjust vertical scroll to keep cursor visible
    // For BORDER/HORIZONTAL style, we need to account for top and bottom borders
    // For HORIZONTAL style: row 0 = top border, row 1+ = content, last row = bottom border
    // For BACKGROUND style, we account for top and bottom padding
    // For BLAND style, no offset needed
    int content_start_row = (tui->input_box_style == INPUT_STYLE_BORDER) ? 1 :
                            (tui->input_box_style == INPUT_STYLE_HORIZONTAL) ? 1 :
                            (tui->input_box_style == INPUT_STYLE_BACKGROUND) ? 1 : 0;
    int border_height_offset = (tui->input_box_style == INPUT_STYLE_BORDER) ? 2 :
                               (tui->input_box_style == INPUT_STYLE_HORIZONTAL) ? 2 :
                               (tui->input_box_style == INPUT_STYLE_BACKGROUND) ? 2 : 0;
    int max_visible_lines = input->win_height - border_height_offset;
    if (cursor_line < input->line_scroll_offset) {
        input->line_scroll_offset = cursor_line;
    } else if (cursor_line >= input->line_scroll_offset + max_visible_lines) {
        input->line_scroll_offset = cursor_line - max_visible_lines + 1;
    }

    // Clear the window
    werase(win);

    // Apply style based on input_box_style
    if (tui->input_box_style == INPUT_STYLE_BACKGROUND) {
        // Style 1: Background color + left border
        // Fill background with input background color
        if (has_colors()) {
            wbkgd(win, COLOR_PAIR(NCURSES_PAIR_INPUT_BG));
        }

        // Draw left border (thin vertical line)
        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_INPUT_BORDER));
        }
        for (int row = 0; row < input->win_height; row++) {
            mvwaddch(win, row, 0, ACS_VLINE);
        }
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_INPUT_BORDER));
        }
    } else if (tui->input_box_style == INPUT_STYLE_BORDER) {
        // Style 2: Full border with no background
        // Reset to default background (removes any previously set background color)
        if (has_colors()) {
            wbkgd(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }

        // Draw box border with rounded corners around the input area
        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_INPUT_BORDER));
        }
        // Draw rounded corners using Unicode box-drawing characters
        int max_y, max_x;
        getmaxyx(win, max_y, max_x);
        // Top-left corner
        mvwprintw(win, 0, 0, "╭");
        // Top-right corner
        mvwprintw(win, 0, max_x - 1, "╮");
        // Bottom-left corner
        mvwprintw(win, max_y - 1, 0, "╰");
        // Bottom-right corner
        mvwprintw(win, max_y - 1, max_x - 1, "╯");
        // Top and bottom horizontal lines
        for (int col = 1; col < max_x - 1; col++) {
            mvwprintw(win, 0, col, "─");
            mvwprintw(win, max_y - 1, col, "─");
        }
        // Left and right vertical lines
        for (int row = 1; row < max_y - 1; row++) {
            mvwprintw(win, row, 0, "│");
            mvwprintw(win, row, max_x - 1, "│");
        }
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_INPUT_BORDER));
        }
    } else if (tui->input_box_style == INPUT_STYLE_HORIZONTAL) {
        // Style 3: Horizontal borders only (top and bottom, no left/right borders)
        // Layout: row 0 = top border, row 1+ = content with caret, last row = bottom border
        // Reset to default background
        if (has_colors()) {
            wbkgd(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }

        // Draw top and bottom horizontal borders
        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_INPUT_BORDER));
        }
        // Top border at row 0
        for (int col = 0; col < input->win_width; col++) {
            mvwaddch(win, 0, col, ACS_HLINE);
        }
        // Bottom border at last row
        for (int col = 0; col < input->win_width; col++) {
            mvwaddch(win, input->win_height - 1, col, ACS_HLINE);
        }
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_INPUT_BORDER));
        }

        // Draw the '❯ ' caret in prompt color at content area (only in INSERT mode)
        // In COMMAND/SEARCH mode, the mode prefix (: or /) will be displayed instead
        if (tui->mode == TUI_MODE_INSERT) {
            if (has_colors()) {
                wattron(win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
            }
            mvwprintw(win, 1, 0, "❯ ");
            if (has_colors()) {
                wattroff(win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
            }
        }
    } else {
        // Style 4: BLAND - just caret '❯' on general background, no borders
        // Reset to default background
        if (has_colors()) {
            wbkgd(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }

        // Draw the '❯ ' caret in prompt color (only in INSERT mode)
        // In COMMAND/SEARCH mode, the mode prefix (: or /) will be displayed instead
        if (tui->mode == TUI_MODE_INSERT) {
            if (has_colors()) {
                wattron(win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
            }
            mvwprintw(win, 0, 0, "❯ ");
            if (has_colors()) {
                wattroff(win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
            }
        }
    }

    // Draw mode prefix on first visible line (command/search mode only)
    if (mode_prefix_len > 0 && input->line_scroll_offset == 0) {
        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
        }
        mvwprintw(win, content_start_row, content_start_col, "%s", mode_prefix);
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
        }
    }

    // Render visible lines with scrolling support
    // Only use INPUT_BG color for BACKGROUND style (it includes a background color)
    if (has_colors() && tui->input_box_style == INPUT_STYLE_BACKGROUND) {
        wattron(win, COLOR_PAIR(NCURSES_PAIR_INPUT_BG));
    }

    int current_line = 0;
    int screen_y = content_start_row;
    int screen_x = content_start_col + effective_prefix_len;

    // Calculate bottom boundary (accounts for border in BORDER/HORIZONTAL style)
    int bottom_boundary = (tui->input_box_style == INPUT_STYLE_BORDER ||
                           tui->input_box_style == INPUT_STYLE_HORIZONTAL) ?
                          (input->win_height - 1) : input->win_height;

    for (int i = 0; i < input->length && screen_y < bottom_boundary; i++) {
        // Skip lines before scroll offset
        if (current_line < input->line_scroll_offset) {
            if (input->buffer[i] == '\n') {
                current_line++;
                screen_x = content_start_col;
            } else {
                screen_x++;
                if (screen_x >= content_start_col + content_width) {
                    current_line++;
                    screen_x = content_start_col;
                }
            }
            continue;
        }

        // Render character
        char c = input->buffer[i];
        if (c == '\n') {
            screen_y++;
            current_line++;
            screen_x = content_start_col;
        } else {
            mvwaddch(win, screen_y, screen_x, (chtype)(unsigned char)c);
            screen_x++;

            // Check if we need to wrap
            if (screen_x >= content_start_col + content_width) {
                screen_y++;
                current_line++;
                screen_x = content_start_col;
            }
        }
    }

    if (has_colors() && tui->input_box_style == INPUT_STYLE_BACKGROUND) {
        wattroff(win, COLOR_PAIR(NCURSES_PAIR_INPUT_BG));
    }

    // Recalculate cursor screen position
    int temp_line = 0;
    int temp_col = effective_prefix_len;
    for (int i = 0; i < input->cursor; i++) {
        if (input->buffer[i] == '\n') {
            temp_line++;
            temp_col = 0;
        } else {
            temp_col++;
            if (temp_col >= content_width) {
                temp_line++;
                temp_col = 0;
            }
        }
    }

    int cursor_screen_y = temp_line - input->line_scroll_offset + content_start_row;
    int cursor_screen_x = content_start_col + temp_col;

    // Bounds check for cursor position
    if (cursor_screen_y >= content_start_row &&
        cursor_screen_y < bottom_boundary &&
        cursor_screen_x >= 0 && cursor_screen_x < input->win_width) {
        wmove(win, cursor_screen_y, cursor_screen_x);
    }

    // Hide cursor in NORMAL mode, show it in INSERT/COMMAND modes
    if (tui->mode == TUI_MODE_NORMAL) {
        curs_set(0);  // Hide cursor
    } else {
        curs_set(2);  // Show block cursor
    }

    // Draw vertical scroll bar on the right edge when input has scrolled
    // Only show when there's content above or below the visible area
    if (tui->mode == TUI_MODE_INSERT || tui->mode == TUI_MODE_COMMAND ||
        tui->mode == TUI_MODE_SEARCH) {
        int total_lines = needed_lines;
        int visible_lines = max_visible_lines;  // Use calculated visible lines (accounts for borders)
        int indicator_col = input->win_width - 1;

        // Show only when there is more content than fits on screen
        if (total_lines > visible_lines && visible_lines > 0) {
            int track_height = visible_lines;
            int thumb_height = (visible_lines * visible_lines) / total_lines;
            if (thumb_height < 1) {
                thumb_height = 1;
            }

            int max_thumb_top = track_height - thumb_height;
            int scroll_range = total_lines - visible_lines;
            int thumb_top = 0;
            if (scroll_range > 0 && max_thumb_top > 0) {
                long long num = (long long)input->line_scroll_offset * (long long)max_thumb_top;
                thumb_top = (int)(num / scroll_range);
            }

            if (thumb_top < 0) {
                thumb_top = 0;
            }
            if (thumb_top > max_thumb_top) {
                thumb_top = max_thumb_top;
            }

            // Set full color for scroll bar (no transparency/dimming)
            if (has_colors()) {
                wattron(win, COLOR_PAIR(NCURSES_PAIR_PROMPT));
            }

            // Draw track (offset by content_start_row for border style)
            for (int row = 0; row < track_height; row++) {
                mvwaddch(win, row + content_start_row, indicator_col, ACS_VLINE);
            }

            // Draw thumb (offset by content_start_row for border style)
            for (int row = thumb_top; row < thumb_top + thumb_height; row++) {
                mvwaddch(win, row + content_start_row, indicator_col, ACS_CKBOARD);
            }

            if (has_colors()) {
                wattroff(win, COLOR_PAIR(NCURSES_PAIR_PROMPT));
            }

            // Restore cursor position after drawing indicators
            if (cursor_screen_y >= content_start_row &&
                cursor_screen_y < bottom_boundary &&
                cursor_screen_x >= 0 && cursor_screen_x < input->win_width) {
                wmove(win, cursor_screen_y, cursor_screen_x);
            }
        }
    }

    wrefresh(win);

    // Suppress unused parameter warning - prompt kept for API compatibility
    (void)prompt;
}

// ============================================================================
// Status Management
// ============================================================================

void tui_update_status(TUIState *tui, const char *status_text) {
    if (!tui || !tui->is_initialized) return;

    const char *message = status_text ? status_text : "";
    LOG_FINE("[TUI] Status update requested: '%s'", message[0] ? message : "(clear)");

    if (message[0] == '\0') {
        status_spinner_stop(tui);
        tui->status_visible = 0;
        free(tui->status_message);
        tui->status_message = NULL;
        if (tui->wm.status_height > 0) {
            render_status_window(tui);
        }
        return;
    }

    if (!tui->status_message || strcmp(tui->status_message, message) != 0) {
        char *copy = strdup(message);
        if (!copy) {
            LOG_ERROR("[TUI] Failed to allocate memory for status message");
            return;
        }
        free(tui->status_message);
        tui->status_message = copy;
    }

    if (status_message_wants_spinner(message)) {
        status_spinner_start(tui);
    } else {
        status_spinner_stop(tui);
    }

    tui->status_visible = 1;

    if (tui->wm.status_height > 0) {
        render_status_window(tui);
    }
}

void tui_refresh(TUIState *tui) {
    if (!tui || !tui->is_initialized) return;
    window_manager_refresh_all(&tui->wm);
}
