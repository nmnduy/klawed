/*
 * tool_search.h - Grep search tool implementation
 */

#ifndef TOOL_SEARCH_H
#define TOOL_SEARCH_H

#include <cjson/cJSON.h>

// Forward declaration of ConversationState
typedef struct ConversationState ConversationState;

/**
 * tool_grep - Searches for patterns in files
 * 
 * @param params JSON object with: { "pattern": <search_pattern>, "path": <optional_path>, "max_results": <optional_limit> }
 * @param state Conversation state containing working_dir and additional_dirs
 * @return JSON object with matches array, match_count, total_matches, and optional warning
 */
cJSON* tool_grep(cJSON *params, ConversationState *state);

#endif // TOOL_SEARCH_H
