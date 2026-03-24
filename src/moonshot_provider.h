/*
 * moonshot_provider.h - Moonshot/Kimi API provider
 *
 * Thin wrapper around OpenAI provider for Moonshot's OpenAI-compatible API.
 * Uses REASONING_CONTENT_PRESERVE mode as Moonshot/Kimi MUST include
 * reasoning_content in subsequent requests (causes 400 error if missing).
 */

#ifndef MOONSHOT_PROVIDER_H
#define MOONSHOT_PROVIDER_H

#include "provider.h"

/**
 * Create a Moonshot provider instance
 *
 * @param api_key - Moonshot API key (required)
 * @param base_url - API endpoint URL (if NULL, uses default Moonshot URL)
 * @return Provider instance (caller must cleanup via provider->cleanup()), or NULL on error
 */
Provider* moonshot_provider_create(const char *api_key, const char *base_url);

/**
 * Create a Moonshot provider instance with extra headers
 *
 * @param api_key - Moonshot API key (required)
 * @param base_url - API endpoint URL (if NULL, uses default Moonshot URL)
 * @param extra_headers - Comma-separated list of extra HTTP headers
 * @return Provider instance (caller must cleanup via provider->cleanup()), or NULL on error
 */
Provider* moonshot_provider_create_with_headers(const char *api_key,
                                                 const char *base_url,
                                                 const char *extra_headers);

#endif // MOONSHOT_PROVIDER_H
