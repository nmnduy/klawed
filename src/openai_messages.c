/*
 * openai_messages.c - OpenAI message format conversion
 */

#define _POSIX_C_SOURCE 200809L

#include "openai_messages.h"
#include "logger.h"
#include "klawed_internal.h"
#include "tools/tool_definitions.h"
#include "util/string_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <bsd/stdlib.h>
#include <string.h>

/**
 * Helper function to insert a message at a specific position in the conversation.
 * Shifts all messages from insert_pos onwards to make room.
 * Must be called with state locked.
 *
 * @param state - Conversation state
 * @param insert_pos - Position to insert at (0-indexed)
 * @param msg - Message to insert (contents are moved, not copied)
 * @return 0 on success, -1 on failure
 */
static int insert_message_at(ConversationState *state, int insert_pos, InternalMessage *msg) {
    if (state->count >= MAX_MESSAGES) {
        LOG_ERROR("insert_message_at: Cannot insert - maximum message count reached");
        return -1;
    }

    if (insert_pos < 0 || insert_pos > state->count) {
        LOG_ERROR("insert_message_at: Invalid position %d (count=%d)", insert_pos, state->count);
        return -1;
    }

    // Shift messages from insert_pos to end using memmove to avoid pointer aliasing
    // This properly moves the InternalMessage structs without duplicating the contents pointers
    if (insert_pos < state->count) {
        memmove(&state->messages[insert_pos + 1],
                &state->messages[insert_pos],
                (size_t)(state->count - insert_pos) * sizeof(InternalMessage));
    }

    // Insert the new message (move, not copy - msg's contents pointer is transferred)
    state->messages[insert_pos] = *msg;
    state->count++;

    LOG_DEBUG("insert_message_at: Inserted message at position %d, new count=%d", insert_pos, state->count);
    return 0;
}

/**
 * Ensure all tool calls have matching tool results.
 * If any are missing, inject synthetic "interrupted" results immediately
 * after the assistant message that made the tool call.
 * Must be called with state locked.
 */
