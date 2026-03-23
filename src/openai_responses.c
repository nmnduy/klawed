/*
 * openai_responses.c - OpenAI Responses API format conversion
 *
 * Converts between internal vendor-agnostic message format
 * and OpenAI's Responses API format (/v1/responses endpoint)
 */

#define _POSIX_C_SOURCE 200809L

#include "openai_responses.h"
#include "openai_provider.h"
#include "openai_messages.h"  // For ensure_tool_results
#include "logger.h"
#include "http_client.h"
#include "klawed_internal.h"
#include "mcp.h"
#include "arena.h"
#include "tool_utils.h"
#include "tools/tool_definitions.h"
#include "dynamic_tools.h"
#include "util/string_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <bsd/stdlib.h>
#include <bsd/string.h>
#include <string.h>

/**
 * Duplicate a string using arena allocation if arena is provided
 * Falls back to strdup if arena is NULL
 */
static char* arena_strdup(Arena *arena, const char *str) {
    if (!str) return NULL;

    if (arena) {
        size_t len = strlen(str) + 1;
        char *copy = arena_alloc(arena, len);
        if (copy) {
            memcpy(copy, str, len);
        }
        return copy;
    } else {
        return strdup(str);
    }
}

/**
 * Build OpenAI Responses API request JSON from internal message format
 *
 * Converts InternalMessage[] to OpenAI's Responses API format:
 * - input: array of items with types like "input_text", "input_image", etc.
 * - output: array of items with types like "output_text", "refusal", etc.
 *
 * @param state - Conversation state with internal messages
 * @param enable_caching - Whether to add cache_control markers
 * @return JSON object with OpenAI Responses API request (caller must free), or NULL on error
 */
cJSON* build_openai_responses_request(ConversationState *state, int enable_caching) {
    if (!state) {
        LOG_ERROR("ConversationState is NULL");
        return NULL;
    }

    if (conversation_state_lock(state) != 0) {
        return NULL;
    }

    // Ensure all tool calls have matching results before building request
    ensure_tool_results(state);

    LOG_DEBUG("Building OpenAI Responses API request (messages: %d, caching: %s)",
              state->count, enable_caching ? "enabled" : "disabled");

    cJSON *request = cJSON_CreateObject();
    if (!request) {
        conversation_state_unlock(state);
        return NULL;
    }

    cJSON_AddStringToObject(request, "model", state->model);
    cJSON_AddNumberToObject(request, "max_output_tokens", state->max_tokens);

    // Create conversation items for Responses API
    // The Responses API uses a flat "input" array that can contain both
    // input items and previous output items (for multi-turn conversations)
    cJSON *input_array = cJSON_CreateArray();
    if (!input_array) {
        cJSON_Delete(request);
        conversation_state_unlock(state);
        return NULL;
    }

    // Convert each internal message to Responses API format
    for (int i = 0; i < state->count; i++) {
        InternalMessage *msg = &state->messages[i];

        if (msg->role == MSG_SYSTEM) {
            // System messages become instructions in Responses API
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];
                if (c->type == INTERNAL_TEXT && c->text) {
                    cJSON_AddStringToObject(request, "instructions", c->text);
                    break;
                }
            }
        }
        else if (msg->role == MSG_USER) {
            // User messages - may contain text or tool responses
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];

                if (c->type == INTERNAL_TEXT && c->text && c->text[0]) {
                    // Regular user text -> message item (skip empty strings)
                    cJSON *text_item = cJSON_CreateObject();
                    cJSON_AddStringToObject(text_item, "type", "message");
                    cJSON_AddStringToObject(text_item, "role", "user");
                    cJSON *content_array = cJSON_CreateArray();
                    cJSON *content_obj = cJSON_CreateObject();
                    cJSON_AddStringToObject(content_obj, "type", "input_text");
                    cJSON_AddStringToObject(content_obj, "text", c->text);
                    cJSON_AddItemToArray(content_array, content_obj);
                    cJSON_AddItemToObject(text_item, "content", content_array);
                    cJSON_AddItemToArray(input_array, text_item);
                }
                else if (c->type == INTERNAL_TOOL_RESPONSE) {
                    // Tool response - wrap in message with role "user"
                    // Use input_text type for tool output (function_call_output is not supported)
                    cJSON *msg_item = cJSON_CreateObject();
                    cJSON_AddStringToObject(msg_item, "type", "message");
                    cJSON_AddStringToObject(msg_item, "role", "user");

                    cJSON *content_array = cJSON_CreateArray();
                    cJSON *tool_result = cJSON_CreateObject();
                    cJSON_AddStringToObject(tool_result, "type", "input_text");

                    // Wrap tool output with its call id inside the text payload. The Responses
                    // API rejects unknown fields on input_text (e.g., call_id), so we encode
                    // the call_id alongside the output JSON inside the text field.
                    cJSON *wrapped = cJSON_CreateObject();
                    cJSON_AddStringToObject(wrapped, "tool_call_id", c->tool_id ? c->tool_id : "");
                    cJSON_AddItemToObject(wrapped, "output",
                        cJSON_Duplicate(c->tool_output, /*recurse*/1));

                    char *output_str = cJSON_PrintUnformatted(wrapped);
                    cJSON_Delete(wrapped);

                    cJSON_AddStringToObject(tool_result, "text", output_str ? output_str : "{}");
                    free(output_str);

                    cJSON_AddItemToArray(content_array, tool_result);
                    cJSON_AddItemToObject(msg_item, "content", content_array);
                    cJSON_AddItemToArray(input_array, msg_item);
                }
            }
        }
        else if (msg->role == MSG_ASSISTANT) {
            // Assistant messages - may contain text and/or tool calls
            // For Responses API multi-turn conversations, we need to include
            // previous assistant messages as output items in the input array.
            // This allows the model to see the conversation history.

            // Check if there's text content or tool calls
            int has_content = 0;
            cJSON *content_array = cJSON_CreateArray();

            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];
                if (c->type == INTERNAL_TEXT && c->text && c->text[0]) {
                    // Add output_text to content array (skip empty strings)
                    cJSON *text_obj = cJSON_CreateObject();
                    cJSON_AddStringToObject(text_obj, "type", "output_text");
                    cJSON_AddStringToObject(text_obj, "text", c->text);
                    cJSON_AddItemToArray(content_array, text_obj);
                    has_content = 1;
                }
                else if (c->type == INTERNAL_TOOL_CALL) {
                    // Add function_call to content array
                    cJSON *func_obj = cJSON_CreateObject();
                    cJSON_AddStringToObject(func_obj, "type", "function_call");
                    cJSON_AddStringToObject(func_obj, "id", c->tool_id);
                    cJSON_AddStringToObject(func_obj, "name", c->tool_name);

                    // Arguments as JSON string
                    char *args_str = cJSON_PrintUnformatted(c->tool_params);
                    cJSON_AddStringToObject(func_obj, "arguments", args_str ? args_str : "{}");
                    free(args_str);

                    cJSON_AddItemToArray(content_array, func_obj);
                    has_content = 1;
                }
            }

            if (has_content) {
                // Create message item with assistant role
                cJSON *msg_item = cJSON_CreateObject();
                cJSON_AddStringToObject(msg_item, "type", "message");
                cJSON_AddStringToObject(msg_item, "role", "assistant");
                cJSON_AddItemToObject(msg_item, "content", content_array);
                cJSON_AddItemToArray(input_array, msg_item);
                LOG_DEBUG("Added assistant message with %d content blocks", msg->content_count);
            } else {
                cJSON_Delete(content_array);
            }
        }
        else if (msg->role == MSG_AUTO_COMPACTION) {
            // Auto-compaction notice - send as system message so model sees it but doesn't respond
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];
                if (c->type == INTERNAL_TEXT && c->text) {
                    cJSON *notice_item = cJSON_CreateObject();
                    cJSON_AddStringToObject(notice_item, "type", "message");
                    cJSON_AddStringToObject(notice_item, "role", "system");
                    cJSON *content_array = cJSON_CreateArray();
                    cJSON *content_obj = cJSON_CreateObject();
                    cJSON_AddStringToObject(content_obj, "type", "input_text");
                    cJSON_AddStringToObject(content_obj, "text", c->text);
                    cJSON_AddItemToArray(content_array, content_obj);
                    cJSON_AddItemToObject(notice_item, "content", content_array);
                    cJSON_AddItemToArray(input_array, notice_item);
                    break;
                }
            }
        }
        else {
            // Unknown message role - log warning but skip the message
            LOG_WARN("Unhandled message role %d at index %d in Responses API, skipping", msg->role, i);
        }
    }

    cJSON_AddItemToObject(request, "input", input_array);

    // Add tools with cache_control support (including MCP tools if available)
    // Use the Responses API-specific tool definitions function
    cJSON *tool_defs = get_openai_subscription_tool_definitions(enable_caching, TOOL_SCHEMA_RESPONSES);
    cJSON_AddItemToObject(request, "tools", tool_defs);

    conversation_state_unlock(state);

    LOG_DEBUG("OpenAI Responses API request built successfully");
    return request;
}

