/*
 * TUI (Terminal User Interface) - ncurses-based implementation
 *
 * This module provides an ncurses-based input bar with full readline-like
 * keyboard shortcuts while preserving scrollback for conversation output.
 */

// Define feature test macros before any includes
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "tui.h"
#include "tui_core.h"
#include "tui_input.h"
#include "tui_conversation.h"
#include "tui_window.h"
#include "tui_search.h"
#include "tui_completion.h"
#include "tui_history.h"
#include "tui_modes.h"
#define COLORSCHEME_EXTERN
#include "colorscheme.h"
#include "history_search.h"
#include "fallback_colors.h"
#include "logger.h"
#include "indicators.h"
#include "klawed_internal.h"
#include <stdlib.h>
#include <bsd/stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>
#include <strings.h>
#include <limits.h>
#include <stdio.h>
#include <bsd/string.h>
#include "message_queue.h"
#include "history_file.h"
#include "array_resize.h"
#include "subagent_manager.h"
#include "commands.h"

#define INITIAL_CONV_CAPACITY 1000
#define INPUT_BUFFER_SIZE 8192
#define INPUT_WIN_MIN_HEIGHT 2  // Min height for input window (content lines, no borders)
#define INPUT_WIN_MAX_HEIGHT_PERCENT 20  // Max height as percentage of viewport
#define CONV_WIN_PADDING 0      // No padding between conv window and input window
#define STATUS_WIN_HEIGHT 1     // Single-line status window
#define TUI_MAX_MESSAGES_PER_FRAME 10  // Max messages processed per frame

// Paste heuristic control: default OFF (use bracketed paste only)
// Enable by setting env var TUI_PASTE_HEURISTIC=1
// Default: enable heuristic (helps when bracketed paste isn't passed through, e.g. tmux)
static int g_enable_paste_heuristic = 1;

// Function prototypes
// Less sensitive defaults: require very fast, large bursts to classify as paste
static int g_paste_gap_ms = 12;         // max gap to count as "rapid" for burst
static int g_paste_burst_min = 60;      // min consecutive keys within gap to enter paste mode
static int g_paste_timeout_ms = 400;    // idle time to finalize paste

// Ncurses color pair definitions are now in tui.h for sharing across TUI components

// Validate TUI window state (debug builds)
// Uses ncurses is_pad() function to check window types

// Render the status window based on current state
static const spinner_variant_t* status_spinner_variant(void) {
    init_global_spinner_variant();
    if (GLOBAL_SPINNER_VARIANT.frames && GLOBAL_SPINNER_VARIANT.count > 0) {
        return &GLOBAL_SPINNER_VARIANT;
    }
    static const spinner_variant_t fallback_variant = { SPINNER_FRAMES, SPINNER_FRAME_COUNT };
    return &fallback_variant;
}

static uint64_t status_spinner_interval_ns(void) {
    // Cast both operands to uint64_t to avoid intermediate ULL promotion
    // that triggers -Wsign-conversion on some platforms (LP64 vs LLP64).
    return (uint64_t)SPINNER_DELAY_MS * (uint64_t)1000000;
}

void render_status_window(TUIState *tui) {
    if (!tui || !tui->wm.status_win) {
        return;
    }


    int height, width;
    getmaxyx(tui->wm.status_win, height, width);
    (void)height;

    werase(tui->wm.status_win);

    int col = 0;

    // MODE INDICATOR - Commented out (hiding input box in normal mode instead)
    // If we want to restore mode indicator, uncomment this section:
    /*
    const char *mode_str;
    int mode_color;
    switch (tui->mode) {
        case TUI_MODE_NORMAL:
            mode_str = "-- NORMAL --";
            mode_color = NCURSES_PAIR_ASSISTANT;
            break;
        case TUI_MODE_INSERT:
            mode_str = "-- INSERT --";
            mode_color = NCURSES_PAIR_PROMPT;
            break;
        case TUI_MODE_COMMAND:
            mode_str = "-- COMMAND --";
            mode_color = NCURSES_PAIR_STATUS;
            break;
        default:
            mode_str = "-- UNKNOWN --";
            mode_color = NCURSES_PAIR_ERROR;
            break;
    }
    int mode_len = (int)strlen(mode_str);
    int mode_col = width - mode_len - 1;
    if (mode_col < 0) mode_col = 0;
    */

    // Prepare status message string (agent status - rightmost)
    char status_str[256] = {0};
    int status_str_len = 0;
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
            snprintf(status_str, sizeof(status_str), "%s %s ", frame, tui->status_message);
            has_spinner = 1;
        } else {
            snprintf(status_str, sizeof(status_str), "%s ", tui->status_message);
        }
        status_str_len = (int)strlen(status_str);
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
            LOG_DEBUG("[TUI] render_status_window: plan_mode=%d, width=%d", plan_mode, width);
        } else {
            LOG_WARN("[TUI] Failed to lock conversation state for plan_mode read");
        }
    } else {
        LOG_WARN("[TUI] No conversation state for plan_mode read");
    }

    if (plan_mode) {
        snprintf(plan_str, sizeof(plan_str), " ● Plan ");
        plan_str_len = (int)strlen(plan_str);
        LOG_DEBUG("[TUI] Plan mode indicator: '%s' (len=%d)", plan_str, plan_str_len);
    }

    // Prepare scroll percentage in NORMAL mode
    char scroll_str[32] = {0};
    int scroll_str_len = 0;
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
    }

    // Prepare token usage (only in NORMAL mode)
    char token_str[128] = {0};
    int token_str_len = 0;
    if (tui->mode == TUI_MODE_NORMAL) {
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
                LOG_DEBUG("[TUI] Retrieved session token totals from DB: prompt=%d completion=%d cached=%d",
                          prompt_tokens, completion_tokens, cached_tokens);
            } else {
                LOG_DEBUG("[TUI] Failed to retrieve session token totals from DB");
            }
        } else {
            LOG_DEBUG("[TUI] No persistence database connection available");
        }

        int total_tokens = prompt_tokens + completion_tokens;

        // Show total tokens and cached tokens in the format: "Token: X (+Y cached) "
        if (cached_tokens > 0) {
            snprintf(token_str, sizeof(token_str), "Token: %d (+%d cached) ",
                     total_tokens, cached_tokens);
        } else {
            snprintf(token_str, sizeof(token_str), "Token: %d ", total_tokens);
        }
        token_str_len = (int)strlen(token_str);
        LOG_DEBUG("[TUI] Rendering token display: %s (mode=NORMAL)", token_str);

        // Debug: warn if token counts are 0 in Normal mode (might indicate a bug)
        if (total_tokens == 0) {
            LOG_DEBUG("[TUI] Warning: Token count is 0 in NORMAL mode");
        }
    }

    // Layout (from right to left):
    // [status_message] <- rightmost
    // [plan_mode] [scroll%] [token] [status_message] <- in NORMAL mode
    // [plan_mode] [status_message] <- in INSERT/COMMAND mode

    // Render status message on the right (rightmost position)
    if (status_str_len > 0 && status_str_len < width) {
        int status_col = width - status_str_len;
        if (status_col < 0) status_col = 0;

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

    // Render token usage (in NORMAL mode, to the left of status)
    if (token_str_len > 0 && token_str_len < width) {
        int token_col = width - status_str_len - token_str_len;
        if (token_col < 0) token_col = 0;

        if (has_colors()) {
            wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_ASSISTANT));
        }
        mvwaddnstr(tui->wm.status_win, 0, token_col, token_str, token_str_len);
        if (has_colors()) {
            wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_ASSISTANT));
        }
    }

    // Render scroll percentage (in NORMAL mode, to the left of token)
    if (scroll_str_len > 0 && scroll_str_len < width) {
        int scroll_col = width - status_str_len - token_str_len - scroll_str_len;
        if (scroll_col < 0) scroll_col = 0;

        if (has_colors()) {
            wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }
        mvwaddnstr(tui->wm.status_win, 0, scroll_col, scroll_str, scroll_str_len);
        if (has_colors()) {
            wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }
    }

    // Render plan mode indicator (to the left of scroll/token or status, depending on mode)
    if (plan_str_len > 0 && plan_str_len < width) {
        int plan_col;
        if (tui->mode == TUI_MODE_NORMAL) {
            // In NORMAL mode: place to the left of scroll percentage
            plan_col = width - status_str_len - token_str_len - scroll_str_len - plan_str_len;
        } else {
            // In INSERT/COMMAND mode: place to the left of status message
            plan_col = width - status_str_len - plan_str_len;
        }
        if (plan_col < 0) plan_col = 0;

        LOG_DEBUG("[TUI] Rendering plan mode at col=%d, width=%d, plan_str_len=%d, mode=%d",
                  plan_col, width, plan_str_len, tui->mode);

        if (has_colors()) {
            wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
        } else {
            wattron(tui->wm.status_win, A_BOLD);
        }
        mvwaddnstr(tui->wm.status_win, 0, plan_col, plan_str, plan_str_len);
        if (has_colors()) {
            wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
        } else {
            wattroff(tui->wm.status_win, A_BOLD);
        }
    } else if (plan_str_len > 0) {
        LOG_DEBUG("[TUI] Plan mode indicator not rendered: plan_str_len=%d, width=%d, condition=%d",
                  plan_str_len, width, (plan_str_len > 0 && plan_str_len < width));
    }

    (void)col;  // Suppress unused variable warning
    (void)has_spinner;  // Suppress unused variable warning

    // MODE INDICATOR RENDERING - Commented out (hiding input box in normal mode instead)
    /*
    if (mode_col < width) {
        if (has_colors()) {
            wattron(tui->status_win, COLOR_PAIR(mode_color) | A_BOLD);
        } else {
            wattron(tui->status_win, A_BOLD);
        }
        mvwaddnstr(tui->status_win, 0, mode_col, mode_str, width - mode_col);
        if (has_colors()) {
            wattroff(tui->status_win, COLOR_PAIR(mode_color) | A_BOLD);
        } else {
            wattroff(tui->status_win, A_BOLD);
        }
    }
    */

    wrefresh(tui->wm.status_win);
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

