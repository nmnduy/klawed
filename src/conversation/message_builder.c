/*
 * message_builder.c - Message creation implementation
 */

#include "message_builder.h"
#include "conversation_state.h"
#include "content_types.h"
#include "../logger.h"
#include <stdlib.h>
#include <string.h>

void add_system_message(ConversationState *state, const char *text) {
    if (conversation_state_lock(state) != 0) {
        return;
    }

    if (state->count >= MAX_MESSAGES) {
        LOG_ERROR("Maximum message count reached");
        conversation_state_unlock(state);
        return;
    }

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_SYSTEM;
    msg->contents = calloc(1, sizeof(InternalContent));
    if (!msg->contents) {
        LOG_ERROR("Failed to allocate memory for message content");
        state->count--;
        conversation_state_unlock(state);
        return;
    }
    msg->content_count = 1;

    // calloc already zeros memory, but explicitly set for analyzer
    msg->contents[0].type = INTERNAL_TEXT;
    msg->contents[0].text = NULL;
    msg->contents[0].tool_id = NULL;
    msg->contents[0].tool_name = NULL;
    msg->contents[0].tool_params = NULL;
    msg->contents[0].tool_output = NULL;

    msg->contents[0].text = strdup(text);
    if (!msg->contents[0].text) {
        LOG_ERROR("Failed to duplicate message text");
        free(msg->contents);
        msg->contents = NULL;
        state->count--;
        conversation_state_unlock(state);
        return;
    }

    conversation_state_unlock(state);
}

void add_user_message(ConversationState *state, const char *text) {
    if (conversation_state_lock(state) != 0) {
        return;
    }

    if (state->count >= MAX_MESSAGES) {
        LOG_ERROR("Maximum message count reached");
        conversation_state_unlock(state);
        return;
    }

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_USER;
    msg->contents = calloc(1, sizeof(InternalContent));
    if (!msg->contents) {
        LOG_ERROR("Failed to allocate memory for message content");
        state->count--; // Rollback count increment
        conversation_state_unlock(state);
        return;
    }
    msg->content_count = 1;

    // calloc already zeros memory, but explicitly set for analyzer
    msg->contents[0].type = INTERNAL_TEXT;
    msg->contents[0].text = NULL;
    msg->contents[0].tool_id = NULL;
    msg->contents[0].tool_name = NULL;
    msg->contents[0].tool_params = NULL;
    msg->contents[0].tool_output = NULL;

    msg->contents[0].text = strdup(text);
    if (!msg->contents[0].text) {
        LOG_ERROR("Failed to duplicate message text");
        free(msg->contents);
        msg->contents = NULL;
        state->count--; // Rollback count increment
        conversation_state_unlock(state);
        return;
    }

    conversation_state_unlock(state);
}

int add_tool_results(ConversationState *state, InternalContent *results, int count) {
    LOG_DEBUG("add_tool_results: Adding %d tool results to conversation", count);

    if (conversation_state_lock(state) != 0) {
        LOG_ERROR("add_tool_results: Failed to acquire conversation lock");
        free_internal_contents(results, count);
        return -1;
    }

    if (state->count >= MAX_MESSAGES) {
        LOG_ERROR("add_tool_results: Cannot add results - maximum message count (%d) reached", MAX_MESSAGES);
        // Free results since they won't be added to state
        free_internal_contents(results, count);
        conversation_state_unlock(state);
        return -1;
    }

    // Log each tool result being added
    for (int i = 0; i < count; i++) {
        InternalContent *result = &results[i];
        LOG_DEBUG("add_tool_results: result[%d]: tool_id=%s, tool_name=%s, is_error=%d",
                  i, result->tool_id ? result->tool_id : "NULL",
                  result->tool_name ? result->tool_name : "NULL",
                  result->is_error);
    }

    // Find the assistant message that contains the tool calls we're responding to.
    // Search backwards from the end to find the most recent assistant message
    // with matching tool_call IDs.
    int insert_pos = state->count;  // Default: append at end
    int found_assistant_idx = -1;

    // First, find any tool_id from our results to match against
    const char *first_tool_id = NULL;
    for (int i = 0; i < count; i++) {
        if (results[i].tool_id) {
            first_tool_id = results[i].tool_id;
            break;
        }
    }

    if (first_tool_id) {
        // Search backwards for assistant message with this tool call
        for (int i = state->count - 1; i >= 0; i--) {
            InternalMessage *msg = &state->messages[i];
            if (msg->role == MSG_ASSISTANT) {
                for (int j = 0; j < msg->content_count; j++) {
                    InternalContent *c = &msg->contents[j];
                    if (c->type == INTERNAL_TOOL_CALL && c->tool_id &&
                        strcmp(c->tool_id, first_tool_id) == 0) {
                        found_assistant_idx = i;
                        insert_pos = i + 1;  // Insert right after this assistant message
                        LOG_DEBUG("add_tool_results: Found matching assistant message at index %d for tool_id=%s",
                                  i, first_tool_id);
                        break;
                    }
                }
                if (found_assistant_idx >= 0) {
                    break;
                }
            }
        }
    }

    if (found_assistant_idx < 0) {
        LOG_WARN("add_tool_results: Could not find matching assistant message, appending at end");
    }

    // If insert position is at the end, just append
    if (insert_pos == state->count) {
        InternalMessage *msg = &state->messages[state->count++];
        msg->role = MSG_USER;
        msg->contents = results;
        msg->content_count = count;
        LOG_INFO("add_tool_results: Successfully added %d tool results as msg[%d]", count, state->count - 1);
    } else {
        // Need to insert at a specific position - shift messages
        // Shift all messages from insert_pos onwards
        for (int i = state->count; i > insert_pos; i--) {
            state->messages[i] = state->messages[i - 1];
        }

        // Insert the new message
        InternalMessage *msg = &state->messages[insert_pos];
        msg->role = MSG_USER;
        msg->contents = results;
        msg->content_count = count;
        state->count++;

        LOG_INFO("add_tool_results: Inserted %d tool results at msg[%d] (after assistant msg[%d])",
                 count, insert_pos, found_assistant_idx);
    }

    conversation_state_unlock(state);
    return 0;
}
