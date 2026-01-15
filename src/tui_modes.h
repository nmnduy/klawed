/*
 * TUI Mode Handling
 *
 * Manages different input modes (Vim-like):
 * - NORMAL mode: Navigation and commands
 * - INSERT mode: Text input
 * - COMMAND mode: Command execution
 * - SEARCH mode: Search pattern input
 * - FILE_SEARCH mode: File search popup
 * - HISTORY_SEARCH mode: History search popup
 */

#ifndef TUI_MODES_H
#define TUI_MODES_H

// Forward declarations
typedef struct TUIStateStruct TUIState;

// Handle input in NORMAL mode
// Returns:
//   0 = character processed
//   1 = submit input (Enter)
//   2 = interrupt (Ctrl+C)
//   3 = exit (Ctrl+D)
//   -1 = error
int tui_modes_handle_normal(TUIState *tui, int ch, const char *prompt, void *user_data);

// Handle input in INSERT mode
// Returns:
//   0 = character processed
//   1 = submit input (Enter, only when buffer is non-empty)
//   2 = interrupt (Ctrl+C)
//   3 = exit (Ctrl+D when buffer is empty)
//   -1 = error
int tui_modes_handle_insert(TUIState *tui, int ch, const char *prompt);

// Handle input in COMMAND mode
// Returns:
//   0 = character processed
//   1 = command executed
//   -1 = error or cancelled
int tui_modes_handle_command(TUIState *tui, int ch, const char *prompt);

// Handle input in SEARCH mode
// Returns:
//   0 = character processed
//   1 = search executed
//   -1 = error or cancelled
int tui_modes_handle_search(TUIState *tui, int ch, const char *prompt);

// Switch to INSERT mode from NORMAL mode
void tui_modes_enter_insert(TUIState *tui);

// Switch to NORMAL mode from INSERT mode
void tui_modes_enter_normal(TUIState *tui);

// Switch to COMMAND mode from NORMAL mode
void tui_modes_enter_command(TUIState *tui);

// Switch to SEARCH mode from NORMAL mode
// direction: 1 for forward ('/'), -1 for backward ('?')
void tui_modes_enter_search(TUIState *tui, int direction);

// Execute a command from COMMAND mode
// Returns 0 on success, -1 on error
int tui_modes_execute_command(TUIState *tui, const char *command);

#endif // TUI_MODES_H
