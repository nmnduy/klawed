/*
 * zai_coding_provider.h - Z.AI GLM Coding Plan API provider
 *
 * Implements the Provider interface for Z.AI GLM Coding Plan API.
 * Uses OpenAI-compatible chat completions format with dedicated coding endpoint.
 *
 * Features:
 * - OpenAI-compatible API format
 * - Dedicated coding endpoint (api.z.ai/api/coding/paas/v4)
 * - Bearer token authentication
 * - Extra headers support
 */

#ifndef ZAI_CODING_PROVIDER_H
#define ZAI_CODING_PROVIDER_H

#include "provider.h"

/**
 * Create a Z.AI GLM Coding Plan provider instance
 *
 * @param api_key - API key for Z.AI (from ZAI_API_KEY_CODING_PLAN)
 * @param base_url - Optional custom base URL (NULL for default coding endpoint)
 * @return Provider instance (caller must cleanup via provider->cleanup()),
 *         or NULL on error
 */
Provider* zai_coding_provider_create(const char *api_key, const char *base_url);

/**
 * Create a Z.AI GLM Coding Plan provider with extra headers
 *
 * @param api_key - API key for Z.AI
 * @param base_url - Optional custom base URL (NULL for default)
 * @param extra_headers - Comma-separated extra headers (e.g., "X-Custom: value")
 * @return Provider instance, or NULL on error
 */
Provider* zai_coding_provider_create_with_headers(const char *api_key,
                                                   const char *base_url,
                                                   const char *extra_headers);

#endif // ZAI_CODING_PROVIDER_H