/**
 * Build HTTP request for OpenAI Responses API
 *
 * Constructs a complete HttpRequest struct including:
 * - URL (from config->base_url)
 * - Headers (Content-Type, Authorization, extra headers)
 * - Body (JSON request from build_openai_responses_request)
 *
 * @param state - Conversation state with messages
 * @param config - OpenAI provider configuration
 * @param enable_caching - Whether to enable prompt caching
 * @param out - Output: HttpRequest struct (caller must free request->headers and request->body on success), or empty struct on error
 */
void build_responses_http_request(ConversationState *state, OpenAIConfig *config, int enable_caching, HttpRequest *out) {
    HttpRequest req = {0};

    if (!config || !config->api_key || !config->base_url) {
        LOG_ERROR("OpenAI config or credentials not initialized");
        *out = req;
        return;
    }

    if (!state) {
        LOG_ERROR("ConversationState is NULL");
        *out = req;
        return;
    }

    LOG_DEBUG("Building HTTP request for Responses API (caching: %s)",
              enable_caching ? "enabled" : "disabled");

    // Build the JSON request body
    cJSON *request_json = build_openai_responses_request(state, enable_caching);
    if (!request_json) {
        LOG_ERROR("Failed to build Responses API request JSON");
        *out = req; return;
    }

    // Serialize to JSON string
    char *body = cJSON_PrintUnformatted(request_json);
    cJSON_Delete(request_json);

    if (!body) {
        LOG_ERROR("Failed to serialize request JSON");
        *out = req; return;
    }

    LOG_DEBUG("OpenAI Responses API: Request serialized, length: %zu bytes", strlen(body));

    // Build authentication header
    char auth_header[512];
    if (config->auth_header_template) {
        // Use custom auth header template (should contain %s for API key)
        const char *percent_s = strstr(config->auth_header_template, "%s");
        if (percent_s) {
            size_t prefix_len = (size_t)(percent_s - config->auth_header_template);
            size_t api_key_len = strlen(config->api_key);
            size_t suffix_len = strlen(percent_s + 2);

            if (prefix_len + api_key_len + suffix_len + 1 < sizeof(auth_header)) {
                strlcpy(auth_header, config->auth_header_template, prefix_len + 1);
                strlcat(auth_header, config->api_key, sizeof(auth_header));
                strlcat(auth_header, percent_s + 2, sizeof(auth_header));
            } else {
                strlcpy(auth_header, config->auth_header_template, sizeof(auth_header));
                LOG_WARN("Auth header template too long, truncated");
            }
        } else {
            strlcpy(auth_header, config->auth_header_template, sizeof(auth_header));
        }
    } else {
        // Default Bearer token format
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->api_key);
    }

    // Set up headers
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    // Add extra headers from config
    if (config->extra_headers) {
        for (int i = 0; i < config->extra_headers_count; i++) {
            if (config->extra_headers[i]) {
                headers = curl_slist_append(headers, config->extra_headers[i]);
            }
        }
    }

    if (!headers) {
        free(body);
        LOG_ERROR("Failed to setup HTTP headers");
        *out = req; return;
    }

    // Populate HttpRequest struct
    req.url = config->base_url;
    req.method = "POST";
    req.body = body;
    req.headers = headers;
    req.connect_timeout_ms = 30000;   // 30 seconds
    req.total_timeout_ms = 300000;    // 5 minutes
    req.enable_streaming = 0;          // Responses API doesn't use SSE streaming

    LOG_DEBUG("HTTP request built for URL: %s", req.url);
    *out = req; return;
}

/**
 * Submit HTTP request to OpenAI Responses API
 *
 * Executes the HTTP request using the HTTP client with progress callback
 * for interrupt handling.
 *
 * @param request - Pre-built HTTP request
 * @param state - Conversation state (for interrupt checking)
 * @return HttpResponse (caller must free with http_response_free()), or NULL on error
 */
HttpResponse* submit_responses_http_request(HttpRequest *request, ConversationState *state) {
    if (!request || !request->url || !request->body) {
        LOG_ERROR("Invalid HTTP request");
        return NULL;
    }

    LOG_DEBUG("Submitting HTTP request to: %s", request->url);

    // Execute the HTTP request
    HttpResponse *response = http_client_execute(request, NULL, state);

    if (!response) {
        LOG_ERROR("Failed to execute HTTP request");
        return NULL;
    }

    if (response->error_message) {
        LOG_ERROR("HTTP request error: %s", response->error_message);
        return response;
    }

    LOG_DEBUG("HTTP response received: status=%ld, body_size=%zu",
              response->status_code,
              response->body ? strlen(response->body) : 0);

    return response;
}

/**
 * Parse OpenAI Responses API response into ApiResponse
 *
 * Converts raw HTTP response body to ApiResponse:
 * - Extracts text content from output array
 * - Parses tool calls if present
 *
 * @param raw_response - Raw HTTP response body (JSON string)
 * @return ApiResponse (caller must free with api_response_free()), or NULL on error
 */
