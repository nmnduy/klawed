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

#ifndef TUI_INPUT_H
#define TUI_INPUT_H

#include <stddef.h>
#include <time.h>

// Forward declarations
typedef struct _win_st WINDOW;
typedef struct TUIStateStruct TUIState;

// Input buffer structure
typedef struct TUIInputBuffer {
    char *buffer;
    size_t capacity;
    int length;
    int cursor;
    WINDOW *win;
    int win_width;
    int win_height;
    // Display state
    int view_offset;         // Horizontal scroll offset for long lines
    int line_scroll_offset;  // Vertical scroll offset (which line to show at top)
    // Paste mode detection
    int paste_mode;          // 1 when in bracketed paste, 0 otherwise
    struct timespec last_input_time;  // Track timing for paste detection
    int rapid_input_count;   // Count of rapid inputs (heuristic for paste)
    // Paste content tracking
    char *paste_content;     // Actual pasted content (kept separate from visible buffer)
    size_t paste_capacity;   // Capacity of paste buffer
    size_t paste_content_len; // Length of pasted content
    int paste_start_pos;     // Position where paste started in buffer
    int paste_placeholder_len; // Length of placeholder in buffer
} TUIInputBuffer;

// Initialize input buffer
// Returns 0 on success, -1 on failure
int tui_input_init(TUIState *tui);

// Free input buffer resources
void tui_input_free(TUIState *tui);

// Insert character(s) at cursor position
// utf8_char: UTF-8 encoded character bytes
// char_bytes: Number of bytes in the UTF-8 character
// Returns 0 on success, -1 on failure
int tui_input_insert_char(TUIInputBuffer *input, const unsigned char *utf8_char, int char_bytes);

// Insert string at cursor position
// Returns 0 on success, -1 on failure
int tui_input_insert_string(TUIInputBuffer *input, const char *str);

// Delete character at cursor position (forward delete)
// Returns number of bytes deleted
int tui_input_delete_char(TUIInputBuffer *input);

// Delete character before cursor (backspace)
// Returns number of bytes deleted
int tui_input_backspace(TUIInputBuffer *input);

// Delete word before cursor (Alt+Backspace)
// Returns number of bytes deleted
int tui_input_delete_word_backward(TUIInputBuffer *input);

// Delete word forward (Alt+d)
// Returns number of bytes deleted
int tui_input_delete_word_forward(TUIInputBuffer *input);

// UTF-8 helper functions
// Get the length of a UTF-8 character from its first byte
int tui_input_utf8_char_length(unsigned char first_byte);

// Check if character is a word boundary
int tui_input_is_word_boundary(char c);

// Move cursor backward by one word
// Returns new cursor position
int tui_input_move_backward_word(const char *buffer, int cursor_pos);

// Move cursor forward by one word
// Returns new cursor position
int tui_input_move_forward_word(const char *buffer, int cursor_pos, int buffer_len);

#endif // TUI_INPUT_H
