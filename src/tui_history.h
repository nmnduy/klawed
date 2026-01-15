/*
 * TUI Input History Management
 *
 * Handles input history navigation and persistence:
 * - History navigation (Ctrl+P/N)
 * - History search popup (Ctrl+R)
 * - History append on submit
 * - Integration with persistent history file
 *
 * This module contains the implementations for input history functions.
 * All public functions are declared here and used by tui.c.
 */

#ifndef TUI_HISTORY_H
#define TUI_HISTORY_H

#include "tui.h"

/*
 * Handle Ctrl+R: Start history search popup
 * Returns: 0 on success, -1 on failure
 */
int tui_history_start_search(TUIState *tui);

/*
 * Handle history search mode key processing
 * Returns: 0 to continue, 1 if should exit loop
 */
int tui_history_process_search_key(TUIState *tui, int ch, const char *prompt);

/*
 * Handle Ctrl+P: Navigate to previous history entry
 * Returns: 0 on success
 */
int tui_history_navigate_prev(TUIState *tui, const char *prompt);

/*
 * Handle Ctrl+N: Navigate to next history entry
 * Returns: 0 on success
 */
int tui_history_navigate_next(TUIState *tui, const char *prompt);

/*
 * Append input to history (both in-memory and persistent)
 * Performs simple de-duplication (skips if same as last entry)
 * Resets history navigation state after append
 * 
 * input: The input string to append (must be non-NULL)
 */
void tui_history_append(TUIState *tui, const char *input);

/*
 * Reset history navigation state
 * Frees saved input and sets index to -1
 */
void tui_history_reset_navigation(TUIState *tui);

#endif // TUI_HISTORY_H
