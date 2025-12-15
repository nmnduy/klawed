/**
 * paste_handler.h
 *
 * Bracketed paste mode support for detecting and safely handling
 * large pastes in terminal applications.
 *
 * Modern terminals support bracketed paste mode (xterm, iTerm2, kitty, etc.)
 * which wraps pasted content in escape sequences:
 *   - Paste start: ESC[200~
 *   - Paste end:   ESC[201~
 */

#ifndef PASTE_HANDLER_H
#define PASTE_HANDLER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/time.h>

// Configuration constants
#define PASTE_BUFFER_SIZE (1024 * 1024)  // 1MB max paste size
#define PASTE_WARN_THRESHOLD 500          // Warn if paste > 500 bytes
#define PASTE_TIME_BURST_MS 50           // Time window for burst detection
#define PASTE_BURST_CHARS 10             // Chars in burst = paste

// Bracketed paste mode escape sequences
#define BRACKETED_PASTE_START "\033[200~"
#define BRACKETED_PASTE_END "\033[201~"
#define ENABLE_BRACKETED_PASTE "\033[?2004h"
#define DISABLE_BRACKETED_PASTE "\033[?2004l"

/**
 * Paste detection state
 */
typedef struct {
    int in_paste;                    // Currently receiving a paste
    char *buffer;                    // Paste buffer
    size_t buffer_size;              // Current buffer size
    size_t buffer_capacity;          // Allocated capacity
    struct timeval last_char_time;   // For time-based detection
    int chars_in_burst;              // Burst detection counter
} PasteState;

/**
 * Paste sanitization options
 */
typedef struct {
    int remove_control_chars;        // Remove non-printable chars (except \n, \t)
    int normalize_newlines;          // Convert \r\n to \n
    int trim_whitespace;             // Trim leading/trailing whitespace
    int collapse_multiple_newlines;  // Collapse multiple newlines to 2 max
} PasteSanitizeOptions;

/**
 * Initialize paste state
 */
static PasteState* paste_state_init(void) {
    PasteState *state = calloc(1, sizeof(PasteState));
    if (!state) return NULL;

    state->buffer_capacity = PASTE_BUFFER_SIZE;
    state->buffer = malloc(state->buffer_capacity);
    if (!state->buffer) {
        free(state);
        return NULL;
    }

    state->in_paste = 0;
    state->buffer_size = 0;
    state->chars_in_burst = 0;
    gettimeofday(&state->last_char_time, NULL);

    return state;
}

/**
 * Free paste state
 */
static void paste_state_free(PasteState *state) {
    if (!state) return;
    free(state->buffer);
    free(state);
}

/**
 * Reset paste buffer
 */
static void paste_state_reset(PasteState *state) {
    if (!state) return;
    state->buffer_size = 0;
    state->in_paste = 0;
    state->chars_in_burst = 0;
    gettimeofday(&state->last_char_time, NULL);
}

/**
 * Enable bracketed paste mode in terminal
 */
__attribute__((unused))
static void enable_bracketed_paste(void) {
    printf(ENABLE_BRACKETED_PASTE);
    fflush(stdout);
}

/**
 * Disable bracketed paste mode in terminal
 */
__attribute__((unused))
static void disable_bracketed_paste(void) {
    printf(DISABLE_BRACKETED_PASTE);
    fflush(stdout);
}

/**
 * Detect paste by timing (fallback for terminals without bracketed paste)
 * Returns 1 if rapid input burst detected (likely paste)
 */
__attribute__((unused))
static int detect_paste_by_timing(PasteState *state) {
    if (!state) return 0;

    struct timeval now;
    gettimeofday(&now, NULL);

    long elapsed_ms = (now.tv_sec - state->last_char_time.tv_sec) * 1000 +
                      (now.tv_usec - state->last_char_time.tv_usec) / 1000;

    if (elapsed_ms < PASTE_TIME_BURST_MS) {
        state->chars_in_burst++;
        if (state->chars_in_burst >= PASTE_BURST_CHARS) {
            return 1; // Paste detected
        }
    } else {
        state->chars_in_burst = 1; // Reset burst counter
    }

    state->last_char_time = now;
    return 0;
}

/**
 * Add character to paste buffer
 * Returns 0 on success, -1 on buffer overflow
 */
static int paste_buffer_add_char(PasteState *state, char c) {
    if (!state) return -1;

    if (state->buffer_size >= state->buffer_capacity - 1) {
        // Buffer full
        return -1;
    }

    state->buffer[state->buffer_size++] = c;
    return 0;
}

/**
 * Sanitize pasted content
 * Modifies buffer in-place, returns new length
 */
