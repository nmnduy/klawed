#ifndef TOOL_REGISTRY_H
#define TOOL_REGISTRY_H

#include <cjson/cJSON.h>
#include "../conversation_state.h"

// Tool handler function type
typedef struct {
    const char *name;
    cJSON* (*handler)(cJSON *params, ConversationState *state);
} Tool;

// Get the array of registered tools
const Tool* get_registered_tools(void);

// Get the number of registered tools
int get_num_registered_tools(void);

// Find a tool by name
const Tool* find_tool_by_name(const char *name);

#endif // TOOL_REGISTRY_H
