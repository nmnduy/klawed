/*
 * explore_tools.h - Explore subagent tools for web research and documentation
 */

#ifndef EXPLORE_TOOLS_H
#define EXPLORE_TOOLS_H

#include <cjson/cJSON.h>

// Check if explore mode is enabled (KLAWED_EXPLORE_MODE=1)
int is_explore_mode_enabled(void);

// Check if web_browse_agent binary is available
int is_web_agent_available(void);

// Tool implementations
cJSON* tool_web_search(cJSON *params, void *state);
cJSON* tool_web_read(cJSON *params, void *state);
cJSON* tool_context7_search(cJSON *params, void *state);
cJSON* tool_context7_docs(cJSON *params, void *state);
// Standalone web_browse_agent runner (available when binary is present, even without explore mode)
cJSON* tool_web_browse_agent(cJSON *params, void *state);

// Tool schema getters (returns JSON string)
const char* explore_tool_web_search_schema(void);
const char* explore_tool_web_read_schema(void);
const char* explore_tool_context7_search_schema(void);
const char* explore_tool_context7_docs_schema(void);
const char* explore_tool_web_browse_agent_schema(void);

#endif // EXPLORE_TOOLS_H
