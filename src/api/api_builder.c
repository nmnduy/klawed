/*
 * api_builder.c - API Request Builder
 */

#define _POSIX_C_SOURCE 200809L

#include "api_builder.h"
#include "../conversation/conversation_state.h"
#include "../openai_messages.h"
#include "../tool_utils.h"
#include "../logger.h"
#include "../model_capabilities.h"
#include "../tools/tool_definitions.h"
#include "../ui/print_helpers.h"
#include "../tui.h"

#ifndef TEST_BUILD
#include "../mcp.h"
#include "../explore_tools.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <strings.h>

/**
 * Check if prompt caching is enabled
 */
static int is_prompt_caching_enabled(void) {
    const char *disable_cache = getenv("DISABLE_PROMPT_CACHING");
    if (disable_cache && (strcmp(disable_cache, "1") == 0 ||
                          strcmp(disable_cache, "true") == 0 ||
                          strcmp(disable_cache, "TRUE") == 0)) {
        return 0;
    }
    return 1;
}

/**
 * Add cache_control to a JSON object (for content blocks)
 */
void add_cache_control(cJSON *obj) {
    cJSON *cache_ctrl = cJSON_CreateObject();
    cJSON_AddStringToObject(cache_ctrl, "type", "ephemeral");
    cJSON_AddItemToObject(obj, "cache_control", cache_ctrl);
}

/**
 * Build request JSON from conversation state (in OpenAI format)
 *
 * This is called by providers to get the request body.
 * Returns: Newly allocated JSON string (caller must free), or NULL on error
 */
