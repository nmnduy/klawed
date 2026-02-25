/*
 * TUI Window Management
 *
 * Manages window layout, sizing, and viewport control:
 * - Window resizing and layout calculation
 * - Viewport refresh operations
 * - Input window height adjustment
 * - Window validation
 * - Resize signal handling
 */

#ifndef TUI_WINDOW_H
#define TUI_WINDOW_H

// Forward declarations
typedef struct TUIStateStruct TUIState;

// Calculate how many visual lines are needed for input buffer
// Accounts for prompt length, wrapping, and newlines
// win_width: Width of the window
// prompt_len: Length of the prompt on the first line
// Returns number of lines needed
int tui_window_calculate_needed_lines(const char *buffer, int buffer_len, int win_width, int prompt_len);

// Resize input window dynamically based on content
// desired_lines: Number of lines the input content needs
// Returns 0 on success, -1 on failure
int tui_window_resize_input(TUIState *tui, int desired_lines);

// Refresh conversation window viewport (using pad)
void tui_window_refresh_conversation_viewport(TUIState *tui);

// Validate TUI window state (debug builds only)
// Checks that all windows are properly initialized
void tui_window_validate(TUIState *tui);

// Clear the global resize flag
void tui_window_clear_resize_flag(void);

// Check if terminal resize is pending
// Returns 1 if resize signal received, 0 otherwise
int tui_window_resize_pending(void);

// Install resize signal handler
// Returns 0 on success, -1 on failure
int tui_window_install_resize_handler(void);

#endif // TUI_WINDOW_H
