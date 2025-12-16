/*
 * bedrock_provider.c - AWS Bedrock API provider implementation
 */

#define _POSIX_C_SOURCE 200809L

#include "klawed_internal.h"  // Must be first to get ApiResponse definition
#include "bedrock_provider.h"
#include "logger.h"
#include "http_client.h"
#include "tui.h"  // For streaming TUI updates

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // for strcasecmp
#include <time.h>
#include <unistd.h>  // for usleep
#include <curl/curl.h>

// ============================================================================
// CURL Helpers
// ============================================================================

// Progress callback placeholder (Ctrl+C handled by TUI)
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

// Convert curl_slist headers to JSON string for logging


// ============================================================================
// Request Building (from ConversationState)
// ============================================================================

// Forward declaration - this will be implemented in klawed.c and exposed via klawed_internal.h
// For now, we declare it here as extern


// ============================================================================
// Streaming Support
// ============================================================================

// Bedrock streaming context passed to SSE callback
typedef struct {
    ConversationState *state;        // For interrupt checking
    char *accumulated_text;          // Accumulated text from deltas
    size_t accumulated_size;
    size_t accumulated_capacity;
    int content_block_index;         // Current content block being streamed
    char *content_block_type;        // Type of current block ("text" or "tool_use")
    char *tool_use_id;               // Tool use ID for current block
    char *tool_use_name;             // Tool name for current block
    char *tool_input_json;           // Accumulated tool input JSON
    size_t tool_input_size;
    size_t tool_input_capacity;
    cJSON *message_start_data;       // Message metadata from message_start
    char *stop_reason;               // Stop reason from message_delta
} BedrockStreamingContext;

static void bedrock_streaming_context_init(BedrockStreamingContext *ctx, ConversationState *state) {
    memset(ctx, 0, sizeof(BedrockStreamingContext));
    ctx->state = state;
    ctx->content_block_index = -1;
    ctx->accumulated_capacity = 4096;
    ctx->accumulated_text = malloc(ctx->accumulated_capacity);
    if (ctx->accumulated_text) {
        ctx->accumulated_text[0] = '\0';
    }
    ctx->tool_input_capacity = 4096;
    ctx->tool_input_json = malloc(ctx->tool_input_capacity);
    if (ctx->tool_input_json) {
        ctx->tool_input_json[0] = '\0';
    }
}

static void bedrock_streaming_context_free(BedrockStreamingContext *ctx) {
    if (!ctx) return;
    free(ctx->accumulated_text);
    free(ctx->content_block_type);
    free(ctx->tool_use_id);
    free(ctx->tool_use_name);
    free(ctx->tool_input_json);
    free(ctx->stop_reason);
    if (ctx->message_start_data) {
        cJSON_Delete(ctx->message_start_data);
    }
}

