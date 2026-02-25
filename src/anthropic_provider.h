/*
 * anthropic_provider.h - Direct Anthropic Messages API provider
 *
 * Implements the Provider interface for calling Anthropic's Messages API
 * using x-api-key authentication and Anthropic-native request/response format.
 */

#ifndef ANTHROPIC_PROVIDER_H
#define ANTHROPIC_PROVIDER_H

#include "provider.h"

/**
 * Anthropic provider configuration
 */
typedef struct {
    char *api_key;        // API key for x-api-key authentication
    char *base_url;       // Anthropic Messages endpoint (e.g., "https://api.anthropic.com/v1/messages")
    char *auth_header_template;  // Custom auth header template (default: "x-api-key: %s")
    char **extra_headers;  // Additional curl headers (NULL-terminated array)
    int extra_headers_count;  // Number of extra headers
} AnthropicConfig;

/**
 * Create an Anthropic provider instance
 *
 * @param api_key - Anthropic API key (required)
 * @param base_url - Messages endpoint URL (if NULL uses default)
 * @return Provider instance (caller must cleanup via provider->cleanup()), or NULL on error
 */
Provider* anthropic_provider_create(const char *api_key, const char *base_url);

#endif // ANTHROPIC_PROVIDER_H
