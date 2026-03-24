/*
 * moonshot_provider.c - Moonshot/Kimi API provider implementation
 *
 * Thin wrapper around OpenAI provider for Moonshot's OpenAI-compatible API.
 */

#define _POSIX_C_SOURCE 200809L

#include "moonshot_provider.h"
#include "openai_provider.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

#define DEFAULT_MOONSHOT_URL "https://api.moonshot.cn/v1/chat/completions"

Provider* moonshot_provider_create(const char *api_key, const char *base_url) {
    LOG_DEBUG("Creating Moonshot provider...");

    if (!api_key || api_key[0] == '\0') {
        LOG_ERROR("Moonshot provider: API key is required");
        return NULL;
    }

    // Use provided base_url or default to Moonshot URL
    const char *url = (base_url && base_url[0] != '\0') ? base_url : DEFAULT_MOONSHOT_URL;

    // Create OpenAI provider with REASONING_CONTENT_PRESERVE mode
    // Moonshot/Kimi MUST include reasoning_content in subsequent requests
    Provider *provider = openai_provider_create_with_reasoning_mode(
        api_key,
        url,
        REASONING_CONTENT_PRESERVE
    );

    if (!provider) {
        LOG_ERROR("Moonshot provider: failed to create underlying OpenAI provider");
        return NULL;
    }

    // Update provider name to Moonshot
    provider->name = "Moonshot";

    LOG_INFO("Moonshot provider created successfully (base URL: %s)", url);
    return provider;
}

Provider* moonshot_provider_create_with_headers(const char *api_key,
                                                 const char *base_url,
                                                 const char *extra_headers) {
    LOG_DEBUG("Creating Moonshot provider with extra headers...");

    Provider *provider = moonshot_provider_create(api_key, base_url);
    if (!provider) {
        return NULL;
    }

    // Apply extra_headers if provided
    if (extra_headers && extra_headers[0] != '\0') {
        LOG_INFO("[Moonshot] Loading extra_headers: %s", extra_headers);

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
                        LOG_DEBUG("[Moonshot] Added extra header: %s", token);
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
