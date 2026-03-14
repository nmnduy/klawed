/*
 * openai_responses_provider.c - OpenAI Responses API Provider
 *
 * Implements the Provider interface dedicated to the OpenAI /v1/responses endpoint.
 * Unlike the generic openai_provider (which auto-detects the Responses API by URL
 * sniffing), this provider always uses the Responses API message format and is
 * selected via provider_type = "openai_responses" in the configuration.
 *
 * The provider reuses the shared helpers in openai_responses.c:
 *   build_responses_http_request()  - builds the HTTP request
 *   submit_responses_http_request() - executes it via http_client
 *   parse_responses_http_response() - parses the JSON response into ApiResponse
 *
 * Authentication is done via a standard Bearer token (OPENAI_API_KEY or the
 * api_key field in the provider config).
 */

#define _POSIX_C_SOURCE 200809L

#include "openai_responses_provider.h"
#include "openai_responses.h"
#include "openai_provider.h"     /* OpenAIConfig definition */
#include "http_client.h"
#include "logger.h"
#include "klawed_internal.h"
#include "retry_logic.h"
#include "arena.h"
#include "util/string_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <bsd/string.h>

/* Default endpoint and model */
#define RESPONSES_DEFAULT_API_BASE "https://api.openai.com/v1/responses"
#define RESPONSES_DEFAULT_MODEL    "gpt-4o"

/* ============================================================================
 * Provider call_api Implementation
 * ============================================================================ */

static void responses_call_api(Provider *self, ConversationState *state,
                                ApiCallResult *out) {
    ApiCallResult result = {0};
    OpenAIResponsesProviderConfig *cfg =
        (OpenAIResponsesProviderConfig *)self->config;

    if (!cfg || !cfg->api_key || cfg->api_key[0] == '\0') {
        result.error_message = strdup(
            "OpenAI Responses provider: no API key configured.\n"
            "Set OPENAI_API_KEY or add api_key to the provider config.");
        result.is_retryable = 0;
        *out = result;
        return;
    }

    if (!state) {
        result.error_message = strdup("OpenAI Responses provider: ConversationState is NULL");
        result.is_retryable = 0;
        *out = result;
        return;
    }

    /* Build a temporary OpenAIConfig from our stored credentials so we can
     * reuse build_responses_http_request() which expects that struct. */
    OpenAIConfig openai_cfg = {0};
    openai_cfg.api_key   = cfg->api_key;
    openai_cfg.base_url  = cfg->api_base;
    /* No extra headers, no custom auth template — plain Bearer token */
    /* Note: model is taken from state->model by build_responses_http_request */
    openai_cfg.extra_headers       = NULL;
    openai_cfg.extra_headers_count = 0;
    openai_cfg.auth_header_template = NULL;

    /* Responses API does not support Anthropic-style caching headers */
    int enable_caching = 0;

    /* Build the HTTP request */
    HttpRequest req = {0};
    build_responses_http_request(state, &openai_cfg, enable_caching, &req);

    if (!req.url || !req.body) {
        result.error_message = strdup("OpenAI Responses provider: failed to build HTTP request");
        result.is_retryable = 0;
        if (req.headers) curl_slist_free_all(req.headers);
        /* req.body is const char* pointing into a cJSON-allocated string;
         * cast away const for free() since we own it here */
        free((char *)(uintptr_t)req.body);
        *out = result;
        return;
    }

    LOG_DEBUG("OpenAI Responses provider: submitting request to %s", req.url);

    /* Submit */
    HttpResponse *http_resp = submit_responses_http_request(&req, state);

    /* Free request resources */
    curl_slist_free_all(req.headers);
    free((char *)(uintptr_t)req.body);

    if (!http_resp) {
        result.error_message = strdup("OpenAI Responses provider: HTTP request failed (no response)");
        result.is_retryable = 1;
        *out = result;
        return;
    }

    /* Populate result metadata */
    result.http_status   = http_resp->status_code;
    result.raw_response  = http_resp->body ? strdup(http_resp->body) : NULL;
    result.headers_json  = http_headers_to_json(http_resp->headers);

    /* Handle transport-level errors */
    if (http_resp->error_message) {
        result.error_message = strdup(http_resp->error_message);
        result.is_retryable  = http_resp->is_retryable;
        http_response_free(http_resp);
        *out = result;
        return;
    }

    http_response_free(http_resp);

    /* HTTP success */
    if (result.http_status >= 200 && result.http_status < 300) {
        if (!result.raw_response) {
            result.error_message = strdup("OpenAI Responses provider: empty response body");
            result.is_retryable = 1;
            *out = result;
            return;
        }

        ApiResponse *api_response = parse_responses_http_response(result.raw_response);
        if (!api_response) {
            result.error_message = strdup("OpenAI Responses provider: failed to parse response JSON");
            result.is_retryable = 1;
            *out = result;
            return;
        }

        result.response = api_response;
        *out = result;
        return;
    }

    /* HTTP error — try to extract the "error.message" field */
    result.is_retryable = is_http_error_retryable(result.http_status);

    if (result.raw_response) {
        cJSON *err_json = cJSON_Parse(result.raw_response);
        if (err_json) {
            cJSON *error_obj = cJSON_GetObjectItem(err_json, "error");
            if (error_obj) {
                cJSON *msg = cJSON_GetObjectItem(error_obj, "message");
                cJSON *type = cJSON_GetObjectItem(error_obj, "type");
                if (msg && cJSON_IsString(msg)) {
                    const char *msg_text  = msg->valuestring;
                    const char *type_text = (type && cJSON_IsString(type))
                                           ? type->valuestring : "";
                    if (is_context_length_error(msg_text, type_text)) {
                        result.error_message  = get_context_length_error_message();
                        result.is_retryable   = 0;
                    } else {
                        result.error_message = strdup(msg_text);
                    }
                }
            }
            cJSON_Delete(err_json);
        }
    }

    if (!result.error_message) {
        char buf[64];
        snprintf(buf, sizeof(buf), "HTTP %ld", result.http_status);
        result.error_message = strdup(buf);
    }

    *out = result;
}

