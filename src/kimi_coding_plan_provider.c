/*
 * kimi_coding_plan_provider.c - Kimi Coding Plan API provider implementation
 *
 * Uses OAuth 2.0 device authorization and OpenAI-compatible API format.
 */

#define _POSIX_C_SOURCE 200809L

#include "klawed_internal.h"  // Must be first to get ApiResponse definition
#include "kimi_coding_plan_provider.h"
#include "kimi_oauth.h"
#include "openai_messages.h"
#include "http_client.h"
#include "logger.h"
#include "arena.h"
#include "retry_logic.h"
#include "tui.h"
#include "util/string_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // For strcasecmp
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <curl/curl.h>
#include <bsd/string.h>

// Default model and API endpoint
#define KIMI_DEFAULT_MODEL "kimi-for-coding"
#define KIMI_API_ENDPOINT "https://api.kimi.com/coding/v1/chat/completions"

// ============================================================================
// Helper Functions
// ============================================================================

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

/**
 * OAuth message callback - displays messages to TUI if available, otherwise console
 * This allows the OAuth flow to show browser login prompts in the conversation TUI
 */
static void kimi_oauth_message_callback(void *user_data, const char *message, int is_error) {
    ConversationState *state = (ConversationState *)user_data;

    if (state && state->tui) {
        // Display in TUI conversation
        TUIColorPair color = is_error ? COLOR_PAIR_ERROR : COLOR_PAIR_STATUS;
        tui_add_conversation_line(state->tui, "[System]", message, color);
        tui_refresh(state->tui);
    } else {
        // Fallback to console output
        if (is_error) {
            fprintf(stderr, "%s\n", message);
        } else {
            printf("%s\n", message);
        }
    }
}

// Progress callback for interrupt handling
static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;

    ConversationState *state = (ConversationState *)clientp;
    if (state && state->interrupt_requested) {
        LOG_DEBUG("Kimi progress callback: interrupt requested, aborting HTTP request");
        return 1;  // Non-zero return aborts the curl transfer
    }

    return 0;  // Continue transfer
}

// ============================================================================
// Streaming Support
// ============================================================================

// Kimi streaming context passed to SSE callback
typedef struct {
    ConversationState *state;        // For interrupt checking and TUI
    char *accumulated_text;          // Accumulated text from deltas
    size_t accumulated_size;
    size_t accumulated_capacity;
    char *accumulated_reasoning;     // Accumulated reasoning_content
    size_t reasoning_size;
    size_t reasoning_capacity;
    char *finish_reason;             // Finish reason from final chunk
    char *model;                     // Model name from chunks
    char *message_id;                // Message ID from chunks
    int tool_calls_count;            // Number of tool calls
    cJSON *tool_calls_array;         // Array of accumulated tool calls
    Arena *arena;                    // Arena for all allocations
    int reasoning_line_added;        // Whether we've added a [Reasoning] line to TUI
    int assistant_line_added;        // Whether we've added an [Assistant] line to TUI
} KimiStreamingContext;

static void kimi_streaming_context_init(KimiStreamingContext *ctx, ConversationState *state) {
    memset(ctx, 0, sizeof(KimiStreamingContext));
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

    // Initialize reasoning_content buffer
    ctx->reasoning_capacity = 4096;
    ctx->accumulated_reasoning = arena_alloc(ctx->arena, ctx->reasoning_capacity);
    if (ctx->accumulated_reasoning) {
        ctx->accumulated_reasoning[0] = '\0';
    }

    ctx->tool_calls_array = cJSON_CreateArray();
}

static void kimi_streaming_context_free(KimiStreamingContext *ctx) {
    if (!ctx) return;

    if (ctx->tool_calls_array) {
        cJSON_Delete(ctx->tool_calls_array);
    }

    if (ctx->arena) {
        arena_destroy(ctx->arena);
    }
}

