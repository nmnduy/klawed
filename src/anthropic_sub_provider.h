/*
 * anthropic_sub_provider.h - Anthropic Subscription Provider
 *
 * Implements the Provider interface for Anthropic using OAuth 2.0 bearer
 * tokens from a Claude.ai subscription (authenticated via `claude auth login`
 * or FileSurf's PKCE flow).
 *
 * The provider sends requests to api.anthropic.com/v1/messages with:
 *   Authorization: Bearer <access_token>
 *   anthropic-beta: oauth-2025-04-20
 *
 * Token storage: ~/.claude/.credentials.json  (same file the Claude CLI uses)
 * Token refresh: platform.claude.com/v1/oauth/token (grant_type=refresh_token)
 *
 * Setup:
 *   1. Set provider_type = "anthropic_sub" in .klawed/config.json
 *   2. Run `claude auth login` (or let FileSurf inject credentials)
 *   3. klawed reads tokens from ~/.claude/.credentials.json automatically
 */

#ifndef ANTHROPIC_SUB_PROVIDER_H
#define ANTHROPIC_SUB_PROVIDER_H

#include "provider.h"
#include "anthropic_oauth.h"

/**
 * Anthropic subscription provider configuration
 */
typedef struct {
    AnthropicOAuthManager *oauth_manager;  /* OAuth manager for token lifecycle */
    char                  *api_base;       /* API base URL */
    char                  *model;          /* Model name (e.g., "claude-opus-4-5") */
} AnthropicSubConfig;

/**
 * Create an Anthropic subscription provider instance.
 *
 * Initializes the OAuth manager, loads tokens from ~/.claude/.credentials.json,
 * and starts the background token refresh thread.
 *
 * Unlike the OpenAI subscription provider, no interactive login is attempted
 * if credentials are missing — the user must run `claude auth login` first.
 *
 * @param model    Model name (if NULL, defaults to "claude-opus-4-5")
 * @param api_base API base URL (if NULL, defaults to "https://api.anthropic.com/v1/messages")
 * @return Provider instance (caller must cleanup via provider->cleanup()),
 *         or NULL on error
 */
Provider* anthropic_sub_provider_create(const char *model, const char *api_base);

#endif /* ANTHROPIC_SUB_PROVIDER_H */