void ensure_tool_results(ConversationState *state) {
    // Build set of tool_call IDs, their source message index, and whether they have results
    typedef struct {
        char *id;
        char *tool_name;
        int source_msg_idx;  // Index of assistant message that made this call
        int has_result;
    } ToolCallInfo;

    ToolCallInfo *tool_calls = NULL;
    int tool_call_count = 0;
    int tool_call_capacity = 0;

    LOG_DEBUG("ensure_tool_results: Starting scan of %d messages", state->count);

    // Scan messages to collect tool calls and check for results
    for (int i = 0; i < state->count; i++) {
        InternalMessage *msg = &state->messages[i];

        if (msg->role == MSG_ASSISTANT) {
            // Collect tool calls from assistant messages
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];
                if (c->type == INTERNAL_TOOL_CALL && c->tool_id) {
                    // Expand array if needed
                    if (tool_call_count >= tool_call_capacity) {
                        tool_call_capacity = tool_call_capacity == 0 ? 8 : tool_call_capacity * 2;
                        ToolCallInfo *new_calls = reallocarray(tool_calls, (size_t)tool_call_capacity, sizeof(ToolCallInfo));
                        if (!new_calls) {
                            LOG_ERROR("Failed to allocate memory for tool call tracking");
                            free(tool_calls);
                            return;
                        }
                        tool_calls = new_calls;
                    }

                    tool_calls[tool_call_count].id = c->tool_id;
                    tool_calls[tool_call_count].tool_name = c->tool_name;
                    tool_calls[tool_call_count].source_msg_idx = i;
                    tool_calls[tool_call_count].has_result = 0;
                    LOG_DEBUG("ensure_tool_results: Found tool call in msg[%d]: id=%s, tool=%s",
                              i, c->tool_id ? c->tool_id : "NULL",
                              c->tool_name ? c->tool_name : "NULL");
                    tool_call_count++;
                }
            }
        } else if (msg->role == MSG_USER) {
            // Check for tool results
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];
                if (c->type == INTERNAL_TOOL_RESPONSE && c->tool_id) {
                    LOG_DEBUG("ensure_tool_results: Found tool result in msg[%d]: id=%s, tool=%s, is_error=%d",
                              i, c->tool_id ? c->tool_id : "NULL",
                              c->tool_name ? c->tool_name : "NULL",
                              c->is_error);
                    // Mark this tool call as having a result
                    int found = 0;
                    for (int k = 0; k < tool_call_count; k++) {
                        if (tool_calls[k].id && strcmp(tool_calls[k].id, c->tool_id) == 0) {
                            tool_calls[k].has_result = 1;
                            found = 1;
                            LOG_DEBUG("ensure_tool_results: Matched result to tool_call[%d]", k);
                            break;
                        }
                    }
                    if (!found) {
                        LOG_WARN("ensure_tool_results: Tool result id=%s has no matching tool call", c->tool_id);
                    }
                }
            }
        }
    }

    // Find missing results and count per assistant message
    int missing_count = 0;
    for (int i = 0; i < tool_call_count; i++) {
        if (!tool_calls[i].has_result) {
            missing_count++;
            LOG_DEBUG("ensure_tool_results: Missing result for tool_call[%d]: id=%s, tool=%s, from msg[%d]",
                      i, tool_calls[i].id ? tool_calls[i].id : "NULL",
                      tool_calls[i].tool_name ? tool_calls[i].tool_name : "NULL",
                      tool_calls[i].source_msg_idx);
        }
    }

    LOG_DEBUG("ensure_tool_results: Summary: %d total tool calls, %d missing results",
              tool_call_count, missing_count);

    if (missing_count > 0) {
        LOG_WARN("Found %d tool call(s) without matching results - injecting synthetic results", missing_count);

        // Group missing tool calls by their source assistant message index
        // We need to insert synthetic results after each assistant message that has missing results
        // Process from highest index to lowest to avoid shifting issues

        // Find unique assistant message indices with missing results
        int *assistant_indices = NULL;
        int assistant_count = 0;
        int assistant_capacity = 0;

        for (int i = 0; i < tool_call_count; i++) {
            if (!tool_calls[i].has_result) {
                int idx = tool_calls[i].source_msg_idx;
                // Check if we already have this index
                int found = 0;
                for (int j = 0; j < assistant_count; j++) {
                    if (assistant_indices[j] == idx) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    if (assistant_count >= assistant_capacity) {
                        assistant_capacity = assistant_capacity == 0 ? 4 : assistant_capacity * 2;
                        int *new_indices = reallocarray(assistant_indices, (size_t)assistant_capacity, sizeof(int));
                        if (!new_indices) {
                            LOG_ERROR("Failed to allocate memory for assistant indices");
                            free(assistant_indices);
                            free(tool_calls);
                            return;
                        }
                        assistant_indices = new_indices;
                    }
                    assistant_indices[assistant_count++] = idx;
                }
            }
        }

        // Sort assistant indices in descending order (process from end to avoid index shifts)
        for (int i = 0; i < assistant_count - 1; i++) {
            for (int j = i + 1; j < assistant_count; j++) {
                if (assistant_indices[j] > assistant_indices[i]) {
                    int tmp = assistant_indices[i];
                    assistant_indices[i] = assistant_indices[j];
                    assistant_indices[j] = tmp;
                }
            }
        }

        // For each assistant message with missing results, insert synthetic results after it
        for (int a = 0; a < assistant_count; a++) {
            int asst_idx = assistant_indices[a];

            // Count missing results for this assistant message
            int missing_for_this = 0;
            for (int i = 0; i < tool_call_count; i++) {
                if (!tool_calls[i].has_result && tool_calls[i].source_msg_idx == asst_idx) {
                    missing_for_this++;
                }
            }

            if (missing_for_this == 0) continue;

            // Check if we have space
            if (state->count >= MAX_MESSAGES) {
                LOG_ERROR("Cannot inject tool results - maximum message count reached");
                free(assistant_indices);
                free(tool_calls);
                return;
            }

            // Create synthetic tool results for this assistant message
            InternalContent *synthetic_results = calloc((size_t)missing_for_this, sizeof(InternalContent));
            if (!synthetic_results) {
                LOG_ERROR("Failed to allocate memory for synthetic tool results");
                free(assistant_indices);
                free(tool_calls);
                return;
            }

            int result_idx = 0;
            for (int i = 0; i < tool_call_count; i++) {
                if (!tool_calls[i].has_result && tool_calls[i].source_msg_idx == asst_idx) {
                    synthetic_results[result_idx].type = INTERNAL_TOOL_RESPONSE;
                    synthetic_results[result_idx].tool_id = strdup(tool_calls[i].id);
                    synthetic_results[result_idx].tool_name = strdup(tool_calls[i].tool_name ? tool_calls[i].tool_name : "unknown");
                    synthetic_results[result_idx].is_error = 1;

                    // Create error output JSON
                    cJSON *error_output = cJSON_CreateObject();
                    cJSON_AddStringToObject(error_output, "error", "Tool execution was interrupted");
                    synthetic_results[result_idx].tool_output = error_output;

                    LOG_INFO("Injected synthetic result for tool_call_id=%s, tool=%s",
                             tool_calls[i].id,
                             tool_calls[i].tool_name ? tool_calls[i].tool_name : "unknown");
                    result_idx++;
                }
            }

            // Create the message to insert
            InternalMessage new_msg = {0};
            new_msg.role = MSG_USER;
            new_msg.contents = synthetic_results;
            new_msg.content_count = missing_for_this;

            // Insert immediately after the assistant message
            int insert_pos = asst_idx + 1;
            if (insert_message_at(state, insert_pos, &new_msg) != 0) {
                // Free contents on failure
                for (int i = 0; i < missing_for_this; i++) {
                    free(synthetic_results[i].tool_id);
                    free(synthetic_results[i].tool_name);
                    if (synthetic_results[i].tool_output) {
                        cJSON_Delete(synthetic_results[i].tool_output);
                    }
                }
                free(synthetic_results);
                free(assistant_indices);
                free(tool_calls);
                return;
            }

            LOG_INFO("ensure_tool_results: Inserted synthetic results at msg[%d] with %d tool results",
                     insert_pos, missing_for_this);
        }

        free(assistant_indices);
    } else {
        LOG_DEBUG("ensure_tool_results: All tool calls have matching results - no action needed");
    }

    free(tool_calls);
}