static void status_spinner_tick(TUIState *tui) {
    if (!tui || !tui->status_spinner_active || !tui->status_visible) {
        return;
    }
    if (tui->wm.status_height <= 0 || !tui->wm.status_win) {
        return;
    }

    uint64_t now = monotonic_time_ns();
    if (tui->status_spinner_last_update_ns == 0) {
        tui->status_spinner_last_update_ns = now;
        return;
    }

    uint64_t delta = now - tui->status_spinner_last_update_ns;
    uint64_t interval_ns = status_spinner_interval_ns();
    if (delta < interval_ns) {
        return;
    }

    uint64_t steps = interval_ns ? delta / interval_ns : 1;
    if (steps == 0) {
        steps = 1;
    }

    const spinner_variant_t *variant = status_spinner_variant();
    int frame_count = (variant->count > 0) ? variant->count : SPINNER_FRAME_COUNT;
    if (frame_count <= 0) {
        return;
    }
    tui->status_spinner_frame = (tui->status_spinner_frame + (int)steps) % frame_count;
    tui->status_spinner_last_update_ns = now;
    render_status_window(tui);
}

// Initialize ncurses color pairs from our colorscheme
// Clear the resize flag (called after handling resize)
/*
static void tui_clear_resize_flag(void) {
    g_resize_flag = 0;
}
*/

// Expand pad capacity if needed
// Pad capacity growth is centralized in WindowManager now.

// Helper: Refresh conversation window viewport (using pad)
void refresh_conversation_viewport(TUIState *tui) {
    if (!tui) return;
    window_manager_refresh_conversation(&tui->wm);
}

// Helper: Render text with search highlighting
// Returns number of characters rendered
static int render_text_with_search_highlight(WINDOW *win, const char *text,
                                           int text_pair __attribute__((unused)),
                                           const char *search_pattern) {
    if (!text || !text[0]) {
        return 0;
    }

    if (!search_pattern || !search_pattern[0]) {
        // No search pattern, render normally
        waddstr(win, text);
        return (int)strlen(text);
    }

    int rendered = 0;
    const char *current = text;
    size_t pattern_len = strlen(search_pattern);

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

    return rendered;
}

// Helper: Render a single conversation entry to the pad
// Returns 0 on success, -1 on error
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

    // For user messages, add padding line before and draw full-width background
    if (is_user_message) {
        // Add one blank line for top padding
        waddch(tui->wm.conv_pad, '\n');
        
        // Get updated position after adding blank line
        int cur_y, cur_x;
        int pad_height, pad_width;
        getyx(tui->wm.conv_pad, cur_y, cur_x);
        getmaxyx(tui->wm.conv_pad, pad_height, pad_width);
        (void)pad_height;
        (void)cur_x;

        // Draw full-width background bar
        if (has_colors()) {
            wattron(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_USER_MSG_BG));
        }
        for (int i = 0; i < pad_width; i++) {
            waddch(tui->wm.conv_pad, ' ');
        }
        // Keep color active for text rendering

        // Move back to start of line to draw content on top
        wmove(tui->wm.conv_pad, cur_y, 0);

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
        if (prefix && prefix[0] != '\0') {
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
                waddstr(tui->wm.conv_pad, prefix);
                waddch(tui->wm.conv_pad, ' ');
                if (has_colors()) {
                    wattroff(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
                }
            }
        }
    }

    // Write text
    if (text && text[0] != '\0') {
        int text_pair;
        if (is_user_message) {
            // User message: text is already on background, just render it
            text_pair = NCURSES_PAIR_USER_MSG_BG;  // Keep background active
        } else if (prefix && prefix[0] != '\0') {
            // Other messages with prefix use foreground
            text_pair = NCURSES_PAIR_FOREGROUND;
        } else {
            // No prefix: use the mapped pair
            text_pair = mapped_pair;
        }

        if (!is_user_message && has_colors()) {
            wattron(tui->wm.conv_pad, COLOR_PAIR(text_pair));
        }

        // Check if we have an active search pattern to highlight
        if (tui->last_search_pattern && tui->last_search_pattern[0] != '\0') {
            render_text_with_search_highlight(tui->wm.conv_pad, text, text_pair, tui->last_search_pattern);
        } else {
            waddstr(tui->wm.conv_pad, text);
        }

        if (has_colors()) {
            wattroff(tui->wm.conv_pad, COLOR_PAIR(text_pair));
        }

        // For user messages, move to end of line and add padding line after
        if (is_user_message) {
            int msg_y, msg_x;
            int pad_height, pad_width;
            getyx(tui->wm.conv_pad, msg_y, msg_x);
            getmaxyx(tui->wm.conv_pad, pad_height, pad_width);
            (void)pad_height;
            (void)msg_x;
            
            // Move to end of line
            wmove(tui->wm.conv_pad, msg_y, pad_width - 1);
            
            // Add newline to finish the background line
            waddch(tui->wm.conv_pad, '\n');
            
            // Add one blank line for bottom padding
            waddch(tui->wm.conv_pad, '\n');
            
            // Exit early to avoid duplicate newline below
            goto skip_newline;
        }
    }

    // Add newline for non-user messages
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

