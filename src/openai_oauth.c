/*
 * openai_oauth.c - OpenAI OAuth 2.0 Device Authorization
 *
 * Implements OAuth 2.0 Device Authorization Grant (RFC 8628) for
 * OpenAI ChatGPT subscription authentication.
 *
 * Flow:
 *   1. POST /oauth/device/code  → get user_code + device_code
 *   2. Show user_code / URL to user (open browser if possible)
 *   3. Poll POST /oauth/token until user authorizes
 *   4. Store access_token + refresh_token in ~/.openai/auth.json
 *   5. Use access_token as "Authorization: Bearer <token>" on api.openai.com
 *   6. Background thread refreshes the token before it expires
 *
 * Token storage: ~/.openai/auth.json  (0600 permissions)
 * Device ID:     not needed (unlike Kimi which uses device headers)
 */

#define _POSIX_C_SOURCE 200809L

#include "openai_oauth.h"
#include "logger.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
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

static char              *get_codex_dir(void);
static char              *get_token_file_path(void);
static char              *get_parent_dir(const char *path);
static int                mkdir_p_with_mode(const char *path, mode_t mode);
static OpenAIOAuthToken  *load_token_from_disk(void);
static int                save_token_to_disk(const OpenAIOAuthToken *token);
static int                reload_token_from_disk_if_newer(OpenAIOAuthManager *manager);
static char              *get_lock_file_path(void);
static int                acquire_refresh_lock(void);
static int                acquire_refresh_lock_nb(void);
static void               release_refresh_lock(int fd);
static OpenAIDeviceAuth  *request_device_authorization(void);
static OpenAIOAuthToken  *poll_for_token(const OpenAIDeviceAuth *auth);
static OpenAIOAuthToken  *refresh_token_internal(OpenAIOAuthManager *manager, int force);
static void              *refresh_thread_func(void *arg);
static void               oauth_display_message(OpenAIOAuthManager *manager,
                                                 const char *msg, int is_error);
static char              *http_post_form(const char *url, const char *body,
                                          long *http_status_out);
static void               revoke_token(const char *token_value,
                                        const char *token_type_hint);

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
 * Return the ~/.codex directory path (caller must free).
 */
static char *get_codex_dir(void) {
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        struct passwd *pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }
    if (!home || home[0] == '\0') {
        LOG_ERROR("[OpenAI OAuth] Cannot determine home directory");
        return NULL;
    }

    char *path = malloc(PATH_MAX);
    if (!path) return NULL;

    if (snprintf(path, PATH_MAX, "%s/.codex", home) >= PATH_MAX) {
        free(path);
        return NULL;
    }
    return path;
}

static char *get_parent_dir(const char *path) {
    if (!path) return NULL;

    const char *slash = strrchr(path, '/');
    if (!slash) return strdup(".");
    if (slash == path) return strdup("/");

    size_t len = (size_t)(slash - path);
    char *dir = malloc(len + 1);
    if (!dir) return NULL;

    memcpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}

/**
 * Return the full path to the auth token file (caller must free).
 * Path: ~/.codex/auth.json (or $OPENAI_OAUTH_PATH if set)
 *
 * Environment variable OPENAI_OAUTH_PATH can be used to override the default
 * location. This is useful for sharing OAuth credentials across machines or
 * using a custom location.
 */
static char *get_token_file_path(void) {
    const char *env_path = getenv("OPENAI_OAUTH_PATH");
    if (env_path && env_path[0] != '\0') {
        LOG_DEBUG("[OpenAI OAuth] Using token path from OPENAI_OAUTH_PATH: %s", env_path);
        return strdup(env_path);
    }

    char *codex_dir = get_codex_dir();
    if (!codex_dir) return NULL;

    char *codex_path = malloc(PATH_MAX);
    if (!codex_path) {
        free(codex_dir);
        return NULL;
    }
    if (snprintf(codex_path, PATH_MAX, "%s/auth.json", codex_dir) >= PATH_MAX) {
        free(codex_path);
        free(codex_dir);
        return NULL;
    }
    free(codex_dir);
    return codex_path;
}

/**
 * Return the full path to the refresh lock file (caller must free).
 * Path: same directory as auth.json, with .lock extension
 *
 * This lock coordinates cross-process token refresh so that only one
 * process performs the network refresh at a time.  Others wait on the
 * lock and then reload the fresh token from disk.
 */
static char *get_lock_file_path(void) {
    // Get the token file path first
    char *token_path = get_token_file_path();
    if (!token_path) return NULL;

    // Find the last dot to replace extension, or append .lock
    char *path = malloc(PATH_MAX);
    if (!path) {
        free(token_path);
        return NULL;
    }

    // Replace .json extension with .lock, or append .lock if no extension
    char *last_dot = strrchr(token_path, '.');
    if (last_dot && strcmp(last_dot, ".json") == 0) {
        size_t base_len = (size_t)(last_dot - token_path);
        if (base_len + 5 >= PATH_MAX) {  // +5 for ".lock\0"
            free(path);
            free(token_path);
            return NULL;
        }
        memcpy(path, token_path, base_len);
        strcpy(path + base_len, ".lock");
    } else {
        if (snprintf(path, PATH_MAX, "%s.lock", token_path) >= PATH_MAX) {
            free(path);
            free(token_path);
            return NULL;
        }
    }

    free(token_path);
    return path;
}

/**
 * Open/create the refresh lock file and acquire an exclusive flock.
 * Blocks until the lock is obtained or until a 10-second timeout expires.
 *
 * @return  fd >= 0 on success (caller must release_refresh_lock(fd)),
 *          -1 on error or timeout.
 */
static int acquire_refresh_lock(void) {
    char *path = get_lock_file_path();
    if (!path) return -1;

    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    free(path);
    if (fd < 0) {
        LOG_WARN("[OpenAI OAuth] Cannot open lock file: %s", strerror(errno));
        return -1;
    }

    /* Try non-blocking first; if busy poll with short sleeps up to 10 s */
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        return fd;
    }

    LOG_DEBUG("[OpenAI OAuth] Refresh lock busy, waiting...");

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 10; /* 10-second hard timeout */

    struct timespec sleep_ts = {0, 50 * 1000 * 1000}; /* 50 ms poll interval */
    while (1) {
        nanosleep(&sleep_ts, NULL);

        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            LOG_DEBUG("[OpenAI OAuth] Acquired refresh lock after wait");
            return fd;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
            LOG_WARN("[OpenAI OAuth] Timed out waiting for refresh lock");
            close(fd);
            return -1;
        }
    }
}