static int kimi_streaming_event_handler(StreamEvent *event, void *userdata) {
    KimiStreamingContext *ctx = (KimiStreamingContext *)userdata;

    // Check for interrupt
    if (ctx->state && ctx->state->interrupt_requested) {
        LOG_DEBUG("Kimi streaming handler: interrupt requested");
        return 1;  // Abort stream
    }

    if (!event || !event->data) {
        if (event && event->type == SSE_EVENT_OPENAI_DONE) {
            LOG_DEBUG("Kimi stream: received [DONE] marker");
        }
        return 0;
    }

    // Handle OpenAI-format chunk
    if (event->type == SSE_EVENT_OPENAI_CHUNK) {
        if (!ctx->arena) {
            LOG_DEBUG("Kimi streaming handler: arena not initialized");
            return 0;
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
                                ctx->assistant_line_added = 1;
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

                    // Handle reasoning_content (Kimi supports thinking models)
                    cJSON *reasoning_content = cJSON_GetObjectItem(delta, "reasoning_content");
                    if (reasoning_content && cJSON_IsString(reasoning_content) && reasoning_content->valuestring) {
                        size_t new_len = strlen(reasoning_content->valuestring);
                        size_t needed = ctx->reasoning_size + new_len + 1;

                        if (needed > ctx->reasoning_capacity) {
                            size_t new_cap = ctx->reasoning_capacity * 2;
                            if (new_cap < needed) new_cap = needed;
                            char *new_buf = arena_alloc(ctx->arena, new_cap);
                            if (new_buf) {
                                if (ctx->accumulated_reasoning && ctx->reasoning_size > 0) {
                                    memcpy(new_buf, ctx->accumulated_reasoning, ctx->reasoning_size);
                                }
                                ctx->accumulated_reasoning = new_buf;
                                ctx->reasoning_capacity = new_cap;
                            }
                        }

                        if (ctx->accumulated_reasoning && needed <= ctx->reasoning_capacity) {
                            // Initialize reasoning line in TUI on first content
                            if (ctx->reasoning_size == 0 && ctx->state && ctx->state->tui) {
                                tui_add_conversation_line(ctx->state->tui, "⟨Reasoning⟩", "", COLOR_PAIR_TOOL_DIM);
                                ctx->reasoning_line_added = 1;
                            }

                            memcpy(ctx->accumulated_reasoning + ctx->reasoning_size,
                                  reasoning_content->valuestring, new_len);
                            ctx->reasoning_size += new_len;
                            ctx->accumulated_reasoning[ctx->reasoning_size] = '\0';

                            // Stream reasoning to TUI if available
                            if (ctx->state && ctx->state->tui) {
                                tui_update_last_conversation_line(ctx->state->tui, reasoning_content->valuestring);
                            }

                            LOG_DEBUG("Accumulated reasoning_content: %zu bytes", ctx->reasoning_size);
                        }
                    }

                    // Handle tool calls
                    cJSON *tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
                    if (tool_calls && cJSON_IsArray(tool_calls)) {
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
                    size_t len = strlen(finish_reason->valuestring) + 1;
                    ctx->finish_reason = arena_alloc(ctx->arena, len);
                    if (ctx->finish_reason) {
                        strlcpy(ctx->finish_reason, finish_reason->valuestring, len);
                        LOG_DEBUG("Kimi stream: finish_reason=%s", ctx->finish_reason);
                    }
                }
            }
        }
    }

    return 0;
}

// ============================================================================
// Kimi Coding Plan Provider Implementation
// ============================================================================

/**
 * Kimi Coding Plan provider's call_api - uses OAuth tokens for authentication
 */