// UTF-8 helper functions (from lineedit.c)
// Calculate how many visual lines are needed for the current buffer
// Note: This assumes first line includes the prompt
// Threshold for when to use placeholder vs direct insertion (characters)
#define PASTE_PLACEHOLDER_THRESHOLD 200

// Insert paste content or placeholder into visible buffer
static void input_finalize_paste(TUIInputBuffer *input) {
    if (!input || !input->paste_content || input->paste_content_len == 0) {
        return;
    }

    int insert_pos = input->paste_start_pos;
    if (insert_pos < 0) insert_pos = 0;
    if (insert_pos > input->length) insert_pos = input->length;

    // For small pastes, insert directly without placeholder
    if (input->paste_content_len < PASTE_PLACEHOLDER_THRESHOLD) {
        // Check if we have space in buffer
        if (input->length + (int)input->paste_content_len >= (int)input->capacity - 1) {
            LOG_WARN("[TUI] Not enough space for pasted content (%zu chars)", input->paste_content_len);
            return;
        }

        int paste_len = (int)input->paste_content_len;

        // Make space for paste content
        memmove(&input->buffer[insert_pos + paste_len],
                &input->buffer[insert_pos],
                (size_t)(input->length - insert_pos + 1));

        // Copy paste content directly
        memcpy(&input->buffer[insert_pos], input->paste_content, input->paste_content_len);

        input->length += paste_len;
        input->cursor = insert_pos + paste_len;
        input->paste_placeholder_len = 0;  // No placeholder used

        LOG_DEBUG("[TUI] Inserted paste content directly at position %d (%zu chars)",
                  insert_pos, input->paste_content_len);
        return;
    }

    // For large pastes, use placeholder
    char placeholder[128];
    int placeholder_len = snprintf(placeholder, sizeof(placeholder),
                                   "[%zu characters pasted]",
                                   input->paste_content_len);

    if (placeholder_len >= (int)sizeof(placeholder)) {
        placeholder_len = sizeof(placeholder) - 1;
    }

    // Check if we have space in buffer
    if (input->length + placeholder_len >= (int)input->capacity - 1) {
        LOG_WARN("[TUI] Not enough space for paste placeholder");
        return;
    }

    // Make space for placeholder
    memmove(&input->buffer[insert_pos + placeholder_len],
            &input->buffer[insert_pos],
            (size_t)(input->length - insert_pos + 1));

    // Copy placeholder
    memcpy(&input->buffer[insert_pos], placeholder, (size_t)placeholder_len);

    input->length += placeholder_len;
    input->cursor = insert_pos + placeholder_len;
    input->paste_placeholder_len = placeholder_len;

    LOG_DEBUG("[TUI] Inserted paste placeholder at position %d: %s",
              insert_pos, placeholder);
}

// Redraw the input window
// Layout: [border (1 col)] [padding (1 col)] [text area] [padding (1 col)]
// The prompt parameter is kept for command/search mode compatibility
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
    int content_start_col;
    int right_margin;

    if (tui->input_box_style == INPUT_STYLE_BACKGROUND) {
        content_start_col = INPUT_CONTENT_START;  // border (1) + padding (1) = 2
        right_margin = INPUT_RIGHT_PADDING;       // padding (1) = 1
    } else {
        // BORDER style: box border on left + padding
        content_start_col = INPUT_LEFT_BORDER_WIDTH + INPUT_LEFT_PADDING;  // 1 + 1 = 2
        right_margin = INPUT_RIGHT_PADDING + INPUT_LEFT_BORDER_WIDTH;      // padding + right border = 2
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
    // For BACKGROUND style, we add one line of top padding and one line of bottom padding
    int window_height_needed = needed_lines;
    if (tui->input_box_style == INPUT_STYLE_BORDER) {
        window_height_needed += 2;  // +2 for top and bottom borders
    } else if (tui->input_box_style == INPUT_STYLE_BACKGROUND) {
        window_height_needed += 2;  // +2 for top and bottom padding
    }
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
    } else {
        content_start_col = INPUT_LEFT_BORDER_WIDTH + INPUT_LEFT_PADDING;
        right_margin = INPUT_RIGHT_PADDING + INPUT_LEFT_BORDER_WIDTH;
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
    // For BORDER style, we need to account for top and bottom borders
    // For BACKGROUND style, we account for top and bottom padding
    int content_start_row = (tui->input_box_style == INPUT_STYLE_BORDER) ? 1 :
                            (tui->input_box_style == INPUT_STYLE_BACKGROUND) ? 1 : 0;
    int border_height_offset = (tui->input_box_style == INPUT_STYLE_BORDER) ? 2 :
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
    } else {
        // Style 2: Full border with no background
        // Reset to default background (removes any previously set background color)
        if (has_colors()) {
            wbkgd(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }

        // Draw box border around the input area
        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_INPUT_BORDER));
        }
        box(win, 0, 0);
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_INPUT_BORDER));
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

    for (int i = 0; i < input->length && screen_y < (input->win_height - (tui->input_box_style == INPUT_STYLE_BORDER ? 1 : 0)); i++) {
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
        cursor_screen_y < (input->win_height - (tui->input_box_style == INPUT_STYLE_BORDER ? 1 : 0)) &&
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
                cursor_screen_y < (input->win_height - (tui->input_box_style == INPUT_STYLE_BORDER ? 1 : 0)) &&
                cursor_screen_x >= 0 && cursor_screen_x < input->win_width) {
                wmove(win, cursor_screen_y, cursor_screen_x);
            }
        }
    }

    wrefresh(win);

    // Suppress unused parameter warning - prompt kept for API compatibility
    (void)prompt;
}




