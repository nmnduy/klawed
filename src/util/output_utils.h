#ifndef OUTPUT_UTILS_H
#define OUTPUT_UTILS_H

#include "../message_queue.h"

/**
 * Output Utilities
 * 
 * Helper functions for formatting and emitting tool output.
 * These functions are aware of TUI mode and oneshot mode.
 */

/**
 * Emit a line of tool output
 * Handles both TUI mode (via message queue) and direct output
 * @param prefix Line prefix (can be empty string)
 * @param text Line text
 */
void tool_emit_line(const char *prefix, const char *text);

/**
 * Emit a diff line with appropriate coloring
 * @param line Diff line to emit
 * @param add_color ANSI color code for added lines
 * @param remove_color ANSI color code for removed lines
 */
void emit_diff_line(const char *line,
                    const char *add_color,
                    const char *remove_color);

/**
 * Set the active tool queue for output
 * Used internally by tool execution system
 * @param queue Message queue to use for output, or NULL for direct output
 */
void output_set_tool_queue(TUIMessageQueue *queue);

/**
 * Get the current tool queue
 * @return Current tool queue, or NULL if not set
 */
TUIMessageQueue* output_get_tool_queue(void);

/**
 * Set oneshot mode flag
 * In oneshot mode, tool outputs are suppressed or wrapped differently
 * @param enabled 1 to enable oneshot mode, 0 to disable
 */
void output_set_oneshot_mode(int enabled);

/**
 * Get oneshot mode flag
 * @return 1 if oneshot mode is enabled, 0 otherwise
 */
int output_get_oneshot_mode(void);

#endif // OUTPUT_UTILS_H
