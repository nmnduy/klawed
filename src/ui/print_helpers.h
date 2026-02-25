/*
 * print_helpers.h - Console output helpers
 *
 * Provides formatted console output functions for different message types.
 * Uses colorscheme for consistent styling.
 */

#ifndef PRINT_HELPERS_H
#define PRINT_HELPERS_H

/**
 * Print an assistant message to console
 * Uses COLORSCHEME_ASSISTANT for role name, COLORSCHEME_FOREGROUND for text
 *
 * @param text Message text to display
 */
void print_assistant(const char *text);

/**
 * Print a tool execution message to console
 * Uses COLORSCHEME_STATUS for tool tag, COLORSCHEME_FOREGROUND for details
 *
 * @param tool_name Name of the tool (e.g., "Bash", "Read")
 * @param details Tool-specific details (can be NULL)
 */
void print_tool(const char *tool_name, const char *details);

/**
 * Print an error message to console
 * Logs to error log file (no stderr output)
 *
 * @param text Error message text
 */
void print_error(const char *text);

/**
 * Print a status message to console
 * Uses COLORSCHEME_STATUS for formatting
 *
 * @param text Status message text
 */
void print_status(const char *text);

#endif // PRINT_HELPERS_H
