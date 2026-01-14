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
static int perform_search(TUIState *tui, const char *pattern, int direction);
// Less sensitive defaults: require very fast, large bursts to classify as paste
static int g_paste_gap_ms = 12;         // max gap to count as "rapid" for burst
static int g_paste_burst_min = 60;      // min consecutive keys within gap to enter paste mode
static int g_paste_timeout_ms = 400;    // idle time to finalize paste

// Global flag to detect terminal resize
static volatile sig_atomic_t g_resize_flag = 0;

// Ncurses color pair definitions are now in tui.h for sharing across TUI components

// Validate TUI window state (debug builds)
// Uses ncurses is_pad() function to check window types
static void validate_tui_windows(TUIState *tui) {
#ifdef DEBUG
    if (!tui) return;
    window_manager_validate(&tui->wm);
#else
    (void)tui;
#endif
}

// Signal handler for window resize
#ifdef SIGWINCH
static void handle_resize(int sig) {
    (void)sig;
    g_resize_flag = 1;
}
#endif

// Check if resize is pending and return the flag status
// This allows external code to check for resize events
/*
static int tui_resize_pending(void) {
    return g_resize_flag != 0;
}
*/

// Convert RGB (0-255) to ncurses color (0-1000)
static short rgb_to_ncurses(int value) {
    return (short)((value * 1000) / 255);
}

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