void tui_update_status(TUIState *tui, const char *status_text) {
    if (!tui || !tui->is_initialized) return;

    const char *message = status_text ? status_text : "";
    LOG_DEBUG("[TUI] Status update requested: '%s'", message[0] ? message : "(clear)");

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

void tui_clear_conversation(TUIState *tui, const char *version, const char *model, const char *working_dir) {
    if (!tui || !tui->is_initialized) return;

    // Validate conversation pad exists
    if (!tui->wm.conv_pad) {
        LOG_ERROR("[TUI] Cannot clear conversation - conv_pad is NULL");
        return;
    }

    // Free all conversation entries
    tui_conversation_free_entries(tui);

    // Clear search pattern when conversation is cleared
    free(tui->last_search_pattern);
    tui->last_search_pattern = NULL;


    // Clear pad and reset content lines
    werase(tui->wm.conv_pad);
    window_manager_set_content_lines(&tui->wm, 0);

    // Add a system message indicating the clear
    tui_add_conversation_line(tui, "[System]", "Conversation history cleared", COLOR_PAIR_STATUS);

    // Show mascot banner again (useful info like current directory)
    // Try to get version/model/working_dir from parameters or TUI state
    const char *ver = version;
    const char *mod = model;
    const char *dir = working_dir;

    // If parameters are NULL but we have conversation state, try to get from there
    if (tui->conversation_state) {
        if (!mod) mod = tui->conversation_state->model;
        if (!dir) dir = tui->conversation_state->working_dir;
    }

    // Show mascot if we have at least model and working directory
    // Version can be NULL - we'll show placeholder in that case
    if (mod && dir) {
        tui_show_startup_banner(tui, ver ? ver : "?", mod, dir);
    }

    // Refresh all windows to ensure consistent state
    window_manager_refresh_all(&tui->wm);
}

// Redraw the entire conversation from stored entries
// This is useful for applying search highlighting after a search
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


void tui_scroll_conversation(TUIState *tui, int direction) {
    if (!tui || !tui->is_initialized || !tui->wm.conv_pad) return;
    window_manager_scroll(&tui->wm, direction);
    window_manager_refresh_conversation(&tui->wm);

    // Update status bar (to show new scroll percentage in NORMAL mode)
    if (tui->mode == TUI_MODE_NORMAL && tui->wm.status_height > 0) {
        render_status_window(tui);
    }

    // Refresh input window to keep cursor visible
    if (tui->wm.input_win) {
        touchwin(tui->wm.input_win);
        wrefresh(tui->wm.input_win);
    }
}

// ============================================================================
// Phase 2: Non-blocking Input and Event Loop Implementation
// ============================================================================

int tui_poll_input(TUIState *tui) {
    if (!tui || !tui->is_initialized || !tui->wm.input_win) {
        return ERR;
    }

    // Make wgetch() non-blocking temporarily
    nodelay(tui->wm.input_win, TRUE);
    int ch = wgetch(tui->wm.input_win);
    nodelay(tui->wm.input_win, FALSE);

    return ch;
}

// Handle command mode input
// Returns: 0 to continue, 1 if command executed, -1 on error/quit

// Handle search mode input
// Returns: 0 to continue, -1 on error/quit

// Perform search through conversation entries
// Returns: 1 if match found, 0 if no match, -1 on error

// Handle normal mode input
// Returns: 0 to continue, 1 to switch to insert mode, -1 on error/quit

// Check if paste mode should be exited due to timeout
// Returns 1 if paste ended, 0 otherwise
static int check_paste_timeout(TUIState *tui, const char *prompt) {
    if (!tui || !tui->input_buffer) {
        return 0;
    }

    TUIInputBuffer *input = tui->input_buffer;

    // Only check if we're in paste mode
    if (!input->paste_mode || input->rapid_input_count == 0) {
        return 0;
    }

    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    long elapsed_ms = (current_time.tv_sec - input->last_input_time.tv_sec) * 1000 +
                      (current_time.tv_nsec - input->last_input_time.tv_nsec) / 1000000;

    // Exit paste mode if there's been a pause exceeding configured timeout
    if (elapsed_ms > g_paste_timeout_ms) {
        input->paste_mode = 0;
        input->rapid_input_count = 0;
        LOG_DEBUG("[TUI] Paste timeout detected - exiting paste mode (heuristic), pasted %zu characters",
                 input->paste_content_len);

        // For heuristic mode, remove the already-inserted characters
        int chars_to_remove = input->cursor - input->paste_start_pos;
        if (chars_to_remove > 0) {
            memmove(&input->buffer[input->paste_start_pos],
                    &input->buffer[input->cursor],
                    (size_t)(input->length - input->cursor + 1));
            input->length -= chars_to_remove;
            input->cursor = input->paste_start_pos;
        }

        // Insert placeholder or content directly
        input_finalize_paste(input);
        input_redraw(tui, prompt);
        return 1;
    }

    return 0;
}

int tui_process_input_char(TUIState *tui, int ch, const char *prompt, void *user_data) {
    if (!tui || !tui->is_initialized || !tui->wm.input_win) {
        return -1;
    }

    TUIInputBuffer *input = tui->input_buffer;
    if (!input) {
        return -1;
    }

    // Handle command mode separately
    if (tui->mode == TUI_MODE_COMMAND) {
        int result = tui_modes_handle_command(tui, ch, prompt);
        if (result == -1) {
            return -1;  // Quit signal
        }
        // Command mode handles all input internally
        return 0;
    }

    // Handle search mode separately
    if (tui->mode == TUI_MODE_SEARCH) {
        int result = tui_modes_handle_search(tui, ch, prompt);
        if (result == -1) {
            return -1;  // Quit signal
        }
        // Search mode handles all input internally
        return 0;
    }

    // Handle normal mode separately
    if (tui->mode == TUI_MODE_NORMAL) {
        int result = tui_modes_handle_normal(tui, ch, prompt, user_data);
        if (result == -1) {
            return -1;  // Quit signal
        }
        // After handling normal mode input, always return
        // We don't want to process the mode-switching key as text
        return 0;
    }

    // Handle file search mode separately
    if (tui->mode == TUI_MODE_FILE_SEARCH) {
        int result = file_search_process_key(&tui->file_search, ch);
        if (result == 1) {
            // Selection made - insert path into input buffer
            const char *selected = file_search_get_selected(&tui->file_search);
            if (selected) {
                tui_input_insert_string(tui->input_buffer, selected);
                LOG_DEBUG("[TUI] Inserted file path: %s", selected);
            }
            file_search_stop(&tui->file_search);
            tui->mode = TUI_MODE_INSERT;
            // Refresh all windows to restore display
            window_manager_refresh_all(&tui->wm);
            input_redraw(tui, prompt);
        } else if (result == -1) {
            // Cancelled
            file_search_stop(&tui->file_search);
            tui->mode = TUI_MODE_INSERT;
            // Refresh all windows to restore display
            window_manager_refresh_all(&tui->wm);
            input_redraw(tui, prompt);
        } else {
            // Continue - just render the popup
            file_search_render(&tui->file_search);
        }
        return 0;
    }

    // Handle history search mode separately
    if (tui->mode == TUI_MODE_HISTORY_SEARCH) {
        return tui_history_process_search_key(tui, ch, prompt);
    }

    if (g_enable_paste_heuristic) {
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);

        long elapsed_ms = (current_time.tv_sec - input->last_input_time.tv_sec) * 1000 +
                          (current_time.tv_nsec - input->last_input_time.tv_nsec) / 1000000;

        // Very conservative thresholds to avoid false positives during normal typing
        if (elapsed_ms < g_paste_gap_ms) {
            input->rapid_input_count++;
            if (input->rapid_input_count >= g_paste_burst_min && !input->paste_mode) {
                input->paste_mode = 1;

                // For heuristic mode, we've already inserted some characters
                // Need to capture what we've inserted so far
                int chars_already_inserted = input->rapid_input_count;
                input->paste_start_pos = input->cursor - chars_already_inserted;
                if (input->paste_start_pos < 0) input->paste_start_pos = 0;

                // Allocate paste buffer if not already allocated
                if (!input->paste_content) {
                    input->paste_capacity = 4096;
                    input->paste_content = malloc(input->paste_capacity);
                    if (!input->paste_content) {
                        LOG_ERROR("[TUI] Failed to allocate paste buffer");
                        input->paste_mode = 0;
                        // Do not early-return; continue processing as normal char
                    }
                }

                // Copy the already-inserted characters to paste buffer
                if (input->paste_content) {
                    input->paste_content_len = (size_t)chars_already_inserted;
                    if (input->paste_content_len > 0) {
                        memcpy(input->paste_content,
                               &input->buffer[input->paste_start_pos],
                               input->paste_content_len);
                    }
                }

                LOG_DEBUG("[TUI] Rapid input detected - entering paste mode (heuristic) at position %d, captured %d chars",
                         input->paste_start_pos, chars_already_inserted);
            }
        } else if (elapsed_ms > 500) {
            // Reset rapid input counter if there's a pause (but not in paste mode)
            if (!input->paste_mode) {
                input->rapid_input_count = 0;
            }
            // Paste mode timeout is handled in check_paste_timeout() called from main loop
        }

        input->last_input_time = current_time;
    }

    // Handle special keys
    if (ch == KEY_RESIZE) {
        tui_handle_resize(tui);
        refresh_conversation_viewport(tui);
        render_status_window(tui);
        input_redraw(tui, prompt);
        return 0;
    } else if (ch == KEY_BTAB) {  // Shift+Tab: toggle plan_mode
        // Extract InteractiveContext from user_data
        // Structure matches InteractiveContext in klawed.c
        typedef struct {
            ConversationState *state;
            TUIState *tui;
            void *worker;
            void *instruction_queue;
            TUIMessageQueue *tui_queue;
            int instruction_queue_capacity;
        } InteractiveContextView;

        if (user_data) {
            InteractiveContextView *ctx = (InteractiveContextView *)user_data;
            ConversationState *state = ctx->state;

            if (state) {
                // Lock the conversation state
                if (conversation_state_lock(state) == 0) {
                    // Toggle plan_mode in state
                    state->plan_mode = state->plan_mode ? 0 : 1;
                    int new_plan_mode = state->plan_mode;

                    // Rebuild system prompt to reflect plan mode change
                    char *new_system_prompt = build_system_prompt(state);
                    if (new_system_prompt) {
                        if (state->count > 0 && state->messages[0].role == MSG_SYSTEM) {
                            free(state->messages[0].contents[0].text);
                            size_t len = strlen(new_system_prompt) + 1;
                            state->messages[0].contents[0].text = malloc(len);
                            if (!state->messages[0].contents[0].text) {
                                LOG_ERROR("[TUI] Failed to allocate memory for updated system prompt");
                            } else {
                                strlcpy(state->messages[0].contents[0].text, new_system_prompt, len);
                                LOG_DEBUG("[TUI] System prompt updated to reflect plan mode change");
                            }
                        }
                        free(new_system_prompt);
                    } else {
                        LOG_ERROR("[TUI] Failed to rebuild system prompt after plan mode toggle");
                    }

                    conversation_state_unlock(state);

                    // Log the toggle
                    LOG_INFO("[TUI] Plan mode toggled: %s", new_plan_mode ? "ON" : "OFF");
                    LOG_DEBUG("[TUI] Plan mode value in state: %d", state->plan_mode);

                    // Refresh status bar to show change
                    render_status_window(tui);
                }
            }
        }
        return 0;
    } else if (ch == 6) {  // Ctrl+F: File search (in INSERT mode)
        // Start file search popup
        LOG_DEBUG("[TUI] Ctrl+F pressed - starting file search");
        if (file_search_start(&tui->file_search,
                              tui->wm.screen_height,
                              tui->wm.screen_width,
                              NULL) == 0) {
            tui->mode = TUI_MODE_FILE_SEARCH;
            file_search_render(&tui->file_search);
        } else {
            LOG_ERROR("[TUI] Failed to start file search");
            beep();
        }
        return 0;
    } else if (ch == 18) {  // Ctrl+R: History search (in INSERT mode)
        // Start history search popup
        tui_history_start_search(tui);
        return 0;
    } else if (ch == 1) {  // Ctrl+A: beginning of line
        input->cursor = 0;
        input_redraw(tui, prompt);
    } else if (ch == 5) {  // Ctrl+E: end of line
        input->cursor = input->length;
        input_redraw(tui, prompt);
    } else if (ch == 4) {  // Ctrl+D: EOF
        return -1;
    } else if (ch == 11) {  // Ctrl+K: kill to end of line
        input->buffer[input->cursor] = '\0';
        input->length = input->cursor;
        input_redraw(tui, prompt);
    } else if (ch == 21) {  // Ctrl+U: kill to beginning of line
        if (input->cursor > 0) {
            memmove(input->buffer,
                    &input->buffer[input->cursor],
                    (size_t)(input->length - input->cursor + 1));
            input->length -= input->cursor;
            input->cursor = 0;
            input_redraw(tui, prompt);
        }
    } else if (ch == 12) {  // Ctrl+L: clear input and search status
        input->buffer[0] = '\0';
        input->length = 0;
        input->cursor = 0;
        input->paste_mode = 0;  // Reset paste mode
        input->rapid_input_count = 0;
        // Also clear search status
        tui_update_status(tui, "");
        free(tui->last_search_pattern);
        tui->last_search_pattern = NULL;
        tui->last_search_match_line = -1;
        input_redraw(tui, prompt);
    } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {  // Backspace
        if (tui_input_backspace(input) > 0) {
            input_redraw(tui, prompt);
        }
    } else if (ch == KEY_DC) {  // Delete key
        if (tui_input_delete_char(input) > 0) {
            input_redraw(tui, prompt);
        }
    } else if (ch == KEY_LEFT) {  // Left arrow
        if (input->cursor > 0) {
            input->cursor--;
            input_redraw(tui, prompt);
        }
    } else if (ch == KEY_RIGHT) {  // Right arrow
        if (input->cursor < input->length) {
            input->cursor++;
            input_redraw(tui, prompt);
        }
    } else if (ch == KEY_HOME) {  // Home
        input->cursor = 0;
        input_redraw(tui, prompt);
    } else if (ch == KEY_END) {  // End
        input->cursor = input->length;
        input_redraw(tui, prompt);
    } else if (ch == 16) {  // Ctrl+P: previous input history
        tui_history_navigate_prev(tui, prompt);
    } else if (ch == 14) {  // Ctrl+N: next input history
        tui_history_navigate_next(tui, prompt);
    } else if (ch == KEY_PPAGE) {  // Page Up: scroll conversation up
        tui_scroll_conversation(tui, -10);
        input_redraw(tui, prompt);
    } else if (ch == KEY_NPAGE) {  // Page Down: scroll conversation down
        tui_scroll_conversation(tui, 10);
        input_redraw(tui, prompt);
    } else if (ch == KEY_UP) {  // Up arrow: scroll conversation up (1 line)
        tui_scroll_conversation(tui, -1);
        input_redraw(tui, prompt);
    } else if (ch == KEY_DOWN) {  // Down arrow: scroll conversation down (1 line)
        tui_scroll_conversation(tui, 1);
        input_redraw(tui, prompt);
    } else if (ch == 10) {  // Ctrl+J: insert newline
        unsigned char newline = '\n';
        if (tui_input_insert_char(input, &newline, 1) == 0) {
            // Skip redraw during paste mode - will redraw once at end
            if (!input->paste_mode) {
                input_redraw(tui, prompt);
            }
        }
    } else if (ch == 13) {  // Enter: submit or newline
        if (input->paste_mode) {
            // In paste mode, Enter inserts newline instead of submitting
            unsigned char newline = '\n';
            if (tui_input_insert_char(input, &newline, 1) == 0) {
                // Skip redraw during paste mode - will redraw once at end
                // (This shouldn't happen since we're in paste mode, but defensive)
                if (!input->paste_mode) {
                    input_redraw(tui, prompt);
                }
            }
            return 0;
        } else {
            // Normal mode: submit
            input->rapid_input_count = 0;  // Reset on submit
            return 1;  // Signal submission
        }
    } else if (ch == 3) {   // Ctrl+C: Interrupt running action
        // Signal interrupt request to event loop
        return 2;
    } else if (ch == 27) {  // ESC sequence (Alt key combinations, bracketed paste, or mode switch)
        // Set nodelay to check for following character
        nodelay(tui->wm.input_win, TRUE);
        int next_ch = wgetch(tui->wm.input_win);

        LOG_DEBUG("[TUI] ESC sequence detected, next_ch=%d", next_ch);

        // If standalone ESC (no following character), switch to NORMAL mode (vim-style)
        if (next_ch == ERR) {
            nodelay(tui->wm.input_win, FALSE);

            // In INSERT mode, ESC/Ctrl+[ leaves INSERT and enters NORMAL (scroll) mode
            if (tui->mode == TUI_MODE_INSERT) {
                tui->mode = TUI_MODE_NORMAL;
                tui->normal_mode_last_key = 0;
                if (tui->wm.status_height > 0) {
                    render_status_window(tui);
                }
                input_redraw(tui, prompt);
            }
            return 0;  // Do not signal interrupt here
        }

        if (next_ch == '[') {
            // Could be bracketed paste sequence or other CSI sequence
            // Read the sequence with a small delay to allow characters to arrive
            nodelay(tui->wm.input_win, FALSE);
            // IMPORTANT: Set timeout on the same window we're reading from.
            // Using timeout() (stdscr) here caused blocking wgetch() on input_win
            // and made the UI appear to hang until more input arrived.
            wtimeout(tui->wm.input_win, 100);  // 100ms timeout for sequence

            int ch1 = wgetch(tui->wm.input_win);
            int ch2 = wgetch(tui->wm.input_win);
            int ch3 = wgetch(tui->wm.input_win);
            int ch4 = wgetch(tui->wm.input_win);

            // Restore blocking behavior on input window
            wtimeout(tui->wm.input_win, -1);  // Back to blocking

            LOG_DEBUG("[TUI] Escape sequence: ESC[%c%c%c%c (values: %d %d %d %d)",
                     ch1 > 0 ? ch1 : '?', ch2 > 0 ? ch2 : '?',
                     ch3 > 0 ? ch3 : '?', ch4 > 0 ? ch4 : '?',
                     ch1, ch2, ch3, ch4);

            // Check for ESC[200~ (paste start) or ESC[201~ (paste end)
            if (ch1 == '2' && ch2 == '0' && ch3 == '0' && ch4 == '~') {
                // Bracketed paste start
                input->paste_mode = 1;
                input->paste_start_pos = input->cursor;
                input->paste_content_len = 0;
                // Allocate paste buffer if not already allocated
                if (!input->paste_content) {
                    input->paste_capacity = 4096;
                    input->paste_content = malloc(input->paste_capacity);
                    if (!input->paste_content) {
                        LOG_ERROR("[TUI] Failed to allocate paste buffer");
                        input->paste_mode = 0;
                        return 0;
                    }
                }
                LOG_DEBUG("[TUI] Bracketed paste mode started at position %d", input->paste_start_pos);
                return 0;
            } else if (ch1 == '2' && ch2 == '0' && ch3 == '1' && ch4 == '~') {
                // Bracketed paste end
                input->paste_mode = 0;
                LOG_DEBUG("[TUI] Bracketed paste mode ended, pasted %zu characters",
                         input->paste_content_len);

                // Insert placeholder or content directly
                input_finalize_paste(input);
                input_redraw(tui, prompt);
                return 0;
            }
            // Other CSI sequences - ignore
            return 0;
        }

        nodelay(tui->wm.input_win, FALSE);

        // Handle Alt key combinations
        if (next_ch == 'b' || next_ch == 'B') {  // Alt+b: backward word
            input->cursor = tui_input_move_backward_word(input->buffer, input->cursor);
            input_redraw(tui, prompt);
        } else if (next_ch == 'f' || next_ch == 'F') {  // Alt+f: forward word
            input->cursor = tui_input_move_forward_word(input->buffer, input->cursor, input->length);
            input_redraw(tui, prompt);
        } else if (next_ch == 'd' || next_ch == 'D') {  // Alt+d: delete next word
            if (tui_input_delete_word_forward(input) > 0) {
                input_redraw(tui, prompt);
            }
        } else if (next_ch == KEY_BACKSPACE || next_ch == 127 || next_ch == 8) {  // Alt+Backspace
            if (tui_input_delete_word_backward(input) > 0) {
                input_redraw(tui, prompt);
            }
        }
    } else if (ch == '\t' || ch == 9) {  // Tab key - trigger autocomplete
        // Handle tab completion for commands starting with '/'
        if (input->buffer && input->buffer[0] == '/') {
            tui_handle_tab_completion(tui, prompt);
        } else {
            // Insert tab character
            unsigned char tab = '\t';
            if (tui_input_insert_char(input, &tab, 1) == 0) {
                if (!input->paste_mode) {
                    input_redraw(tui, prompt);
                }
            }
        }
    } else if (ch >= 32 && ch < 127) {  // Printable ASCII
        unsigned char c = (unsigned char)ch;
        if (tui_input_insert_char(input, &c, 1) == 0) {
            // Skip redraw during paste mode - will redraw once at end
            if (!input->paste_mode) {
                input_redraw(tui, prompt);
            }
        }
    } else if (ch >= 128) {  // UTF-8 multibyte character (basic support)
        unsigned char c = (unsigned char)ch;
        if (tui_input_insert_char(input, &c, 1) == 0) {
            // Skip redraw during paste mode - will redraw once at end
            if (!input->paste_mode) {
                input_redraw(tui, prompt);
            }
        }
    }

    return 0;  // Continue processing
}