/**
 * Try to acquire the refresh lock without blocking.
 *
 * @return  fd >= 0 if we obtained the lock (caller must release_refresh_lock),
 *          -1 if the lock is held by another process (refresh in progress).
 */
static int acquire_refresh_lock_nb(void) {
    char *path = get_lock_file_path();
    if (!path) return -1;

    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    free(path);
    if (fd < 0) {
        LOG_WARN("[OpenAI OAuth] Cannot open lock file: %s", strerror(errno));
        return -1;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        return fd; /* We own the lock */
    }

    /* Lock is held — another process is refreshing */
    close(fd);
    return -1;
}

/**
 * Release a previously acquired refresh lock.
 */
static void release_refresh_lock(int fd) {
    if (fd < 0) return;
    flock(fd, LOCK_UN);
    close(fd);
}

/* ============================================================================
 * Token Storage
 * ============================================================================ */

/**
 * Load OAuth token from ~/.codex/auth.json.
 * Returns NULL if no valid token exists.
 */
static OpenAIOAuthToken *load_token_from_disk(void) {
    char *path = get_token_file_path();
    if (!path) return NULL;

    FILE *f = fopen(path, "r");
    if (!f) {
        free(path);
        LOG_DEBUG("[OpenAI OAuth] No token file found");
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

    cJSON *json = cJSON_Parse(content);
    free(content);
    if (!json) {
        free(path);
        LOG_WARN("[OpenAI OAuth] Failed to parse token file");
        return NULL;
    }

    OpenAIOAuthToken *token = calloc(1, sizeof(OpenAIOAuthToken));
    if (!token) {
        cJSON_Delete(json);
        return NULL;
    }

    cJSON *token_root = json;
    cJSON *tokens = cJSON_GetObjectItem(json, "tokens");
    if (tokens && cJSON_IsObject(tokens)) {
        token_root = tokens;
    }

    cJSON *access_token  = cJSON_GetObjectItem(token_root, "access_token");
    cJSON *refresh_token = cJSON_GetObjectItem(token_root, "refresh_token");
    cJSON *expires_at    = cJSON_GetObjectItem(token_root, "expires_at");
    cJSON *token_type    = cJSON_GetObjectItem(token_root, "token_type");
    cJSON *scope         = cJSON_GetObjectItem(token_root, "scope");
    cJSON *id_token      = cJSON_GetObjectItem(token_root, "id_token");
    cJSON *account_id    = cJSON_GetObjectItem(token_root, "account_id");

    if (access_token  && cJSON_IsString(access_token))  token->access_token  = strdup(access_token->valuestring);
    if (refresh_token && cJSON_IsString(refresh_token)) token->refresh_token = strdup(refresh_token->valuestring);
    if (expires_at    && cJSON_IsNumber(expires_at))    token->expires_at    = (time_t)expires_at->valuedouble;
    if (token_type    && cJSON_IsString(token_type))    token->token_type    = strdup(token_type->valuestring);
    if (scope         && cJSON_IsString(scope))         token->scope         = strdup(scope->valuestring);
    if (id_token      && cJSON_IsString(id_token))      token->id_token      = strdup(id_token->valuestring);
    cJSON *client_id_item = cJSON_GetObjectItem(token_root, "client_id");
    if (client_id_item && cJSON_IsString(client_id_item)) token->client_id     = strdup(client_id_item->valuestring);
    if (account_id && cJSON_IsString(account_id)) token->account_id = strdup(account_id->valuestring);

    cJSON_Delete(json);
    free(path);

    if (!token->access_token) {
        LOG_WARN("[OpenAI OAuth] Token file missing access_token");
        openai_oauth_token_free(token);
        return NULL;
    }

    LOG_DEBUG("[OpenAI OAuth] Loaded token from disk (expires_at: %ld)",
              (long)token->expires_at);
    return token;
}

/**
 * Save OAuth token to ~/.codex/auth.json with 0600 permissions.
 */
static int save_token_to_disk(const OpenAIOAuthToken *token) {
    if (!token || !token->access_token) return -1;

    char *path = get_token_file_path();
    if (!path) return -1;

    char *dir = get_parent_dir(path);
    if (!dir) {
        free(path);
        return -1;
    }

    if (mkdir_p_with_mode(dir, 0700) != 0) {
        LOG_ERROR("[OpenAI OAuth] Failed to create token directory");
        free(dir);
        free(path);
        return -1;
    }
    free(dir);

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        free(path);
        return -1;
    }

    cJSON *tokens = cJSON_CreateObject();
    if (!tokens) {
        cJSON_Delete(json);
        free(path);
        return -1;
    }
    cJSON_AddStringToObject(json, "auth_mode", "chatgpt");
    if (token->access_token) cJSON_AddStringToObject(tokens, "access_token", token->access_token);
    if (token->refresh_token) cJSON_AddStringToObject(tokens, "refresh_token", token->refresh_token);
    if (token->id_token) cJSON_AddStringToObject(tokens, "id_token", token->id_token);
    if (token->account_id) cJSON_AddStringToObject(tokens, "account_id", token->account_id);
    cJSON_AddItemToObject(json, "tokens", tokens);

    char *json_str = cJSON_Print(json);
    cJSON_Delete(json);
    if (!json_str) {
        free(path);
        return -1;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        LOG_ERROR("[OpenAI OAuth] Failed to open token file for writing: %s",
                  strerror(errno));
        free(json_str);
        free(path);
        return -1;
    }

    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        free(json_str);
        free(path);
        return -1;
    }

    fprintf(f, "%s\n", json_str);
    fclose(f);
    free(json_str);

    LOG_DEBUG("[OpenAI OAuth] Token saved to disk: %s", path);
    free(path);
    return 0;
}

/**
 * Reload from disk if the on-disk token is newer than the in-memory one.
 * Handles multi-process cases (e.g., subagent refreshed the token).
 *
 * @return 1 if a new token was loaded, 0 otherwise
 */
