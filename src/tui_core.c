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
#include <stdlib.h>
#include <bsd/stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <locale.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define INPUT_BUFFER_SIZE 8192
#define INPUT_WIN_MIN_HEIGHT 2
#define INPUT_WIN_MAX_HEIGHT_PERCENT 20
#define CONV_WIN_PADDING 0
#define STATUS_WIN_HEIGHT 1

// Convert RGB (0-255) to ncurses color (0-1000)
static short rgb_to_ncurses(int value) {
    return (short)((value * 1000) / 255);
}

// Initialize ncurses color pairs from theme
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

            // Input background color (theme background with subtle user color tint)
            // Blend 5% of user color into background for subtle highlight
            int bg_r = (g_theme.background_rgb.r * 95 + g_theme.user_rgb.r * 5) / 100;
            int bg_g = (g_theme.background_rgb.g * 95 + g_theme.user_rgb.g * 5) / 100;
            int bg_b = (g_theme.background_rgb.b * 95 + g_theme.user_rgb.b * 5) / 100;
            init_color(23,
                rgb_to_ncurses(bg_r),
                rgb_to_ncurses(bg_g),
                rgb_to_ncurses(bg_b));

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
            init_pair(NCURSES_PAIR_USER_MSG_BG, 16, 23);       // User message background (same as input bg)

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
            init_pair(NCURSES_PAIR_USER_MSG_BG, (short)fg_idx, (short)236);   // User message background (same as input bg)

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
            init_pair(NCURSES_PAIR_USER_MSG_BG, COLOR_WHITE, COLOR_BLACK);  // Fallback: user message background
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
        init_pair(NCURSES_PAIR_USER_MSG_BG, COLOR_WHITE, COLOR_BLACK);  // Fallback: user message background
    }
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
    tui_window_install_resize_handler();

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

void tui_show_startup_banner(TUIState *tui, const char *version, const char *model, const char *working_dir) {
    if (!tui || !tui->is_initialized) return;

    // Format banner lines with ASCII art cat mascot
    char line1[256];
    char line2[256];
    char line3[256];
    char tip_line[512];

    // Create content lines without box borders
    snprintf(line1, sizeof(line1), "  /\\_/\\   klawed v%s", version ? version : "?");
    snprintf(line2, sizeof(line2), " ( o.o )  %s", model);
    snprintf(line3, sizeof(line3), "  > ^ <    %s", working_dir);

    // Add padding before mascot
    tui_add_conversation_line(tui, NULL, "", COLOR_PAIR_FOREGROUND);

    // Add banner lines to conversation window (without box)
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