ApiResponse* parse_responses_http_response(const char *raw_response) {
    if (!raw_response) {
        LOG_ERROR("Raw response is NULL");
        return NULL;
    }

    LOG_DEBUG("Parsing Responses API response");

    // Parse JSON response
    cJSON *json = cJSON_Parse(raw_response);
    if (!json) {
        LOG_ERROR("Failed to parse JSON response");
        return NULL;
    }

    // Create arena for ApiResponse allocations (16KB default size)
    Arena *arena = arena_create(16 * 1024);
    if (!arena) {
        LOG_ERROR("Failed to create arena for ApiResponse");
        cJSON_Delete(json);
        return NULL;
    }

    // Allocate ApiResponse from arena
    ApiResponse *api_response = arena_alloc(arena, sizeof(ApiResponse));
    if (!api_response) {
        LOG_ERROR("Failed to allocate ApiResponse from arena");
        arena_destroy(arena);
        cJSON_Delete(json);
        return NULL;
    }

    // Initialize ApiResponse
    memset(api_response, 0, sizeof(ApiResponse));
    api_response->arena = arena;
    api_response->error_message = NULL;
    api_response->raw_response = json;

    // Parse the output array to extract text and tool calls
    cJSON *output = cJSON_GetObjectItem(json, "output");
    if (output && cJSON_IsArray(output)) {
        // First pass: count tool calls (text is accumulated)
        int tool_call_count = 0;

        cJSON *item = NULL;
        cJSON_ArrayForEach(item, output) {
            cJSON *type = cJSON_GetObjectItem(item, "type");
            if (!type || !cJSON_IsString(type)) {
                continue;
            }

            if (strcmp(type->valuestring, "message") == 0) {
                cJSON *content = cJSON_GetObjectItem(item, "content");
                if (content && cJSON_IsArray(content)) {
                    cJSON *content_item = NULL;
                    cJSON_ArrayForEach(content_item, content) {
                        cJSON *content_type = cJSON_GetObjectItem(content_item, "type");
                        if (!content_type || !cJSON_IsString(content_type)) {
                            continue;
                        }

                        if (strcmp(content_type->valuestring, "function_call") == 0) {
                            tool_call_count++;
                        }
                    }
                }
            }
        }

        // Allocate for tool calls
        if (tool_call_count > 0) {
            api_response->tools = arena_alloc(api_response->arena,
                                             (size_t)tool_call_count * sizeof(ToolCall));
            if (!api_response->tools) {
                LOG_ERROR("Failed to allocate tool calls from arena");
                api_response_free(api_response);
                return NULL;
            }
            // Initialize tool call structures
            memset(api_response->tools, 0, (size_t)tool_call_count * sizeof(ToolCall));
            api_response->tool_count = tool_call_count;
        }

        // Second pass: extract content
        int tool_idx = 0;
        char *combined_text = NULL;
        size_t text_capacity = 0;
        size_t text_length = 0;

        cJSON_ArrayForEach(item, output) {
            cJSON *type = cJSON_GetObjectItem(item, "type");
            if (!type || !cJSON_IsString(type)) {
                continue;
            }

            if (strcmp(type->valuestring, "message") == 0) {
                cJSON *content = cJSON_GetObjectItem(item, "content");
                if (!content || !cJSON_IsArray(content)) {
                    continue;
                }

                cJSON *content_item = NULL;
                cJSON_ArrayForEach(content_item, content) {
                    cJSON *content_type = cJSON_GetObjectItem(content_item, "type");
                    if (!content_type || !cJSON_IsString(content_type)) {
                        continue;
                    }

                    if (strcmp(content_type->valuestring, "output_text") == 0) {
                        cJSON *text = cJSON_GetObjectItem(content_item, "text");
                        if (text && cJSON_IsString(text) && text->valuestring) {
                            size_t text_len = strlen(text->valuestring);
                            size_t needed = text_length + text_len + 1;

                            if (needed > text_capacity) {
                                size_t new_cap = text_capacity ? text_capacity * 2 : 1024;
                                if (new_cap < needed) new_cap = needed;

                                // Allocate new buffer from arena
                                char *new_buf = arena_alloc(api_response->arena, new_cap);
                                if (!new_buf) {
                                    // Note: combined_text is allocated from arena, no need to free
                                    api_response_free(api_response);
                                    return NULL;
                                }

                                // Copy existing content if any
                                if (combined_text && text_length > 0) {
                                    memcpy(new_buf, combined_text, text_length);
                                }
                                combined_text = new_buf;
                                text_capacity = new_cap;
                            }

                            if (text_length == 0) {
                                memcpy(combined_text, text->valuestring, text_len + 1);
                            } else {
                                memcpy(combined_text + text_length, text->valuestring, text_len + 1);
                            }
                            text_length += text_len;
                        }
                    } else if (strcmp(content_type->valuestring, "function_call") == 0) {
                        cJSON *id = cJSON_GetObjectItem(content_item, "id");
                        cJSON *name = cJSON_GetObjectItem(content_item, "name");
                        cJSON *arguments = cJSON_GetObjectItem(content_item, "arguments");

                        if (!id || !name || !arguments) {
                            LOG_WARN("Malformed function_call, skipping");
                            continue;
                        }

                        api_response->tools[tool_idx].id = arena_strdup(api_response->arena, id->valuestring);
                        api_response->tools[tool_idx].name = arena_strdup(api_response->arena, name->valuestring);

                        // Parse arguments JSON string to cJSON object
                        const char *args_str = arguments->valuestring;
                        api_response->tools[tool_idx].parameters = cJSON_Parse(args_str ? args_str : "{}");
                        if (!api_response->tools[tool_idx].parameters) {
                            LOG_WARN("Failed to parse tool arguments, using empty object");
                            api_response->tools[tool_idx].parameters = cJSON_CreateObject();
                        }

                        tool_idx++;
                    }
                }
            }
        }

        if (combined_text && text_length > 0) {
            api_response->message.text = combined_text;
            // Trim whitespace from the extracted content
            trim_whitespace(api_response->message.text);
        }
        // Note: if combined_text is not used, it's allocated from arena
        // and will be freed when arena is destroyed
    }

    LOG_DEBUG("Parsed Responses API response: text=%s, tools=%d",
              api_response->message.text ? "yes" : "no",
              api_response->tool_count);

    return api_response;
}

/**
 * Parse OpenAI Responses API response into internal message format
 *
 * Converts OpenAI Responses API response to InternalMessage:
 * - output array with items -> INTERNAL_TEXT or INTERNAL_TOOL_CALL blocks
 *
 * @param response - OpenAI Responses API response JSON
 * @param out - Output: InternalMessage (caller must free contents), or empty message on error
 */
