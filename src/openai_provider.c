/*
 * openai_provider.c - OpenAI-compatible API provider implementation
 */

#define _POSIX_C_SOURCE 200809L

#include "klawed_internal.h"  // Must be first to get ApiResponse definition
#include "openai_provider.h"
#include "openai_streaming.h"  // Streaming accumulator
#include "logger.h"
#include "http_client.h"
#include "tui.h"  // For streaming TUI updates
#include "message_queue.h"  // For thread-safe streaming updates
#include "openai_responses.h"  // For Responses API support
#include "openai_chat_parser.h"
#include "tool_utils.h"
#include "util/string_utils.h" // For trim_whitespace

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <curl/curl.h>
#include <bsd/string.h>
// Socket support removed - will be reimplemented with ZMQ
#include "retry_logic.h"  // For common retry logic
#include "arena.h"        // For arena allocation

// Default Anthropic API URL
#define DEFAULT_ANTHROPIC_URL "https://api.anthropic.com/v1/messages"

// ============================================================================
// HTTP Client Wrapper
// ============================================================================

// Progress callback for interrupt handling
static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;

    // clientp is the ConversationState* passed via progress_data parameter
    ConversationState *state = (ConversationState *)clientp;
    if (state && state->interrupt_requested) {
        LOG_DEBUG("Progress callback: interrupt requested, aborting HTTP request");
        return 1;  // Non-zero return aborts the curl transfer
    }

    return 0;  // Continue transfer
}

// ============================================================================
// Request Building (using new message format)
// ============================================================================

#include "openai_messages.h"


// ============================================================================
// Streaming Support for OpenAI
// ============================================================================

// OpenAI streaming context passed to SSE callback
typedef struct {
    OpenAIStreamingAccumulator acc;
    ConversationState *state;
    int assistant_line_added;
    int reasoning_line_added;
} OpenAIProviderStreamingContext;

// Streaming context functions removed - using openai_streaming.h now

