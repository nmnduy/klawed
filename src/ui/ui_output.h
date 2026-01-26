/*
 * ui_output.h - UI abstraction layer
 *
 * Provides functions for outputting messages to the UI,
 * whether in TUI mode or console mode.
 */

#ifndef UI_OUTPUT_H
#define UI_OUTPUT_H

#include "../tui.h"
#include "../message_queue.h"
#include "../spinner_messages.h"

/**
 * Append a line to the UI conversation area
 *
 * Handles output routing based on active mode:
 * - TUI queue mode: Posts message to queue
 * - TUI direct mode: Calls TUI functions
 * - Console mode: Falls back to print_* functions
 *
 * @param tui TUI state pointer (NULL if not in TUI mode)
 * @param queue Message queue pointer (NULL if not using queue)
 * @param prefix Message prefix (e.g., "[Assistant]", "[Tool: Bash]")
 * @param text Message text
 * @param color Color pair for TUI display
 */
void ui_append_line(TUIState *tui,
                    TUIMessageQueue *queue,
                    const char *prefix,
                    const char *text,
                    TUIColorPair color);

/**
 * Set the status line text
 *
 * @param tui TUI state pointer (NULL if not in TUI mode)
 * @param queue Message queue pointer (NULL if not using queue)
 * @param status_text Status text to display
 */
void ui_set_status(TUIState *tui,
                   TUIMessageQueue *queue,
                   const char *status_text);

/**
 * Set a random varied status message based on context
 *
 * @param tui TUI state pointer (NULL if not in TUI mode)
 * @param queue Message queue pointer (NULL if not using queue)
 * @param context Message context (API call, tool running, etc.)
 */
void ui_set_status_varied(TUIState *tui,
                          TUIMessageQueue *queue,
                          SpinnerMessageContext context);

/**
 * Set a random varied status message for a specific tool
 *
 * @param tui TUI state pointer (NULL if not in TUI mode)
 * @param queue Message queue pointer (NULL if not using queue)
 * @param tool_name Name of the tool being executed
 */
void ui_set_status_for_tool(TUIState *tui,
                            TUIMessageQueue *queue,
                            const char *tool_name);

/**
 * Show an error message in the UI
 *
 * @param tui TUI state pointer (NULL if not in TUI mode)
 * @param queue Message queue pointer (NULL if not using queue)
 * @param error_text Error message to display
 */
void ui_show_error(TUIState *tui,
                   TUIMessageQueue *queue,
                   const char *error_text);

#endif // UI_OUTPUT_H