static int bedrock_streaming_event_handler(StreamEvent *event, void *userdata) {
    BedrockStreamingContext *ctx = (BedrockStreamingContext *)userdata;

    // Check for interrupt
    if (ctx->state && ctx->state->interrupt_requested) {
        LOG_DEBUG("Bedrock streaming handler: interrupt requested");
        return 1;  // Abort stream
    }

    if (!event || !event->data) {
        // Ping or invalid event
        return 0;
    }

    // Bedrock uses the same Anthropic Messages API streaming format
    // So we can reuse the same event handling logic
    switch (event->type) {
        case SSE_EVENT_MESSAGE_START:
            // Store message metadata
            if (ctx->message_start_data) {
                cJSON_Delete(ctx->message_start_data);
            }
            ctx->message_start_data = cJSON_Duplicate(event->data, 1);
            LOG_DEBUG("Bedrock stream: message_start");

            // Initialize TUI for streaming by adding an empty assistant line
            if (ctx->state && ctx->state->tui) {
                tui_add_conversation_line(ctx->state->tui, "[Assistant]", "", COLOR_PAIR_ASSISTANT);
            }
            break;

        case SSE_EVENT_CONTENT_BLOCK_START: {
            // New content block starting
            cJSON *index = cJSON_GetObjectItem(event->data, "index");
            cJSON *content_block = cJSON_GetObjectItem(event->data, "content_block");
            if (index && cJSON_IsNumber(index)) {
                ctx->content_block_index = index->valueint;
            }
            if (content_block) {
                cJSON *type = cJSON_GetObjectItem(content_block, "type");
                if (type && cJSON_IsString(type)) {
                    free(ctx->content_block_type);
                    ctx->content_block_type = strdup(type->valuestring);

                    if (strcmp(type->valuestring, "tool_use") == 0) {
                        cJSON *id = cJSON_GetObjectItem(content_block, "id");
                        cJSON *name = cJSON_GetObjectItem(content_block, "name");
                        if (id && cJSON_IsString(id)) {
                            free(ctx->tool_use_id);
                            ctx->tool_use_id = strdup(id->valuestring);
                        }
                        if (name && cJSON_IsString(name)) {
                            free(ctx->tool_use_name);
                            ctx->tool_use_name = strdup(name->valuestring);
                        }
                        ctx->tool_input_size = 0;
                        if (ctx->tool_input_json) {
                            ctx->tool_input_json[0] = '\0';
                        }
                    }
                }
            }
            LOG_DEBUG("Bedrock stream: content_block_start (index=%d, type=%s)",
                     ctx->content_block_index,
                     ctx->content_block_type ? ctx->content_block_type : "unknown");
            break;
        }

        case SSE_EVENT_CONTENT_BLOCK_DELTA: {
            // Delta with new content
            cJSON *delta = cJSON_GetObjectItem(event->data, "delta");
            if (delta) {
                cJSON *type = cJSON_GetObjectItem(delta, "type");
                if (type && cJSON_IsString(type)) {
                    if (strcmp(type->valuestring, "text_delta") == 0) {
                        cJSON *text = cJSON_GetObjectItem(delta, "text");
                        if (text && cJSON_IsString(text) && text->valuestring) {
                            size_t new_len = strlen(text->valuestring);
                            size_t needed = ctx->accumulated_size + new_len + 1;

                            if (needed > ctx->accumulated_capacity) {
                                size_t new_cap = ctx->accumulated_capacity * 2;
                                if (new_cap < needed) new_cap = needed;
                                char *new_buf = realloc(ctx->accumulated_text, new_cap);
                                if (new_buf) {
                                    ctx->accumulated_text = new_buf;
                                    ctx->accumulated_capacity = new_cap;
                                }
                            }

                            if (ctx->accumulated_text && needed <= ctx->accumulated_capacity) {
                                memcpy(ctx->accumulated_text + ctx->accumulated_size,
                                      text->valuestring, new_len);
                                ctx->accumulated_size += new_len;
                                ctx->accumulated_text[ctx->accumulated_size] = '\0';

                                // Update TUI with new text delta
                                if (ctx->state && ctx->state->tui) {
                                    tui_update_last_conversation_line(ctx->state->tui, text->valuestring);
                                }

                                LOG_DEBUG("Bedrock stream delta: %s", text->valuestring);
                            }
                        }
                    } else if (strcmp(type->valuestring, "input_json_delta") == 0) {
                        // Tool input JSON delta
                        cJSON *partial_json = cJSON_GetObjectItem(delta, "partial_json");
                        if (partial_json && cJSON_IsString(partial_json) && partial_json->valuestring) {
                            size_t new_len = strlen(partial_json->valuestring);
                            size_t needed = ctx->tool_input_size + new_len + 1;

                            if (needed > ctx->tool_input_capacity) {
                                size_t new_cap = ctx->tool_input_capacity * 2;
                                if (new_cap < needed) new_cap = needed;
                                char *new_buf = realloc(ctx->tool_input_json, new_cap);
                                if (new_buf) {
                                    ctx->tool_input_json = new_buf;
                                    ctx->tool_input_capacity = new_cap;
                                }
                            }

                            if (ctx->tool_input_json && needed <= ctx->tool_input_capacity) {
                                memcpy(ctx->tool_input_json + ctx->tool_input_size,
                                      partial_json->valuestring, new_len);
                                ctx->tool_input_size += new_len;
                                ctx->tool_input_json[ctx->tool_input_size] = '\0';
                                LOG_DEBUG("Bedrock stream: tool input delta");
                            }
                        }
                    }
                }
            }
            break;
        }

        case SSE_EVENT_CONTENT_BLOCK_STOP:
            LOG_DEBUG("Bedrock stream: content_block_stop");
            break;

        case SSE_EVENT_MESSAGE_DELTA: {
            // Message metadata update (stop_reason, usage, etc.)
            cJSON *delta = cJSON_GetObjectItem(event->data, "delta");
            if (delta) {
                cJSON *stop_reason = cJSON_GetObjectItem(delta, "stop_reason");
                if (stop_reason && cJSON_IsString(stop_reason)) {
                    free(ctx->stop_reason);
                    ctx->stop_reason = strdup(stop_reason->valuestring);
                    LOG_DEBUG("Bedrock stream: stop_reason=%s", ctx->stop_reason);
                }
            }
            break;
        }

        case SSE_EVENT_MESSAGE_STOP:
            LOG_DEBUG("Bedrock stream: message_stop");
            break;

        case SSE_EVENT_ERROR: {
            cJSON *error = cJSON_GetObjectItem(event->data, "error");
            if (error) {
                cJSON *message = cJSON_GetObjectItem(error, "message");
                if (message && cJSON_IsString(message)) {
                    LOG_ERROR("Bedrock stream error: %s", message->valuestring);
                }
            }
            return 1;  // Abort on error
        }

        case SSE_EVENT_PING:
            // Keepalive ping
            break;

        case SSE_EVENT_OPENAI_CHUNK:
        case SSE_EVENT_OPENAI_DONE:
            // These are OpenAI-specific events, not used in Bedrock
            LOG_WARN("Bedrock stream: unexpected OpenAI event type %d", event->type);
            break;

        default:
            LOG_DEBUG("Bedrock stream: unknown event type %d", event->type);
            break;
    }

    return 0;  // Continue streaming
}

