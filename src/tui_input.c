/*
 * TUI Input Buffer Management
 *
 * Handles input buffer operations including:
 * - Buffer initialization and cleanup
 * - Character/string insertion and deletion
 * - Cursor movement
 * - Word boundary operations
 * - UTF-8 character handling
 */

// Define feature test macros before any includes
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "tui_input.h"
#include "tui.h"
#include "logger.h"
#include "array_resize.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <ncurses.h>
#include <time.h>

#define INPUT_BUFFER_SIZE 8192

// UTF-8 helper: Get the length of a UTF-8 character from its first byte
int tui_input_utf8_char_length(unsigned char first_byte) {
    if ((first_byte & 0x80) == 0) return 1;  // 0xxxxxxx
    if ((first_byte & 0xE0) == 0xC0) return 2;  // 110xxxxx
    if ((first_byte & 0xF0) == 0xE0) return 3;  // 1110xxxx
    if ((first_byte & 0xF8) == 0xF0) return 4;  // 11110xxx
    return 1;  // Invalid, treat as single byte
}

// Check if character is a word boundary
int tui_input_is_word_boundary(char c) {
    return !isalnum(c) && c != '_';
}

// Move cursor backward by one word
int tui_input_move_backward_word(const char *buffer, int cursor_pos) {
    if (cursor_pos <= 0) return 0;
    int pos = cursor_pos - 1;
    while (pos > 0 && tui_input_is_word_boundary(buffer[pos])) pos--;
    while (pos > 0 && !tui_input_is_word_boundary(buffer[pos])) pos--;
    if (pos > 0 && tui_input_is_word_boundary(buffer[pos])) pos++;
    return pos;
}

// Move cursor forward by one word
int tui_input_move_forward_word(const char *buffer, int cursor_pos, int buffer_len) {
    if (cursor_pos >= buffer_len) return buffer_len;
    int pos = cursor_pos;
    while (pos < buffer_len && !tui_input_is_word_boundary(buffer[pos])) pos++;
    while (pos < buffer_len && tui_input_is_word_boundary(buffer[pos])) pos++;
    return pos;
}

// Initialize input buffer
int tui_input_init(TUIState *tui) {
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
    int h = 0;
    int w = 0;
    getmaxyx(tui->wm.input_win, h, w);
    input->win_width = w;  // No borders
    input->win_height = h;

    tui->input_buffer = input;
    return 0;
}

// Free input buffer
void tui_input_free(TUIState *tui) {
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
int tui_input_insert_char(TUIInputBuffer *input, const unsigned char *utf8_char, int char_bytes) {
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
int tui_input_insert_string(TUIInputBuffer *input, const char *str) {
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
        size_t new_capacity = 0;
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
int tui_input_delete_char(TUIInputBuffer *input) {
    if (!input || input->cursor >= input->length) {
        return 0;  // Nothing to delete
    }

    // Find the length of the UTF-8 character at cursor
    int char_len = tui_input_utf8_char_length((unsigned char)input->buffer[input->cursor]);

    // Delete the character by moving subsequent text left
    memmove(&input->buffer[input->cursor],
            &input->buffer[input->cursor + char_len],
            (size_t)(input->length - input->cursor - char_len + 1));

    input->length -= char_len;
    return char_len;
}

// Delete character before cursor (backspace)
int tui_input_backspace(TUIInputBuffer *input) {
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
int tui_input_delete_word_backward(TUIInputBuffer *input) {
    if (!input || input->cursor <= 0) {
        return 0;
    }

    int word_start = input->cursor - 1;
    while (word_start > 0 && tui_input_is_word_boundary(input->buffer[word_start])) {
        word_start--;
    }
    while (word_start > 0 && !tui_input_is_word_boundary(input->buffer[word_start])) {
        word_start--;
    }
    if (word_start > 0 && tui_input_is_word_boundary(input->buffer[word_start])) {
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
int tui_input_delete_word_forward(TUIInputBuffer *input) {
    if (!input || input->cursor >= input->length) {
        return 0;
    }

    int word_end = tui_input_move_forward_word(input->buffer, input->cursor, input->length);
    int delete_count = word_end - input->cursor;

    if (delete_count > 0) {
        memmove(&input->buffer[input->cursor],
                &input->buffer[word_end],
                (size_t)(input->length - word_end + 1));
        input->length -= delete_count;
    }

    return delete_count;
}
