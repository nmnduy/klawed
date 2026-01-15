/*
 * TUI Tab Completion Functionality
 *
 * Implements tab completion for:
 * - Vim-style commands (in command mode: :q, :quit, :w, etc.)
 * - Slash commands (in normal mode: /exit, /add-dir, etc.)
 */

#ifndef TUI_COMPLETION_H
#define TUI_COMPLETION_H

// Forward declarations
typedef struct TUIStateStruct TUIState;

/**
 * Handle tab completion for slash commands (commands starting with '/')
 *
 * Uses the commands system to get completions for slash commands like
 * /exit, /add-dir, etc. Replaces the current word with the completion.
 *
 * @param tui     TUI state
 * @param prompt  Current prompt string (for redraw)
 * @return 1 if completion was applied, 0 otherwise
 */
int tui_handle_tab_completion(TUIState *tui, const char *prompt);

/**
 * Find matching vim-style commands for tab completion
 *
 * Used in command mode (after typing ':') to complete vim-style commands
 * like 'q', 'quit', 'w', 'write', 'wq', 'noh', 'nohlsearch'.
 *
 * @param prefix       Command prefix to match (without ':')
 * @param matches      Array to fill with matching command pointers
 * @param max_matches  Maximum number of matches to return
 * @return Number of matches found
 */
int tui_find_command_matches(const char *prefix, const char **matches, int max_matches);

#endif // TUI_COMPLETION_H
