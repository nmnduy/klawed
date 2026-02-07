/*
 * kimi_oauth.h - Kimi Coding Plan OAuth 2.0 Device Authorization
 *
 * Implements OAuth 2.0 Device Authorization Grant (RFC 8628) for
 * Kimi Coding Plan API authentication.
 *
 * Key features:
 * - Device code flow (no browser required on device)
 * - Persistent token storage in ~/.kimi/credentials/
 * - Background token refresh thread
 * - Device identification headers
 */

#ifndef KIMI_OAUTH_H
#define KIMI_OAUTH_H

#include <pthread.h>
#include <time.h>

/**
 * Callback type for displaying messages during OAuth flow
 * Used to redirect output to TUI instead of console
 *
 * @param user_data User-provided context (e.g., TUI state)
 * @param message Message to display
 * @param is_error 1 if this is an error message, 0 for normal/info
 */
typedef void (*KimiOAuthMessageCallback)(void *user_data, const char *message, int is_error);

// OAuth Configuration
#define KIMI_OAUTH_CLIENT_ID "17e5f671-d194-4dfb-9706-5516cb48c098"
#define KIMI_VERSION "1.8.0"
#define KIMI_OAUTH_HOST "https://auth.kimi.com"
#define KIMI_API_BASE "https://api.kimi.com/coding/v1"

// Token refresh threshold (refresh if less than this many seconds remaining)
#define KIMI_TOKEN_REFRESH_THRESHOLD_SECONDS 300  // 5 minutes

// Background refresh interval
#define KIMI_TOKEN_REFRESH_INTERVAL_SECONDS 60  // Check every minute

// Device authorization timeout
#define KIMI_DEVICE_AUTH_TIMEOUT_SECONDS 900  // 15 minutes

/**
 * Device authorization response from Kimi OAuth server
 */
typedef struct {
    char *user_code;                    // Code to display to user
    char *device_code;                  // Code for polling token endpoint
    char *verification_uri;             // Base verification URL
    char *verification_uri_complete;    // URL with user_code embedded
    int expires_in;                     // Seconds until device code expires
    int interval;                       // Polling interval in seconds
} KimiDeviceAuth;

/**
 * OAuth token response
 */
typedef struct {
    char *access_token;      // JWT access token for API calls
    char *refresh_token;     // Token for refreshing access_token
    time_t expires_at;       // Unix timestamp when access_token expires
    char *token_type;        // Token type (usually "Bearer")
    char *scope;             // Granted scopes
} KimiOAuthToken;

/**
 * OAuth manager - handles token lifecycle and background refresh
 */
typedef struct {
    KimiOAuthToken *token;              // Current OAuth token (protected by mutex)
    pthread_mutex_t token_mutex;        // Protects token access
    pthread_t refresh_thread;           // Background refresh thread
    volatile int refresh_thread_running; // Flag to stop refresh thread
    volatile int refresh_thread_started; // Flag indicating thread was started
    char *device_id;                    // Persistent device identifier
    KimiOAuthMessageCallback message_callback;  // Optional callback for messages (TUI)
    void *message_callback_user_data;   // User data for message callback
} KimiOAuthManager;

/**
 * Initialize OAuth manager
 * Loads existing token from disk if available
 *
 * @return Initialized manager, or NULL on error
 */
KimiOAuthManager* kimi_oauth_manager_create(void);

/**
 * Set message display callback for OAuth operations
 * When set, messages will be sent to this callback instead of printed to console
 *
 * @param manager OAuth manager
 * @param callback Message callback function (NULL to disable)
 * @param user_data User data passed to callback
 */
void kimi_oauth_set_message_callback(KimiOAuthManager *manager,
                                      KimiOAuthMessageCallback callback,
                                      void *user_data);

/**
 * Destroy OAuth manager and stop background refresh
 *
 * @param manager Manager to destroy
 */
void kimi_oauth_manager_destroy(KimiOAuthManager *manager);

/**
 * Check if we have a valid (or refreshable) token
 *
 * @param manager OAuth manager
 * @return 1 if authenticated, 0 if login required
 */
int kimi_oauth_is_authenticated(KimiOAuthManager *manager);

/**
 * Perform interactive device authorization flow
 * Opens browser and waits for user to authorize
 *
 * @param manager OAuth manager
 * @return 0 on success, -1 on error
 */
int kimi_oauth_login(KimiOAuthManager *manager);

/**
 * Get current access token (refreshes if needed)
 * Caller must NOT free the returned string
 *
 * @param manager OAuth manager
 * @return Access token string, or NULL if not authenticated
 */
const char* kimi_oauth_get_access_token(KimiOAuthManager *manager);

/**
 * Force token refresh
 *
 * @param manager OAuth manager
 * @return 0 on success, -1 on error
 */
int kimi_oauth_refresh(KimiOAuthManager *manager);

/**
 * Start background token refresh thread
 *
 * @param manager OAuth manager
 * @return 0 on success, -1 on error
 */
int kimi_oauth_start_refresh_thread(KimiOAuthManager *manager);

/**
 * Stop background token refresh thread
 *
 * @param manager OAuth manager
 */
void kimi_oauth_stop_refresh_thread(KimiOAuthManager *manager);

/**
 * Logout - delete stored tokens
 *
 * @param manager OAuth manager
 */
void kimi_oauth_logout(KimiOAuthManager *manager);

/**
 * Reload token from disk to pick up changes from other processes.
 * Call this when receiving 401 errors before retrying.
 *
 * @param manager OAuth manager
 * @return 1 if a new token was loaded, 0 if no change
 */
int kimi_oauth_reload_from_disk(KimiOAuthManager *manager);

/**
 * Get device headers for API requests
 * Caller must free returned headers with curl_slist_free_all
 *
 * @param manager OAuth manager
 * @return curl_slist with device headers, or NULL on error
 */
struct curl_slist* kimi_oauth_get_device_headers(KimiOAuthManager *manager);

/**
 * Free device authorization response
 *
 * @param auth Device authorization to free
 */
void kimi_device_auth_free(KimiDeviceAuth *auth);

/**
 * Free OAuth token
 *
 * @param token Token to free
 */
void kimi_oauth_token_free(KimiOAuthToken *token);

#endif // KIMI_OAUTH_H