const char* tui_get_input_buffer(TUIState *tui) {
    if (!tui || !tui->input_buffer || tui->input_buffer->length == 0) {
        return NULL;
    }

    TUIInputBuffer *input = tui->input_buffer;

    // If there's no paste content, return buffer as-is
    if (!input->paste_content || input->paste_content_len == 0 ||
        input->paste_placeholder_len == 0) {
        return input->buffer;
    }

    // We have paste content that needs to be reconstructed
    // Buffer structure: [text before placeholder][placeholder][text after placeholder]
    // We need: [text before placeholder][actual paste content][text after placeholder]

    // Calculate sizes
    size_t before_len = (size_t)input->paste_start_pos;
    size_t after_start = (size_t)(input->paste_start_pos + input->paste_placeholder_len);
    size_t after_len = 0;
    if (after_start <= (size_t)input->length) {
        after_len = (size_t)input->length - after_start;
    } else {
        // Inconsistent indices; avoid underflow and return best-effort buffer
        LOG_WARN("[TUI] Paste reconstruction index out of range (after_start=%zu, length=%d)", after_start, input->length);
        after_start = (size_t)input->length;
        after_len = 0;
    }
    size_t total_len = before_len + input->paste_content_len + after_len;

    // Allocate temporary buffer for reconstruction
    // Use a static buffer that grows as needed (freed on next call or cleanup)
    static char *reconstructed = NULL;
    static size_t reconstructed_capacity = 0;

    if (total_len + 1 > reconstructed_capacity) {
        reconstructed_capacity = total_len + 1024;  // Extra space
        char *new_buf = realloc(reconstructed, reconstructed_capacity);
        if (!new_buf) {
            LOG_ERROR("[TUI] Failed to allocate buffer for paste reconstruction");
            return input->buffer;  // Fallback to placeholder version
        }
        reconstructed = new_buf;
    }

    // Reconstruct: before + paste_content + after
    char *dest = reconstructed;
    if (before_len > 0) {
        memcpy(dest, input->buffer, before_len);
        dest += before_len;
    }
    if (input->paste_content_len > 0) {
        memcpy(dest, input->paste_content, input->paste_content_len);
        dest += input->paste_content_len;
    }
    if (after_len > 0) {
        memcpy(dest, &input->buffer[after_start], after_len);
        dest += after_len;
    }
    *dest = '\0';

    LOG_DEBUG("[TUI] Reconstructed input with paste: before=%zu, paste=%zu, after=%zu, total=%zu",
              before_len, input->paste_content_len, after_len, total_len);

    return reconstructed;
}

