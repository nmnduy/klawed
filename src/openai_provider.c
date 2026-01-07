/*
 * openai_provider.c - OpenAI-compatible API provider implementation
 */

#define _POSIX_C_SOURCE 200809L

#include "klawed_internal.h"  // Must be first to get ApiResponse definition
#include "openai_provider.h"
#include "logger.h"
#include "http_client.h"
#include "tui.h"  // For streaming TUI updates
#include "openai_responses.h"  // For Responses API support

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

/**
 * Duplicate a string using arena allocation
 * Returns: Newly allocated string from arena, or NULL on error
 */
static char* arena_strdup(Arena *arena, const char *str) {
    if (!str || !arena) return NULL;

    size_t len = strlen(str) + 1;  // +1 for null terminator
    char *new_str = arena_alloc(arena, len);
    if (!new_str) return NULL;

    strlcpy(new_str, str, len);
    return new_str;
}

// ============================================================================
// Request Building (using new message format)
// ============================================================================

#include "openai_messages.h"

// Helper to check if prompt caching is enabled
static int is_prompt_caching_enabled(void) {
    const char *disable_env = getenv("DISABLE_PROMPT_CACHING");
    return !(disable_env && (strcmp(disable_env, "1") == 0 ||
                             strcmp(disable_env, "true") == 0 ||
                             strcmp(disable_env, "TRUE") == 0));
}

// Convert curl_slist headers to JSON string for logging


// ============================================================================
// Streaming Support for OpenAI
// ============================================================================

// OpenAI streaming context passed to SSE callback
typedef struct {
    ConversationState *state;        // For interrupt checking and TUI
    char *accumulated_text;          // Accumulated text from deltas
    size_t accumulated_size;
    size_t accumulated_capacity;
    char *finish_reason;             // Finish reason from final chunk
    char *model;                     // Model name from chunks
    char *message_id;                // Message ID from chunks
    int tool_calls_count;            // Number of tool calls
    cJSON *tool_calls_array;         // Array of accumulated tool calls
    Arena *arena;                    // Arena for all allocations
} OpenAIStreamingContext;

static void openai_streaming_context_init(OpenAIStreamingContext *ctx, ConversationState *state) {
    memset(ctx, 0, sizeof(OpenAIStreamingContext));
    ctx->state = state;

    // Create arena for all allocations (64KB should be enough for streaming)
    ctx->arena = arena_create(64 * 1024);
    if (!ctx->arena) {
        return;
    }

    ctx->accumulated_capacity = 4096;
    ctx->accumulated_text = arena_alloc(ctx->arena, ctx->accumulated_capacity);
    if (ctx->accumulated_text) {
        ctx->accumulated_text[0] = '\0';
    }
    ctx->tool_calls_array = cJSON_CreateArray();
}

// Helper function to send streaming event to socket


static void openai_streaming_context_free(OpenAIStreamingContext *ctx) {
    if (!ctx) return;

    // Note: accumulated_text, finish_reason, model, and message_id are all
    // allocated from the arena, so they don't need individual free() calls

    if (ctx->tool_calls_array) {
        cJSON_Delete(ctx->tool_calls_array);
    }

    if (ctx->arena) {
        arena_destroy(ctx->arena);
    }
}

