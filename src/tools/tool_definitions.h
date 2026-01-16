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
cJSON* get_tool_definitions(struct ConversationState *state, int enable_caching);

#endif // TOOL_DEFINITIONS_H