void tui_clear_input_buffer(TUIState *tui) {
    if (!tui || !tui->input_buffer) {
        return;
    }

    tui->input_buffer->buffer[0] = '\0';
    tui->input_buffer->length = 0;
    tui->input_buffer->cursor = 0;
    tui->input_buffer->view_offset = 0;
    tui->input_buffer->line_scroll_offset = 0;
    tui->input_buffer->paste_mode = 0;  // Reset paste mode on clear
    tui->input_buffer->rapid_input_count = 0;

    // Clear paste tracking
    tui->input_buffer->paste_content_len = 0;
    tui->input_buffer->paste_start_pos = 0;
    tui->input_buffer->paste_placeholder_len = 0;
}

int tui_insert_input_text(TUIState *tui, const char *text) {
    if (!tui || !tui->input_buffer || !text) {
        return -1;
    }

    // Insert the text into the input buffer
    if (tui_input_insert_string(tui->input_buffer, text) != 0) {
        return -1;
    }

    // Note: The caller should ensure the TUI is refreshed after this call
    // (e.g., via tui_resume() -> tui_refresh() for commands that suspend the TUI)
    return 0;
}

void tui_redraw_input(TUIState *tui, const char *prompt) {
    input_redraw(tui, prompt);
}

static TUIColorPair infer_color_from_prefix(const char *prefix) {
    if (!prefix) {
        return COLOR_PAIR_DEFAULT;
    }
    if (strstr(prefix, "User")) {
        return COLOR_PAIR_USER;
    }
    if (strstr(prefix, "Assistant")) {
        return COLOR_PAIR_ASSISTANT;
    }
    if (strstr(prefix, "Tool")) {
        return COLOR_PAIR_TOOL;
    }
    if (strstr(prefix, "Error")) {
        return COLOR_PAIR_ERROR;
    }
    if (strstr(prefix, "Diff")) {
        return COLOR_PAIR_STATUS;
    }
    if (strstr(prefix, "System")) {
        return COLOR_PAIR_STATUS;
    }
    if (strstr(prefix, "Prompt")) {
        return COLOR_PAIR_PROMPT;
    }
    // Heuristic: any other bracketed role tag like "[Bash]", "[Read]" is a tool
    if (prefix[0] == '[') {
        const char *close = strchr(prefix, ']');
        if (close && close > prefix + 1) {
            // Extract inner label and compare against known non-tool roles
            size_t len = (size_t)(close - (prefix + 1));
            char buf[32];
            if (len >= sizeof(buf)) len = sizeof(buf) - 1;
            memcpy(buf, prefix + 1, len);
            buf[len] = '\0';
            // Normalize to lowercase for comparison
            for (size_t i = 0; i < len; i++) {
                if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] = (char)(buf[i] - 'A' + 'a');
            }
            if (strcmp(buf, "user") != 0 &&
                strcmp(buf, "assistant") != 0 &&
                strcmp(buf, "error") != 0 &&
                strcmp(buf, "system") != 0 &&
                strcmp(buf, "status") != 0 &&
                strcmp(buf, "prompt") != 0) {
                return COLOR_PAIR_TOOL;
            }
        }
    }
    return COLOR_PAIR_DEFAULT;
}