static int openai_streaming_event_handler(StreamEvent *event, void *userdata) {
    OpenAIProviderStreamingContext *ctx = (OpenAIProviderStreamingContext *)userdata;

    // Check for interrupt
    if (ctx->state && ctx->state->interrupt_requested) {
        LOG_DEBUG("OpenAI streaming handler: interrupt requested");
        return 1;  // Abort stream
    }

    // Process accumulation
    int ret = openai_streaming_process_event(&ctx->acc, event);
    if (ret != 0) {
        return ret;
    }

    // Handle TUI streaming updates (only for chunks)
    if (event->type == SSE_EVENT_OPENAI_CHUNK && event->data) {
        cJSON *choices = cJSON_GetObjectItem(event->data, "choices");
        if (choices && cJSON_IsArray(choices)) {
            cJSON *choice = cJSON_GetArrayItem(choices, 0);
            if (choice) {
                cJSON *delta = cJSON_GetObjectItem(choice, "delta");
                if (delta) {
                    // Stream text content
                    cJSON *content = cJSON_GetObjectItem(delta, "content");
                    if (content && cJSON_IsString(content) && content->valuestring && content->valuestring[0]) {
                        if (!ctx->assistant_line_added) {
                            if (ctx->state) {
                                if (ctx->state->tui_queue) {
                                    post_tui_stream_start(ctx->state->tui_queue, "[Assistant]", COLOR_PAIR_ASSISTANT);
                                } else if (ctx->state->tui) {
                                    tui_add_conversation_line(ctx->state->tui, "[Assistant]", "", COLOR_PAIR_ASSISTANT);
                                }
                            }
                            ctx->assistant_line_added = 1;
                        }
                        if (ctx->state) {
                            if (ctx->state->tui_queue) {
                                post_tui_message(ctx->state->tui_queue, TUI_MSG_STREAM_APPEND, content->valuestring);
                            } else if (ctx->state->tui) {
                                tui_update_last_conversation_line(ctx->state->tui, content->valuestring);
                            }
                            // Send streaming chunk to SQLite queue if enabled
                            if (ctx->state->sqlite_queue_context) {
                                sqlite_queue_send_streaming_chunk(ctx->state->sqlite_queue_context,
                                                                   "client", content->valuestring);
                            }
                        }
                    }

                    // Stream reasoning content
                    cJSON *reasoning = cJSON_GetObjectItem(delta, "reasoning_content");
                    if (reasoning && cJSON_IsString(reasoning) && reasoning->valuestring && reasoning->valuestring[0]) {
                        if (!ctx->reasoning_line_added) {
                            if (ctx->state) {
                                if (ctx->state->tui_queue) {
                                    post_tui_stream_start(ctx->state->tui_queue, "⟨Reasoning⟩", COLOR_PAIR_TOOL_DIM);
                                } else if (ctx->state->tui) {
                                    tui_add_conversation_line(ctx->state->tui, "⟨Reasoning⟩", "", COLOR_PAIR_TOOL_DIM);
                                }
                            }
                            ctx->reasoning_line_added = 1;
                        }
                        if (ctx->state) {
                            if (ctx->state->tui_queue) {
                                post_tui_message(ctx->state->tui_queue, TUI_MSG_STREAM_APPEND, reasoning->valuestring);
                            } else if (ctx->state->tui) {
                                tui_update_last_conversation_line(ctx->state->tui, reasoning->valuestring);
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}

/**
 * OpenAI provider's call_api - handles Bearer token authentication
 * Simple single-attempt API call with no auth rotation logic
 */
static void openai_call_api(Provider *self, ConversationState *state, ApiCallResult *out) {
    ApiCallResult result = {0};
    OpenAIConfig *config = (OpenAIConfig*)self->config;

    if (!config || !config->api_key || !config->base_url) {
        result.error_message = strdup("OpenAI config or credentials not initialized");
        result.is_retryable = 0;
        *out = result;
        return;
    }

    int enable_streaming = is_streaming_enabled(state);

    // Detect if we are targeting the new /responses endpoint (case-insensitive)
    int use_responses_api = 0;
    if (config->base_url) {
        // Check for "/responses" in the URL (case-insensitive)
        const char *url = config->base_url;
        const char *pattern = "/responses";
        size_t pattern_len = strlen(pattern);

        for (size_t i = 0; url[i] && url[i + pattern_len - 1]; i++) {
            if (url[i] == '/') {
                int match = 1;
                for (size_t j = 0; j < pattern_len; j++) {
                    if (tolower((unsigned char)url[i + j]) != tolower((unsigned char)pattern[j])) {
                        match = 0;
                        break;
                    }
                }
                // Check that after "/responses" we have end of string, '/', '?', or '#'
                if (match) {
                    char next_char = url[i + pattern_len];
                    if (next_char == '\0' || next_char == '/' || next_char == '?' || next_char == '#') {
                        use_responses_api = 1;
                        LOG_DEBUG("OpenAI provider: detected responses API URL: %s", url);
                        break;
                    }
                }
            }
        }
    }

    // Build request JSON using appropriate format
    // Note: OpenAI-compatible APIs do NOT support Anthropic's cache_control field.
    // We always pass enable_caching=0 for OpenAI provider since it's an Anthropic-specific
    // extension that causes validation errors with strict OpenAI-compatible APIs (Fireworks, etc.)
    int enable_caching = 0;
    cJSON *request = NULL;

    if (use_responses_api) {
        LOG_INFO("OpenAI provider: using Responses API format");
        request = build_openai_responses_request(state, enable_caching);
    } else {
        // Check if we need to preserve reasoning_content (for Moonshot/Kimi)
        int include_reasoning = (config->reasoning_content_mode == REASONING_CONTENT_PRESERVE);
        request = build_openai_request_with_reasoning(state, enable_caching, include_reasoning);
    }

    if (!request) {
        result.error_message = strdup("Failed to build request JSON");
        result.is_retryable = 0;
        *out = result; return;
    }

    LOG_DEBUG("OpenAI: Built request with caching %s, format: %s",
              enable_caching ? "enabled" : "disabled",
              use_responses_api ? "Responses API" : "Chat Completions");

    // Add streaming parameter if enabled
    if (enable_streaming) {
        cJSON_AddBoolToObject(request, "stream", cJSON_True);
        LOG_DEBUG("OpenAI provider: streaming enabled");
    }

    char *openai_json = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    LOG_DEBUG("OpenAI: Request serialized, length: %zu bytes", openai_json ? strlen(openai_json) : 0);

    if (!openai_json) {
        result.error_message = strdup("Failed to serialize request JSON");
        result.is_retryable = 0;
        *out = result; return;
    }

    // Build full URL (base_url is already complete for OpenAI, just use it directly)
    // If caller provided a /responses URL, we send as-is; otherwise treat as chat/completions-compatible
    const char *url = config->base_url;

    // Set up headers
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // Add authentication header (custom format or default Bearer token)
    char auth_header[512];
    if (config->auth_header_template) {
        // Use custom auth header template (should contain %s for API key)
        // Find %s in the template and replace it with the API key
        const char *percent_s = strstr(config->auth_header_template, "%s");
        if (percent_s) {
            // Calculate lengths
            size_t prefix_len = (size_t)(percent_s - config->auth_header_template);
            size_t api_key_len = strlen(config->api_key);
            size_t suffix_len = strlen(percent_s + 2); // +2 to skip "%s"

            // Build auth header manually
            if (prefix_len + api_key_len + suffix_len + 1 < sizeof(auth_header)) {
                strlcpy(auth_header, config->auth_header_template, prefix_len + 1);
                // Use strlcat for safety
                strlcat(auth_header, config->api_key, sizeof(auth_header));
                strlcat(auth_header, percent_s + 2, sizeof(auth_header));
            } else {
                // Fallback if template is too long
                strlcpy(auth_header, config->auth_header_template, sizeof(auth_header));
                LOG_WARN("Auth header template too long, truncated");
            }
        } else {
            // No %s found, use template as-is
            strlcpy(auth_header, config->auth_header_template, sizeof(auth_header));
        }
    } else {
        // Default Bearer token format
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->api_key);
    }
    headers = curl_slist_append(headers, auth_header);

    // Add extra headers from environment
    if (config->extra_headers) {
        for (int i = 0; i < config->extra_headers_count; i++) {
            if (config->extra_headers[i]) {
                headers = curl_slist_append(headers, config->extra_headers[i]);
            }
        }
    }

    if (!headers) {
        result.error_message = strdup("Failed to setup HTTP headers");
        result.is_retryable = 0;
        result.request_json = openai_json;  // Store for logging
        result.headers_json = NULL;  // No headers to log
        *out = result; return;
    }

    // Execute HTTP request using HTTP client
    HttpRequest req = {0};
    req.url = url;
    req.method = "POST";
    req.body = openai_json;
    req.headers = headers;
    req.connect_timeout_ms = 30000;  // 30 seconds
    req.total_timeout_ms = 300000;   // 5 minutes
    req.enable_streaming = enable_streaming;

    // Store request JSON for logging (caller must free)
    result.request_json = openai_json;

    // Include endpoint info for logging/debugging
    result.headers_json = http_headers_to_json(headers);

    // Initialize streaming context if needed
    OpenAIProviderStreamingContext stream_ctx = {0};
    HttpResponse *http_resp = NULL;

    if (enable_streaming) {
        openai_streaming_accumulator_init(&stream_ctx.acc);
        stream_ctx.state = state;
        http_resp = http_client_execute_stream(&req, openai_streaming_event_handler, &stream_ctx, progress_callback, state);
    } else {
        http_resp = http_client_execute(&req, progress_callback, state);
    }

    if (!http_resp) {
        result.error_message = strdup("Failed to execute HTTP request");
        result.is_retryable = 0;
        curl_slist_free_all(headers);
        if (enable_streaming) openai_streaming_accumulator_free(&stream_ctx.acc);
        *out = result; return;
    }

    // Copy results from HTTP response
    result.duration_ms = http_resp->duration_ms;
    result.http_status = http_resp->status_code;
    result.raw_response = http_resp->body ? strdup(http_resp->body) : NULL;
    if (result.headers_json) {
        free(result.headers_json);
        result.headers_json = NULL;
    }
    result.headers_json = http_headers_to_json(http_resp->headers);

    // Handle HTTP errors
    if (http_resp->error_message) {
        result.error_message = strdup(http_resp->error_message);
        result.is_retryable = http_resp->is_retryable;
        http_response_free(http_resp);
        curl_slist_free_all(headers);
        if (enable_streaming) openai_streaming_context_free(&stream_ctx);
        *out = result; return;
    }

    // Clean up HTTP response (but keep body since we duplicated it)
    char *body_to_free = http_resp->body;
    http_resp->body = NULL;  // Prevent double free
    http_response_free(http_resp);
    free(body_to_free);

    // Free headers (they were copied by http_client_execute)
    curl_slist_free_all(headers);

    // Check HTTP status
    if (result.http_status >= 200 && result.http_status < 300) {
        // Success
        cJSON *raw_json = NULL;

        // If streaming was used, reconstruct response from streaming context
        if (enable_streaming) {
            LOG_DEBUG("Reconstructing OpenAI response from streaming context");

            // Build synthetic response in OpenAI format
            raw_json = cJSON_CreateObject();
            cJSON_AddStringToObject(raw_json, "id", stream_ctx.acc.message_id ? stream_ctx.acc.message_id : "streaming");
            cJSON_AddStringToObject(raw_json, "object", "chat.completion");
            cJSON_AddStringToObject(raw_json, "model", stream_ctx.acc.model ? stream_ctx.acc.model : "unknown");
            time_t now = time(NULL);
            cJSON_AddNumberToObject(raw_json, "created", (double)now);

            cJSON *choices = cJSON_CreateArray();
            cJSON *choice = cJSON_CreateObject();
            cJSON_AddNumberToObject(choice, "index", 0);

            cJSON *message = cJSON_CreateObject();
            cJSON_AddStringToObject(message, "role", "assistant");

            // Add content if we have text
            if (stream_ctx.acc.accumulated_text && stream_ctx.acc.accumulated_size > 0) {
                cJSON_AddStringToObject(message, "content", stream_ctx.acc.accumulated_text);
            } else {
                cJSON_AddNullToObject(message, "content");
            }

            // Add reasoning_content if we have any (for thinking models)
            if (stream_ctx.acc.accumulated_reasoning && stream_ctx.acc.reasoning_size > 0) {
                cJSON_AddStringToObject(message, "reasoning_content", stream_ctx.acc.accumulated_reasoning);
                LOG_DEBUG("Added reasoning_content to response (%zu bytes)", stream_ctx.acc.reasoning_size);
            }

            // Add tool calls using the shared accumulator's filtering
            if (stream_ctx.acc.tool_accumulator) {
                cJSON *all_tool_calls = tool_accumulator_get_tool_calls(stream_ctx.acc.tool_accumulator);
                int raw_count = all_tool_calls ? cJSON_GetArraySize(all_tool_calls) : 0;
                int valid_count = tool_accumulator_count_valid(stream_ctx.acc.tool_accumulator);
                LOG_DEBUG("Streaming tool call reconstruction: raw_count=%d, valid_count=%d",
                          raw_count, valid_count);

                if (all_tool_calls && raw_count > 0) {
                    char *raw_tool_calls_str = cJSON_PrintUnformatted(all_tool_calls);
                    if (raw_tool_calls_str) {
                        LOG_DEBUG("Streaming tool call reconstruction raw payload: %s",
                                  raw_tool_calls_str);
                        free(raw_tool_calls_str);
                    }
                }

                if (valid_count > 0) {
                    cJSON *filtered_tool_calls = tool_accumulator_filter_valid(stream_ctx.acc.tool_accumulator);
                    if (filtered_tool_calls && cJSON_GetArraySize(filtered_tool_calls) > 0) {
                        char *filtered_tool_calls_str = cJSON_PrintUnformatted(filtered_tool_calls);
                        if (filtered_tool_calls_str) {
                            LOG_DEBUG("Streaming tool call reconstruction filtered payload: %s",
                                      filtered_tool_calls_str);
                            free(filtered_tool_calls_str);
                        }
                        cJSON_AddItemToObject(message, "tool_calls", filtered_tool_calls);
                    } else {
                        cJSON_Delete(filtered_tool_calls);
                    }
                }
            }

            cJSON_AddItemToObject(choice, "message", message);
            cJSON_AddStringToObject(choice, "finish_reason",
                stream_ctx.acc.finish_reason ? stream_ctx.acc.finish_reason : "stop");

            cJSON_AddItemToArray(choices, choice);
            cJSON_AddItemToObject(raw_json, "choices", choices);

            // Add usage (placeholder since OpenAI streaming doesn't always include it)
            cJSON *usage = cJSON_CreateObject();
            cJSON_AddNumberToObject(usage, "prompt_tokens", 0);
            cJSON_AddNumberToObject(usage, "completion_tokens", 0);
            cJSON_AddNumberToObject(usage, "total_tokens", 0);
            cJSON_AddItemToObject(raw_json, "usage", usage);

            // Free streaming context
            openai_streaming_context_free(&stream_ctx);
        } else {
            // Non-streaming: parse response
            raw_json = cJSON_Parse(result.raw_response);
            if (!raw_json) {
                result.error_message = strdup("Failed to parse JSON response");
                result.is_retryable = 1;  // Malformed response might be transient, retry
                free(result.headers_json);  // Clean up headers JSON in error paths
                result.headers_json = NULL;
                *out = result; return;
            }
        }

        // Extract vendor-agnostic response data
        // Create arena for ApiResponse and all its string data
        Arena *arena = arena_create(16384);  // 16KB arena for API response
        if (!arena) {
            result.error_message = strdup("Failed to create arena for ApiResponse");
            result.is_retryable = 0;
            cJSON_Delete(raw_json);
            free(result.headers_json);  // Clean up headers JSON in error paths
            result.headers_json = NULL;
            if (enable_streaming) openai_streaming_context_free(&stream_ctx);
            *out = result; return;
        }

        // Allocate ApiResponse from arena
        ApiResponse *api_response = arena_alloc(arena, sizeof(ApiResponse));
        if (!api_response) {
            result.error_message = strdup("Failed to allocate ApiResponse from arena");
            result.is_retryable = 0;
            arena_destroy(arena);
            cJSON_Delete(raw_json);
            free(result.headers_json);  // Clean up headers JSON in error paths
            result.headers_json = NULL;
            if (enable_streaming) openai_streaming_context_free(&stream_ctx);
            *out = result; return;
        }

        // Initialize ApiResponse
        memset(api_response, 0, sizeof(ApiResponse));
        api_response->arena = arena;
        api_response->ui_streamed = enable_streaming ? 1 : 0;

        // Initialize error_message to NULL
        api_response->error_message = NULL;

        // Keep raw response for history
        api_response->raw_response = raw_json;

        // Detect API format: Responses API has "output" array, Chat Completions has "choices"
        cJSON *output = cJSON_GetObjectItem(raw_json, "output");
        cJSON *message = NULL;
        int message_is_synthetic = 0;  // Track if we created message object (needs cleanup)

        if (output && cJSON_IsArray(output) && cJSON_GetArraySize(output) > 0) {
            // Responses API format
            LOG_DEBUG("Parsing Responses API format");

            // Find the message item and function_call items in output array
            cJSON *message_item = NULL;
            cJSON *output_item = NULL;
            cJSON *tool_calls_array = NULL;  // For function_call items

            cJSON_ArrayForEach(output_item, output) {
                cJSON *type = cJSON_GetObjectItem(output_item, "type");
                if (type && cJSON_IsString(type)) {
                    if (strcmp(type->valuestring, "message") == 0) {
                        message_item = output_item;
                    } else if (strcmp(type->valuestring, "function_call") == 0) {
                        // Build tool_calls array from function_call items
                        if (!tool_calls_array) {
                            tool_calls_array = cJSON_CreateArray();
                        }
                        cJSON *tool_call = cJSON_CreateObject();

                        // Extract call_id for id field
                        cJSON *call_id = cJSON_GetObjectItem(output_item, "call_id");
                        if (call_id && cJSON_IsString(call_id)) {
                            cJSON_AddStringToObject(tool_call, "id", call_id->valuestring);
                        } else {
                            cJSON_AddStringToObject(tool_call, "id", "");
                        }

                        // Extract name
                        cJSON *name = cJSON_GetObjectItem(output_item, "name");
                        if (name && cJSON_IsString(name)) {
                            cJSON_AddStringToObject(tool_call, "name", name->valuestring);
                        }

                        // Extract arguments
                        cJSON *arguments = cJSON_GetObjectItem(output_item, "arguments");
                        if (arguments && cJSON_IsString(arguments)) {
                            cJSON_AddStringToObject(tool_call, "arguments", arguments->valuestring);
                        }

                        cJSON_AddItemToArray(tool_calls_array, tool_call);
                    }
                }
            }

            // Build a synthetic message object compatible with Chat Completions format
            message = cJSON_CreateObject();
            message_is_synthetic = 1;
            cJSON_AddStringToObject(message, "role", "assistant");

            // Handle message item for text content (if present)
            if (message_item) {
                // Get content array from message item
                cJSON *content_array = cJSON_GetObjectItem(message_item, "content");
                if (content_array && cJSON_IsArray(content_array)) {
                    // Extract text from content array items with type "output_text"
                    // First pass: calculate total length needed
                    size_t total_length = 0;
                    cJSON *content_item = NULL;
                    cJSON_ArrayForEach(content_item, content_array) {
                        cJSON *type = cJSON_GetObjectItem(content_item, "type");
                        if (type && cJSON_IsString(type) && strcmp(type->valuestring, "output_text") == 0) {
                            cJSON *text = cJSON_GetObjectItem(content_item, "text");
                            if (text && cJSON_IsString(text) && text->valuestring) {
                                total_length += strlen(text->valuestring);
                            }
                        }
                    }

                    // Second pass: allocate and copy if we have text
                    if (total_length > 0) {
                        char *text_content = malloc(total_length + 1);
                        if (!text_content) {
                            cJSON_Delete(message);
                            if (tool_calls_array) cJSON_Delete(tool_calls_array);
                            result.error_message = strdup("Failed to allocate text buffer");
                            result.is_retryable = 0;
                            api_response_free(api_response);
                            free(result.headers_json);
                            result.headers_json = NULL;
                            *out = result; return;
                        }

                        size_t text_length = 0;
                        cJSON_ArrayForEach(content_item, content_array) {
                            cJSON *type = cJSON_GetObjectItem(content_item, "type");
                            if (type && cJSON_IsString(type) && strcmp(type->valuestring, "output_text") == 0) {
                                cJSON *text = cJSON_GetObjectItem(content_item, "text");
                                if (text && cJSON_IsString(text) && text->valuestring) {
                                    size_t text_len = strlen(text->valuestring);
                                    memcpy(text_content + text_length, text->valuestring, text_len);
                                    text_length += text_len;
                                }
                            }
                        }
                        text_content[text_length] = '\0';

                        cJSON_AddStringToObject(message, "content", text_content);
                        free(text_content);
                    } else {
                        cJSON_AddNullToObject(message, "content");
                    }
                } else {
                    cJSON_AddNullToObject(message, "content");
                }
            } else {
                // No message item, content is null
                cJSON_AddNullToObject(message, "content");
            }

            // Add tool_calls to message if we have function_call items
            if (tool_calls_array && cJSON_GetArraySize(tool_calls_array) > 0) {
                // Convert function_call format to standard tool_calls format
                cJSON *tool_calls = cJSON_CreateArray();
                int func_count = cJSON_GetArraySize(tool_calls_array);

                for (int i = 0; i < func_count; i++) {
                    cJSON *func_call = cJSON_GetArrayItem(tool_calls_array, i);
                    cJSON *tool_call = cJSON_CreateObject();

                    // Copy id
                    cJSON *id = cJSON_GetObjectItem(func_call, "id");
                    if (id && cJSON_IsString(id)) {
                        cJSON_AddStringToObject(tool_call, "id", id->valuestring);
                    } else {
                        cJSON_AddStringToObject(tool_call, "id", "");
                    }

                    cJSON_AddStringToObject(tool_call, "type", "function");

                    // Create function object
                    cJSON *function = cJSON_CreateObject();
                    cJSON *name = cJSON_GetObjectItem(func_call, "name");
                    if (name && cJSON_IsString(name)) {
                        cJSON_AddStringToObject(function, "name", name->valuestring);
                    }
                    cJSON *arguments = cJSON_GetObjectItem(func_call, "arguments");
                    if (arguments && cJSON_IsString(arguments)) {
                        cJSON_AddStringToObject(function, "arguments", arguments->valuestring);
                    }
                    cJSON_AddItemToObject(tool_call, "function", function);

                    cJSON_AddItemToArray(tool_calls, tool_call);
                }

                cJSON_AddItemToObject(message, "tool_calls", tool_calls);
                cJSON_Delete(tool_calls_array);
                tool_calls_array = NULL;
            } else {
                if (tool_calls_array) cJSON_Delete(tool_calls_array);
            }
        } else {
            // Chat Completions API format
            LOG_DEBUG("Parsing Chat Completions API format");
            cJSON *choices = cJSON_GetObjectItem(raw_json, "choices");
            if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
                result.error_message = strdup("Invalid response format: no choices or output");
                result.is_retryable = 0;
                api_response_free(api_response);
                free(result.headers_json);
                result.headers_json = NULL;
                *out = result; return;
            }

            cJSON *choice = cJSON_GetArrayItem(choices, 0);
            message = cJSON_GetObjectItem(choice, "message");
            if (!message) {
                result.error_message = strdup("Invalid response format: no message");
                result.is_retryable = 0;
                api_response_free(api_response);
                free(result.headers_json);
                result.headers_json = NULL;
                *out = result; return;
            }
        }

        if (openai_fill_api_response_from_message(api_response, message, "OpenAI") != 0) {
            result.error_message = strdup("Failed to allocate tool calls from arena");
            result.is_retryable = 0;
            if (message_is_synthetic) cJSON_Delete(message);
            api_response_free(api_response);
            free(result.headers_json);
            result.headers_json = NULL;
            *out = result; return;
        }

        // Clean up synthetic message if we created one for Responses API
        if (message_is_synthetic) {
            cJSON_Delete(message);
        }

        result.response = api_response;
        *out = result; return;
    }

    // HTTP error
    result.is_retryable = is_http_error_retryable(result.http_status);

    // Extract error message from response if JSON
    cJSON *error_json = cJSON_Parse(result.raw_response);
    if (error_json) {
        cJSON *error_obj = cJSON_GetObjectItem(error_json, "error");
        if (error_obj) {
            cJSON *message = cJSON_GetObjectItem(error_obj, "message");
            cJSON *error_type = cJSON_GetObjectItem(error_obj, "type");

            // Check for context length limit error
            if (message && cJSON_IsString(message)) {
                const char *msg_text = message->valuestring;
                const char *type_text = (error_type && cJSON_IsString(error_type)) ? error_type->valuestring : "";

                // Detect context length overflow errors
                if (is_context_length_error(msg_text, type_text)) {
                    // Provide user-friendly context length error message
                    result.error_message = get_context_length_error_message();
                    result.is_retryable = 0;  // Context length errors are not retryable
                } else {
                    // Use the original error message for other types of errors
                    result.error_message = strdup(msg_text);
                }
            }
        }
        cJSON_Delete(error_json);
    }

    if (!result.error_message) {
        char buf[256];
        snprintf(buf, sizeof(buf), "HTTP %ld", result.http_status);
        result.error_message = strdup(buf);
    }

    free(result.headers_json);  // Clean up headers JSON in error paths
    result.headers_json = NULL;
    if (enable_streaming) openai_streaming_context_free(&stream_ctx);
    *out = result; return;
}

/**
 * Cleanup OpenAI provider resources
 */
static void openai_cleanup(Provider *self) {
    if (!self) return;

    LOG_DEBUG("OpenAI provider: cleaning up resources");

    if (self->config) {
        OpenAIConfig *config = (OpenAIConfig*)self->config;
        free(config->api_key);
        free(config->base_url);
        free(config->auth_header_template);

        // Free extra headers
        if (config->extra_headers) {
            for (int i = 0; i < config->extra_headers_count; i++) {
                free(config->extra_headers[i]);
            }
            free(config->extra_headers);
        }

        free(config);
    }

    free(self);
    LOG_DEBUG("OpenAI provider: cleanup complete");
}

// ============================================================================
// Public API
// ============================================================================

Provider* openai_provider_create_with_reasoning_mode(const char *api_key,
                                                      const char *base_url,
                                                      ReasoningContentMode reasoning_mode) {
    LOG_DEBUG("Creating OpenAI provider (reasoning_mode=%d)...", reasoning_mode);

    if (!api_key || api_key[0] == '\0') {
        LOG_ERROR("OpenAI provider: API key is required");
        return NULL;
    }

    // Allocate provider structure
    Provider *provider = calloc(1, sizeof(Provider));
    if (!provider) {
        LOG_ERROR("OpenAI provider: failed to allocate provider");
        return NULL;
    }

    // Allocate config structure
    OpenAIConfig *config = calloc(1, sizeof(OpenAIConfig));
    if (!config) {
        LOG_ERROR("OpenAI provider: failed to allocate config");
        free(provider);
        return NULL;
    }

    // Set reasoning content mode
    config->reasoning_content_mode = reasoning_mode;

    // Copy API key
    config->api_key = strdup(api_key);
    if (!config->api_key) {
        LOG_ERROR("OpenAI provider: failed to duplicate API key");
        free(config);
        free(provider);
        return NULL;
    }

    // Copy or set default base URL - ensure it has the proper endpoint path
    if (base_url && base_url[0] != '\0') {
        // Check if base_url already has an endpoint path (contains "/v1/" or "/v2/" or "/v3/" or "/v4/" etc)
        // If it does, use it as-is; otherwise append the OpenAI endpoint
        if (strstr(base_url, "/v1/") != NULL || strstr(base_url, "/v2/") != NULL ||
            strstr(base_url, "/v3/") != NULL || strstr(base_url, "/v4/") != NULL) {
            // Already has an endpoint path (likely Anthropic, OpenAI, or custom)
            config->base_url = strdup(base_url);
        } else {
            // Base domain only - append OpenAI chat completions endpoint
            size_t url_len = strlen(base_url) + strlen("/v1/chat/completions") + 1;
            config->base_url = malloc(url_len);
            if (config->base_url) {
                snprintf(config->base_url, url_len, "%s/v1/chat/completions", base_url);
                LOG_INFO("OpenAI provider: appended endpoint path to base URL: %s", config->base_url);
            }
        }

        if (!config->base_url) {
            LOG_ERROR("OpenAI provider: failed to set base URL");
            free(config->api_key);
            free(config);
            free(provider);
            return NULL;
        }
    } else {
        config->base_url = strdup(DEFAULT_ANTHROPIC_URL);
        if (!config->base_url) {
            LOG_ERROR("OpenAI provider: failed to set default base URL");
            free(config->api_key);
            free(config);
            free(provider);
            return NULL;
        }
    }

    // Read custom auth header template from environment
    const char *auth_header_env = getenv("OPENAI_AUTH_HEADER");
    if (auth_header_env && auth_header_env[0] != '\0') {
        config->auth_header_template = strdup(auth_header_env);
        if (!config->auth_header_template) {
            LOG_ERROR("OpenAI provider: failed to duplicate auth header template");
            free(config->base_url);
            free(config->api_key);
            free(config);
            free(provider);
            return NULL;
        }
        LOG_INFO("OpenAI provider: using custom auth header template: %s", config->auth_header_template);
    } else {
        config->auth_header_template = NULL;  // Use default Bearer token format
    }

    // Read extra headers from environment
    const char *extra_headers_env = getenv("OPENAI_EXTRA_HEADERS");
    if (extra_headers_env && extra_headers_env[0] != '\0') {
        // Parse comma-separated headers
        char *extra_headers_copy = strdup(extra_headers_env);
        if (!extra_headers_copy) {
            LOG_ERROR("OpenAI provider: failed to duplicate extra headers string");
            free(config->auth_header_template);
            free(config->base_url);
            free(config->api_key);
            free(config);
            free(provider);
            return NULL;
        }

        // Count headers
        char *token = strtok(extra_headers_copy, ",");
        config->extra_headers_count = 0;
        while (token) {
            config->extra_headers_count++;
            token = strtok(NULL, ",");
        }

        // Allocate array
        config->extra_headers = calloc((size_t)config->extra_headers_count + 1, sizeof(char*));
        if (!config->extra_headers) {
            LOG_ERROR("OpenAI provider: failed to allocate extra headers array");
            free(extra_headers_copy);
            free(config->auth_header_template);
            free(config->base_url);
            free(config->api_key);
            free(config);
            free(provider);
            return NULL;
        }

        // Copy headers - reset buffer for second strtok pass
        // Since extra_headers_copy was allocated with strdup(extra_headers_env),
        // it has exactly strlen(extra_headers_env) + 1 bytes
        memcpy(extra_headers_copy, extra_headers_env, strlen(extra_headers_env) + 1);
        token = strtok(extra_headers_copy, ",");
        for (int i = 0; i < config->extra_headers_count && token; i++) {
            // Trim whitespace
            while (*token == ' ' || *token == '\t') token++;
            char *end = token + strlen(token) - 1;
            while (end > token && (*end == ' ' || *end == '\t')) end--;
            *(end + 1) = '\0';

            config->extra_headers[i] = strdup(token);
            if (!config->extra_headers[i]) {
                LOG_ERROR("OpenAI provider: failed to duplicate extra header");
                // Cleanup allocated headers
                for (int j = 0; j < i; j++) {
                    free(config->extra_headers[j]);
                }
                free(config->extra_headers);
                free(extra_headers_copy);
                free(config->auth_header_template);
                free(config->base_url);
                free(config->api_key);
                free(config);
                free(provider);
                return NULL;
            }
            token = strtok(NULL, ",");
        }
        config->extra_headers[config->extra_headers_count] = NULL;  // NULL-terminate

        LOG_INFO("OpenAI provider: loaded %d extra headers", config->extra_headers_count);
        free(extra_headers_copy);
    } else {
        config->extra_headers = NULL;
        config->extra_headers_count = 0;
    }

    // Set up provider interface
    provider->name = "OpenAI";
    provider->config = config;
    provider->call_api = openai_call_api;
    provider->cleanup = openai_cleanup;

    LOG_INFO("OpenAI provider created successfully (base URL: %s, reasoning_mode=%d)",
             config->base_url, config->reasoning_content_mode);
    return provider;
}

Provider* openai_provider_create(const char *api_key, const char *base_url) {
    // Default: discard reasoning_content (standard OpenAI/DeepSeek behavior)
    return openai_provider_create_with_reasoning_mode(api_key, base_url, REASONING_CONTENT_DISCARD);
}

ReasoningContentMode openai_provider_get_reasoning_mode(Provider *provider) {
    if (!provider || !provider->config) {
        return REASONING_CONTENT_DISCARD;
    }

    // Check if this is an OpenAI-compatible provider by checking if it has our call_api function
    if (provider->call_api != openai_call_api) {
        return REASONING_CONTENT_DISCARD;
    }

    OpenAIConfig *config = (OpenAIConfig *)provider->config;
    return config->reasoning_content_mode;
}
