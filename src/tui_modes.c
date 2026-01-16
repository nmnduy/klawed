/*
 * TUI Mode Handling
 *
 * Manages different input modes (Vim-like):
 * - NORMAL mode: Navigation and commands
 * - INSERT mode: Text input
 * - COMMAND mode: Command execution
 * - SEARCH mode: Search pattern input
 */

// Define feature test macros before any includes
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "tui_modes.h"
#include "tui.h"
#include "tui_input.h"
#include "tui_paste.h"
#include "tui_completion.h"
#include "tui_search.h"
#include "tui_window.h"
#include "tui_history.h"
#include "tui_conversation.h"
#include "file_search.h"
#include "history_search.h"
#include "window_manager.h"
#include "message_queue.h"
#include "logger.h"
#include "klawed_internal.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ncurses.h>
#include <stdio.h>
#include <limits.h>
#include <bsd/string.h>

// Forward declarations of internal helper functions from tui.c
// These are used but defined elsewhere - we need to access them
extern int buffer_reserve(void **buffer, size_t *capacity, size_t new_capacity);

/*
 * Handle input in COMMAND mode
 * Returns:
 *   0 = character processed
 *   1 = command executed
 *  -1 = error or quit signal
 */
int tui_modes_handle_command(TUIState *tui, int ch, const char *prompt) {
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
    } else if (ch == '\t') {  // Tab - command completion
        // Get the current command (without ':' prefix)
        const char *cmd = tui->command_buffer + 1;

        // Find matching commands
        const char *matches[32];
        int match_count = tui_find_command_matches(cmd, matches, 32);

        if (match_count == 0) {
            // No matches, beep
            beep();
        } else if (match_count == 1) {
            // Single match - complete it
            const char *completion = matches[0];
            size_t completion_len = strlen(completion);

            // Check if we have space in the buffer
            if (completion_len + 2 <= (size_t)tui->command_buffer_capacity) {  // +2 for ':' and '\0'
                tui->command_buffer[0] = ':';
                strlcpy(tui->command_buffer + 1, completion, (size_t)(tui->command_buffer_capacity - 1));
                tui->command_buffer_len = (int)completion_len + 1;
                input_redraw(tui, prompt);
            }
        } else {
            // Multiple matches - find common prefix
            size_t common_len = strlen(matches[0]);
            for (int i = 1; i < match_count; i++) {
                size_t j = 0;
                while (j < common_len && matches[0][j] == matches[i][j]) {
                    j++;
                }
                common_len = j;
            }

            // If common prefix is longer than current input, complete to common prefix
            size_t current_len = strlen(cmd);
            if (common_len > current_len) {
                // Complete to common prefix
                if (common_len + 2 <= (size_t)tui->command_buffer_capacity) {  // +2 for ':' and '\0'
                    tui->command_buffer[0] = ':';
                    memcpy(tui->command_buffer + 1, matches[0], common_len);
                    tui->command_buffer[common_len + 1] = '\0';
                    tui->command_buffer_len = (int)common_len + 1;
                    input_redraw(tui, prompt);
                }
            } else {
                // Show available options in status line
                char status_msg[512];
                size_t pos = 0;

                // Build message with all matches
                const char *prefix = "Available: ";
                strlcpy(status_msg, prefix, sizeof(status_msg));
                pos = strlen(status_msg);

                for (int i = 0; i < match_count && pos < sizeof(status_msg) - 10; i++) {
                    if (i > 0) {
                        if (pos + 2 < sizeof(status_msg)) {
                            strlcat(status_msg, ", ", sizeof(status_msg));
                            pos = strlen(status_msg);
                        }
                    }
                    size_t match_len = strlen(matches[i]);
                    if (pos + match_len < sizeof(status_msg) - 1) {
                        strlcat(status_msg, matches[i], sizeof(status_msg));
                        pos = strlen(status_msg);
                    }
                }

                tui_update_status(tui, status_msg);
                beep();
            }
        }
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

/*
 * Handle input in SEARCH mode
 * Returns:
 *   0 = character processed
 *   1 = search executed
 *  -1 = error or cancelled
 */
int tui_modes_handle_search(TUIState *tui, int ch, const char *prompt) {
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
            tui_search_perform(tui, tui->search_buffer, tui->search_direction);

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

/*
 * Handle input in NORMAL mode
 * Returns:
 *   0 = character processed
 *   1 = submit input (Enter)
 *   2 = interrupt (Ctrl+C)
 *   3 = exit (Ctrl+D)
 *  -1 = error
 */
int tui_modes_handle_normal(TUIState *tui, int ch, const char *prompt, void *user_data) {
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
        (void)pad_w;  // Suppress unused variable warning
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
                tui_search_perform(tui, tui->last_search_pattern, tui->search_direction);
                redraw_conversation(tui);
                input_redraw(tui, prompt);
            } else {
                // No previous search
                tui_update_status(tui, "No previous search pattern");
            }
            break;

        case 'N':  // Repeat search in opposite direction
            if (tui->last_search_pattern) {
                tui_search_perform(tui, tui->last_search_pattern, -tui->search_direction);
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
                int next_para = tui_search_find_next_paragraph(tui->wm.conv_pad, current_scroll, content_lines);

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
                int prev_para = tui_search_find_prev_paragraph(tui->wm.conv_pad, current_scroll, content_lines);

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
            // Cycle through background -> border -> bland -> background
            if (tui->input_box_style == INPUT_STYLE_BACKGROUND) {
                tui->input_box_style = INPUT_STYLE_BORDER;
                tui_update_status(tui, "Input box style: border");
            } else if (tui->input_box_style == INPUT_STYLE_BORDER) {
                tui->input_box_style = INPUT_STYLE_BLAND;
                tui_update_status(tui, "Input box style: bland");
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