// ============================================================================
// Bedrock Provider Implementation
// ============================================================================

/**
 * Helper: Execute a single HTTP request with current credentials
 * Returns: ApiCallResult (caller must free fields)
 */
static ApiCallResult bedrock_execute_request(BedrockConfig *config, const char *bedrock_json, ConversationState *state, int enable_streaming) {
    ApiCallResult result = {0};

    // Use streaming endpoint if streaming is enabled
    char *streaming_endpoint = NULL;
    const char *endpoint_url = config->endpoint;

    if (enable_streaming) {
        streaming_endpoint = bedrock_build_streaming_endpoint(config->region, config->model_id);
        if (!streaming_endpoint) {
            result.error_message = strdup("Failed to build streaming endpoint");
            result.is_retryable = 0;
            return result;
        }
        endpoint_url = streaming_endpoint;
    }

    // Sign request with SigV4 using current credentials
    struct curl_slist *headers = bedrock_sign_request(
        NULL, "POST", endpoint_url, bedrock_json,
        config->creds, config->region, AWS_BEDROCK_SERVICE
    );

    if (!headers) {
        result.error_message = strdup("Failed to sign request with AWS SigV4");
        result.is_retryable = 0;
        result.headers_json = NULL;  // No headers to log
        free(streaming_endpoint);
        return result;
    }

    // Build HTTP request using the new HTTP client
    HttpRequest req = {0};
    req.url = endpoint_url;
    req.method = "POST";
    req.body = bedrock_json;
    req.headers = headers;
    req.connect_timeout_ms = 30000;  // 30 seconds
    req.total_timeout_ms = 300000;   // 5 minutes
    req.follow_redirects = 0;
    req.verbose = 0;
    req.enable_streaming = enable_streaming;

    // Initialize streaming context if needed
    BedrockStreamingContext stream_ctx = {0};
    HttpResponse *http_resp = NULL;

    if (enable_streaming) {
        bedrock_streaming_context_init(&stream_ctx, state);
        http_resp = http_client_execute_stream(&req, bedrock_streaming_event_handler, &stream_ctx, progress_callback, state);
    } else {
        http_resp = http_client_execute(&req, progress_callback, state);
    }

    // Convert headers to JSON for logging before freeing them
    char *headers_json = http_headers_to_json(headers);
    result.headers_json = headers_json;  // Store for logging (caller must free)

    // Free the headers list (http_client_execute makes its own copy)
    curl_slist_free_all(headers);
    free(streaming_endpoint);

    if (!http_resp) {
        result.error_message = strdup("Failed to execute HTTP request (memory allocation failed)");
        result.is_retryable = 0;
        free(result.headers_json);  // Clean up headers JSON
        result.headers_json = NULL;
        if (enable_streaming) bedrock_streaming_context_free(&stream_ctx);
        return result;
    }

    result.duration_ms = http_resp->duration_ms;
    result.http_status = http_resp->status_code;

    // Handle HTTP errors
    if (http_resp->error_message) {
        result.error_message = strdup(http_resp->error_message);
        result.is_retryable = http_resp->is_retryable;
        http_response_free(http_resp);
        if (enable_streaming) bedrock_streaming_context_free(&stream_ctx);
        return result;
    }

    result.raw_response = http_resp->body ? strdup(http_resp->body) : NULL;

    // Clean up HTTP response (but keep body since we duplicated it)
    char *body_to_free = http_resp->body;
    http_resp->body = NULL;  // Prevent double free
    http_response_free(http_resp);
    free(body_to_free);

    // Check HTTP status
    if (result.http_status >= 200 && result.http_status < 300) {
        // Success
        cJSON *openai_json = NULL;

        // If streaming was used, reconstruct response from streaming context
        if (enable_streaming) {
            LOG_DEBUG("Reconstructing Bedrock response from streaming context");

            // Build synthetic response in Anthropic format, then convert to OpenAI
            cJSON *anth_response = cJSON_CreateObject();
            cJSON_AddStringToObject(anth_response, "id", "streaming");
            cJSON_AddStringToObject(anth_response, "type", "message");
            cJSON_AddStringToObject(anth_response, "role", "assistant");

            cJSON *content_array = cJSON_CreateArray();

            // Add text content if we have any
            if (stream_ctx.accumulated_text && stream_ctx.accumulated_size > 0) {
                cJSON *text_block = cJSON_CreateObject();
                cJSON_AddStringToObject(text_block, "type", "text");
                cJSON_AddStringToObject(text_block, "text", stream_ctx.accumulated_text);
                cJSON_AddItemToArray(content_array, text_block);
            }

            // Add tool use if we have any
            if (stream_ctx.tool_use_id && stream_ctx.tool_use_name) {
                cJSON *tool_block = cJSON_CreateObject();
                cJSON_AddStringToObject(tool_block, "type", "tool_use");
                cJSON_AddStringToObject(tool_block, "id", stream_ctx.tool_use_id);
                cJSON_AddStringToObject(tool_block, "name", stream_ctx.tool_use_name);

                // Parse tool input JSON
                cJSON *input = NULL;
                if (stream_ctx.tool_input_json && stream_ctx.tool_input_size > 0) {
                    input = cJSON_Parse(stream_ctx.tool_input_json);
                }
                if (!input) {
                    input = cJSON_CreateObject();
                }
                cJSON_AddItemToObject(tool_block, "input", input);
                cJSON_AddItemToArray(content_array, tool_block);
            }

            cJSON_AddItemToObject(anth_response, "content", content_array);
            cJSON_AddStringToObject(anth_response, "stop_reason",
                stream_ctx.stop_reason ? stream_ctx.stop_reason : "end_turn");

            // Convert Anthropic format to OpenAI format
            char *anth_str = cJSON_PrintUnformatted(anth_response);
            cJSON_Delete(anth_response);

            if (anth_str) {
                openai_json = bedrock_convert_response(anth_str);
                free(anth_str);
            }

            // Free streaming context
            bedrock_streaming_context_free(&stream_ctx);
        } else {
            // Non-streaming: convert Bedrock response to OpenAI format
            openai_json = bedrock_convert_response(result.raw_response);
        }

        if (!openai_json) {
            result.error_message = strdup("Failed to parse Bedrock response");
            result.is_retryable = 0;
            char *tmp_headers = result.headers_json;
            result.headers_json = NULL;
            free(tmp_headers);  // Clean up headers JSON in error paths
            if (enable_streaming) bedrock_streaming_context_free(&stream_ctx);
            return result;
        }

        // Now extract vendor-agnostic response data (same as OpenAI provider)
        ApiResponse *api_response = calloc(1, sizeof(ApiResponse));
        if (!api_response) {
            result.error_message = strdup("Failed to allocate ApiResponse");
            result.is_retryable = 0;
            cJSON_Delete(openai_json);
            char *tmp_headers = result.headers_json;
            result.headers_json = NULL;
            free(tmp_headers);  // Clean up headers JSON in error paths
            return result;
        }

        // Keep raw response for history
        api_response->raw_response = openai_json;

        // Extract message from OpenAI response format
        cJSON *choices = cJSON_GetObjectItem(openai_json, "choices");
        if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
            result.error_message = strdup("Invalid response format: no choices");
            result.is_retryable = 0;
            api_response_free(api_response);
            char *tmp_headers = result.headers_json;
            result.headers_json = NULL;
            free(tmp_headers);  // Clean up headers JSON in error paths
            return result;
        }

        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (!message) {
            result.error_message = strdup("Invalid response format: no message");
            result.is_retryable = 0;
            api_response_free(api_response);
            char *tmp_headers = result.headers_json;
            result.headers_json = NULL;
            free(tmp_headers);  // Clean up headers JSON in error paths
            return result;
        }

        // Extract text content
        cJSON *content = cJSON_GetObjectItem(message, "content");
        if (content && cJSON_IsString(content) && content->valuestring) {
            api_response->message.text = strdup(content->valuestring);
        } else {
            api_response->message.text = NULL;
        }

        // Extract tool calls (same as OpenAI provider)
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
                api_response->tools = calloc((size_t)valid_count, sizeof(ToolCall));
                if (!api_response->tools) {
                    result.error_message = strdup("Failed to allocate tool calls");
                    result.is_retryable = 0;
                    api_response_free(api_response);
                    char *tmp_headers = result.headers_json;
                    result.headers_json = NULL;
                    free(tmp_headers);  // Clean up headers JSON in error paths
                    return result;
                }

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

                    // Copy tool call data
                    api_response->tools[tool_idx].id =
                        (id && cJSON_IsString(id)) ? strdup(id->valuestring) : NULL;
                    api_response->tools[tool_idx].name =
                        (name && cJSON_IsString(name)) ? strdup(name->valuestring) : NULL;

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

        result.response = api_response;
        return result;
    }

    // HTTP error
    result.is_retryable = (result.http_status == 429 ||
                           result.http_status == 408 ||
                           result.http_status >= 500);

    // Extract error message from response if JSON
    cJSON *error_json = cJSON_Parse(result.raw_response);
    if (error_json) {
        cJSON *message = cJSON_GetObjectItem(error_json, "message");
        if (message && cJSON_IsString(message)) {
            const char *msg_text = message->valuestring;

            // Check for context length overflow errors (after OpenAI conversion)
            if ((strstr(msg_text, "maximum context length") != NULL) ||
                (strstr(msg_text, "context length") != NULL && strstr(msg_text, "tokens") != NULL) ||
                (strstr(msg_text, "too many tokens") != NULL)) {

                // Provide user-friendly context length error message
                result.error_message = strdup(
                    "Context length exceeded. The conversation has grown too large for the model's memory. "
                    "Try starting a new conversation or reduce the amount of code/files being discussed."
                );
                result.is_retryable = 0;  // Context length errors are not retryable
            } else {
                // Use the original error message for other types of errors
                result.error_message = strdup(msg_text);
            }
        }
        cJSON_Delete(error_json);
    }

    if (!result.error_message) {
        char buf[256];
        snprintf(buf, sizeof(buf), "HTTP %ld", result.http_status);
        result.error_message = strdup(buf);
    }

    char *tmp_headers = result.headers_json;
    result.headers_json = NULL;
    free(tmp_headers);  // Clean up headers JSON in error paths
    return result;
}