static int reload_token_from_disk_if_newer(OpenAIOAuthManager *manager) {
    if (!manager) return 0;

    OpenAIOAuthToken *disk = load_token_from_disk();
    if (!disk) return 0;

    int loaded = 0;

    if (!manager->token) {
        manager->token = disk;
        loaded = 1;
        LOG_INFO("[OpenAI OAuth] Loaded token from disk (no in-memory token)");
    } else {
        int refresh_differs = 1;
        if (manager->token->refresh_token && disk->refresh_token) {
            refresh_differs = strcmp(manager->token->refresh_token,
                                     disk->refresh_token) != 0;
        }
        int expires_later = disk->expires_at > manager->token->expires_at;

        if (refresh_differs || expires_later) {
            OpenAIOAuthToken *old = manager->token;
            manager->token = disk;
            openai_oauth_token_free(old);
            loaded = 1;
            LOG_INFO("[OpenAI OAuth] Reloaded newer token from disk");
        } else {
            openai_oauth_token_free(disk);
        }
    }
    return loaded;
}

/* ============================================================================
 * HTTP Helpers
 * ============================================================================ */

/**
 * POST application/x-www-form-urlencoded body to url.
 * Returns response body string (caller must free), or NULL on error.
 */
static char *http_post_form(const char *url, const char *body,
                             long *http_status_out) {
    if (!url) return NULL;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers,
                                "Content-Type: application/x-www-form-urlencoded");
    /* Standard Accept header */
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
        LOG_ERROR("[OpenAI OAuth] HTTP POST to %s failed (no response)", url);
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
        LOG_ERROR("[OpenAI OAuth] HTTP error: %s", resp->error_message);
    }

    http_response_free(resp);
    return result;
}

/* ============================================================================
 * Device Authorization Flow
 * ============================================================================ */

/**
 * Request device authorization from OpenAI OAuth server.
 * Returns a DeviceAuth struct the caller must free with openai_device_auth_free().
 */
static OpenAIDeviceAuth *request_device_authorization(void) {
    const char *body =
        "client_id=" OPENAI_OAUTH_CLIENT_ID
        "&scope=" "openid%20profile%20email%20offline_access"
        "&audience=" "https%3A%2F%2Fapi.openai.com%2Fv1";

    long status = 0;
    char *response = http_post_form(OPENAI_DEVICE_AUTH_ENDPOINT, body, &status);
    if (!response) {
        LOG_ERROR("[OpenAI OAuth] Device authorization request failed");
        return NULL;
    }

    if (status != 200) {
        LOG_ERROR("[OpenAI OAuth] Device auth returned status %ld: %s",
                  status, response);
        free(response);
        return NULL;
    }

    cJSON *json = cJSON_Parse(response);
    free(response);
    if (!json) {
        LOG_ERROR("[OpenAI OAuth] Failed to parse device auth response");
        return NULL;
    }

    OpenAIDeviceAuth *auth = calloc(1, sizeof(OpenAIDeviceAuth));
    if (!auth) {
        cJSON_Delete(json);
        return NULL;
    }

    cJSON *user_code       = cJSON_GetObjectItem(json, "user_code");
    cJSON *device_code     = cJSON_GetObjectItem(json, "device_code");
    cJSON *verification_uri          = cJSON_GetObjectItem(json, "verification_uri");
    cJSON *verification_uri_complete = cJSON_GetObjectItem(json, "verification_uri_complete");
    cJSON *expires_in      = cJSON_GetObjectItem(json, "expires_in");
    cJSON *interval        = cJSON_GetObjectItem(json, "interval");

    if (user_code && cJSON_IsString(user_code))
        auth->user_code = strdup(user_code->valuestring);
    if (device_code && cJSON_IsString(device_code))
        auth->device_code = strdup(device_code->valuestring);
    if (verification_uri && cJSON_IsString(verification_uri))
        auth->verification_uri = strdup(verification_uri->valuestring);
    if (verification_uri_complete && cJSON_IsString(verification_uri_complete))
        auth->verification_uri_complete = strdup(verification_uri_complete->valuestring);
    if (expires_in && cJSON_IsNumber(expires_in))
        auth->expires_in = expires_in->valueint;
    if (interval && cJSON_IsNumber(interval))
        auth->interval = interval->valueint;

    cJSON_Delete(json);

    if (!auth->user_code || !auth->device_code) {
        LOG_ERROR("[OpenAI OAuth] Device auth response missing required fields");
        openai_device_auth_free(auth);
        return NULL;
    }

    if (auth->expires_in <= 0) auth->expires_in = OPENAI_DEVICE_AUTH_TIMEOUT_SECONDS;
    if (auth->interval   <= 0) auth->interval   = 5;

    LOG_DEBUG("[OpenAI OAuth] Device auth: user_code=%s expires_in=%d",
              auth->user_code, auth->expires_in);
    return auth;
}

/**
 * Poll the token endpoint until the user authorizes or the device code expires.
 * Returns the token on success, NULL on error/timeout.
 */