static void dispatch_tui_message(TUIState *tui, TUIMessage *msg) {
    if (!tui || !msg) {
        return;
    }

    switch (msg->type) {
        case TUI_MSG_ADD_LINE: {
            if (!msg->text) {
                tui_add_conversation_line(tui, "", "", COLOR_PAIR_DEFAULT);
                break;
            }

            char *mutable_text = msg->text;
            const char *content = mutable_text;

            if (mutable_text[0] == '[') {
                char *close = strchr(mutable_text, ']');
                if (close) {
                    size_t prefix_len = (size_t)(close - mutable_text + 1);
                    char *prefix = malloc(prefix_len + 1);
                    if (prefix) {
                        memcpy(prefix, mutable_text, prefix_len);
                        prefix[prefix_len] = '\0';
                    }

                    const char *content_start = close + 1;
                    while (*content_start == ' ') {
                        content_start++;
                    }

                    const char *color_source = prefix ? prefix : mutable_text;
                    tui_add_conversation_line(
                        tui,
                        prefix ? prefix : "",
                        content_start,
                        infer_color_from_prefix(color_source));

                    free(prefix);
                    break;
                }
            }

            // Check for diff lines (no brackets, just colored by first character)
            TUIColorPair diff_color = COLOR_PAIR_DEFAULT;
            size_t text_len = strlen(mutable_text);

            if (text_len > 0) {
                char first = mutable_text[0];
                char second = (text_len > 1) ? mutable_text[1] : '\0';

                if (first == '+' && second != '+') {
                    diff_color = COLOR_PAIR_USER;  // Green for additions
                } else if (first == '-' && second != '-') {
                    diff_color = COLOR_PAIR_ERROR;  // Red for deletions
                } else if (first == '@' && second == '@') {
                    diff_color = COLOR_PAIR_STATUS;  // Status color for hunk headers
                }
            }

            tui_add_conversation_line(tui, "", content, diff_color);
            break;
        }

        case TUI_MSG_STATUS:
            tui_update_status(tui, msg->text ? msg->text : "");
            break;

        case TUI_MSG_CLEAR:
            tui_clear_conversation(tui, NULL, NULL, NULL);
            break;

        case TUI_MSG_ERROR:
            tui_add_conversation_line(
                tui,
                "[Error]",
                msg->text ? msg->text : "Unknown error",
                COLOR_PAIR_ERROR);
            break;

        case TUI_MSG_TODO_UPDATE:
            // Placeholder for future TODO list integration
            break;



        default:
            /* Unknown message type; ignore */
            break;
    }
}

static int process_tui_messages(TUIState *tui,
                                TUIMessageQueue *msg_queue,
                                int max_messages) {
    if (!tui || !msg_queue || max_messages <= 0) {
        return 0;
    }

    int processed = 0;
    TUIMessage msg = {0};

    while (processed < max_messages) {
        int rc = poll_tui_message(msg_queue, &msg);
        if (rc <= 0) {
            if (rc < 0) {
                LOG_WARN("[TUI] Failed to poll message queue");
            }
            break;
        }

        dispatch_tui_message(tui, &msg);
        free(msg.text);
        msg.text = NULL;
        processed++;
    }

    return processed;
}

