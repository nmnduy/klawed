/*
 * anthropic_oauth.c - Anthropic Claude OAuth 2.0 Token Management
 *
 * Loads OAuth tokens written by `claude auth login` (or FileSurf) from
 * ~/.claude/.credentials.json, refreshes them when near expiry, and writes
 * rotated refresh tokens back to disk.
 *
 * The initial browser-based PKCE auth flow is NOT implemented here — that is
 * handled externally (Claude CLI, FileSurf). This module only manages the
 * token lifecycle after the first token has been placed on disk.
 */

#define _POSIX_C_SOURCE 200809L

#include "anthropic_oauth.h"
#include "logger.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <bsd/string.h>
#include <bsd/stdlib.h>

/* ============================================================================
 * Internal Forward Declarations
 * ============================================================================ */

static char                *get_credentials_file_path(void);
static int                  mkdir_p_with_mode(const char *path, mode_t mode);
static AnthropicOAuthToken *load_token_from_disk(void);
static int                  save_token_to_disk(const AnthropicOAuthToken *token);
static int                  reload_token_from_disk_if_newer(AnthropicOAuthManager *manager);
static AnthropicOAuthToken *refresh_token_internal(AnthropicOAuthManager *manager, int force);
static void                *refresh_thread_func(void *arg);
static char                *http_post_form(const char *url, const char *body,
                                            long *http_status_out);

/* ============================================================================
 * Filesystem Helpers
 * ============================================================================ */

/**
 * Create directory recursively with specified mode.
 */
static int mkdir_p_with_mode(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    char *p = NULL;

    if (strlcpy(tmp, path, sizeof(tmp)) >= sizeof(tmp)) {
        return -1;
    }
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

/**
 * Return the path to .credentials.json (caller must free).
 *
 * Priority:
 *   1. $CLAUDE_CONFIG_DIR/.credentials.json
 *   2. ~/.claude/.credentials.json
 */
static char *get_credentials_file_path(void) {
    const char *config_dir = getenv("CLAUDE_CONFIG_DIR");

    if (!config_dir || config_dir[0] == '\0') {
        /* Use ~/.claude */
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0') {
            struct passwd *pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
        }
        if (!home || home[0] == '\0') {
            LOG_ERROR("[Anthropic OAuth] Cannot determine home directory");
            return NULL;
        }

        char *path = malloc(PATH_MAX);
        if (!path) return NULL;
        if (snprintf(path, PATH_MAX, "%s/.claude/.credentials.json", home) >= PATH_MAX) {
            free(path);
            return NULL;
        }
        return path;
    }

    char *path = malloc(PATH_MAX);
    if (!path) return NULL;
    if (snprintf(path, PATH_MAX, "%s/.credentials.json", config_dir) >= PATH_MAX) {
        free(path);
        return NULL;
    }
    return path;
}

/* ============================================================================
 * Token Storage
 * ============================================================================ */

/**
 * Load OAuth token from .credentials.json.
 *
 * The file is written by `claude auth login` with the following structure:
 * {
 *   "claudeAiOauth": {
 *     "accessToken":  "...",
 *     "refreshToken": "...",
 *     "expiresAt":    1234567890000   // milliseconds since epoch
 *   }
 * }
 */
