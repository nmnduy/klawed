/*
 * ncurses_input.c - ncurses-based input bar implementation
 *
 * Provides readline-like functionality using ncurses with full keyboard support
 */

#include "ncurses_input.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <ctype.h>

#define INITIAL_BUFFER_SIZE 8192
#define DEFAULT_HISTORY_SIZE 100

// ============================================================================
// Helper Functions
// ============================================================================

// Check if character is word boundary
static int is_word_boundary(char c) {
    return !isalnum(c) && c != '_';
}

// Move cursor backward by one word
static int move_backward_word(const char *buffer, int cursor_pos) {
    if (cursor_pos <= 0) return 0;

    int pos = cursor_pos - 1;

    // Skip trailing whitespace/punctuation
    while (pos > 0 && is_word_boundary(buffer[pos])) {
        pos--;
    }

    // Skip the word characters
    while (pos > 0 && !is_word_boundary(buffer[pos])) {
        pos--;
    }

    // If we stopped at a boundary (not at start), move one forward
    if (pos > 0 && is_word_boundary(buffer[pos])) {
        pos++;
    }

    return pos;
}

// Move cursor forward by one word
static int move_forward_word(const char *buffer, int cursor_pos, int buffer_len) {
    if (cursor_pos >= buffer_len) return buffer_len;

    int pos = cursor_pos;

    // Skip current word characters
    while (pos < buffer_len && !is_word_boundary(buffer[pos])) {
        pos++;
    }

    // Skip trailing whitespace/punctuation
    while (pos < buffer_len && is_word_boundary(buffer[pos])) {
        pos++;
    }

    return pos;
}

// ============================================================================
// History Management
// ============================================================================

static void history_init(NCursesInput *input) {
    input->history_capacity = DEFAULT_HISTORY_SIZE;
    input->history = calloc((size_t)input->history_capacity, sizeof(char*));
    if (!input->history) {
        LOG_ERROR("Failed to allocate history buffer");
        exit(1);
    }
    input->history_count = 0;
    input->history_position = -1;
    input->saved_input = NULL;
}

static void history_add(NCursesInput *input, const char *entry) {
    if (!entry || entry[0] == '\0') {
        return;  // Don't add empty entries
    }

    // Don't add if it's the same as the last entry
    if (input->history_count > 0 &&
        strcmp(input->history[input->history_count - 1], entry) == 0) {
        return;
    }

    // If at capacity, remove oldest entry
    if (input->history_count >= input->history_capacity) {
        free(input->history[0]);
        memmove(&input->history[0], &input->history[1],
                sizeof(char*) * (size_t)(input->history_capacity - 1));
        input->history_count--;
    }

    // Add new entry
    input->history[input->history_count] = strdup(entry);
    input->history_count++;
    input->history_position = -1;  // Reset navigation position
}

static void history_free(NCursesInput *input) {
    for (int i = 0; i < input->history_count; i++) {
        free(input->history[i]);
    }
    free(input->history);
    input->history = NULL;
    input->history_count = 0;
    input->history_capacity = 0;
    input->history_position = -1;
}

// ============================================================================
// Buffer Operations
// ============================================================================

// Insert character at cursor position
static int buffer_insert_char(NCursesInput *input, char c) {
    if (input->length + 1 >= (int)input->buffer_capacity - 1) {
        // Expand buffer
        size_t new_capacity = input->buffer_capacity * 2;
        char *new_buffer = realloc(input->buffer, new_capacity);
        if (!new_buffer) {
            return -1;  // Buffer full
        }
        input->buffer = new_buffer;
        input->buffer_capacity = new_capacity;
    }

    // Make space for the new character
    memmove(&input->buffer[input->cursor + 1], &input->buffer[input->cursor],
            (size_t)(input->length - input->cursor + 1));

    input->buffer[input->cursor] = c;
    input->length++;
    input->cursor++;
    return 0;
}

// Delete character at cursor position (forward delete)
static int buffer_delete_char(NCursesInput *input) {
    if (input->cursor >= input->length) {
        return 0;  // Nothing to delete
    }

    // Delete the character by moving subsequent text left
    memmove(&input->buffer[input->cursor],
           &input->buffer[input->cursor + 1],
           (size_t)(input->length - input->cursor));

    input->length--;
    return 1;
}

