/*
 * message_parser.c - OpenAI message format parsing implementation
 */

#include "message_parser.h"
#include "conversation_state.h"
#include "../logger.h"
#include <stdlib.h>
#include <string.h>

void add_assistant_message_openai(ConversationState *state, cJSON *message) {
    if (conversation_state_lock(state) != 0) {
        return;
    }

    if (state->count >= MAX_MESSAGES) {
        LOG_ERROR("Maximum message count reached");
        conversation_state_unlock(state);
        return;
    }

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_ASSISTANT;

    // Count content blocks (text + tool calls)
    int content_count = 0;
    cJSON *content = cJSON_GetObjectItem(message, "content");
    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");

    if (content && cJSON_IsString(content) && content->valuestring) {
        content_count++;
    }

    // Count VALID tool calls (those with 'function' field)
    int tool_calls_count = 0;
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        int array_size = cJSON_GetArraySize(tool_calls);
        for (int i = 0; i < array_size; i++) {
            cJSON *tool_call = cJSON_GetArrayItem(tool_calls, i);
            cJSON *function = cJSON_GetObjectItem(tool_call, "function");
            cJSON *id = cJSON_GetObjectItem(tool_call, "id");
            if (function && id && cJSON_IsString(id)) {
                tool_calls_count++;
            } else {
                LOG_WARN("Skipping malformed tool_call at index %d (missing 'function' or 'id' field)", i);
            }
        }
        content_count += tool_calls_count;
    }

    // Ensure we have at least some content
    if (content_count == 0) {
        LOG_WARN("Assistant message has no content");
        state->count--; // Rollback count increment
        conversation_state_unlock(state);
        return;
    }

    msg->contents = calloc((size_t)content_count, sizeof(InternalContent));
    if (!msg->contents) {
        LOG_ERROR("Failed to allocate memory for message content");
        state->count--; // Rollback count increment
        conversation_state_unlock(state);
        return;
    }
    msg->content_count = content_count;

    int idx = 0;

    // Add text content if present
    if (content && cJSON_IsString(content) && content->valuestring) {
        msg->contents[idx].type = INTERNAL_TEXT;
        msg->contents[idx].text = strdup(content->valuestring);
        if (!msg->contents[idx].text) {
            LOG_ERROR("Failed to duplicate message text");
            free(msg->contents);
            msg->contents = NULL;
            state->count--;
            conversation_state_unlock(state);
            return;
        }
        idx++;
    }

    // Add tool calls if present
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        int array_size = cJSON_GetArraySize(tool_calls);
        for (int i = 0; i < array_size; i++) {
            cJSON *tool_call = cJSON_GetArrayItem(tool_calls, i);
            cJSON *id = cJSON_GetObjectItem(tool_call, "id");
            cJSON *function = cJSON_GetObjectItem(tool_call, "function");

            // Skip malformed tool calls (already logged warning during counting)
            if (!function || !id || !cJSON_IsString(id)) {
                continue;
            }

            cJSON *name = cJSON_GetObjectItem(function, "name");
            cJSON *arguments = cJSON_GetObjectItem(function, "arguments");

            msg->contents[idx].type = INTERNAL_TOOL_CALL;
            msg->contents[idx].tool_id = strdup(id->valuestring);
            if (!msg->contents[idx].tool_id) {
                LOG_ERROR("Failed to duplicate tool use ID");
                // Cleanup previously allocated content
                for (int j = 0; j < idx; j++) {
                    free(msg->contents[j].text);
                    free(msg->contents[j].tool_id);
                    free(msg->contents[j].tool_name);
                }
                free(msg->contents);
                msg->contents = NULL;
                state->count--;
                conversation_state_unlock(state);
                return;
            }
            msg->contents[idx].tool_name = strdup(name->valuestring);
            if (!msg->contents[idx].tool_name) {
                LOG_ERROR("Failed to duplicate tool name");
                free(msg->contents[idx].tool_id);
                // Cleanup previously allocated content
                for (int j = 0; j < idx; j++) {
                    free(msg->contents[j].text);
                    free(msg->contents[j].tool_id);
                    free(msg->contents[j].tool_name);
                }
                free(msg->contents);
                msg->contents = NULL;
                state->count--;
                conversation_state_unlock(state);
                return;
            }

            // Parse arguments string as JSON
            if (arguments && cJSON_IsString(arguments)) {
                msg->contents[idx].tool_params = cJSON_Parse(arguments->valuestring);
                if (!msg->contents[idx].tool_params) {
                    LOG_WARN("Failed to parse tool arguments, using empty object");
                    msg->contents[idx].tool_params = cJSON_CreateObject();
                }
            } else {
                msg->contents[idx].tool_params = cJSON_CreateObject();
            }
            idx++;
        }
    }

    conversation_state_unlock(state);
}