static AnthropicOAuthToken *load_token_from_disk(void) {
    char *path = get_credentials_file_path();
    if (!path) return NULL;

    FILE *f = fopen(path, "r");
    free(path);
    if (!f) {
        LOG_DEBUG("[Anthropic OAuth] No credentials file found");
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize <= 0 || fsize > 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *content = malloc((size_t)fsize + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    size_t nread = fread(content, 1, (size_t)fsize, f);
    fclose(f);

    if (nread != (size_t)fsize) {
        free(content);
        return NULL;
    }
    content[fsize] = '\0';

    cJSON *root = cJSON_Parse(content);
    free(content);
    if (!root) {
        LOG_WARN("[Anthropic OAuth] Failed to parse credentials file");
        return NULL;
    }

    /* Navigate into claudeAiOauth object */
    cJSON *oauth = cJSON_GetObjectItem(root, "claudeAiOauth");
    if (!oauth) {
        LOG_WARN("[Anthropic OAuth] credentials.json missing 'claudeAiOauth' key");
        cJSON_Delete(root);
        return NULL;
    }

    AnthropicOAuthToken *token = calloc(1, sizeof(AnthropicOAuthToken));
    if (!token) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *access_token  = cJSON_GetObjectItem(oauth, "accessToken");
    cJSON *refresh_token = cJSON_GetObjectItem(oauth, "refreshToken");
    cJSON *expires_at    = cJSON_GetObjectItem(oauth, "expiresAt");

    if (access_token  && cJSON_IsString(access_token))
        token->access_token  = strdup(access_token->valuestring);
    if (refresh_token && cJSON_IsString(refresh_token))
        token->refresh_token = strdup(refresh_token->valuestring);
    if (expires_at && cJSON_IsNumber(expires_at)) {
        /* expiresAt is in milliseconds */
        token->expires_at = (time_t)(expires_at->valuedouble / 1000.0);
    }

    cJSON_Delete(root);

    if (!token->access_token) {
        LOG_WARN("[Anthropic OAuth] credentials.json missing accessToken");
        anthropic_oauth_token_free(token);
        return NULL;
    }

    LOG_DEBUG("[Anthropic OAuth] Loaded token from disk (expires_at: %ld)",
              (long)token->expires_at);
    return token;
}

/**
 * Save OAuth token back to .credentials.json (0600 permissions).
 * Preserves the claudeAiOauth wrapper expected by the Claude CLI.
 */
static int save_token_to_disk(const AnthropicOAuthToken *token) {
    if (!token || !token->access_token) return -1;

    char *path = get_credentials_file_path();
    if (!path) return -1;

    /* Ensure parent directory exists */
    char dir[PATH_MAX];
    strlcpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        if (mkdir_p_with_mode(dir, 0700) != 0) {
            LOG_ERROR("[Anthropic OAuth] Failed to create credentials directory");
            free(path);
            return -1;
        }
    }

    cJSON *root  = cJSON_CreateObject();
    cJSON *oauth = cJSON_CreateObject();
    if (!root || !oauth) {
        cJSON_Delete(root);
        cJSON_Delete(oauth);
        free(path);
        return -1;
    }

    cJSON_AddStringToObject(oauth, "accessToken", token->access_token);
    if (token->refresh_token)
        cJSON_AddStringToObject(oauth, "refreshToken", token->refresh_token);
    /* Store expiresAt in milliseconds to match Claude CLI format */
    cJSON_AddNumberToObject(oauth, "expiresAt", (double)token->expires_at * 1000.0);

    cJSON_AddItemToObject(root, "claudeAiOauth", oauth);

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json_str) {
        free(path);
        return -1;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    free(path);
    if (fd < 0) {
        LOG_ERROR("[Anthropic OAuth] Failed to open credentials file for writing: %s",
                  strerror(errno));
        free(json_str);
        return -1;
    }

    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        free(json_str);
        return -1;
    }

    fprintf(f, "%s\n", json_str);
    fclose(f);
    free(json_str);

    LOG_DEBUG("[Anthropic OAuth] Token saved to disk");
    return 0;
}

/**
 * Reload from disk if the on-disk token is newer than the in-memory one.
 * Handles multi-process cases (subagents, Claude CLI background refresh).
 *
 * @return 1 if a new token was loaded, 0 otherwise
 */
