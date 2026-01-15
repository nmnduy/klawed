/*
 * TUI Core Initialization and Management
 *
 * Core TUI lifecycle management:
 * - Initialization and cleanup
 * - Status updates and display
 * - Startup banner
 * - Terminal suspend/resume
 * - Vim-fugitive availability checking
 * - Refresh operations
 * - Scrolling control
 */

#ifndef TUI_CORE_H
#define TUI_CORE_H

#include <stdbool.h>

// Forward declarations
typedef struct TUIStateStruct TUIState;
typedef struct ConversationState ConversationState;
typedef struct PersistenceDB PersistenceDB;

// Initialize TUI subsystem
// tui: TUI state structure (caller-allocated)
// state: Conversation state (for plan_mode and other state queries)
// Returns 0 on success, -1 on failure
int tui_core_init(TUIState *tui, ConversationState *state);

// Clean up TUI resources
void tui_core_cleanup(TUIState *tui);

// Suspend TUI (restore terminal to normal mode for external commands)
// Returns 0 on success, -1 on error
int tui_core_suspend(TUIState *tui);

// Resume TUI (restore terminal to program mode after external commands)
// Returns 0 on success, -1 on error
int tui_core_resume(TUIState *tui);

// Update status message
// status_text: New status message to display (NULL to clear)
void tui_core_update_status(TUIState *tui, const char *status_text);

// Refresh the display (call after making changes)
void tui_core_refresh(TUIState *tui);

// Show startup banner with version, model, and working directory
void tui_core_show_startup_banner(TUIState *tui, const char *version, const char *model, const char *working_dir);

// Clear conversation display and show mascot banner
void tui_core_clear_conversation(TUIState *tui, const char *version, const char *model, const char *working_dir);

// Scroll conversation up/down
// direction: positive = scroll down, negative = scroll up
void tui_core_scroll_conversation(TUIState *tui, int direction);

// Set database connection for token usage queries
void tui_core_set_persistence_db(TUIState *tui, PersistenceDB *db, const char *session_id);

// Vim-fugitive availability checking
// Check if vim-fugitive is available (cached result)
// Returns: -1 = unknown/not checked yet, 0 = not available, 1 = available
int tui_core_get_vim_fugitive_available(TUIState *tui);

// Start background check for vim-fugitive availability
// This spawns a thread to check without blocking the main thread
void tui_core_start_vim_fugitive_check(TUIState *tui);

#endif // TUI_CORE_H
