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
#include "openai_streaming.h"
#include "openai_chat_parser.h"
#include "http_client.h"
#include "logger.h"
#include "arena.h"
#include "retry_logic.h"
#include "tui.h"
#include "message_queue.h"  // For thread-safe streaming updates
#include "tool_utils.h"

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
 * OAuth message callback - displays messages to TUI if available, otherwise console
 * This allows the OAuth flow to show browser login prompts in the conversation TUI
 */
static void kimi_oauth_message_callback(void *user_data, const char *message, int is_error) {
    ConversationState *state = (ConversationState *)user_data;

    if (state && (state->tui_queue || state->tui)) {
        // Display in TUI conversation
        TUIColorPair color = is_error ? COLOR_PAIR_ERROR : COLOR_PAIR_STATUS;
        if (state->tui_queue) {
            post_tui_stream_start(state->tui_queue, "[System]", (int)color);
            post_tui_message(state->tui_queue, TUI_MSG_STREAM_APPEND, message);
        } else {
            tui_add_conversation_line(state->tui, "[System]", message, color);
            tui_refresh(state->tui);
        }
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
    OpenAIStreamingAccumulator acc;
    ConversationState *state;
    int reasoning_line_added;
    int assistant_line_added;
} KimiStreamingContext;

static void kimi_streaming_context_init(KimiStreamingContext *ctx, ConversationState *state) {
    memset(ctx, 0, sizeof(KimiStreamingContext));
    ctx->state = state;
    openai_streaming_accumulator_init(&ctx->acc);
}

static void kimi_streaming_context_free(KimiStreamingContext *ctx) {
    if (!ctx) return;
    openai_streaming_accumulator_free(&ctx->acc);
}

static int kimi_streaming_event_handler(StreamEvent *event, void *userdata) {
    KimiStreamingContext *ctx = (KimiStreamingContext *)userdata;

    // Check for interrupt
    if (ctx->state && ctx->state->interrupt_requested) {
        LOG_DEBUG("Kimi streaming handler: interrupt requested");
        return 1;  // Abort stream
    }

    int ret = openai_streaming_process_event(&ctx->acc, event);
    if (ret != 0) {
        return ret;
    }

    if (event->type == SSE_EVENT_OPENAI_CHUNK && event->data) {
        cJSON *choices = cJSON_GetObjectItem(event->data, "choices");
        if (choices && cJSON_IsArray(choices)) {
            cJSON *choice = cJSON_GetArrayItem(choices, 0);
            if (choice) {
                cJSON *delta = cJSON_GetObjectItem(choice, "delta");
                if (delta) {
                    cJSON *content = cJSON_GetObjectItem(delta, "content");
                    if (content && cJSON_IsString(content) && content->valuestring && content->valuestring[0]) {
                        if (!ctx->assistant_line_added && ctx->state) {
                            if (ctx->state->tui_queue) {
                                post_tui_stream_start(ctx->state->tui_queue, "[Assistant]", COLOR_PAIR_ASSISTANT);
                            } else if (ctx->state->tui) {
                                tui_add_conversation_line(ctx->state->tui, "[Assistant]", "", COLOR_PAIR_ASSISTANT);
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

                    cJSON *reasoning = cJSON_GetObjectItem(delta, "reasoning_content");
                    if (reasoning && cJSON_IsString(reasoning) && reasoning->valuestring && reasoning->valuestring[0]) {
                        if (!ctx->reasoning_line_added && ctx->state) {
                            if (ctx->state->tui_queue) {
                                post_tui_stream_start(ctx->state->tui_queue, "⟨Reasoning⟩", COLOR_PAIR_TOOL_DIM);
                            } else if (ctx->state->tui) {
                                tui_add_conversation_line(ctx->state->tui, "⟨Reasoning⟩", "", COLOR_PAIR_TOOL_DIM);
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
    int enable_streaming = is_streaming_enabled(state);

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
            raw_json = openai_streaming_build_response(&stream_ctx.acc);
            if (raw_json && config->model) {
                cJSON *model = cJSON_GetObjectItem(raw_json, "model");
                if (!model || !cJSON_IsString(model) || strcmp(model->valuestring, "unknown") == 0) {
                    if (model) {
                        cJSON_ReplaceItemInObject(raw_json, "model", cJSON_CreateString(config->model));
                    } else {
                        cJSON_AddStringToObject(raw_json, "model", config->model);
                    }
                }
            }
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

        ApiResponse *api_response = openai_parse_chat_completion_response(raw_json, "Kimi");
        if (!api_response) {
            result.error_message = strdup("Failed to parse Kimi response");
            result.is_retryable = 0;
            free(result.headers_json);
            result.headers_json = NULL;
            *out = result;
            return;
        }

        api_response->ui_streamed = enable_streaming ? 1 : 0;

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
