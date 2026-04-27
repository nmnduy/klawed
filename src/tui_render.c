/*
 * TUI Rendering & Display
 *
 * Handles all rendering operations including:
 * - Status window rendering with spinner
 * - Conversation pad rendering
 * - Input window rendering
 * - Search highlighting
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "tui_render.h"
#include "tui.h"
#include "tui_input.h"
#include "tui_window.h"
#include "tui_conversation.h"
#define COLORSCHEME_EXTERN
#include "colorscheme.h"
#include "fallback_colors.h"
#include "logger.h"
#include "indicators.h"
#include "window_manager.h"
#include "klawed_internal.h"
#include "persistence.h"
#include "spinner_effects.h"
#include "text_diffusion.h"
#include "markdown_render.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include <ncurses.h>
#include <bsd/string.h>
#include <wchar.h>
#include <locale.h>
#include <assert.h>
#include <ctype.h>

#define TUI_DRAW_OK 0
#define TUI_DRAW_CLIPPED 1
#define TUI_DRAW_SKIPPED 2

typedef struct {
    int width;
    int height;
} TuiWindowSize;

// ============================================================================
// Helper Functions
// ============================================================================

static const char *portable_strcasestr(const char *haystack, const char *needle) {
    size_t needle_len = 0;
    const char *cursor = NULL;

    if (!haystack || !needle) {
        return NULL;
    }

    needle_len = strnlen(needle, SIZE_MAX);
    if (needle_len == 0U) {
        return haystack;
    }

    for (cursor = haystack; *cursor != '\0'; cursor++) {
        size_t offset = 0;

        while (offset < needle_len) {
            unsigned char hay_char = (unsigned char)cursor[offset];
            unsigned char needle_char = (unsigned char)needle[offset];

            if (hay_char == '\0') {
                break;
            }
            if (tolower(hay_char) != tolower(needle_char)) {
                break;
            }
            offset++;
        }

        if (offset == needle_len) {
            return cursor;
        }
    }

    return NULL;
}

static int clamp_nonnegative(int value) {
    return value < 0 ? 0 : value;
}

static TuiWindowSize tui_get_window_size(WINDOW *win) {
    TuiWindowSize size = {0};

    if (!win) {
        return size;
    }

    getmaxyx(win, size.height, size.width);
    size.width = clamp_nonnegative(size.width);
    size.height = clamp_nonnegative(size.height);
    return size;
}

static int tui_window_has_point(WINDOW *win, int y, int x) {
    TuiWindowSize size = tui_get_window_size(win);

    if (!win) {
        return 0;
    }

    if (y < 0 || x < 0) {
        return 0;
    }

    if (y >= size.height || x >= size.width) {
        return 0;
    }

    return 1;
}

static int tui_window_remaining_columns(WINDOW *win, int y, int x) {
    TuiWindowSize size = tui_get_window_size(win);

    if (!tui_window_has_point(win, y, x)) {
        return 0;
    }

    (void)y;
    return clamp_nonnegative(size.width - x);
}

static int tui_safe_mvwaddnstr(WINDOW *win, int y, int x, const char *text, int len) {
    int available = 0;
    int clipped_len = 0;

    if (!win || !text || len <= 0) {
        return TUI_DRAW_SKIPPED;
    }

    available = tui_window_remaining_columns(win, y, x);
    if (available <= 0) {
        return TUI_DRAW_SKIPPED;
    }

    clipped_len = len;
    if (clipped_len > available) {
        clipped_len = available;
    }

    if (clipped_len <= 0) {
        return TUI_DRAW_SKIPPED;
    }

    if (mvwaddnstr(win, y, x, text, clipped_len) == ERR) {
        return TUI_DRAW_SKIPPED;
    }

    return clipped_len == len ? TUI_DRAW_OK : TUI_DRAW_CLIPPED;
}

static int tui_safe_mvwaddch(WINDOW *win, int y, int x, chtype ch) {
    if (!tui_window_has_point(win, y, x)) {
        return TUI_DRAW_SKIPPED;
    }

    if (mvwaddch(win, y, x, ch) == ERR) {
        return TUI_DRAW_SKIPPED;
    }

    return TUI_DRAW_OK;
}

static int tui_safe_mvwprint_char(WINDOW *win, int y, int x, const char *glyph) {
    if (!glyph) {
        return TUI_DRAW_SKIPPED;
    }

    return tui_safe_mvwaddnstr(win, y, x, glyph, (int)strlen(glyph));
}

static int tui_safe_wmove(WINDOW *win, int y, int x) {
    if (!tui_window_has_point(win, y, x)) {
        return TUI_DRAW_SKIPPED;
    }

    if (wmove(win, y, x) == ERR) {
        return TUI_DRAW_SKIPPED;
    }

    return TUI_DRAW_OK;
}

static int tui_safe_waddch(WINDOW *win, chtype ch) {
    int cur_y = 0;
    int cur_x = 0;

    if (!win) {
        return TUI_DRAW_SKIPPED;
    }

    getyx(win, cur_y, cur_x);
    if (!tui_window_has_point(win, cur_y, cur_x)) {
        return TUI_DRAW_SKIPPED;
    }

    if (waddch(win, ch) == ERR) {
        return TUI_DRAW_SKIPPED;
    }

    return TUI_DRAW_OK;
}

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
    // Use the already-initialized global variant (initialized once when spinner starts)
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
        // Pick a fresh random spinner variant for this thinking session
        init_global_spinner_variant();
        // Initialize spinner effect with pulse
        spinner_effect_init(&tui->status_spinner_effect, SPINNER_EFFECT_PULSE,
                            SPINNER_COLOR_SOLID,
                            get_spinner_color_status(),
                            NULL);
    }
    tui->status_spinner_active = 1;
    tui->status_spinner_last_update_ns = monotonic_time_ns();
    tui_update_terminal_title(tui);
}

static void status_spinner_stop(TUIState *tui) {
    if (!tui) {
        return;
    }
    tui->status_spinner_active = 0;
    tui->status_spinner_frame = 0;
    tui->status_spinner_last_update_ns = 0;
    tui->status_spinner_spring_initialized = 0;
    tui->status_spinner_pos = 0.0;
    tui->status_spinner_vel = 0.0;
    // Reset pacman state
    tui->pacman_dots_eaten = 0;
    tui->pacman_direction = 1;
    tui->pacman_anim_frame = 0;
    tui->pacman_last_step_ns = 0;
    tui_update_terminal_title(tui);
}

// ============================================================================
// Pacman Thinking Style Rendering
// ============================================================================

// Get the maximum context tokens from env var or default (200k)
static int get_pacman_max_context(void) {
    const char *env = getenv("KLAWED_PACMAN_MAX_CONTEXT");
    if (env) {
        char *endptr;
        long val = strtol(env, &endptr, 10);
        if (endptr != env && *endptr == '\0' && val > 0 && val <= 10000000) {
            return (int)val;
        }
    }
    return 200000; // Default: 200k tokens
}

// Build the pacman bar.
//
// Layout (total width = max_dots + 2, e.g. 18 for max_dots=16):
//
//   pos 0   : origin marker "·"  (always a dot; becomes blank only if pacman is at 0)
//   pos 1…anchor : sweep zone — dots when uneaten, blank when eaten
//   pos anchor   : Pac-Man "ᗧ" (mouth open) or "●" (mouth closed)
//   pos anchor+1…max_dots-1 : uneaten dots "·"
//   pos max_dots : end sentinel "•"
//
// anchor  = floor(ratio * (max_dots - 1)), clamped 0…max_dots-1
// ratio   = prompt_tokens / max_context
//
// Idle (is_working=0):
//   Pac-Man sits at anchor, mouth closed "●", static.
//   Origin "·" visible at pos 0 if anchor > 0.
//
// Working (is_working=1):
//   pacman_dots_eaten sweeps 0 → anchor then resets to 0.
//   The sweep advances one step every PACMAN_STEP_NS (150ms), independent of frame rate.
//   Positions [0, pacman_dots_eaten) are blank (eaten trail).
//   Position pacman_dots_eaten is Pac-Man (mouth alternates ᗧ/● every 300ms).
//   Positions (pacman_dots_eaten, anchor] are uneaten dots "·".
//   Positions (anchor, max_dots-1] are always-uneaten dots "·".
//   If sweep position == 0, origin shows Pac-Man; otherwise origin shows "·".
//
#define PACMAN_STEP_NS 150000000ULL  // 150ms per sweep step (real-time based)

static void build_pacman_frame(TUIState *tui, char *buf, size_t buf_size, int prompt_tokens, int is_working) {
    if (!tui || !buf || buf_size == 0) return;

    int max_context = get_pacman_max_context();
    int max_dots = tui->pacman_max_dots;
    if (max_dots <= 0) max_dots = 16; // Fallback

    // Compute anchor: how far right Pac-Man rests when idle
    int anchor = 0;
    if (max_context > 0 && prompt_tokens > 0) {
        double ratio = (double)prompt_tokens / (double)max_context;
        if (ratio > 1.0) ratio = 1.0;
        anchor = (int)(ratio * (max_dots - 1));
        if (anchor >= max_dots) anchor = max_dots - 1;
    }

    // Advance sweep animation when working (time-based, not frame-based)
    if (is_working) {
        uint64_t now = monotonic_time_ns();
        if (tui->pacman_last_step_ns == 0) {
            tui->pacman_last_step_ns = now; // Initialize on first working frame
        } else if (now - tui->pacman_last_step_ns >= PACMAN_STEP_NS) {
            tui->pacman_last_step_ns = now;
            tui->pacman_dots_eaten++;
            if (tui->pacman_dots_eaten > anchor) {
                tui->pacman_dots_eaten = 0; // Loop back
            }
        }
    } else {
        // Idle: Pac-Man sits at anchor
        tui->pacman_dots_eaten = anchor;
        tui->pacman_last_step_ns = 0; // Reset so animation restarts cleanly next time
        tui->pacman_anim_frame = 0;
    }

    int sweep = tui->pacman_dots_eaten; // Current Pac-Man position (0..anchor)

    // Mouth: open (ᗧ) / closed (●)
    // Animate when working; always open-mouth when idle (ᗧ = classic open face)
    int mouth_open;
    if (is_working) {
        // Toggle mouth every 300ms based on real time
        uint64_t now = monotonic_time_ns();
        mouth_open = (int)((now / 300000000ULL) % 2);
    } else {
        mouth_open = 1; // idle: open mouth "ᗧ" facing right
    }

    // Build the display string
    // Layout: positions 0…max_dots-1 then sentinel "•"
    char tmp[16] = {0};
    size_t idx = 0;
    int i;

    for (i = 0; i <= max_dots && idx < buf_size - 8; i++) {
        if (i == max_dots) {
            // End sentinel
            strlcpy(tmp, "•", sizeof(tmp));
        } else if (i == sweep) {
            // Pac-Man's current position
            strlcpy(tmp, mouth_open ? "ᗧ" : "●", sizeof(tmp));
        } else if (i < sweep) {
            // Eaten trail — blank, EXCEPT position 0 always keeps the origin dot
            // when Pac-Man has moved past it (i.e. sweep > 0)
            strlcpy(tmp, (i == 0) ? "·" : " ", sizeof(tmp));
        } else {
            // Uneaten dot
            strlcpy(tmp, "·", sizeof(tmp));
        }
        size_t len = strlen(tmp);
        if (idx + len < buf_size - 1) {
            memcpy(buf + idx, tmp, len);
            idx += len;
        }
    }

    buf[idx] = '\0';
}

// Initialize pacman state when thinking starts
static void pacman_init(TUIState *tui, int available_width) {
    if (!tui) return;
    (void)available_width;

    // Fixed bar width: 16 dot positions + 1 sentinel "•"
    int max_dots = 16;

    tui->pacman_max_dots = max_dots;
    tui->pacman_dots_eaten = 0;
    tui->pacman_direction = 1;
    tui->pacman_anim_frame = 0;
    tui->pacman_last_step_ns = 0;
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

    // Prepare status message components (agent status - now on LEFT)
    // Note: We render spinner and text separately to use ncurses colors properly.
    // ANSI escape codes don't work with ncurses - they get displayed literally.
    char status_text[512] = {0};  // Status text without spinner (extra room for prefix)
    char spinner_frame[16] = {0}; // Current spinner frame character
    int status_text_len = 0;
    int spinner_frame_len = 0;
    int status_display_width = 0;
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

            // Store spinner frame for separate rendering
            snprintf(spinner_frame, sizeof(spinner_frame), "%s", frame);
            spinner_frame_len = (int)strlen(spinner_frame);
            has_spinner = 1;

            // When screen is narrow, show only spinner without text
            // to make space for token count and scroll percentage
            if (width < narrow_threshold) {
                status_text[0] = '\0';
                status_text_len = 0;
                // Display width is just the spinner (1 character typically)
                status_display_width = utf8_display_width(spinner_frame);
            } else {
                // Update text diffusion animation and get display text
                text_diffusion_update(&tui->status_text_diffusion);
                const char *diffused_text = text_diffusion_get_display(&tui->status_text_diffusion);

                snprintf(status_text, sizeof(status_text), " %s", diffused_text);
                status_text_len = (int)strlen(status_text);
                // Display width = spinner + space + text
                status_display_width = utf8_display_width(spinner_frame) + utf8_display_width(status_text);
            }
        } else {
            // When screen is narrow, hide status text entirely
            // to make space for token count and scroll percentage
            if (width >= narrow_threshold) {
                snprintf(status_text, sizeof(status_text), "%s", tui->status_message);
                status_text_len = (int)strlen(status_text);
                status_display_width = utf8_display_width(status_text);
            }
        }
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
        snprintf(plan_str, sizeof(plan_str), " ● Plan");
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

        snprintf(scroll_str, sizeof(scroll_str), " %d%%", percentage);
        scroll_str_len = (int)strlen(scroll_str);
        scroll_display_width = utf8_display_width(scroll_str);
    }

    // Prepare token usage (show when non-zero, regardless of mode)
    char token_str[128] = {0};
    int token_str_len = 0;
    int token_display_width = 0;

    // Query total prompt/completion tokens and cached tokens for this session
    int64_t prompt_tokens = 0;
    int64_t completion_tokens = 0;
    int64_t cached_tokens = 0;
    if (tui->persistence_db) {
        if (persistence_get_session_token_usage(tui->persistence_db,
                                                tui->session_id,
                                                &prompt_tokens,
                                                &completion_tokens,
                                                &cached_tokens) == 0) {
            LOG_FINE("[TUI] Retrieved session token totals from DB: prompt=%ld completion=%ld cached=%ld",
                     (long)prompt_tokens, (long)completion_tokens, (long)cached_tokens);
        } else {
            LOG_FINE("[TUI] Failed to retrieve session token totals from DB");
        }
    } else {
        LOG_FINE("[TUI] No persistence database connection available");
    }

    // During streaming, the current API call's tokens haven't been logged to the database yet.
    // Fall back to last_prompt_tokens from conversation state to show current request size.
    if (prompt_tokens == 0 && tui->conversation_state) {
        if (conversation_state_lock(tui->conversation_state) == 0) {
            if (tui->conversation_state->last_prompt_tokens > 0) {
                prompt_tokens = tui->conversation_state->last_prompt_tokens;
                LOG_FINE("[TUI] Using last_prompt_tokens from conversation state: %ld",
                         (long)prompt_tokens);
            }
            conversation_state_unlock(tui->conversation_state);
        }
    }

    // Show token count when non-zero, but NOT in PACMAN mode (pacman bar replaces it)
    // Format: "17715 tok" (prompt tokens only — output tokens are usually negligible)
    if (prompt_tokens > 0 && tui->thinking_style != THINKING_STYLE_PACMAN) {
        snprintf(token_str, sizeof(token_str), " %ld tok", (long)prompt_tokens);
        token_str_len = (int)strlen(token_str);
        token_display_width = utf8_display_width(token_str);
        LOG_FINE("[TUI] Rendering token display: %s (mode=%d, width=%d)", token_str, tui->mode, width);
    }
    // Prepare help text for NORMAL mode (shown when no active status)
    // Two variants: full (wide screens) and short (narrow screens)
    char help_str[128] = {0};
    int help_str_len = 0;
    int help_display_width = 0;
    char help_str_short[64] = {0};
    int help_str_short_len = 0;
    int help_display_width_short = 0;
    if (tui->mode == TUI_MODE_NORMAL && !has_spinner && status_text_len == 0) {
        // Full hint: includes backward search, page-down/up
        snprintf(help_str, sizeof(help_str),
                 " i=insert  j/k=scroll  /=search  ?=back  ^D/^U=pgdn/up ");
        help_str_len = (int)strlen(help_str);
        help_display_width = utf8_display_width(help_str);
        // Short fallback: existing hints only
        snprintf(help_str_short, sizeof(help_str_short),
                 " i=insert  j/k=scroll  /=search ");
        help_str_short_len = (int)strlen(help_str_short);
        help_display_width_short = utf8_display_width(help_str_short);
    }

    // Layout: spinner + status message on the LEFT, indicators on the RIGHT
    // Left side: spinner + LLM status message
    // Right side (in order from right): plan mode, scroll %, token usage

    // Calculate total width needed for right-side indicators
    int right_total_width = 0;
    if (plan_str_len > 0) right_total_width += plan_display_width;
    if (scroll_str_len > 0) right_total_width += scroll_display_width;
    if (token_str_len > 0) right_total_width += token_display_width;

    // Calculate where right-side content starts
    int right_start_col = width - right_total_width;
    if (right_start_col < 0) right_start_col = 0;

    // Render spinner and status text on the LEFT
    int left_col = 0;
    int left_limit = right_start_col;  // Don't overlap with right-side indicators

    if (has_spinner && spinner_frame_len > 0 && left_col + status_display_width <= left_limit) {
        // Check if we should render pacman style instead of regular spinner
        if (tui->thinking_style == THINKING_STYLE_PACMAN) {
            // Get prompt tokens from the already-calculated token totals above
            // prompt_tokens is already available from the earlier calculation

            // Initialize pacman if needed
            if (tui->pacman_max_dots <= 0) {
                pacman_init(tui, left_limit);
            }

            // Build pacman frame
            // Only animate when has_spinner (working), static when idle
            char pacman_buf[64] = {0};
            build_pacman_frame(tui, pacman_buf, sizeof(pacman_buf), (int)prompt_tokens, has_spinner);
            int pacman_len = (int)strlen(pacman_buf);

            // Render pacman with STATUS color
            if (has_colors()) {
                wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
            } else {
                wattron(tui->wm.status_win, A_BOLD);
            }
            tui_safe_mvwaddnstr(tui->wm.status_win, 0, left_col, pacman_buf, pacman_len);
            if (has_colors()) {
                wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
            } else {
                wattroff(tui->wm.status_win, A_BOLD);
            }
            left_col += utf8_display_width(pacman_buf);
            // No status text in pacman style — bar only
        } else {
            // Render regular spinner character with STATUS color (yellow)
            if (has_colors()) {
                wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
            } else {
                wattron(tui->wm.status_win, A_BOLD);
            }
            tui_safe_mvwaddnstr(tui->wm.status_win, 0, left_col, spinner_frame, spinner_frame_len);
            if (has_colors()) {
                wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
            } else {
                wattroff(tui->wm.status_win, A_BOLD);
            }
            left_col += utf8_display_width(spinner_frame);

            // Render status text after spinner (if present)
            if (status_text_len > 0 && left_col + utf8_display_width(status_text) <= left_limit) {
                if (has_colors()) {
                    wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
                } else {
                    wattron(tui->wm.status_win, A_BOLD);
                }
                tui_safe_mvwaddnstr(tui->wm.status_win, 0, left_col, status_text, status_text_len);
                if (has_colors()) {
                    wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
                } else {
                    wattroff(tui->wm.status_win, A_BOLD);
                }
            }
        }
    } else if (tui->thinking_style == THINKING_STYLE_PACMAN) {
        // PACMAN mode: show static pacman bar even when not thinking (no spinner)
        // Initialize pacman if needed
        if (tui->pacman_max_dots <= 0) {
            pacman_init(tui, left_limit);
        }

        // Build static pacman frame (is_working = false, so no mouth animation)
        char pacman_buf[64] = {0};
        build_pacman_frame(tui, pacman_buf, sizeof(pacman_buf), (int)prompt_tokens, 0);
        int pacman_len = (int)strlen(pacman_buf);

        // Render pacman with STATUS color
        if (has_colors()) {
            wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
        } else {
            wattron(tui->wm.status_win, A_BOLD);
        }
        tui_safe_mvwaddnstr(tui->wm.status_win, 0, left_col, pacman_buf, pacman_len);
        if (has_colors()) {
            wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
        } else {
            wattroff(tui->wm.status_win, A_BOLD);
        }
        left_col += utf8_display_width(pacman_buf);
    } else if (status_text_len > 0 && status_display_width <= left_limit) {
        // No spinner, just render status text on the left
        if (has_colors()) {
            wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
        } else {
            wattron(tui->wm.status_win, A_BOLD);
        }
        tui_safe_mvwaddnstr(tui->wm.status_win, 0, left_col, status_text, status_text_len);
        if (has_colors()) {
            wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
        } else {
            wattroff(tui->wm.status_win, A_BOLD);
        }
    } else if (help_str_len > 0) {
        // Render help text centered in the available space
        // Try the full hint first; fall back to the short version if it doesn't fit
        int available_width = right_start_col - left_col;
        const char *active_help = NULL;
        int active_help_len = 0;
        int active_help_width = 0;
        if (help_display_width <= available_width) {
            active_help = help_str;
            active_help_len = help_str_len;
            active_help_width = help_display_width;
        } else if (help_display_width_short > 0 && help_display_width_short <= available_width) {
            active_help = help_str_short;
            active_help_len = help_str_short_len;
            active_help_width = help_display_width_short;
        }
        if (active_help) {
            int help_col = left_col + (available_width - active_help_width) / 2;
            if (help_col < left_col) help_col = left_col;
            if (has_colors()) {
                wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_TOOL_DIM) | A_DIM);
            }
            tui_safe_mvwaddnstr(tui->wm.status_win, 0, help_col, active_help, active_help_len);
            if (has_colors()) {
                wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_TOOL_DIM) | A_DIM);
            }
        }
    }

    // Render right-aligned indicators (token usage, scroll %, plan mode)
    // Order from left to right: token usage, scroll %, plan mode
    int right_col = right_start_col;

    // Token usage
    if (token_str_len > 0 && right_col + token_display_width <= width) {
        if (has_colors()) {
            wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_TOOL_DIM) | A_DIM);
        }
        tui_safe_mvwaddnstr(tui->wm.status_win, 0, right_col, token_str, token_str_len);
        if (has_colors()) {
            wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_TOOL_DIM) | A_DIM);
        }
        right_col += token_display_width;
    }

    // Scroll percentage (NORMAL mode only)
    if (scroll_str_len > 0 && right_col + scroll_display_width <= width) {
        if (has_colors()) {
            wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }
        tui_safe_mvwaddnstr(tui->wm.status_win, 0, right_col, scroll_str, scroll_str_len);
        if (has_colors()) {
            wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }
        right_col += scroll_display_width;
    }

    // Plan mode indicator (always visible when enabled)
    if (plan_str_len > 0 && right_col + plan_display_width <= width) {
        LOG_FINE("[TUI] Rendering plan mode at col=%d, width=%d, plan_display_width=%d, mode=%d",
                  right_col, width, plan_display_width, tui->mode);
        if (has_colors()) {
            wattron(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
        } else {
            wattron(tui->wm.status_win, A_BOLD);
        }
        tui_safe_mvwaddnstr(tui->wm.status_win, 0, right_col, plan_str, plan_str_len);
        if (has_colors()) {
            wattroff(tui->wm.status_win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
        } else {
            wattroff(tui->wm.status_win, A_BOLD);
        }
        right_col += plan_display_width;
    } else if (plan_str_len > 0) {
        LOG_FINE("[TUI] Plan mode indicator not rendered: plan_display_width=%d, width=%d, condition=%d",
                  plan_display_width, width, (plan_str_len > 0 && plan_display_width < width));
    }

    (void)has_spinner;  // Suppress unused variable warning

    // Use wnoutrefresh instead of wrefresh to avoid moving the physical cursor.
    // The cursor should remain in the input window, not appear after the spinner.
    wnoutrefresh(tui->wm.status_win);
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
    const char *remaining = NULL;
    const char *match = NULL;
    size_t pattern_len = 0;
    int rendered = 0;
    int cur_y = 0;
    int cur_x = 0;

    if (!win || !text || !text[0]) {
        return 0;
    }

    if (!search_pattern || !search_pattern[0]) {
        if (bg_pair > 0 && has_colors()) {
            wattron(win, COLOR_PAIR(bg_pair));
        }
        /* Use waddnstr directly to let ncurses handle line wrapping.
         * tui_safe_mvwaddnstr clips to remaining columns on the current line,
         * which would silently truncate long text. */
        getyx(win, cur_y, cur_x);
        if (cur_y >= 0 && cur_x >= 0) {
            int written = waddnstr(win, text, (int)strlen(text));
            rendered = (written != ERR) ? (int)strlen(text) : 0;
        }
        if (bg_pair > 0 && has_colors()) {
            wattroff(win, COLOR_PAIR(bg_pair));
        }
        return rendered;
    }

    pattern_len = strlen(search_pattern);
    remaining = text;

    if (bg_pair > 0 && has_colors()) {
        wattron(win, COLOR_PAIR(bg_pair));
    }

    while (*remaining) {
        match = portable_strcasestr(remaining, search_pattern);
        if (!match) {
            break;
        }

        if (match > remaining) {
            size_t before_len = (size_t)(match - remaining);
            getyx(win, cur_y, cur_x);
            /* Use waddnstr to let ncurses wrap; don't clip to line width */
            if (cur_y >= 0 && cur_x >= 0 && waddnstr(win, remaining, (int)before_len) != ERR) {
                rendered += (int)before_len;
            }
        }

        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_SEARCH) | A_BOLD);
        }
        getyx(win, cur_y, cur_x);
        if (cur_y >= 0 && cur_x >= 0 && waddnstr(win, match, (int)pattern_len) != ERR) {
            rendered += (int)pattern_len;
        }
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_SEARCH) | A_BOLD);
        }

        remaining = match + pattern_len;
    }

    if (*remaining) {
        getyx(win, cur_y, cur_x);
        if (cur_y >= 0 && cur_x >= 0 && waddnstr(win, remaining, (int)strlen(remaining)) != ERR) {
            rendered += (int)strlen(remaining);
        }
    }

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
    (void)border_pair;  // Unused, kept for API compatibility

    // Render border character only (│) with border color - no space
    if (has_colors()) {
        wattron(pad, COLOR_PAIR(NCURSES_PAIR_ASSISTANT_BORDER_BG) | A_BOLD);
    }
    {
        int cur_y = 0;
        int cur_x = 0;
        getyx(pad, cur_y, cur_x);
        (void)tui_safe_mvwprint_char(pad, cur_y, cur_x, "│");
    }
    if (has_colors()) {
        wattroff(pad, COLOR_PAIR(NCURSES_PAIR_ASSISTANT_BORDER_BG) | A_BOLD);
        // Reset to foreground color (no background) for the space and text
        wattron(pad, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
    }
    // Add space after border with foreground color (no background)
    {
        int cur_y = 0;
        int cur_x = 0;
        getyx(pad, cur_y, cur_x);
        (void)tui_safe_mvwaddch(pad, cur_y, cur_x, ' ');
    }
    (void)border_str;  // No longer used - we render │ and space separately

    // Render text content with search highlighting if active
    if (tui->last_search_pattern && tui->last_search_pattern[0] != '\0') {
        char *seg_buf = malloc(len + 1);
        if (seg_buf) {
            memcpy(seg_buf, segment, len);
            seg_buf[len] = '\0';
            render_text_with_search_highlight(pad, seg_buf, 0, tui->last_search_pattern, 0);
            free(seg_buf);
        } else {
            {
                int cur_y = 0;
                int cur_x = 0;
                getyx(pad, cur_y, cur_x);
                if (cur_y >= 0 && cur_x >= 0) waddnstr(pad, segment, (int)len);
            }
        }
    } else {
        {
            int cur_y = 0;
            int cur_x = 0;
            getyx(pad, cur_y, cur_x);
            if (cur_y >= 0 && cur_x >= 0) waddnstr(pad, segment, (int)len);
        }
    }

    if (has_colors()) {
        wattroff(pad, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
    }

    if (add_newline) {
        // Check if cursor has already wrapped to next line after filling
        // If we filled to the right edge, ncurses auto-wraps and cursor is at x=0
        // In that case, we don't need to add an explicit newline
        int cur_y, cur_x;
        getyx(pad, cur_y, cur_x);
        (void)cur_y;
        if (cur_x > 0) {
            // Cursor hasn't wrapped yet, need explicit newline
            (void)tui_safe_waddch(pad, '\n');
        }
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

// ============================================================================
// Markdown-aware border segment rendering
// ============================================================================

static void md_border_glyph(WINDOW *pad, int in_code_block) {
    if (has_colors()) {
        if (in_code_block) {
            wattron(pad, COLOR_PAIR(NCURSES_PAIR_TOOL_DIM) | A_DIM);
        } else {
            wattron(pad, COLOR_PAIR(NCURSES_PAIR_ASSISTANT_BORDER_BG) | A_BOLD);
        }
    }
    {
        int cur_y = 0;
        int cur_x = 0;
        getyx(pad, cur_y, cur_x);
        (void)tui_safe_mvwprint_char(pad, cur_y, cur_x, "│");
    }
    if (has_colors()) {
        if (in_code_block) {
            wattroff(pad, COLOR_PAIR(NCURSES_PAIR_TOOL_DIM) | A_DIM);
        } else {
            wattroff(pad, COLOR_PAIR(NCURSES_PAIR_ASSISTANT_BORDER_BG) | A_BOLD);
        }
    }
}

static void md_text_space(WINDOW *pad, int in_code_block) {
    if (has_colors()) {
        wattron(pad, COLOR_PAIR(in_code_block ? NCURSES_PAIR_TOOL_DIM : NCURSES_PAIR_FOREGROUND));
    }
    {
        int cur_y = 0;
        int cur_x = 0;
        getyx(pad, cur_y, cur_x);
        (void)tui_safe_mvwaddch(pad, cur_y, cur_x, ' ');
    }
    if (has_colors()) {
        wattroff(pad, COLOR_PAIR(in_code_block ? NCURSES_PAIR_TOOL_DIM : NCURSES_PAIR_FOREGROUND));
    }
}

static void render_md_segment(TUIState *tui, const char *segment, size_t len,
                              int border_pair, const char *border_str,
                              bool add_newline, int in_code_block,
                              int text_pair, int search_active) {
    WINDOW *pad = tui->wm.conv_pad;

    (void)border_pair;
    (void)border_str;

    md_border_glyph(pad, in_code_block);
    md_text_space(pad, in_code_block);

    if (in_code_block) {
        wattron(pad, A_DIM);
    }

    if (search_active) {
        char *seg_buf = malloc(len + 1);
        if (seg_buf) {
            memcpy(seg_buf, segment, len);
            seg_buf[len] = '\0';
            render_text_with_search_highlight(pad, seg_buf, 0, tui->last_search_pattern, 0);
            free(seg_buf);
        } else {
            int cur_y = 0;
            int cur_x = 0;
            getyx(pad, cur_y, cur_x);
            if (cur_y >= 0 && cur_x >= 0) {
                waddnstr(pad, segment, (int)len);
            }
        }
    } else {
        if (in_code_block) {
            int cur_y = 0;
            int cur_x = 0;
            getyx(pad, cur_y, cur_x);
            if (cur_y >= 0 && cur_x >= 0) {
                waddnstr(pad, segment, (int)len);
            }
        } else {
            markdown_render_inline(tui, segment, len, text_pair);
        }
    }

    if (in_code_block) {
        wattroff(pad, A_DIM);
    }

    if (add_newline) {
        int cur_y = 0;
        int cur_x = 0;
        getyx(pad, cur_y, cur_x);
        if (cur_x > 0) {
            (void)tui_safe_waddch(pad, '\n');
        }
    }
}

static void render_md_header_segment(TUIState *tui, const char *segment, size_t len,
                                     int border_pair, const char *border_str,
                                     bool add_newline, int text_pair, int search_active) {
    WINDOW *pad = tui->wm.conv_pad;

    (void)border_pair;
    (void)border_str;

    md_border_glyph(pad, 0);
    md_text_space(pad, 0);

    if (has_colors()) {
        wattron(pad, COLOR_PAIR(text_pair));
    }
    wattron(pad, A_BOLD);

    if (search_active) {
        char *seg_buf = malloc(len + 1);
        if (seg_buf) {
            memcpy(seg_buf, segment, len);
            seg_buf[len] = '\0';
            render_text_with_search_highlight(pad, seg_buf, 0, tui->last_search_pattern, 0);
            free(seg_buf);
        } else {
            int cur_y = 0;
            int cur_x = 0;
            getyx(pad, cur_y, cur_x);
            if (cur_y >= 0 && cur_x >= 0) {
                waddnstr(pad, segment, (int)len);
            }
        }
    } else {
        markdown_render_inline(tui, segment, len, text_pair);
    }

    wattroff(pad, A_BOLD);
    if (has_colors()) {
        wattroff(pad, COLOR_PAIR(text_pair));
    }

    if (add_newline) {
        int cur_y = 0;
        int cur_x = 0;
        getyx(pad, cur_y, cur_x);
        if (cur_x > 0) {
            (void)tui_safe_waddch(pad, '\n');
        }
    }
}

// Render text with inline markdown, handling line wrapping.
// Used for caret-style assistant messages (no left border).
static void render_caret_text_markdown(TUIState *tui, const char *text, int text_pair) {
    WINDOW *pad = tui->wm.conv_pad;
    int pad_width, pad_height;
    getmaxyx(pad, pad_height, pad_width);
    (void)pad_height;

    int content_width = pad_width;
    if (content_width < 1) content_width = 1;

    const char *line_start = text;
    const char *p = text;

    while (*p) {
        while (*p && *p != '\n') p++;

        size_t line_len = (size_t)(p - line_start);

        if (line_len == 0) {
            if (*p == '\n') {
                waddch(pad, '\n');
                p++;
            }
            line_start = p;
            continue;
        }

        int line_display_width;
        char *tmp = malloc(line_len + 1);
        if (tmp) {
            memcpy(tmp, line_start, line_len);
            tmp[line_len] = '\0';
            line_display_width = utf8_display_width(tmp);
            free(tmp);
        } else {
            line_display_width = (int)line_len;
        }

        if (line_display_width <= content_width) {
            markdown_render_inline(tui, line_start, line_len, text_pair);
        } else {
            const char *chunk_start = line_start;
            size_t remaining = line_len;
            while (remaining > 0) {
                size_t chunk_bytes = find_wrap_point(chunk_start, remaining, content_width);
                markdown_render_inline(tui, chunk_start, chunk_bytes, text_pair);
                chunk_start += chunk_bytes;
                remaining -= chunk_bytes;
                if (remaining > 0) {
                    waddch(pad, '\n');
                }
            }
        }

        if (*p == '\n') {
            waddch(pad, '\n');
            p++;
        }
        line_start = p;
    }
}

// Helper to render text with a left border for assistant messages
// Handles line wrapping by adding border at start of each new line
// Uses NCURSES_PAIR_ASSISTANT_BG for subtle background highlighting
void render_text_with_left_border(TUIState *tui, const char *text, int text_pair,
                                  int border_pair, const char *border_str) {
    if (!text || !text[0]) {
        return;
    }

    WINDOW *pad = tui->wm.conv_pad;
    int pad_width = 0;
    int pad_height = 0;
    getmaxyx(pad, pad_height, pad_width);
    (void)pad_height;

    int border_display_width = utf8_display_width(border_str);
    int content_width = pad_width - border_display_width;
    if (content_width < 1) {
        content_width = 1;
    }

    const char *line_start = text;
    const char *p = text;
    int in_code_block = 0;
    int search_active = (tui->last_search_pattern && tui->last_search_pattern[0] != '\0');

    while (*p) {
        while (*p && *p != '\n') {
            p++;
        }

        size_t line_len = (size_t)(p - line_start);

        if (line_len == 0) {
            render_bordered_segment(tui, "", 0, border_pair, border_str, (*p == '\n'));
        } else {
            int fence = markdown_code_fence(line_start, line_len);
            int is_code_line = 0;

            if (fence != 0) {
                in_code_block = !in_code_block;
                is_code_line = 1;
            } else if (in_code_block) {
                is_code_line = 1;
            }

            if (is_code_line) {
                int line_display_width = 0;
                char *tmp = malloc(line_len + 1);
                if (tmp) {
                    memcpy(tmp, line_start, line_len);
                    tmp[line_len] = '\0';
                    line_display_width = utf8_display_width(tmp);
                    free(tmp);
                } else {
                    line_display_width = (int)line_len;
                }

                if (line_display_width <= content_width) {
                    render_md_segment(tui, line_start, line_len, border_pair, border_str,
                                      (*p == '\n'), 1, text_pair, search_active);
                } else {
                    const char *chunk_start = line_start;
                    size_t remaining = line_len;

                    while (remaining > 0) {
                        size_t chunk_bytes = find_wrap_point(chunk_start, remaining, content_width);
                        render_md_segment(tui, chunk_start, chunk_bytes, border_pair, border_str,
                                          true, 1, text_pair, search_active);
                        chunk_start += chunk_bytes;
                        remaining -= chunk_bytes;
                    }
                }
            } else {
                int hlevel = markdown_header_level(line_start, line_len);
                int is_hrule = markdown_hrule(line_start, line_len);
                size_t prefix_len = 0;
                int list_number = 0;
                char list_type = markdown_list_item(line_start, line_len, &prefix_len, &list_number);
                size_t quote_len = 0;
                int is_quote = markdown_blockquote(line_start, line_len, &quote_len);
                int is_special_prefix = 0;
                size_t skip_prefix = 0;

                if (list_type != 0) {
                    is_special_prefix = 1;
                    skip_prefix = prefix_len;
                } else if (is_quote) {
                    is_special_prefix = 1;
                    skip_prefix = quote_len;
                }

                if (is_hrule) {
                    static const char hrule[] = "────────────────────────";
                    render_md_segment(tui, hrule, sizeof(hrule) - 1, border_pair, border_str,
                                      (*p == '\n'), 1, text_pair, search_active);
                } else {
                    int line_display_width = 0;
                    char *tmp = malloc(line_len + 1);
                    if (tmp) {
                        memcpy(tmp, line_start, line_len);
                        tmp[line_len] = '\0';
                        line_display_width = utf8_display_width(tmp);
                        free(tmp);
                    } else {
                        line_display_width = (int)line_len;
                    }

                    if (line_display_width <= content_width) {
                        if (hlevel > 0) {
                            size_t skip = 0;
                            while (skip < line_len && (line_start[skip] == '#' || isspace((unsigned char)line_start[skip]))) {
                                skip++;
                            }
                            render_md_header_segment(tui, line_start + skip, line_len - skip,
                                                     border_pair, border_str, (*p == '\n'),
                                                     text_pair, search_active);
                        } else if (is_special_prefix) {
                            render_md_segment(tui, line_start + skip_prefix, line_len - skip_prefix,
                                              border_pair, border_str, (*p == '\n'), 0,
                                              text_pair, search_active);
                        } else {
                            render_md_segment(tui, line_start, line_len, border_pair, border_str,
                                              (*p == '\n'), 0, text_pair, search_active);
                        }
                    } else {
                        const char *chunk_start = line_start;
                        size_t remaining = line_len;
                        int first_chunk = 1;

                        while (remaining > 0) {
                            size_t chunk_bytes = find_wrap_point(chunk_start, remaining, content_width);

                            if (first_chunk && hlevel > 0) {
                                size_t skip = 0;
                                while (skip < line_len && (line_start[skip] == '#' || isspace((unsigned char)line_start[skip]))) {
                                    skip++;
                                }
                                if (chunk_bytes > skip) {
                                    render_md_header_segment(tui, chunk_start + skip, chunk_bytes - skip,
                                                             border_pair, border_str, true,
                                                             text_pair, search_active);
                                } else {
                                    render_md_header_segment(tui, chunk_start, chunk_bytes,
                                                             border_pair, border_str, true,
                                                             text_pair, search_active);
                                }
                                first_chunk = 0;
                            } else if (first_chunk && is_special_prefix) {
                                if (chunk_bytes > skip_prefix) {
                                    render_md_segment(tui, chunk_start + skip_prefix, chunk_bytes - skip_prefix,
                                                      border_pair, border_str, true, 0,
                                                      text_pair, search_active);
                                } else {
                                    render_md_segment(tui, chunk_start, chunk_bytes,
                                                      border_pair, border_str, true, 0,
                                                      text_pair, search_active);
                                }
                                first_chunk = 0;
                            } else {
                                render_md_segment(tui, chunk_start, chunk_bytes, border_pair, border_str,
                                                  true, 0, text_pair, search_active);
                                first_chunk = 0;
                            }

                            chunk_start += chunk_bytes;
                            remaining -= chunk_bytes;
                        }
                    }
                }
            }
        }

        if (*p == '\n') {
            p++;
            line_start = p;
        }
    }

    WINDOW *pad_final = tui->wm.conv_pad;
    int cur_y = 0;
    int cur_x = 0;
    getyx(pad_final, cur_y, cur_x);
    if (cur_x > 0) {
        (void)tui_safe_waddch(pad_final, '\n');
    }
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
        case COLOR_PAIR_TOOL_DIM:
            mapped_pair = NCURSES_PAIR_TOOL_DIM;
            break;
        case COLOR_PAIR_DIFF_CONTEXT:
            mapped_pair = NCURSES_PAIR_DIFF_CONTEXT;
            break;
        default:
            /* Keep default mapped_pair (foreground) */
            break;
    }

    // Move to end of pad
    int start_line = window_manager_get_content_lines(&tui->wm);
    (void)tui_safe_wmove(tui->wm.conv_pad, start_line, 0);

    // Check if this is a [User] or [Assistant] message to apply new styling
    int is_user_message = (prefix && strcmp(prefix, "[User]") == 0);
    int is_assistant_message = (prefix && strcmp(prefix, "[Assistant]") == 0);

    // For user messages, add padding line before and caret prefix
    if (is_user_message) {
        // Reset tool tracking - user messages break the tool output chain
        free(tui->last_tool_name);
        tui->last_tool_name = NULL;

        // Add one blank line for top padding
        (void)tui_safe_waddch(tui->wm.conv_pad, '\n');

        // Render prefix '❯ ' with bold user color (matches input box caret)
        if (has_colors()) {
            wattron(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_USER) | A_BOLD);
        }
        (void)tui_safe_mvwaddnstr(tui->wm.conv_pad, start_line + 1, 0, "❯ ", (int)strlen("❯ "));
        if (has_colors()) {
            wattroff(tui->wm.conv_pad, COLOR_PAIR(NCURSES_PAIR_USER) | A_BOLD);
        }
    } else if (is_assistant_message) {
        // Reset tool tracking - assistant messages break the tool output chain
        free(tui->last_tool_name);
        tui->last_tool_name = NULL;
        // Assistant message: check response style
        if (tui->response_style == RESPONSE_STYLE_BORDER) {
            // Border style: use left border decoration (│ ) on each line
            int text_pair = NCURSES_PAIR_FOREGROUND;
            const char *border_str = "│ ";

            // Render the text content with left border
            render_text_with_left_border(tui, text, text_pair, mapped_pair, border_str);

            goto skip_newline;
        } else {
            // Caret style: leading '>>> ' prefix with no border
            if (has_colors()) {
                wattron(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
            }
            { int cur_y = 0; int cur_x = 0; getyx(tui->wm.conv_pad, cur_y, cur_x); (void)tui_safe_mvwaddnstr(tui->wm.conv_pad, cur_y, cur_x, ">>> ", (int)strlen(">>> ")); }
            if (has_colors()) {
                wattroff(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
            }
            // Fall through to write text normally
        }
    } else {
        // Write prefix for other (non-user, non-assistant) messages
        // If text is empty (streaming placeholder), skip rendering — tui_update_last_conversation_line
        // will render the prefix when the first text chunk arrives (cursor at col 0).
        // This matches the [Assistant] border-mode behavior and prevents a spurious empty prefix line.
        if ((!text || text[0] == '\0') && prefix && prefix[0] != '\0') {
            goto skip_newline;
        }
        if (prefix && prefix[0] != '\0') {
            // Use the conversation module to get the appropriate display prefix
            // This handles tree connector logic for consecutive same-tool outputs
            const char *display_prefix = tui_conversation_get_tool_display_prefix(tui, prefix);

            // Check if we're using the tree connector (└─)
            int is_tree_connector = (display_prefix != prefix);

            if (has_colors()) {
                wattron(tui->wm.conv_pad, COLOR_PAIR(mapped_pair) | A_BOLD);
            }

            { int cur_y = 0; int cur_x = 0; getyx(tui->wm.conv_pad, cur_y, cur_x); (void)tui_safe_mvwaddnstr(tui->wm.conv_pad, cur_y, cur_x, display_prefix, (int)strlen(display_prefix)); }

            // Add space after prefix, but not for tree connector (it already includes space)
            if (!is_tree_connector) {
                { int cur_y = 0; int cur_x = 0; getyx(tui->wm.conv_pad, cur_y, cur_x); (void)tui_safe_mvwaddch(tui->wm.conv_pad, cur_y, cur_x, ' '); }
            }

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
            // Check for tool messages: prefix starts with "●" (UTF-8: 0xE2 0x97 0x8F)
            int is_tool_message = (prefix[0] == '\xe2' && prefix[1] == '\x97' && prefix[2] == '\x8f');
            if (is_tool_message) {
                // Tool message: use dimmed color for text (tag keeps tool color)
                text_pair = NCURSES_PAIR_TOOL_DIM;
            } else {
                // Other messages with prefix use foreground
                text_pair = NCURSES_PAIR_FOREGROUND;
            }
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
        } else if (is_assistant_message && tui->response_style == RESPONSE_STYLE_CARET) {
            // Caret-style assistant: render line by line with inline markdown
            render_caret_text_markdown(tui, text, text_pair);
        } else {
            /* Use waddnstr directly to let ncurses handle line wrapping
             * (tui_safe_mvwaddnstr clips to remaining columns, truncating long text) */
            { int cur_y = 0; int cur_x = 0; getyx(tui->wm.conv_pad, cur_y, cur_x); if (cur_y >= 0 && cur_x >= 0) waddnstr(tui->wm.conv_pad, text, (int)strlen(text)); }
        }

        if (has_colors()) {
            wattroff(tui->wm.conv_pad, COLOR_PAIR(text_pair));
        }

        // For user messages, add padding line after
        if (is_user_message) {
            // Add one blank line for bottom padding
            (void)tui_safe_waddch(tui->wm.conv_pad, '\n');

            // Exit early to avoid duplicate newline below
            goto skip_newline;
        }
    }

    // Add newline (for messages that didn't use goto skip_newline)
    (void)tui_safe_waddch(tui->wm.conv_pad, '\n');

skip_newline:
    ; // Empty statement required after label

    // Update total lines (get actual cursor position after wrapping)
    int cur_y, cur_x;
    getyx(tui->wm.conv_pad, cur_y, cur_x);

    // Safety check: ensure cursor is within pad bounds
    int current_pad_height, current_pad_width;
    getmaxyx(tui->wm.conv_pad, current_pad_height, current_pad_width);
    (void)current_pad_width;
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
        cur_x = 0;  // Reset cur_x after clamping cur_y
    }

    // If the cursor is mid-line (cur_x > 0), the current row has content and
    // must be counted — so content_lines = cur_y + 1.
    // If cur_x == 0, the cursor is on a fresh empty row (after a newline),
    // meaning content occupies rows 0..cur_y-1, so content_lines = cur_y.
    {
        int content_lines = (cur_x > 0) ? cur_y + 1 : cur_y;
        window_manager_set_content_lines(&tui->wm, content_lines);
    }

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

    // Reset tool tracking for fresh redraw
    free(tui->last_tool_name);
    tui->last_tool_name = NULL;

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
            tui_safe_mvwaddch(win, row, 0, ACS_VLINE);
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
        tui_safe_mvwprint_char(win, 0, 0, "╭");
        // Top-right corner
        tui_safe_mvwprint_char(win, 0, max_x - 1, "╮");
        // Bottom-left corner
        tui_safe_mvwprint_char(win, max_y - 1, 0, "╰");
        // Bottom-right corner
        tui_safe_mvwprint_char(win, max_y - 1, max_x - 1, "╯");
        // Top and bottom horizontal lines
        for (int col = 1; col < max_x - 1; col++) {
            tui_safe_mvwprint_char(win, 0, col, "─");
            tui_safe_mvwprint_char(win, max_y - 1, col, "─");
        }
        // Left and right vertical lines
        for (int row = 1; row < max_y - 1; row++) {
            tui_safe_mvwprint_char(win, row, 0, "│");
            tui_safe_mvwprint_char(win, row, max_x - 1, "│");
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
            tui_safe_mvwaddch(win, 0, col, ACS_HLINE);
        }
        // Bottom border at last row
        for (int col = 0; col < input->win_width; col++) {
            tui_safe_mvwaddch(win, input->win_height - 1, col, ACS_HLINE);
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
            tui_safe_mvwprint_char(win, 1, 0, "❯ ");
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
            tui_safe_mvwprint_char(win, 0, 0, "❯ ");
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
        tui_safe_mvwaddnstr(win, content_start_row, content_start_col,
                           mode_prefix, (int)strlen(mode_prefix));
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
            tui_safe_mvwaddch(win, screen_y, screen_x, (chtype)(unsigned char)c);
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
        (void)tui_safe_wmove(win, cursor_screen_y, cursor_screen_x);
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
                (void)tui_safe_mvwaddch(win, row + content_start_row, indicator_col, ACS_VLINE);
            }

            // Draw thumb (offset by content_start_row for border style)
            for (int row = thumb_top; row < thumb_top + thumb_height; row++) {
                (void)tui_safe_mvwaddch(win, row + content_start_row, indicator_col, ACS_CKBOARD);
            }

            if (has_colors()) {
                wattroff(win, COLOR_PAIR(NCURSES_PAIR_PROMPT));
            }

            // Restore cursor position after drawing indicators
            if (cursor_screen_y >= content_start_row &&
                cursor_screen_y < bottom_boundary &&
                cursor_screen_x >= 0 && cursor_screen_x < input->win_width) {
                (void)tui_safe_wmove(win, cursor_screen_y, cursor_screen_x);
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
        text_diffusion_reset(&tui->status_text_diffusion);
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

        // Start text diffusion animation for the new message
        text_diffusion_set_target(&tui->status_text_diffusion, message);
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

// ============================================================================
// TODO Banner Rendering
// ============================================================================

// UTF-8 icons for TODO banner (using colorizable characters)
#define TODO_ICON_CURRENT   "▶"    // Right-pointing triangle (colorizable)
#define TODO_ICON_PENDING   "○"    // Unfilled circle (colorizable)
#define TODO_ICON_COMPLETED "◎"    // Bullseye target (colorizable)

int tui_render_todo_banner(TUIState *tui, const TodoList *list) {
    if (!tui || !tui->is_initialized) {
        return 0;
    }

    // Count todos by status
    size_t in_progress_count = 0;
    size_t pending_count = 0;
    size_t completed_count = 0;

    if (list && list->count > 0) {
        for (size_t i = 0; i < list->count; i++) {
            if (list->items[i].status == TODO_IN_PROGRESS) {
                in_progress_count++;
            } else if (list->items[i].status == TODO_PENDING) {
                pending_count++;
            } else if (list->items[i].status == TODO_COMPLETED) {
                completed_count++;
            }
        }
    }

    size_t total_count = in_progress_count + pending_count + completed_count;
    size_t incomplete_count = in_progress_count + pending_count;

    // If no todos at all, or all todos are completed, hide the TODO window
    if (total_count == 0 || incomplete_count == 0) {
        if (tui->wm.todo_win) {
            window_manager_hide_todo_window(&tui->wm);
            // Refresh to clear the hidden window from screen
            window_manager_refresh_all(&tui->wm);
        }
        return 0;
    }

    // Calculate needed height: 1 line per task (up to max)
    // Show at most 4 tasks to save space
    size_t max_display_tasks = 4;
    size_t display_tasks = total_count > max_display_tasks ? max_display_tasks : total_count;
    // +1 padding when there are pending items
    size_t padding_lines = (pending_count > 0) ? 1 : 0;
    int needed_height = (int)(display_tasks + 1 + padding_lines);

    // Show the TODO window
    if (window_manager_show_todo_window(&tui->wm, needed_height) != 0) {
        return 0;
    }

    // Clear and render the TODO banner
    WINDOW *win = tui->wm.todo_win;
    werase(win);

    int width = tui->wm.screen_width;
    int row = (pending_count > 0) ? 1 : 0;  // Add padding line when there are pending items

    // Render all tasks (in_progress first, then pending, then completed)
    // Using single theme color (STATUS) with left border style like assistant messages
    size_t tasks_shown = 0;
    int max_task_len = width - 5;  // Border + space + icon + padding
    if (max_task_len < 20) max_task_len = 20;
    if (max_task_len > 250) max_task_len = 250;

    // First pass: show in_progress tasks
    for (size_t i = 0; i < list->count && tasks_shown < max_display_tasks; i++) {
        if (list->items[i].status != TODO_IN_PROGRESS) continue;

        const char *icon = TODO_ICON_CURRENT;
        const char *task_text = list->items[i].active_form;

        // Draw left border in status color (like assistant message border style)
        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
        }
        tui_safe_mvwprint_char(win, row, 0, "│");
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
        }

        // Draw the icon in status color
        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
        }
        tui_safe_mvwaddnstr(win, row, 2, icon, (int)strlen(icon));
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS) | A_BOLD);
        }

        // Truncate and draw task text in regular foreground color
        char task_buf[256];
        size_t task_len = strlen(task_text);
        if (task_len > (size_t)max_task_len) {
            snprintf(task_buf, sizeof(task_buf), "%.*s...", max_task_len - 3, task_text);
        } else {
            snprintf(task_buf, sizeof(task_buf), "%s", task_text);
        }

        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }
        tui_safe_mvwaddnstr(win, row, 4, task_buf, (int)strlen(task_buf));
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }

        row++;
        tasks_shown++;
    }

    // Second pass: show pending tasks
    for (size_t i = 0; i < list->count && tasks_shown < max_display_tasks; i++) {
        if (list->items[i].status != TODO_PENDING) continue;

        const char *icon = TODO_ICON_PENDING;
        const char *task_text = list->items[i].content;

        // Draw left border in status color
        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }
        tui_safe_mvwprint_char(win, row, 0, "│");
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }

        // Draw the icon in status color
        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }
        tui_safe_mvwaddnstr(win, row, 2, icon, (int)strlen(icon));
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }

        // Truncate and draw task text in regular foreground color
        char task_buf[256];
        size_t task_len = strlen(task_text);
        if (task_len > (size_t)max_task_len) {
            snprintf(task_buf, sizeof(task_buf), "%.*s...", max_task_len - 3, task_text);
        } else {
            snprintf(task_buf, sizeof(task_buf), "%s", task_text);
        }

        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }
        tui_safe_mvwaddnstr(win, row, 4, task_buf, (int)strlen(task_buf));
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }

        row++;
        tasks_shown++;
    }

    // Third pass: show completed tasks
    for (size_t i = 0; i < list->count && tasks_shown < max_display_tasks; i++) {
        if (list->items[i].status != TODO_COMPLETED) continue;

        const char *icon = TODO_ICON_COMPLETED;
        const char *task_text = list->items[i].content;

        // Draw left border in status color (dimmed for completed)
        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }
        tui_safe_mvwprint_char(win, row, 0, "│");
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }

        // Draw the icon in status color
        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }
        tui_safe_mvwaddnstr(win, row, 2, icon, (int)strlen(icon));
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }

        // Truncate and draw task text in regular foreground color
        char task_buf[256];
        size_t task_len = strlen(task_text);
        if (task_len > (size_t)max_task_len) {
            snprintf(task_buf, sizeof(task_buf), "%.*s...", max_task_len - 3, task_text);
        } else {
            snprintf(task_buf, sizeof(task_buf), "%s", task_text);
        }

        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }
        tui_safe_mvwaddnstr(win, row, 4, task_buf, (int)strlen(task_buf));
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }

        row++;
        tasks_shown++;
    }

    // If we have more tasks than we can show, indicate it
    if (total_count > max_display_tasks) {
        char more_buf[64];
        size_t more_count = total_count - max_display_tasks;
        snprintf(more_buf, sizeof(more_buf), "... and %zu more", more_count);

        // Draw left border for the "more" line too
        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }
        tui_safe_mvwprint_char(win, row, 0, "│");
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
        }

        if (has_colors()) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }
        (void)tui_safe_mvwaddnstr(win, row, 4, more_buf, (int)strlen(more_buf));
        if (has_colors()) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }
    }

    wnoutrefresh(win);

    return 1;
}