/**
 * Bedrock provider's call_api - handles AWS authentication with smart rotation detection
 */
static ApiCallResult bedrock_call_api(Provider *self, ConversationState *state) {
    ApiCallResult result = {0};
    BedrockConfig *config = (BedrockConfig*)self->config;

    if (!config) {
        result.error_message = strdup("Bedrock config not initialized");
        result.is_retryable = 0;
        return result;
    }

    const char *profile = getenv(ENV_AWS_PROFILE);
    if (!profile) profile = "default";

    // === STEP 0: If no credentials, authenticate first ===
    if (!config->creds) {
        LOG_INFO("No credentials available on startup, authenticating...");
        printf("\nAWS credentials not found. Starting authentication...\n");

        if (bedrock_authenticate(profile) == 0) {
            LOG_INFO("Authentication successful, loading credentials...");

            // Poll for credentials after authentication
            AWSCredentials *new_creds = NULL;
            int max_attempts = 10;
            int attempt = 0;

            for (attempt = 0; attempt < max_attempts; attempt++) {
                if (attempt > 0) {
                    usleep(200000);  // 200ms between attempts
                }

                LOG_DEBUG("Polling for credentials after auth (attempt %d/%d)...", attempt + 1, max_attempts);
                AWSCredentials *polled_creds = bedrock_load_credentials(profile, config->region);

                if (polled_creds && polled_creds->access_key_id) {
                    LOG_INFO("✓ Credentials loaded successfully (attempt %d)", attempt + 1);
                    new_creds = polled_creds;
                    break;
                } else {
                    LOG_DEBUG("✗ No credentials yet (attempt %d)", attempt + 1);
                    if (polled_creds) bedrock_creds_free(polled_creds);
                }
            }

            if (new_creds) {
                config->creds = new_creds;
                LOG_INFO("Credentials loaded, proceeding with API call");
            } else {
                result.error_message = strdup("Failed to load credentials after authentication");
                result.is_retryable = 0;
                return result;
            }
        } else {
            result.error_message = strdup("Authentication failed");
            result.is_retryable = 0;
            return result;
        }
    }

    // === STEP 1: Save initial token state (for external rotation detection) ===
    char *saved_access_key = NULL;
    if (config->creds && config->creds->access_key_id) {
        saved_access_key = strdup(config->creds->access_key_id);
        LOG_DEBUG("Saved current access key ID for rotation detection: %.10s...", saved_access_key);
    }

    // === Build request (do this once, reuse for retries) ===
    char *openai_json = build_request_json_from_state(state);
    if (!openai_json) {
        result.error_message = strdup("Failed to build request JSON");
        result.is_retryable = 0;
        free(saved_access_key);
        return result;
    }

    LOG_DEBUG("Bedrock: Built OpenAI-format request, converting to Bedrock format");

    char *bedrock_json = bedrock_convert_request(openai_json);
    free(openai_json);
    
    LOG_DEBUG("Bedrock: Converted to Bedrock format, request length: %zu bytes",
              bedrock_json ? strlen(bedrock_json) : 0);

    if (!bedrock_json) {
        result.error_message = strdup("Failed to convert request to Bedrock format");
        result.is_retryable = 0;
        free(saved_access_key);
        return result;
    }

    // Update profile from config if available
    if (config->creds && config->creds->profile) {
        profile = config->creds->profile;
    }

    // Check if streaming is enabled via environment variable
    int enable_streaming = 0;
    const char *streaming_env = getenv("KLAWED_ENABLE_STREAMING");
    if (streaming_env && (strcmp(streaming_env, "1") == 0 || strcasecmp(streaming_env, "true") == 0)) {
        enable_streaming = 1;
        LOG_DEBUG("Bedrock provider: streaming enabled");
    }

    // === STEP 2: First API call attempt ===
    LOG_DEBUG("Executing first API call attempt...");
    result = bedrock_execute_request(config, bedrock_json, state, enable_streaming);

    // Success on first try
    if (result.response) {
        LOG_INFO("API call succeeded on first attempt");
        free(saved_access_key);
        result.request_json = bedrock_json;  // Store for logging (caller frees)
        return result;
    }

    // === STEP 3: Auth error? Check for external credential rotation ===
    if (result.http_status == 401 || result.http_status == 403 || result.http_status == 400) {
        LOG_WARN("Authentication error (HTTP %ld): %s", result.http_status, result.error_message);
        LOG_DEBUG("=== CHECKING FOR EXTERNAL CREDENTIAL ROTATION ===");

        // Try loading fresh credentials from profile
        AWSCredentials *fresh_creds = bedrock_load_credentials(profile, config->region);

        if (fresh_creds) {
            LOG_DEBUG("Loaded fresh credentials from profile");

            // === STEP 4: Compare tokens - was it rotated externally? ===
            int externally_rotated = 0;
            if (saved_access_key && fresh_creds->access_key_id) {
                externally_rotated = (strcmp(saved_access_key, fresh_creds->access_key_id) != 0);
                LOG_DEBUG("Token comparison: saved=%.10s, fresh=%.10s, rotated=%s",
                         saved_access_key, fresh_creds->access_key_id,
                         externally_rotated ? "YES" : "NO");
            }

            if (externally_rotated) {
                // === STEP 4a: External rotation detected - use new credentials ===
                LOG_INFO("✓ Detected externally rotated credentials (another process updated tokens)");
                printf("\nDetected new AWS credentials from external source. Using updated credentials...\n");

                // Update config with externally rotated credentials
                bedrock_creds_free(config->creds);
                config->creds = fresh_creds;
                free(saved_access_key);
                saved_access_key = strdup(fresh_creds->access_key_id);
                result.auth_refreshed = 1;

                // Free previous error result
                free(result.raw_response);
                free(result.error_message);

                // === STEP 5: Retry with externally rotated credentials ===
                LOG_DEBUG("Retrying API call with externally rotated credentials...");
                result = bedrock_execute_request(config, bedrock_json, state, enable_streaming);

                if (result.response) {
                    LOG_INFO("API call succeeded after using externally rotated credentials");
                    free(saved_access_key);
                    result.request_json = bedrock_json;  // Store for logging (caller frees)
                    return result;
                }

                LOG_WARN("API call still failed after external rotation: %s", result.error_message);
            } else {
                // === STEP 4b: No external rotation - force auth token rotation ===
                LOG_INFO("✗ Credentials unchanged, forcing authentication token rotation...");
                bedrock_creds_free(fresh_creds);

                // Call rotation command (aws sso login)
                LOG_DEBUG("Calling bedrock_authenticate to rotate credentials...");
                if (bedrock_authenticate(profile) == 0) {
                    LOG_INFO("Authentication successful, waiting for credential cache to update...");

                    // Poll for new credentials (AWS SSO writes credentials asynchronously)
                    // Try up to 10 times with 200ms intervals (max 2 seconds total)
                    AWSCredentials *new_creds = NULL;
                    int max_attempts = 10;
                    int attempt = 0;

                    for (attempt = 0; attempt < max_attempts; attempt++) {
                        if (attempt > 0) {
                            usleep(200000);  // 200ms between attempts
                        }

                        LOG_DEBUG("Polling for updated credentials (attempt %d/%d)...", attempt + 1, max_attempts);
                        AWSCredentials *polled_creds = bedrock_load_credentials(profile, config->region);

                        if (polled_creds && polled_creds->access_key_id) {
                            // Check if credentials have changed
                            if (saved_access_key && strcmp(saved_access_key, polled_creds->access_key_id) != 0) {
                                LOG_INFO("✓ Detected new credentials after rotation (attempt %d)", attempt + 1);
                                LOG_DEBUG("Old key: %.10s..., New key: %.10s...",
                                         saved_access_key, polled_creds->access_key_id);
                                new_creds = polled_creds;
                                break;
                            } else {
                                LOG_DEBUG("✗ Credentials unchanged (attempt %d)", attempt + 1);
                                bedrock_creds_free(polled_creds);
                            }
                        } else {
                            LOG_DEBUG("✗ Failed to load credentials (attempt %d)", attempt + 1);
                            if (polled_creds) bedrock_creds_free(polled_creds);
                        }
                    }

                    if (new_creds) {
                        bedrock_creds_free(config->creds);
                        config->creds = new_creds;
                        free(saved_access_key);
                        saved_access_key = strdup(new_creds->access_key_id);
                        result.auth_refreshed = 1;

                        // Free previous error result
                        free(result.raw_response);
                        free(result.error_message);

                        // === STEP 5: Retry with rotated credentials ===
                        LOG_DEBUG("Retrying API call with rotated credentials...");
                        result = bedrock_execute_request(config, bedrock_json, state, enable_streaming);

                        if (result.response) {
                            LOG_INFO("API call succeeded after credential rotation");
                            free(saved_access_key);
                            result.request_json = bedrock_json;  // Store for logging (caller frees)
                            return result;
                        }

                        LOG_WARN("API call still failed after rotation: %s", result.error_message);
                    } else {
                        LOG_ERROR("Failed to detect new credentials after authentication (timed out after %d attempts)", max_attempts);
                    }
                } else {
                    LOG_ERROR("Authentication command failed");
                }
            }
        } else {
            LOG_ERROR("Failed to load fresh credentials from profile");
        }

        // === STEP 6: Still auth error? One final rotation attempt ===
        if ((result.http_status == 401 || result.http_status == 403 || result.http_status == 400) &&
            result.auth_refreshed) {
            LOG_WARN("Auth error persists after rotation, attempting one final rotation...");

            if (bedrock_authenticate(profile) == 0) {
                LOG_INFO("Final authentication successful, polling for updated credentials...");

                // Poll for new credentials with the same strategy
                AWSCredentials *final_creds = NULL;
                int max_attempts = 10;
                int attempt = 0;
                char *current_key = config->creds && config->creds->access_key_id ?
                                   strdup(config->creds->access_key_id) : NULL;

                for (attempt = 0; attempt < max_attempts; attempt++) {
                    if (attempt > 0) {
                        usleep(200000);  // 200ms between attempts
                    }

                    LOG_DEBUG("Polling for final credential update (attempt %d/%d)...", attempt + 1, max_attempts);
                    AWSCredentials *polled_creds = bedrock_load_credentials(profile, config->region);

                    if (polled_creds && polled_creds->access_key_id) {
                        // Check if credentials have changed
                        if (current_key && strcmp(current_key, polled_creds->access_key_id) != 0) {
                            LOG_INFO("✓ Detected new credentials on final rotation (attempt %d)", attempt + 1);
                            LOG_DEBUG("Old key: %.10s..., New key: %.10s...",
                                     current_key, polled_creds->access_key_id);
                            final_creds = polled_creds;
                            break;
                        } else {
                            LOG_DEBUG("✗ Credentials unchanged on final rotation (attempt %d)", attempt + 1);
                            bedrock_creds_free(polled_creds);
                        }
                    } else {
                        LOG_DEBUG("✗ Failed to load credentials on final rotation (attempt %d)", attempt + 1);
                        if (polled_creds) bedrock_creds_free(polled_creds);
                    }
                }

                free(current_key);

                if (final_creds) {
                    bedrock_creds_free(config->creds);
                    config->creds = final_creds;

                    // Free previous error result
                    free(result.raw_response);
                    free(result.error_message);

                    // === STEP 7: Final retry ===
                    LOG_DEBUG("Final API call attempt with re-rotated credentials...");
                    result = bedrock_execute_request(config, bedrock_json, state, enable_streaming);

                    if (result.response) {
                        LOG_INFO("API call succeeded on final retry");
                    } else {
                        LOG_ERROR("API call failed even after final credential rotation");
                    }
                } else {
                    LOG_ERROR("Failed to detect new credentials on final rotation (timed out after %d attempts)", max_attempts);
                }
            }
        }
    }

    // Cleanup
    free(saved_access_key);
    result.request_json = bedrock_json;  // Store for logging even on error (caller frees)

    return result;
}