char* build_request_json_from_state(ConversationState *state) {
    if (!state) {
        LOG_ERROR("ConversationState is NULL");
        return NULL;
    }

    if (conversation_state_lock(state) != 0) {
        return NULL;
    }

    // Ensure all tool calls have matching results before building request
#ifndef TEST_BUILD
    ensure_tool_results(state);
#endif

    char *json_str = NULL;

    // Check if prompt caching is enabled
    int enable_caching = is_prompt_caching_enabled();
    LOG_DEBUG("Building request (caching: %s, messages: %d)",
              enable_caching ? "enabled" : "disabled", state->count);

    // Build request body
    cJSON *request = cJSON_CreateObject();
    if (!request) {
        LOG_ERROR("Failed to allocate request object");
        goto unlock;
    }

    cJSON_AddStringToObject(request, "model", state->model);

    // Use context-aware max_tokens: cap to safe limit based on actual prompt tokens
    // context_buffer of 500 tokens for system prompts, tool definitions, etc.
    int safe_max_tokens = state->max_tokens;
    if (state->last_prompt_tokens > 0 && state->context_limit > 0) {
        safe_max_tokens = get_safe_max_tokens(state->model, state->last_prompt_tokens,
                                              state->max_tokens, 500);
    }
    cJSON_AddNumberToObject(request, "max_completion_tokens", safe_max_tokens);

    // Add messages in OpenAI format
    cJSON *messages_array = cJSON_CreateArray();
    if (!messages_array) {
        LOG_ERROR("Failed to allocate messages array");
        goto unlock;
    }
    for (int i = 0; i < state->count; i++) {
        // Handle auto-compaction messages specially
        if (state->messages[i].role == MSG_AUTO_COMPACTION) {
            for (int j = 0; j < state->messages[i].content_count; j++) {
                InternalContent *c = &state->messages[i].contents[j];
                if (c->type == INTERNAL_TEXT && c->text) {
                    cJSON *notice_msg = cJSON_CreateObject();
                    if (notice_msg) {
                        cJSON_AddStringToObject(notice_msg, "role", "system");
                        cJSON_AddStringToObject(notice_msg, "content", c->text);
                        cJSON_AddItemToArray(messages_array, notice_msg);
                    }
                    break;
                }
            }
            continue; // Skip normal processing for this message
        }

        cJSON *msg = cJSON_CreateObject();
        if (!msg) {
            LOG_ERROR("Failed to allocate message object");
            goto unlock;
        }

        // Determine role
        const char *role;
        if (state->messages[i].role == MSG_SYSTEM) {
            role = "system";
        } else if (state->messages[i].role == MSG_USER) {
            role = "user";
        } else if (state->messages[i].role == MSG_ASSISTANT) {
            role = "assistant";
        } else {
            // Unknown role - log warning and treat as assistant (fallback)
            LOG_WARN("Unhandled message role %d at index %d in api_builder, treating as assistant", state->messages[i].role, i);
            role = "assistant";
        }
        cJSON_AddStringToObject(msg, "role", role);

        // Determine if this is one of the last 3 messages (for cache breakpoint)
        // We want to cache the last few messages to speed up subsequent turns
        int is_recent_message = (i >= state->count - 3) && enable_caching;

        // Build content based on message type
        if (state->messages[i].role == MSG_SYSTEM) {
            // System messages: use content array with cache_control if enabled
            if (state->messages[i].content_count > 0 &&
                state->messages[i].contents[0].type == INTERNAL_TEXT) {

                // For system messages, use content array to support cache_control
                cJSON *content_array = cJSON_CreateArray();
                cJSON *text_block = cJSON_CreateObject();
                cJSON_AddStringToObject(text_block, "type", "text");
                cJSON_AddStringToObject(text_block, "text", state->messages[i].contents[0].text);

                // Add cache_control to system message if caching is enabled
                // This is the first cache breakpoint (system prompt)
                if (enable_caching) {
                    add_cache_control(text_block);
                }

                cJSON_AddItemToArray(content_array, text_block);
                cJSON_AddItemToObject(msg, "content", content_array);
            }
        } else if (state->messages[i].role == MSG_USER) {
            // User messages: check if it's tool results or plain text
            int has_tool_results = 0;
            for (int j = 0; j < state->messages[i].content_count; j++) {
                if (state->messages[i].contents[j].type == INTERNAL_TOOL_RESPONSE) {
                    has_tool_results = 1;
                    break;
                }
            }

            if (has_tool_results) {
                // First, add any text content as a user message
                int has_text_content = 0;
                for (int j = 0; j < state->messages[i].content_count; j++) {
                    InternalContent *cb = &state->messages[i].contents[j];
                    if (cb->type == INTERNAL_TEXT && cb->text && cb->text[0]) {
                        has_text_content = 1;
                        break;
                    }
                }

                if (has_text_content) {
                    // Add user message with text content
                    cJSON *content_array = cJSON_CreateArray();
                    for (int j = 0; j < state->messages[i].content_count; j++) {
                        InternalContent *cb = &state->messages[i].contents[j];
                        if (cb->type == INTERNAL_TEXT && cb->text && cb->text[0]) {
                            cJSON *text_block = cJSON_CreateObject();
                            cJSON_AddStringToObject(text_block, "type", "text");
                            cJSON_AddStringToObject(text_block, "text", cb->text);

                            // Add cache_control to the last user message
                            if (i == state->count - 1) {
                                add_cache_control(text_block);
                            }

                            cJSON_AddItemToArray(content_array, text_block);
                        }
                    }
                    cJSON_AddItemToObject(msg, "content", content_array);
                    cJSON_AddItemToArray(messages_array, msg);
                } else {
                    // No text content, free the unused msg object
                    cJSON_Delete(msg);
                }

                // Then add tool results as separate "tool" role messages
                for (int j = 0; j < state->messages[i].content_count; j++) {
                    InternalContent *cb = &state->messages[i].contents[j];
                    if (cb->type == INTERNAL_TOOL_RESPONSE) {
                        cJSON *tool_msg = cJSON_CreateObject();
                        cJSON_AddStringToObject(tool_msg, "role", "tool");
                        cJSON_AddStringToObject(tool_msg, "tool_call_id", cb->tool_id);
                        // Convert result to string
                        char *result_str = cJSON_PrintUnformatted(cb->tool_output);
                        cJSON_AddStringToObject(tool_msg, "content", result_str);
                        free(result_str);
                        cJSON_AddItemToArray(messages_array, tool_msg);
                    }
                }
                continue; // Continue to next message
            } else {
                // Regular user message - handle text and image content
                if (state->messages[i].content_count > 0) {
                    // Use content array for recent messages to support cache_control and mixed content
                    if (is_recent_message) {
                        cJSON *content_array = cJSON_CreateArray();

                        for (int j = 0; j < state->messages[i].content_count; j++) {
                            InternalContent *cb = &state->messages[i].contents[j];

                            if (cb->type == INTERNAL_TEXT && cb->text && cb->text[0]) {
                                // Text content (skip empty strings)
                                cJSON *text_block = cJSON_CreateObject();
                                cJSON_AddStringToObject(text_block, "type", "text");
                                cJSON_AddStringToObject(text_block, "text", cb->text);

                                // Add cache_control to the last user message
                                if (i == state->count - 1) {
                                    add_cache_control(text_block);
                                }

                                cJSON_AddItemToArray(content_array, text_block);
                            } else if (cb->type == INTERNAL_IMAGE) {
                                // Image content - OpenAI format
                                cJSON *image_block = cJSON_CreateObject();
                                cJSON_AddStringToObject(image_block, "type", "image_url");
                                cJSON *image_url = cJSON_CreateObject();

                                // Calculate required buffer size for data URL
                                size_t data_url_size = strlen("data:") + strlen(cb->mime_type) +
                                                     strlen(";base64,") + strlen(cb->base64_data) + 1;
                                char *data_url = malloc(data_url_size);
                                if (data_url) {
                                    snprintf(data_url, data_url_size, "data:%s;base64,%s",
                                             cb->mime_type, cb->base64_data);
                                    cJSON_AddStringToObject(image_url, "url", data_url);
                                    free(data_url);
                                }
                                cJSON_AddItemToObject(image_block, "image_url", image_url);
                                cJSON_AddItemToArray(content_array, image_block);
                            }
                        }

                        cJSON_AddItemToObject(msg, "content", content_array);
                    } else {
                        // For older messages, use simple string content (images not supported in simple format)
                        if (state->messages[i].contents[0].type == INTERNAL_TEXT) {
                            cJSON_AddStringToObject(msg, "content", state->messages[i].contents[0].text);
                        }
                    }
                }
                // Add the user message to the messages array
                cJSON_AddItemToArray(messages_array, msg);
            }
        } else {
            // Assistant messages
            cJSON *tool_calls = NULL;
            char *text_content = NULL;

            for (int j = 0; j < state->messages[i].content_count; j++) {
                InternalContent *cb = &state->messages[i].contents[j];

                if (cb->type == INTERNAL_TEXT && cb->text && cb->text[0]) {
                    // Only use non-empty text content
                    text_content = cb->text;
                } else if (cb->type == INTERNAL_TOOL_CALL) {
                    if (!tool_calls) {
                        tool_calls = cJSON_CreateArray();
                    }
                    cJSON *tool_call = cJSON_CreateObject();
                    cJSON_AddStringToObject(tool_call, "id", cb->tool_id);
                    cJSON_AddStringToObject(tool_call, "type", "function");
                    cJSON *function = cJSON_CreateObject();
                    cJSON_AddStringToObject(function, "name", cb->tool_name);
                    char *args_str = cJSON_PrintUnformatted(cb->tool_params);
                    cJSON_AddStringToObject(function, "arguments", args_str);
                    free(args_str);
                    cJSON_AddItemToObject(tool_call, "function", function);
                    cJSON_AddItemToArray(tool_calls, tool_call);
                }
            }

            // Add content (may be null if only tool calls)
            if (text_content) {
                cJSON_AddStringToObject(msg, "content", text_content);
            } else {
                cJSON_AddNullToObject(msg, "content");
            }

            if (tool_calls) {
                cJSON_AddItemToObject(msg, "tool_calls", tool_calls);
            }

            cJSON_AddItemToArray(messages_array, msg);
        }
    }

    cJSON_AddItemToObject(request, "messages", messages_array);

    // Add tools with cache_control support (including MCP tools if available)
    // In plan mode, exclude Bash, Subagent, Write, and Edit tools
    cJSON *tool_defs = get_tool_definitions(state, enable_caching);

    // Check for duplicate tool names before adding to request
    const char *duplicate_name = detect_duplicate_tool_names(tool_defs);
    if (duplicate_name) {
        LOG_ERROR("Duplicate tool name detected: '%s'", duplicate_name);
        if (state && state->tui) {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg),
                     "ERROR: Duplicate tool definition detected: '%s'. "
                     "This will cause API errors. Please check your tool configuration.",
                     duplicate_name);
            tui_add_conversation_line(state->tui, "[Error]", error_msg, COLOR_PAIR_ERROR);
        }
    }

    cJSON_AddItemToObject(request, "tools", tool_defs);

    conversation_state_unlock(state);
    state = NULL;

    json_str = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    LOG_DEBUG("Request built successfully (size: %zu bytes)", json_str ? strlen(json_str) : 0);
    return json_str;

unlock:
    conversation_state_unlock(state);
    if (request) {
        cJSON_Delete(request);
    }
    return NULL;
}
