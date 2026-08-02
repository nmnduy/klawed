/*
 * TUI Core Initialization and Cleanup
 *
 * Handles core TUI lifecycle operations including initialization,
 * cleanup, suspend/resume, and startup display.
 */

// Define feature test macros before any includes
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "tui_core.h"
#include "tui.h"
#include "tui_input.h"
#include "tui_conversation.h"
#include "tui_window.h"
#include "file_search.h"
#include "history_search.h"
#define COLORSCHEME_EXTERN
#include "colorscheme.h"
#include "fallback_colors.h"
#include "logger.h"
#include "window_manager.h"
#include "history_file.h"
#include "subagent_manager.h"
#include "config.h"
#include "data_dir.h"
#include "text_diffusion.h"
#include "voice_mode.h"
#include <stdlib.h>
#include <bsd/stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <locale.h>
#include <langinfo.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define INPUT_BUFFER_SIZE 8192
#define INPUT_WIN_MIN_HEIGHT 2
#define INPUT_WIN_MAX_HEIGHT_PERCENT 20
#define CONV_WIN_PADDING 1
#define STATUS_WIN_HEIGHT 1

// Convert RGB (0-255) to ncurses color (0-1000)
static short rgb_to_ncurses(int value) {
    return (short)((value * 1000) / 255);
}

static RGB blend_rgb(RGB base, RGB accent, int accent_pct) {
    RGB blended;
    int base_pct = 100 - accent_pct;

    blended.r = (base.r * base_pct + accent.r * accent_pct) / 100;
    blended.g = (base.g * base_pct + accent.g * accent_pct) / 100;
    blended.b = (base.b * base_pct + accent.b * accent_pct) / 100;

    return blended;
}

