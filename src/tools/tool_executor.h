#ifndef TOOL_EXECUTOR_H
#define TOOL_EXECUTOR_H

#include <cjson/cJSON.h>
#include "../conversation_state.h"

// Execute a tool by name with given parameters
// Returns a JSON object with the tool result
cJSON* execute_tool(const char *tool_name, cJSON *input, ConversationState *state);

// Validate that a tool name is in the provided tools list
// Returns 1 if valid, 0 if invalid (hallucinated)
int is_tool_allowed(const char *tool_name, ConversationState *state);

#endif // TOOL_EXECUTOR_H