static int reload_token_from_disk_if_newer(AnthropicOAuthManager *manager) {
    if (!manager) return 0;

    AnthropicOAuthToken *disk = load_token_from_disk();
    if (!disk) return 0;

    int loaded = 0;

    if (!manager->token) {
        manager->token = disk;
        loaded = 1;
        LOG_INFO("[Anthropic OAuth] Loaded token from disk (no in-memory token)");
    } else {
        int refresh_differs = 1;
        if (manager->token->refresh_token && disk->refresh_token) {
            refresh_differs = strcmp(manager->token->refresh_token,
                                     disk->refresh_token) != 0;
        }
        int expires_later = disk->expires_at > manager->token->expires_at;

        if (refresh_differs || expires_later) {
            AnthropicOAuthToken *old = manager->token;
            manager->token = disk;
            anthropic_oauth_token_free(old);
            loaded = 1;
            LOG_INFO("[Anthropic OAuth] Reloaded newer token from disk");
        } else {
            anthropic_oauth_token_free(disk);
        }
    }
    return loaded;
}

/* ============================================================================
 * HTTP Helpers
 * ============================================================================ */

/**
 * POST application/x-www-form-urlencoded body to url.
 * Returns response body (caller must free), or NULL on error.
 */
static char *http_post_form(const char *url, const char *body,
                             long *http_status_out) {
    if (!url) return NULL;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers,
                                "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, "Accept: application/json");

    HttpRequest req = {0};
    req.url                = url;
    req.method             = "POST";
    req.body               = body;
    req.headers            = headers;
    req.connect_timeout_ms = 30000;
    req.total_timeout_ms   = 60000;

    HttpResponse *resp = http_client_execute(&req, NULL, NULL);
    curl_slist_free_all(headers);

    if (!resp) {
        LOG_ERROR("[Anthropic OAuth] HTTP POST to %s failed (no response)", url);
        return NULL;
    }

    if (http_status_out) {
        *http_status_out = resp->status_code;
    }

    char *result = NULL;
    if (resp->body) {
        result = strdup(resp->body);
    }

    if (resp->error_message) {
        LOG_ERROR("[Anthropic OAuth] HTTP error: %s", resp->error_message);
    }

    http_response_free(resp);
    return result;
}

/* ============================================================================
 * Token Refresh
 * ============================================================================ */

/**
 * Refresh the access token using the stored refresh_token.
 * IMPORTANT: refresh_token rotates on each use — always save the new one!
 *
 * Reloads from disk first to handle multi-process scenarios.
 * Returns a new token on success, NULL if refresh failed or not needed.
 */