// Initialize ncurses color pairs from theme
void tui_reload_colors(void) {
    // Check if terminal supports colors
    if (!has_colors()) {
        LOG_DEBUG("[TUI] Terminal does not support colors");
        return;
    }

    start_color();

    // Try to use terminal's default colors as base
    // This allows -1 to mean "default/transparent" background
    // If it fails, we'll use COLOR_BLACK as fallback for backgrounds
    int default_colors_supported = (use_default_colors() == OK);
    short default_bg = default_colors_supported ? -1 : COLOR_BLACK;

    if (!default_colors_supported) {
        LOG_DEBUG("[TUI] Terminal does not support default colors, using COLOR_BLACK for backgrounds");
    }

    // If we have a loaded theme, use it to initialize custom colors
    if (g_theme_loaded) {
        LOG_DEBUG("[TUI] Initializing ncurses colors from loaded theme");

        int supports_256 = (COLORS >= 256);
        RGB input_bg_rgb = blend_rgb(g_theme.background_rgb, g_theme.user_rgb, 5);
        RGB assistant_bg_rgb = blend_rgb(g_theme.background_rgb, g_theme.assistant_rgb, 3);
        RGB error_bg_rgb = blend_rgb(g_theme.background_rgb, g_theme.error_rgb, 4);
        RGB tool_dim_rgb = blend_rgb(g_theme.foreground_rgb, g_theme.background_rgb, 50);
        // Code block: foreground dimmed 15% toward background (recessed "set type" feel)
        RGB code_block_bg_rgb = blend_rgb(g_theme.background_rgb, g_theme.foreground_rgb, 8);
        // Inline code: subtle cool wash from assistant color
        RGB inline_code_bg_rgb = blend_rgb(g_theme.background_rgb, g_theme.assistant_rgb, 6);
        // H1 accent: use the theme's header/assistant color for the accent line
        RGB h1_accent_rgb = g_theme.assistant_rgb;
        // Status bar: background darkened 8% (shadow line between earth and sky)
        RGB status_bg_rgb = g_theme.background_rgb;
        status_bg_rgb.r = (status_bg_rgb.r * 92) / 100;
        status_bg_rgb.g = (status_bg_rgb.g * 92) / 100;
        status_bg_rgb.b = (status_bg_rgb.b * 92) / 100;

        // Define custom colors (colors 16-32 are safe to redefine)
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

            // TODO accent color (magenta/color5 - distinct from assistant cyan)
            init_color(27,
                rgb_to_ncurses(g_theme.todo_accent_rgb.r),
                rgb_to_ncurses(g_theme.todo_accent_rgb.g),
                rgb_to_ncurses(g_theme.todo_accent_rgb.b));

            init_color(23,
                rgb_to_ncurses(input_bg_rgb.r),
                rgb_to_ncurses(input_bg_rgb.g),
                rgb_to_ncurses(input_bg_rgb.b));

            // Input border color (use user/green color)
            init_color(24,
                rgb_to_ncurses(g_theme.user_rgb.r),
                rgb_to_ncurses(g_theme.user_rgb.g),
                rgb_to_ncurses(g_theme.user_rgb.b));

            init_color(25,
                rgb_to_ncurses(assistant_bg_rgb.r),
                rgb_to_ncurses(assistant_bg_rgb.g),
                rgb_to_ncurses(assistant_bg_rgb.b));

            init_color(26,
                rgb_to_ncurses(tool_dim_rgb.r),
                rgb_to_ncurses(tool_dim_rgb.g),
                rgb_to_ncurses(tool_dim_rgb.b));

            init_color(28,
                rgb_to_ncurses(error_bg_rgb.r),
                rgb_to_ncurses(error_bg_rgb.g),
                rgb_to_ncurses(error_bg_rgb.b));

            // Code block recessed background
            init_color(29,
                rgb_to_ncurses(code_block_bg_rgb.r),
                rgb_to_ncurses(code_block_bg_rgb.g),
                rgb_to_ncurses(code_block_bg_rgb.b));

            // Inline code subtle tint background
            init_color(30,
                rgb_to_ncurses(inline_code_bg_rgb.r),
                rgb_to_ncurses(inline_code_bg_rgb.g),
                rgb_to_ncurses(inline_code_bg_rgb.b));

            // H1 accent color (header/assistant color)
            init_color(31,
                rgb_to_ncurses(h1_accent_rgb.r),
                rgb_to_ncurses(h1_accent_rgb.g),
                rgb_to_ncurses(h1_accent_rgb.b));

            // Status bar shadow background (slightly darker than main bg)
            init_color(32,
                rgb_to_ncurses(status_bg_rgb.r),
                rgb_to_ncurses(status_bg_rgb.g),
                rgb_to_ncurses(status_bg_rgb.b));

            // Initialize color pairs with custom colors
            init_pair(NCURSES_PAIR_FOREGROUND, 16, default_bg);
            init_pair(NCURSES_PAIR_USER, 17, default_bg);
            init_pair(NCURSES_PAIR_ASSISTANT, 18, default_bg);
            init_pair(NCURSES_PAIR_STATUS, 19, default_bg);
            init_pair(NCURSES_PAIR_ERROR, 20, default_bg);
            // Use dedicated tool color pair (distinct from assistant)
            // Unify tool color with status to reduce color variance
            init_pair(NCURSES_PAIR_TOOL, 19, default_bg);
            init_pair(NCURSES_PAIR_PROMPT, 17, default_bg);  // Use USER color for prompt
            // TODO color pairs
            init_pair(NCURSES_PAIR_TODO_COMPLETED, 17, default_bg);    // Green (same as USER)
            init_pair(NCURSES_PAIR_TODO_IN_PROGRESS, 19, default_bg);  // Yellow (same as STATUS)
            init_pair(NCURSES_PAIR_TODO_PENDING, 27, default_bg);      // Magenta (TODO accent - distinct from assistant)
            init_pair(NCURSES_PAIR_SEARCH, 22, default_bg);            // Search highlight (color5 from theme)
            init_pair(NCURSES_PAIR_INPUT_BG, 16, 23);          // Foreground on subtle background
            init_pair(NCURSES_PAIR_INPUT_BORDER, 24, default_bg);      // Border/accent color
            init_pair(NCURSES_PAIR_USER_MSG_BG, 16, 23);       // User message background (same as input bg)
            init_pair(NCURSES_PAIR_ASSISTANT_BG, 16, 25);      // Foreground on subtle assistant background
            init_pair(NCURSES_PAIR_ASSISTANT_BORDER_BG, 18, default_bg);  // Assistant color with no background (for border)
            init_pair(NCURSES_PAIR_TOOL_DIM, 26, default_bg);             // Dimmed gray for tool text
            init_pair(NCURSES_PAIR_DIFF_CONTEXT, 26, default_bg);         // Dimmed gray for diff context (same as tool dim)
            init_pair(NCURSES_PAIR_GOAL, 16, default_bg);                 // Goal tag uses foreground color
            init_pair(NCURSES_PAIR_ERROR_BG, 20, 28);                    // Error text on error-tinted background
            init_pair(NCURSES_PAIR_CODE_BLOCK, 16, 29);                  // Code text on recessed background
            init_pair(NCURSES_PAIR_INLINE_CODE, 16, 30);                 // Inline code on subtle tint
            init_pair(NCURSES_PAIR_H1_ACCENT, 31, default_bg);           // H1 accent color for text + rule
            init_pair(NCURSES_PAIR_STATUS_BG, 16, default_bg);          // Status bar: foreground on main background

            LOG_DEBUG("[TUI] Custom colors initialized with truecolor support");
        } else if (supports_256) {
            // Map theme colors to nearest 256-color palette indices
            int fg_idx = rgb_to_256_index(g_theme.foreground_rgb);
            int user_idx = rgb_to_256_index(g_theme.user_rgb);
            int assistant_idx = rgb_to_256_index(g_theme.assistant_rgb);
            int status_idx = rgb_to_256_index(g_theme.status_rgb);
            int error_idx = rgb_to_256_index(g_theme.error_rgb);
            int search_idx = rgb_to_256_index(g_theme.search_rgb);
            int todo_accent_idx = rgb_to_256_index(g_theme.todo_accent_rgb);
            int input_bg_idx = rgb_to_256_index(input_bg_rgb);
            int assistant_bg_idx = rgb_to_256_index(assistant_bg_rgb);
            int error_bg_idx = rgb_to_256_index(error_bg_rgb);
            int tool_dim_idx = rgb_to_256_index(tool_dim_rgb);
            int code_block_bg_idx = rgb_to_256_index(code_block_bg_rgb);
            int inline_code_bg_idx = rgb_to_256_index(inline_code_bg_rgb);
            int h1_accent_idx = rgb_to_256_index(h1_accent_rgb);

            init_pair(NCURSES_PAIR_FOREGROUND, (short)fg_idx, default_bg);
            init_pair(NCURSES_PAIR_USER, (short)user_idx, default_bg);
            init_pair(NCURSES_PAIR_ASSISTANT, (short)assistant_idx, default_bg);
            init_pair(NCURSES_PAIR_STATUS, (short)status_idx, default_bg);
            init_pair(NCURSES_PAIR_ERROR, (short)error_idx, default_bg);
            // Use status color for tool tag to reduce color variance
            init_pair(NCURSES_PAIR_TOOL, (short)status_idx, default_bg);
            init_pair(NCURSES_PAIR_PROMPT, (short)user_idx, default_bg);
            // TODO color pairs
            init_pair(NCURSES_PAIR_TODO_COMPLETED, (short)user_idx, default_bg);
            init_pair(NCURSES_PAIR_TODO_IN_PROGRESS, (short)status_idx, default_bg);
            init_pair(NCURSES_PAIR_TODO_PENDING, (short)todo_accent_idx, default_bg);  // Magenta - distinct from assistant
            init_pair(NCURSES_PAIR_SEARCH, (short)search_idx, default_bg);  // Search highlight (color5 from theme)
            init_pair(NCURSES_PAIR_INPUT_BG, (short)fg_idx, (short)input_bg_idx);
            init_pair(NCURSES_PAIR_INPUT_BORDER, (short)user_idx, default_bg);  // Border color (user/green)
            init_pair(NCURSES_PAIR_USER_MSG_BG, (short)fg_idx, (short)input_bg_idx);
            init_pair(NCURSES_PAIR_ASSISTANT_BG, (short)fg_idx, (short)assistant_bg_idx);
            init_pair(NCURSES_PAIR_ASSISTANT_BORDER_BG, (short)assistant_idx, default_bg);  // Assistant color with no background (for border)
            init_pair(NCURSES_PAIR_TOOL_DIM, (short)tool_dim_idx, default_bg);
            init_pair(NCURSES_PAIR_DIFF_CONTEXT, (short)tool_dim_idx, default_bg);
            init_pair(NCURSES_PAIR_GOAL, (short)fg_idx, default_bg);      // Goal tag uses foreground color
            init_pair(NCURSES_PAIR_ERROR_BG, (short)error_idx, (short)error_bg_idx);  // Error text on error-tinted bg
            init_pair(NCURSES_PAIR_CODE_BLOCK, (short)fg_idx, (short)code_block_bg_idx);
            init_pair(NCURSES_PAIR_INLINE_CODE, (short)fg_idx, (short)inline_code_bg_idx);
            init_pair(NCURSES_PAIR_H1_ACCENT, (short)h1_accent_idx, default_bg);
            init_pair(NCURSES_PAIR_STATUS_BG, (short)fg_idx, default_bg);

            LOG_DEBUG("[TUI] Custom colors initialized using 256-color palette (no direct color change support)");
        } else {
            LOG_DEBUG("[TUI] Terminal does not support color changes or 256 colors, using standard colors");
            // Fall back to standard ncurses colors
            init_pair(NCURSES_PAIR_FOREGROUND, COLOR_WHITE, default_bg);
            init_pair(NCURSES_PAIR_USER, COLOR_GREEN, default_bg);
            init_pair(NCURSES_PAIR_ASSISTANT, COLOR_CYAN, default_bg);
            init_pair(NCURSES_PAIR_STATUS, COLOR_YELLOW, default_bg);
            init_pair(NCURSES_PAIR_ERROR, COLOR_RED, default_bg);
            // Use magenta for tool tag (distinct from assistant cyan)
            // Unify tool color with status color
            init_pair(NCURSES_PAIR_TOOL, COLOR_YELLOW, default_bg);
            init_pair(NCURSES_PAIR_PROMPT, COLOR_GREEN, default_bg);
            // TODO color pairs
            init_pair(NCURSES_PAIR_TODO_COMPLETED, COLOR_GREEN, default_bg);
            init_pair(NCURSES_PAIR_TODO_IN_PROGRESS, COLOR_YELLOW, default_bg);
            init_pair(NCURSES_PAIR_TODO_PENDING, COLOR_MAGENTA, default_bg);  // Magenta - distinct from assistant cyan
            init_pair(NCURSES_PAIR_SEARCH, COLOR_MAGENTA, default_bg);  // Fallback: magenta for search highlights
            init_pair(NCURSES_PAIR_INPUT_BG, COLOR_WHITE, COLOR_BLACK);  // Fallback: white on black
            init_pair(NCURSES_PAIR_INPUT_BORDER, COLOR_GREEN, default_bg);  // Fallback: green border (user color)
            init_pair(NCURSES_PAIR_USER_MSG_BG, COLOR_WHITE, COLOR_BLACK);  // Fallback: user message background
            init_pair(NCURSES_PAIR_ASSISTANT_BG, COLOR_WHITE, default_bg);  // Fallback: no background (use default)
            init_pair(NCURSES_PAIR_ASSISTANT_BORDER_BG, COLOR_CYAN, default_bg);  // Fallback: cyan (assistant color)
            init_pair(NCURSES_PAIR_TOOL_DIM, COLOR_WHITE, default_bg);  // Fallback: foreground color for dimmed tool text
            init_pair(NCURSES_PAIR_DIFF_CONTEXT, COLOR_WHITE, default_bg);  // Fallback: foreground color for diff context
            init_pair(NCURSES_PAIR_GOAL, COLOR_WHITE, default_bg);  // Goal tag uses foreground color
            init_pair(NCURSES_PAIR_ERROR_BG, COLOR_RED, COLOR_BLACK);  // Fallback: red on black for error tint
            init_pair(NCURSES_PAIR_CODE_BLOCK, COLOR_WHITE, COLOR_BLACK);  // Fallback: code on black
            init_pair(NCURSES_PAIR_INLINE_CODE, COLOR_WHITE, COLOR_BLACK);  // Fallback: inline code on black
            init_pair(NCURSES_PAIR_H1_ACCENT, COLOR_CYAN, default_bg);  // Fallback: cyan for H1 accent
            init_pair(NCURSES_PAIR_STATUS_BG, COLOR_WHITE, default_bg);  // Fallback: white on default
        }
    } else {
        LOG_DEBUG("[TUI] No theme loaded, using standard ncurses colors");
        // Use standard ncurses color constants
        init_pair(NCURSES_PAIR_FOREGROUND, COLOR_WHITE, default_bg);
        init_pair(NCURSES_PAIR_USER, COLOR_GREEN, default_bg);
        init_pair(NCURSES_PAIR_ASSISTANT, COLOR_CYAN, default_bg);
        init_pair(NCURSES_PAIR_STATUS, COLOR_YELLOW, default_bg);
        init_pair(NCURSES_PAIR_ERROR, COLOR_RED, default_bg);
        init_pair(NCURSES_PAIR_PROMPT, COLOR_GREEN, default_bg);
        // Ensure tool pair is initialized; use magenta for distinction
        // Unify tool color with status color
        init_pair(NCURSES_PAIR_TOOL, COLOR_YELLOW, default_bg);
        // TODO color pairs
        init_pair(NCURSES_PAIR_TODO_COMPLETED, COLOR_GREEN, default_bg);
        init_pair(NCURSES_PAIR_TODO_IN_PROGRESS, COLOR_YELLOW, default_bg);
        init_pair(NCURSES_PAIR_TODO_PENDING, COLOR_MAGENTA, default_bg);  // Magenta - distinct from assistant cyan
        init_pair(NCURSES_PAIR_SEARCH, COLOR_MAGENTA, default_bg);  // Fallback: magenta for search highlights
        init_pair(NCURSES_PAIR_INPUT_BG, COLOR_WHITE, COLOR_BLACK);  // Fallback: white on black
        init_pair(NCURSES_PAIR_INPUT_BORDER, COLOR_GREEN, default_bg);  // Fallback: green border (user color)
        init_pair(NCURSES_PAIR_USER_MSG_BG, COLOR_WHITE, COLOR_BLACK);  // Fallback: user message background
        init_pair(NCURSES_PAIR_ASSISTANT_BG, COLOR_WHITE, default_bg);  // Fallback: no background (use default)
        init_pair(NCURSES_PAIR_ASSISTANT_BORDER_BG, COLOR_CYAN, default_bg);  // Fallback: cyan (assistant color)
        init_pair(NCURSES_PAIR_TOOL_DIM, COLOR_WHITE, default_bg);  // Fallback: foreground color for dimmed tool text
        init_pair(NCURSES_PAIR_DIFF_CONTEXT, COLOR_WHITE, default_bg);  // Fallback: foreground color for diff context
        init_pair(NCURSES_PAIR_GOAL, COLOR_WHITE, default_bg);  // Goal tag uses foreground color
        init_pair(NCURSES_PAIR_ERROR_BG, COLOR_RED, COLOR_BLACK);  // Fallback: red on black for error tint
        init_pair(NCURSES_PAIR_CODE_BLOCK, COLOR_WHITE, COLOR_BLACK);  // Fallback: code on black
        init_pair(NCURSES_PAIR_INLINE_CODE, COLOR_WHITE, COLOR_BLACK);  // Fallback: inline code on black
        init_pair(NCURSES_PAIR_H1_ACCENT, COLOR_CYAN, default_bg);  // Fallback: cyan for H1 accent
        init_pair(NCURSES_PAIR_STATUS_BG, COLOR_WHITE, default_bg);  // Fallback: white on default
    }
}

