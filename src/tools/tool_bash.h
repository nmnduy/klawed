/*
 * tool_bash.h - Bash command execution tool
 */

#ifndef TOOL_BASH_H
#define TOOL_BASH_H

#include <cjson/cJSON.h>

// Forward declaration of ConversationState
typedef struct ConversationState ConversationState;

/**
 * tool_bash - Executes bash commands with timeout protection
 * 
 * @param params JSON object with: { "command": <command>, "timeout": <optional_seconds> }
 * @param state Conversation state containing interrupt flag
 * @return JSON object with exit_code, output, and optional timeout_error/truncation_warning
 */
cJSON* tool_bash(cJSON *params, ConversationState *state);

#endif // TOOL_BASH_H