static OpenAIOAuthToken *poll_for_token(const OpenAIDeviceAuth *auth) {
    if (!auth || !auth->device_code) return NULL;

    int interval = auth->interval > 0 ? auth->interval : 5;
    time_t start = time(NULL);
    int expires  = auth->expires_in > 0 ? auth->expires_in
                                        : OPENAI_DEVICE_AUTH_TIMEOUT_SECONDS;

    /* Build static part of the request body */
    char body[1024];
    snprintf(body, sizeof(body),
             "grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code"
             "&device_code=%s"
             "&client_id=%s",
             auth->device_code, OPENAI_OAUTH_CLIENT_ID);

    while (1) {
        if (time(NULL) - start > expires) {
            LOG_ERROR("[OpenAI OAuth] Device authorization timed out");
            return NULL;
        }

        sleep((unsigned int)interval);

        long status = 0;
        char *response = http_post_form(OPENAI_TOKEN_ENDPOINT, body, &status);
        if (!response) {
            LOG_WARN("[OpenAI OAuth] Token poll request failed, retrying...");
            continue;
        }

        cJSON *json = cJSON_Parse(response);
        free(response);
        if (!json) {
            LOG_WARN("[OpenAI OAuth] Failed to parse token poll response, retrying...");
            continue;
        }

        /* Check for OAuth error */
        cJSON *error = cJSON_GetObjectItem(json, "error");
        if (error && cJSON_IsString(error)) {
            const char *err = error->valuestring;

            if (strcmp(err, "authorization_pending") == 0) {
                LOG_DEBUG("[OpenAI OAuth] Authorization pending...");
                cJSON_Delete(json);
                continue;
            } else if (strcmp(err, "slow_down") == 0) {
                interval += 5;
                LOG_DEBUG("[OpenAI OAuth] slow_down: increasing interval to %d", interval);
                cJSON_Delete(json);
                continue;
            } else if (strcmp(err, "expired_token") == 0) {
                LOG_ERROR("[OpenAI OAuth] Device code expired");
                cJSON_Delete(json);
                return NULL;
            } else if (strcmp(err, "access_denied") == 0) {
                LOG_ERROR("[OpenAI OAuth] Authorization denied by user");
                cJSON_Delete(json);
                return NULL;
            } else {
                cJSON *desc = cJSON_GetObjectItem(json, "error_description");
                LOG_ERROR("[OpenAI OAuth] Token error: %s (%s)", err,
                          desc && cJSON_IsString(desc) ? desc->valuestring : "");
                cJSON_Delete(json);
                return NULL;
            }
        }

        /* Parse successful token response */
        OpenAIOAuthToken *token = calloc(1, sizeof(OpenAIOAuthToken));
        if (!token) {
            cJSON_Delete(json);
            return NULL;
        }

        cJSON *access_token  = cJSON_GetObjectItem(json, "access_token");
        cJSON *refresh_token = cJSON_GetObjectItem(json, "refresh_token");
        cJSON *expires_in_j  = cJSON_GetObjectItem(json, "expires_in");
        cJSON *token_type    = cJSON_GetObjectItem(json, "token_type");
        cJSON *scope         = cJSON_GetObjectItem(json, "scope");
        cJSON *id_token      = cJSON_GetObjectItem(json, "id_token");

        if (access_token  && cJSON_IsString(access_token))  token->access_token  = strdup(access_token->valuestring);
        if (refresh_token && cJSON_IsString(refresh_token)) token->refresh_token = strdup(refresh_token->valuestring);
        if (expires_in_j  && cJSON_IsNumber(expires_in_j))  token->expires_at    = time(NULL) + (time_t)expires_in_j->valueint;
        if (token_type    && cJSON_IsString(token_type))    token->token_type    = strdup(token_type->valuestring);
        if (scope         && cJSON_IsString(scope))         token->scope         = strdup(scope->valuestring);
        if (id_token      && cJSON_IsString(id_token))      token->id_token      = strdup(id_token->valuestring);

        cJSON_Delete(json);

        if (!token->access_token) {
            LOG_ERROR("[OpenAI OAuth] Token response missing access_token");
            openai_oauth_token_free(token);
            return NULL;
        }

        LOG_INFO("[OpenAI OAuth] Access token obtained successfully");
        return token;
    }
}

/* ============================================================================
 * Token Refresh
 * ============================================================================ */

/**
 * Refresh the access token using the refresh_token.
 * IMPORTANT: refresh_token rotates on each use — always save the new one!
 *
 * Reloads from disk first to handle multi-process scenarios.
 * Returns new token on success, NULL if refresh failed or not needed.
 */
static OpenAIOAuthToken *refresh_token_internal(OpenAIOAuthManager *manager, int force) {
    if (!manager) return NULL;

    /* First reload from disk: another process may have refreshed already */
    reload_token_from_disk_if_newer(manager);

    if (!manager->token || !manager->token->refresh_token) {
        LOG_ERROR("[OpenAI OAuth] No refresh token available");
        return NULL;
    }

    /* Skip if not needed (unless forced) */
    if (!force) {
        time_t remaining = manager->token->expires_at - time(NULL);
        if (remaining >= OPENAI_TOKEN_REFRESH_THRESHOLD_SECONDS) {
            LOG_DEBUG("[OpenAI OAuth] Token still fresh (%ld s remaining)",
                      (long)remaining);
            return NULL;
        }
    }

    /* Build refresh request body */
    /* Use client_id stored in token if available (e.g. codex-issued tokens) */
    const char *client_id_to_use = (manager->token->client_id && manager->token->client_id[0])
                                   ? manager->token->client_id
                                   : OPENAI_OAUTH_CLIENT_ID;
    char body[2048];
    snprintf(body, sizeof(body),
             "grant_type=refresh_token"
             "&refresh_token=%s"
             "&client_id=%s",
             manager->token->refresh_token, client_id_to_use);

    long status = 0;
    char *response = http_post_form(OPENAI_TOKEN_ENDPOINT, body, &status);

    /* Wipe sensitive stack data */
    explicit_bzero(body, sizeof(body));

    if (!response) {
        LOG_ERROR("[OpenAI OAuth] Token refresh request failed");
        return NULL;
    }

    /* 401: another process may have rotated the token */
    if (status == 401) {
        LOG_WARN("[OpenAI OAuth] Refresh returned 401, checking disk for newer token...");
        free(response);
        if (reload_token_from_disk_if_newer(manager)) {
            time_t remaining = manager->token ? manager->token->expires_at - time(NULL) : 0;
            if (remaining > 0) {
                LOG_INFO("[OpenAI OAuth] Found valid token on disk after 401");
                return NULL;
            }
        }
        return NULL;
    }

    if (status != 200) {
        LOG_ERROR("[OpenAI OAuth] Token refresh failed with status %ld: %s",
                  status, response);
        free(response);
        return NULL;
    }

    cJSON *json = cJSON_Parse(response);
    free(response);
    if (!json) {
        LOG_ERROR("[OpenAI OAuth] Failed to parse refresh response");
        return NULL;
    }

    cJSON *error = cJSON_GetObjectItem(json, "error");
    if (error && cJSON_IsString(error)) {
        LOG_ERROR("[OpenAI OAuth] Refresh error: %s", error->valuestring);
        cJSON_Delete(json);
        return NULL;
    }

    OpenAIOAuthToken *token = calloc(1, sizeof(OpenAIOAuthToken));
    if (!token) {
        cJSON_Delete(json);
        return NULL;
    }

    cJSON *access_token  = cJSON_GetObjectItem(json, "access_token");
    cJSON *refresh_token = cJSON_GetObjectItem(json, "refresh_token");
    cJSON *expires_in    = cJSON_GetObjectItem(json, "expires_in");
    cJSON *token_type    = cJSON_GetObjectItem(json, "token_type");
    cJSON *scope         = cJSON_GetObjectItem(json, "scope");
    cJSON *id_token      = cJSON_GetObjectItem(json, "id_token");

    if (access_token  && cJSON_IsString(access_token))  token->access_token  = strdup(access_token->valuestring);
    if (refresh_token && cJSON_IsString(refresh_token)) token->refresh_token = strdup(refresh_token->valuestring);
    if (expires_in    && cJSON_IsNumber(expires_in))    token->expires_at    = time(NULL) + (time_t)expires_in->valueint;
    if (token_type    && cJSON_IsString(token_type))    token->token_type    = strdup(token_type->valuestring);
    if (scope         && cJSON_IsString(scope))         token->scope         = strdup(scope->valuestring);
    if (id_token      && cJSON_IsString(id_token))      token->id_token      = strdup(id_token->valuestring);

    cJSON_Delete(json);

    if (!token->access_token || !token->refresh_token) {
        LOG_ERROR("[OpenAI OAuth] Refresh response missing required fields");
        openai_oauth_token_free(token);
        return NULL;
    }

    /* Carry over client_id from old token so future refreshes use same client */
    if (!token->client_id && manager->token && manager->token->client_id) {
        token->client_id = strdup(manager->token->client_id);
    }

    LOG_INFO("[OpenAI OAuth] Token refreshed (new expires_at: %ld)",
             (long)token->expires_at);
    return token;
}