// Thread function to check vim-fugitive availability in background
static void* check_vim_fugitive_thread(void *arg) {
    TUIState *tui = (TUIState *)arg;
    if (!tui) return NULL;

    LOG_DEBUG("[TUI] Background thread checking vim-fugitive availability");

    // Check if vim-fugitive is available by running vim with a test command
    // On macOS, wrap with timeout to prevent hangs from credential prompts or
    // vim waiting for user input if misconfigured.
    // CRITICAL: redirect stdin from /dev/null. vim (spawned via popen) inherits
    // the terminal as stdin, and a blocking tty read by the child makes the main
    // thread's tcsetattr() (inside endwin() during a terminal resize) block
    // forever on macOS — the tty lock held by the pending read never releases.
    // This deadlocked the whole TUI on the first resize within ~5s of startup.
    char test_cmd[512];
#ifdef __APPLE__
    snprintf(test_cmd, sizeof(test_cmd),
             "timeout 5 vim -c \"if exists(':Git') | q | else | cquit 1 | endif\" -c \"q\" < /dev/null 2>&1");
#else
    snprintf(test_cmd, sizeof(test_cmd),
             "vim -c \"if exists(':Git') | q | else | cquit 1 | endif\" -c \"q\" < /dev/null 2>&1");
#endif

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

int tui_init(TUIState *tui, ConversationState *state) {
    if (!tui) return -1;

    // Store global pointer for input resize callback
    // Set locale for UTF-8 support
    setlocale(LC_ALL, "");

    // Log locale diagnostics for debugging encoding issues
    {
        const char *loc_name = setlocale(LC_CTYPE, NULL);
        const char *codeset = nl_langinfo(CODESET);
        const char *curses_ver = curses_version();
        LOG_DEBUG("[TUI] Locale: LC_CTYPE=%s codeset=%s MB_CUR_MAX=%d curses=%s",
                  loc_name ? loc_name : "(null)",
                  codeset ? codeset : "(null)",
                  (int)MB_CUR_MAX,
                  curses_ver ? curses_ver : "(null)");
    }

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
    mousemask(ALL_MOUSE_EVENTS, NULL);  // Intercept mouse events to prevent escape sequences from leaking into input buffer
    nodelay(stdscr, FALSE);  // Blocking input
    curs_set(2);  // Make cursor very visible (block cursor)

    // Enable bracketed paste mode (allows detecting pasted content)
    // ESC[?2004h enables, ESC[?2004l disables
    printf("\033[?2004h");
    fflush(stdout);

    // Configure paste heuristic (default: enabled). Only override when env provided
    const char *ph = getenv("TUI_PASTE_HEURISTIC");
    if (ph) {
        // Note: These variables are defined in tui.c, not tui_core.c
        // We don't control them here, just documenting that they exist
    }

    // Optional tuning for heuristic thresholds
    const char *gap = getenv("TUI_PASTE_GAP_MS");
    if (gap) {
        // These settings are handled in tui.c
        (void)gap;
    }
    const char *burst = getenv("TUI_PASTE_BURST_MIN");
    if (burst) {
        (void)burst;
    }
    const char *pto = getenv("TUI_PASTE_TIMEOUT_MS");
    if (pto) {
        (void)pto;
    }

    // Initialize colors from colorscheme
    tui_reload_colors();

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
    tui->last_assistant_line = -1;  // No assistant message yet
    tui->streaming_entry_index = -1;  // No active streaming entry
    tui->needs_conv_pad_rebuild = 0;   // No pending pad rebuild
    tui->status_message = NULL;
    tui->status_visible = 0;
    tui->status_spinner_active = 0;
    tui->status_spinner_frame = 0;
    tui->status_spinner_last_update_ns = 0;

    // Initialize pacman thinking style state
    tui->pacman_dots_eaten = 0;
    tui->pacman_direction = 1;
    tui->pacman_max_dots = 0;
    tui->pacman_anim_frame = 0;
    tui->pacman_last_step_ns = 0;

    // Initialize text diffusion effect
    text_diffusion_init(&tui->status_text_diffusion);

    // Initialize mode (start in INSERT mode for immediate input)
    tui->mode = TUI_MODE_INSERT;
    tui->normal_mode_last_key = 0;
    tui->auto_scroll_enabled = 1;     // Start with auto-scroll on

    // Initialize marks (a-z, all unset)
    memset(&tui->marks, 0, sizeof(tui->marks));
    for (int i = 0; i < MAX_MARKS; i++) {
        tui->marks.marks[i].line_number = -1;
        tui->marks.marks[i].name = (char)('a' + i);
        tui->marks.marks[i].is_set = 0;
    }
    tui->marks.pending = 0;

    // Initialize command palette state
    tui->cmd_palette_active = 0;
    tui->cmd_palette_filter = NULL;
    tui->cmd_palette_filter_len = 0;
    tui->cmd_palette_filter_capacity = 0;
    tui->cmd_palette_selected = 0;
    tui->cmd_palette_commands = NULL;
    tui->cmd_palette_count = 0;

    // Load config and apply input box style (default to horizontal if not found)
    KlawedConfig loaded_config;
    if (config_load(&loaded_config) == 0) {
        tui->input_box_style = loaded_config.input_box_style;
        tui->response_style = loaded_config.response_style;
        tui->thinking_style = loaded_config.thinking_style;
        tui->mascot_style = loaded_config.mascot_style;
        tui->wrap_enabled = loaded_config.wrap_enabled;
        LOG_DEBUG("[TUI] Loaded input_box_style from config: %s",
                  config_input_style_to_string(tui->input_box_style));
        LOG_DEBUG("[TUI] Loaded response_style from config: %s",
                  config_response_style_to_string(tui->response_style));
        LOG_DEBUG("[TUI] Loaded thinking_style from config: %s",
                  config_thinking_style_to_string(tui->thinking_style));
        LOG_DEBUG("[TUI] Loaded mascot_style from config: %s",
                  config_mascot_style_to_string(tui->mascot_style));
        LOG_DEBUG("[TUI] Loaded wrap_enabled from config: %d",
                  tui->wrap_enabled);
    } else {
        tui->input_box_style = INPUT_STYLE_BACKGROUND;
        tui->response_style = RESPONSE_STYLE_BORDER;
        tui->thinking_style = THINKING_STYLE_WAVE;
        tui->mascot_style = MASCOT_NYAN;
        tui->wrap_enabled = 1;
    }

    // Override mascot style from environment variable
    const char *mascot_env = getenv("KLAWED_MASCOT");
    if (mascot_env && mascot_env[0] != '\0') {
        tui->mascot_style = config_mascot_style_from_string(mascot_env);
        LOG_DEBUG("[TUI] Overriding mascot_style from KLAWED_MASCOT env: %s -> %s",
                  mascot_env, config_mascot_style_to_string(tui->mascot_style));
    }

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

    // Initialize command auto-complete state
    tui->cmd_autocomplete_active = 0;
    tui->cmd_autocomplete_filter = NULL;
    tui->cmd_autocomplete_selected = -1;
    tui->cmd_autocomplete_options = NULL;
    tui->cmd_autocomplete_count = 0;
    tui->cmd_autocomplete_prefix_type = 0;

    // Initialize input buffer
    if (tui_input_init(tui) != 0) {
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

    // Skip history file in no-storage mode (diagnostic for TUI hangs)
    if (data_dir_is_no_storage_mode()) {
        LOG_INFO("[TUI] History file skipped (KLAWED_NO_STORAGE=1)");
        tui->history_file = NULL;
    } else {
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
    }

    // Register resize handler (if available)
    tui_window_install_resize_handler();

    // Initialize vim-fugitive availability tracking
    tui->vim_fugitive_available = -1;  // Unknown state
    tui->vim_fugitive_mutex_initialized = 0;
    if (pthread_mutex_init(&tui->vim_fugitive_mutex, NULL) == 0) {
        tui->vim_fugitive_mutex_initialized = 1;
    } else {
        LOG_WARN("[TUI] Failed to initialize vim-fugitive mutex");
    }

    // Initialize tool output connection tracking
    tui->last_tool_name = NULL;

    // Initialize voice mode state
    tui->voice_mode = (VoiceModeState *)calloc(1, sizeof(VoiceModeState));
    if (tui->voice_mode) {
        voice_mode_init(tui->voice_mode, tui);
    } else {
        LOG_WARN("[TUI] Failed to allocate voice mode state");
    }

    tui->is_initialized = 1;

    LOG_DEBUG("[TUI] Initialized (screen=%dx%d, conv_h=%d, status_h=%d, input_h=%d)",
              tui->wm.screen_width, tui->wm.screen_height, tui->wm.conv_viewport_height,
              tui->wm.status_height, tui->wm.input_height);

    // Validate initial window setup
    tui_window_validate(tui);

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
    tui_conversation_free_entries(tui);

    // Free input state
    tui_input_free(tui);

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

    // Free command auto-complete state
    free(tui->cmd_autocomplete_filter);
    tui->cmd_autocomplete_filter = NULL;
    if (tui->cmd_autocomplete_options) {
        for (int i = 0; i < tui->cmd_autocomplete_count; i++) {
            free(tui->cmd_autocomplete_options[i]);
        }
        free(tui->cmd_autocomplete_options);
        tui->cmd_autocomplete_options = NULL;
    }
    tui->cmd_autocomplete_count = 0;
    tui->cmd_autocomplete_selected = -1;
    tui->cmd_autocomplete_active = 0;

    // Free command palette state
    free(tui->cmd_palette_filter);
    tui->cmd_palette_filter = NULL;
    tui->cmd_palette_filter_len = 0;
    tui->cmd_palette_filter_capacity = 0;
    tui->cmd_palette_active = 0;
    tui->cmd_palette_commands = NULL;
    tui->cmd_palette_count = 0;
    tui->cmd_palette_matched_count = 0;
    tui->cmd_palette_selected = 0;

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

    // Free tool name tracking
    free(tui->last_tool_name);
    tui->last_tool_name = NULL;

    // Free voice mode state
    if (tui->voice_mode) {
        voice_mode_cleanup(tui->voice_mode);
        free(tui->voice_mode);
        tui->voice_mode = NULL;
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

void tui_render_todo_list(TUIState *tui, const TodoList *list) {
    if (!tui || !list || list->count == 0) {
        return;  // No todos to display
    }

    // Render each todo item with its status-specific color
    for (size_t i = 0; i < list->count; i++) {
        const TodoItem *item = &list->items[i];
        char line[1024];
        TUIColorPair color;
        const char *symbol = NULL;
        const char *text = NULL;

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

void tui_show_startup_banner(TUIState *tui, const char *version, const char *model, const char *working_dir, const char *session_id) {
    if (!tui || !tui->is_initialized) return;

    // Defensive check: model and working_dir must not be NULL
    if (!model || !working_dir) {
        LOG_ERROR("[TUI] Cannot show startup banner: model or working_dir is NULL");
        return;
    }

    // Check if VLTRN mode is enabled
    const char *vltrn_mode = getenv("VLTRN_MODE");
    int is_vltrn = (vltrn_mode && strcmp(vltrn_mode, "1") == 0);

    const char *name = is_vltrn ? "vltrn" : "klawed";

    // Format banner: nyan cat on the right of info text, shifted one line up
    char info1[256];
    char info2[256];
    char info3[256];
    char info4[256];

    snprintf(info1, sizeof(info1), "%s v%s", name, version ? version : "?");
    snprintf(info2, sizeof(info2), "%s", model);
    snprintf(info3, sizeof(info3), "%s", working_dir);
    snprintf(info4, sizeof(info4), "%s", session_id ? session_id : "(no session)");

    // Add padding before banner
    tui_add_conversation_line(tui, NULL, "", COLOR_PAIR_FOREGROUND);

    // Add banner lines to conversation window (skip cat mascot in VLTRN mode)
    if (!is_vltrn) {
        if (tui->mascot_style == MASCOT_CLASSIC) {
            /* Classic cat: inline text alongside cat */
            char line1[256];
            char line2[256];
            char line3[256];
            char line4[256];
            snprintf(line1, sizeof(line1), "  /\\_/\\   %s v%s", name, version ? version : "?");
            snprintf(line2, sizeof(line2), " ( o.o )  %s", model);
            snprintf(line3, sizeof(line3), "  > ^ <   %s", working_dir);
            snprintf(line4, sizeof(line4), "          %s", session_id ? session_id : "(no session)");
            tui_add_conversation_line(tui, NULL, line1, COLOR_PAIR_ASSISTANT);
            tui_add_conversation_line(tui, NULL, line2, COLOR_PAIR_ASSISTANT);
            tui_add_conversation_line(tui, NULL, line3, COLOR_PAIR_ASSISTANT);
            tui_add_conversation_line(tui, NULL, line4, COLOR_PAIR_ASSISTANT);
        } else {
            /* Nyan cat: text on left, ~ wave trail, cat on right (shifted one line up) */
            if (tui->wm.screen_width >= 65) {
                char line1[256], line2[256], line3[256], line4[256], line5[256];
                const char *cats[] = {
                    " ,------------.",
                    "|  ` .` `. `. |",
                    "| `. ` `,^----^.",
                    "\\| .  `. | @ w @|",
                    " `v-v----\"-v-v-\""
                };
                const char *texts[] = { "", info1, info2, info3, info4 };
                char *lines[] = { line1, line2, line3, line4, line5 };

                /* Compute cat column: at least 15-char trail after longest text */
                size_t max_tlen = 0;
                for (int i = 0; i < 5; i++) {
                    size_t tlen = strlen(texts[i]);
                    if (tlen > max_tlen) max_tlen = tlen;
                }
                size_t cat_col = max_tlen + 16;  /* +1 space, +15 trail chars */
                if (cat_col < 30) cat_col = 30;  /* minimum cat position */

                for (int i = 0; i < 5; i++) {
                    size_t pos = 0;
                    size_t tlen = strlen(texts[i]);
                    if (tlen > 0) {
                        strlcpy(lines[i], texts[i], 256);
                        pos = tlen;
                        if (pos < 255) lines[i][pos++] = ' ';
                    }
                    /* Fill ~ wave trail to cat_col */
                    int ti = 0;
                    while (pos < cat_col && pos < 255) {
                        lines[i][pos] = "~ "[ti % 2];
                        pos++;
                        ti++;
                    }
                    /* Space + cat */
                    if (pos < 255) lines[i][pos++] = ' ';
                    strlcpy(lines[i] + pos, cats[i], 256 - pos);
                }

                tui_add_conversation_line(tui, NULL, line1, COLOR_PAIR_ASSISTANT);
                tui_add_conversation_line(tui, NULL, line2, COLOR_PAIR_ASSISTANT);
                tui_add_conversation_line(tui, NULL, line3, COLOR_PAIR_ASSISTANT);
                tui_add_conversation_line(tui, NULL, line4, COLOR_PAIR_ASSISTANT);
                tui_add_conversation_line(tui, NULL, line5, COLOR_PAIR_ASSISTANT);
            } else {
                /* Narrow screen: stacked, no blank line between */
                tui_add_conversation_line(tui, NULL, " ,------------.", COLOR_PAIR_ASSISTANT);
                tui_add_conversation_line(tui, NULL, " |  ` .` `. `. |", COLOR_PAIR_ASSISTANT);
                tui_add_conversation_line(tui, NULL, " | `. ` `,^----^.", COLOR_PAIR_ASSISTANT);
                tui_add_conversation_line(tui, NULL, "\\| .  `. | @ w @|", COLOR_PAIR_ASSISTANT);
                tui_add_conversation_line(tui, NULL, " `v-v----\"-v-v-\"", COLOR_PAIR_ASSISTANT);
                tui_add_conversation_line(tui, NULL, info1, COLOR_PAIR_ASSISTANT);
                tui_add_conversation_line(tui, NULL, info2, COLOR_PAIR_ASSISTANT);
                tui_add_conversation_line(tui, NULL, info3, COLOR_PAIR_ASSISTANT);
                tui_add_conversation_line(tui, NULL, info4, COLOR_PAIR_ASSISTANT);
            }
        }
    }
    tui_add_conversation_line(tui, NULL, "", COLOR_PAIR_FOREGROUND);  // Blank line

    // Tips array: randomly select one to display at startup
    static const char *tips[] = {
        "Esc/Ctrl+[ to enter Scroll mode (vim-style); press 'i' to insert.",
        "In Scroll mode, Scroll: j/k (line), Ctrl+D/U (half page), gg/G (top/bottom).",
        "In Scroll mode, use ( and ) to jump between text blocks (paragraphs).",
        "Press Shift+Tab to toggle Plan mode (read-only tools only).",
        "Press Ctrl+C to cancel a running API/tool action.",
        "In Normal mode, :!cmd runs a shell command in the current dir (like Vim).",
        "In Normal mode, :re !cmd puts the command output into the input box.",
        "In Normal mode, :git opens vim-fugitive (requires vim-fugitive plugin).",
        "Press Ctrl+D to exit quickly.",
        "Set KLAWED_THEME to change colors. Try light themes: atom-one-light, pencil-light, solarized-light, tomorrow.",
        "Set KLAWED_LOG_LEVEL=DEBUG for verbose logs.",
        "API history stored in ./.klawed/api_calls.db (configurable via KLAWED_DB_PATH).",
        "Insert mode supports readline keys: Ctrl+A, Ctrl+E, Alt+B, Alt+F.",
        "Interrupt long tool runs any time with Ctrl+C.",
        "Press Ctrl+F to open file search popup (fuzzy find files, supports Alt+B/F/D/⌫).",
        "Press Ctrl+R to open history search popup (fuzzy find previous commands).",
        "MCP is disabled by default; enable with KLAWED_MCP_ENABLED=1 and configure servers in ~/.klawed.",
        "Use /clear to clear conversation; /quit or /exit to leave.",
        "Use :help to see all available commands.",
        "Token usage stats shown in status bar when in Normal mode (Esc).",
        "Exit methods: Ctrl+D, /quit, or /exit.",
        "Tmux users: set -g allow-rename on; set -g automatic-rename off; set -g pane-border-status top in tmux.conf to see pane titles."
    };
    size_t tips_count = sizeof(tips) / sizeof(tips[0]);

    // Compute a simple per-process pseudo-random index without relying on global srand
    unsigned int seed = (unsigned int)(time(NULL) ^ getpid());
    size_t tip_index = tips_count ? (seed % tips_count) : 0;
    char tip_line[512];
    snprintf(tip_line, sizeof(tip_line), "Tip: %s", tips[tip_index]);

    tui_add_conversation_line(tui, NULL, tip_line, COLOR_PAIR_STATUS);
    tui_add_conversation_line(tui, NULL, "", COLOR_PAIR_FOREGROUND);

    /* If running inside tmux, show a one-time config reminder.
     * Tmux blocks escape-sequence renames by default (allow-rename is off).
     * Users need these settings for pane titles to appear:
     *   set -g allow-rename on         (allow apps to rename panes)
     *   set -g automatic-rename off    (don't overwrite our custom name)
     *   set -g pane-border-status top  (show per-pane titles)
     */
    {
        const char *tmux_env = getenv("TMUX");
        if (tmux_env && tmux_env[0] != '\0') {
            tui_add_conversation_line(tui, NULL,
                "TMUX detected: pane titles require "
                "'set -g allow-rename on; set -g automatic-rename off; set -g pane-border-status top' "
                "in your tmux.conf. See docs/tmux-integration.md.",
                COLOR_PAIR_TOOL_DIM);
            tui_add_conversation_line(tui, NULL, "", COLOR_PAIR_FOREGROUND);
        }
    }
}

// Simple cache for ANSI color pairs to avoid recreating them
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
