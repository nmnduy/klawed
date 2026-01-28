/*
 * deepseek_provider.c - DeepSeek API provider implementation
 *
 * Thin wrapper around OpenAI provider for DeepSeek's OpenAI-compatible API.
 */

#define _POSIX_C_SOURCE 200809L

#include "deepseek_provider.h"
#include "openai_provider.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

#define DEFAULT_DEEPSEEK_URL "https://api.deepseek.com/v1/chat/completions"

Provider* deepseek_provider_create(const char *api_key, const char *base_url) {
    LOG_DEBUG("Creating DeepSeek provider...");

    if (!api_key || api_key[0] == '\0') {
        LOG_ERROR("DeepSeek provider: API key is required");
        return NULL;
    }

    // Use provided base_url or default to DeepSeek URL
    const char *url = (base_url && base_url[0] != '\0') ? base_url : DEFAULT_DEEPSEEK_URL;

    // Create OpenAI provider with REASONING_CONTENT_DISCARD mode
    // DeepSeek MUST NOT include reasoning_content in subsequent requests
    Provider *provider = openai_provider_create_with_reasoning_mode(
        api_key,
        url,
        REASONING_CONTENT_DISCARD
    );

    if (!provider) {
        LOG_ERROR("DeepSeek provider: failed to create underlying OpenAI provider");
        return NULL;
    }

    // Update provider name to DeepSeek
    provider->name = "DeepSeek";

    LOG_INFO("DeepSeek provider created successfully (base URL: %s)", url);
    return provider;
}