void parse_openai_responses_response(cJSON *response, InternalMessage *out) {
    InternalMessage msg = {0};
    msg.role = MSG_ASSISTANT;

    if (!response) {
        LOG_ERROR("Response is NULL");
        *out = msg;
        return;
    }

    cJSON *output = cJSON_GetObjectItem(response, "output");
    if (!output || !cJSON_IsArray(output)) {
        LOG_ERROR("Invalid Responses API response: missing 'output' array");
        *out = msg;
        return;
    }

    // Count content blocks in output array
    int count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, output) {
        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (!type || !cJSON_IsString(type)) {
            continue;
        }

        if (strcmp(type->valuestring, "message") == 0) {
            // Message contains content array with text and/or tool calls
            cJSON *content = cJSON_GetObjectItem(item, "content");
            if (content && cJSON_IsArray(content)) {
                count += cJSON_GetArraySize(content);
            }
        }
    }

    if (count == 0) {
        LOG_WARN("Response has no content or tool_calls");
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
    cJSON_ArrayForEach(item, output) {
        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (!type || !cJSON_IsString(type)) {
            continue;
        }

        if (strcmp(type->valuestring, "message") == 0) {
            cJSON *content = cJSON_GetObjectItem(item, "content");
            if (!content || !cJSON_IsArray(content)) {
                continue;
            }

            cJSON *content_item = NULL;
            cJSON_ArrayForEach(content_item, content) {
                if (idx >= count) break;

                cJSON *content_type = cJSON_GetObjectItem(content_item, "type");
                if (!content_type || !cJSON_IsString(content_type)) {
                    continue;
                }

                if (strcmp(content_type->valuestring, "output_text") == 0) {
                    cJSON *text = cJSON_GetObjectItem(content_item, "text");
                    if (text && cJSON_IsString(text) && text->valuestring) {
                        msg.contents[idx].type = INTERNAL_TEXT;
                        msg.contents[idx].text = strdup_trim(text->valuestring);
                        if (!msg.contents[idx].text) {
                            LOG_ERROR("Failed to duplicate text content");
                        }
                        idx++;
                    }
                } else if (strcmp(content_type->valuestring, "function_call") == 0) {
                    cJSON *id = cJSON_GetObjectItem(content_item, "id");
                    cJSON *name = cJSON_GetObjectItem(content_item, "name");
                    cJSON *arguments = cJSON_GetObjectItem(content_item, "arguments");

                    if (!id || !name || !arguments) {
                        LOG_WARN("Malformed function_call, skipping");
                        continue;
                    }

                    msg.contents[idx].type = INTERNAL_TOOL_CALL;
                    msg.contents[idx].tool_id = strdup(id->valuestring);
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
        }
    }

    LOG_DEBUG("Parsed OpenAI Responses API response: %d content blocks", msg.content_count);
    *out = msg;
}

/**
 * Build OpenAI Responses API tool definitions
 *
 * Returns tool definitions in the Responses API format.
 * The Responses API uses the same tool format as Chat Completions for
 * function tools: a flat array with type: "function" for each tool.
 *
 * @param state - Conversation state
 * @param enable_caching - Whether to add cache_control markers
 * @return JSON array of tool definitions (caller must free), or NULL on error
 */
cJSON* get_tool_definitions_for_responses_api(ConversationState *state, int enable_caching) {
    cJSON *tool_array = cJSON_CreateArray();
    if (!tool_array) {
        return NULL;
    }

    int plan_mode = state ? state->plan_mode : 0;

    // Check if we're running as a subagent - if so, exclude the Subagent tool to prevent recursion
    const char *is_subagent_env = getenv("KLAWED_IS_SUBAGENT");
    int is_subagent = is_subagent_env && (strcmp(is_subagent_env, "1") == 0 ||
                                         strcasecmp(is_subagent_env, "true") == 0 ||
                                         strcasecmp(is_subagent_env, "yes") == 0);

    LOG_DEBUG("[TOOLS] get_tool_definitions_for_responses_api: plan_mode=%d, is_subagent=%d",
              plan_mode, is_subagent);

    // Helper macro to create a tool definition
    // Responses API expects tools with name/description/parameters at top level
#define CREATE_TOOL(tool_obj, tool_name, tool_desc, params_obj) \
    do { \
        tool_obj = cJSON_CreateObject(); \
        cJSON_AddStringToObject(tool_obj, "type", "function"); \
        cJSON_AddStringToObject(tool_obj, "name", tool_name); \
        cJSON_AddStringToObject(tool_obj, "description", tool_desc); \
        cJSON_AddItemToObject(tool_obj, "parameters", params_obj); \
        cJSON_AddItemToArray(tool_array, tool_obj); \
    } while(0)

    // Sleep tool
    cJSON *sleep_params = cJSON_CreateObject();
    cJSON_AddStringToObject(sleep_params, "type", "object");
    cJSON *sleep_props = cJSON_CreateObject();
    cJSON *duration_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(duration_prop, "type", "integer");
    cJSON_AddStringToObject(duration_prop, "description", "Duration to sleep in seconds");
    cJSON_AddItemToObject(sleep_props, "duration", duration_prop);
    cJSON_AddItemToObject(sleep_params, "properties", sleep_props);
    cJSON *sleep_req = cJSON_CreateArray();
    cJSON_AddItemToArray(sleep_req, cJSON_CreateString("duration"));
    cJSON_AddItemToObject(sleep_params, "required", sleep_req);

    if (!is_tool_disabled("Sleep")) {
        cJSON *sleep_tool;
        CREATE_TOOL(sleep_tool, "Sleep",
                    "Pauses execution for specified duration (seconds)",
                    sleep_params);
    } else {
        cJSON_Delete(sleep_params);
        LOG_INFO("Tool 'Sleep' is disabled via KLAWED_DISABLE_TOOLS");
    }

    // Read tool
    cJSON *read_params = cJSON_CreateObject();
    cJSON_AddStringToObject(read_params, "type", "object");
    cJSON *read_props = cJSON_CreateObject();
    cJSON *read_path = cJSON_CreateObject();
    cJSON_AddStringToObject(read_path, "type", "string");
    cJSON_AddStringToObject(read_path, "description", "The absolute path to the file");
    cJSON_AddItemToObject(read_props, "file_path", read_path);
    cJSON *read_start = cJSON_CreateObject();
    cJSON_AddStringToObject(read_start, "type", "integer");
    cJSON_AddStringToObject(read_start, "description", "Optional: Starting line number (1-indexed, inclusive)");
    cJSON_AddItemToObject(read_props, "start_line", read_start);
    cJSON *read_end = cJSON_CreateObject();
    cJSON_AddStringToObject(read_end, "type", "integer");
    cJSON_AddStringToObject(read_end, "description", "Optional: Ending line number (1-indexed, inclusive)");
    cJSON_AddItemToObject(read_props, "end_line", read_end);
    cJSON_AddItemToObject(read_params, "properties", read_props);
    cJSON *read_req = cJSON_CreateArray();
    cJSON_AddItemToArray(read_req, cJSON_CreateString("file_path"));
    cJSON_AddItemToObject(read_params, "required", read_req);

    if (!is_tool_disabled("Read")) {
        cJSON *read_tool;
        CREATE_TOOL(read_tool, "Read",
                    "Reads a file from the filesystem with optional line range support",
                    read_params);
    } else {
        cJSON_Delete(read_params);
        LOG_INFO("Tool 'Read' is disabled via KLAWED_DISABLE_TOOLS");
    }

    // Bash, Subagent, Write, and Edit tools - excluded in plan mode
    if (!plan_mode) {
        // Bash tool
        cJSON *bash_params = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_params, "type", "object");
        cJSON *bash_props = cJSON_CreateObject();
        cJSON *bash_cmd = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_cmd, "type", "string");
        cJSON_AddStringToObject(bash_cmd, "description", "The command to execute");
        cJSON_AddItemToObject(bash_props, "command", bash_cmd);
        cJSON *bash_timeout = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_timeout, "type", "integer");
        cJSON_AddStringToObject(bash_timeout, "description",
            "Optional: Timeout in seconds. Default: 30 (from KLAWED_BASH_TIMEOUT env var). "
            "Set to 0 for no timeout. Commands that timeout will return exit code -2.");
        cJSON_AddItemToObject(bash_props, "timeout", bash_timeout);
        cJSON_AddItemToObject(bash_params, "properties", bash_props);
        cJSON *bash_req = cJSON_CreateArray();
        cJSON_AddItemToArray(bash_req, cJSON_CreateString("command"));
        cJSON_AddItemToObject(bash_params, "required", bash_req);

        if (!is_tool_disabled("Bash")) {
            cJSON *bash_tool;
            CREATE_TOOL(bash_tool, "Bash",
                        "Executes bash commands. Note: stderr is automatically redirected to stdout "
                        "to prevent terminal corruption, so both stdout and stderr output will be "
                        "captured in the 'output' field. Commands have a configurable timeout "
                        "(default: 30 seconds) to prevent hanging. Use the 'timeout' parameter to "
                        "override the default or set to 0 for no timeout. If the output exceeds "
                        "12,228 bytes, it will be truncated and a 'truncation_warning' field "
                        "will be added to the result.",
                        bash_params);
        } else {
            cJSON_Delete(bash_params);
            LOG_INFO("Tool 'Bash' is disabled via KLAWED_DISABLE_TOOLS");
        }

        // Subagent tool - exclude if running as subagent to prevent recursion
        if (!is_subagent && !is_tool_disabled("Subagent")) {
            cJSON *subagent_params = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_params, "type", "object");
            cJSON *subagent_props = cJSON_CreateObject();
            cJSON *subagent_prompt = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_prompt, "type", "string");
            cJSON_AddStringToObject(subagent_prompt, "description",
                "The task prompt for the subagent. Be specific and include all necessary context.");
            cJSON_AddItemToObject(subagent_props, "prompt", subagent_prompt);
            cJSON *subagent_timeout = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_timeout, "type", "integer");
            cJSON_AddStringToObject(subagent_timeout, "description",
                "Optional: Timeout in seconds. Default: 300 (5 minutes). Set to 0 for no timeout.");
            cJSON_AddItemToObject(subagent_props, "timeout", subagent_timeout);
            cJSON *subagent_tail = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_tail, "type", "integer");
            cJSON_AddStringToObject(subagent_tail, "description",
                "Optional: Number of lines to return from end of log. Default: 100. The summary is usually at the end.");
            cJSON_AddItemToObject(subagent_props, "tail_lines", subagent_tail);
            cJSON *subagent_provider = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_provider, "type", "string");
            cJSON_AddStringToObject(subagent_provider, "description",
                "Optional: LLM provider name to use for this subagent (e.g., 'gpt-4-turbo', 'sonnet-4.5-bedrock'). "
                "If not specified, subagent inherits the parent's provider configuration.");
            cJSON_AddItemToObject(subagent_props, "provider", subagent_provider);
            cJSON *subagent_working_dir = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_working_dir, "type", "string");
            cJSON_AddStringToObject(subagent_working_dir, "description",
                "Optional: Working directory for the subagent. If not specified, subagent inherits "
                "the parent's working directory. Must be an absolute path.");
            cJSON_AddItemToObject(subagent_props, "working_dir", subagent_working_dir);
            cJSON_AddItemToObject(subagent_params, "properties", subagent_props);
            cJSON *subagent_req = cJSON_CreateArray();
            cJSON_AddItemToArray(subagent_req, cJSON_CreateString("prompt"));
            cJSON_AddItemToObject(subagent_params, "required", subagent_req);

            cJSON *subagent_tool;
            CREATE_TOOL(subagent_tool, "Subagent",
                        "Spawns a new instance of klawed with the same configuration to work on a "
                        "delegated task in a fresh context. The subagent runs independently and writes "
                        "all output (stdout and stderr) to a log file. Returns the tail of the log "
                        "(last 100 lines by default) which typically contains the task summary. "
                        "For large outputs, use Read tool to access the full log file, or Grep to "
                        "search for specific content. Use this when: (1) you need a fresh context "
                        "without conversation history, (2) delegating a complex independent task, "
                        "(3) avoiding context limit issues. Note: The subagent has full tool access "
                        "including Write, Edit, and Bash.",
                        subagent_params);
        } else if (!is_subagent) {
            LOG_INFO("Tool 'Subagent' is disabled via KLAWED_DISABLE_TOOLS");
        }
        if (!is_subagent && !is_tool_disabled("CheckSubagentProgress")) {

            // CheckSubagentProgress tool
            cJSON *check_progress_params = cJSON_CreateObject();
            cJSON_AddStringToObject(check_progress_params, "type", "object");
            cJSON *check_progress_props = cJSON_CreateObject();
            cJSON *check_pid = cJSON_CreateObject();
            cJSON_AddStringToObject(check_pid, "type", "integer");
            cJSON_AddStringToObject(check_pid, "description",
                "Process ID of the subagent (from Subagent tool response)");
            cJSON_AddItemToObject(check_progress_props, "pid", check_pid);
            cJSON *check_log = cJSON_CreateObject();
            cJSON_AddStringToObject(check_log, "type", "string");
            cJSON_AddStringToObject(check_log, "description",
                "Path to subagent log file (from Subagent tool response)");
            cJSON_AddItemToObject(check_progress_props, "log_file", check_log);
            cJSON *check_tail = cJSON_CreateObject();
            cJSON_AddStringToObject(check_tail, "type", "integer");
            cJSON_AddStringToObject(check_tail, "description",
                "Optional: Number of lines to read from end of log (default: 50)");
            cJSON_AddItemToObject(check_progress_props, "tail_lines", check_tail);
            cJSON_AddItemToObject(check_progress_params, "properties", check_progress_props);

            cJSON *check_progress_tool;
            CREATE_TOOL(check_progress_tool, "CheckSubagentProgress",
                        "Checks the progress of a running subagent by reading its log file. "
                        "Returns whether the subagent is still running and the tail of its output. "
                        "Use this to monitor long-running subagent tasks.",
                        check_progress_params);
        } else if (!is_subagent) {
            LOG_INFO("Tool 'CheckSubagentProgress' is disabled via KLAWED_DISABLE_TOOLS");
        }
        if (!is_subagent && !is_tool_disabled("InterruptSubagent")) {

            // InterruptSubagent tool
            cJSON *interrupt_params = cJSON_CreateObject();
            cJSON_AddStringToObject(interrupt_params, "type", "object");
            cJSON *interrupt_props = cJSON_CreateObject();
            cJSON *interrupt_pid = cJSON_CreateObject();
            cJSON_AddStringToObject(interrupt_pid, "type", "integer");
            cJSON_AddStringToObject(interrupt_pid, "description",
                "Process ID of the subagent to interrupt (from Subagent tool response)");
            cJSON_AddItemToObject(interrupt_props, "pid", interrupt_pid);
            cJSON_AddItemToObject(interrupt_params, "properties", interrupt_props);
            cJSON *interrupt_req = cJSON_CreateArray();
            cJSON_AddItemToArray(interrupt_req, cJSON_CreateString("pid"));
            cJSON_AddItemToObject(interrupt_params, "required", interrupt_req);

            cJSON *interrupt_tool;
            CREATE_TOOL(interrupt_tool, "InterruptSubagent",
                        "Interrupts and stops a running subagent. Use this to cancel a subagent "
                        "that is stuck, taking too long, or no longer needed. "
                        "You can interrupt a subagent at any time.",
                        interrupt_params);
        } else if (!is_subagent) {
            LOG_INFO("Tool 'InterruptSubagent' is disabled via KLAWED_DISABLE_TOOLS");
        }

        // Write tool
        cJSON *write_params = cJSON_CreateObject();
        cJSON_AddStringToObject(write_params, "type", "object");
        cJSON *write_props = cJSON_CreateObject();
        cJSON *write_path = cJSON_CreateObject();
        cJSON_AddStringToObject(write_path, "type", "string");
        cJSON_AddStringToObject(write_path, "description", "Path to the file to write");
        cJSON_AddItemToObject(write_props, "file_path", write_path);
        cJSON *write_content = cJSON_CreateObject();
        cJSON_AddStringToObject(write_content, "type", "string");
        cJSON_AddStringToObject(write_content, "description", "Content to write to the file");
        cJSON_AddItemToObject(write_props, "content", write_content);
        cJSON_AddItemToObject(write_params, "properties", write_props);
        cJSON *write_req = cJSON_CreateArray();
        cJSON_AddItemToArray(write_req, cJSON_CreateString("file_path"));
        cJSON_AddItemToArray(write_req, cJSON_CreateString("content"));
        cJSON_AddItemToObject(write_params, "required", write_req);

        if (!is_tool_disabled("Write")) {
            cJSON *write_tool;
            CREATE_TOOL(write_tool, "Write",
                        "Writes content to a file. To avoid hitting token limits, make smaller changes and call "
                        "the Write tool multiple times with focused content instead of writing entire "
                        "files at once. Break large operations into logical chunks (e.g., write a single "
                        "function at a time, one section at a time).",
                        write_params);
        } else {
            cJSON_Delete(write_params);
            LOG_INFO("Tool 'Write' is disabled via KLAWED_DISABLE_TOOLS");
        }

        // Edit tool
        cJSON *edit_params = cJSON_CreateObject();
        cJSON_AddStringToObject(edit_params, "type", "object");
        cJSON *edit_props = cJSON_CreateObject();
        cJSON *edit_path = cJSON_CreateObject();
        cJSON_AddStringToObject(edit_path, "type", "string");
        cJSON_AddStringToObject(edit_path, "description", "Path to the file to edit");
        cJSON_AddItemToObject(edit_props, "file_path", edit_path);
        cJSON *edit_old = cJSON_CreateObject();
        cJSON_AddStringToObject(edit_old, "type", "string");
        cJSON_AddStringToObject(edit_old, "description", "Exact text to find and replace. Only simple string matching is supported.");
        cJSON_AddItemToObject(edit_props, "old_string", edit_old);
        cJSON *edit_new = cJSON_CreateObject();
        cJSON_AddStringToObject(edit_new, "type", "string");
        cJSON_AddStringToObject(edit_new, "description", "Replacement text. Will replace the first occurrence of old_string.");
        cJSON_AddItemToObject(edit_props, "new_string", edit_new);
        cJSON_AddItemToObject(edit_params, "properties", edit_props);
        cJSON *edit_req = cJSON_CreateArray();
        cJSON_AddItemToArray(edit_req, cJSON_CreateString("file_path"));
        cJSON_AddItemToArray(edit_req, cJSON_CreateString("old_string"));
        cJSON_AddItemToArray(edit_req, cJSON_CreateString("new_string"));
        cJSON_AddItemToObject(edit_params, "required", edit_req);

        if (!is_tool_disabled("Edit")) {
            cJSON *edit_tool;
            CREATE_TOOL(edit_tool, "Edit",
                        "Performs simple string replacement in files. Replaces the first occurrence of "
                        "old_string with new_string. To avoid hitting token limits, make smaller changes and call "
                        "the Edit tool multiple times with focused edits instead of making massive "
                        "changes in a single call. Break large operations into logical chunks (e.g., "
                        "edit one function at a time, one section at a time).",
                        edit_params);
        } else {
            cJSON_Delete(edit_params);
            LOG_INFO("Tool 'Edit' is disabled via KLAWED_DISABLE_TOOLS");
        }

        // MultiEdit tool
        cJSON *multiedit_params = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_params, "type", "object");
        cJSON *multiedit_props = cJSON_CreateObject();
        cJSON *multiedit_path = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_path, "type", "string");
        cJSON_AddStringToObject(multiedit_path, "description", "Path to the file to edit");
        cJSON_AddItemToObject(multiedit_props, "file_path", multiedit_path);
        cJSON *multiedit_edits = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_edits, "type", "array");
        cJSON_AddStringToObject(multiedit_edits, "description", "Array of edit objects, each with old_string and new_string fields");
        cJSON *multiedit_edit_items = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_edit_items, "type", "object");
        cJSON *multiedit_edit_props = cJSON_CreateObject();
        cJSON *multiedit_old = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_old, "type", "string");
        cJSON_AddStringToObject(multiedit_old, "description", "Exact text to find and replace");
        cJSON_AddItemToObject(multiedit_edit_props, "old_string", multiedit_old);
        cJSON *multiedit_new = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_new, "type", "string");
        cJSON_AddStringToObject(multiedit_new, "description", "Replacement text");
        cJSON_AddItemToObject(multiedit_edit_props, "new_string", multiedit_new);
        cJSON_AddItemToObject(multiedit_edit_items, "properties", multiedit_edit_props);
        cJSON *multiedit_edit_req = cJSON_CreateArray();
        cJSON_AddItemToArray(multiedit_edit_req, cJSON_CreateString("old_string"));
        cJSON_AddItemToArray(multiedit_edit_req, cJSON_CreateString("new_string"));
        cJSON_AddItemToObject(multiedit_edit_items, "required", multiedit_edit_req);
        cJSON_AddItemToObject(multiedit_edits, "items", multiedit_edit_items);
        cJSON_AddItemToObject(multiedit_props, "edits", multiedit_edits);
        cJSON_AddItemToObject(multiedit_params, "properties", multiedit_props);
        cJSON *multiedit_req = cJSON_CreateArray();
        cJSON_AddItemToArray(multiedit_req, cJSON_CreateString("file_path"));
        cJSON_AddItemToArray(multiedit_req, cJSON_CreateString("edits"));
        cJSON_AddItemToObject(multiedit_params, "required", multiedit_req);

        if (!is_tool_disabled("MultiEdit")) {
            cJSON *multiedit_tool;
            CREATE_TOOL(multiedit_tool, "MultiEdit",
                        "Performs multiple string replacements in a file. Applies edits sequentially in "
                        "the order provided. Each edit replaces the first occurrence of old_string with "
                        "new_string. Returns counts of successful and failed edits.",
                        multiedit_params);
        } else {
            cJSON_Delete(multiedit_params);
            LOG_INFO("Tool 'MultiEdit' is disabled via KLAWED_DISABLE_TOOLS");
        }
    }

    // Glob tool
    cJSON *glob_params = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_params, "type", "object");
    cJSON *glob_props = cJSON_CreateObject();
    cJSON *glob_pattern = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_pattern, "type", "string");
    cJSON_AddStringToObject(glob_pattern, "description", "Glob pattern to match files against");
    cJSON_AddItemToObject(glob_props, "pattern", glob_pattern);
    cJSON_AddItemToObject(glob_params, "properties", glob_props);
    cJSON *glob_req = cJSON_CreateArray();
    cJSON_AddItemToArray(glob_req, cJSON_CreateString("pattern"));
    cJSON_AddItemToObject(glob_params, "required", glob_req);

    if (!is_tool_disabled("Glob")) {
        cJSON *glob_tool;
        CREATE_TOOL(glob_tool, "Glob",
                    "Finds files matching a pattern",
                    glob_params);
    } else {
        cJSON_Delete(glob_params);
        LOG_INFO("Tool 'Glob' is disabled via KLAWED_DISABLE_TOOLS");
    }

    // Grep tool
    cJSON *grep_params = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_params, "type", "object");
    cJSON *grep_props = cJSON_CreateObject();
    cJSON *grep_pattern = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_pattern, "type", "string");
    cJSON_AddStringToObject(grep_pattern, "description", "Pattern to search for");
    cJSON_AddItemToObject(grep_props, "pattern", grep_pattern);
    cJSON *grep_path = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_path, "type", "string");
    cJSON_AddStringToObject(grep_path, "description", "Path to search in (default: .)");
    cJSON_AddItemToObject(grep_props, "path", grep_path);
    cJSON_AddItemToObject(grep_params, "properties", grep_props);
    cJSON *grep_req = cJSON_CreateArray();
    cJSON_AddItemToArray(grep_req, cJSON_CreateString("pattern"));
    cJSON_AddItemToObject(grep_params, "required", grep_req);

    if (!is_tool_disabled("Grep")) {
        cJSON *grep_tool;
        CREATE_TOOL(grep_tool, "Grep",
                    "Searches for patterns in files. Results limited to 100 matches by default "
                    "(configurable via KLAWED_GREP_MAX_RESULTS). Automatically excludes common "
                    "build directories, dependencies, and binary files (.git, node_modules, build/, "
                    "*.min.js, etc). Returns 'match_count' and 'warning' if truncated.",
                    grep_params);
    } else {
        cJSON_Delete(grep_params);
        LOG_INFO("Tool 'Grep' is disabled via KLAWED_DISABLE_TOOLS");
    }

    // UploadImage tool (conditionally added based on KLAWED_DISABLE_TOOLS)
    if (!is_tool_disabled("UploadImage")) {
        cJSON *upload_image_params = cJSON_CreateObject();
        cJSON_AddStringToObject(upload_image_params, "type", "object");
        cJSON *upload_image_props = cJSON_CreateObject();
        cJSON *image_path_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(image_path_prop, "type", "string");
        cJSON_AddStringToObject(image_path_prop, "description", "Path to the image file to upload");
        cJSON_AddItemToObject(upload_image_props, "file_path", image_path_prop);
        cJSON_AddItemToObject(upload_image_params, "properties", upload_image_props);
        cJSON *upload_image_req = cJSON_CreateArray();
        cJSON_AddItemToArray(upload_image_req, cJSON_CreateString("file_path"));
        cJSON_AddItemToObject(upload_image_params, "required", upload_image_req);

        cJSON *upload_image_tool;
        CREATE_TOOL(upload_image_tool, "UploadImage",
                    "Uploads an image file to be included in the conversation context using OpenAI-compatible format. Supports common image formats (PNG, JPEG, GIF, WebP).",
                    upload_image_params);
    } else {
        LOG_INFO("Tool 'UploadImage' is disabled via KLAWED_DISABLE_TOOLS");
    }

    // TodoWrite tool
    cJSON *todo_params = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_params, "type", "object");
    cJSON *todo_props = cJSON_CreateObject();

    cJSON *todos_array = cJSON_CreateObject();
    cJSON_AddStringToObject(todos_array, "type", "array");
    cJSON_AddStringToObject(todos_array, "description",
        "Array of todo items to display. Replaces the entire todo list.");

    cJSON *todos_items = cJSON_CreateObject();
    cJSON_AddStringToObject(todos_items, "type", "object");
    cJSON *item_props = cJSON_CreateObject();

    cJSON *content_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(content_prop, "type", "string");
    cJSON_AddStringToObject(content_prop, "description",
        "Task description in imperative form (e.g., 'Run tests')");
    cJSON_AddItemToObject(item_props, "content", content_prop);

    cJSON *active_form_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(active_form_prop, "type", "string");
    cJSON_AddStringToObject(active_form_prop, "description",
        "Task description in present continuous form (e.g., 'Running tests')");
    cJSON_AddItemToObject(item_props, "activeForm", active_form_prop);

    cJSON *status_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(status_prop, "type", "string");
    cJSON *status_enum = cJSON_CreateArray();
    cJSON_AddItemToArray(status_enum, cJSON_CreateString("pending"));
    cJSON_AddItemToArray(status_enum, cJSON_CreateString("in_progress"));
    cJSON_AddItemToArray(status_enum, cJSON_CreateString("completed"));
    cJSON_AddItemToObject(status_prop, "enum", status_enum);
    cJSON_AddStringToObject(status_prop, "description",
        "Current status of the task");
    cJSON_AddItemToObject(item_props, "status", status_prop);

    cJSON_AddItemToObject(todos_items, "properties", item_props);
    cJSON *item_required = cJSON_CreateArray();
    cJSON_AddItemToArray(item_required, cJSON_CreateString("content"));
    cJSON_AddItemToArray(item_required, cJSON_CreateString("activeForm"));
    cJSON_AddItemToArray(item_required, cJSON_CreateString("status"));
    cJSON_AddItemToObject(todos_items, "required", item_required);

    cJSON_AddItemToObject(todos_array, "items", todos_items);
    cJSON_AddItemToObject(todo_props, "todos", todos_array);
    cJSON_AddItemToObject(todo_params, "properties", todo_props);
    cJSON *todo_req = cJSON_CreateArray();
    cJSON_AddItemToArray(todo_req, cJSON_CreateString("todos"));
    cJSON_AddItemToObject(todo_params, "required", todo_req);

    if (!is_tool_disabled("TodoWrite")) {
        cJSON *todo_tool;
        CREATE_TOOL(todo_tool, "TodoWrite",
                    "Creates and updates a task list to track progress on multi-step tasks",
                    todo_params);
        // Add cache_control to TodoWrite if caching is enabled (cache breakpoint before dynamic tools)
        if (enable_caching) {
            add_cache_control(todo_tool);
        }
    } else {
        cJSON_Delete(todo_params);
        LOG_INFO("Tool 'TodoWrite' is disabled via KLAWED_DISABLE_TOOLS");
    }

    // Memory tools (Responses format)
    add_memory_tools(tool_array, TOOL_SCHEMA_RESPONSES);

    // Add MCP tools if MCP is enabled and configured
    #ifndef TEST_BUILD
    if (state && state->mcp_config && mcp_is_enabled()) {
        LOG_DEBUG("get_tool_definitions_for_responses_api: Adding MCP tools");

        // Dynamic MCP tools discovered from servers
        cJSON *mcp_tools = mcp_get_all_tools(state->mcp_config);
        if (mcp_tools && cJSON_IsArray(mcp_tools)) {
            int mcp_tool_count = cJSON_GetArraySize(mcp_tools);
            LOG_DEBUG("get_tool_definitions_for_responses_api: Found %d dynamic MCP tools", mcp_tool_count);
            cJSON *t = NULL;
            cJSON_ArrayForEach(t, mcp_tools) {
                // MCP tools come in Messages API format: { type: "function", function: { name, description, parameters } }
                // Convert to Responses API format: { type: "function", name, description, parameters }
                cJSON *func = cJSON_GetObjectItem(t, "function");
                if (!func) continue;

                cJSON *name_obj = cJSON_GetObjectItem(func, "name");
                const char *tool_name = name_obj && cJSON_IsString(name_obj) ? name_obj->valuestring : "unknown";
                LOG_DEBUG("get_tool_definitions_for_responses_api: Adding dynamic MCP tool '%s'", tool_name);

                // Create flat tool definition for Responses API
                cJSON *flat_tool = cJSON_CreateObject();
                cJSON_AddStringToObject(flat_tool, "type", "function");

                // Copy name
                if (name_obj) {
                    cJSON_AddStringToObject(flat_tool, "name", name_obj->valuestring);
                }

                // Copy description if present
                cJSON *desc = cJSON_GetObjectItem(func, "description");
                if (desc && cJSON_IsString(desc)) {
                    cJSON_AddStringToObject(flat_tool, "description", desc->valuestring);
                }

                // Copy parameters if present
                cJSON *params = cJSON_GetObjectItem(func, "parameters");
                if (params) {
                    cJSON_AddItemToObject(flat_tool, "parameters", cJSON_Duplicate(params, 1));
                }

                cJSON_AddItemToArray(tool_array, flat_tool);
            }
            cJSON_Delete(mcp_tools);
        }

        // ListMcpResources tool
        cJSON *list_res_params = cJSON_CreateObject();
        cJSON_AddStringToObject(list_res_params, "type", "object");
        cJSON *list_res_props = cJSON_CreateObject();
        cJSON *server_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(server_prop, "type", "string");
        cJSON_AddStringToObject(server_prop, "description",
            "Optional server name to filter resources by. If not provided, resources from all servers will be returned.");
        cJSON_AddItemToObject(list_res_props, "server", server_prop);
        cJSON_AddItemToObject(list_res_params, "properties", list_res_props);

        cJSON *list_res_tool;
        CREATE_TOOL(list_res_tool, "ListMcpResources",
                    "Lists available resources from configured MCP servers. "
                    "Each resource object includes a 'server' field indicating which server it's from.",
                    list_res_params);

        // ReadMcpResource tool
        cJSON *read_res_params = cJSON_CreateObject();
        cJSON_AddStringToObject(read_res_params, "type", "object");
        cJSON *read_res_props = cJSON_CreateObject();
        cJSON *read_server_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(read_server_prop, "type", "string");
        cJSON_AddStringToObject(read_server_prop, "description", "The name of the MCP server to read from");
        cJSON_AddItemToObject(read_res_props, "server", read_server_prop);
        cJSON *uri_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(uri_prop, "type", "string");
        cJSON_AddStringToObject(uri_prop, "description", "The URI of the resource to read");
        cJSON_AddItemToObject(read_res_props, "uri", uri_prop);
        cJSON_AddItemToObject(read_res_params, "properties", read_res_props);
        cJSON *read_res_req = cJSON_CreateArray();
        cJSON_AddItemToArray(read_res_req, cJSON_CreateString("server"));
        cJSON_AddItemToArray(read_res_req, cJSON_CreateString("uri"));
        cJSON_AddItemToObject(read_res_params, "required", read_res_req);

        cJSON *read_res_tool;
        CREATE_TOOL(read_res_tool, "ReadMcpResource",
                    "Reads a specific resource from an MCP server, identified by server name and resource URI.",
                    read_res_params);

        // CallMcpTool tool (generic MCP tool invoker)
        cJSON *call_params = cJSON_CreateObject();
        cJSON_AddStringToObject(call_params, "type", "object");
        cJSON *call_props = cJSON_CreateObject();
        cJSON *call_server_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(call_server_prop, "type", "string");
        cJSON_AddStringToObject(call_server_prop, "description", "The MCP server name (as in config)");
        cJSON_AddItemToObject(call_props, "server", call_server_prop);
        cJSON *call_tool_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(call_tool_prop, "type", "string");
        cJSON_AddStringToObject(call_tool_prop, "description", "The tool name exposed by the server");
        cJSON_AddItemToObject(call_props, "tool", call_tool_prop);
        cJSON *call_args_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(call_args_prop, "type", "object");
        cJSON_AddStringToObject(call_args_prop, "description", "Arguments object per the tool's JSON schema");
        cJSON_AddItemToObject(call_props, "arguments", call_args_prop);
        cJSON_AddItemToObject(call_params, "properties", call_props);
        cJSON *call_req = cJSON_CreateArray();
        cJSON_AddItemToArray(call_req, cJSON_CreateString("server"));
        cJSON_AddItemToArray(call_req, cJSON_CreateString("tool"));
        cJSON_AddItemToObject(call_params, "required", call_req);

        cJSON *call_tool;
        CREATE_TOOL(call_tool, "CallMcpTool",
                    "Calls a specific MCP tool by server and tool name with JSON arguments.",
                    call_params);

        LOG_INFO("Added MCP resource tools (ListMcpResources, ReadMcpResource, CallMcpTool)");
    }
    #else
    (void)state;  // Suppress unused parameter warning in test builds
    #endif

    // Load and add dynamic tools from JSON file
    DynamicToolsRegistry dynamic_registry;
    dynamic_tools_init(&dynamic_registry);

    char dynamic_tools_path[DYNAMIC_TOOLS_PATH_MAX];
    if (dynamic_tools_get_path(dynamic_tools_path, sizeof(dynamic_tools_path)) == 0) {
        LOG_INFO("Loading dynamic tools from: %s", dynamic_tools_path);
        int loaded = dynamic_tools_load_from_file(&dynamic_registry, dynamic_tools_path);
        if (loaded > 0) {
            // Add dynamic tools in Responses API format (flat structure)
            for (int i = 0; i < dynamic_registry.count; i++) {
                const DynamicToolDef *tool_def = &dynamic_registry.tools[i];
                cJSON *tool = cJSON_CreateObject();
                if (!tool) continue;

                cJSON_AddStringToObject(tool, "type", "function");
                cJSON_AddStringToObject(tool, "name", tool_def->name);
                cJSON_AddStringToObject(tool, "description", tool_def->description);

                if (tool_def->parameters) {
                    cJSON_AddItemToObject(tool, "parameters", cJSON_Duplicate(tool_def->parameters, 1));
                }

                cJSON_AddItemToArray(tool_array, tool);
            }
            LOG_INFO("Added %d dynamic tool(s) to Responses API tool definitions", dynamic_registry.count);
        } else if (loaded == 0) {
            LOG_DEBUG("No dynamic tools found in %s", dynamic_tools_path);
        } else {
            LOG_WARN("Failed to load dynamic tools from %s", dynamic_tools_path);
        }
    } else {
        LOG_DEBUG("No dynamic tools file found (checked KLAWED_DYNAMIC_TOOLS env, local .klawed/dynamic_tools.json, and ~/.klawed/dynamic_tools.json)");
    }

    dynamic_tools_cleanup(&dynamic_registry);

    return tool_array;
}
