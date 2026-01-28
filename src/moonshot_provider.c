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
