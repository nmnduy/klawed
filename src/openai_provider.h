/*
 * openai_provider.h - OpenAI-compatible API provider
 *
 * Implements the Provider interface for OpenAI-compatible APIs
 * (OpenAI, Anthropic direct API, DeepSeek, Moonshot/Kimi, etc.)
 */

#ifndef OPENAI_PROVIDER_H
#define OPENAI_PROVIDER_H

#include "provider.h"

/**
 * Reasoning content handling mode for thinking models
 *
 * Different providers handle reasoning_content differently in multi-turn conversations:
 * - DeepSeek: MUST include reasoning_content on assistant messages with tool_calls
 *   when using reasoning models. Omitting it causes HTTP 400.
 * - Moonshot/Kimi: MUST include reasoning_content in subsequent requests when it was
 *   originally present (causes 400 error if missing).
 * - OpenAI: Does not use reasoning_content (uses different mechanism for o1/o3 models)
 */
typedef enum {
    REASONING_CONTENT_DISCARD = 0,  // Don't include in requests (default, DeepSeek behavior)
    REASONING_CONTENT_PRESERVE      // Include in subsequent requests (Moonshot/Kimi behavior)
} ReasoningContentMode;

/**
 * OpenAI provider configuration
 */
typedef struct {
    char *api_key;        // API key for Bearer token authentication
    char *base_url;       // Base API URL (e.g., "https://api.anthropic.com/v1/messages")
    char *auth_header_template;  // Custom auth header template (e.g., "Authorization: Bearer %s" or "x-api-key: %s")
    char **extra_headers;  // Additional curl headers (NULL-terminated array)
    int extra_headers_count;  // Number of extra headers
    ReasoningContentMode reasoning_content_mode;  // How to handle reasoning_content in requests
} OpenAIConfig;

/**
 * Create an OpenAI provider instance
 *
 * @param api_key - API key for authentication (required)
 * @param base_url - Base API URL (if NULL, uses default Anthropic URL)
 * @return Provider instance (caller must cleanup via provider->cleanup()), or NULL on error
 */
Provider* openai_provider_create(const char *api_key, const char *base_url);

/**
 * Create an OpenAI provider instance with specific reasoning content mode
 *
 * @param api_key - API key for authentication (required)
 * @param base_url - Base API URL (if NULL, uses default Anthropic URL)
 * @param reasoning_mode - How to handle reasoning_content in requests
 * @return Provider instance (caller must cleanup via provider->cleanup()), or NULL on error
 */
Provider* openai_provider_create_with_reasoning_mode(const char *api_key,
                                                      const char *base_url,
                                                      ReasoningContentMode reasoning_mode);

/**
 * Get the reasoning content mode for a provider
 *
 * @param provider - Provider instance
 * @return ReasoningContentMode, or REASONING_CONTENT_DISCARD if not an OpenAI provider
 */
ReasoningContentMode openai_provider_get_reasoning_mode(Provider *provider);

#endif // OPENAI_PROVIDER_H
