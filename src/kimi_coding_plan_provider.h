/*
 * kimi_coding_plan_provider.h - Kimi Coding Plan API provider
 *
 * Implements the Provider interface for Kimi Coding Plan API using OAuth 2.0
 * device authorization flow. Uses OpenAI-compatible chat completions format.
 *
 * Features:
 * - OAuth 2.0 device authorization (RFC 8628)
 * - Background token refresh thread
 * - Device identification headers
 * - Reasoning content preservation (for thinking models)
 */

#ifndef KIMI_CODING_PLAN_PROVIDER_H
#define KIMI_CODING_PLAN_PROVIDER_H

#include "provider.h"
#include "kimi_oauth.h"

/**
 * Kimi Coding Plan provider configuration
 */
typedef struct {
    KimiOAuthManager *oauth_manager;  // OAuth manager for token lifecycle
    char *api_base;                   // API base URL
    char *model;                      // Model name (e.g., "kimi-for-coding")
} KimiCodingPlanConfig;

/**
 * Create a Kimi Coding Plan provider instance
 *
 * Initializes OAuth manager, checks authentication state, and starts
 * background token refresh thread. If not authenticated, will attempt
 * interactive login.
 *
 * @param model - Model name (if NULL, uses default "kimi-for-coding")
 * @return Provider instance (caller must cleanup via provider->cleanup()),
 *         or NULL on error
 */
Provider* kimi_coding_plan_provider_create(const char *model);

#endif // KIMI_CODING_PLAN_PROVIDER_H