// Delete character before cursor (backspace)
static int buffer_backspace(NCursesInput *input) {
    if (input->cursor <= 0) {
        return 0;  // Nothing to delete
    }

    memmove(&input->buffer[input->cursor - 1], &input->buffer[input->cursor],
            (size_t)(input->length - input->cursor + 1));
    input->length--;
    input->cursor--;
    return 1;
}

// Delete word before cursor (Alt+Backspace)
static int buffer_delete_word_backward(NCursesInput *input) {
    if (input->cursor <= 0) {
        return 0;  // Nothing to delete
    }

    int word_start = move_backward_word(input->buffer, input->cursor);
    int delete_count = input->cursor - word_start;

    if (delete_count > 0) {
        memmove(&input->buffer[word_start], &input->buffer[input->cursor],
                (size_t)(input->length - input->cursor + 1));
        input->length -= delete_count;
        input->cursor = word_start;
    }

    return delete_count;
}

// Delete word after cursor (Alt+d)
static int buffer_delete_word_forward(NCursesInput *input) {
    if (input->cursor >= input->length) {
        return 0;  // Nothing to delete
    }

    int word_end = move_forward_word(input->buffer, input->cursor, input->length);
    int delete_count = word_end - input->cursor;

    if (delete_count > 0) {
        memmove(&input->buffer[input->cursor], &input->buffer[word_end],
                (size_t)(input->length - word_end + 1));
        input->length -= delete_count;
    }

    return delete_count;
}

// ============================================================================
// Display Functions
// ============================================================================

// Calculate number of visual lines needed for the buffer
static int calculate_needed_lines(const char *buffer, int buffer_len,
                                   int available_width, int prompt_len) {
    if (buffer_len == 0) return 1;

    int lines = 1;
    int current_line_width = prompt_len;  // First line includes prompt

    for (int i = 0; i < buffer_len; i++) {
        if (buffer[i] == '\n') {
            lines++;
            current_line_width = 0;
        } else {
            current_line_width++;
            if (current_line_width >= available_width) {
                lines++;
                current_line_width = 0;
            }
        }
    }

    return lines;
}