static AnthropicOAuthToken *refresh_token_internal(AnthropicOAuthManager *manager,
                                                    int force) {
    if (!manager) return NULL;

    /* Reload from disk first: another process may have refreshed already */
    reload_token_from_disk_if_newer(manager);

    if (!manager->token || !manager->token->refresh_token) {
        LOG_ERROR("[Anthropic OAuth] No refresh token available");
        return NULL;
    }

    /* Skip if not needed (unless forced) */
    if (!force) {
        time_t remaining = manager->token->expires_at - time(NULL);
        if (remaining >= ANTHROPIC_TOKEN_REFRESH_THRESHOLD_SECONDS) {
            LOG_DEBUG("[Anthropic OAuth] Token still fresh (%ld s remaining)",
                      (long)remaining);
            return NULL;
        }
    }

    /* Build refresh request body */
    char body[2048];
    snprintf(body, sizeof(body),
             "grant_type=refresh_token"
             "&refresh_token=%s",
             manager->token->refresh_token);

    long status = 0;
    char *response = http_post_form(ANTHROPIC_OAUTH_TOKEN_ENDPOINT, body, &status);

    /* Wipe sensitive stack data */
    explicit_bzero(body, sizeof(body));

    if (!response) {
        LOG_ERROR("[Anthropic OAuth] Token refresh request failed");
        return NULL;
    }

    /* 401: another process may have rotated the token already */
    if (status == 401) {
        LOG_WARN("[Anthropic OAuth] Refresh returned 401, checking disk for newer token...");
        free(response);
        if (reload_token_from_disk_if_newer(manager)) {
            time_t remaining = manager->token
                               ? manager->token->expires_at - time(NULL) : 0;
            if (remaining > 0) {
                LOG_INFO("[Anthropic OAuth] Found valid token on disk after 401");
                return NULL;
            }
        }
        return NULL;
    }

    if (status != 200) {
        LOG_ERROR("[Anthropic OAuth] Token refresh failed with status %ld: %s",
                  status, response);
        free(response);
        return NULL;
    }

    cJSON *json = cJSON_Parse(response);
    free(response);
    if (!json) {
        LOG_ERROR("[Anthropic OAuth] Failed to parse refresh response");
        return NULL;
    }

    cJSON *error = cJSON_GetObjectItem(json, "error");
    if (error && cJSON_IsString(error)) {
        LOG_ERROR("[Anthropic OAuth] Refresh error: %s", error->valuestring);
        cJSON_Delete(json);
        return NULL;
    }

    AnthropicOAuthToken *token = calloc(1, sizeof(AnthropicOAuthToken));
    if (!token) {
        cJSON_Delete(json);
        return NULL;
    }

    cJSON *access_token  = cJSON_GetObjectItem(json, "access_token");
    cJSON *refresh_token = cJSON_GetObjectItem(json, "refresh_token");
    cJSON *expires_in    = cJSON_GetObjectItem(json, "expires_in");

    if (access_token  && cJSON_IsString(access_token))
        token->access_token  = strdup(access_token->valuestring);
    if (refresh_token && cJSON_IsString(refresh_token))
        token->refresh_token = strdup(refresh_token->valuestring);
    if (expires_in && cJSON_IsNumber(expires_in))
        token->expires_at = time(NULL) + (time_t)expires_in->valueint;

    cJSON_Delete(json);

    if (!token->access_token || !token->refresh_token) {
        LOG_ERROR("[Anthropic OAuth] Refresh response missing required fields");
        anthropic_oauth_token_free(token);
        return NULL;
    }

    LOG_INFO("[Anthropic OAuth] Token refreshed (new expires_at: %ld)",
             (long)token->expires_at);
    return token;
}

/* ============================================================================
 * Background Refresh Thread
 * ============================================================================ */