/* ============================================================================
 * Background Refresh Thread
 * ============================================================================ */

static void *refresh_thread_func(void *arg) {
    OpenAIOAuthManager *manager = (OpenAIOAuthManager *)arg;
    if (!manager) return NULL;

    LOG_DEBUG("[OpenAI OAuth] Refresh thread started");

    while (manager->refresh_thread_running) {
        for (int i = 0;
             i < OPENAI_TOKEN_REFRESH_INTERVAL_SECONDS && manager->refresh_thread_running;
             i++) {
            sleep(1);
        }
        if (!manager->refresh_thread_running) break;

        pthread_mutex_lock(&manager->token_mutex);

        if (manager->token && manager->token->expires_at > 0) {
            time_t remaining = manager->token->expires_at - time(NULL);
            if (remaining < OPENAI_TOKEN_REFRESH_THRESHOLD_SECONDS) {
                LOG_INFO("[OpenAI OAuth] Background refresh (expires in %ld s)",
                         (long)remaining);
                /* Acquire cross-process refresh lock before the network call.
                 * If another process is already refreshing, it will finish and
                 * write the new token to disk; we skip our own network refresh
                 * and reload from disk instead. */
                int lock_fd = acquire_refresh_lock_nb();
                if (lock_fd < 0) {
                    /* Another process holds the lock — wait for it, then reload */
                    LOG_INFO("[OpenAI OAuth] Background refresh deferred (another process refreshing)");
                    pthread_mutex_unlock(&manager->token_mutex);
                    lock_fd = acquire_refresh_lock();
                    if (lock_fd >= 0) {
                        release_refresh_lock(lock_fd);
                    }
                    pthread_mutex_lock(&manager->token_mutex);
                    reload_token_from_disk_if_newer(manager);
                } else {
                    OpenAIOAuthToken *new_token = refresh_token_internal(manager, 0);
                    release_refresh_lock(lock_fd);
                    if (new_token) {
                        OpenAIOAuthToken *old = manager->token;
                        manager->token = new_token;
                        if (save_token_to_disk(new_token) != 0) {
                            LOG_WARN("[OpenAI OAuth] Failed to save refreshed token");
                        }
                        openai_oauth_token_free(old);
                        LOG_INFO("[OpenAI OAuth] Background refresh successful");
                    }
                }
            }
        }

        pthread_mutex_unlock(&manager->token_mutex);
    }

    LOG_DEBUG("[OpenAI OAuth] Refresh thread stopped");
    return NULL;
}

/* ============================================================================
 * Message Display Helper
 * ============================================================================ */

static void oauth_display_message(OpenAIOAuthManager *manager,
                                   const char *msg, int is_error) {
    if (!msg) return;

    if (manager && manager->message_callback) {
        manager->message_callback(manager->message_callback_user_data,
                                  msg, is_error);
    } else {
        if (is_error) {
            fprintf(stderr, "%s\n", msg);
        } else {
            printf("%s\n", msg);
        }
    }
}

/* ============================================================================
 * Token Revocation (used at logout)
 * ============================================================================ */

