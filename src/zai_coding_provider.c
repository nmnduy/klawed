/*
 * zai_coding_provider.c - Z.AI GLM Coding Plan API provider implementation
 *
 * Thin wrapper around OpenAI provider for Z.AI's OpenAI-compatible API.
 * Uses the dedicated coding endpoint: https://api.z.ai/api/coding/paas/v4
 */

#define _POSIX_C_SOURCE 200809L

#include "zai_coding_provider.h"
#include "openai_provider.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

// Default Z.AI Coding Plan endpoint
#define DEFAULT_ZAI_CODING_URL "https://api.z.ai/api/coding/paas/v4/chat/completions"
// Fallback to general endpoint if needed
#define DEFAULT_ZAI_GENERAL_URL "https://api.z.ai/api/paas/v4/chat/completions"

Provider* zai_coding_provider_create(const char *api_key, const char *base_url) {
    LOG_DEBUG("Creating Z.AI GLM Coding Plan provider...");

    // Enhanced logging for debugging 401 authentication errors
    if (!api_key) {
        LOG_ERROR("[Z.AI] Authentication failed: api_key is NULL");
        return NULL;
    }
    
    size_t key_len = strlen(api_key);
    if (key_len == 0) {
        LOG_ERROR("[Z.AI] Authentication failed: api_key is empty string");
        return NULL;
    }
    
    // Log key details (safely - only first/last 4 chars if long enough)
    if (key_len >= 8) {
        LOG_INFO("[Z.AI] API key provided: length=%zu, prefix=%.4s..., suffix=...%.4s", 
                 key_len, api_key, api_key + key_len - 4);
    } else {
        LOG_INFO("[Z.AI] API key provided: length=%zu (short key)", key_len);
    }

    // Use provided base_url or default to Z.AI Coding endpoint
    const char *url = (base_url && base_url[0] != '\0') ? base_url : DEFAULT_ZAI_CODING_URL;
    LOG_INFO("[Z.AI] Using endpoint: %s", url);

    // Create OpenAI provider with REASONING_CONTENT_PRESERVE mode
    // GLM models may include reasoning_content that should be preserved
    Provider *provider = openai_provider_create_with_reasoning_mode(
        api_key,
        url,
        REASONING_CONTENT_PRESERVE
    );

    if (!provider) {
        LOG_ERROR("[Z.AI] Failed to create underlying OpenAI provider (check previous logs for details)");
        LOG_ERROR("[Z.AI] Common causes: invalid API key format, network issues, or endpoint unreachable");
        return NULL;
    }

    // Update provider name to Z.AI
    provider->name = "Z.AI GLM Coding";
    
    // Verify the API key was stored correctly in the underlying config
    OpenAIConfig *cfg = (OpenAIConfig *)provider->config;
    if (cfg && cfg->api_key) {
        size_t stored_key_len = strlen(cfg->api_key);
        LOG_INFO("[Z.AI] Provider created successfully (base URL: %s, stored key length: %zu)", 
                 url, stored_key_len);
        if (stored_key_len != key_len) {
            LOG_WARN("[Z.AI] Key length mismatch: provided=%zu, stored=%zu", key_len, stored_key_len);
        }
    } else {
        LOG_WARN("[Z.AI] Provider created but unable to verify API key storage");
    }
    
    return provider;
}

Provider* zai_coding_provider_create_with_headers(const char *api_key,
                                                   const char *base_url,
                                                   const char *extra_headers) {
    LOG_DEBUG("[Z.AI] Creating provider with extra headers...");
    
    // Log extra_headers info
    if (extra_headers && extra_headers[0] != '\0') {
        LOG_INFO("[Z.AI] Extra headers provided: %s", extra_headers);
    } else {
        LOG_DEBUG("[Z.AI] No extra headers provided");
    }

    Provider *provider = zai_coding_provider_create(api_key, base_url);
    if (!provider) {
        LOG_ERROR("[Z.AI] Failed to create provider in create_with_headers");
        return NULL;
    }

    // Apply extra_headers if provided
    if (extra_headers && extra_headers[0] != '\0') {
        LOG_INFO("[Z.AI] Loading extra_headers: %s", extra_headers);

        OpenAIConfig *cfg = (OpenAIConfig *)provider->config;
        if (cfg) {
            // Parse comma-separated headers
            char *extra_headers_copy = strdup(extra_headers);
            if (extra_headers_copy) {
                // Count headers
                int count = 0;
                char *temp = strdup(extra_headers);
                if (temp) {
                    char *token = strtok(temp, ",");
                    while (token) {
                        count++;
                        token = strtok(NULL, ",");
                    }
                    free(temp);
                }
                LOG_INFO("[Z.AI] Parsing %d extra header(s)", count);

                // Allocate array
                cfg->extra_headers = calloc((size_t)count + 1, sizeof(char*));
                if (cfg->extra_headers) {
                    cfg->extra_headers_count = count;
                    int idx = 0;
                    char *token = strtok(extra_headers_copy, ",");
                    while (token && idx < count) {
                        cfg->extra_headers[idx] = strdup(token);
                        LOG_INFO("[Z.AI] Added extra header [%d]: %s", idx, token);
                        idx++;
                        token = strtok(NULL, ",");
                    }
                } else {
                    LOG_ERROR("[Z.AI] Failed to allocate memory for extra headers");
                }
                free(extra_headers_copy);
            } else {
                LOG_ERROR("[Z.AI] Failed to duplicate extra_headers string");
            }
        } else {
            LOG_ERROR("[Z.AI] Cannot apply extra headers: provider config is NULL");
        }
    } else {
        LOG_DEBUG("[Z.AI] No extra headers to apply");
    }

    LOG_INFO("[Z.AI] Provider with headers created successfully");
    return provider;
}