static void *refresh_thread_func(void *arg) {
    AnthropicOAuthManager *manager = (AnthropicOAuthManager *)arg;
    if (!manager) return NULL;

    LOG_DEBUG("[Anthropic OAuth] Refresh thread started");

    while (manager->refresh_thread_running) {
        for (int i = 0;
             i < ANTHROPIC_TOKEN_REFRESH_INTERVAL_SECONDS && manager->refresh_thread_running;
             i++) {
            sleep(1);
        }
        if (!manager->refresh_thread_running) break;

        pthread_mutex_lock(&manager->token_mutex);

        if (manager->token && manager->token->expires_at > 0) {
            time_t remaining = manager->token->expires_at - time(NULL);
            if (remaining < ANTHROPIC_TOKEN_REFRESH_THRESHOLD_SECONDS) {
                LOG_INFO("[Anthropic OAuth] Background refresh (expires in %ld s)",
                         (long)remaining);
                AnthropicOAuthToken *new_token =
                    refresh_token_internal(manager, 0);
                if (new_token) {
                    AnthropicOAuthToken *old = manager->token;
                    manager->token = new_token;
                    if (save_token_to_disk(new_token) != 0) {
                        LOG_WARN("[Anthropic OAuth] Failed to save refreshed token");
                    }
                    anthropic_oauth_token_free(old);
                    LOG_INFO("[Anthropic OAuth] Background refresh successful");
                }
            }
        }

        pthread_mutex_unlock(&manager->token_mutex);
    }

    LOG_DEBUG("[Anthropic OAuth] Refresh thread stopped");
    return NULL;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

/**
 * Create and initialize an Anthropic OAuth manager.
 */
AnthropicOAuthManager *anthropic_oauth_manager_create(void) {
    AnthropicOAuthManager *manager = calloc(1, sizeof(AnthropicOAuthManager));
    if (!manager) {
        LOG_ERROR("[Anthropic OAuth] Failed to allocate manager");
        return NULL;
    }

    if (pthread_mutex_init(&manager->token_mutex, NULL) != 0) {
        LOG_ERROR("[Anthropic OAuth] Failed to initialize mutex");
        free(manager);
        return NULL;
    }

    /* Try to load existing token */
    manager->token = load_token_from_disk();
    if (manager->token) {
        LOG_INFO("[Anthropic OAuth] Loaded existing token from disk");
    } else {
        LOG_WARN("[Anthropic OAuth] No credentials found — "
                 "run 'claude auth login' or configure FileSurf to inject tokens");
    }

    return manager;
}

/**
 * Destroy the OAuth manager.
 */
void anthropic_oauth_manager_destroy(AnthropicOAuthManager *manager) {
    if (!manager) return;

    anthropic_oauth_stop_refresh_thread(manager);

    pthread_mutex_lock(&manager->token_mutex);
    anthropic_oauth_token_free(manager->token);
    manager->token = NULL;
    pthread_mutex_unlock(&manager->token_mutex);

    pthread_mutex_destroy(&manager->token_mutex);
    free(manager);

    LOG_DEBUG("[Anthropic OAuth] Manager destroyed");
}

/**
 * Check if we have a valid (or refreshable) token.
 */
int anthropic_oauth_is_authenticated(AnthropicOAuthManager *manager) {
    if (!manager) return 0;

    pthread_mutex_lock(&manager->token_mutex);
    int authenticated = (manager->token != NULL &&
                         manager->token->access_token != NULL);

    /* If we have a refresh token, we can always re-authenticate */
    if (!authenticated && manager->token && manager->token->refresh_token) {
        authenticated = 1;
    }

    /* If access token is expired and there's no refresh token, not authenticated */
    if (authenticated && !manager->token->refresh_token) {
        time_t now = time(NULL);
        if (manager->token->expires_at > 0 &&
            manager->token->expires_at <= now) {
            authenticated = 0;
        }
    }

    pthread_mutex_unlock(&manager->token_mutex);
    return authenticated;
}

/**
 * Get current access token, refreshing if needed.
 */
const char *anthropic_oauth_get_access_token(AnthropicOAuthManager *manager) {
    if (!manager) return NULL;

    pthread_mutex_lock(&manager->token_mutex);

    /* Try disk if no in-memory token */
    if (!manager->token || !manager->token->access_token) {
        reload_token_from_disk_if_newer(manager);
        if (!manager->token || !manager->token->access_token) {
            pthread_mutex_unlock(&manager->token_mutex);
            return NULL;
        }
    }

    /* Always reload to catch updates from other processes */
    reload_token_from_disk_if_newer(manager);

    /* Refresh if expiring soon */
    time_t now = time(NULL);
    time_t remaining = manager->token->expires_at > 0
                       ? manager->token->expires_at - now
                       : (time_t)INT_MAX;

    if (remaining < ANTHROPIC_TOKEN_REFRESH_THRESHOLD_SECONDS) {
        LOG_INFO("[Anthropic OAuth] Token expiring in %ld s, refreshing...",
                 (long)remaining);
        AnthropicOAuthToken *new_token = refresh_token_internal(manager, 0);
        if (new_token) {
            AnthropicOAuthToken *old = manager->token;
            manager->token = new_token;
            if (save_token_to_disk(new_token) != 0) {
                LOG_WARN("[Anthropic OAuth] Failed to save refreshed token");
            }
            anthropic_oauth_token_free(old);
        } else {
            /* Re-check: refresh_token_internal may have reloaded from disk */
            if (manager->token && manager->token->expires_at > 0) {
                remaining = manager->token->expires_at - now;
            }
            if (remaining <= 0) {
                LOG_ERROR("[Anthropic OAuth] Token expired and refresh failed");
                pthread_mutex_unlock(&manager->token_mutex);
                return NULL;
            }
        }
    }

    const char *tok = manager->token->access_token;
    pthread_mutex_unlock(&manager->token_mutex);
    return tok;
}

/**
 * Force token refresh (public API).
 */
int anthropic_oauth_refresh(AnthropicOAuthManager *manager, int force) {
    if (!manager) return -1;

    pthread_mutex_lock(&manager->token_mutex);

    reload_token_from_disk_if_newer(manager);

    if (!manager->token || !manager->token->refresh_token) {
        pthread_mutex_unlock(&manager->token_mutex);
        LOG_ERROR("[Anthropic OAuth] No refresh token available");
        return -1;
    }

    if (!force) {
        time_t remaining = manager->token->expires_at - time(NULL);
        if (remaining >= ANTHROPIC_TOKEN_REFRESH_THRESHOLD_SECONDS) {
            LOG_INFO("[Anthropic OAuth] Token already fresh, skipping refresh");
            pthread_mutex_unlock(&manager->token_mutex);
            return 0;
        }
    }

    AnthropicOAuthToken *new_token = refresh_token_internal(manager, force);
    if (!new_token) {
        time_t remaining = manager->token
                           ? manager->token->expires_at - time(NULL) : 0;
        if (remaining >= ANTHROPIC_TOKEN_REFRESH_THRESHOLD_SECONDS) {
            LOG_INFO("[Anthropic OAuth] Token refreshed by another process");
            pthread_mutex_unlock(&manager->token_mutex);
            return 0;
        }
        pthread_mutex_unlock(&manager->token_mutex);
        return -1;
    }

    AnthropicOAuthToken *old = manager->token;
    manager->token = new_token;
    if (save_token_to_disk(new_token) != 0) {
        LOG_WARN("[Anthropic OAuth] Failed to save refreshed token");
    }
    anthropic_oauth_token_free(old);

    pthread_mutex_unlock(&manager->token_mutex);
    return 0;
}

/**
 * Reload token from disk (public API for 401 recovery).
 */
int anthropic_oauth_reload_from_disk(AnthropicOAuthManager *manager) {
    if (!manager) return 0;

    pthread_mutex_lock(&manager->token_mutex);
    int result = reload_token_from_disk_if_newer(manager);
    pthread_mutex_unlock(&manager->token_mutex);

    if (result) {
        LOG_INFO("[Anthropic OAuth] Reloaded token from disk");
    }
    return result;
}

/**
 * Start background token refresh thread.
 */
int anthropic_oauth_start_refresh_thread(AnthropicOAuthManager *manager) {
    if (!manager) return -1;

    if (manager->refresh_thread_started) {
        LOG_DEBUG("[Anthropic OAuth] Refresh thread already running");
        return 0;
    }

    manager->refresh_thread_running = 1;
    if (pthread_create(&manager->refresh_thread, NULL,
                       refresh_thread_func, manager) != 0) {
        LOG_ERROR("[Anthropic OAuth] Failed to create refresh thread");
        manager->refresh_thread_running = 0;
        return -1;
    }
    manager->refresh_thread_started = 1;
    LOG_INFO("[Anthropic OAuth] Background refresh thread started");
    return 0;
}

/**
 * Stop background token refresh thread.
 */
void anthropic_oauth_stop_refresh_thread(AnthropicOAuthManager *manager) {
    if (!manager || !manager->refresh_thread_started) return;

    manager->refresh_thread_running = 0;
    pthread_join(manager->refresh_thread, NULL);
    manager->refresh_thread_started = 0;
    LOG_DEBUG("[Anthropic OAuth] Refresh thread stopped");
}

/* ============================================================================
 * Struct Freeing
 * ============================================================================ */

void anthropic_oauth_token_free(AnthropicOAuthToken *token) {
    if (!token) return;

    if (token->access_token) {
        explicit_bzero(token->access_token, strlen(token->access_token));
        free(token->access_token);
    }
    if (token->refresh_token) {
        explicit_bzero(token->refresh_token, strlen(token->refresh_token));
        free(token->refresh_token);
    }

    free(token);
}
