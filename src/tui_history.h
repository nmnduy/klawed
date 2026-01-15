/*
 * TUI Input History
 *
 * Manages input history navigation including:
 * - History array management
 * - History file integration
 * - Up/down arrow navigation (Ctrl+P/N)
 * - History search integration
 */

#ifndef TUI_HISTORY_H
#define TUI_HISTORY_H

// Forward declarations
typedef struct TUIStateStruct TUIState;

// Navigate to previous history entry (Ctrl+P, Up arrow)
// Returns 0 on success, -1 on error or no more history
int tui_history_navigate_prev(TUIState *tui);

// Navigate to next history entry (Ctrl+N, Down arrow)
// Returns 0 on success, -1 on error or no more history
int tui_history_navigate_next(TUIState *tui);

// Save current input to history and reset navigation state
// Returns 0 on success, -1 on error
int tui_history_save_current_input(TUIState *tui);

// Reset history navigation state (call when input changes while browsing)
void tui_history_reset_navigation(TUIState *tui);

// Load history entries from history file into memory
// Returns 0 on success, -1 on error
int tui_history_load_from_file(TUIState *tui);

// Free in-memory history entries
void tui_history_free_entries(TUIState *tui);

#endif // TUI_HISTORY_H