static void kimi_coding_plan_call_api(Provider *self, ConversationState *state, ApiCallResult *out) {
    ApiCallResult result = {0};
    KimiCodingPlanConfig *config = (KimiCodingPlanConfig*)self->config;

    if (!config || !config->oauth_manager) {
        result.error_message = strdup("Kimi config or OAuth manager not initialized");
        result.is_retryable = 0;
        *out = result;
        return;
    }

    // Set up TUI message callback for OAuth operations
    // This ensures browser login prompts appear in the conversation TUI
    kimi_oauth_set_message_callback(config->oauth_manager, kimi_oauth_message_callback, state);

    // Get access token (will refresh if needed)
    const char *access_token = kimi_oauth_get_access_token(config->oauth_manager);
    if (!access_token) {
        // Try to login if not authenticated
        LOG_INFO("Kimi: Not authenticated, attempting login...");
        if (kimi_oauth_login(config->oauth_manager) != 0) {
            result.error_message = strdup("Kimi OAuth login failed. Please try again.");
            result.is_retryable = 0;
            *out = result;
            return;
        }
        access_token = kimi_oauth_get_access_token(config->oauth_manager);
        if (!access_token) {
            result.error_message = strdup("Failed to get access token after login");
            result.is_retryable = 0;
            *out = result;
            return;
        }
    }

    // Check if streaming is enabled
    int enable_streaming = 0;
    const char *streaming_env = getenv("KLAWED_ENABLE_STREAMING");
    if (streaming_env && (strcmp(streaming_env, "1") == 0 || strcasecmp(streaming_env, "true") == 0)) {
        enable_streaming = 1;
    }

    // Build request JSON using OpenAI format with reasoning_content preserved
    // Kimi requires reasoning_content to be included in subsequent requests
    int include_reasoning = 1;  // Preserve reasoning_content (Kimi/Moonshot behavior)
    cJSON *request = build_openai_request_with_reasoning(state, 0, include_reasoning);

    if (!request) {
        result.error_message = strdup("Failed to build request JSON");
        result.is_retryable = 0;
        *out = result;
        return;
    }

    // Override model if specified in config
    if (config->model) {
        cJSON *model_item = cJSON_GetObjectItem(request, "model");
        if (model_item) {
            cJSON_ReplaceItemInObject(request, "model", cJSON_CreateString(config->model));
        } else {
            cJSON_AddStringToObject(request, "model", config->model);
        }
    }

    // Add streaming parameter if enabled
    if (enable_streaming) {
        cJSON_AddBoolToObject(request, "stream", cJSON_True);
        LOG_DEBUG("Kimi provider: streaming enabled");
    }

    char *request_json = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    LOG_DEBUG("Kimi: Request serialized, length: %zu bytes", request_json ? strlen(request_json) : 0);

    if (!request_json) {
        result.error_message = strdup("Failed to serialize request JSON");
        result.is_retryable = 0;
        *out = result;
        return;
    }

    // Build URL
    const char *url = config->api_base ? config->api_base : KIMI_API_ENDPOINT;

    // Set up headers - start with device headers from OAuth manager
    struct curl_slist *headers = kimi_oauth_get_device_headers(config->oauth_manager);
    if (!headers) {
        headers = NULL;  // Will create new list below
    }

    // Add User-Agent header (required - Kimi validates this)
    headers = curl_slist_append(headers, "User-Agent: KimiCLI/" KIMI_VERSION);

    // Add content type
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // Add authorization header
    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", access_token);
    headers = curl_slist_append(headers, auth_header);

    if (!headers) {
        result.error_message = strdup("Failed to setup HTTP headers");
        result.is_retryable = 0;
        result.request_json = request_json;
        *out = result;
        return;
    }

    // Execute HTTP request
    HttpRequest req = {0};
    req.url = url;
    req.method = "POST";
    req.body = request_json;
    req.headers = headers;
    req.connect_timeout_ms = 30000;   // 30 seconds
    req.total_timeout_ms = 300000;    // 5 minutes
    req.enable_streaming = enable_streaming;

    // Store request JSON for logging
    result.request_json = request_json;
    result.headers_json = http_headers_to_json(headers);

    // Initialize streaming context if needed
    KimiStreamingContext stream_ctx = {0};
    HttpResponse *http_resp = NULL;

    if (enable_streaming) {
        kimi_streaming_context_init(&stream_ctx, state);
        http_resp = http_client_execute_stream(&req, kimi_streaming_event_handler, &stream_ctx, progress_callback, state);
    } else {
        http_resp = http_client_execute(&req, progress_callback, state);
    }

    if (!http_resp) {
        result.error_message = strdup("Failed to execute HTTP request");
        result.is_retryable = 0;
        curl_slist_free_all(headers);
        if (enable_streaming) kimi_streaming_context_free(&stream_ctx);
        *out = result;
        return;
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
        if (enable_streaming) kimi_streaming_context_free(&stream_ctx);
        *out = result;
        return;
    }

    // Clean up HTTP response
    char *body_to_free = http_resp->body;
    http_resp->body = NULL;
    http_response_free(http_resp);
    free(body_to_free);
    curl_slist_free_all(headers);

    // Check HTTP status
    if (result.http_status >= 200 && result.http_status < 300) {
        // Success
        cJSON *raw_json = NULL;

        if (enable_streaming) {
            LOG_DEBUG("Reconstructing Kimi response from streaming context");

            // Build synthetic response in OpenAI format
            raw_json = cJSON_CreateObject();
            cJSON_AddStringToObject(raw_json, "id", stream_ctx.message_id ? stream_ctx.message_id : "streaming");
            cJSON_AddStringToObject(raw_json, "object", "chat.completion");
            cJSON_AddStringToObject(raw_json, "model", stream_ctx.model ? stream_ctx.model : config->model);
            time_t now = time(NULL);
            cJSON_AddNumberToObject(raw_json, "created", (double)now);

            cJSON *choices = cJSON_CreateArray();
            cJSON *choice = cJSON_CreateObject();
            cJSON_AddNumberToObject(choice, "index", 0);

            cJSON *message = cJSON_CreateObject();
            cJSON_AddStringToObject(message, "role", "assistant");

            if (stream_ctx.accumulated_text && stream_ctx.accumulated_size > 0) {
                cJSON_AddStringToObject(message, "content", stream_ctx.accumulated_text);
            } else {
                cJSON_AddNullToObject(message, "content");
            }

            // Add reasoning_content if we have any
            if (stream_ctx.accumulated_reasoning && stream_ctx.reasoning_size > 0) {
                cJSON_AddStringToObject(message, "reasoning_content", stream_ctx.accumulated_reasoning);
                LOG_DEBUG("Added reasoning_content to response (%zu bytes)", stream_ctx.reasoning_size);
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

            // Add usage placeholder
            cJSON *usage = cJSON_CreateObject();
            cJSON_AddNumberToObject(usage, "prompt_tokens", 0);
            cJSON_AddNumberToObject(usage, "completion_tokens", 0);
            cJSON_AddNumberToObject(usage, "total_tokens", 0);
            cJSON_AddItemToObject(raw_json, "usage", usage);

            kimi_streaming_context_free(&stream_ctx);
        } else {
            // Non-streaming: parse response
            raw_json = cJSON_Parse(result.raw_response);
            if (!raw_json) {
                result.error_message = strdup("Failed to parse JSON response");
                result.is_retryable = 1;
                free(result.headers_json);
                result.headers_json = NULL;
                *out = result;
                return;
            }
        }

        // Create arena for ApiResponse
        Arena *arena = arena_create(16384);
        if (!arena) {
            result.error_message = strdup("Failed to create arena for ApiResponse");
            result.is_retryable = 0;
            cJSON_Delete(raw_json);
            free(result.headers_json);
            result.headers_json = NULL;
            *out = result;
            return;
        }

        // Allocate ApiResponse from arena
        ApiResponse *api_response = arena_alloc(arena, sizeof(ApiResponse));
        if (!api_response) {
            result.error_message = strdup("Failed to allocate ApiResponse from arena");
            result.is_retryable = 0;
            arena_destroy(arena);
            cJSON_Delete(raw_json);
            free(result.headers_json);
            result.headers_json = NULL;
            *out = result;
            return;
        }

        // Initialize ApiResponse
        memset(api_response, 0, sizeof(ApiResponse));
        api_response->arena = arena;
        api_response->error_message = NULL;
        api_response->raw_response = raw_json;

        // Parse Chat Completions format
        cJSON *choices = cJSON_GetObjectItem(raw_json, "choices");
        if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
            result.error_message = strdup("Invalid response format: no choices");
            result.is_retryable = 0;
            api_response_free(api_response);
            free(result.headers_json);
            result.headers_json = NULL;
            *out = result;
            return;
        }

        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (!message) {
            result.error_message = strdup("Invalid response format: no message");
            result.is_retryable = 0;
            api_response_free(api_response);
            free(result.headers_json);
            result.headers_json = NULL;
            *out = result;
            return;
        }

        // Extract text content
        cJSON *content = cJSON_GetObjectItem(message, "content");
        if (content && cJSON_IsString(content) && content->valuestring) {
            api_response->message.text = arena_strdup(api_response->arena, content->valuestring);
            if (api_response->message.text) {
                trim_whitespace(api_response->message.text);
            }
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
                    api_response_free(api_response);
                    free(result.headers_json);
                    result.headers_json = NULL;
                    *out = result;
                    return;
                }

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

                    api_response->tools[tool_idx].id =
                        (id && cJSON_IsString(id)) ? arena_strdup(api_response->arena, id->valuestring) : NULL;
                    api_response->tools[tool_idx].name =
                        (name && cJSON_IsString(name)) ? arena_strdup(api_response->arena, name->valuestring) : NULL;

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

        result.response = api_response;
        *out = result;
        return;
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

            if (message && cJSON_IsString(message)) {
                const char *msg_text = message->valuestring;
                const char *type_text = (error_type && cJSON_IsString(error_type)) ? error_type->valuestring : "";

                // Check for context length overflow errors
                if (is_context_length_error(msg_text, type_text)) {
                    result.error_message = get_context_length_error_message();
                    result.is_retryable = 0;
                } else {
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

    // Check for token expiration (401 Unauthorized)
    if (result.http_status == 401) {
        LOG_INFO("Kimi: Token may have expired, attempting recovery...");

        // First, reload from disk - another process (subagent) may have refreshed
        int reloaded = kimi_oauth_reload_from_disk(config->oauth_manager);
        if (reloaded) {
            LOG_INFO("Kimi: Reloaded updated token from disk (refreshed by another process)");
            result.is_retryable = 1;  // Allow retry with new token from disk
        } else {
            // No newer token on disk, try to refresh ourselves
            LOG_INFO("Kimi: No newer token on disk, attempting refresh...");
            if (kimi_oauth_refresh(config->oauth_manager, 1) == 0) {
                LOG_INFO("Kimi: Token refreshed successfully");
                result.is_retryable = 1;  // Allow retry with new token
            } else {
                // Refresh failed - try one more disk reload in case refresh raced
                reloaded = kimi_oauth_reload_from_disk(config->oauth_manager);
                if (reloaded) {
                    LOG_INFO("Kimi: Found refreshed token on disk after failed refresh");
                    result.is_retryable = 1;
                } else {
                    // Refresh failed - clear invalid token to force re-authentication
                    LOG_ERROR("Kimi: Token refresh failed, clearing credentials for re-authentication");
                    kimi_oauth_logout(config->oauth_manager);
                    // Replace error message to guide user
                    free(result.error_message);
                    result.error_message = strdup("Kimi OAuth token expired or revoked. Please run again to re-authenticate.");
                    result.is_retryable = 0;  // Don't retry automatically
                }
            }
        }
    }

    free(result.headers_json);
    result.headers_json = NULL;
    if (enable_streaming) kimi_streaming_context_free(&stream_ctx);
    *out = result;
}

/**
 * Cleanup Kimi Coding Plan provider resources
 */
static void kimi_coding_plan_cleanup(Provider *self) {
    if (!self) return;

    LOG_DEBUG("Kimi Coding Plan provider: cleaning up resources");

    if (self->config) {
        KimiCodingPlanConfig *config = (KimiCodingPlanConfig*)self->config;

        // Stop refresh thread and destroy OAuth manager
        if (config->oauth_manager) {
            kimi_oauth_stop_refresh_thread(config->oauth_manager);
            kimi_oauth_manager_destroy(config->oauth_manager);
        }

        free(config->api_base);
        free(config->model);
        free(config);
    }

    free(self);
    LOG_DEBUG("Kimi Coding Plan provider: cleanup complete");
}

// ============================================================================
// Public API
// ============================================================================

Provider* kimi_coding_plan_provider_create(const char *model) {
    LOG_DEBUG("Creating Kimi Coding Plan provider...");

    // Allocate provider structure
    Provider *provider = calloc(1, sizeof(Provider));
    if (!provider) {
        LOG_ERROR("Kimi Coding Plan provider: failed to allocate provider");
        return NULL;
    }

    // Allocate config structure
    KimiCodingPlanConfig *config = calloc(1, sizeof(KimiCodingPlanConfig));
    if (!config) {
        LOG_ERROR("Kimi Coding Plan provider: failed to allocate config");
        free(provider);
        return NULL;
    }

    // Create OAuth manager
    config->oauth_manager = kimi_oauth_manager_create();
    if (!config->oauth_manager) {
        LOG_ERROR("Kimi Coding Plan provider: failed to create OAuth manager");
        free(config);
        free(provider);
        return NULL;
    }

    // Check if authenticated, if not try to login
    if (!kimi_oauth_is_authenticated(config->oauth_manager)) {
        LOG_INFO("Kimi Coding Plan: Not authenticated, starting device authorization...");
        if (kimi_oauth_login(config->oauth_manager) != 0) {
            LOG_ERROR("Kimi Coding Plan provider: OAuth login failed");
            kimi_oauth_manager_destroy(config->oauth_manager);
            free(config);
            free(provider);
            return NULL;
        }
    }

    // Start background refresh thread
    if (kimi_oauth_start_refresh_thread(config->oauth_manager) != 0) {
        LOG_WARN("Kimi Coding Plan provider: failed to start refresh thread (will refresh manually)");
        // Continue anyway - token refresh will happen manually when needed
    }

    // Set API base URL
    config->api_base = strdup(KIMI_API_ENDPOINT);
    if (!config->api_base) {
        LOG_ERROR("Kimi Coding Plan provider: failed to set API base URL");
        kimi_oauth_stop_refresh_thread(config->oauth_manager);
        kimi_oauth_manager_destroy(config->oauth_manager);
        free(config);
        free(provider);
        return NULL;
    }

    // Set model (use default if not specified)
    if (model && model[0] != '\0') {
        config->model = strdup(model);
    } else {
        config->model = strdup(KIMI_DEFAULT_MODEL);
    }
    if (!config->model) {
        LOG_ERROR("Kimi Coding Plan provider: failed to set model");
        free(config->api_base);
        kimi_oauth_stop_refresh_thread(config->oauth_manager);
        kimi_oauth_manager_destroy(config->oauth_manager);
        free(config);
        free(provider);
        return NULL;
    }

    // Set up provider interface
    provider->name = "Kimi Coding Plan";
    provider->config = config;
    provider->call_api = kimi_coding_plan_call_api;
    provider->cleanup = kimi_coding_plan_cleanup;

    LOG_INFO("Kimi Coding Plan provider created successfully (model: %s, endpoint: %s)",
             config->model, config->api_base);
    return provider;
}