static size_t paste_sanitize(char *buffer, size_t len, PasteSanitizeOptions *opts) {
    if (!buffer || len == 0) return 0;

    // Default options
    PasteSanitizeOptions default_opts = {
        .remove_control_chars = 1,
        .normalize_newlines = 1,
        .trim_whitespace = 1,
        .collapse_multiple_newlines = 1
    };
    if (!opts) opts = &default_opts;

    size_t read_pos = 0, write_pos = 0;
    int newline_count = 0;

    // Skip leading whitespace if trimming
    if (opts->trim_whitespace) {
        while (read_pos < len && isspace((unsigned char)buffer[read_pos])) {
            read_pos++;
        }
    }

    while (read_pos < len) {
        char c = buffer[read_pos++];

        // Normalize \r\n to \n
        if (opts->normalize_newlines && c == '\r') {
            if (read_pos < len && buffer[read_pos] == '\n') {
                read_pos++; // Skip \n, will add below
            }
            c = '\n';
        }

        // Handle newlines
        if (c == '\n') {
            newline_count++;
            if (opts->collapse_multiple_newlines && newline_count > 2) {
                continue; // Skip excessive newlines
            }
            buffer[write_pos++] = c;
        }
        // Handle printable chars, tabs
        else if ((c >= 32 && c < 127) || c == '\t') {
            newline_count = 0;
            buffer[write_pos++] = c;
        }
        // Handle control characters
        else if (!opts->remove_control_chars) {
            buffer[write_pos++] = c;
        }
        // Else: skip control character
    }

    // Trim trailing whitespace
    if (opts->trim_whitespace) {
        while (write_pos > 0 && isspace((unsigned char)buffer[write_pos - 1])) {
            write_pos--;
        }
    }

    buffer[write_pos] = '\0';
    return write_pos;
}

/**
 * Get preview of pasted content (first N chars)
 */
__attribute__((unused))
static char* paste_get_preview(const char *content, size_t len, size_t preview_len) {
    if (!content || len == 0) return NULL;

    size_t actual_len = (len < preview_len) ? len : preview_len;
    char *preview = malloc(actual_len + 4); // +4 for "..."
    if (!preview) return NULL;

    if (len > preview_len) {
        // Use snprintf for safe bounds-checked string formatting
        snprintf(preview, actual_len + 4, "%.*s...", (int)actual_len, content);
    } else {
        // Use snprintf for safe bounds-checked string copying
        snprintf(preview, actual_len + 1, "%.*s", (int)actual_len, content);
    }

    return preview;
}

/**
 * Check if sequence matches bracketed paste start
 * Returns number of characters consumed (6 if match, 0 if no match)
 */
static int check_paste_start_sequence(const char *buffer, size_t len) {
    if (len < 6) return 0;
    if (strncmp(buffer, BRACKETED_PASTE_START, 6) == 0) {
        return 6;
    }
    return 0;
}

/**
 * Check if sequence matches bracketed paste end
 * Returns number of characters consumed (6 if match, 0 if no match)
 */
static int check_paste_end_sequence(const char *buffer, size_t len) {
    if (len < 6) return 0;
    if (strncmp(buffer, BRACKETED_PASTE_END, 6) == 0) {
        return 6;
    }
    return 0;
}

/**
 * Process character for paste detection
 * Returns:
 *   0 = normal character, not in paste
 *   1 = paste started
 *   2 = paste in progress, character buffered
 *   3 = paste ended, buffer contains complete paste
 *  -1 = buffer overflow
 */
__attribute__((unused))
static int paste_process_char(PasteState *state, char c) {
    if (!state) return 0;

    // Not in paste - check for start sequence
    if (!state->in_paste) {
        if (c == '\033') {
            // Might be start of bracketed paste sequence
            // Buffer this and wait for more chars
            paste_buffer_add_char(state, c);
            return 2; // Buffering potential sequence
        }

        // Check if buffer contains paste start sequence
        if (state->buffer_size > 0) {
            paste_buffer_add_char(state, c);

            int consumed = check_paste_start_sequence(state->buffer, state->buffer_size);
            if (consumed > 0) {
                // Paste started! Clear the escape sequence from buffer
                state->buffer_size = 0;
                state->in_paste = 1;
                return 1;
            }

            // Not a paste sequence - might be other escape sequence
            // If buffer has incomplete sequence, keep buffering
            if (state->buffer_size < 6 && state->buffer[0] == '\033') {
                return 2; // Keep buffering
            }

            // Not a paste sequence at all, reset
            paste_state_reset(state);
        }

        return 0; // Normal character
    }

    // In paste - buffer characters and check for end sequence
    if (paste_buffer_add_char(state, c) < 0) {
        return -1; // Buffer overflow
    }

    // Check for paste end sequence (last 6 chars)
    if (state->buffer_size >= 6) {
        const char *tail = state->buffer + state->buffer_size - 6;
        int consumed = check_paste_end_sequence(tail, 6);
        if (consumed > 0) {
            // Paste ended! Remove end sequence from buffer
            state->buffer_size -= 6;
            state->buffer[state->buffer_size] = '\0';
            state->in_paste = 0;
            return 3; // Paste complete
        }
    }

    return 2; // Paste in progress
}

/**
 * Get the completed paste content
 * Returns pointer to internal buffer (do not free)
 */
static const char* paste_get_content(PasteState *state, size_t *out_len) {
    if (!state) return NULL;
    if (out_len) *out_len = state->buffer_size;
    state->buffer[state->buffer_size] = '\0'; // Ensure null-terminated
    return state->buffer;
}

#endif // PASTE_HANDLER_H
