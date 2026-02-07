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
#include "tui_render.h"
#define COLORSCHEME_EXTERN
#include "colorscheme.h"
#include "history_search.h"
#include "fallback_colors.h"
#include "logger.h"
#include "indicators.h"
#include "spinner_effects.h"
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

// ============================================================================
// Helper Functions for Event Loop
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

static void status_spinner_tick(TUIState *tui) {
    if (!tui || !tui->status_spinner_active || !tui->status_visible) {
        return;
    }
    if (tui->wm.status_height <= 0 || !tui->wm.status_win) {
        return;
    }

    uint64_t now = monotonic_time_ns();

    // Initialize spring on first tick
    if (!tui->status_spinner_spring_initialized) {
        tui->status_spinner_pos = 0.0;
        tui->status_spinner_vel = 0.0;
        // 60 FPS, fast angular frequency (15 Hz), bouncy damping (0.25) for playful motion
        spring_init(&tui->status_spinner_spring, 1.0/60.0, 15.0, 0.25);
        tui->status_spinner_spring_initialized = 1;
        tui->status_spinner_last_update_ns = now;
        return;
    }

    // Calculate time delta in seconds
    double delta_s = (double)(now - tui->status_spinner_last_update_ns) / 1e9;
    if (delta_s > 0.1) {
        // Cap large deltas to prevent instability
        delta_s = 0.1;
    }

    // Target angle increases continuously for spinning effect
    // Use a slightly randomized angular velocity to add organic feel
    // ~40 rad/s base speed = ~6 rotations per second
    double angular_velocity = 40.0;
    double target_pos = tui->status_spinner_pos + delta_s * angular_velocity;

    // Update spring with scaled time step
    spring_update(tui->status_spinner_spring, &tui->status_spinner_pos, &tui->status_spinner_vel, target_pos);

    // Convert angular position to frame index
    const spinner_variant_t *variant = status_spinner_variant();
    int frame_count = (variant->count > 0) ? variant->count : SPINNER_FRAME_COUNT;
    if (frame_count <= 0) {
        return;
    }

    // Modulo arithmetic for smooth wrapping
    double frame_pos = fmod(tui->status_spinner_pos, (double)frame_count);
    if (frame_pos < 0) {
        frame_pos += frame_count;
    }

    tui->status_spinner_frame = (int)(frame_pos + 0.5); // Round to nearest frame

    // Update spinner effect phase
    float delta_time_float = (float)delta_s;
    spinner_effect_update_phase(&tui->status_spinner_effect,
                               delta_time_float,
                               tui->status_spinner_frame,
                               frame_count);

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
// Helper: Render text with search highlighting
// Returns number of characters rendered
// Helper: Render a single conversation entry to the pad
// Returns 0 on success, -1 on error
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

    // Show mascot banner first (useful info like current directory)
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

    // Add a system message indicating the clear (after mascot)
    tui_add_conversation_line(tui, "[System]", "Conversation history cleared", COLOR_PAIR_STATUS);

    // Refresh all windows to ensure consistent state
    window_manager_refresh_all(&tui->wm);
}

// Redraw the entire conversation from stored entries
// This is useful for applying search highlighting after a search
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
        // Handle tab completion for commands starting with '/' or ':'
        if (input->buffer && (input->buffer[0] == '/' || input->buffer[0] == ':')) {
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
    // Check for circle prefix (● = UTF-8: 0xE2 0x97 0x8F) which indicates tool
    if ((unsigned char)prefix[0] == 0xE2 &&
        (unsigned char)prefix[1] == 0x97 &&
        (unsigned char)prefix[2] == 0x8F) {
        return COLOR_PAIR_TOOL;
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

            // Check for circle prefix (● = UTF-8: 0xE2 0x97 0x8F) which indicates tool
            if ((unsigned char)mutable_text[0] == 0xE2 &&
                (unsigned char)mutable_text[1] == 0x97 &&
                (unsigned char)mutable_text[2] == 0x8F) {
                // Format is "● ToolName details"
                // Find the space after the tool name
                const char *after_circle = mutable_text + 3;  // Skip the ● (3 bytes)
                while (*after_circle == ' ') after_circle++;  // Skip space after ●

                // Find end of tool name (next space)
                const char *tool_name_end = after_circle;
                while (*tool_name_end && *tool_name_end != ' ') tool_name_end++;

                // Build prefix "● ToolName"
                size_t prefix_len = (size_t)(tool_name_end - mutable_text);
                char *prefix = malloc(prefix_len + 1);
                if (prefix) {
                    memcpy(prefix, mutable_text, prefix_len);
                    prefix[prefix_len] = '\0';
                }

                // Content starts after tool name
                const char *content_start = tool_name_end;
                while (*content_start == ' ') content_start++;

                tui_add_conversation_line(
                    tui,
                    prefix ? prefix : "",
                    content_start,
                    COLOR_PAIR_TOOL);

                free(prefix);
                break;
            }

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
            TUIColorPair diff_color = COLOR_PAIR_DIFF_CONTEXT;  // Dimmed gray for context lines
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

            // Re-render file search popup if active (must be on top after resize)
            if (tui->mode == TUI_MODE_FILE_SEARCH && tui->file_search.is_active) {
                file_search_render(&tui->file_search);
            }
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

        // After spinner update, ensure cursor stays in input window
        // status_spinner_tick uses wnoutrefresh, so we need to sync the screen
        // and ensure the cursor is positioned in the input window, not the status bar
        if (tui->status_spinner_active && tui->wm.input_win && tui->mode != TUI_MODE_NORMAL) {
            // Use wnoutrefresh to update virtual screen without moving physical cursor
            wnoutrefresh(tui->wm.input_win);
            // Sync virtual screen to physical, cursor will be at input window's position
            doupdate();
        }

        // Render TODO banner if there are incomplete todos
        // This shows/hides the TODO window as needed
        if (tui->conversation_state && tui->conversation_state->todo_list) {
            tui_render_todo_banner(tui, tui->conversation_state->todo_list);
        }

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
            // Re-render file search popup if active (must be on top)
            if (tui->mode == TUI_MODE_FILE_SEARCH && tui->file_search.is_active) {
                file_search_render(&tui->file_search);
            }
        }
    } while (processed > 0);
}