/* ============================================================================
 * Provider cleanup
 * ============================================================================ */

static void responses_cleanup(Provider *self) {
    if (!self) return;

    OpenAIResponsesProviderConfig *cfg =
        (OpenAIResponsesProviderConfig *)self->config;

    if (cfg) {
        free(cfg->api_key);
        free(cfg->api_base);
        free(cfg->model);
        free(cfg);
    }

    free(self);
}

/* ============================================================================
 * Factory function
 * ============================================================================ */

Provider *openai_responses_provider_create(const char *api_key,
                                            const char *api_base,
                                            const char *model) {
    /* Resolve API key: argument takes priority, then OPENAI_API_KEY env var */
    const char *resolved_key = (api_key && api_key[0] != '\0') ? api_key
                                                                : getenv("OPENAI_API_KEY");
    if (!resolved_key || resolved_key[0] == '\0') {
        LOG_ERROR("OpenAI Responses provider: no API key available "
                  "(set OPENAI_API_KEY or configure api_key in provider config)");
        return NULL;
    }

    const char *resolved_base  = (api_base && api_base[0] != '\0') ? api_base
                                                                     : RESPONSES_DEFAULT_API_BASE;
    const char *resolved_model = (model && model[0] != '\0') ? model
                                                              : RESPONSES_DEFAULT_MODEL;

    Provider *prov = calloc(1, sizeof(Provider));
    if (!prov) {
        LOG_ERROR("OpenAI Responses provider: failed to allocate Provider");
        return NULL;
    }

    OpenAIResponsesProviderConfig *cfg =
        calloc(1, sizeof(OpenAIResponsesProviderConfig));
    if (!cfg) {
        LOG_ERROR("OpenAI Responses provider: failed to allocate config");
        free(prov);
        return NULL;
    }

    cfg->api_key  = strdup(resolved_key);
    cfg->api_base = strdup(resolved_base);
    cfg->model    = strdup(resolved_model);

    if (!cfg->api_key || !cfg->api_base || !cfg->model) {
        LOG_ERROR("OpenAI Responses provider: strdup failed");
        free(cfg->api_key);
        free(cfg->api_base);
        free(cfg->model);
        free(cfg);
        free(prov);
        return NULL;
    }

    prov->config   = cfg;
    prov->call_api = responses_call_api;
    prov->cleanup  = responses_cleanup;

    LOG_INFO("OpenAI Responses provider created (model: %s, url: %s)",
             cfg->model, cfg->api_base);

    return prov;
}
