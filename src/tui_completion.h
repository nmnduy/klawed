/*
 * TUI Tab Completion
 *
 * Handles tab completion for command mode:
 * - Command matching and completion
 * - Multiple match handling
 * - Command cycling
 */

#ifndef TUI_COMPLETION_H
#define TUI_COMPLETION_H

// Forward declarations
typedef struct TUIStateStruct TUIState;

// Find commands matching a prefix
// prefix: Command prefix to match
// matches: Output array for matching commands
// max_matches: Maximum number of matches to return
// Returns number of matches found
int tui_completion_find_command_matches(const char *prefix, const char **matches, int max_matches);

// Handle tab completion in current mode
// Returns 0 if completion was handled, -1 otherwise
int tui_completion_handle_tab(TUIState *tui, const char *prompt);

#endif // TUI_COMPLETION_H
