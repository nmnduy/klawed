/*
 * anthropic_oauth.h - Anthropic Claude OAuth 2.0 Token Management
 *
 * Manages OAuth tokens for Claude.ai subscriptions (claude auth login).
 * Unlike the OpenAI OAuth module (which drives the full device-code flow),
 * this module only handles the *token side*:
 *
 *   - Loading tokens written by `claude auth login` (PKCE auth-code flow)
 *     from ~/.claude/.credentials.json   (or $CLAUDE_CONFIG_DIR/.credentials.json)
 *   - Refreshing expired tokens via platform.claude.com/v1/oauth/token
 *   - Writing rotated refresh tokens back to .credentials.json
 *   - Background refresh thread
 *
 * The initial auth flow (browser → PKCE → token) is handled by the Claude CLI
 * or by FileSurf; klawed only consumes the tokens it places in the credentials
 * file.
 *
 * Token storage format (.credentials.json):
 * {
 *   "claudeAiOauth": {
 *     "accessToken":  "...",
 *     "refreshToken": "...",
 *     "expiresAt":    1234567890000,   // milliseconds since epoch
 *     "scopes":       ["user:profile", ...]
 *   }
 * }
 */

#ifndef ANTHROPIC_OAUTH_H
#define ANTHROPIC_OAUTH_H

#include <pthread.h>
#include <time.h>

/* ============================================================================
 * OAuth Configuration Constants
 * ============================================================================ */

/* Token endpoint used by Claude Code / claude CLI */
#define ANTHROPIC_OAUTH_TOKEN_ENDPOINT  "https://claude.ai/oauth/token"

/* Required beta header for OAuth-authenticated API calls */
#define ANTHROPIC_OAUTH_BETA_HEADER     "anthropic-beta: oauth-2025-04-20"

/* API endpoint (same as API-key variant) */
#define ANTHROPIC_SUB_API_BASE          "https://api.anthropic.com/v1/messages"

/* Token timing */
#define ANTHROPIC_TOKEN_REFRESH_THRESHOLD_SECONDS   300   /* Refresh when < 5 min remaining */
#define ANTHROPIC_TOKEN_REFRESH_INTERVAL_SECONDS    60    /* Check every minute */

/* ============================================================================
 * Structs
 * ============================================================================ */

/**
 * OAuth token for a Claude subscription
 */
typedef struct {
    char   *access_token;   /* Bearer token for API calls */
    char   *refresh_token;  /* Token for refreshing access_token (rotates!) */
    time_t  expires_at;     /* Unix timestamp when access_token expires */
} AnthropicOAuthToken;

/**
 * OAuth manager — handles token lifecycle and background refresh
 */
typedef struct {
    AnthropicOAuthToken *token;                         /* Current token (mutex-protected) */
    pthread_mutex_t      token_mutex;                   /* Protects token access */
    pthread_t            refresh_thread;                /* Background refresh thread */
    volatile int         refresh_thread_running;        /* Flag to stop refresh thread */
    volatile int         refresh_thread_started;        /* Thread was started */
} AnthropicOAuthManager;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Create and initialize an Anthropic OAuth manager.
 * Loads any existing token from ~/.claude/.credentials.json.
 *
 * @return Initialized manager, or NULL on error. Caller must destroy.
 */
AnthropicOAuthManager* anthropic_oauth_manager_create(void);

/**
 * Destroy the OAuth manager and stop background refresh.
 *
 * @param manager Manager to destroy
 */
void anthropic_oauth_manager_destroy(AnthropicOAuthManager *manager);

/**
 * Check if we have a valid (or refreshable) token.
 *
 * @param manager OAuth manager
 * @return 1 if authenticated, 0 if login required
 */
int anthropic_oauth_is_authenticated(AnthropicOAuthManager *manager);

/**
 * Get current access token (refreshes if needed).
 * Caller must NOT free the returned string.
 *
 * @param manager OAuth manager
 * @return Access token string, or NULL if not authenticated / refresh failed
 */
const char* anthropic_oauth_get_access_token(AnthropicOAuthManager *manager);

/**
 * Force token refresh.
 *
 * @param manager OAuth manager
 * @param force   If 1, refresh regardless of expiration; 0 only if near expiry
 * @return 0 on success, -1 on error
 */
int anthropic_oauth_refresh(AnthropicOAuthManager *manager, int force);

/**
 * Reload token from disk to pick up changes from other processes (subagents,
 * or the Claude CLI running a background refresh).
 *
 * @param manager OAuth manager
 * @return 1 if a new token was loaded, 0 if no change
 */
int anthropic_oauth_reload_from_disk(AnthropicOAuthManager *manager);

/**
 * Start background token refresh thread.
 *
 * @param manager OAuth manager
 * @return 0 on success, -1 on error
 */
int anthropic_oauth_start_refresh_thread(AnthropicOAuthManager *manager);

/**
 * Stop background token refresh thread.
 *
 * @param manager OAuth manager
 */
void anthropic_oauth_stop_refresh_thread(AnthropicOAuthManager *manager);

/**
 * Free an OAuth token struct (securely wipes sensitive fields).
 *
 * @param token Token to free
 */
void anthropic_oauth_token_free(AnthropicOAuthToken *token);

#endif /* ANTHROPIC_OAUTH_H */
