/*
 * openai_sub_provider.h - OpenAI Subscription Provider
 *
 * Implements the Provider interface for OpenAI using OAuth 2.0 device
 * authorization. Users authenticate with their existing ChatGPT Plus/Pro
 * subscription instead of needing a separate API key with billing.
 *
 * The provider uses the standard OpenAI chat completions API
 * (api.openai.com/v1) with an OAuth access token instead of an API key.
 *
 * Setup:
 *   1. Set provider_type = "openai_sub" in config
 *   2. On first use, klawed will prompt for browser-based login
 *   3. Token is stored in ~/.openai/auth.json and auto-refreshed
 */

#ifndef OPENAI_SUB_PROVIDER_H
#define OPENAI_SUB_PROVIDER_H

#include "provider.h"
#include "openai_oauth.h"

/**
 * OpenAI subscription provider configuration
 */
typedef struct {
    OpenAIOAuthManager *oauth_manager;       /* OAuth manager for token lifecycle */
    char               *api_base;            /* API base URL (default: https://api.openai.com/v1) */
    char               *model;               /* Model name (e.g., "gpt-4o") */
    char               *previous_response_id; /* Previous Responses API response ID for conversation continuity */
} OpenAISubConfig;

/**
 * Create an OpenAI subscription provider instance.
 *
 * Initializes the OAuth manager, checks authentication, and starts
 * the background token refresh thread. If not authenticated, will
 * prompt for interactive device authorization.
 *
 * @param model    Model name (if NULL, defaults to "gpt-4o")
 * @param api_base API base URL (if NULL, defaults to "https://api.openai.com/v1")
 * @return Provider instance (caller must cleanup via provider->cleanup()),
 *         or NULL on error
 */
Provider* openai_sub_provider_create(const char *model, const char *api_base);

#endif /* OPENAI_SUB_PROVIDER_H */
