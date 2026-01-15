/*
 * TUI Rendering & Display
 *
 * Handles all rendering operations including:
 * - Color initialization
 * - Input window rendering
 * - Status window rendering
 * - Conversation pad rendering
 * - Search highlighting
 * - TODO list rendering
 * - Subagent display rendering
 */

#ifndef TUI_RENDER_H
#define TUI_RENDER_H

#include <stdbool.h>
#include "tui.h"

// Forward declarations
typedef struct TUIStateStruct TUIState;
typedef struct _win_st WINDOW;
typedef struct TodoList TodoList;

// Initialize ncurses color pairs
// Sets up all color pairs used by TUI
void tui_render_init_colors(void);

// Convert RGB (0-255) to ncurses color (0-1000)
short tui_render_rgb_to_ncurses(int value);

// Render the input window with current buffer content
// prompt: Prompt string to display
void tui_render_input(TUIState *tui, const char *prompt);

// Render the status window based on current state
void tui_render_status(TUIState *tui);

// Render a single conversation entry to the pad
// Returns 0 on success, -1 on error
int tui_render_entry_to_pad(TUIState *tui, const char *prefix, const char *text, TUIColorPair color_pair);

// Render text with search pattern highlighting
// Returns number of characters rendered
int tui_render_text_with_search_highlight(WINDOW *win, const char *text,
                                         int text_pair,
                                         const char *search_pattern);

// Redraw the entire conversation from entries array
void tui_render_redraw_conversation(TUIState *tui);

// Render a TODO list with colored items based on status
void tui_render_todo_list_impl(TUIState *tui, const TodoList *list);

// Render active subagent processes with their status and log tail
void tui_render_active_subagents_impl(TUIState *tui);

// Status spinner functions
// Get current spinner variant configuration
const void* tui_render_status_spinner_variant(void);

// Get spinner frame update interval in nanoseconds
uint64_t tui_render_status_spinner_interval_ns(void);

// Check if status message should display spinner animation
bool tui_render_status_message_wants_spinner(const char *message);

// Start status spinner animation
void tui_render_status_spinner_start(TUIState *tui);

// Stop status spinner animation
void tui_render_status_spinner_stop(TUIState *tui);

// Update spinner to next frame
void tui_render_status_spinner_tick(TUIState *tui);

// Get monotonic time in nanoseconds (for spinner timing)
uint64_t tui_render_monotonic_time_ns(void);

#endif // TUI_RENDER_H
