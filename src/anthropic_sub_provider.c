/*
 * anthropic_sub_provider.c - Anthropic Subscription Provider
 *
 * Implements the Provider interface for Anthropic using OAuth 2.0 bearer
 * tokens from a Claude.ai subscription. Tokens are loaded from
 * ~/.claude/.credentials.json (written by `claude auth login` or FileSurf).
 *
 * This is functionally identical to anthropic_provider.c except:
 *   - Authentication uses "Authorization: Bearer <token>" instead of x-api-key
 *   - An additional "anthropic-beta: oauth-2025-04-20" header is added
 *   - Token lifecycle (load/refresh/rotate) is managed by AnthropicOAuthManager
 *   - No interactive login is attempted; auth is bootstrapped externally
 */

#define _POSIX_C_SOURCE 200809L

#include "klawed_internal.h"
#include "anthropic_sub_provider.h"
#include "anthropic_oauth.h"
#include "openai_messages.h"
#include "logger.h"
#include "http_client.h"
#include "tui.h"
#include "message_queue.h"
#include "sqlite_queue.h"
#include "util/string_utils.h"
#include "anthropic_provider.h"  /* AnthropicStreamingContext and streaming helpers */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <errno.h>
#include <curl/curl.h>
#include <bsd/string.h>
#include "retry_logic.h"

#define ANTHROPIC_SUB_DEFAULT_MODEL     "claude-opus-4-5"
#define ANTHROPIC_VERSION_HEADER        "anthropic-version: 2023-06-01"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static char *arena_strdup(Arena *arena, const char *str) {
    if (!str || !arena) return NULL;
    size_t len = strlen(str) + 1;
    char *out = arena_alloc(arena, len);
    if (!out) return NULL;
    strlcpy(out, str, len);
    return out;
}

/* Progress callback for interrupt handling */
static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                              curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    ConversationState *state = (ConversationState *)clientp;
    if (state && state->interrupt_requested) {
        LOG_DEBUG("Anthropic Sub: interrupt requested, aborting HTTP request");
        return 1;
    }
    return 0;
}

/* ============================================================================
 * Provider call_api Implementation
 * ============================================================================ */