/**
 * Build OpenAI request JSON from internal message format (with reasoning_content support)
 */
cJSON* build_openai_request_with_reasoning(ConversationState *state, int enable_caching, int include_reasoning_content) {
    if (!state) {
        LOG_ERROR("ConversationState is NULL");
        return NULL;
    }

    if (conversation_state_lock(state) != 0) {
        return NULL;
    }

    // Ensure all tool calls have matching results before building request
    ensure_tool_results(state);

    LOG_DEBUG("Building OpenAI request (messages: %d, caching: %s, reasoning: %s)",
              state->count, enable_caching ? "enabled" : "disabled",
              include_reasoning_content ? "preserve" : "discard");

    cJSON *request = cJSON_CreateObject();
    if (!request) {
        conversation_state_unlock(state);
        return NULL;
    }

    cJSON_AddStringToObject(request, "model", state->model);
    cJSON_AddNumberToObject(request, "max_completion_tokens", state->max_tokens);

    cJSON *messages_array = cJSON_CreateArray();
    if (!messages_array) {
        cJSON_Delete(request);
        conversation_state_unlock(state);
        return NULL;
    }

    // Convert each internal message to OpenAI format
    for (int i = 0; i < state->count; i++) {
        InternalMessage *msg = &state->messages[i];

        // Determine if this is a recent message (for cache breakpoints)
        int is_recent_message = (i >= state->count - 3) && enable_caching;

        if (msg->role == MSG_SYSTEM) {
            // System messages
            cJSON *sys_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(sys_msg, "role", "system");

            // Find text content
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];
                if (c->type == INTERNAL_TEXT && c->text) {
                    // Use content array for cache_control support
                    if (enable_caching) {
                        cJSON *content_array = cJSON_CreateArray();
                        cJSON *text_block = cJSON_CreateObject();
                        cJSON_AddStringToObject(text_block, "type", "text");
                        cJSON_AddStringToObject(text_block, "text", c->text);
                        add_cache_control(text_block);
                        cJSON_AddItemToArray(content_array, text_block);
                        cJSON_AddItemToObject(sys_msg, "content", content_array);
                    } else {
                        cJSON_AddStringToObject(sys_msg, "content", c->text);
                    }
                    break;
                }
            }

            cJSON_AddItemToArray(messages_array, sys_msg);
        }
        else if (msg->role == MSG_USER) {
            // User messages - may contain text or tool responses
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];

                if (c->type == INTERNAL_TEXT && c->text && c->text[0]) {
                    // Regular user text (skip empty strings)
                    cJSON *user_msg = cJSON_CreateObject();
                    cJSON_AddStringToObject(user_msg, "role", "user");

                    // Use content array for recent messages to support cache_control
                    if (is_recent_message && i == state->count - 1) {
                        cJSON *content_array = cJSON_CreateArray();
                        cJSON *text_block = cJSON_CreateObject();
                        cJSON_AddStringToObject(text_block, "type", "text");
                        cJSON_AddStringToObject(text_block, "text", c->text);
                        add_cache_control(text_block);
                        cJSON_AddItemToArray(content_array, text_block);
                        cJSON_AddItemToObject(user_msg, "content", content_array);
                    } else {
                        cJSON_AddStringToObject(user_msg, "content", c->text);
                    }

                    cJSON_AddItemToArray(messages_array, user_msg);
                }
                else if (c->type == INTERNAL_TOOL_RESPONSE) {
                    // Tool response - OpenAI uses "tool" role
                    cJSON *tool_msg = cJSON_CreateObject();
                    cJSON_AddStringToObject(tool_msg, "role", "tool");
                    cJSON_AddStringToObject(tool_msg, "tool_call_id", c->tool_id);

                    // Convert output to string
                    char *output_str = cJSON_PrintUnformatted(c->tool_output);
                    cJSON_AddStringToObject(tool_msg, "content", output_str ? output_str : "{}");
                    free(output_str);

                    cJSON_AddItemToArray(messages_array, tool_msg);
                }
            }
        }
        else if (msg->role == MSG_ASSISTANT) {
            // Assistant messages - may contain text and/or tool calls
            cJSON *asst_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(asst_msg, "role", "assistant");

            // Collect text content and reasoning_content (skip empty strings for text)
            // Also check for reasoning_content on empty text blocks or tool calls
            char *text_content = NULL;
            char *reasoning_content_str = NULL;
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];
                if (c->type == INTERNAL_TEXT) {
                    // Capture non-empty text content
                    if (c->text && c->text[0]) {
                        text_content = c->text;
                    }
                    // Capture reasoning_content even if text is empty
                    // (Moonshot/Kimi returns reasoning_content with empty content string)
                    if (include_reasoning_content && c->reasoning_content && !reasoning_content_str) {
                        reasoning_content_str = c->reasoning_content;
                        LOG_DEBUG("Found reasoning_content on text content block (text %s)",
                                  text_content ? "present" : "empty");
                    }
                    if (text_content) break;  // Stop if we found non-empty text
                }
            }

            // If no reasoning_content found in text blocks, check tool calls
            // (Some providers may have reasoning_content on tool call blocks)
            if (include_reasoning_content && !reasoning_content_str) {
                for (int j = 0; j < msg->content_count; j++) {
                    InternalContent *c = &msg->contents[j];
                    if (c->type == INTERNAL_TOOL_CALL && c->reasoning_content) {
                        reasoning_content_str = c->reasoning_content;
                        LOG_DEBUG("Found reasoning_content on tool call content block");
                        break;
                    }
                }
            }

            // Collect tool calls
            cJSON *tool_calls = NULL;
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];
                if (c->type == INTERNAL_TOOL_CALL) {
                    if (!tool_calls) {
                        tool_calls = cJSON_CreateArray();
                    }

                    cJSON *tc = cJSON_CreateObject();
                    cJSON_AddStringToObject(tc, "id", c->tool_id);
                    cJSON_AddStringToObject(tc, "type", "function");

                    cJSON *func = cJSON_CreateObject();
                    cJSON_AddStringToObject(func, "name", c->tool_name);

                    char *args_str = cJSON_PrintUnformatted(c->tool_params);
                    cJSON_AddStringToObject(func, "arguments", args_str ? args_str : "{}");
                    free(args_str);

                    cJSON_AddItemToObject(tc, "function", func);
                    cJSON_AddItemToArray(tool_calls, tc);
                }
            }

            // Add content (required field in OpenAI API)
            // Note: Moonshot/Kimi requires empty string "" instead of null when reasoning_content is present
            if (text_content) {
                cJSON_AddStringToObject(asst_msg, "content", text_content);
            } else if (include_reasoning_content && reasoning_content_str) {
                // Moonshot/Kimi: use empty string when reasoning_content is present
                cJSON_AddStringToObject(asst_msg, "content", "");
            } else {
                cJSON_AddNullToObject(asst_msg, "content");
            }

            // Add reasoning_content if preserving it (for Moonshot/Kimi)
            if (reasoning_content_str) {
                cJSON_AddStringToObject(asst_msg, "reasoning_content", reasoning_content_str);
                LOG_DEBUG("Including reasoning_content in assistant message (msg %d)", i);
            }

            // Add tool_calls if present
            if (tool_calls) {
                cJSON_AddItemToObject(asst_msg, "tool_calls", tool_calls);
            } else {
                cJSON_Delete(tool_calls);  // Free if empty
            }

            cJSON_AddItemToArray(messages_array, asst_msg);
        }
        else if (msg->role == MSG_AUTO_COMPACTION) {
            // Auto-compaction notice - send as system message so model sees it
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];
                if (c->type == INTERNAL_TEXT && c->text) {
                    cJSON *notice_msg = cJSON_CreateObject();
                    cJSON_AddStringToObject(notice_msg, "role", "system");
                    cJSON_AddStringToObject(notice_msg, "content", c->text);
                    cJSON_AddItemToArray(messages_array, notice_msg);
                    break;
                }
            }
        }
        else {
            // Unknown message role - log warning but skip the message
            LOG_WARN("Unhandled message role %d at index %d, skipping", msg->role, i);
        }
    }

    cJSON_AddItemToObject(request, "messages", messages_array);
    conversation_state_unlock(state);

    // Validate request structure - scan for tool calls without results
    LOG_DEBUG("Validating request structure before sending to API");
    int msg_count = cJSON_GetArraySize(messages_array);
    int tool_call_count = 0;
    int tool_result_count = 0;

    for (int i = 0; i < msg_count; i++) {
        cJSON *msg = cJSON_GetArrayItem(messages_array, i);
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        if (!role || !cJSON_IsString(role)) continue;

        if (strcmp(role->valuestring, "assistant") == 0) {
            cJSON *tool_calls = cJSON_GetObjectItem(msg, "tool_calls");
            if (tool_calls && cJSON_IsArray(tool_calls)) {
                int calls_in_msg = cJSON_GetArraySize(tool_calls);
                tool_call_count += calls_in_msg;
                LOG_DEBUG("Validation: Message[%d] (assistant) has %d tool_calls", i, calls_in_msg);
            }
        } else if (strcmp(role->valuestring, "user") == 0) {
            // Count tool response content blocks (Anthropic format: user messages with type="tool_result")
            cJSON *content = cJSON_GetObjectItem(msg, "content");
            if (content && cJSON_IsArray(content)) {
                int content_count = cJSON_GetArraySize(content);
                for (int j = 0; j < content_count; j++) {
                    cJSON *item = cJSON_GetArrayItem(content, j);
                    cJSON *type = cJSON_GetObjectItem(item, "type");
                    if (type && cJSON_IsString(type) && strcmp(type->valuestring, "tool_result") == 0) {
                        tool_result_count++;
                        LOG_DEBUG("Validation: Message[%d] (user) has tool_result content block", i);
                    }
                }
                if (content_count > 0) {
                    LOG_DEBUG("Validation: Message[%d] (user) has %d content blocks", i, content_count);
                }
            }
        } else if (strcmp(role->valuestring, "tool") == 0) {
            // OpenAI format: separate tool messages
            tool_result_count++;
            LOG_DEBUG("Validation: Message[%d] (tool) counted as tool result", i);
        }
    }

    LOG_INFO("Request validation: %d messages, %d tool_calls, %d tool_results",
             msg_count, tool_call_count, tool_result_count);

    if (tool_call_count > tool_result_count) {
        LOG_WARN("Request may be invalid: %d tool_calls but only %d tool_results",
                 tool_call_count, tool_result_count);
    }

    // Add tools with cache_control support (including MCP tools if available)
    // In plan mode, exclude Bash, Write, and Edit tools
    cJSON *tool_defs = get_tool_definitions(state, enable_caching);
    cJSON_AddItemToObject(request, "tools", tool_defs);

    LOG_DEBUG("OpenAI request built successfully");
    return request;
}