static int openai_streaming_event_handler(StreamEvent *event, void *userdata) {
    OpenAIStreamingContext *ctx = (OpenAIStreamingContext *)userdata;

    // Check for interrupt
    if (ctx->state && ctx->state->interrupt_requested) {
        LOG_DEBUG("OpenAI streaming handler: interrupt requested");
        return 1;  // Abort stream
    }

    if (!event || !event->data) {
        // Ping or [DONE] marker
        if (event && event->type == SSE_EVENT_OPENAI_DONE) {
            LOG_DEBUG("OpenAI stream: received [DONE] marker");
        }
        return 0;
    }

    // Socket streaming support removed - will be reimplemented with ZMQ

    // OpenAI chunk format: { "id": "...", "object": "chat.completion.chunk", "choices": [...], ... }
    if (event->type == SSE_EVENT_OPENAI_CHUNK) {
        // Check if arena was successfully created
        if (!ctx->arena) {
            LOG_DEBUG("OpenAI streaming handler: arena not initialized");
            return 0;  // Continue but don't process
        }

        // Extract model and id if not yet seen
        if (!ctx->model) {
            cJSON *model = cJSON_GetObjectItem(event->data, "model");
            if (model && cJSON_IsString(model)) {
                size_t len = strlen(model->valuestring) + 1;
                ctx->model = arena_alloc(ctx->arena, len);
                if (ctx->model) {
                    strlcpy(ctx->model, model->valuestring, len);
                }
            }
        }
        if (!ctx->message_id) {
            cJSON *id = cJSON_GetObjectItem(event->data, "id");
            if (id && cJSON_IsString(id)) {
                size_t len = strlen(id->valuestring) + 1;
                ctx->message_id = arena_alloc(ctx->arena, len);
                if (ctx->message_id) {
                    strlcpy(ctx->message_id, id->valuestring, len);
                }
            }
        }

        // Process choices array
        cJSON *choices = cJSON_GetObjectItem(event->data, "choices");
        if (choices && cJSON_IsArray(choices)) {
            cJSON *choice = cJSON_GetArrayItem(choices, 0);
            if (choice) {
                cJSON *delta = cJSON_GetObjectItem(choice, "delta");
                if (delta) {
                    // Handle text content
                    cJSON *content = cJSON_GetObjectItem(delta, "content");
                    if (content && cJSON_IsString(content) && content->valuestring) {
                        size_t new_len = strlen(content->valuestring);
                        size_t needed = ctx->accumulated_size + new_len + 1;

                        if (needed > ctx->accumulated_capacity) {
                            size_t new_cap = ctx->accumulated_capacity * 2;
                            if (new_cap < needed) new_cap = needed;
                            char *new_buf = arena_alloc(ctx->arena, new_cap);
                            if (new_buf) {
                                // Copy existing data if any
                                if (ctx->accumulated_text && ctx->accumulated_size > 0) {
                                    memcpy(new_buf, ctx->accumulated_text, ctx->accumulated_size);
                                }
                                ctx->accumulated_text = new_buf;
                                ctx->accumulated_capacity = new_cap;
                            }
                        }

                        if (ctx->accumulated_text && needed <= ctx->accumulated_capacity) {
                            // Initialize TUI on first content
                            if (ctx->accumulated_size == 0 && ctx->state && ctx->state->tui) {
                                tui_add_conversation_line(ctx->state->tui, "[Assistant]", "", COLOR_PAIR_ASSISTANT);
                            }

                            memcpy(ctx->accumulated_text + ctx->accumulated_size,
                                  content->valuestring, new_len);
                            ctx->accumulated_size += new_len;
                            ctx->accumulated_text[ctx->accumulated_size] = '\0';

                            // Stream to TUI if available
                            if (ctx->state && ctx->state->tui) {
                                tui_update_last_conversation_line(ctx->state->tui, content->valuestring);
                            }
                        }
                    }

                    // Handle tool calls
                    cJSON *tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
                    if (tool_calls && cJSON_IsArray(tool_calls)) {
                        // OpenAI streams tool calls incrementally with index and deltas
                        cJSON *tool_call = NULL;
                        cJSON_ArrayForEach(tool_call, tool_calls) {
                            cJSON *index_obj = cJSON_GetObjectItem(tool_call, "index");
                            if (!index_obj || !cJSON_IsNumber(index_obj)) continue;

                            int index = index_obj->valueint;

                            // Ensure array has enough space
                            while (cJSON_GetArraySize(ctx->tool_calls_array) <= index) {
                                cJSON *new_tool = cJSON_CreateObject();
                                cJSON_AddStringToObject(new_tool, "id", "");
                                cJSON_AddStringToObject(new_tool, "type", "function");
                                cJSON *function = cJSON_CreateObject();
                                cJSON_AddStringToObject(function, "name", "");
                                cJSON_AddStringToObject(function, "arguments", "");
                                cJSON_AddItemToObject(new_tool, "function", function);
                                cJSON_AddItemToArray(ctx->tool_calls_array, new_tool);
                            }

                            cJSON *existing_tool = cJSON_GetArrayItem(ctx->tool_calls_array, index);
                            if (!existing_tool) continue;

                            // Update id if present
                            cJSON *id_obj = cJSON_GetObjectItem(tool_call, "id");
                            if (id_obj && cJSON_IsString(id_obj) && id_obj->valuestring[0]) {
                                cJSON_ReplaceItemInObject(existing_tool, "id", cJSON_CreateString(id_obj->valuestring));
                            }

                            // Update function data
                            cJSON *function_delta = cJSON_GetObjectItem(tool_call, "function");
                            if (function_delta) {
                                cJSON *existing_function = cJSON_GetObjectItem(existing_tool, "function");
                                if (!existing_function) {
                                    existing_function = cJSON_CreateObject();
                                    cJSON_AddItemToObject(existing_tool, "function", existing_function);
                                }

                                cJSON *name_obj = cJSON_GetObjectItem(function_delta, "name");
                                if (name_obj && cJSON_IsString(name_obj)) {
                                    cJSON_ReplaceItemInObject(existing_function, "name", cJSON_Duplicate(name_obj, 1));
                                }

                                cJSON *args_obj = cJSON_GetObjectItem(function_delta, "arguments");
                                if (args_obj && cJSON_IsString(args_obj)) {
                                    // Append to existing arguments
                                    cJSON *existing_args = cJSON_GetObjectItem(existing_function, "arguments");
                                    if (existing_args && cJSON_IsString(existing_args)) {
                                        size_t old_len = strlen(existing_args->valuestring);
                                        size_t new_len = strlen(args_obj->valuestring);
                                        char *combined = malloc(old_len + new_len + 1);
                                        if (combined) {
                                            memcpy(combined, existing_args->valuestring, old_len);
                                            memcpy(combined + old_len, args_obj->valuestring, new_len);
                                            combined[old_len + new_len] = '\0';
                                            cJSON_ReplaceItemInObject(existing_function, "arguments", cJSON_CreateString(combined));
                                            free(combined);
                                        }
                                    } else {
                                        cJSON_AddStringToObject(existing_function, "arguments", args_obj->valuestring);
                                    }
                                }
                            }
                        }
                        ctx->tool_calls_count = cJSON_GetArraySize(ctx->tool_calls_array);
                    }
                }

                // Handle finish_reason
                cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
                if (finish_reason && cJSON_IsString(finish_reason) && finish_reason->valuestring) {
                    // Note: No need to free previous finish_reason - it's allocated from arena
                    size_t len = strlen(finish_reason->valuestring) + 1;
                    ctx->finish_reason = arena_alloc(ctx->arena, len);
                    if (ctx->finish_reason) {
                        strlcpy(ctx->finish_reason, finish_reason->valuestring, len);
                        LOG_DEBUG("OpenAI stream: finish_reason=%s", ctx->finish_reason);
                    }
                }
            }
        }
    }

    return 0;
}

