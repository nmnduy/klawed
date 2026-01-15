/*
 * TUI Tab Completion Functionality
 *
 * Implements tab completion for vim-style commands and slash commands.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "tui_completion.h"
#include "tui.h"
#include "tui_input.h"
#include "commands.h"
#include "array_resize.h"
#include "logger.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

// List of available vim-style commands for tab completion
static const char* vim_commands[] = {
    "q",
    "quit",
    "w",
    "write",
    "wq",
    "noh",
    "nohlsearch",
    NULL  // Sentinel
};

// Find matching vim-style commands for tab completion
// Returns: number of matches found, fills matches array (up to max_matches)
int tui_find_command_matches(const char *prefix, const char **matches, int max_matches) {
    if (!prefix || !matches || max_matches <= 0) {
        return 0;
    }

    int prefix_len = (int)strlen(prefix);
    int match_count = 0;

    // If prefix is empty (just ":"), don't return all commands
    if (prefix_len == 0) {
        return 0;
    }

    // Find all commands that start with the prefix
    for (int i = 0; vim_commands[i] != NULL && match_count < max_matches; i++) {
        if (strncmp(vim_commands[i], prefix, (size_t)prefix_len) == 0) {
            matches[match_count++] = vim_commands[i];
        }
    }

    return match_count;
}

// Handle tab completion for commands starting with '/'
int tui_handle_tab_completion(TUIState *tui, const char *prompt) {
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

        // Redraw input
        tui_redraw_input(tui, prompt);
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
