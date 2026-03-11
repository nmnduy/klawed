/*
 * openai_oauth.h - OpenAI OAuth 2.0 Device Authorization
 *
 * Implements OAuth 2.0 Device Authorization Grant (RFC 8628) for
 * OpenAI ChatGPT subscription authentication.
 *
 * This allows users with a ChatGPT Plus/Pro subscription to authenticate
 * using their existing subscription rather than needing a separate API key.
 *
 * Key features:
 * - Device code flow (no browser required on the device itself)
 * - Persistent token storage in ~/.openai/auth.json
 * - Background token refresh thread
 * - Token rotation support (refresh_token rotates on each use)
 */

#ifndef OPENAI_OAUTH_H
#define OPENAI_OAUTH_H

#include <pthread.h>
#include <time.h>

/**
 * Callback type for displaying messages during OAuth flow
 * Used to redirect output to TUI instead of console
 *
 * @param user_data User-provided context (e.g., TUI state)
 * @param message   Message to display
 * @param is_error  1 if this is an error message, 0 for normal/info
 */
typedef void (*OpenAIOAuthMessageCallback)(void *user_data, const char *message, int is_error);

/* ============================================================================
 * OAuth Configuration Constants
 * ============================================================================ */

/* Auth0 tenant used by OpenAI */
#define OPENAI_AUTH0_HOST           "https://auth0.openai.com"

/* Client ID for the OpenAI CLI / headless device auth */
#define OPENAI_OAUTH_CLIENT_ID      "app_TlhIBpuQDJLnLhbAkFpDH9rS"

/* OAuth scopes requested */
#define OPENAI_OAUTH_SCOPE          "openid profile email offline_access"

/* API audience (resource server) */
#define OPENAI_OAUTH_AUDIENCE       "https://api.openai.com/v1"

/* OAuth endpoints */
#define OPENAI_DEVICE_AUTH_ENDPOINT OPENAI_AUTH0_HOST "/oauth/device/code"
#define OPENAI_TOKEN_ENDPOINT       OPENAI_AUTH0_HOST "/oauth/token"
#define OPENAI_REVOKE_ENDPOINT      OPENAI_AUTH0_HOST "/oauth/revoke"

/* API base URL used with subscription tokens */
#define OPENAI_SUB_API_BASE         "https://api.openai.com/v1"

/* Token timing */
#define OPENAI_TOKEN_REFRESH_THRESHOLD_SECONDS  300   /* Refresh when < 5 min remaining */
#define OPENAI_TOKEN_REFRESH_INTERVAL_SECONDS   60    /* Check every minute */
#define OPENAI_DEVICE_AUTH_TIMEOUT_SECONDS      900   /* 15 minute timeout */

/* ============================================================================
 * Structs
 * ============================================================================ */

/**
 * Device authorization response from OpenAI OAuth server
 */
typedef struct {
    char *user_code;                    /* Code to display to user */
    char *device_code;                  /* Code for polling token endpoint */
    char *verification_uri;             /* Base verification URL */
    char *verification_uri_complete;    /* URL with user_code embedded */
    int   expires_in;                   /* Seconds until device code expires */
    int   interval;                     /* Polling interval in seconds */
} OpenAIDeviceAuth;

/**
 * OAuth token (access + refresh)
 */
typedef struct {
    char   *access_token;   /* Bearer token for API calls */
    char   *refresh_token;  /* Token for refreshing access_token (rotates!) */
    time_t  expires_at;     /* Unix timestamp when access_token expires */
    char   *token_type;     /* Usually "Bearer" */
    char   *scope;          /* Granted scopes */
    char   *id_token;       /* OpenID Connect identity token (optional) */
} OpenAIOAuthToken;

/**
 * OAuth manager - handles token lifecycle and background refresh
 */
typedef struct {
    OpenAIOAuthToken            *token;                     /* Current token (mutex-protected) */
    pthread_mutex_t              token_mutex;               /* Protects token access */
    pthread_t                    refresh_thread;            /* Background refresh thread */
    volatile int                 refresh_thread_running;    /* Flag to stop refresh thread */
    volatile int                 refresh_thread_started;    /* Thread was started */
    OpenAIOAuthMessageCallback   message_callback;          /* Optional TUI message callback */
    void                        *message_callback_user_data; /* User data for callback */
} OpenAIOAuthManager;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Create and initialize an OAuth manager.
 * Loads any existing token from disk (~/.openai/auth.json).
 *
 * @return Initialized manager, or NULL on error. Caller must destroy.
 */
OpenAIOAuthManager* openai_oauth_manager_create(void);

/**
 * Destroy the OAuth manager and stop background refresh.
 *
 * @param manager Manager to destroy
 */
void openai_oauth_manager_destroy(OpenAIOAuthManager *manager);

/**
 * Set message display callback for OAuth operations.
 * When set, messages are sent to the callback instead of printed to console.
 *
 * @param manager   OAuth manager
 * @param callback  Message callback (NULL to disable)
 * @param user_data User data passed to callback
 */
void openai_oauth_set_message_callback(OpenAIOAuthManager *manager,
                                        OpenAIOAuthMessageCallback callback,
                                        void *user_data);

/**
 * Check if we have a valid (or refreshable) token.
 *
 * @param manager OAuth manager
 * @return 1 if authenticated, 0 if login required
 */
int openai_oauth_is_authenticated(OpenAIOAuthManager *manager);

/**
 * Perform interactive device authorization flow.
 * Displays a URL and user code, then polls until the user authorizes.
 *
 * @param manager OAuth manager
 * @return 0 on success, -1 on error
 */
int openai_oauth_login(OpenAIOAuthManager *manager);

/**
 * Get current access token (refreshes if needed).
 * Caller must NOT free the returned string.
 *
 * @param manager OAuth manager
 * @return Access token string, or NULL if not authenticated / refresh failed
 */
const char* openai_oauth_get_access_token(OpenAIOAuthManager *manager);

/**
 * Force token refresh.
 *
 * @param manager OAuth manager
 * @param force   If 1, refresh regardless of expiration; 0 only if near expiry
 * @return 0 on success, -1 on error
 */
int openai_oauth_refresh(OpenAIOAuthManager *manager, int force);

/**
 * Reload token from disk to pick up changes from other processes (subagents).
 * Call this when receiving 401 errors before retrying.
 *
 * @param manager OAuth manager
 * @return 1 if a new token was loaded, 0 if no change
 */
int openai_oauth_reload_from_disk(OpenAIOAuthManager *manager);

/**
 * Start background token refresh thread.
 *
 * @param manager OAuth manager
 * @return 0 on success, -1 on error
 */
int openai_oauth_start_refresh_thread(OpenAIOAuthManager *manager);

/**
 * Stop background token refresh thread.
 *
 * @param manager OAuth manager
 */
void openai_oauth_stop_refresh_thread(OpenAIOAuthManager *manager);

/**
 * Logout - revoke and delete stored tokens.
 *
 * @param manager OAuth manager
 */
void openai_oauth_logout(OpenAIOAuthManager *manager);

/**
 * Free a device auth response struct.
 *
 * @param auth Device auth to free
 */
void openai_device_auth_free(OpenAIDeviceAuth *auth);

/**
 * Free an OAuth token struct (securely wipes sensitive fields).
 *
 * @param token Token to free
 */
void openai_oauth_token_free(OpenAIOAuthToken *token);

#endif /* OPENAI_OAUTH_H */