static void anthropic_sub_call_api(Provider *self, ConversationState *state,
                                    ApiCallResult *out) {
    ApiCallResult result = {0};
    AnthropicSubConfig *config = (AnthropicSubConfig *)self->config;

    if (!config || !config->oauth_manager) {
        result.error_message = strdup("Anthropic sub: config or OAuth manager not initialized");
        result.is_retryable  = 0;
        *out = result;
        return;
    }

    /* Get access token (refreshes if needed) */
    const char *access_token = anthropic_oauth_get_access_token(config->oauth_manager);
    if (!access_token) {
        result.error_message = strdup(
            "Anthropic subscription: no valid token found.\n"
            "Run 'claude auth login' to authenticate, or ensure FileSurf has\n"
            "injected credentials to ~/.claude/.credentials.json.");
        result.is_retryable = 0;
        *out = result;
        return;
    }

    /* Check if streaming is enabled */
    int enable_streaming = 0;
    const char *streaming_env = getenv("KLAWED_ENABLE_STREAMING");
    if (streaming_env &&
        (strcmp(streaming_env, "1") == 0 || strcasecmp(streaming_env, "true") == 0)) {
        enable_streaming = 1;
    }

    /* Build request using standard Anthropic format
     * We reuse the OpenAI→Anthropic conversion pipeline from anthropic_provider.c */
    int enable_caching = 1;
    const char *disable_env = getenv("DISABLE_PROMPT_CACHING");
    if (disable_env &&
        (strcmp(disable_env, "1") == 0 || strcasecmp(disable_env, "true") == 0)) {
        enable_caching = 0;
    }

    cJSON *openai_req_obj = build_openai_request(state, enable_caching);
    if (!openai_req_obj) {
        result.error_message = strdup("Failed to build request JSON");
        result.is_retryable  = 0;
        *out = result;
        return;
    }

    char *openai_req = cJSON_PrintUnformatted(openai_req_obj);
    cJSON_Delete(openai_req_obj);
    if (!openai_req) {
        result.error_message = strdup("Failed to serialize request JSON");
        result.is_retryable  = 0;
        *out = result;
        return;
    }

    char *anth_req = anthropic_convert_openai_to_anthropic_request(openai_req);
    if (!anth_req) {
        result.error_message = strdup("Failed to convert request to Anthropic format");
        result.is_retryable  = 0;
        result.request_json  = openai_req;
        *out = result;
        return;
    }

    /* Override model if configured */
    if (config->model && config->model[0] != '\0') {
        cJSON *req_json = cJSON_Parse(anth_req);
        if (req_json) {
            cJSON_DeleteItemFromObject(req_json, "model");
            cJSON_AddStringToObject(req_json, "model", config->model);
            free(anth_req);
            anth_req = cJSON_PrintUnformatted(req_json);
            cJSON_Delete(req_json);
        }
        if (!anth_req) {
            result.error_message = strdup("Failed to serialize Anthropic request with model");
            result.is_retryable  = 0;
            result.request_json  = openai_req;
            *out = result;
            return;
        }
    }

    /* Enable streaming in request body */
    if (enable_streaming) {
        cJSON *req_json = cJSON_Parse(anth_req);
        if (req_json) {
            cJSON_AddBoolToObject(req_json, "stream", 1);
            free(anth_req);
            anth_req = cJSON_PrintUnformatted(req_json);
            cJSON_Delete(req_json);
        }
    }

    /* Build headers */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    /* OAuth Bearer auth — different from the x-api-key used by anthropic_provider */
    char auth_header[2048];
    snprintf(auth_header, sizeof(auth_header),
             "Authorization: Bearer %s", access_token);
    headers = curl_slist_append(headers, auth_header);

    /* Required OAuth beta header */
    headers = curl_slist_append(headers, ANTHROPIC_OAUTH_BETA_HEADER);

    /* Standard Anthropic version header */
    const char *version_env = getenv("ANTHROPIC_VERSION");
    if (version_env && version_env[0]) {
        char buf[128];
        snprintf(buf, sizeof(buf), "anthropic-version: %s", version_env);
        headers = curl_slist_append(headers, buf);
    } else {
        headers = curl_slist_append(headers, ANTHROPIC_VERSION_HEADER);
    }

    if (!headers) {
        result.error_message = strdup("Failed to setup HTTP headers");
        result.is_retryable  = 0;
        result.request_json  = anth_req;
        free(openai_req);
        *out = result;
        return;
    }

    result.request_json = anth_req;
    result.headers_json = http_headers_to_json(headers);

    /* Execute request */
    const char *url = config->api_base ? config->api_base : ANTHROPIC_SUB_API_BASE;

    HttpRequest req = {0};
    req.url                = url;
    req.method             = "POST";
    req.body               = anth_req;
    req.headers            = headers;
    req.connect_timeout_ms = 30000;
    req.total_timeout_ms   = 300000;
    req.enable_streaming   = enable_streaming;

    HttpResponse *http_resp = NULL;
    AnthropicStreamingContext stream_ctx;

    if (enable_streaming) {
        anthropic_streaming_context_init(&stream_ctx, state);
        http_resp = http_client_execute_stream(&req, anthropic_streaming_event_handler,
                                               &stream_ctx, progress_callback, state);
    } else {
        http_resp = http_client_execute(&req, progress_callback, state);
    }

    /* Update headers_json with response headers */
    if (result.headers_json) { free(result.headers_json); result.headers_json = NULL; }
    result.headers_json = http_headers_to_json(headers);
    curl_slist_free_all(headers);

    if (!http_resp) {
        result.error_message = strdup("Failed to execute HTTP request");
        result.is_retryable  = 0;
        free(openai_req);
        if (enable_streaming) anthropic_streaming_context_free(&stream_ctx);
        *out = result;
        return;
    }

    result.duration_ms = http_resp->duration_ms;
    result.http_status = http_resp->status_code;

    if (http_resp->error_message) {
        result.error_message = strdup(http_resp->error_message);
        result.is_retryable  = http_resp->is_retryable;
        http_response_free(http_resp);
        free(openai_req);
        if (enable_streaming) anthropic_streaming_context_free(&stream_ctx);
        *out = result;
        return;
    }

    result.raw_response = http_resp->body ? strdup(http_resp->body) : NULL;
    http_response_free(http_resp);

    /* Handle 401 — token may have been rotated externally */
    if (result.http_status == 401) {
        LOG_INFO("Anthropic Sub: 401 received, attempting token recovery...");

        int reloaded = anthropic_oauth_reload_from_disk(config->oauth_manager);
        if (reloaded) {
            LOG_INFO("Anthropic Sub: Reloaded updated token from disk");
            result.is_retryable = 1;
        } else {
            LOG_INFO("Anthropic Sub: No newer token on disk, attempting refresh...");
            if (anthropic_oauth_refresh(config->oauth_manager, 1) == 0) {
                LOG_INFO("Anthropic Sub: Token refreshed successfully");
                result.is_retryable = 1;
            } else {
                reloaded = anthropic_oauth_reload_from_disk(config->oauth_manager);
                if (reloaded) {
                    result.is_retryable = 1;
                } else {
                    LOG_ERROR("Anthropic Sub: Token refresh failed");
                    free(result.error_message);
                    result.error_message = strdup(
                        "Anthropic OAuth token expired or revoked. "
                        "Run 'claude auth login' to re-authenticate.");
                    result.is_retryable = 0;
                }
            }
        }

        free(openai_req);
        if (enable_streaming) anthropic_streaming_context_free(&stream_ctx);
        *out = result;
        return;
    }

    /* Success path */
    if (result.http_status >= 200 && result.http_status < 300) {
        cJSON *openai_like = NULL;

        if (enable_streaming) {
            /* Reconstruct response from streaming context */
            cJSON *synth_response = cJSON_CreateObject();
            if (synth_response) {
                cJSON_AddStringToObject(synth_response, "id", "streaming");
                cJSON_AddStringToObject(synth_response, "type", "message");
                cJSON_AddStringToObject(synth_response, "role", "assistant");

                cJSON *content = cJSON_CreateArray();
                if (stream_ctx.accumulated_text && stream_ctx.accumulated_size > 0) {
                    cJSON *text_block = cJSON_CreateObject();
                    cJSON_AddStringToObject(text_block, "type", "text");
                    cJSON_AddStringToObject(text_block, "text", stream_ctx.accumulated_text);
                    cJSON_AddItemToArray(content, text_block);
                }
                cJSON_AddItemToObject(synth_response, "content", content);
                cJSON_AddStringToObject(synth_response, "stop_reason",
                                        stream_ctx.stop_reason
                                        ? stream_ctx.stop_reason : "end_turn");

                if (stream_ctx.message_start_data) {
                    cJSON *usage_src = cJSON_GetObjectItem(stream_ctx.message_start_data, "usage");
                    if (usage_src) {
                        cJSON_AddItemToObject(synth_response, "usage",
                                              cJSON_Duplicate(usage_src, 1));
                    }
                }

                char *synth_str = cJSON_PrintUnformatted(synth_response);
                if (synth_str) {
                    free(result.raw_response);
                    result.raw_response = synth_str;
                }

                openai_like = anthropic_convert_response_to_openai(synth_str ? synth_str : "{}");
                cJSON_Delete(synth_response);
            }

            anthropic_streaming_context_free(&stream_ctx);
        } else {
            openai_like = anthropic_convert_response_to_openai(result.raw_response);
        }

        if (!openai_like) {
            result.error_message = strdup("Failed to parse Anthropic response");
            result.is_retryable  = 1;
            free(openai_req);
            *out = result;
            return;
        }

        Arena *arena = arena_create(16384);
        if (!arena) {
            result.error_message = strdup("Failed to create arena for ApiResponse");
            result.is_retryable  = 0;
            cJSON_Delete(openai_like);
            free(openai_req);
            *out = result;
            return;
        }

        ApiResponse *api_resp = arena_alloc(arena, sizeof(ApiResponse));
        if (!api_resp) {
            result.error_message = strdup("Failed to allocate ApiResponse from arena");
            result.is_retryable  = 0;
            arena_destroy(arena);
            cJSON_Delete(openai_like);
            free(openai_req);
            *out = result;
            return;
        }

        memset(api_resp, 0, sizeof(ApiResponse));
        api_resp->arena        = arena;
        api_resp->raw_response = openai_like;

        cJSON *choices = cJSON_GetObjectItem(openai_like, "choices");
        if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
            result.error_message = strdup("Invalid response format: no choices");
            result.is_retryable  = 0;
            api_response_free(api_resp);
            free(openai_req);
            *out = result;
            return;
        }
        cJSON *choice  = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (!message) {
            result.error_message = strdup("Invalid response format: no message");
            result.is_retryable  = 0;
            api_response_free(api_resp);
            free(openai_req);
            *out = result;
            return;
        }

        cJSON *content_j = cJSON_GetObjectItem(message, "content");
        if (content_j && cJSON_IsString(content_j) && content_j->valuestring) {
            api_resp->message.text = arena_strdup(arena, content_j->valuestring);
            if (api_resp->message.text) {
                trim_whitespace(api_resp->message.text);
            }
        }

        cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
        if (tool_calls && cJSON_IsArray(tool_calls)) {
            int raw_count = cJSON_GetArraySize(tool_calls);
            int valid = 0;
            for (int i = 0; i < raw_count; i++) {
                cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
                if (cJSON_GetObjectItem(tc, "function")) valid++;
            }
            if (valid > 0) {
                api_resp->tools = arena_alloc(arena, (size_t)valid * sizeof(ToolCall));
                if (api_resp->tools) {
                    memset(api_resp->tools, 0, (size_t)valid * sizeof(ToolCall));
                    int idx = 0;
                    for (int i = 0; i < raw_count; i++) {
                        cJSON *tc   = cJSON_GetArrayItem(tool_calls, i);
                        cJSON *id_j = cJSON_GetObjectItem(tc, "id");
                        cJSON *fn   = cJSON_GetObjectItem(tc, "function");
                        if (!fn) continue;
                        cJSON *name_j = cJSON_GetObjectItem(fn, "name");
                        cJSON *args_j = cJSON_GetObjectItem(fn, "arguments");

                        api_resp->tools[idx].id = (id_j && cJSON_IsString(id_j))
                            ? arena_strdup(arena, id_j->valuestring) : NULL;
                        api_resp->tools[idx].name = (name_j && cJSON_IsString(name_j) && name_j->valuestring[0])
                            ? arena_strdup(arena, name_j->valuestring) : NULL;

                        if (args_j && cJSON_IsString(args_j)) {
                            api_resp->tools[idx].parameters = cJSON_Parse(args_j->valuestring);
                            if (!api_resp->tools[idx].parameters)
                                api_resp->tools[idx].parameters = cJSON_CreateObject();
                        } else {
                            api_resp->tools[idx].parameters = cJSON_CreateObject();
                        }
                        idx++;
                    }
                    api_resp->tool_count = valid;
                }
            }
        }

        result.response     = api_resp;
        result.is_retryable = 0;
        free(openai_req);
        *out = result;
        return;
    }

    /* HTTP error handling */
    result.is_retryable = is_http_error_retryable(result.http_status);

    if (enable_streaming) anthropic_streaming_context_free(&stream_ctx);

    cJSON *err = cJSON_Parse(result.raw_response);
    if (err) {
        cJSON *error_obj = cJSON_GetObjectItem(err, "error");
        if (error_obj) {
            cJSON *msg  = cJSON_GetObjectItem(error_obj, "message");
            cJSON *type = cJSON_GetObjectItem(error_obj, "type");
            if (msg && cJSON_IsString(msg)) {
                const char *txt      = msg->valuestring;
                const char *type_txt = (type && cJSON_IsString(type))
                                       ? type->valuestring : "";
                if (is_context_length_error(txt, type_txt)) {
                    result.error_message = get_context_length_error_message();
                    result.is_retryable  = 0;
                } else {
                    result.error_message = strdup(txt);
                }
            }
        }
        cJSON_Delete(err);
    }

    if (!result.error_message) {
        char buf[64];
        snprintf(buf, sizeof(buf), "HTTP %ld", result.http_status);
        result.error_message = strdup(buf);
    }

    free(openai_req);
    *out = result;
}