// Calculate cursor position in screen coordinates (line, column)
static void calculate_cursor_position(const char *buffer, int cursor_pos,
                                      int available_width, int prompt_len,
                                      int *out_line, int *out_col) {
    int line = 0;
    int col = 0;

    // Start at column 0; first line offset will be added after loop
    for (int i = 0; i < cursor_pos; i++) {
        if (buffer[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
            if (col >= available_width) {
                line++;
                col = 0;
            }
        }
    }

    // First line starts after prompt only if we're on line 0
    if (line == 0) {
        col += prompt_len;
    }

    *out_line = line;
    *out_col = col;
}

// Redraw the input window with multiline support
static void redraw_input(NCursesInput *input, const char *prompt) {
    int prompt_len = (int)strlen(prompt);
    int available_width = input->window_width;

    // Calculate how many lines we need
    int needed_lines = calculate_needed_lines(input->buffer, input->length,
                                              available_width, prompt_len);

    // Request resize if needed and callback is available
    if (input->resizer) {
        int desired_height = needed_lines;
        if (desired_height < input->min_height) {
            desired_height = input->min_height;
        } else if (desired_height > input->max_height) {
            desired_height = input->max_height;
        }

        if (desired_height != input->window_height) {
            int granted_height = input->resizer(input->resizer_ctx, desired_height);
            if (granted_height > 0) {
                input->window_height = granted_height;
                getmaxyx(input->window, input->window_height, input->window_width);
            }
        }
    }

    werase(input->window);

    int available_height = input->window_height;

    // Calculate cursor screen position
    int cursor_line = 0, cursor_col = 0;
    calculate_cursor_position(input->buffer, input->cursor,
                              available_width, prompt_len,
                              &cursor_line, &cursor_col);

    // Adjust vertical scroll to keep cursor visible
    if (cursor_line < input->line_scroll_offset) {
        input->line_scroll_offset = cursor_line;
    } else if (cursor_line >= input->line_scroll_offset + available_height) {
        input->line_scroll_offset = cursor_line - available_height + 1;
    }

    // Render visible lines and track cursor position
    int screen_line = 0;
    int current_line = 0;
    int current_col = 0;

    // Initial render column: include prompt offset only if first content line is visible
    int render_col = (input->line_scroll_offset == 0) ? prompt_len : 0;

    // Draw prompt on first line
    if (input->line_scroll_offset == 0) {
        mvwprintw(input->window, 0, 0, "%s", prompt);
    }

    // Track cursor position while rendering
    int cursor_screen_line = -1;
    int cursor_screen_col = 0;

    for (int i = 0; i < input->length && screen_line < available_height; i++) {
        // Skip lines before scroll offset
        if (current_line < input->line_scroll_offset) {
            if (input->buffer[i] == '\n') {
                current_line++;
                current_col = 0;
            } else {
                current_col++;
                if (current_col >= available_width) {
                    current_line++;
                    current_col = 0;
                }
            }
            continue;
        }

        // Track cursor position
        if (i == input->cursor) {
            cursor_screen_line = screen_line;
            cursor_screen_col = render_col;
        }

        // Render character
        if (input->buffer[i] == '\n') {
            // Show newline as special character
            mvwaddch(input->window, screen_line, render_col, '↵' | A_DIM);
            screen_line++;
            render_col = 0;
            current_line++;
            current_col = 0;
        } else {
            mvwaddch(input->window, screen_line, render_col, input->buffer[i]);
            render_col++;
            current_col++;

            if (render_col >= available_width) {
                screen_line++;
                render_col = 0;
                current_line++;
                current_col = 0;
            }
        }
    }

    // Check cursor at end of buffer
    if (input->cursor == input->length && cursor_screen_line == -1) {
        cursor_screen_line = screen_line;
        cursor_screen_col = render_col;
    }

    // Position cursor
    if (cursor_screen_line >= 0 && cursor_screen_line < available_height) {
        wmove(input->window, cursor_screen_line, cursor_screen_col);
    }

    wrefresh(input->window);
}

// ============================================================================
// API Implementation
// ============================================================================

int ncurses_input_init(NCursesInput *input, WINDOW *window,
                      CompletionFn completer, void *ctx) {
    if (!input || !window) return -1;

    input->window = window;
    input->buffer = malloc(INITIAL_BUFFER_SIZE);
    if (!input->buffer) {
        LOG_ERROR("Failed to allocate input buffer");
        return -1;
    }

    input->buffer_capacity = INITIAL_BUFFER_SIZE;
    input->buffer[0] = '\0';
    input->cursor = 0;
    input->length = 0;
    input->scroll_offset = 0;
    input->line_scroll_offset = 0;

    // Get window dimensions
    getmaxyx(window, input->window_height, input->window_width);

    // Initialize history
    history_init(input);

    // Set completion
    input->completer = completer;
    input->completer_ctx = ctx;

    // Initialize resize support
    input->resizer = NULL;
    input->resizer_ctx = NULL;
    input->min_height = 1;
    input->max_height = 3;

    // Initialize paste tracking
    input->paste_content = NULL;
    input->paste_content_len = 0;
    input->paste_placeholder_start = 0;
    input->paste_placeholder_len = 0;

    // Enable keypad mode for arrow keys and function keys
    keypad(window, TRUE);

    // Disable echo and set nodelay to non-blocking for paste detection
    noecho();

    return 0;
}

void ncurses_input_free(NCursesInput *input) {
    if (!input) return;

    free(input->buffer);
    input->buffer = NULL;
    input->buffer_capacity = 0;
    input->cursor = 0;
    input->length = 0;

    history_free(input);

    free(input->saved_input);
    input->saved_input = NULL;

    // Free paste tracking
    free(input->paste_content);
    input->paste_content = NULL;
    input->paste_content_len = 0;
}

void ncurses_completion_free(CompletionResult *result) {
    if (!result) return;

    for (int i = 0; i < result->count; i++) {
        free(result->options[i]);
    }
    free(result->options);
    free(result);
}

void ncurses_input_set_resize_callback(NCursesInput *input, ResizeFn resizer,
                                       void *ctx, int min_height, int max_height) {
    if (!input) return;

    input->resizer = resizer;
    input->resizer_ctx = ctx;
    input->min_height = (min_height > 0) ? min_height : 1;
    input->max_height = (max_height > min_height) ? max_height : min_height;
}

char* ncurses_input_readline(NCursesInput *input, const char *prompt) {
    if (!input || !prompt) return NULL;

    // Reset buffer
    input->buffer[0] = '\0';
    input->length = 0;
    input->cursor = 0;
    input->scroll_offset = 0;

    // Clear saved input from previous history navigation
    free(input->saved_input);
    input->saved_input = NULL;
    input->history_position = -1;

    // Initial draw
    redraw_input(input, prompt);

    int running = 1;
    while (running) {
        int ch = wgetch(input->window);

        if (ch == ERR) {
            // No input available
            continue;
        }

        switch (ch) {
            // ============================================================
            // Navigation keys
            // ============================================================
            case KEY_LEFT:
                if (input->cursor > 0) {
                    input->cursor--;
                    redraw_input(input, prompt);
                }
                break;

            case KEY_RIGHT:
                if (input->cursor < input->length) {
                    input->cursor++;
                    redraw_input(input, prompt);
                }
                break;

            case KEY_HOME:
            case 1:  // Ctrl+A
                input->cursor = 0;
                redraw_input(input, prompt);
                break;

            case KEY_END:
            case 5:  // Ctrl+E
                input->cursor = input->length;
                redraw_input(input, prompt);
                break;

            // ============================================================
            // History navigation
            // ============================================================
            case KEY_UP:
            case 16:  // Ctrl+P - previous history
                if (input->history_count > 0) {
                    // Save current input if this is the first Up press
                    if (input->history_position == -1) {
                        free(input->saved_input);
                        input->saved_input = strdup(input->buffer);
                        input->history_position = input->history_count;
                    }

                    // Navigate to previous entry
                    if (input->history_position > 0) {
                        input->history_position--;
                        const char *hist_entry = input->history[input->history_position];
                        strlcpy(input->buffer, hist_entry, input->buffer_capacity);
                        input->length = (int)strlen(input->buffer);
                        input->cursor = input->length;
                        redraw_input(input, prompt);
                    }
                }
                break;

            case KEY_DOWN:
            case 14:  // Ctrl+N - next history
                if (input->history_position != -1) {
                    input->history_position++;

                    if (input->history_position >= input->history_count) {
                        // Restore saved input
                        if (input->saved_input) {
                            strlcpy(input->buffer, input->saved_input, input->buffer_capacity);
                            input->length = (int)strlen(input->buffer);
                            input->cursor = input->length;
                        } else {
                            input->buffer[0] = '\0';
                            input->length = 0;
                            input->cursor = 0;
                        }
                        input->history_position = -1;
                    } else {
                        // Show next entry
                        const char *hist_entry = input->history[input->history_position];
                        strlcpy(input->buffer, hist_entry, input->buffer_capacity);
                        input->length = (int)strlen(input->buffer);
                        input->cursor = input->length;
                    }
                    redraw_input(input, prompt);
                }
                break;

            // ============================================================
            // Editing keys
            // ============================================================
            case KEY_BACKSPACE:
            case 127:
            case 8:
                if (buffer_backspace(input)) {
                    redraw_input(input, prompt);
                }
                break;

            case KEY_DC:  // Delete key
                if (buffer_delete_char(input)) {
                    redraw_input(input, prompt);
                }
                break;

            case 11:  // Ctrl+K - kill to end of line
                input->buffer[input->cursor] = '\0';
                input->length = input->cursor;
                redraw_input(input, prompt);
                break;

            case 21:  // Ctrl+U - kill to beginning of line
                if (input->cursor > 0) {
                    memmove(input->buffer, &input->buffer[input->cursor],
                           (size_t)(input->length - input->cursor + 1));
                    input->length -= input->cursor;
                    input->cursor = 0;
                    redraw_input(input, prompt);
                }
                break;

            case 12:  // Ctrl+L - clear entire input
                input->buffer[0] = '\0';
                input->length = 0;
                input->cursor = 0;
                redraw_input(input, prompt);
                break;

            // ============================================================
            // Word operations (Alt/Esc sequences)
            // ============================================================
            case 27:  // ESC - may be Alt key or escape sequence
                {
                    // Set nodelay mode to check for follow-up character
                    nodelay(input->window, TRUE);
                    int next_ch = wgetch(input->window);
                    nodelay(input->window, FALSE);

                    if (next_ch == ERR) {
                        // Standalone ESC - ignore for now
                        break;
                    }

                    switch (next_ch) {
                        case 'b':  // Alt+b - backward word
                        case 'B':
                            input->cursor = move_backward_word(input->buffer, input->cursor);
                            redraw_input(input, prompt);
                            break;

                        case 'f':  // Alt+f - forward word
                        case 'F':
                            input->cursor = move_forward_word(input->buffer, input->cursor, input->length);
                            redraw_input(input, prompt);
                            break;

                        case 'd':  // Alt+d - delete next word
                        case 'D':
                            if (buffer_delete_word_forward(input)) {
                                redraw_input(input, prompt);
                            }
                            break;

                        case 127:  // Alt+Backspace - delete previous word
                        case 8:
                            if (buffer_delete_word_backward(input)) {
                                redraw_input(input, prompt);
                            }
                            break;
                    }
                }
                break;

            // ============================================================
            // Submit and control
            // ============================================================
            case '\r':       // Enter key (with nonl() mode) - submit
            case KEY_ENTER:  // Keypad Enter - submit
                running = 0;
                break;

            case '\n':  // Ctrl+J (newline, ASCII 10) - insert newline for multiline input
                if (buffer_insert_char(input, '\n') == 0) {
                    redraw_input(input, prompt);
                }
                break;

            case 4:  // Ctrl+D - EOF
                return NULL;

            // ============================================================
            // Tab completion
            // ============================================================
            case '\t':
                if (input->completer) {
                    CompletionResult *res = input->completer(input->buffer,
                                                            input->cursor,
                                                            input->completer_ctx);
                    if (!res || res->count == 0) {
                        // No completions, beep
                        beep();
                        if (res) ncurses_completion_free(res);
                    } else if (res->count == 1) {
                        // Single completion: replace current word
                        const char *opt = res->options[0];
                        int optlen = (int)strlen(opt);

                        // Find start of current word
                        int start = input->cursor - 1;
                        while (start >= 0 && input->buffer[start] != ' ' &&
                               input->buffer[start] != '\t') {
                            start--;
                        }
                        start++;

                        int tail_len = input->length - input->cursor;
                        size_t needed = (size_t)(start + optlen + tail_len + 1);

                        if (needed > input->buffer_capacity) {
                            char *new_buffer = realloc(input->buffer, needed);
                            if (new_buffer) {
                                input->buffer = new_buffer;
                                input->buffer_capacity = needed;
                            }
                        }

                        // Move tail
                        memmove(input->buffer + start + optlen,
                               input->buffer + input->cursor,
                               (size_t)(tail_len + 1));

                        // Copy completion
                        memcpy(input->buffer + start, opt, (size_t)optlen);
                        input->cursor = start + optlen;
                        input->length = start + optlen + tail_len;

                        ncurses_completion_free(res);
                        redraw_input(input, prompt);
                    } else {
                        // Multiple completions - would need to show list
                        // For now, just beep
                        beep();
                        ncurses_completion_free(res);
                    }
                } else {
                    beep();
                }
                break;

            // ============================================================
            // Regular printable characters
            // ============================================================
            default:
                if (ch >= 32 && ch < 127) {
                    if (buffer_insert_char(input, (char)ch) == 0) {
                        redraw_input(input, prompt);
                    }
                }
                break;
        }
    }

    // Add to history (if not empty)
    if (input->buffer[0] != '\0') {
        history_add(input, input->buffer);
    }

    return strdup(input->buffer);
}
