#ifndef TOOL_DEFINITIONS_H
#define TOOL_DEFINITIONS_H

#include <cjson/cJSON.h>

// Forward declarations
struct ConversationState;

/**
 * get_tool_definitions - Build the complete tool definitions array for API
 * @state: Conversation state (may be NULL in test builds)
 * @enable_caching: Whether to add cache control markers to tools
 *
 * Returns a cJSON array containing all tool definitions in OpenAI-compatible format.
 * The returned cJSON object must be freed by the caller.
 *
 * Tool selection is based on:
 * - plan_mode: Excludes write-capable tools (Bash, Write, Edit, MultiEdit, Subagent)
 * - is_subagent: Excludes Subagent, CheckSubagentProgress, and InterruptSubagent
 * - KLAWED_DISABLE_TOOLS: Excludes tools listed in environment variable
 * - MCP enabled: Includes MCP resource and dynamic tools
 * - Explore mode: Includes web search and Context7 tools
 */
typedef enum {
    TOOL_SCHEMA_MESSAGES,
    TOOL_SCHEMA_RESPONSES
} ToolSchemaFormat;

void add_memory_tools(cJSON *tool_array, ToolSchemaFormat format);
cJSON* get_tool_definitions(struct ConversationState *state, int enable_caching);
cJSON* get_openai_subscription_tool_definitions(int enable_caching,
                                                ToolSchemaFormat format);

/**
 * detect_duplicate_tool_names - Check for duplicate tool names in the tool array
 * @tool_array: cJSON array of tool definitions
 *
 * Returns a pointer to the duplicate tool name if found, NULL otherwise.
 * The returned pointer references the name field in the JSON and must NOT be freed by caller.
 *
 * This function detects when the same tool name appears multiple times in the tool array,
 * which would cause API errors like "tool is already defined at toolConfig.tools.N".
 */
const char* detect_duplicate_tool_names(cJSON *tool_array);

#endif // TOOL_DEFINITIONS_H
