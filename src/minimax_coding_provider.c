/*
 * minimax_coding_provider.c - MiniMax Coding Plan API provider implementation
 *
 * Thin wrapper around Anthropic provider for MiniMax's Anthropic-compatible API.
 * Uses the endpoint: https://api.minimax.io/anthropic/v1/messages
 */

#define _POSIX_C_SOURCE 200809L

#include "minimax_coding_provider.h"
#include "anthropic_provider.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>

// Default MiniMax Coding Plan endpoint (Anthropic-compatible)
#define DEFAULT_MINIMAX_CODING_URL "https://api.minimax.io/anthropic/v1/messages"

Provider* minimax_coding_provider_create(const char *api_key, const char *base_url) {
    LOG_DEBUG("Creating MiniMax Coding Plan provider...");

    if (!api_key || api_key[0] == '\0') {
        LOG_ERROR("MiniMax Coding Plan provider: API key is required");
        return NULL;
    }

    // Use provided base_url or default to MiniMax Coding endpoint
    const char *url = (base_url && base_url[0] != '\0') ? base_url : DEFAULT_MINIMAX_CODING_URL;

    // Create Anthropic provider
    Provider *provider = anthropic_provider_create(api_key, url);

    if (!provider) {
        LOG_ERROR("MiniMax Coding Plan provider: failed to create underlying Anthropic provider");
        return NULL;
    }

    // Update provider name to MiniMax Coding
    provider->name = "MiniMax Coding";

    LOG_INFO("MiniMax Coding Plan provider created successfully (base URL: %s)", url);
    return provider;
}
