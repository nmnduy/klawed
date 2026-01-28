/*
 * deepseek_provider.h - DeepSeek API provider
 *
 * Thin wrapper around OpenAI provider for DeepSeek's OpenAI-compatible API.
 * Uses REASONING_CONTENT_DISCARD mode as DeepSeek MUST NOT include
 * reasoning_content in subsequent requests (causes 400 error).
 */

#ifndef DEEPSEEK_PROVIDER_H
#define DEEPSEEK_PROVIDER_H

#include "provider.h"

/**
 * Create a DeepSeek provider instance
 *
 * @param api_key - DeepSeek API key (required)
 * @param base_url - API endpoint URL (if NULL, uses default DeepSeek URL)
 * @return Provider instance (caller must cleanup via provider->cleanup()), or NULL on error
 */
Provider* deepseek_provider_create(const char *api_key, const char *base_url);

#endif // DEEPSEEK_PROVIDER_H