/**
 * Build OpenAI request JSON from internal message format
 */
cJSON* build_openai_request(ConversationState *state, int enable_caching) {
    // Default: don't include reasoning_content (DeepSeek/OpenAI behavior)
    return build_openai_request_with_reasoning(state, enable_caching, 0);
}

/**
 * Parse OpenAI response into internal message format
 */
void parse_openai_response(cJSON *response, InternalMessage *out) {
    InternalMessage msg = {0};
    msg.role = MSG_ASSISTANT;

    if (!response) {
        LOG_ERROR("Response is NULL");
        msg.contents = NULL;
        msg.content_count = 0;
        *out = msg;
        return;
    }

    cJSON *choices = cJSON_GetObjectItem(response, "choices");
    if (!choices || !cJSON_IsArray(choices)) {
        LOG_ERROR("Invalid response: missing 'choices' array");
        msg.contents = NULL;
        msg.content_count = 0;
        *out = msg;
        return;
    }

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    if (!choice) {
        LOG_ERROR("Invalid response: empty 'choices' array");
        msg.contents = NULL;
        msg.content_count = 0;
        *out = msg;
        return;
    }

    cJSON *message = cJSON_GetObjectItem(choice, "message");
    if (!message) {
        LOG_ERROR("Invalid response: missing 'message' object");
        msg.contents = NULL;
        msg.content_count = 0;
        *out = msg;
        return;
    }

    // Count content blocks
    int count = 0;
    cJSON *content = cJSON_GetObjectItem(message, "content");
    cJSON *reasoning_content = cJSON_GetObjectItem(message, "reasoning_content");
    if (content && cJSON_IsString(content) && content->valuestring) {
        count++;
    }

    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        count += cJSON_GetArraySize(tool_calls);
    }

    if (count == 0) {
        LOG_WARN("Response has no content or tool_calls");
        msg.contents = NULL;
        msg.content_count = 0;
        *out = msg;
        return;
    }

    // Allocate content array
    msg.contents = calloc((size_t)count, sizeof(InternalContent));
    if (!msg.contents) {
        LOG_ERROR("Failed to allocate content array");
        *out = msg;
        return;
    }
    msg.content_count = count;

    int idx = 0;
    int reasoning_content_stored = 0;

    // Parse text content
    if (content && cJSON_IsString(content) && content->valuestring) {
        msg.contents[idx].type = INTERNAL_TEXT;
        msg.contents[idx].text = strdup_trim(content->valuestring);
        if (!msg.contents[idx].text) {
            LOG_ERROR("Failed to duplicate text content");
        }

        // Store reasoning_content on text block if present (for thinking models)
        if (reasoning_content && cJSON_IsString(reasoning_content) && reasoning_content->valuestring) {
            msg.contents[idx].reasoning_content = strdup(reasoning_content->valuestring);
            if (msg.contents[idx].reasoning_content) {
                LOG_DEBUG("Stored reasoning_content (%zu bytes) on text content",
                          strlen(msg.contents[idx].reasoning_content));
                reasoning_content_stored = 1;
            }
        }
        idx++;
    }

    // Parse tool calls
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        cJSON *tc = NULL;
        cJSON_ArrayForEach(tc, tool_calls) {
            if (idx >= count) break;

            cJSON *id = cJSON_GetObjectItem(tc, "id");
            cJSON *func = cJSON_GetObjectItem(tc, "function");
            if (!func) continue;

            cJSON *name = cJSON_GetObjectItem(func, "name");
            cJSON *arguments = cJSON_GetObjectItem(func, "arguments");

            if (!id || !name || !arguments) {
                LOG_WARN("Malformed tool_call, skipping");
                continue;
            }

            msg.contents[idx].type = INTERNAL_TOOL_CALL;

            // Store reasoning_content on first tool call if no text content existed
            // This is needed for Moonshot/Kimi which requires reasoning_content in tool call messages
            if (!reasoning_content_stored && reasoning_content &&
                cJSON_IsString(reasoning_content) && reasoning_content->valuestring) {
                msg.contents[idx].reasoning_content = strdup(reasoning_content->valuestring);
                if (msg.contents[idx].reasoning_content) {
                    LOG_DEBUG("Stored reasoning_content (%zu bytes) on tool call (no text content)",
                              strlen(msg.contents[idx].reasoning_content));
                    reasoning_content_stored = 1;
                }
            }

            // Copy tool_id
            msg.contents[idx].tool_id = strdup(id->valuestring);

            // Copy tool_name
            msg.contents[idx].tool_name = strdup(name->valuestring);

            // Parse arguments JSON string to cJSON object
            const char *args_str = arguments->valuestring;
            msg.contents[idx].tool_params = cJSON_Parse(args_str ? args_str : "{}");
            if (!msg.contents[idx].tool_params) {
                LOG_WARN("Failed to parse tool arguments, using empty object");
                msg.contents[idx].tool_params = cJSON_CreateObject();
            }

            idx++;
        }
    }

    LOG_DEBUG("Parsed OpenAI response: %d content blocks", msg.content_count);
    *out = msg;
}