/* ============================================================================
 * Provider Cleanup
 * ============================================================================ */

static void anthropic_sub_cleanup(Provider *self) {
    if (!self) return;

    if (self->config) {
        AnthropicSubConfig *config = (AnthropicSubConfig *)self->config;
        if (config->oauth_manager) {
            anthropic_oauth_stop_refresh_thread(config->oauth_manager);
            anthropic_oauth_manager_destroy(config->oauth_manager);
        }
        free(config->api_base);
        free(config->model);
        free(config);
    }
    free(self);
    LOG_DEBUG("Anthropic Sub provider: cleanup complete");
}

/* ============================================================================
 * Public API
 * ============================================================================ */

Provider *anthropic_sub_provider_create(const char *model, const char *api_base) {
    LOG_DEBUG("Creating Anthropic subscription provider...");

    Provider *provider = calloc(1, sizeof(Provider));
    if (!provider) {
        LOG_ERROR("Anthropic Sub: failed to allocate provider");
        return NULL;
    }

    AnthropicSubConfig *config = calloc(1, sizeof(AnthropicSubConfig));
    if (!config) {
        LOG_ERROR("Anthropic Sub: failed to allocate config");
        free(provider);
        return NULL;
    }

    /* Create OAuth manager (loads token from ~/.claude/.credentials.json) */
    config->oauth_manager = anthropic_oauth_manager_create();
    if (!config->oauth_manager) {
        LOG_ERROR("Anthropic Sub: failed to create OAuth manager");
        free(config);
        free(provider);
        return NULL;
    }

    if (!anthropic_oauth_is_authenticated(config->oauth_manager)) {
        LOG_ERROR("Anthropic Sub: no valid credentials found in "
                  "~/.claude/.credentials.json");
        anthropic_oauth_manager_destroy(config->oauth_manager);
        free(config);
        free(provider);
        return NULL;
    }

    /* Start background refresh thread */
    if (anthropic_oauth_start_refresh_thread(config->oauth_manager) != 0) {
        LOG_WARN("Anthropic Sub: failed to start refresh thread (will refresh manually)");
    }

    /* Set API base URL */
    const char *base = (api_base && api_base[0] != '\0')
                       ? api_base : ANTHROPIC_SUB_API_BASE;
    config->api_base = strdup(base);
    if (!config->api_base) {
        LOG_ERROR("Anthropic Sub: failed to set API base URL");
        anthropic_oauth_stop_refresh_thread(config->oauth_manager);
        anthropic_oauth_manager_destroy(config->oauth_manager);
        free(config);
        free(provider);
        return NULL;
    }

    /* Set model */
    const char *mdl = (model && model[0] != '\0') ? model : ANTHROPIC_SUB_DEFAULT_MODEL;
    config->model = strdup(mdl);
    if (!config->model) {
        LOG_ERROR("Anthropic Sub: failed to set model");
        free(config->api_base);
        anthropic_oauth_stop_refresh_thread(config->oauth_manager);
        anthropic_oauth_manager_destroy(config->oauth_manager);
        free(config);
        free(provider);
        return NULL;
    }

    /* Wire up provider interface */
    provider->name     = "Anthropic Subscription";
    provider->config   = config;
    provider->call_api = anthropic_sub_call_api;
    provider->cleanup  = anthropic_sub_cleanup;

    LOG_INFO("Anthropic Sub provider created (model=%s, api_base=%s)",
             config->model, config->api_base);
    return provider;
}