static void render_status_window(TUIState *tui) {
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
static void init_ncurses_colors(void) {
    // Check if terminal supports colors
    if (!has_colors()) {
        LOG_DEBUG("[TUI] Terminal does not support colors");
        return;
    }

    start_color();
    use_default_colors();  // Use terminal's default colors as base

    // If we have a loaded theme, use it to initialize custom colors
    if (g_theme_loaded) {
        LOG_DEBUG("[TUI] Initializing ncurses colors from loaded theme");

        int supports_256 = (COLORS >= 256);

        // Define custom colors (colors 16-21 are safe to redefine)
        if (can_change_color()) {
            // Foreground
            init_color(16,
                rgb_to_ncurses(g_theme.foreground_rgb.r),
                rgb_to_ncurses(g_theme.foreground_rgb.g),
                rgb_to_ncurses(g_theme.foreground_rgb.b));

            // User (green)
            init_color(17,
                rgb_to_ncurses(g_theme.user_rgb.r),
                rgb_to_ncurses(g_theme.user_rgb.g),
                rgb_to_ncurses(g_theme.user_rgb.b));

            // Assistant (blue/cyan)
            init_color(18,
                rgb_to_ncurses(g_theme.assistant_rgb.r),
                rgb_to_ncurses(g_theme.assistant_rgb.g),
                rgb_to_ncurses(g_theme.assistant_rgb.b));

            // Status (yellow)
            init_color(19,
                rgb_to_ncurses(g_theme.status_rgb.r),
                rgb_to_ncurses(g_theme.status_rgb.g),
                rgb_to_ncurses(g_theme.status_rgb.b));

            // Error (red)
            init_color(20,
                rgb_to_ncurses(g_theme.error_rgb.r),
                rgb_to_ncurses(g_theme.error_rgb.g),
                rgb_to_ncurses(g_theme.error_rgb.b));

            // Tool color (use theme tool color for clear distinction)
            init_color(21,
                rgb_to_ncurses(g_theme.tool_rgb.r),
                rgb_to_ncurses(g_theme.tool_rgb.g),
                rgb_to_ncurses(g_theme.tool_rgb.b));

            // Search highlight color (magenta/color5 from theme)
            init_color(22,
                rgb_to_ncurses(g_theme.search_rgb.r),
                rgb_to_ncurses(g_theme.search_rgb.g),
                rgb_to_ncurses(g_theme.search_rgb.b));

            // Input background color (very subtle blue tint, transparent)
            init_color(23, 40, 50, 80);  // ~5-8% with subtle blue tint

            // Input border color (use user/green color)
            init_color(24,
                rgb_to_ncurses(g_theme.user_rgb.r),
                rgb_to_ncurses(g_theme.user_rgb.g),
                rgb_to_ncurses(g_theme.user_rgb.b));

            // Initialize color pairs with custom colors
            init_pair(NCURSES_PAIR_FOREGROUND, 16, -1);  // -1 = default background
            init_pair(NCURSES_PAIR_USER, 17, -1);
            init_pair(NCURSES_PAIR_ASSISTANT, 18, -1);
            init_pair(NCURSES_PAIR_STATUS, 19, -1);
            init_pair(NCURSES_PAIR_ERROR, 20, -1);
            // Use dedicated tool color pair (distinct from assistant)
            // Unify tool color with status to reduce color variance
            init_pair(NCURSES_PAIR_TOOL, 19, -1);
            init_pair(NCURSES_PAIR_PROMPT, 17, -1);  // Use USER color for prompt
            // TODO color pairs
            init_pair(NCURSES_PAIR_TODO_COMPLETED, 17, -1);    // Green (same as USER)
            init_pair(NCURSES_PAIR_TODO_IN_PROGRESS, 19, -1);  // Yellow (same as STATUS)
            init_pair(NCURSES_PAIR_TODO_PENDING, 18, -1);      // Cyan (same as ASSISTANT)
            init_pair(NCURSES_PAIR_SEARCH, 22, -1);            // Search highlight (color5 from theme)
            init_pair(NCURSES_PAIR_INPUT_BG, 16, 23);          // Foreground on subtle background
            init_pair(NCURSES_PAIR_INPUT_BORDER, 24, -1);      // Border/accent color

            LOG_DEBUG("[TUI] Custom colors initialized with truecolor support");
        } else if (supports_256) {
            // Map theme colors to nearest 256-color palette indices
            int fg_idx = rgb_to_256_index(g_theme.foreground_rgb);
            int user_idx = rgb_to_256_index(g_theme.user_rgb);
            int assistant_idx = rgb_to_256_index(g_theme.assistant_rgb);
            int status_idx = rgb_to_256_index(g_theme.status_rgb);
            int error_idx = rgb_to_256_index(g_theme.error_rgb);
            int search_idx = rgb_to_256_index(g_theme.search_rgb);

            init_pair(NCURSES_PAIR_FOREGROUND, (short)fg_idx, (short)-1);
            init_pair(NCURSES_PAIR_USER, (short)user_idx, (short)-1);
            init_pair(NCURSES_PAIR_ASSISTANT, (short)assistant_idx, (short)-1);
            init_pair(NCURSES_PAIR_STATUS, (short)status_idx, (short)-1);
            init_pair(NCURSES_PAIR_ERROR, (short)error_idx, (short)-1);
            // Use status color for tool tag to reduce color variance
            init_pair(NCURSES_PAIR_TOOL, (short)status_idx, (short)-1);
            init_pair(NCURSES_PAIR_PROMPT, (short)user_idx, (short)-1);
            // TODO color pairs
            init_pair(NCURSES_PAIR_TODO_COMPLETED, (short)user_idx, (short)-1);
            init_pair(NCURSES_PAIR_TODO_IN_PROGRESS, (short)status_idx, (short)-1);
            init_pair(NCURSES_PAIR_TODO_PENDING, (short)assistant_idx, (short)-1);
            init_pair(NCURSES_PAIR_SEARCH, (short)search_idx, (short)-1);  // Search highlight (color5 from theme)
            init_pair(NCURSES_PAIR_INPUT_BG, (short)fg_idx, (short)236);   // Foreground on dark gray (236 in 256 palette)
            init_pair(NCURSES_PAIR_INPUT_BORDER, (short)user_idx, (short)-1);  // Border color (user/green)

            LOG_DEBUG("[TUI] Custom colors initialized using 256-color palette (no direct color change support)");
        } else {
            LOG_DEBUG("[TUI] Terminal does not support color changes or 256 colors, using standard colors");
            // Fall back to standard ncurses colors
            init_pair(NCURSES_PAIR_FOREGROUND, COLOR_WHITE, -1);
            init_pair(NCURSES_PAIR_USER, COLOR_GREEN, -1);
            init_pair(NCURSES_PAIR_ASSISTANT, COLOR_CYAN, -1);
            init_pair(NCURSES_PAIR_STATUS, COLOR_YELLOW, -1);
            init_pair(NCURSES_PAIR_ERROR, COLOR_RED, -1);
            // Use magenta for tool tag (distinct from assistant cyan)
            // Unify tool color with status color
            init_pair(NCURSES_PAIR_TOOL, COLOR_YELLOW, -1);
            init_pair(NCURSES_PAIR_PROMPT, COLOR_GREEN, -1);
            // TODO color pairs
            init_pair(NCURSES_PAIR_TODO_COMPLETED, COLOR_GREEN, -1);
            init_pair(NCURSES_PAIR_TODO_IN_PROGRESS, COLOR_YELLOW, -1);
            init_pair(NCURSES_PAIR_TODO_PENDING, COLOR_CYAN, -1);
            init_pair(NCURSES_PAIR_SEARCH, COLOR_MAGENTA, -1);  // Fallback: magenta for search highlights
            init_pair(NCURSES_PAIR_INPUT_BG, COLOR_WHITE, COLOR_BLACK);  // Fallback: white on black
            init_pair(NCURSES_PAIR_INPUT_BORDER, COLOR_GREEN, -1);  // Fallback: green border (user color)
        }
    } else {
        LOG_DEBUG("[TUI] No theme loaded, using standard ncurses colors");
        // Use standard ncurses color constants
        init_pair(NCURSES_PAIR_FOREGROUND, COLOR_WHITE, -1);
        init_pair(NCURSES_PAIR_USER, COLOR_GREEN, -1);
        init_pair(NCURSES_PAIR_ASSISTANT, COLOR_CYAN, -1);
        init_pair(NCURSES_PAIR_STATUS, COLOR_YELLOW, -1);
        init_pair(NCURSES_PAIR_ERROR, COLOR_RED, -1);
        init_pair(NCURSES_PAIR_PROMPT, COLOR_GREEN, -1);
        // Ensure tool pair is initialized; use magenta for distinction
        // Unify tool color with status color
        init_pair(NCURSES_PAIR_TOOL, COLOR_YELLOW, -1);
        // TODO color pairs
        init_pair(NCURSES_PAIR_TODO_COMPLETED, COLOR_GREEN, -1);
        init_pair(NCURSES_PAIR_TODO_IN_PROGRESS, COLOR_YELLOW, -1);
        init_pair(NCURSES_PAIR_TODO_PENDING, COLOR_CYAN, -1);
        init_pair(NCURSES_PAIR_SEARCH, COLOR_MAGENTA, -1);  // Fallback: magenta for search highlights
        init_pair(NCURSES_PAIR_INPUT_BG, COLOR_WHITE, COLOR_BLACK);  // Fallback: white on black
        init_pair(NCURSES_PAIR_INPUT_BORDER, COLOR_GREEN, -1);  // Fallback: green border (user color)
    }
}

// Clear the resize flag (called after handling resize)
/*
static void tui_clear_resize_flag(void) {
    g_resize_flag = 0;
}
*/

// Message type categories for spacing logic
typedef enum {
    MSG_TYPE_UNKNOWN = 0,
    MSG_TYPE_USER,
    MSG_TYPE_ASSISTANT,
    MSG_TYPE_TOOL,
    MSG_TYPE_SYSTEM,
    MSG_TYPE_EMPTY
} MessageType;

// Helper: Classify message type from prefix
static MessageType get_message_type(const char *prefix) {
    if (!prefix || prefix[0] == '\0') {
        return MSG_TYPE_EMPTY;
    }

    if (strcmp(prefix, "[User]") == 0) {
        return MSG_TYPE_USER;
    }
    if (strcmp(prefix, "[Assistant]") == 0) {
        return MSG_TYPE_ASSISTANT;
    }
    if (strcmp(prefix, "[System]") == 0 || strcmp(prefix, "[Error]") == 0 ||
        strcmp(prefix, "[Transcription]") == 0) {
        return MSG_TYPE_SYSTEM;
    }
    // Check for tools - must come after checking specific system prefixes
    // Matches "[Tool: ...]" or any tool name in brackets like "[Bash]", "[Read]", etc.
    if (prefix[0] == '[') {
        return MSG_TYPE_TOOL;
    }

    return MSG_TYPE_UNKNOWN;
}

// Helper: Add a conversation entry to the TUI state
static int add_conversation_entry(TUIState *tui, const char *prefix, const char *text, TUIColorPair color_pair) {
    if (!tui) return -1;

    // Ensure capacity
    if (tui->entries_count >= tui->entries_capacity) {
        int new_capacity = tui->entries_capacity == 0 ? INITIAL_CONV_CAPACITY : tui->entries_capacity * 2;
        void *entries_ptr = (void *)tui->entries;
        size_t capacity = (size_t)tui->entries_capacity;
        if (array_ensure_capacity(&entries_ptr, &capacity, (size_t)new_capacity,
                                  sizeof(ConversationEntry), NULL) != 0) {
            LOG_ERROR("[TUI] Failed to allocate memory for conversation entries");
            return -1;
        }
        tui->entries = (ConversationEntry *)entries_ptr;
        tui->entries_capacity = (int)capacity;
    }

    // Allocate and copy strings
    ConversationEntry *entry = &tui->entries[tui->entries_count];
    entry->prefix = prefix ? strdup(prefix) : NULL;
    entry->text = text ? strdup(text) : NULL;
    entry->color_pair = color_pair;

    if ((prefix && !entry->prefix) || (text && !entry->text)) {
        free(entry->prefix);
        free(entry->text);
        LOG_ERROR("[TUI] Failed to allocate memory for conversation entry strings");
        return -1;
    }

    tui->entries_count++;
    return 0;
}

// Helper: Free all conversation entries
static void free_conversation_entries(TUIState *tui) {
    if (!tui || !tui->entries) return;

    for (int i = 0; i < tui->entries_count; i++) {
        free(tui->entries[i].prefix);
        free(tui->entries[i].text);
    }
    free(tui->entries);
    tui->entries = NULL;
    tui->entries_count = 0;
    tui->entries_capacity = 0;
}

// Expand pad capacity if needed
// Pad capacity growth is centralized in WindowManager now.

// Helper: Refresh conversation window viewport (using pad)
static void refresh_conversation_viewport(TUIState *tui) {
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
static int render_entry_to_pad(TUIState *tui, const char *prefix, const char *text, TUIColorPair color_pair) {
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

    // Write prefix if present
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

    // Write text
    if (text && text[0] != '\0') {
        int text_pair = (prefix && prefix[0] != '\0') ? NCURSES_PAIR_FOREGROUND : mapped_pair;
        if (has_colors()) {
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
    }

    // Add newline
    waddch(tui->wm.conv_pad, '\n');

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
static int utf8_char_length(unsigned char first_byte) {
    if ((first_byte & 0x80) == 0) return 1;  // 0xxxxxxx
    if ((first_byte & 0xE0) == 0xC0) return 2;  // 110xxxxx
    if ((first_byte & 0xF0) == 0xE0) return 3;  // 1110xxxx
    if ((first_byte & 0xF8) == 0xF0) return 4;  // 11110xxx
    return 1;  // Invalid, treat as single byte
}

// Not used in current implementation, but kept for potential future UTF-8 handling
// static int is_utf8_continuation(unsigned char byte) {
//     return (byte & 0xC0) == 0x80;
// }

static int is_word_boundary(char c) {
    return !isalnum(c) && c != '_';
}

static int move_backward_word(const char *buffer, int cursor_pos) {
    if (cursor_pos <= 0) return 0;
    int pos = cursor_pos - 1;
    while (pos > 0 && is_word_boundary(buffer[pos])) pos--;
    while (pos > 0 && !is_word_boundary(buffer[pos])) pos--;
    if (pos > 0 && is_word_boundary(buffer[pos])) pos++;
    return pos;
}

static int move_forward_word(const char *buffer, int cursor_pos, int buffer_len) {
    if (cursor_pos >= buffer_len) return buffer_len;
    int pos = cursor_pos;
    while (pos < buffer_len && !is_word_boundary(buffer[pos])) pos++;
    while (pos < buffer_len && is_word_boundary(buffer[pos])) pos++;
    return pos;
}

// Input buffer management
struct TUIInputBuffer {
    char *buffer;
    size_t capacity;
    int length;
    int cursor;
    WINDOW *win;
    int win_width;
    int win_height;
    // Display state
    int view_offset;         // Horizontal scroll offset for long lines
    int line_scroll_offset;  // Vertical scroll offset (which line to show at top)
    // Paste mode detection
    int paste_mode;          // 1 when in bracketed paste, 0 otherwise
    struct timespec last_input_time;  // Track timing for paste detection
    int rapid_input_count;   // Count of rapid inputs (heuristic for paste)
    // Paste content tracking
    char *paste_content;     // Actual pasted content (kept separate from visible buffer)
    size_t paste_capacity;   // Capacity of paste buffer
    size_t paste_content_len; // Length of pasted content
    int paste_start_pos;     // Position where paste started in buffer
    int paste_placeholder_len; // Length of placeholder in buffer
};

// Calculate how many visual lines are needed for the current buffer
// Note: This assumes first line includes the prompt
static int calculate_needed_lines(const char *buffer, int buffer_len, int win_width, int prompt_len) {
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

// Resize input window dynamically (called from redraw)
static int resize_input_window(TUIState *tui, int desired_lines) {
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

// Initialize input buffer
static int input_init(TUIState *tui) {
    if (!tui || !tui->wm.input_win) {
        return -1;
    }

    TUIInputBuffer *input = calloc(1, sizeof(TUIInputBuffer));
    if (!input) {
        return -1;
    }

    input->buffer = malloc(INPUT_BUFFER_SIZE);
    if (!input->buffer) {
        free(input);
        return -1;
    }

    input->capacity = INPUT_BUFFER_SIZE;
    input->buffer[0] = '\0';
    input->length = 0;
    input->cursor = 0;
    input->win = tui->wm.input_win;
    input->view_offset = 0;
    input->line_scroll_offset = 0;
    input->paste_mode = 0;
    input->rapid_input_count = 0;
    clock_gettime(CLOCK_MONOTONIC, &input->last_input_time);

    // Initialize paste tracking
    input->paste_content = NULL;
    input->paste_capacity = 0;
    input->paste_content_len = 0;
    input->paste_start_pos = 0;
    input->paste_placeholder_len = 0;

    // Get window dimensions
    int h, w;
    getmaxyx(tui->wm.input_win, h, w);
    input->win_width = w;  // No borders
    input->win_height = h;

    tui->input_buffer = input;
    return 0;
}

// Free input buffer
static void input_free(TUIState *tui) {
    if (!tui || !tui->input_buffer) {
        return;
    }

    free(tui->input_buffer->buffer);
    tui->input_buffer->buffer = NULL;
    tui->input_buffer->capacity = 0;
    tui->input_buffer->length = 0;
    tui->input_buffer->cursor = 0;

    free(tui->input_buffer->paste_content);
    tui->input_buffer->paste_content = NULL;
    tui->input_buffer->paste_capacity = 0;
    tui->input_buffer->paste_content_len = 0;

    free(tui->input_buffer);
    tui->input_buffer = NULL;
}

// Insert character(s) at cursor position
static int input_insert_char(TUIInputBuffer *input, const unsigned char *utf8_char, int char_bytes) {
    if (!input) {
        return -1;
    }

    // If in paste mode, accumulate in paste buffer
    if (input->paste_mode && input->paste_content) {
        // Expand paste buffer if needed
        if (input->paste_content_len + (size_t)char_bytes >= input->paste_capacity) {
            size_t new_capacity = input->paste_capacity * 2;
            void *paste_ptr = (void *)input->paste_content;
            if (buffer_reserve(&paste_ptr, &input->paste_capacity, new_capacity) != 0) {
                LOG_ERROR("[TUI] Failed to expand paste buffer");
                return -1;
            }
            input->paste_content = (char *)paste_ptr;
        }

        // Append to paste buffer
        for (int i = 0; i < char_bytes; i++) {
            input->paste_content[input->paste_content_len++] = (char)utf8_char[i];
        }

        // Don't insert into visible buffer during paste - we'll add placeholder at end
        return 0;
    }

    if (input->length + char_bytes >= (int)input->capacity - 1) {
        return -1;  // Buffer full
    }

    // Make space for the new character(s)
    memmove(&input->buffer[input->cursor + char_bytes],
            &input->buffer[input->cursor],
            (size_t)(input->length - input->cursor + 1));

    // Copy the character bytes
    for (int i = 0; i < char_bytes; i++) {
        input->buffer[input->cursor + i] = (char)utf8_char[i];
    }

    input->length += char_bytes;
    input->cursor += char_bytes;
    return 0;
}

// Insert string at cursor position
static int input_insert_string(TUIInputBuffer *input, const char *str) {
    if (!input || !str || !input->buffer) {
        return -1;
    }

    size_t str_len = strlen(str);
    if (str_len == 0) {
        return 0;  // Nothing to insert
    }

    // Check if we have enough space, resize if needed
    if ((size_t)(input->length + (int)str_len) >= input->capacity - 1) {
        // Calculate new capacity
        size_t new_capacity;
        if (__builtin_add_overflow((size_t)input->length, str_len, &new_capacity) ||
            __builtin_add_overflow(new_capacity, (size_t)1024, &new_capacity)) {
            return -1;  // Overflow
        }

        void *buf_ptr = (void *)input->buffer;
        if (buffer_reserve(&buf_ptr, &input->capacity, new_capacity) != 0) {
            return -1;  // Resize failed
        }
        input->buffer = (char *)buf_ptr;
    }

    // Make space for the new string
    memmove(&input->buffer[(size_t)input->cursor + str_len],
            &input->buffer[input->cursor],
            (size_t)(input->length - input->cursor + 1));

    // Copy the string
    memcpy(&input->buffer[input->cursor], str, str_len);

    input->length += (int)str_len;
    input->cursor += (int)str_len;
    return 0;
}

// Delete character at cursor position (forward delete)
static int input_delete_char(TUIInputBuffer *input) {
    if (!input || input->cursor >= input->length) {
        return 0;  // Nothing to delete
    }

    // Find the length of the UTF-8 character at cursor
    int char_len = utf8_char_length((unsigned char)input->buffer[input->cursor]);

    // Delete the character by moving subsequent text left
    memmove(&input->buffer[input->cursor],
            &input->buffer[input->cursor + char_len],
            (size_t)(input->length - input->cursor - char_len + 1));

    input->length -= char_len;
    return char_len;
}

// Delete character before cursor (backspace)
static int input_backspace(TUIInputBuffer *input) {
    if (!input || input->cursor <= 0) {
        return 0;  // Nothing to delete
    }

    memmove(&input->buffer[input->cursor - 1],
            &input->buffer[input->cursor],
            (size_t)(input->length - input->cursor + 1));
    input->length--;
    input->cursor--;
    return 1;
}

// Delete word before cursor (Alt+Backspace)
static int input_delete_word_backward(TUIInputBuffer *input) {
    if (!input || input->cursor <= 0) {
        return 0;
    }

    int word_start = input->cursor - 1;
    while (word_start > 0 && is_word_boundary(input->buffer[word_start])) {
        word_start--;
    }
    while (word_start > 0 && !is_word_boundary(input->buffer[word_start])) {
        word_start--;
    }
    if (word_start > 0 && is_word_boundary(input->buffer[word_start])) {
        word_start++;
    }

    int delete_count = input->cursor - word_start;
    if (delete_count > 0) {
        memmove(&input->buffer[word_start],
                &input->buffer[input->cursor],
                (size_t)(input->length - input->cursor + 1));
        input->length -= delete_count;
        input->cursor = word_start;
    }

    return delete_count;
}

// Delete word forward (Alt+d)
static int input_delete_word_forward(TUIInputBuffer *input) {
    if (!input || input->cursor >= input->length) {
        return 0;
    }

    int word_end = move_forward_word(input->buffer, input->cursor, input->length);
    int delete_count = word_end - input->cursor;

    if (delete_count > 0) {
        memmove(&input->buffer[input->cursor],
                &input->buffer[word_end],
                (size_t)(input->length - word_end + 1));
        input->length -= delete_count;
    }

    return delete_count;
}

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

static void input_redraw(TUIState *tui, const char *prompt) {
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
    int needed_lines = calculate_needed_lines(input->buffer, input->length,
                                              content_width, effective_prefix_len);

    // Request window resize (this will be a no-op if size hasn't changed)
    // For BORDER style, we need extra height for top and bottom borders
    int window_height_needed = needed_lines;
    if (tui->input_box_style == INPUT_STYLE_BORDER) {
        window_height_needed += 2;  // +2 for top and bottom borders
    }
    resize_input_window(tui, window_height_needed);
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
    int content_start_row = (tui->input_box_style == INPUT_STYLE_BORDER) ? 1 : 0;
    int border_height_offset = (tui->input_box_style == INPUT_STYLE_BORDER) ? 2 : 0;
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

int tui_init(TUIState *tui, ConversationState *state) {
    if (!tui) return -1;

    // Store global pointer for input resize callback
    // Set locale for UTF-8 support
    setlocale(LC_ALL, "");

    // Initialize ncurses
    initscr();

    // Set ESC delay to 25ms for responsive ESC/Ctrl+[ mode switching
    // Default is 1000ms which feels sluggish. 25ms is enough to detect
    // escape sequences (arrow keys, etc.) while feeling instant to users.
    set_escdelay(25);

    // Use raw mode so Ctrl+C is delivered as a key (ASCII 3)
    raw();     // Disable line buffering and signal generation (incl. SIGINT)
    noecho();  // Don't echo input
    nonl();    // Don't translate Enter to newline (allows distinguishing Enter from Ctrl+J)
    keypad(stdscr, TRUE);  // Enable function keys
    nodelay(stdscr, FALSE);  // Blocking input
    curs_set(2);  // Make cursor very visible (block cursor)

    // Enable bracketed paste mode (allows detecting pasted content)
    // ESC[?2004h enables, ESC[?2004l disables
    printf("\033[?2004h");
    fflush(stdout);

    // Configure paste heuristic (default: enabled). Only override when env provided
    const char *ph = getenv("TUI_PASTE_HEURISTIC");
    if (ph) {
        if ((strcmp(ph, "1") == 0 || strcasecmp(ph, "true") == 0 || strcasecmp(ph, "on") == 0)) {
            g_enable_paste_heuristic = 1;
        } else if ((strcmp(ph, "0") == 0 || strcasecmp(ph, "false") == 0 || strcasecmp(ph, "off") == 0)) {
            g_enable_paste_heuristic = 0;
        }
    }

    // Optional tuning for heuristic thresholds
    const char *gap = getenv("TUI_PASTE_GAP_MS");
    if (gap) {
        long v = strtol(gap, NULL, 10);
        if (v >= 1 && v <= 100) g_paste_gap_ms = (int)v;
    }
    const char *burst = getenv("TUI_PASTE_BURST_MIN");
    if (burst) {
        long v = strtol(burst, NULL, 10);
        if (v >= 1 && v <= 10000) g_paste_burst_min = (int)v;
    }
    const char *pto = getenv("TUI_PASTE_TIMEOUT_MS");
    if (pto) {
        long v = strtol(pto, NULL, 10);
        if (v >= 100 && v <= 10000) g_paste_timeout_ms = (int)v;
    }

    // Initialize colors from colorscheme
    init_ncurses_colors();

    // Get screen dimensions to calculate max input height
    int screen_height, screen_width;
    getmaxyx(stdscr, screen_height, screen_width);
    (void)screen_width;  // Unused

    // Calculate max input height as 20% of screen height
    // Formula: max_height = (screen_height * percentage / 100)
    // Minimum of INPUT_WIN_MIN_HEIGHT to ensure at least some content lines
    int calculated_max_height = (screen_height * INPUT_WIN_MAX_HEIGHT_PERCENT) / 100;
    if (calculated_max_height < INPUT_WIN_MIN_HEIGHT) {
        calculated_max_height = INPUT_WIN_MIN_HEIGHT;
    }

    // Initialize WindowManager (owner of ncurses windows)
    WindowManagerConfig cfg = DEFAULT_WINDOW_CONFIG;
    cfg.min_conv_height = 5;
    cfg.min_input_height = INPUT_WIN_MIN_HEIGHT;
    cfg.max_input_height = calculated_max_height;
    cfg.status_height = STATUS_WIN_HEIGHT;
    cfg.padding = CONV_WIN_PADDING;

    if (window_manager_init(&tui->wm, &cfg) != 0) {
        endwin();
        return -1;
    }
    // Start with zero content lines
    window_manager_set_content_lines(&tui->wm, 0);

    // Store conversation state reference
    tui->conversation_state = state;

    // Initialize conversation entries
    tui->entries = NULL;
    tui->entries_count = 0;
    tui->entries_capacity = 0;
    tui->status_message = NULL;
    tui->status_visible = 0;
    tui->status_spinner_active = 0;
    tui->status_spinner_frame = 0;
    tui->status_spinner_last_update_ns = 0;

    // Initialize mode (start in INSERT mode for immediate input)
    tui->mode = TUI_MODE_INSERT;
    tui->input_box_style = INPUT_STYLE_BACKGROUND;  // Default to background style
    tui->normal_mode_last_key = 0;

    // Initialize command mode buffer
    tui->command_buffer = NULL;
    tui->command_buffer_len = 0;
    tui->command_buffer_capacity = 0;

    // Initialize search state
    tui->search_buffer = NULL;
    tui->search_buffer_len = 0;
    tui->search_buffer_capacity = 0;
    tui->search_direction = 1;  // Default forward search
    tui->last_search_match_line = -1;
    tui->last_search_pattern = NULL;

    // Initialize input buffer
    if (input_init(tui) != 0) {
        window_manager_destroy(&tui->wm);
        endwin();
        return -1;
    }

    // Initialize input history (persistent)
    tui->input_history = NULL;
    tui->input_history_count = 0;
    tui->input_history_capacity = 0;
    tui->input_history_index = -1;
    tui->input_saved_before_history = NULL;

    // Initialize file search
    if (file_search_init(&tui->file_search) != 0) {
        LOG_WARN("[TUI] Failed to initialize file search");
        // Non-fatal - continue without file search
    }
    // Initialize history search
    if (history_search_init(&tui->history_search) != 0) {
        LOG_WARN("[TUI] Failed to initialize history search");
        // Non-fatal - continue without history search
    } else {
        LOG_DEBUG("[TUI] History search initialized successfully");
    }
    tui->history_file = history_file_open(NULL);
    if (tui->history_file) {
        int limit = 100;  // default history size in memory
        const char *env_limit = getenv("KLAWED_HISTORY_MAX");
        if (env_limit && *env_limit) {
            long v = strtol(env_limit, NULL, 10);
            if (v > 0 && v < 100000) limit = (int)v;
        }
        int loaded = 0;
        char **entries = history_file_load_recent(tui->history_file, limit, &loaded);
        if (entries && loaded > 0) {
            tui->input_history = entries;
            tui->input_history_count = loaded;
            tui->input_history_capacity = loaded;
        }
    }

    // Register resize handler (if available)
#ifdef SIGWINCH
    signal(SIGWINCH, handle_resize);
#endif

    // Initialize vim-fugitive availability tracking
    tui->vim_fugitive_available = -1;  // Unknown state
    tui->vim_fugitive_mutex_initialized = 0;
    if (pthread_mutex_init(&tui->vim_fugitive_mutex, NULL) == 0) {
        tui->vim_fugitive_mutex_initialized = 1;
    } else {
        LOG_WARN("[TUI] Failed to initialize vim-fugitive mutex");
    }

    tui->is_initialized = 1;

    LOG_DEBUG("[TUI] Initialized (screen=%dx%d, conv_h=%d, status_h=%d, input_h=%d)",
              tui->wm.screen_width, tui->wm.screen_height, tui->wm.conv_viewport_height,
              tui->wm.status_height, tui->wm.input_height);

    // Validate initial window setup
    validate_tui_windows(tui);

    if (tui->wm.status_height > 0) {
        render_status_window(tui);
    }

    refresh();

    // Start background check for vim-fugitive availability
    tui_start_vim_fugitive_check(tui);

    return 0;
}

void tui_cleanup(TUIState *tui) {
    if (!tui || !tui->is_initialized) return;

    // Free conversation entries
    free_conversation_entries(tui);

    // Free input state
    input_free(tui);

    // Free status message
    free(tui->status_message);
    tui->status_message = NULL;

    // Free command buffer
    free(tui->command_buffer);
    tui->command_buffer = NULL;

    // Free search state
    free(tui->search_buffer);
    tui->search_buffer = NULL;
    free(tui->last_search_pattern);
    tui->last_search_pattern = NULL;

    // Free file search state
    file_search_free(&tui->file_search);
    // Free history search state
    history_search_free(&tui->history_search);
    // Destroy ncurses windows via window manager
    window_manager_destroy(&tui->wm);

    // Disable bracketed paste mode
    printf("\033[?2004l");
    fflush(stdout);

    // End ncurses
    endwin();

    tui->is_initialized = 0;

    // Print a newline to ensure clean exit
    printf("\n");
    LOG_DEBUG("[TUI] Cleaned up ncurses resources");
    fflush(stdout);

    // Clean up vim-fugitive mutex
    if (tui->vim_fugitive_mutex_initialized) {
        pthread_mutex_destroy(&tui->vim_fugitive_mutex);
        tui->vim_fugitive_mutex_initialized = 0;
    }

    // Free input history
    if (tui->input_history) {
        for (int i = 0; i < tui->input_history_count; i++) {
            free(tui->input_history[i]);
        }
        free(tui->input_history);
        tui->input_history = NULL;
        tui->input_history_count = 0;
        tui->input_history_capacity = 0;
    }
    free(tui->input_saved_before_history);
    tui->input_saved_before_history = NULL;

    // Close history DB
    if (tui->history_file) {
        history_file_close(tui->history_file);
        tui->history_file = NULL;
    }
}

int tui_suspend(TUIState *tui) {
    if (!tui || !tui->is_initialized) {
        return -1;
    }

    if (tui->terminal_suspended) {
        LOG_DEBUG("[TUI] Terminal already suspended");
        return 0;
    }

    LOG_DEBUG("[TUI] Suspending terminal for external command");

    // Save current terminal state
    def_prog_mode();

    // Disable bracketed paste mode
    printf("\033[?2004l");
    fflush(stdout);

    // End ncurses mode (restores terminal to normal state)
    endwin();

    tui->terminal_suspended = 1;
    return 0;
}

int tui_resume(TUIState *tui) {
    if (!tui || !tui->is_initialized) {
        return -1;
    }

    if (!tui->terminal_suspended) {
        LOG_DEBUG("[TUI] Terminal not suspended");
        return 0;
    }

    LOG_DEBUG("[TUI] Resuming terminal after external command");

    // Restore ncurses mode
    reset_prog_mode();
    refresh();

    // Re-enable bracketed paste mode
    printf("\033[?2004h");
    fflush(stdout);

    // Redraw the TUI
    tui_refresh(tui);

    tui->terminal_suspended = 0;
    return 0;
}

// Thread function to check vim-fugitive availability in background
static void* check_vim_fugitive_thread(void *arg) {
    TUIState *tui = (TUIState *)arg;
    if (!tui) return NULL;

    LOG_DEBUG("[TUI] Background thread checking vim-fugitive availability");

    // Check if vim-fugitive is available by running vim with a test command
    char test_cmd[512];
    snprintf(test_cmd, sizeof(test_cmd),
             "vim -c \"if exists(':Git') | q | else | cquit 1 | endif\" -c \"q\" 2>&1");

    FILE *fp = popen(test_cmd, "r");
    if (!fp) {
        LOG_WARN("[TUI] Failed to check vim-fugitive availability in background thread");
        return NULL;
    }

    char buffer[256];
    // Read output to check for errors
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Just consume output
    }

    int rc = pclose(fp);
    // vim returns 0 if fugitive exists (Git command exists), non-zero otherwise

    int available = (rc == 0) ? 1 : 0;

    // Update the cached value with thread-safe mutex
    if (tui->vim_fugitive_mutex_initialized) {
        pthread_mutex_lock(&tui->vim_fugitive_mutex);
        tui->vim_fugitive_available = available;
        pthread_mutex_unlock(&tui->vim_fugitive_mutex);

        LOG_DEBUG("[TUI] Background check complete: vim-fugitive %s",
                  available ? "available" : "not available");
    } else {
        LOG_WARN("[TUI] Cannot update vim-fugitive availability - mutex not initialized");
    }

    return NULL;
}

void tui_start_vim_fugitive_check(TUIState *tui) {
    if (!tui) return;

    // Only start check if we haven't checked yet
    if (tui->vim_fugitive_mutex_initialized) {
        pthread_mutex_lock(&tui->vim_fugitive_mutex);
        int current = tui->vim_fugitive_available;
        pthread_mutex_unlock(&tui->vim_fugitive_mutex);

        if (current != -1) {
            LOG_DEBUG("[TUI] vim-fugitive availability already checked: %d", current);
            return;
        }
    }

    // Start background thread
    pthread_t thread;
    if (pthread_create(&thread, NULL, check_vim_fugitive_thread, tui) != 0) {
        LOG_WARN("[TUI] Failed to create background thread for vim-fugitive check");
        return;
    }

    // Detach thread so it cleans up automatically
    pthread_detach(thread);

    LOG_DEBUG("[TUI] Started background thread to check vim-fugitive availability");
}

int tui_get_vim_fugitive_available(TUIState *tui) {
    if (!tui) return -1;

    if (!tui->vim_fugitive_mutex_initialized) {
        return -1;
    }

    pthread_mutex_lock(&tui->vim_fugitive_mutex);
    int result = tui->vim_fugitive_available;
    pthread_mutex_unlock(&tui->vim_fugitive_mutex);

    return result;
}

void tui_add_conversation_line(TUIState *tui, const char *prefix, const char *text, TUIColorPair color_pair) {
    if (!tui || !tui->is_initialized) return;

    // Validate conversation pad exists (critical - prevent segfault)
    if (!tui->wm.conv_pad) {
        LOG_ERROR("[TUI] Cannot add conversation line - conv_pad is NULL");
        return;
    }

    // IMPORTANT: Capture "at bottom" state BEFORE adding new content
    // This is needed because after content is added, max_scroll increases
    // and the scroll_offset (which was at bottom) will appear to be less than max_scroll
    int was_at_bottom = 0;
    if (tui->mode == TUI_MODE_NORMAL || tui->mode == TUI_MODE_COMMAND) {
        int scroll_offset = window_manager_get_scroll_offset(&tui->wm);
        int max_scroll = window_manager_get_max_scroll(&tui->wm);
        int content_lines = window_manager_get_content_lines(&tui->wm);

        if (content_lines == 0 || max_scroll <= 0) {
            // No content or everything fits in viewport
            was_at_bottom = 1;
        } else if (scroll_offset >= max_scroll - 1) {
            // Already at bottom (with 1-line tolerance for 98-100% range)
            was_at_bottom = 1;
        }
        LOG_DEBUG("[TUI] Pre-add scroll state: scroll_offset=%d, max_scroll=%d, was_at_bottom=%d",
                  scroll_offset, max_scroll, was_at_bottom);
    }

    // Check if we need to add spacing between different message types
    // Look at the most recent non-empty entry to determine if spacing is needed
    MessageType current_type = get_message_type(prefix);
    MessageType previous_type = MSG_TYPE_UNKNOWN;

    // Find the most recent non-empty entry
    for (int i = tui->entries_count - 1; i >= 0; i--) {
        MessageType entry_type = get_message_type(tui->entries[i].prefix);
        if (entry_type != MSG_TYPE_EMPTY) {
            previous_type = entry_type;
            break;
        }
    }

    // Add blank line if transitioning between different message types
    // (but not for empty lines or unknown types, and not if previous was empty/unknown)
    int should_add_spacing = 0;
    if (current_type != MSG_TYPE_EMPTY && current_type != MSG_TYPE_UNKNOWN &&
        previous_type != MSG_TYPE_EMPTY && previous_type != MSG_TYPE_UNKNOWN &&
        current_type != previous_type) {
        should_add_spacing = 1;
    }

    // Get pad dimensions for capacity estimation
    int pad_height, pad_width;
    getmaxyx(tui->wm.conv_pad, pad_height, pad_width);
    (void)pad_height;

    // Calculate how many lines the entries will take when wrapped
    int prefix_len = (prefix && prefix[0] != '\0') ? (int)strlen(prefix) + 1 : 0; // +1 for space
    int text_len = (text && text[0] != '\0') ? (int)strlen(text) : 0;

    // Estimate wrapped lines (conservative)
    int estimated_lines = 1; // At least 1 line for the entry
    if (should_add_spacing) {
        estimated_lines += 1; // Add one for the spacing line
    }

    if (text_len > 0) {
        // Count newlines in text (each newline is a line break)
        int newline_count = 0;
        for (int i = 0; i < text_len; i++) {
            if (text[i] == '\n') {
                newline_count++;
            }
        }

        // Each newline in text is definitely a line break
        // Text without newlines might wrap
        // Be conservative: assume worst-case wrapping
        estimated_lines += newline_count + ((prefix_len + text_len) / (pad_width / 2)) + 5;
    }

    // Ensure pad has enough capacity (centralized via WindowManager)
    int current_lines = window_manager_get_content_lines(&tui->wm);
    // Check for integer overflow before calculating needed capacity
    int needed_capacity;
    if (current_lines > INT_MAX - estimated_lines ||
        current_lines + estimated_lines > INT_MAX - 500) {
        LOG_ERROR("[TUI] Capacity calculation would overflow! current=%d, estimated=%d",
                 current_lines, estimated_lines);
        needed_capacity = INT_MAX;
    } else {
        needed_capacity = current_lines + estimated_lines + 500; // Increased safety buffer
    }

    if (needed_capacity > tui->wm.conv_pad_capacity) {
        if (window_manager_ensure_pad_capacity(&tui->wm, needed_capacity) != 0) {
            LOG_ERROR("[TUI] Failed to ensure pad capacity via WindowManager");
        }
    }

    // Double-check pad exists before writing
    if (!tui->wm.conv_pad) {
        LOG_ERROR("[TUI] Cannot write to conversation - conv_pad is NULL");
        return;
    }

    // Insert blank line for spacing if needed
    if (should_add_spacing) {
        if (add_conversation_entry(tui, NULL, "", COLOR_PAIR_FOREGROUND) != 0) {
            LOG_ERROR("[TUI] Failed to add spacing entry");
            // Continue anyway - spacing is not critical
        } else {
            // Render the spacing line
            render_entry_to_pad(tui, NULL, "", COLOR_PAIR_FOREGROUND);
        }
    }

    // Add entry to conversation history
    if (add_conversation_entry(tui, prefix, text, color_pair) != 0) {
        LOG_ERROR("[TUI] Failed to add conversation entry");
        return;
    }

    // Render the actual entry
    int start_line = window_manager_get_content_lines(&tui->wm);
    if (render_entry_to_pad(tui, prefix, text, color_pair) != 0) {
        LOG_ERROR("[TUI] Failed to render entry to pad");
        return;
    }

    int cur_y = window_manager_get_content_lines(&tui->wm);
    LOG_DEBUG("[TUI] Added line, total_lines now %d (estimated %d, actual %d)",
              cur_y, estimated_lines, cur_y - start_line);

    // Auto-scroll logic:
    // - In INSERT mode: always auto-scroll
    // - In NORMAL/COMMAND mode: auto-scroll only if we WERE at 98-100% scroll height
    //   BEFORE content was added (using was_at_bottom captured earlier)
    if (tui->mode == TUI_MODE_INSERT) {
        window_manager_scroll_to_bottom(&tui->wm);
    } else if (tui->mode == TUI_MODE_NORMAL || tui->mode == TUI_MODE_COMMAND) {
        // Use the was_at_bottom state captured BEFORE content was added
        if (was_at_bottom) {
            window_manager_scroll_to_bottom(&tui->wm);
            LOG_DEBUG("[TUI] Auto-scroll: scrolling to bottom (was_at_bottom=1)");
        } else {
            LOG_DEBUG("[TUI] Auto-scroll: not scrolling (was_at_bottom=0)");
        }
    }
    window_manager_refresh_conversation(&tui->wm);

    if (tui->wm.status_height > 0) {
        render_status_window(tui);
    }

    // Redraw input window to ensure it stays visible
    if (tui->wm.input_win) {
        touchwin(tui->wm.input_win);
        wrefresh(tui->wm.input_win);
    }
}

void tui_update_last_conversation_line(TUIState *tui, const char *text) {
    if (!tui || !tui->is_initialized || !text) return;

    // Validate conversation pad exists
    if (!tui->wm.conv_pad) {
        LOG_ERROR("[TUI] Cannot update conversation line - conv_pad is NULL");
        return;
    }

    // IMPORTANT: Capture "at bottom" state BEFORE adding new content
    // This is needed because after content is added, max_scroll increases
    // and the scroll_offset (which was at bottom) will appear to be less than max_scroll
    int was_at_bottom = 0;
    if (tui->mode == TUI_MODE_NORMAL || tui->mode == TUI_MODE_COMMAND) {
        int scroll_offset = window_manager_get_scroll_offset(&tui->wm);
        int max_scroll = window_manager_get_max_scroll(&tui->wm);
        int content_lines = window_manager_get_content_lines(&tui->wm);

        if (content_lines == 0 || max_scroll <= 0) {
            // No content or everything fits in viewport
            was_at_bottom = 1;
        } else if (scroll_offset >= max_scroll - 1) {
            // Already at bottom (with 1-line tolerance for 98-100% range)
            was_at_bottom = 1;
        }
        LOG_DEBUG("[TUI] Pre-update scroll state: scroll_offset=%d, max_scroll=%d, was_at_bottom=%d",
                  scroll_offset, max_scroll, was_at_bottom);
    }

    // Update the last entry in the conversation history
    if (tui->entries_count > 0) {
        ConversationEntry *last_entry = &tui->entries[tui->entries_count - 1];

        // Append new text to the existing text
        size_t old_len = last_entry->text ? strlen(last_entry->text) : 0;
        size_t new_len = strlen(text);
        char *new_text = realloc(last_entry->text, old_len + new_len + 1);
        if (new_text) {
            if (old_len == 0) {
                new_text[0] = '\0';
            }
            strlcat(new_text, text, old_len + new_len + 1);
            last_entry->text = new_text;

            // Just append to the end of the pad (simple approach)
            // Get current cursor position
            int cur_y, cur_x;
            getyx(tui->wm.conv_pad, cur_y, cur_x);

            // If we're at the beginning of a line and there's a prefix,
            // we need to handle it differently
            if (cur_x == 0 && last_entry->prefix && last_entry->prefix[0] != '\0') {
                // We shouldn't get here in streaming mode
                LOG_WARN("[TUI] Streaming update but at start of line");
                return;
            }

            // Write the new text at current position
            waddstr(tui->wm.conv_pad, text);

            // Update content lines
            getyx(tui->wm.conv_pad, cur_y, cur_x);
            (void)cur_x;
            window_manager_set_content_lines(&tui->wm, cur_y);
        }
    } else {
        // No entries exist - create a new one
        add_conversation_entry(tui, "", text, COLOR_PAIR_ASSISTANT);
    }

    // Auto-scroll logic:
    // - In INSERT mode: always auto-scroll
    // - In NORMAL/COMMAND mode: auto-scroll only if we WERE at 98-100% scroll height
    //   BEFORE content was added (using was_at_bottom captured earlier)
    if (tui->mode == TUI_MODE_INSERT) {
        window_manager_scroll_to_bottom(&tui->wm);
    } else if (tui->mode == TUI_MODE_NORMAL || tui->mode == TUI_MODE_COMMAND) {
        // Use the was_at_bottom state captured BEFORE content was added
        if (was_at_bottom) {
            window_manager_scroll_to_bottom(&tui->wm);
            LOG_DEBUG("[TUI] Auto-scroll (update): scrolling to bottom (was_at_bottom=1)");
        } else {
            LOG_DEBUG("[TUI] Auto-scroll (update): not scrolling (was_at_bottom=0)");
        }
    }
    window_manager_refresh_conversation(&tui->wm);

    // Redraw input window
    if (tui->wm.input_win) {
        touchwin(tui->wm.input_win);
        wrefresh(tui->wm.input_win);
    }
}

void tui_render_todo_list(TUIState *tui, const TodoList *list) {
    if (!tui || !list || list->count == 0) {
        return;  // No todos to display
    }

    // Add header line
    tui_add_conversation_line(tui, "[Assistant]", "Here are the current tasks:", COLOR_PAIR_ASSISTANT);

    // Render each todo item with its status-specific color
    for (size_t i = 0; i < list->count; i++) {
        const TodoItem *item = &list->items[i];
        char line[1024];
        TUIColorPair color;
        const char *symbol;
        const char *text;

        // Determine color, symbol, and text based on status
        switch (item->status) {
            case TODO_COMPLETED:
                color = COLOR_PAIR_TODO_COMPLETED;
                symbol = "✓";
                text = item->content;
                break;
            case TODO_IN_PROGRESS:
                color = COLOR_PAIR_TODO_IN_PROGRESS;
                symbol = "⋯";
                text = item->active_form;
                break;
            case TODO_PENDING:
            default:
                color = COLOR_PAIR_TODO_PENDING;
                symbol = "○";
                text = item->content;
                break;
        }

        // Format the line with indentation
        snprintf(line, sizeof(line), "    %s %s", symbol, text);

        // Add line without prefix (so the color applies to the whole line)
        tui_add_conversation_line(tui, NULL, line, color);
    }
}

void tui_render_active_subagents(TUIState *tui) {
    if (!tui || !tui->conversation_state) {
        return;
    }

    // Access subagent manager from conversation state
    SubagentManager *mgr = tui->conversation_state->subagent_manager;
    if (!mgr) {
        return;
    }

    // Get running count
    int running_count = subagent_manager_get_running_count(mgr);
    if (running_count == 0) {
        return;  // No active subagents to display
    }

    // Add header
    char header[256];
    snprintf(header, sizeof(header), "━━━━━━━ Active Subagents (%d running) ━━━━━━━", running_count);
    tui_add_conversation_line(tui, NULL, header, COLOR_PAIR_TOOL);

    // Iterate through all tracked processes
    for (int i = 0; i < mgr->process_count; i++) {
        SubagentProcess proc_copy;
        if (subagent_manager_get_process(mgr, i, &proc_copy) != 0) {
            continue;
        }

        // Skip completed processes
        if (proc_copy.completed) {
            free(proc_copy.log_file);
            free(proc_copy.prompt);
            free(proc_copy.last_log_tail);
            continue;
        }

        // Display PID and prompt (truncated)
        char proc_info[512];
        char truncated_prompt[100];
        if (proc_copy.prompt && strlen(proc_copy.prompt) > 80) {
            snprintf(truncated_prompt, sizeof(truncated_prompt), "%.77s...", proc_copy.prompt);
        } else {
            snprintf(truncated_prompt, sizeof(truncated_prompt), "%s", proc_copy.prompt ? proc_copy.prompt : "(no prompt)");
        }

        snprintf(proc_info, sizeof(proc_info), "  [PID %d] %s", proc_copy.pid, truncated_prompt);
        tui_add_conversation_line(tui, NULL, proc_info, COLOR_PAIR_STATUS);

        // Display log tail if available
        if (proc_copy.last_log_tail && strlen(proc_copy.last_log_tail) > 0) {
            // Split log tail into lines and display each
            char *tail_copy = strdup(proc_copy.last_log_tail);
            if (tail_copy) {
                char *line = strtok(tail_copy, "\n");
                int line_count = 0;
                const int max_lines = 5;  // Show max 5 lines per subagent

                while (line && line_count < max_lines) {
                    char indented[1024];
                    snprintf(indented, sizeof(indented), "    %s", line);
                    tui_add_conversation_line(tui, NULL, indented, COLOR_PAIR_FOREGROUND);
                    line = strtok(NULL, "\n");
                    line_count++;
                }

                // If there are more lines, indicate truncation
                if (line != NULL) {
                    tui_add_conversation_line(tui, NULL, "    [... more output in log file ...]", COLOR_PAIR_STATUS);
                }

                free(tail_copy);
            }
        } else {
            tui_add_conversation_line(tui, NULL, "    (waiting for output...)", COLOR_PAIR_STATUS);
        }

        // Add spacing between subagents
        tui_add_conversation_line(tui, NULL, "", COLOR_PAIR_FOREGROUND);

        // Free copied strings
        free(proc_copy.log_file);
        free(proc_copy.prompt);
        free(proc_copy.last_log_tail);
    }

    // Add footer
    tui_add_conversation_line(tui, NULL, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━", COLOR_PAIR_TOOL);
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
    free_conversation_entries(tui);

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
static void redraw_conversation(TUIState *tui) {
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

        if (entry->text && entry->text[0] != '\0') {
            int text_pair = (entry->prefix && entry->prefix[0] != '\0') ? NCURSES_PAIR_FOREGROUND : mapped_pair;
            if (has_colors()) {
                wattron(tui->wm.conv_pad, COLOR_PAIR(text_pair));
            }
            waddstr(tui->wm.conv_pad, entry->text);
            if (has_colors()) {
                wattroff(tui->wm.conv_pad, COLOR_PAIR(text_pair));
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

void tui_show_startup_banner(TUIState *tui, const char *version, const char *model, const char *working_dir) {
    if (!tui || !tui->is_initialized) return;

    // Format banner lines with ASCII art cat mascot
    char line1[256];
    char line2[256];
    char line3[256];
    char tip_line[512];

    // ASCII art cat mascot
    snprintf(line1, sizeof(line1), "  /\\_/\\   klawed v%s", version ? version : "?");
    snprintf(line2, sizeof(line2), " ( o.o )  %s", model);
    snprintf(line3, sizeof(line3), "  > ^ <    %s", working_dir);

    // Add padding before mascot
    tui_add_conversation_line(tui, NULL, "", COLOR_PAIR_FOREGROUND);

    // Add banner lines to conversation window
    tui_add_conversation_line(tui, NULL, line1, COLOR_PAIR_ASSISTANT);
    tui_add_conversation_line(tui, NULL, line2, COLOR_PAIR_ASSISTANT);
    tui_add_conversation_line(tui, NULL, line3, COLOR_PAIR_ASSISTANT);
    tui_add_conversation_line(tui, NULL, "", COLOR_PAIR_FOREGROUND);  // Blank line

    // Tips array: randomly select one to display at startup
    static const char *tips[] = {
        "Esc/Ctrl+[ to enter Scroll mode (vim-style); press 'i' to insert.",
        "In Scroll mode, Scroll: j/k (line), Ctrl+D/U (half page), gg/G (top/bottom).",
        "In Scroll mode, use ( and ) to jump between text blocks (paragraphs).",
        /* "Use PageUp/PageDown or Arrow keys to scroll.", */
        /* "Type /help for commands (e.g., /clear, /exit, /add-dir).", */
        "Press Shift+Tab to toggle Plan mode (read-only tools only).",
        "Press Ctrl+C to cancel a running API/tool action.",
        "In Normal mode, :!cmd runs a shell command in the current dir (like Vim).",
        "In Normal mode, :re !cmd puts the command output into the input box.",
        "In Normal mode, :git opens vim-fugitive (requires vim-fugitive plugin).",
        /* "Use /add-dir to attach a directory as context.", */
        "Press Ctrl+D to exit quickly.",
        /* "Use /voice to record and transcribe audio (requires PortAudio).", */
        "Set KLAWED_THEME to change colors. Available: tender (default), kitty-default, dracula, gruvbox-dark, solarized-dark, black-metal.",
        "Set KLAWED_LOG_LEVEL=DEBUG for verbose logs.",
        "API history stored in ./.klawed/api_calls.db (configurable via KLAWED_DB_PATH).",
        "Insert mode supports readline keys: Ctrl+A, Ctrl+E, Alt+B, Alt+F.",
        /* "Switch models via OPENAI_MODEL or ANTHROPIC_MODEL environment variables.", */
        /* "Enable Bedrock with KLAWED_USE_BEDROCK=1 and ANTHROPIC_MODEL set.", */
        "Interrupt long tool runs any time with Ctrl+C.",
        "Press Ctrl+F to open file search popup (fuzzy find files, supports Alt+B/F/D/⌫).",
        "Press Ctrl+R to open history search popup (fuzzy find previous commands).",        /* "Disable prompt caching with DISABLE_PROMPT_CACHING=1 if needed.", */
        "MCP is disabled by default; enable with KLAWED_MCP_ENABLED=1 and configure servers in ~/.config/klawed/.",
        "Use /clear to clear conversation; /quit or /exit to leave.",
        "Use :help to see all available commands.",
        "Token usage stats shown in status bar when in Normal mode (Esc).",
        "Exit methods: Ctrl+D, /quit, or /exit."
    };
    size_t tips_count = sizeof(tips) / sizeof(tips[0]);

    // Compute a simple per-process pseudo-random index without relying on global srand
    unsigned int seed = (unsigned int)(time(NULL) ^ getpid());
    size_t tip_index = tips_count ? (seed % tips_count) : 0;
    snprintf(tip_line, sizeof(tip_line), "Tip: %s", tips[tip_index]);

    tui_add_conversation_line(tui, NULL, tip_line, COLOR_PAIR_STATUS);
    tui_add_conversation_line(tui, NULL, "", COLOR_PAIR_FOREGROUND);
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
static int handle_command_mode_input(TUIState *tui, int ch, const char *prompt) {
    if (!tui || !tui->command_buffer) {
        return 0;
    }

    if (ch == 27) {  // ESC - cancel command mode
        tui->mode = TUI_MODE_NORMAL;
        tui->command_buffer_len = 0;
        if (tui->command_buffer) {
            tui->command_buffer[0] = '\0';
        }
        if (tui->wm.status_height > 0) {
            render_status_window(tui);
        }
        input_redraw(tui, prompt);
        return 0;
    } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {  // Backspace
        if (tui->command_buffer_len > 1) {  // Keep the ':'
            tui->command_buffer_len--;
            tui->command_buffer[tui->command_buffer_len] = '\0';
            input_redraw(tui, prompt);
        } else {
            // Backspace on just ':' exits command mode
            tui->mode = TUI_MODE_NORMAL;
            tui->command_buffer_len = 0;
            tui->command_buffer[0] = '\0';
            if (tui->wm.status_height > 0) {
                render_status_window(tui);
            }
            input_redraw(tui, prompt);
        }
        return 0;
    } else if (ch == 12) {  // Ctrl+L: clear command buffer (keep just ':')
        tui->command_buffer[0] = ':';
        tui->command_buffer[1] = '\0';
        tui->command_buffer_len = 1;
        input_redraw(tui, prompt);
        return 0;
    } else if (ch == 13 || ch == 10) {  // Enter - execute command
        // Parse and execute command
        const char *cmd = tui->command_buffer + 1;  // Skip the ':'

        if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
            // Quit command
            return -1;
        } else if (strcmp(cmd, "w") == 0 || strcmp(cmd, "write") == 0) {
            // Write command (in our context, maybe save conversation?)
            tui_add_conversation_line(tui, "[System]", "Write command not yet implemented", COLOR_PAIR_STATUS);
        } else if (strcmp(cmd, "wq") == 0) {
            // Write and quit
            return -1;
        } else if (strcmp(cmd, "noh") == 0 || strcmp(cmd, "nohlsearch") == 0) {
            // Clear search highlight (status)
            tui_update_status(tui, "");
            free(tui->last_search_pattern);
            tui->last_search_pattern = NULL;
            tui->last_search_match_line = -1;
            // Exit command mode
            tui->mode = TUI_MODE_NORMAL;
            tui->command_buffer_len = 0;
            tui->command_buffer[0] = '\0';
            if (tui->wm.status_height > 0) {
                render_status_window(tui);
            }
            input_redraw(tui, prompt);
            return 0;
        } else if (cmd[0] == '!') {
            // Vim-style shell escape: :!<cmd>
            const char *shell_cmd = cmd + 1;
            // Skip leading whitespace
            while (*shell_cmd == ' ' || *shell_cmd == '\t') {
                shell_cmd++;
            }
            // Run in foreground, like Vim: suspend TUI, run system(), resume
            if (tui_suspend(tui) != 0) {
                LOG_ERROR("[TUI] Failed to suspend TUI for shell command");
            } else {
                // system() runs in current working directory and inherits env
                // User can Ctrl+C to stop (same terminal)
                int rc = system(shell_cmd);
                (void)rc;  // We intentionally do not display output or status

                // Vim-style: show prompt and wait for Enter before resuming
                // Loop to handle multiple commands like Vim does
                while (1) {
                    printf("\nPress ENTER or type command to continue");
                    fflush(stdout);

                    // Read a line from stdin with dynamic allocation
                    char *line = NULL;
                    size_t linecap = 0;
                    ssize_t linelen = getline(&line, &linecap, stdin);

                    if (linelen == -1) {
                        // Handle EOF or error
                        LOG_DEBUG("[TUI] EOF or error reading from stdin after shell command");
                        free(line);
                        break;
                    }

                    // Remove trailing newline
                    if (linelen > 0 && line[linelen-1] == '\n') {
                        line[linelen-1] = '\0';
                        linelen--;
                    }

                    // If line is empty (just Enter), break the loop
                    if (linelen == 0) {
                        free(line);
                        break;
                    }

                    // User typed a command, run it
                    int rc2 = system(line);
                    (void)rc2;
                    free(line);
                    // Loop continues to show prompt again
                }

                if (tui_resume(tui) != 0) {
                    LOG_ERROR("[TUI] Failed to resume TUI after shell command");
                }
            }
            // Stay in Normal mode after command (like Vim)
            tui->mode = TUI_MODE_NORMAL;
            tui->command_buffer_len = 0;
            tui->command_buffer[0] = '\0';
            if (tui->wm.status_height > 0) {
                render_status_window(tui);
            }
            input_redraw(tui, prompt);
            return 0;
        } else if (strncmp(cmd, "re !", 4) == 0) {
            // Optional: :re !<cmd> to replace input buffer with command output
            const char *shell_cmd = cmd + 4;
            if (shell_cmd[0] != '\0') {
                FILE *fp = popen(shell_cmd, "r");
                if (fp) {
                    char *line = NULL;
                    size_t cap = 0;
                    ssize_t nread;
                    // Clear current input buffer
                    if (!tui->input_buffer || !tui->input_buffer->buffer) {
                        LOG_ERROR("[TUI] input_buffer or buffer is NULL in :re ! command");
                        pclose(fp);
                        return 0;
                    }
                    tui->input_buffer->length = 0;
                    tui->input_buffer->cursor = 0;
                    tui->input_buffer->buffer[0] = '\0';
                    while ((nread = getline(&line, &cap, fp)) != -1) {
                        // Ensure capacity
                        // Check for overflow in capacity calculation
                        size_t needed_capacity;
                        if (__builtin_add_overflow((size_t)tui->input_buffer->length, (size_t)nread, &needed_capacity) ||
                            __builtin_add_overflow(needed_capacity, (size_t)1, &needed_capacity)) {
                            LOG_ERROR("[TUI] Capacity calculation overflow in :re ! command");
                            break;
                        }
                        if (needed_capacity > tui->input_buffer->capacity) {
                            size_t new_capacity;
                            if (__builtin_add_overflow(needed_capacity, (size_t)1024, &new_capacity)) {
                                LOG_ERROR("[TUI] New capacity calculation overflow in :re ! command");
                                break;
                            }
                            void *buf_ptr = (void *)tui->input_buffer->buffer;
                            if (buffer_reserve(&buf_ptr, &tui->input_buffer->capacity, new_capacity) != 0) {
                                LOG_ERROR("[TUI] Failed to expand input buffer for :re ! output");
                                break;
                            }
                            tui->input_buffer->buffer = (char *)buf_ptr;
                        }
                        // Check buffer is valid after potential resize
                        if (!tui->input_buffer->buffer) {
                            LOG_ERROR("[TUI] input_buffer->buffer is NULL after resize in :re ! command");
                            break;
                        }
                        // Check for integer overflow
                        if (nread > INT_MAX - tui->input_buffer->length) {
                            LOG_ERROR("[TUI] Integer overflow in :re ! command, nread=%zd, length=%d", nread, tui->input_buffer->length);
                            break;
                        }
                        memcpy(tui->input_buffer->buffer + tui->input_buffer->length, line, (size_t)nread);
                        tui->input_buffer->length += (int)nread;
                        tui->input_buffer->buffer[tui->input_buffer->length] = '\0';
                    }
                    free(line);
                    pclose(fp);
                    // Move cursor to end
                    tui->input_buffer->cursor = tui->input_buffer->length;
                    tui->input_buffer->view_offset = 0;
                    tui->input_buffer->line_scroll_offset = 0;
                    input_redraw(tui, prompt);
                } else {
                    LOG_ERROR("[TUI] Failed to run command for :re !");
                }
            }
            // Stay in Normal mode after command
            tui->mode = TUI_MODE_NORMAL;
            tui->command_buffer_len = 0;
            tui->command_buffer[0] = '\0';
            if (tui->wm.status_height > 0) {
                render_status_window(tui);
            }
            input_redraw(tui, prompt);
            return 0;
        } else if (cmd[0] != '\0') {
            // Unknown command
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Unknown command: %s", cmd);
            tui_add_conversation_line(tui, "[Error]", error_msg, COLOR_PAIR_ERROR);
        }

        // Exit command mode
        tui->mode = TUI_MODE_NORMAL;
        tui->command_buffer_len = 0;
        tui->command_buffer[0] = '\0';
        if (tui->wm.status_height > 0) {
            render_status_window(tui);
        }
        input_redraw(tui, prompt);
        return 0;
    } else if (ch >= 32 && ch < 127) {  // Printable ASCII
        // Add to command buffer
        if (tui->command_buffer_len < tui->command_buffer_capacity - 1) {
            tui->command_buffer[tui->command_buffer_len++] = (char)ch;
            tui->command_buffer[tui->command_buffer_len] = '\0';
            input_redraw(tui, prompt);
        }
        return 0;
    }

    return 0;
}

// Helper: Check if a line is empty (only whitespace)
static int is_line_empty(WINDOW *pad, int line) {
    if (!pad || line < 0) {
        return 0;
    }

    int pad_width, pad_height;
    getmaxyx(pad, pad_height, pad_width);

    if (line >= pad_height) {
        return 0;
    }

    // Check first 100 columns (or pad width, whichever is smaller)
    int cols_to_check = pad_width < 100 ? pad_width : 100;

    for (int col = 0; col < cols_to_check; col++) {
        chtype ch = mvwinch(pad, line, col);
        char c = (char)(ch & A_CHARTEXT);

        // If we find any non-whitespace character, line is not empty
        if (c != ' ' && c != '\t' && c != '\0' && c != '\n') {
            return 0;
        }
    }

    return 1;  // Line is empty
}

// Helper: Find next paragraph boundary (empty line) going down
// Returns the line number of the next empty line, or -1 if none found
static int find_next_paragraph(WINDOW *pad, int start_line, int max_lines) {
    if (!pad || start_line < 0 || max_lines <= 0) {
        return -1;
    }

    int pad_height, pad_width;
    getmaxyx(pad, pad_height, pad_width);
    (void)pad_width;  // Unused

    // Start searching from the line after start_line
    // Skip current position if it's already on an empty line
    int search_start = start_line + 1;

    // If we're on an empty line, skip past consecutive empty lines first
    while (search_start < max_lines && search_start < pad_height &&
           is_line_empty(pad, search_start)) {
        search_start++;
    }

    // Now find the next empty line
    for (int line = search_start; line < max_lines && line < pad_height; line++) {
        if (is_line_empty(pad, line)) {
            return line;
        }
    }

    // No paragraph boundary found, return max_lines (scroll to end)
    return max_lines - 1;
}

// Helper: Find previous paragraph boundary (empty line) going up
// Returns the line number of the previous empty line, or 0 if none found
static int find_prev_paragraph(WINDOW *pad, int start_line, int max_lines) {
    if (!pad || start_line < 0 || max_lines <= 0) {
        return 0;
    }

    int pad_height, pad_width;
    getmaxyx(pad, pad_height, pad_width);
    (void)pad_width;  // Unused
    (void)max_lines;  // Unused

    // Start searching from the line before start_line
    // Skip current position if it's already on an empty line
    int search_start = start_line - 1;

    // If we're on an empty line, skip past consecutive empty lines first
    while (search_start >= 0 && is_line_empty(pad, search_start)) {
        search_start--;
    }

    // Now find the previous empty line
    for (int line = search_start; line >= 0; line--) {
        if (is_line_empty(pad, line)) {
            return line;
        }
    }

    // No paragraph boundary found, return 0 (scroll to top)
    return 0;
}

// Handle search mode input
// Returns: 0 to continue, -1 on error/quit
static int handle_search_mode_input(TUIState *tui, int ch, const char *prompt) {
    if (!tui || !tui->search_buffer) {
        return 0;
    }

    if (ch == 27) {  // ESC - cancel search mode
        tui->mode = TUI_MODE_NORMAL;
        tui->search_buffer_len = 0;
        if (tui->search_buffer) {
            tui->search_buffer[0] = '\0';
        }
        // Clear search pattern and redraw without highlights
        free(tui->last_search_pattern);
        tui->last_search_pattern = NULL;
        redraw_conversation(tui);
        if (tui->wm.status_height > 0) {
            render_status_window(tui);
        }
        input_redraw(tui, prompt);
        return 0;
    } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {  // Backspace
        if (tui->search_buffer_len > 0) {
            tui->search_buffer_len--;
            tui->search_buffer[tui->search_buffer_len] = '\0';
            input_redraw(tui, prompt);
        } else {
            // Backspace on empty search buffer exits search mode
            tui->mode = TUI_MODE_NORMAL;
            tui->search_buffer_len = 0;
            tui->search_buffer[0] = '\0';
            // Clear search pattern and redraw without highlights
            free(tui->last_search_pattern);
            tui->last_search_pattern = NULL;
            redraw_conversation(tui);
            if (tui->wm.status_height > 0) {
                render_status_window(tui);
            }
            input_redraw(tui, prompt);
        }
        return 0;
    } else if (ch == 12) {  // Ctrl+L: clear search buffer
        tui->search_buffer[0] = '\0';
        tui->search_buffer_len = 0;
        input_redraw(tui, prompt);
        return 0;
    } else if (ch == 13 || ch == 10) {  // Enter - execute search
        // Execute search with current pattern
        if (tui->search_buffer_len > 0) {
            // Store last search pattern
            free(tui->last_search_pattern);
            tui->last_search_pattern = strdup(tui->search_buffer);

            // Perform search
            perform_search(tui, tui->search_buffer, tui->search_direction);

            // Redraw conversation with search highlights
            redraw_conversation(tui);
        }

        // Exit search mode
        tui->mode = TUI_MODE_NORMAL;
        tui->search_buffer_len = 0;
        tui->search_buffer[0] = '\0';
        if (tui->wm.status_height > 0) {
            render_status_window(tui);
        }
        input_redraw(tui, prompt);
        return 0;
    } else if (ch >= 32 && ch < 127) {  // Printable ASCII
        // Add to search buffer
        if (tui->search_buffer_len < tui->search_buffer_capacity - 1) {
            tui->search_buffer[tui->search_buffer_len++] = (char)ch;
            tui->search_buffer[tui->search_buffer_len] = '\0';
            input_redraw(tui, prompt);
        }
        return 0;
    }

    return 0;
}

// Perform search through conversation entries
// Returns: 1 if match found, 0 if no match, -1 on error
static int perform_search(TUIState *tui, const char *pattern, int direction) {
    if (!tui || !pattern || !pattern[0]) {
        return -1;
    }

    // Get current scroll position
    int current_scroll = window_manager_get_scroll_offset(&tui->wm);
    int content_lines = window_manager_get_content_lines(&tui->wm);

    // Determine start position for search
    int start_line;
    if (direction == 1) {  // Forward search
        // Start from line after current scroll position
        start_line = current_scroll + 1;
        if (start_line >= content_lines) {
            start_line = 0;  // Wrap around to beginning
        }
    } else {  // Backward search
        // Start from line before current scroll position
        start_line = current_scroll - 1;
        if (start_line < 0) {
            start_line = content_lines - 1;  // Wrap around to end
        }
    }

    // Search through lines in the pad
    int found_line = -1;
    int line = start_line;
    int steps = 0;
    int max_steps = content_lines;

    while (steps < max_steps) {
        // Check if line contains the pattern
        // We need to get the text from the pad
        WINDOW *pad = tui->wm.conv_pad;
        if (!pad) {
            break;
        }

        int pad_height, pad_width;
        getmaxyx(pad, pad_height, pad_width);

        if (line >= pad_height) {
            // Line doesn't exist in pad
            if (direction == 1) {
                line = 0;  // Wrap around
            } else {
                line = pad_height - 1;  // Wrap around
            }
            steps++;
            continue;
        }

        // Read a chunk of text from the pad line
        char line_text[256];
        int col = 0;
        int text_len = 0;

        // Read up to 255 characters from the line
        while (col < pad_width && text_len < 255) {
            chtype ch = mvwinch(pad, line, col);
            char c = (char)(ch & A_CHARTEXT);

            if (c == '\0' || c == '\n') {
                break;
            }

            line_text[text_len++] = c;
            col++;
        }
        line_text[text_len] = '\0';

        // Simple case-insensitive search
        char *pattern_lower = strdup(pattern);
        char *line_lower = strdup(line_text);
        if (pattern_lower && line_lower) {
            // Convert to lowercase for case-insensitive search
            for (int i = 0; pattern_lower[i]; i++) {
                pattern_lower[i] = (char)tolower((unsigned char)pattern_lower[i]);
            }
            for (int i = 0; line_lower[i]; i++) {
                line_lower[i] = (char)tolower((unsigned char)line_lower[i]);
            }

            if (strstr(line_lower, pattern_lower) != NULL) {
                found_line = line;
                break;
            }
        }

        free(pattern_lower);
        free(line_lower);

        // Move to next line
        if (direction == 1) {
            line++;
            if (line >= content_lines) {
                line = 0;  // Wrap around
            }
        } else {
            line--;
            if (line < 0) {
                line = content_lines - 1;  // Wrap around
            }
        }
        steps++;
    }

    if (found_line >= 0) {
        // Scroll to found line
        tui_scroll_conversation(tui, found_line - current_scroll);
        tui->last_search_match_line = found_line;

        // Show status message
        char status_msg[256];
        snprintf(status_msg, sizeof(status_msg), "Search: %s (match at line %d)",
                pattern, found_line + 1);
        tui_update_status(tui, status_msg);

        return 1;
    } else {
        // No match found
        char status_msg[256];
        snprintf(status_msg, sizeof(status_msg), "Search: %s (no match)", pattern);
        tui_update_status(tui, status_msg);

        return 0;
    }
}

// Handle normal mode input
// Returns: 0 to continue, 1 to switch to insert mode, -1 on error/quit
static int handle_normal_mode_input(TUIState *tui, int ch, const char *prompt, void *user_data) {
    if (!tui || !tui->wm.conv_pad) {
        return 0;
    }

    // Structure for extracting ConversationState from user_data
    // Matches InteractiveContext in klawed.c
    typedef struct {
        ConversationState *state;
        TUIState *tui;
        void *worker;
        void *instruction_queue;
        TUIMessageQueue *tui_queue;
        int instruction_queue_capacity;
    } InteractiveContextView;

    // Calculate scrolling parameters based on the visible viewport height,
    // not the pad capacity (using pad size could make half-page jumps too large
    // or appear to do nothing depending on scroll position).
    int viewport_h = tui->wm.conv_viewport_height;
    if (viewport_h <= 0) {
        // Fallback to a sane default if viewport height is unavailable
        int pad_h, pad_w;
        getmaxyx(tui->wm.conv_pad, pad_h, pad_w);
        viewport_h = pad_h > 0 ? pad_h : 1;
    }

    int half_page = viewport_h / 2;
    int full_page = viewport_h - 2;  // Leave a bit of overlap
    if (half_page < 1) half_page = 1;
    if (full_page < 1) full_page = 1;

    // Handle key combinations (like gg, G)
    if (tui->normal_mode_last_key == 'g' && ch == 'g') {
        // gg: Go to top
        window_manager_scroll_to_top(&tui->wm);
        tui->normal_mode_last_key = 0;
        refresh_conversation_viewport(tui);
        if (tui->wm.status_height > 0) {
            render_status_window(tui);
        }
        input_redraw(tui, prompt);
        return 0;
    }

    // Reset last key for non-g keys
    if (ch != 'g') {
        tui->normal_mode_last_key = 0;
    }

    switch (ch) {
        case 'i':  // Enter insert mode (insert at cursor)
        case 'a':  // Enter insert mode (append after cursor)
        case 'A':  // Enter insert mode (append at end of line)
        case 'I':  // Enter insert mode (insert at beginning of line)
        case 'o':  // Enter insert mode (open line below)
        case 'O':  // Enter insert mode (open line above)
            tui->mode = TUI_MODE_INSERT;
            if (tui->wm.status_height > 0) {
                render_status_window(tui);
            }
            input_redraw(tui, prompt);  // Redraw to show input box
            return 0;  // Mode switched, continue processing (not submission)

        case '/':  // Enter forward search mode
            tui->mode = TUI_MODE_SEARCH;
            tui->search_direction = 1;  // Forward search
            // Initialize search buffer
            if (!tui->search_buffer) {
                tui->search_buffer_capacity = 256;
                tui->search_buffer = malloc((size_t)tui->search_buffer_capacity);
                if (!tui->search_buffer) {
                    LOG_ERROR("[TUI] Failed to allocate search buffer");
                    tui->mode = TUI_MODE_NORMAL;
                    return 0;
                }
            }
            tui->search_buffer[0] = '\0';  // Start with empty buffer
            tui->search_buffer_len = 0;
            if (tui->wm.status_height > 0) {
                render_status_window(tui);
            }
            input_redraw(tui, "");  // Redraw to show search buffer
            return 0;

        case '?':  // Enter backward search mode
            tui->mode = TUI_MODE_SEARCH;
            tui->search_direction = -1;  // Backward search
            // Initialize search buffer
            if (!tui->search_buffer) {
                tui->search_buffer_capacity = 256;
                tui->search_buffer = malloc((size_t)tui->search_buffer_capacity);
                if (!tui->search_buffer) {
                    LOG_ERROR("[TUI] Failed to allocate search buffer");
                    tui->mode = TUI_MODE_NORMAL;
                    return 0;
                }
            }
            tui->search_buffer[0] = '\0';  // Start with empty buffer
            tui->search_buffer_len = 0;
            if (tui->wm.status_height > 0) {
                render_status_window(tui);
            }
            input_redraw(tui, "");  // Redraw to show search buffer
            return 0;

        case 'n':  // Repeat search in same direction
            if (tui->last_search_pattern) {
                perform_search(tui, tui->last_search_pattern, tui->search_direction);
                redraw_conversation(tui);
                input_redraw(tui, prompt);
            } else {
                // No previous search
                tui_update_status(tui, "No previous search pattern");
            }
            break;

        case 'N':  // Repeat search in opposite direction
            if (tui->last_search_pattern) {
                perform_search(tui, tui->last_search_pattern, -tui->search_direction);
                redraw_conversation(tui);
                input_redraw(tui, prompt);
            } else {
                // No previous search
                tui_update_status(tui, "No previous search pattern");
            }
            break;

        case ':':  // Enter command mode
            tui->mode = TUI_MODE_COMMAND;
            // Initialize command buffer with ':'
            if (!tui->command_buffer) {
                tui->command_buffer_capacity = 256;
                tui->command_buffer = malloc((size_t)tui->command_buffer_capacity);
                if (!tui->command_buffer) {
                    LOG_ERROR("[TUI] Failed to allocate command buffer");
                    tui->mode = TUI_MODE_NORMAL;
                    return 0;
                }
            }
            tui->command_buffer[0] = ':';
            tui->command_buffer[1] = '\0';
            tui->command_buffer_len = 1;
            if (tui->wm.status_height > 0) {
                render_status_window(tui);
            }
            input_redraw(tui, "");  // Redraw to show command buffer
            return 0;

        case 'j':  // Scroll down 1 line (Vim j)
        case KEY_DOWN:
        case 5:  // Ctrl+E: Scroll down 1 line (Vim Ctrl-E)
            tui_scroll_conversation(tui, 1);
            input_redraw(tui, prompt);
            break;

        case 'k':  // Scroll up 1 line (Vim k)
        case KEY_UP:
        case 25:  // Ctrl+Y: Scroll up 1 line (Vim Ctrl-Y)
            tui_scroll_conversation(tui, -1);
            input_redraw(tui, prompt);
            break;

        case 4:  // Ctrl+D: Scroll down half page
            tui_scroll_conversation(tui, half_page);
            input_redraw(tui, prompt);
            break;

        case 21:  // Ctrl+U: Scroll up half page
            tui_scroll_conversation(tui, -half_page);
            input_redraw(tui, prompt);
            break;

        case 6:  // Ctrl+F: Scroll down full page
        case KEY_NPAGE:  // Page Down
            tui_scroll_conversation(tui, full_page);
            input_redraw(tui, prompt);
            break;

        case 2:  // Ctrl+B: Scroll up full page
        case KEY_PPAGE:  // Page Up
            tui_scroll_conversation(tui, -full_page);
            input_redraw(tui, prompt);
            break;

        case 'g':  // First 'g' in 'gg' sequence
            tui->normal_mode_last_key = 'g';
            break;

        case 'G':  // Go to bottom
            window_manager_scroll_to_bottom(&tui->wm);
            refresh_conversation_viewport(tui);
            if (tui->wm.status_height > 0) {
                render_status_window(tui);
            }
            input_redraw(tui, prompt);
            break;

        case ')':  // Jump to next paragraph (text block)
            {
                int current_scroll = window_manager_get_scroll_offset(&tui->wm);
                int content_lines = window_manager_get_content_lines(&tui->wm);

                // Find next paragraph boundary from current scroll position
                int next_para = find_next_paragraph(tui->wm.conv_pad, current_scroll, content_lines);

                if (next_para > current_scroll) {
                    // Calculate how many lines to scroll
                    int scroll_delta = next_para - current_scroll;
                    tui_scroll_conversation(tui, scroll_delta);

                    LOG_DEBUG("[TUI] Paragraph jump down: from line %d to %d (delta=%d)",
                             current_scroll, next_para, scroll_delta);
                }
                input_redraw(tui, prompt);
            }
            break;

        case '(':  // Jump to previous paragraph (text block)
            {
                int current_scroll = window_manager_get_scroll_offset(&tui->wm);
                int content_lines = window_manager_get_content_lines(&tui->wm);

                // Find previous paragraph boundary from current scroll position
                int prev_para = find_prev_paragraph(tui->wm.conv_pad, current_scroll, content_lines);

                if (prev_para < current_scroll) {
                    // Calculate how many lines to scroll (negative for up)
                    int scroll_delta = prev_para - current_scroll;
                    tui_scroll_conversation(tui, scroll_delta);

                    LOG_DEBUG("[TUI] Paragraph jump up: from line %d to %d (delta=%d)",
                             current_scroll, prev_para, scroll_delta);
                }
                input_redraw(tui, prompt);
            }
            break;

        case KEY_BTAB:  // Shift+Tab: toggle plan_mode (also works in Normal mode)
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
            return 0;  // Handled, don't pass to default case

        case 'q':  // Quit (when input is empty)
            if (tui->input_buffer && tui->input_buffer->length == 0) {
                return -1;  // Signal quit
            }
            break;

        case KEY_RESIZE:
            tui_handle_resize(tui);
            refresh_conversation_viewport(tui);
            render_status_window(tui);
            input_redraw(tui, prompt);
            break;
        case 12:  // Ctrl+L: clear search status (like Vim's :noh)
            // Clear search status message and reset search state
            tui_update_status(tui, "");
            free(tui->last_search_pattern);
            tui->last_search_pattern = NULL;
            tui->last_search_match_line = -1;
            // Refresh to update status bar
            if (tui->wm.status_height > 0) {
                render_status_window(tui);
            }
            input_redraw(tui, prompt);
            break;

        case 'b':  // Toggle input box style
            // Toggle between background and border styles
            if (tui->input_box_style == INPUT_STYLE_BACKGROUND) {
                tui->input_box_style = INPUT_STYLE_BORDER;
                tui_update_status(tui, "Input box style: border");
            } else {
                tui->input_box_style = INPUT_STYLE_BACKGROUND;
                tui_update_status(tui, "Input box style: background");
            }
            // Refresh to show the style change
            if (tui->wm.status_height > 0) {
                render_status_window(tui);
            }
            input_redraw(tui, prompt);
            break;

        default:
            /* Unhandled key in normal mode */
            break;
    }

    return 0;
}

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

// Handle tab completion for commands starting with '/'
static int handle_tab_completion(TUIState *tui, const char *prompt) {
    if (!tui || !tui->input_buffer || !tui->input_buffer->buffer) {
        return 0;
    }

    TUIInputBuffer *input = tui->input_buffer;

    // Only handle commands starting with '/'
    if (input->buffer[0] != '/') {
        return 0;
    }

    // Get completions using the commands system
    CompletionResult *res = commands_tab_completer(input->buffer, input->cursor, NULL);
    if (!res || res->count == 0) {
        // No completions
        if (res) {
            // Free the result
            for (int i = 0; i < res->count; i++) {
                free(res->options[i]);
            }
            free(res->options);
            free(res);
        }
        beep();
        return 0;
    }

    if (res->count == 1) {
        // Single completion: replace the current word
        const char *opt = res->options[0];
        int optlen = (int)strlen(opt);

        // Find start of current word (backtrack to space or beginning)
        int start = input->cursor - 1;
        while (start >= 0 && input->buffer[start] != ' ' && input->buffer[start] != '\t') {
            start--;
        }
        start++;  // Move to first character of word

        int tail_len = input->length - input->cursor;

        // Calculate new buffer size needed
        size_t new_len = (size_t)(start + optlen + tail_len);
        if (new_len + 1 > input->capacity) {
            // Need to resize buffer
            size_t new_capacity = new_len + 1024;  // Add some extra space
            void *buf_ptr = (void *)input->buffer;
            if (buffer_reserve(&buf_ptr, &input->capacity, new_capacity) != 0) {
                // Resize failed
                for (int i = 0; i < res->count; i++) {
                    free(res->options[i]);
                }
                free(res->options);
                free(res);
                return 0;
            }
            input->buffer = (char *)buf_ptr;
        }

        // Move tail to make room for completion
        if (tail_len > 0) {
            memmove(input->buffer + start + optlen,
                   input->buffer + input->cursor,
                   (size_t)(tail_len + 1));  // +1 for null terminator
        }

        // Copy completion
        memcpy(input->buffer + start, opt, (size_t)optlen);
        input->cursor = start + optlen;
        input->length = start + optlen + tail_len;
        input->buffer[input->length] = '\0';

        // Free completion result
        for (int i = 0; i < res->count; i++) {
            free(res->options[i]);
        }
        free(res->options);
        free(res);

        input_redraw(tui, prompt);
        return 1;
    } else {
        // Multiple completions - show them in status line
        // For now, just beep and free resources
        // TODO: Show completion list
        for (int i = 0; i < res->count; i++) {
            free(res->options[i]);
        }
        free(res->options);
        free(res);
        beep();
        return 0;
    }
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
        int result = handle_command_mode_input(tui, ch, prompt);
        if (result == -1) {
            return -1;  // Quit signal
        }
        // Command mode handles all input internally
        return 0;
    }

    // Handle search mode separately
    if (tui->mode == TUI_MODE_SEARCH) {
        int result = handle_search_mode_input(tui, ch, prompt);
        if (result == -1) {
            return -1;  // Quit signal
        }
        // Search mode handles all input internally
        return 0;
    }

    // Handle normal mode separately
    if (tui->mode == TUI_MODE_NORMAL) {
        int result = handle_normal_mode_input(tui, ch, prompt, user_data);
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
                input_insert_string(tui->input_buffer, selected);
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
        LOG_DEBUG("[TUI] Processing key %d in history search mode", ch);
        int result = history_search_process_key(&tui->history_search, ch);
        if (result == 1) {
            // Selection made - insert command into input buffer
            const char *selected = history_search_get_selected(&tui->history_search);
            if (selected) {
                input_insert_string(tui->input_buffer, selected);
                LOG_DEBUG("[TUI] Inserted history command: %s", selected);
            }
            history_search_stop(&tui->history_search);
            tui->mode = TUI_MODE_INSERT;
            // Refresh all windows to restore display
            window_manager_refresh_all(&tui->wm);
            input_redraw(tui, prompt);
        } else if (result == -1) {
            // Cancelled
            history_search_stop(&tui->history_search);
            tui->mode = TUI_MODE_INSERT;
            // Refresh all windows to restore display
            window_manager_refresh_all(&tui->wm);
            input_redraw(tui, prompt);
        } else {
            // Continue - just render the popup
            history_search_render(&tui->history_search);
        }
        return 0;
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
        } else {
            LOG_ERROR("[TUI] Failed to start history search");
            beep();
        }
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
        if (input_backspace(input) > 0) {
            input_redraw(tui, prompt);
        }
    } else if (ch == KEY_DC) {  // Delete key
        if (input_delete_char(input) > 0) {
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
                    input_redraw(tui, prompt);
                }
            }
        }
    } else if (ch == 14) {  // Ctrl+N: next input history
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
                input_redraw(tui, prompt);
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
                    input_redraw(tui, prompt);
                }
            }
        }
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
        if (input_insert_char(input, &newline, 1) == 0) {
            // Skip redraw during paste mode - will redraw once at end
            if (!input->paste_mode) {
                input_redraw(tui, prompt);
            }
        }
    } else if (ch == 13) {  // Enter: submit or newline
        if (input->paste_mode) {
            // In paste mode, Enter inserts newline instead of submitting
            unsigned char newline = '\n';
            if (input_insert_char(input, &newline, 1) == 0) {
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
            input->cursor = move_backward_word(input->buffer, input->cursor);
            input_redraw(tui, prompt);
        } else if (next_ch == 'f' || next_ch == 'F') {  // Alt+f: forward word
            input->cursor = move_forward_word(input->buffer, input->cursor, input->length);
            input_redraw(tui, prompt);
        } else if (next_ch == 'd' || next_ch == 'D') {  // Alt+d: delete next word
            if (input_delete_word_forward(input) > 0) {
                input_redraw(tui, prompt);
            }
        } else if (next_ch == KEY_BACKSPACE || next_ch == 127 || next_ch == 8) {  // Alt+Backspace
            if (input_delete_word_backward(input) > 0) {
                input_redraw(tui, prompt);
            }
        }
    } else if (ch == '\t' || ch == 9) {  // Tab key - trigger autocomplete
        // Handle tab completion for commands starting with '/'
        if (input->buffer && input->buffer[0] == '/') {
            handle_tab_completion(tui, prompt);
        } else {
            // Insert tab character
            unsigned char tab = '\t';
            if (input_insert_char(input, &tab, 1) == 0) {
                if (!input->paste_mode) {
                    input_redraw(tui, prompt);
                }
            }
        }
    } else if (ch >= 32 && ch < 127) {  // Printable ASCII
        unsigned char c = (unsigned char)ch;
        if (input_insert_char(input, &c, 1) == 0) {
            // Skip redraw during paste mode - will redraw once at end
            if (!input->paste_mode) {
                input_redraw(tui, prompt);
            }
        }
    } else if (ch >= 128) {  // UTF-8 multibyte character (basic support)
        unsigned char c = (unsigned char)ch;
        if (input_insert_char(input, &c, 1) == 0) {
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
    if (input_insert_string(tui->input_buffer, text) != 0) {
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
            if (mutable_text[0] == '+' && mutable_text[1] != '+') {
                diff_color = COLOR_PAIR_USER;  // Green for additions
            } else if (mutable_text[0] == '-' && mutable_text[1] != '-') {
                diff_color = COLOR_PAIR_ERROR;  // Red for deletions
            } else if (mutable_text[0] == '@' && mutable_text[1] == '@') {
                diff_color = COLOR_PAIR_STATUS;  // Status color for hunk headers
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
        if (g_resize_flag) {
            g_resize_flag = 0;
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
                    // Save to persistent history (keep DB open)
                    // Append to in-memory history with simple de-dup of last entry
                    if (tui->history_file) {
                        history_file_append(tui->history_file, ext_buffer);
                    }
                    if (tui->input_history_count == 0 ||
                        strcmp(tui->input_history[tui->input_history_count - 1], ext_buffer) != 0) {
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
                            tui->input_history[tui->input_history_count++] = strdup(ext_buffer);
                        }
                    }
                    // Reset history navigation state after submit
                    free(tui->input_saved_before_history);
                    tui->input_saved_before_history = NULL;
                    tui->input_history_index = -1;
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
                    // Save to persistent history (keep DB open)
                    // Append to in-memory history with simple de-dup of last entry
                    if (tui->history_file) {
                        history_file_append(tui->history_file, input);
                    }
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
                    // Reset history navigation state after submit
                    free(tui->input_saved_before_history);
                    tui->input_saved_before_history = NULL;
                    tui->input_history_index = -1;
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