static void revoke_token(const char *token_value, const char *token_type_hint) {
    if (!token_value || token_value[0] == '\0') return;

    char body[4096];
    if (token_type_hint) {
        snprintf(body, sizeof(body),
                 "token=%s&client_id=%s&token_type_hint=%s",
                 token_value, OPENAI_OAUTH_CLIENT_ID, token_type_hint);
    } else {
        snprintf(body, sizeof(body),
                 "token=%s&client_id=%s",
                 token_value, OPENAI_OAUTH_CLIENT_ID);
    }

    long status = 0;
    char *response = http_post_form(OPENAI_REVOKE_ENDPOINT, body, &status);

    explicit_bzero(body, sizeof(body));

    if (response) {
        free(response);
    }
    if (status == 200) {
        LOG_INFO("[OpenAI OAuth] Token revoked successfully");
    } else {
        LOG_WARN("[OpenAI OAuth] Token revocation returned status %ld", status);
    }
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

/**
 * Create and initialize an OAuth manager.
 */
OpenAIOAuthManager *openai_oauth_manager_create(void) {
    OpenAIOAuthManager *manager = calloc(1, sizeof(OpenAIOAuthManager));
    if (!manager) {
        LOG_ERROR("[OpenAI OAuth] Failed to allocate manager");
        return NULL;
    }

    if (pthread_mutex_init(&manager->token_mutex, NULL) != 0) {
        LOG_ERROR("[OpenAI OAuth] Failed to initialize mutex");
        free(manager);
        return NULL;
    }

    /* Try to load existing token */
    manager->token = load_token_from_disk();
    if (manager->token) {
        LOG_INFO("[OpenAI OAuth] Loaded existing token from disk");
    }

    return manager;
}

/**
 * Destroy the OAuth manager.
 */
void openai_oauth_manager_destroy(OpenAIOAuthManager *manager) {
    if (!manager) return;

    openai_oauth_stop_refresh_thread(manager);

    pthread_mutex_lock(&manager->token_mutex);
    openai_oauth_token_free(manager->token);
    manager->token = NULL;
    pthread_mutex_unlock(&manager->token_mutex);

    pthread_mutex_destroy(&manager->token_mutex);
    free(manager);

    LOG_DEBUG("[OpenAI OAuth] Manager destroyed");
}

/**
 * Set the message display callback.
 */
void openai_oauth_set_message_callback(OpenAIOAuthManager *manager,
                                        OpenAIOAuthMessageCallback callback,
                                        void *user_data) {
    if (!manager) return;
    manager->message_callback           = callback;
    manager->message_callback_user_data = user_data;
}

/**
 * Check if we have a valid (or refreshable) token.
 */
int openai_oauth_is_authenticated(OpenAIOAuthManager *manager) {
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
 * Perform interactive device authorization flow.
 */
int openai_oauth_login(OpenAIOAuthManager *manager) {
    if (!manager) return -1;

    /* Step 1: Request device authorization */
    OpenAIDeviceAuth *auth = request_device_authorization();
    if (!auth) {
        oauth_display_message(manager,
                              "Failed to start OpenAI device authorization.", 1);
        return -1;
    }

    /* Step 2: Display instructions */
    oauth_display_message(manager, "", 0);
    oauth_display_message(manager, "================================================", 0);
    oauth_display_message(manager, "  OPENAI SUBSCRIPTION LOGIN", 0);
    oauth_display_message(manager, "================================================", 0);
    oauth_display_message(manager, "", 0);

    char msg[1024];

    if (auth->verification_uri_complete) {
        snprintf(msg, sizeof(msg), "  Visit: %s",
                 auth->verification_uri_complete);
    } else {
        const char *base = auth->verification_uri
                           ? auth->verification_uri
                           : "https://auth.openai.com/activate";
        snprintf(msg, sizeof(msg), "  Visit: %s", base);
    }
    oauth_display_message(manager, msg, 0);
    oauth_display_message(manager, "", 0);

    snprintf(msg, sizeof(msg), "  Code:  %s", auth->user_code ? auth->user_code : "(none)");
    oauth_display_message(manager, msg, 0);
    oauth_display_message(manager, "", 0);
    oauth_display_message(manager, "  Sign in with your OpenAI account to authorize.", 0);
    oauth_display_message(manager, "  (ChatGPT Plus/Pro subscription required)", 0);
    oauth_display_message(manager, "", 0);

    /* Try to open a browser automatically */
    const char *open_url = auth->verification_uri_complete
                           ? auth->verification_uri_complete
                           : auth->verification_uri;
    if (open_url) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "xdg-open '%s' 2>/dev/null || "
                 "open '%s' 2>/dev/null || "
                 "start '%s' 2>/dev/null",
                 open_url, open_url, open_url);
        int rc = system(cmd);
        if (rc == 0) {
            oauth_display_message(manager, "  Browser opened automatically.", 0);
        }
    }

    oauth_display_message(manager, "  Waiting for authorization...", 0);
    oauth_display_message(manager, "", 0);

    /* Step 3: Poll for token */
    OpenAIOAuthToken *token = poll_for_token(auth);
    openai_device_auth_free(auth);

    if (!token) {
        oauth_display_message(manager, "Authorization failed or timed out.", 1);
        return -1;
    }

    /* Step 4: Store token */
    pthread_mutex_lock(&manager->token_mutex);
    openai_oauth_token_free(manager->token);
    manager->token = token;
    pthread_mutex_unlock(&manager->token_mutex);

    if (save_token_to_disk(token) != 0) {
        LOG_WARN("[OpenAI OAuth] Failed to save token to disk");
    }

    oauth_display_message(manager, "  Authorization successful!", 0);
    char *token_path = get_token_file_path();
    if (token_path) {
        snprintf(msg, sizeof(msg), "  Token saved to %s", token_path);
        oauth_display_message(manager, msg, 0);
        free(token_path);
    } else {
        oauth_display_message(manager, "  Token saved to disk", 0);
    }
    oauth_display_message(manager, "", 0);

    return 0;
}

/**
 * Get the current access token, refreshing if needed.
 */
