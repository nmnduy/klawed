/*
 * tool_sleep.h - Sleep tool implementation
 */

#ifndef TOOL_SLEEP_H
#define TOOL_SLEEP_H

#include <cjson/cJSON.h>

// Forward declaration of ConversationState
typedef struct ConversationState ConversationState;

/**
 * tool_sleep - Pauses execution for specified duration
 *
 * @param params JSON object with: { "duration": <seconds> }
 * @param state Conversation state (unused)
 * @return JSON object with status and duration
 */
cJSON* tool_sleep(cJSON *params, ConversationState *state);

#endif // TOOL_SLEEP_H
