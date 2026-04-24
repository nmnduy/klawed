/*
 * TUI Paste Detection & Handling
 *
 * Manages paste mode detection and content handling:
 * - Bracketed paste mode
 * - Heuristic paste detection (rapid input)
 * - Paste content buffering
 * - Placeholder insertion for large pastes
 * - Paste timeout detection
 */

#include "tui_paste.h"
#include "tui.h"
#include "tui_input.h"
#include "logger.h"
#include "array_resize.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <strings.h>

// Global paste detection configuration
// Default: enable heuristic (helps when bracketed paste isn't passed through, e.g. tmux)
static int g_enable_paste_heuristic = 1;

// Less sensitive defaults: require very fast, large bursts to classify as paste
static int g_paste_gap_ms = 12;         // max gap to count as "rapid" for burst
static int g_paste_burst_min = 60;      // min consecutive keys within gap to enter paste mode
static int g_paste_timeout_ms = 400;    // idle time to finalize paste

// Get paste detection configuration from environment
void tui_paste_init_config(void) {
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
}

// Check if paste detection heuristic is enabled
int tui_paste_heuristic_enabled(void) {
    return g_enable_paste_heuristic;
}

// Get paste detection parameters
void tui_paste_get_params(int *gap_ms, int *burst_min, int *timeout_ms) {
    if (gap_ms) *gap_ms = g_paste_gap_ms;
    if (burst_min) *burst_min = g_paste_burst_min;
    if (timeout_ms) *timeout_ms = g_paste_timeout_ms;
}

// Insert paste content or placeholder into visible buffer
// Called when paste mode ends to finalize the paste
void tui_paste_finalize(TUIInputBuffer *input) {
    if (!input || !input->paste_content || input->paste_content_len == 0) {
        return;
    }

    int insert_pos = input->paste_start_pos;
    if (insert_pos < 0) insert_pos = 0;
    if (insert_pos > input->length) insert_pos = input->length;

    // For small pastes, insert directly without placeholder
    if (input->paste_content_len < TUI_PASTE_PLACEHOLDER_THRESHOLD) {
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

// Check if paste mode should be exited due to timeout
// Returns 1 if paste ended, 0 otherwise
int tui_paste_check_timeout(TUIState *tui, const char *prompt) {
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
        tui_paste_finalize(input);

        // Redraw input window
        if (tui->wm.input_win) {
            // We need to call input_redraw, but it's in the main tui.c
            // For now, we'll just refresh the window
            // The caller (event loop) will handle the full redraw
            (void)prompt;  // Mark as used to avoid warning
        }
        return 1;
    }

    return 0;
}

// Expand a previous paste's placeholder into actual content in the buffer.
// This ensures each paste is resolved before the next one begins.
static void paste_expand_previous(TUIInputBuffer *input) {
    if (!input || !input->paste_content || input->paste_content_len == 0 ||
        input->paste_placeholder_len == 0) {
        return;
    }

    int insert_pos = input->paste_start_pos;
    if (insert_pos < 0) insert_pos = 0;
    if (insert_pos > input->length) insert_pos = input->length;

    int paste_len = (int)input->paste_content_len;
    int placeholder_len = input->paste_placeholder_len;
    int size_change = paste_len - placeholder_len;

    // Check if we need to grow the buffer
    if (input->length + size_change >= (int)input->capacity - 1) {
        size_t needed = (size_t)(input->length + size_change + 4096);
        void *buf_ptr = (void *)input->buffer;
        if (buffer_reserve(&buf_ptr, &input->capacity, needed) != 0) {
            LOG_WARN("[TUI] Cannot grow buffer to expand paste content (%zu chars)",\
                     input->paste_content_len);
            return;
        }
        input->buffer = (char *)buf_ptr;
    }

    int after_pos = insert_pos + placeholder_len;
    int after_len = input->length - after_pos;

    // Move the text after the placeholder to make room for paste content
    memmove(&input->buffer[insert_pos + paste_len],
            &input->buffer[after_pos],
            (size_t)(after_len + 1));  // +1 for null terminator

    // Copy paste content into the gap
    memcpy(&input->buffer[insert_pos], input->paste_content, input->paste_content_len);

    input->length += size_change;
    input->cursor = insert_pos + paste_len;

    // Mark this paste as resolved
    input->paste_placeholder_len = 0;
    input->paste_content_len = 0;
}

// Start paste mode (called when bracketed paste sequence detected)
void tui_paste_start_mode(TUIInputBuffer *input) {
    if (!input) {
        return;
    }

    // First, expand any previous unresolved paste placeholder
    paste_expand_previous(input);

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
            return;
        }
    }

    LOG_DEBUG("[TUI] Bracketed paste mode started at position %d", input->paste_start_pos);
}

// End paste mode and finalize content
void tui_paste_end_mode(TUIInputBuffer *input) {
    if (!input || !input->paste_mode) {
        return;
    }

    input->paste_mode = 0;
    LOG_DEBUG("[TUI] Bracketed paste mode ended, pasted %zu characters",
             input->paste_content_len);

    // Insert placeholder or content directly
    tui_paste_finalize(input);
}

// Check if input buffer is in paste mode
int tui_paste_is_active(const TUIInputBuffer *input) {
    return input && input->paste_mode;
}

// Update paste detection timing (called for each input character)
// Returns: 1 if paste mode was just entered, 0 otherwise
int tui_paste_update_timing(TUIInputBuffer *input) {
    if (!input || !g_enable_paste_heuristic) {
        return 0;
    }

    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    long elapsed_ms = (current_time.tv_sec - input->last_input_time.tv_sec) * 1000 +
                      (current_time.tv_nsec - input->last_input_time.tv_nsec) / 1000000;

    // Very conservative thresholds to avoid false positives during normal typing
    if (elapsed_ms < g_paste_gap_ms) {
        input->rapid_input_count++;
        if (input->rapid_input_count >= g_paste_burst_min && !input->paste_mode) {
            // If there's a previous unresolved paste, expand it first
            paste_expand_previous(input);
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
                    return 0;
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
            return 1;  // Just entered paste mode
        }
    } else if (elapsed_ms > 500) {
        // Reset rapid input counter if there's a pause (but not in paste mode)
        if (!input->paste_mode) {
            input->rapid_input_count = 0;
        }
        // Paste mode timeout is handled in tui_paste_check_timeout()
    }

    input->last_input_time = current_time;
    return 0;
}

// Accumulate character in paste buffer (called during paste mode)
int tui_paste_accumulate_char(TUIInputBuffer *input, const unsigned char *utf8_char, int char_bytes) {
    if (!input || !input->paste_mode || !input->paste_content) {
        return -1;
    }

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

    return 0;
}

// Reset paste state (called on input buffer clear)
void tui_paste_reset(TUIInputBuffer *input) {
    if (!input) {
        return;
    }

    input->paste_mode = 0;
    input->rapid_input_count = 0;
    input->paste_content_len = 0;
    input->paste_start_pos = 0;
    input->paste_placeholder_len = 0;
}