const char *openai_oauth_get_access_token(OpenAIOAuthManager *manager) {
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

    /* Always reload from disk to catch updates from subagents */
    reload_token_from_disk_if_newer(manager);

    /* Refresh if expiring soon */
    time_t now = time(NULL);
    time_t remaining = manager->token->expires_at > 0
                       ? manager->token->expires_at - now
                       : (time_t)INT_MAX;

    if (remaining < OPENAI_TOKEN_REFRESH_THRESHOLD_SECONDS) {
        LOG_INFO("[OpenAI OAuth] Token expiring in %ld s, refreshing...",
                 (long)remaining);

        /* Acquire cross-process lock before network call */
        int lock_fd = acquire_refresh_lock_nb();
        if (lock_fd < 0) {
            /* Another process is refreshing — wait then reload */
            LOG_INFO("[OpenAI OAuth] get_access_token: waiting for concurrent refresh");
            pthread_mutex_unlock(&manager->token_mutex);
            lock_fd = acquire_refresh_lock();
            if (lock_fd >= 0) {
                release_refresh_lock(lock_fd);
            }
            pthread_mutex_lock(&manager->token_mutex);
            reload_token_from_disk_if_newer(manager);
            if (manager->token && manager->token->access_token) {
                const char *tok = manager->token->access_token;
                pthread_mutex_unlock(&manager->token_mutex);
                return tok;
            }
            pthread_mutex_unlock(&manager->token_mutex);
            return NULL;
        }

        OpenAIOAuthToken *new_token = refresh_token_internal(manager, 0);
        release_refresh_lock(lock_fd);

        if (new_token) {
            OpenAIOAuthToken *old = manager->token;
            manager->token = new_token;
            if (save_token_to_disk(new_token) != 0) {
                LOG_WARN("[OpenAI OAuth] Failed to save refreshed token");
            }
            openai_oauth_token_free(old);
        } else {
            /* Re-check: refresh_token_internal may have reloaded from disk */
            if (manager->token && manager->token->expires_at > 0) {
                remaining = manager->token->expires_at - now;
            }
            if (remaining <= 0) {
                LOG_ERROR("[OpenAI OAuth] Token expired and refresh failed");
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
 * Force token refresh.
 */
int openai_oauth_refresh(OpenAIOAuthManager *manager, int force) {
    if (!manager) return -1;

    pthread_mutex_lock(&manager->token_mutex);

    reload_token_from_disk_if_newer(manager);

    if (!manager->token || !manager->token->refresh_token) {
        pthread_mutex_unlock(&manager->token_mutex);
        LOG_ERROR("[OpenAI OAuth] No refresh token available");
        return -1;
    }

    /* If not forced, skip if token is fresh after disk reload */
    if (!force) {
        time_t remaining = manager->token->expires_at - time(NULL);
        if (remaining >= OPENAI_TOKEN_REFRESH_THRESHOLD_SECONDS) {
            LOG_INFO("[OpenAI OAuth] Token already fresh, skipping refresh");
            pthread_mutex_unlock(&manager->token_mutex);
            return 0;
        }
    }

    /* Acquire cross-process refresh lock before network call.
     * If another process is already refreshing, wait for it and reload
     * from disk rather than racing it with a duplicate network request. */
    int lock_fd = acquire_refresh_lock_nb();
    if (lock_fd < 0) {
        /* Another process holds the lock — wait for completion, then reload */
        LOG_INFO("[OpenAI OAuth] Refresh deferred: another process is refreshing");
        pthread_mutex_unlock(&manager->token_mutex);
        lock_fd = acquire_refresh_lock();
        if (lock_fd >= 0) {
            release_refresh_lock(lock_fd);
        }
        pthread_mutex_lock(&manager->token_mutex);
        reload_token_from_disk_if_newer(manager);
        time_t remaining = manager->token ? manager->token->expires_at - time(NULL) : 0;
        if (remaining >= OPENAI_TOKEN_REFRESH_THRESHOLD_SECONDS) {
            LOG_INFO("[OpenAI OAuth] Token updated by other process");
            pthread_mutex_unlock(&manager->token_mutex);
            return 0;
        }
        pthread_mutex_unlock(&manager->token_mutex);
        return -1;
    }

    OpenAIOAuthToken *new_token = refresh_token_internal(manager, force);
    release_refresh_lock(lock_fd);

    if (!new_token) {
        /* Check if disk reload (inside refresh_token_internal) handled it */
        time_t remaining = manager->token ? manager->token->expires_at - time(NULL) : 0;
        if (remaining >= OPENAI_TOKEN_REFRESH_THRESHOLD_SECONDS) {
            LOG_INFO("[OpenAI OAuth] Token refreshed by another process");
            pthread_mutex_unlock(&manager->token_mutex);
            return 0;
        }
        pthread_mutex_unlock(&manager->token_mutex);
        return -1;
    }

    OpenAIOAuthToken *old = manager->token;
    manager->token = new_token;
    if (save_token_to_disk(new_token) != 0) {
        LOG_WARN("[OpenAI OAuth] Failed to save refreshed token");
    }
    openai_oauth_token_free(old);

    pthread_mutex_unlock(&manager->token_mutex);
    return 0;
}

/**
 * Reload token from disk (public API for 401 recovery).
 */
int openai_oauth_reload_from_disk(OpenAIOAuthManager *manager) {
    if (!manager) return 0;

    pthread_mutex_lock(&manager->token_mutex);
    int result = reload_token_from_disk_if_newer(manager);
    pthread_mutex_unlock(&manager->token_mutex);

    if (result) {
        LOG_INFO("[OpenAI OAuth] Reloaded token from disk");
    }
    return result;
}

/**
 * Wait for any in-progress cross-process token refresh to complete.
 *
 * If another process currently holds the refresh lock this call blocks until
 * it releases the lock, then reloads the new token from disk.  If no refresh
 * is in progress the call returns immediately after a quick disk check.
 *
 * Used by the 401 handler so it can pick up a freshly rotated token without
 * racing into its own (duplicate) network refresh.
 *
 * @return 1 if the token was reloaded from disk, 0 otherwise.
 */
int openai_oauth_wait_for_refresh(OpenAIOAuthManager *manager) {
    if (!manager) return 0;

    /* Non-blocking check first: if we get it immediately, nobody is refreshing */
    int lock_fd = acquire_refresh_lock_nb();
    if (lock_fd >= 0) {
        /* Lock was free — no concurrent refresh, just do a quick disk check */
        release_refresh_lock(lock_fd);
        pthread_mutex_lock(&manager->token_mutex);
        int reloaded = reload_token_from_disk_if_newer(manager);
        pthread_mutex_unlock(&manager->token_mutex);
        return reloaded;
    }

    /* Lock is held — a refresh is in progress.  Wait for it. */
    LOG_INFO("[OpenAI OAuth] Waiting for concurrent token refresh to complete...");
    lock_fd = acquire_refresh_lock();
    if (lock_fd >= 0) {
        release_refresh_lock(lock_fd);
    }

    /* Refresh finished: reload the new token from disk */
    pthread_mutex_lock(&manager->token_mutex);
    int reloaded = reload_token_from_disk_if_newer(manager);
    pthread_mutex_unlock(&manager->token_mutex);

    if (reloaded) {
        LOG_INFO("[OpenAI OAuth] Reloaded updated token after waiting for refresh");
    }
    return reloaded;
}


/**
 * Extract chatgpt_account_id from JWT access token claims.
 * The access token JWT has a claim "https://api.openai.com/auth" containing
 * {"chatgpt_account_id": "..."}.
 */
static char *extract_account_id_from_jwt(const char *access_token) {
    if (!access_token || access_token[0] == '\0') return NULL;

    /* Find the payload part (second JWT segment) */
    const char *dot1 = strchr(access_token, '.');
    if (!dot1) return NULL;
    const char *payload_b64 = dot1 + 1;
    const char *dot2 = strchr(payload_b64, '.');
    if (!dot2) return NULL;

    size_t encoded_len = (size_t)(dot2 - payload_b64);
    if (encoded_len == 0 || encoded_len > 16384) return NULL;

    /* Convert base64url to base64 (add padding, swap - and _) */
    size_t pad = (4 - encoded_len % 4) % 4;
    size_t buf_len = encoded_len + pad + 1;
    char *b64 = malloc(buf_len);
    if (!b64) return NULL;
    strlcpy(b64, payload_b64, encoded_len + 1);
    for (size_t i = 0; i < pad; i++) b64[encoded_len + i] = '=';
    b64[encoded_len + pad] = '\0';
    for (size_t i = 0; i < encoded_len; i++) {
        if (b64[i] == '-') b64[i] = '+';
        else if (b64[i] == '_') b64[i] = '/';
    }

    /* Decode base64 manually */
    static const signed char dtable[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    size_t b64_len = strlen(b64);
    size_t out_len = (b64_len / 4) * 3 + 3;
    char *json_str = malloc(out_len);
    if (!json_str) { free(b64); return NULL; }
    size_t j = 0;
    for (size_t i = 0; i + 3 < b64_len; i += 4) {
        signed char c0 = dtable[(unsigned char)b64[i]];
        signed char c1 = dtable[(unsigned char)b64[i+1]];
        signed char c2 = dtable[(unsigned char)b64[i+2]];
        signed char c3 = dtable[(unsigned char)b64[i+3]];
        if (c0 < 0 || c1 < 0) break;
        json_str[j++] = (char)((c0 << 2) | (c1 >> 4));
        if (c2 >= 0) json_str[j++] = (char)((c1 << 4) | (c2 >> 2));
        if (c3 >= 0) json_str[j++] = (char)((c2 << 6) | c3);
    }
    free(b64);
    json_str[j] = '\0';

    cJSON *json = cJSON_Parse(json_str);
    free(json_str);
    if (!json) return NULL;

    char *account_id = NULL;
    /* JWT claim "https://api.openai.com/auth" contains chatgpt_account_id */
    cJSON *auth_claim = cJSON_GetObjectItem(json, "https://api.openai.com/auth");
    if (auth_claim && cJSON_IsObject(auth_claim)) {
        cJSON *acct = cJSON_GetObjectItem(auth_claim, "chatgpt_account_id");
        if (acct && cJSON_IsString(acct) && acct->valuestring) {
            account_id = strdup(acct->valuestring);
        }
    }
    cJSON_Delete(json);
    return account_id;
}

const char *openai_oauth_get_account_id(OpenAIOAuthManager *manager) {
    if (!manager) return NULL;
    pthread_mutex_lock(&manager->token_mutex);
    /* Return cached account_id if available */
    if (manager->token && manager->token->account_id) {
        const char *aid = manager->token->account_id;
        pthread_mutex_unlock(&manager->token_mutex);
        return aid;
    }
    /* Try to extract from current access token JWT */
    if (manager->token && manager->token->access_token) {
        char *aid = extract_account_id_from_jwt(manager->token->access_token);
        if (aid) {
            free(manager->token->account_id);
            manager->token->account_id = aid;
            pthread_mutex_unlock(&manager->token_mutex);
            return manager->token->account_id;
        }
    }
    pthread_mutex_unlock(&manager->token_mutex);
    return NULL;
}


/**
 * Start background token refresh thread.
 */
int openai_oauth_start_refresh_thread(OpenAIOAuthManager *manager) {
    if (!manager) return -1;

    if (manager->refresh_thread_started) {
        LOG_DEBUG("[OpenAI OAuth] Refresh thread already running");
        return 0;
    }

    manager->refresh_thread_running = 1;
    if (pthread_create(&manager->refresh_thread, NULL,
                       refresh_thread_func, manager) != 0) {
        LOG_ERROR("[OpenAI OAuth] Failed to create refresh thread");
        manager->refresh_thread_running = 0;
        return -1;
    }
    manager->refresh_thread_started = 1;
    LOG_INFO("[OpenAI OAuth] Background refresh thread started");
    return 0;
}

/**
 * Stop background token refresh thread.
 */
void openai_oauth_stop_refresh_thread(OpenAIOAuthManager *manager) {
    if (!manager || !manager->refresh_thread_started) return;

    manager->refresh_thread_running = 0;
    pthread_join(manager->refresh_thread, NULL);
    manager->refresh_thread_started = 0;
    LOG_DEBUG("[OpenAI OAuth] Refresh thread stopped");
}

/**
 * Logout: revoke tokens and delete the token file.
 */
void openai_oauth_logout(OpenAIOAuthManager *manager) {
    if (!manager) return;

    openai_oauth_stop_refresh_thread(manager);

    pthread_mutex_lock(&manager->token_mutex);

    /* Revoke refresh token (most important to revoke) */
    if (manager->token && manager->token->refresh_token) {
        revoke_token(manager->token->refresh_token, "refresh_token");
    }

    openai_oauth_token_free(manager->token);
    manager->token = NULL;

    pthread_mutex_unlock(&manager->token_mutex);

    /* Delete the token file */
    char *path = get_token_file_path();
    if (path) {
        if (unlink(path) == 0) {
            LOG_INFO("[OpenAI OAuth] Deleted token file: %s", path);
        } else if (errno != ENOENT) {
            LOG_WARN("[OpenAI OAuth] Failed to delete token file: %s",
                     strerror(errno));
        }
        free(path);
    }

    LOG_INFO("[OpenAI OAuth] Logged out");
}

/* ============================================================================
 * Struct Freeing
 * ============================================================================ */

void openai_device_auth_free(OpenAIDeviceAuth *auth) {
    if (!auth) return;
    free(auth->user_code);
    free(auth->device_code);
    free(auth->verification_uri);
    free(auth->verification_uri_complete);
    free(auth);
}

void openai_oauth_token_free(OpenAIOAuthToken *token) {
    if (!token) return;

    /* Securely wipe sensitive fields before freeing */
    if (token->access_token) {
        explicit_bzero(token->access_token, strlen(token->access_token));
        free(token->access_token);
    }
    if (token->refresh_token) {
        explicit_bzero(token->refresh_token, strlen(token->refresh_token));
        free(token->refresh_token);
    }
    if (token->id_token) {
        explicit_bzero(token->id_token, strlen(token->id_token));
        free(token->id_token);
    }

    free(token->token_type);
    free(token->scope);
    free(token->client_id);
    free(token->account_id);
    free(token);
}