// ============================================================================
// OpenAI Provider Implementation
// ============================================================================

/**
 * OpenAI provider's call_api - handles Bearer token authentication
 * Simple single-attempt API call with no auth rotation logic
 */
static ApiCallResult openai_call_api(Provider *self, ConversationState *state) {
    ApiCallResult result = {0};
    OpenAIConfig *config = (OpenAIConfig*)self->config;

    if (!config || !config->api_key || !config->base_url) {
        result.error_message = strdup("OpenAI config or credentials not initialized");
        result.is_retryable = 0;
        return result;
    }

    // Check if streaming is enabled via environment variable
    int enable_streaming = 0;
    const char *streaming_env = getenv("KLAWED_ENABLE_STREAMING");
    if (streaming_env && (strcmp(streaming_env, "1") == 0 || strcasecmp(streaming_env, "true") == 0)) {
        enable_streaming = 1;
    }

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
    int enable_caching = is_prompt_caching_enabled();
    cJSON *request = NULL;

    if (use_responses_api) {
        LOG_INFO("OpenAI provider: using Responses API format");
        request = build_openai_responses_request(state, enable_caching);
    } else {
        request = build_openai_request(state, enable_caching);
    }

    if (!request) {
        result.error_message = strdup("Failed to build request JSON");
        result.is_retryable = 0;
        return result;
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
        return result;
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
        return result;
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
    OpenAIStreamingContext stream_ctx = {0};
    HttpResponse *http_resp = NULL;

    if (enable_streaming) {
        openai_streaming_context_init(&stream_ctx, state);
        http_resp = http_client_execute_stream(&req, openai_streaming_event_handler, &stream_ctx, progress_callback, state);
    } else {
        http_resp = http_client_execute(&req, progress_callback, state);
    }

    if (!http_resp) {
        result.error_message = strdup("Failed to execute HTTP request");
        result.is_retryable = 0;
        curl_slist_free_all(headers);
        if (enable_streaming) openai_streaming_context_free(&stream_ctx);
        return result;
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
        return result;
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
            cJSON_AddStringToObject(raw_json, "id", stream_ctx.message_id ? stream_ctx.message_id : "streaming");
            cJSON_AddStringToObject(raw_json, "object", "chat.completion");
            cJSON_AddStringToObject(raw_json, "model", stream_ctx.model ? stream_ctx.model : "unknown");
            time_t now = time(NULL);
            cJSON_AddNumberToObject(raw_json, "created", (double)now);

            cJSON *choices = cJSON_CreateArray();
            cJSON *choice = cJSON_CreateObject();
            cJSON_AddNumberToObject(choice, "index", 0);

            cJSON *message = cJSON_CreateObject();
            cJSON_AddStringToObject(message, "role", "assistant");

            // Add content if we have text
            if (stream_ctx.accumulated_text && stream_ctx.accumulated_size > 0) {
                cJSON_AddStringToObject(message, "content", stream_ctx.accumulated_text);
            } else {
                cJSON_AddNullToObject(message, "content");
            }

            // Add tool calls if we have any
            if (stream_ctx.tool_calls_count > 0) {
                cJSON_AddItemToObject(message, "tool_calls", cJSON_Duplicate(stream_ctx.tool_calls_array, 1));
            }

            cJSON_AddItemToObject(choice, "message", message);
            cJSON_AddStringToObject(choice, "finish_reason",
                stream_ctx.finish_reason ? stream_ctx.finish_reason : "stop");

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
                result.is_retryable = 0;
                free(result.headers_json);  // Clean up headers JSON in error paths
                result.headers_json = NULL;
                return result;
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
            return result;
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
            return result;
        }

        // Initialize ApiResponse
        memset(api_response, 0, sizeof(ApiResponse));
        api_response->arena = arena;

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
                            return result;
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
                return result;
            }

            cJSON *choice = cJSON_GetArrayItem(choices, 0);
            message = cJSON_GetObjectItem(choice, "message");
            if (!message) {
                result.error_message = strdup("Invalid response format: no message");
                result.is_retryable = 0;
                api_response_free(api_response);
                free(result.headers_json);
                result.headers_json = NULL;
                return result;
            }
        }

        // Extract text content
        cJSON *content = cJSON_GetObjectItem(message, "content");
        if (content && cJSON_IsString(content) && content->valuestring) {
            api_response->message.text = arena_strdup(api_response->arena, content->valuestring);
        } else {
            api_response->message.text = NULL;
        }

        // Extract and validate tool calls
        cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
        if (tool_calls && cJSON_IsArray(tool_calls)) {
            int raw_tool_count = cJSON_GetArraySize(tool_calls);

            // First pass: count valid tool calls
            int valid_count = 0;
            for (int i = 0; i < raw_tool_count; i++) {
                cJSON *tool_call = cJSON_GetArrayItem(tool_calls, i);
                cJSON *function = cJSON_GetObjectItem(tool_call, "function");
                if (function) {
                    valid_count++;
                }
            }

            if (valid_count > 0) {
                api_response->tools = arena_alloc(api_response->arena,
                                                 (size_t)valid_count * sizeof(ToolCall));
                if (!api_response->tools) {
                    result.error_message = strdup("Failed to allocate tool calls from arena");
                    result.is_retryable = 0;
                    if (message_is_synthetic) cJSON_Delete(message);
                    api_response_free(api_response);
                    free(result.headers_json);  // Clean up headers JSON in error paths
                    result.headers_json = NULL;
                    return result;
                }

                // Initialize tool call array
                memset(api_response->tools, 0, (size_t)valid_count * sizeof(ToolCall));

                // Second pass: extract valid tool calls
                int tool_idx = 0;
                for (int i = 0; i < raw_tool_count; i++) {
                    cJSON *tool_call = cJSON_GetArrayItem(tool_calls, i);
                    cJSON *id = cJSON_GetObjectItem(tool_call, "id");
                    cJSON *function = cJSON_GetObjectItem(tool_call, "function");

                    if (!function) {
                        LOG_WARN("Skipping malformed tool_call at index %d (missing 'function' field)", i);
                        continue;
                    }

                    cJSON *name = cJSON_GetObjectItem(function, "name");
                    cJSON *arguments = cJSON_GetObjectItem(function, "arguments");

                    // Copy tool call data using arena allocation
                    api_response->tools[tool_idx].id =
                        (id && cJSON_IsString(id)) ? arena_strdup(api_response->arena, id->valuestring) : NULL;
                    api_response->tools[tool_idx].name =
                        (name && cJSON_IsString(name)) ? arena_strdup(api_response->arena, name->valuestring) : NULL;

                    // Parse arguments string to cJSON
                    if (arguments && cJSON_IsString(arguments)) {
                        api_response->tools[tool_idx].parameters = cJSON_Parse(arguments->valuestring);
                        if (!api_response->tools[tool_idx].parameters) {
                            LOG_WARN("Failed to parse tool arguments, using empty object");
                            api_response->tools[tool_idx].parameters = cJSON_CreateObject();
                        }
                    } else {
                        api_response->tools[tool_idx].parameters = cJSON_CreateObject();
                    }

                    tool_idx++;
                }
                api_response->tool_count = valid_count;
            }
        }

        // Clean up synthetic message if we created one for Responses API
        if (message_is_synthetic) {
            cJSON_Delete(message);
        }

        result.response = api_response;
        return result;
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
    return result;
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

Provider* openai_provider_create(const char *api_key, const char *base_url) {
    LOG_DEBUG("Creating OpenAI provider...");

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

    LOG_INFO("OpenAI provider created successfully (base URL: %s)", config->base_url);
    return provider;
}
