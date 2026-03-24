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

    if (!api_key || api_key[0] == '\0') {
        LOG_ERROR("Z.AI Coding Plan provider: API key is required");
        return NULL;
    }

    // Use provided base_url or default to Z.AI Coding endpoint
    const char *url = (base_url && base_url[0] != '\0') ? base_url : DEFAULT_ZAI_CODING_URL;

    // Create OpenAI provider with REASONING_CONTENT_PRESERVE mode
    // GLM models may include reasoning_content that should be preserved
    Provider *provider = openai_provider_create_with_reasoning_mode(
        api_key,
        url,
        REASONING_CONTENT_PRESERVE
    );

    if (!provider) {
        LOG_ERROR("Z.AI Coding Plan provider: failed to create underlying OpenAI provider");
        return NULL;
    }

    // Update provider name to Z.AI
    provider->name = "Z.AI GLM Coding";

    LOG_INFO("Z.AI GLM Coding Plan provider created successfully (base URL: %s)", url);
    return provider;
}

Provider* zai_coding_provider_create_with_headers(const char *api_key,
                                                   const char *base_url,
                                                   const char *extra_headers) {
    LOG_DEBUG("Creating Z.AI GLM Coding Plan provider with extra headers...");

    Provider *provider = zai_coding_provider_create(api_key, base_url);
    if (!provider) {
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

                // Allocate array
                cfg->extra_headers = calloc((size_t)count + 1, sizeof(char*));
                if (cfg->extra_headers) {
                    cfg->extra_headers_count = count;
                    int idx = 0;
                    char *token = strtok(extra_headers_copy, ",");
                    while (token && idx < count) {
                        cfg->extra_headers[idx] = strdup(token);
                        LOG_DEBUG("[Z.AI] Added extra header: %s", token);
                        idx++;
                        token = strtok(NULL, ",");
                    }
                }
                free(extra_headers_copy);
            }
        }
    }

    return provider;
}