/**
 * Free internal message contents
 */
void free_internal_message(InternalMessage *msg) {
    if (!msg) return;

    // Check if contents is valid before trying to free individual elements
    if (!msg->contents) {
        msg->content_count = 0;
        return;
    }

    // Safety check: if content_count is negative or very large, something is wrong
    if (msg->content_count < 0 || msg->content_count > 10000) {
        LOG_ERROR("Invalid content_count in free_internal_message: %d", msg->content_count);
        free(msg->contents);
        msg->contents = NULL;
        msg->content_count = 0;
        return;
    }

    for (int i = 0; i < msg->content_count; i++) {
        InternalContent *c = &msg->contents[i];

        // Only free pointers that are non-NULL
        if (c->text) {
            free(c->text);
            c->text = NULL;
        }
        if (c->tool_id) {
            free(c->tool_id);
            c->tool_id = NULL;
        }
        if (c->tool_name) {
            free(c->tool_name);
            c->tool_name = NULL;
        }
        if (c->reasoning_content) {
            free(c->reasoning_content);
            c->reasoning_content = NULL;
        }

        if (c->tool_params) {
            cJSON_Delete(c->tool_params);
            c->tool_params = NULL;
        }
        if (c->tool_output) {
            cJSON_Delete(c->tool_output);
            c->tool_output = NULL;
        }
    }

    free(msg->contents);
    msg->contents = NULL;
    msg->content_count = 0;
}
