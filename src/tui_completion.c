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
    "clear",
    "help",
    "vim",
    "git",
    NULL  // Sentinel
};

// Common bash commands for autocomplete
static const char* bash_commands[] = {
    "ls", "cd", "pwd", "cat", "grep", "find", "echo", "touch", "mkdir", "rm",
    "mv", "cp", "chmod", "chown", "ps", "kill", "top", "df", "du", "tar",
    "gzip", "gunzip", "zip", "unzip", "ssh", "scp", "curl", "wget", "git",
    "make", "gcc", "python", "python3", "node", "npm", "pip", "vim", "nano",
    "emacs", "less", "more", "head", "tail", "wc", "sort", "uniq", "diff",
    "patch", "sed", "awk", "xargs", "basename", "dirname", "whoami", "date",
    "history", "alias", "export", "source", "bash", "sh", "man", "which",
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

// Find matching bash commands for tab completion
// Returns: number of matches found, fills matches array (up to max_matches)
int tui_find_bash_command_matches(const char *prefix, const char **matches, int max_matches) {
    if (!prefix || !matches || max_matches <= 0) {
        return 0;
    }

    int prefix_len = (int)strlen(prefix);
    int match_count = 0;

    // If prefix is empty, don't return all commands
    if (prefix_len == 0) {
        return 0;
    }

    // Find all bash commands that start with the prefix
    for (int i = 0; bash_commands[i] != NULL && match_count < max_matches; i++) {
        if (strncmp(bash_commands[i], prefix, (size_t)prefix_len) == 0) {
            matches[match_count++] = bash_commands[i];
        }
    }

    return match_count;
}

// Generic completion helper - replaces the word at cursor with completion
static int apply_completion(TUIState *tui, const char *completion, const char *prompt) {
    if (!tui || !tui->input_buffer || !completion) {
        return 0;
    }

    TUIInputBuffer *input = tui->input_buffer;
    int optlen = (int)strlen(completion);

    // Find start of current word (backtrack to space, ':', '!' or beginning)
    int start = input->cursor - 1;
    while (start >= 0 && input->buffer[start] != ' ' && input->buffer[start] != '\t' &&
           input->buffer[start] != ':' && input->buffer[start] != '!') {
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
    memcpy(input->buffer + start, completion, (size_t)optlen);
    input->cursor = start + optlen;
    input->length = start + optlen + tail_len;
    input->buffer[input->length] = '\0';

    // Redraw input
    tui_redraw_input(tui, prompt);
    return 1;
}

// Handle tab completion for commands starting with '/'
int tui_handle_tab_completion(TUIState *tui, const char *prompt) {
    if (!tui || !tui->input_buffer || !tui->input_buffer->buffer) {
        return 0;
    }

    TUIInputBuffer *input = tui->input_buffer;

    // Handle vim commands starting with ':'
    if (input->buffer[0] == ':') {
        // Check if it's a bash command: :!<cmd>
        if (input->length >= 2 && input->buffer[1] == '!') {
            // Extract the bash command part (after :!)
            // bash_cmd would be: input->buffer + 2
            
            // Find the start of the word at cursor
            int word_start = input->cursor - 1;
            while (word_start > 1 && input->buffer[word_start] != ' ' && 
                   input->buffer[word_start] != '\t' && input->buffer[word_start] != '!') {
                word_start--;
            }
            if (input->buffer[word_start] == ' ' || input->buffer[word_start] == '\t' || 
                input->buffer[word_start] == '!') {
                word_start++;
            }
            
            // Extract prefix for completion
            int prefix_len = input->cursor - word_start;
            if (prefix_len < 0) prefix_len = 0;
            if (prefix_len > 63) prefix_len = 63;
            
            char prefix[64];
            if (prefix_len > 0) {
                memcpy(prefix, input->buffer + word_start, (size_t)prefix_len);
            }
            prefix[prefix_len] = '\0';
            
            // Find matching bash commands
            const char *matches[64];
            int match_count = tui_find_bash_command_matches(prefix, matches, 64);
            
            if (match_count == 0) {
                beep();
                return 0;
            } else if (match_count == 1) {
                // Single match - apply completion
                return apply_completion(tui, matches[0], prompt);
            } else {
                // Multiple matches - for now just beep
                // TODO: Show completion list
                beep();
                return 0;
            }
        }
        
        // Regular vim command (e.g., :q, :w, :quit)
        // Extract command part (after :)
        const char *cmd = input->buffer + 1;
        
        // Find where the command name ends (space or end of string)
        int cmd_len = 0;
        while (cmd[cmd_len] != '\0' && cmd[cmd_len] != ' ' && cmd[cmd_len] != '\t') {
            cmd_len++;
        }
        
        // Only complete if cursor is within the command name
        if (input->cursor <= cmd_len + 1) {  // +1 for the ':'
            // Find matching vim commands
            const char *matches[32];
            int match_count = tui_find_command_matches(cmd, matches, 32);
            
            if (match_count == 0) {
                beep();
                return 0;
            } else if (match_count == 1) {
                // Single match - apply completion
                return apply_completion(tui, matches[0], prompt);
            } else {
                // Multiple matches - for now just beep
                // TODO: Show completion list
                beep();
                return 0;
            }
        }
        
        return 0;
    }

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
