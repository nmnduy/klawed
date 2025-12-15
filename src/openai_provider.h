/*
 * openai_provider.h - OpenAI-compatible API provider
 *
 * Implements the Provider interface for OpenAI-compatible APIs
 * (OpenAI, Anthropic direct API, etc.)
 */

#ifndef OPENAI_PROVIDER_H
#define OPENAI_PROVIDER_H

#include "provider.h"

/**
 * OpenAI provider configuration
 */
typedef struct {
    char *api_key;        // API key for Bearer token authentication
    char *base_url;       // Base API URL (e.g., "https://api.anthropic.com/v1/messages")
    char *auth_header_template;  // Custom auth header template (e.g., "Authorization: Bearer %s" or "x-api-key: %s")
    char **extra_headers;  // Additional curl headers (NULL-terminated array)
    int extra_headers_count;  // Number of extra headers
} OpenAIConfig;

/**
 * Create an OpenAI provider instance
 *
 * @param api_key - API key for authentication (required)
 * @param base_url - Base API URL (if NULL, uses default Anthropic URL)
 * @return Provider instance (caller must cleanup via provider->cleanup()), or NULL on error
 */
Provider* openai_provider_create(const char *api_key, const char *base_url);

#endif // OPENAI_PROVIDER_H