int tui_event_loop(TUIState *tui, const char *prompt,
                   int (*submit_callback)(const char *input, void *user_data),
                   int (*interrupt_callback)(void *user_data),
                   int (*keypress_callback)(void *user_data),
                   int (*external_input_callback)(void *user_data, char *buffer, int buffer_size),
                   void *user_data,
                   void *msg_queue_ptr) {
    if (!tui || !tui->is_initialized || !submit_callback) {
        return -1;
    }

    TUIMessageQueue *msg_queue = (TUIMessageQueue *)msg_queue_ptr;
    int running = 1;
    const long frame_time_us = 8333;  // ~120 FPS (1/120 second in microseconds)

    // Note: tui->conversation_state is already set during tui_init()
    // No need to copy plan_mode separately
    (void)user_data;  // Mark as unused to avoid compiler warning

    // Clear input buffer at start
    tui_clear_input_buffer(tui);

    // Initial draw (ensure all windows reflect current size)
    refresh_conversation_viewport(tui);
    render_status_window(tui);
    tui_redraw_input(tui, prompt);

    while (running) {
        struct timespec frame_start;
        clock_gettime(CLOCK_MONOTONIC, &frame_start);

        // 1. Check for resize
        if (tui_window_resize_pending()) {
            tui_window_clear_resize_flag();
            tui_handle_resize(tui);

            // Verify windows are still valid after resize
            if (!tui->wm.conv_pad || !tui->wm.input_win) {
                LOG_ERROR("[TUI] Windows invalid after resize, exiting event loop");
                running = 0;
                break;
            }

            refresh_conversation_viewport(tui);
            render_status_window(tui);
            tui_redraw_input(tui, prompt);
        }

        // 2. Check for paste timeout (even when no input arrives)
        check_paste_timeout(tui, prompt);

        // 3. Check for external input (e.g., sockets)
        if (external_input_callback) {
            char ext_buffer[4096];
            int ext_bytes = external_input_callback(user_data, ext_buffer, sizeof(ext_buffer));
            if (ext_bytes > 0) {
                // Process external input
                ext_buffer[ext_bytes] = '\0';
                LOG_DEBUG("[TUI] External input received: %s", ext_buffer);

                // Check for termination signal
                if (ext_bytes == 1 && ext_buffer[0] == 0x04) {
                    LOG_INFO("Termination signal received via external input");
                    running = 0;
                    break;
                }

                // Check for termination command
                if (ext_bytes >= 12 && strncmp(ext_buffer, "__TERMINATE__", 12) == 0) {
                    LOG_INFO("Termination command received via external input");
                    running = 0;
                    break;
                }

                // Submit the input
                if (ext_buffer[0] != '\0') {
                    LOG_DEBUG("[TUI] Submitting external input (%d bytes)", ext_bytes);
                    // Append to history (both in-memory and persistent)
                    tui_history_append(tui, ext_buffer);
                    // Call the callback
                    int callback_result = submit_callback(ext_buffer, user_data);

                    // Clear input buffer after submission, unless callback returns -1
                    // (used for :re ! commands that insert output into input buffer)
                    if (callback_result != -1) {
                        tui_clear_input_buffer(tui);
                        tui_redraw_input(tui, prompt);
                    }

                    // Check if callback wants to exit
                    if (callback_result == 1) {
                        LOG_DEBUG("[TUI] Callback requested exit (code=%d)", callback_result);
                        running = 0;
                    }
                }
            } else if (ext_bytes < 0) {
                // Error or termination requested
                LOG_DEBUG("[TUI] External input callback returned error/termination");
                running = 0;
                break;
            }
        }

        // 4. Poll for TUI input (non-blocking)
        // If in paste mode, drain all available input quickly
        int chars_processed = 0;
        // Drain more than 1 char per frame to avoid artificial delays/lag on quick typing
        int max_chars_per_frame = (tui->input_buffer && tui->input_buffer->paste_mode) ? 10000 : 32;

        while (chars_processed < max_chars_per_frame) {
            int ch = tui_poll_input(tui);
            if (ch == ERR) {
                break;  // No more input available
            }

            chars_processed++;

            int result = tui_process_input_char(tui, ch, prompt, user_data);

            // Notify about keypress (after processing, and only for normal input)
            // Skip for Ctrl+C (result==2) since interrupt callback handles that
            if (keypress_callback && result == 0) {
                keypress_callback(user_data);
            }
            if (result == 1) {
                // Enter pressed - submit input
                const char *input = tui_get_input_buffer(tui);
                if (input && strlen(input) > 0) {
                    LOG_DEBUG("[TUI] Submitting input (%zu bytes)", strlen(input));
                    // Append to history (both in-memory and persistent)
                    tui_history_append(tui, input);
                    // Call the callback
                    int callback_result = submit_callback(input, user_data);

                    // Clear input buffer after submission, unless callback returns -1
                    // (used for :re ! commands that insert output into input buffer)
                    if (callback_result != -1) {
                        tui_clear_input_buffer(tui);
                        tui_redraw_input(tui, prompt);
                    }

                    // Check if callback wants to exit
                    if (callback_result == 1) {
                        LOG_DEBUG("[TUI] Callback requested exit (code=%d)", callback_result);
                        running = 0;
                    }
                }
                break;  // Stop processing after submission
            } else if (result == 2) {
                // Ctrl+C pressed - interrupt requested
                LOG_DEBUG("[TUI] Interrupt requested (Ctrl+C)");
                if (interrupt_callback) {
                    int interrupt_result = interrupt_callback(user_data);
                    if (interrupt_result != 0) {
                        LOG_DEBUG("[TUI] Interrupt callback requested exit (code=%d)", interrupt_result);
                        running = 0;
                    }
                }
                // Stay in INSERT mode after interrupt (user can press Esc/Ctrl+[ to enter NORMAL mode if desired)
                break;  // Stop processing after interrupt
            } else if (result == -1) {
                // EOF/quit signal
                LOG_DEBUG("[TUI] Input processing returned EOF/quit");
                running = 0;
                break;
            }

            // If not in paste mode anymore, stop draining and process one char per frame
            if (tui->input_buffer && !tui->input_buffer->paste_mode) {
                break;
            }
        }

        if (chars_processed > 1) {
            LOG_DEBUG("[TUI] Fast-drained %d characters in paste mode", chars_processed);
        }

        // 4. Process TUI message queue (if provided)
        if (msg_queue) {
            int messages_processed = process_tui_messages(tui, msg_queue, TUI_MAX_MESSAGES_PER_FRAME);
            if (messages_processed > 0) {
                LOG_DEBUG("[TUI] Processed %d queued message(s)", messages_processed);
                tui_redraw_input(tui, prompt);
            }
        }

        // 5. Update subagent status periodically (every frame)
        // This updates internal state but doesn't render yet
        if (tui->conversation_state && tui->conversation_state->subagent_manager) {
            // Update status of all tracked subagents (check if running, read log tails)
            // Use 5 lines of tail output for monitoring
            subagent_manager_update_all(tui->conversation_state->subagent_manager, 5);
        }

        // Update spinner animation if active
        status_spinner_tick(tui);

        // 5. Sleep to maintain frame rate
        struct timespec frame_end;
        clock_gettime(CLOCK_MONOTONIC, &frame_end);

        long elapsed_ns = (frame_end.tv_sec - frame_start.tv_sec) * 1000000000L +
                         (frame_end.tv_nsec - frame_start.tv_nsec);
        long elapsed_us = elapsed_ns / 1000;

        if (elapsed_us < frame_time_us) {
            usleep((useconds_t)(frame_time_us - elapsed_us));
      }
    }

    return 0;
}

void tui_drain_message_queue(TUIState *tui, const char *prompt, void *msg_queue_ptr) {
    if (!tui || !msg_queue_ptr) {
        return;
    }

    TUIMessageQueue *msg_queue = (TUIMessageQueue *)msg_queue_ptr;
    int processed = 0;

    do {
        processed = process_tui_messages(tui, msg_queue, TUI_MAX_MESSAGES_PER_FRAME);
        if (processed > 0) {
            if (prompt) {
                tui_redraw_input(tui, prompt);
            } else {
                tui_refresh(tui);
            }
        }
    } while (processed > 0);
}