/**
 * Cleanup Bedrock provider resources
 */
static void bedrock_cleanup(Provider *self) {
    if (!self) return;

    LOG_DEBUG("Bedrock provider: cleaning up resources");

    if (self->config) {
        BedrockConfig *config = (BedrockConfig*)self->config;
        // Use the existing bedrock_config_free function from aws_bedrock.c
        bedrock_config_free(config);
    }

    free(self);
    LOG_DEBUG("Bedrock provider: cleanup complete");
}

// ============================================================================
// Public API
// ============================================================================

Provider* bedrock_provider_create(const char *model) {
    LOG_DEBUG("Creating Bedrock provider...");

    if (!model || model[0] == '\0') {
        LOG_ERROR("Bedrock provider: model name is required");
        return NULL;
    }

    // Initialize Bedrock configuration using existing function
    BedrockConfig *config = bedrock_config_init(model);
    if (!config) {
        LOG_ERROR("Bedrock provider: failed to initialize Bedrock configuration");
        return NULL;
    }

    // Allocate provider structure
    Provider *provider = calloc(1, sizeof(Provider));
    if (!provider) {
        LOG_ERROR("Bedrock provider: failed to allocate provider");
        bedrock_config_free(config);
        return NULL;
    }

    // Set up provider interface
    provider->name = "Bedrock";
    provider->config = config;
    provider->call_api = bedrock_call_api;
    provider->cleanup = bedrock_cleanup;

    LOG_INFO("Bedrock provider created successfully (region: %s, model: %s)",
             config->region, config->model_id);
    return provider;
}
