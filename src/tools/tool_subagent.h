/*
 * tool_subagent.h - Subagent management tools
 */

#ifndef TOOL_SUBAGENT_H
#define TOOL_SUBAGENT_H

#include <cjson/cJSON.h>

// Forward declaration of ConversationState
typedef struct ConversationState ConversationState;

/**
 * tool_subagent - Spawns a subagent process for task delegation
 * 
 * @param params JSON object with: { "prompt": <task_description>, "timeout": <optional_seconds> }
 * @param state Conversation state containing working_dir and subagent_manager
 * @return JSON object with pid, log_file, timeout_seconds, and message
 */
cJSON* tool_subagent(cJSON *params, ConversationState *state);

/**
 * tool_check_subagent_progress - Checks progress of a running subagent
 * 
 * @param params JSON object with: { "pid": <process_id>, "log_file": <optional_log_path>, "tail_lines": <optional_line_count> }
 * @param state Conversation state containing interrupt flag
 * @return JSON object with status, progress info, and log tail
 */
cJSON* tool_check_subagent_progress(cJSON *params, ConversationState *state);

/**
 * tool_interrupt_subagent - Interrupts a running subagent
 * 
 * @param params JSON object with: { "pid": <process_id> }
 * @param state Conversation state containing interrupt flag
 * @return JSON object with kill status and message
 */
cJSON* tool_interrupt_subagent(cJSON *params, ConversationState *state);

#endif // TOOL_SUBAGENT_H
