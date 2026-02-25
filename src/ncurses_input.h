/*
 * ncurses_input.h - ncurses-based input bar with full keyboard support
 *
 * Provides a readline-like input experience using ncurses with:
 * - Cursor movement (arrow keys, Ctrl+a/e, Alt+b/f, Home/End)
 * - Text editing (insert, delete, backspace)
 * - Word operations (Alt+d, Alt+backspace)
 * - Line operations (Ctrl+k, Ctrl+u, Ctrl+l)
 * - Multiline input (Ctrl+J for newline)
 * - History navigation (Up/Down arrows)
 * - Tab completion support (via callback)
 * - Paste handling (bracketed paste)
 */

#ifndef NCURSES_INPUT_H
#define NCURSES_INPUT_H

#include <ncurses.h>
#include <stddef.h>

// Forward declarations
typedef struct NCursesInput NCursesInput;

// ============================================================================
// Completion Support
// ============================================================================

typedef struct CompletionResult {
    char **options;      // Array of completion options
    int count;           // Number of options
    int selected;        // Which option is highlighted (for cycling)
} CompletionResult;

// Completion callback: given line + cursor position, return suggestions
// Context pointer can be used to pass ConversationState or other data
typedef CompletionResult* (*CompletionFn)(const char *line, int cursor_pos, void *ctx);

// Resize callback: called when input needs more/less height
// Returns the new height that was granted (may be less than requested)
typedef int (*ResizeFn)(void *ctx, int requested_height);

// ============================================================================
// NCurses Input Bar
// ============================================================================

typedef struct NCursesInput {
    WINDOW *window;          // ncurses window for input area
    char *buffer;            // Input buffer (dynamically allocated)
    size_t buffer_capacity;  // Capacity of buffer
    int cursor;              // Cursor position (0 to length)
    int length;              // Current length of input
    int window_height;       // Current height of input window (in lines)
    int window_width;        // Width of input window (in columns)
    int scroll_offset;       // Horizontal scroll offset for long lines
    int line_scroll_offset;  // Vertical scroll offset for multiline (0 = show last lines)

    // History support
    char **history;          // Array of history strings
    int history_capacity;    // Max history entries
    int history_count;       // Current number of entries
    int history_position;    // Current position when navigating (-1 = not navigating)
    char *saved_input;       // Saved input when navigating history

    // Completion support
    CompletionFn completer;  // Optional: for tab completion
    void *completer_ctx;     // Context passed to completer

    // Resize support
    ResizeFn resizer;        // Optional: for dynamic height adjustment
    void *resizer_ctx;       // Context passed to resizer
    int min_height;          // Minimum window height (lines)
    int max_height;          // Maximum window height (lines)

    // Paste tracking
    char *paste_content;     // Actual pasted content (kept separate from visible buffer)
    size_t paste_content_len; // Length of pasted content
    int paste_placeholder_start; // Start position of placeholder in buffer
    int paste_placeholder_len;   // Length of placeholder in buffer
} NCursesInput;

// ============================================================================
// API Functions
// ============================================================================

/**
 * Initialize an ncurses input bar
 *
 * @param input      Pointer to NCursesInput struct to initialize
 * @param window     ncurses WINDOW to use for input (should be pre-created)
 * @param completer  Optional completion callback (can be NULL)
 * @param ctx        Optional context passed to completer (can be NULL)
 * @return           0 on success, -1 on failure
 */
int ncurses_input_init(NCursesInput *input, WINDOW *window,
                      CompletionFn completer, void *ctx);

/**
 * Set resize callback for dynamic height adjustment
 *
 * @param input      Pointer to initialized NCursesInput
 * @param resizer    Resize callback function
 * @param ctx        Context passed to resizer
 * @param min_height Minimum height in lines (default 1)
 * @param max_height Maximum height in lines (default 3)
 */
void ncurses_input_set_resize_callback(NCursesInput *input, ResizeFn resizer,
                                       void *ctx, int min_height, int max_height);

/**
 * Read a line of input with editing support
 *
 * Returns: Newly allocated string with input (caller must free)
 *          NULL on EOF (Ctrl+D)
 *
 * @param input      Pointer to initialized NCursesInput
 * @param prompt     Prompt string to display
 */
char* ncurses_input_readline(NCursesInput *input, const char *prompt);

/**
 * Free resources associated with ncurses input
 *
 * @param input  Pointer to NCursesInput to cleanup
 */
void ncurses_input_free(NCursesInput *input);

/**
 * Free a completion result
 *
 * @param result  Pointer to CompletionResult to free
 */
void ncurses_completion_free(CompletionResult *result);

#endif // NCURSES_INPUT_H
