/*
 * tool_output_display.h - Tool-specific output formatting
 *
 * Provides human-readable formatting for tool execution results.
 */

#ifndef TOOL_OUTPUT_DISPLAY_H
#define TOOL_OUTPUT_DISPLAY_H

#include <cjson/cJSON.h>

// Forward declaration
typedef struct ConversationState ConversationState;

/**
 * Print human-readable tool output to console
 * Handles tool-specific formatting for different tool types
 *
 * @param tool_name Name of the tool that was executed
 * @param tool_details Brief details about the tool invocation (can be NULL)
 * @param tool_result JSON result object from the tool
 */
void print_human_readable_tool_output(const char *tool_name,
                                      const char *tool_details,
                                      cJSON *tool_result);

/**
 * Extract tool details string from tool arguments
 * Creates a human-readable summary of tool invocation
 *
 * @param tool_name Name of the tool
 * @param arguments JSON arguments object
 * @return Static buffer with details string, or NULL if no details available
 */
char* get_tool_details(const char *tool_name, cJSON *arguments);

/**
 * Print token usage statistics for a conversation session
 *
 * @param state Conversation state containing session ID and persistence DB
 */
void print_token_usage(ConversationState *state);

#endif // TOOL_OUTPUT_DISPLAY_H
