/*
 * kimi_oauth.c - Kimi Coding Plan OAuth 2.0 Device Authorization
 *
 * Implements OAuth 2.0 Device Authorization Grant (RFC 8628) for
 * Kimi Coding Plan API authentication.
 */

#define _POSIX_C_SOURCE 200809L

#include "kimi_oauth.h"
#include "logger.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <pwd.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <bsd/string.h>
#include <bsd/stdlib.h>

// ============================================================================
// Constants
// ============================================================================

#define KIMI_DEVICE_AUTH_ENDPOINT KIMI_OAUTH_HOST "/api/oauth/device_authorization"
#define KIMI_TOKEN_ENDPOINT KIMI_OAUTH_HOST "/api/oauth/token"
#define KIMI_PLATFORM "kimi_cli"
#define KIMI_VERSION "1.0.0"

// ============================================================================
// Internal Helper Declarations
// ============================================================================

static char* get_or_create_device_id(void);
static char* get_device_model(void);
static char* get_device_name(void);
static KimiOAuthToken* load_token_from_disk(void);
static int save_token_to_disk(const KimiOAuthToken *token);
static KimiDeviceAuth* request_device_authorization(KimiOAuthManager *manager);
static KimiOAuthToken* poll_for_token(KimiOAuthManager *manager,
                                       const KimiDeviceAuth *device_auth);
static KimiOAuthToken* refresh_token_internal(KimiOAuthManager *manager);
static void* refresh_thread_func(void *arg);
static char* get_kimi_dir(void);
static char* get_credentials_dir(void);
static int mkdir_p_with_mode(const char *path, mode_t mode);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Create directory recursively with specified mode
 */
static int mkdir_p_with_mode(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    if (strlcpy(tmp, path, sizeof(tmp)) >= sizeof(tmp)) {
        return -1;  // Path too long
    }
    len = strlen(tmp);

    // Remove trailing slash
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    // Create directories recursively
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    // Create final directory
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

/**
 * Get ~/.kimi directory path
 */
static char* get_kimi_dir(void) {
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        struct passwd *pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }

    if (!home || home[0] == '\0') {
        LOG_ERROR("Cannot determine home directory");
        return NULL;
    }

    char *path = malloc(PATH_MAX);
    if (!path) return NULL;

    if (snprintf(path, PATH_MAX, "%s/.kimi", home) >= PATH_MAX) {
        free(path);
        return NULL;
    }

    return path;
}

/**
 * Get ~/.kimi/credentials directory path
 */
static char* get_credentials_dir(void) {
    char *kimi_dir = get_kimi_dir();
    if (!kimi_dir) return NULL;

    char *path = malloc(PATH_MAX);
    if (!path) {
        free(kimi_dir);
        return NULL;
    }

    if (snprintf(path, PATH_MAX, "%s/credentials", kimi_dir) >= PATH_MAX) {
        free(path);
        free(kimi_dir);
        return NULL;
    }

    free(kimi_dir);
    return path;
}

/**
 * Generate a random UUID v4
 */
static char* generate_uuid(void) {
    char *uuid = malloc(37);  // 36 chars + null
    if (!uuid) return NULL;

    // Generate 16 random bytes
    unsigned char bytes[16];
    arc4random_buf(bytes, sizeof(bytes));

    // Set version (4) and variant bits
    bytes[6] = (unsigned char)((bytes[6] & 0x0F) | 0x40);  // version 4
    bytes[8] = (unsigned char)((bytes[8] & 0x3F) | 0x80);  // variant 1

    snprintf(uuid, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5],
             bytes[6], bytes[7],
             bytes[8], bytes[9],
             bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);

    return uuid;
}

/**
 * Get or create stable device ID
 * Stored in ~/.kimi/device_id with 0600 permissions
 */
static char* get_or_create_device_id(void) {
    char *kimi_dir = get_kimi_dir();
    if (!kimi_dir) return NULL;

    // Create ~/.kimi directory if needed
    if (mkdir_p_with_mode(kimi_dir, 0700) != 0) {
        LOG_ERROR("Failed to create kimi directory: %s", kimi_dir);
        free(kimi_dir);
        return NULL;
    }

    char device_id_path[PATH_MAX];
    if (snprintf(device_id_path, sizeof(device_id_path), "%s/device_id",
                 kimi_dir) >= (int)sizeof(device_id_path)) {
        free(kimi_dir);
        return NULL;
    }
    free(kimi_dir);

    // Try to read existing device ID
    FILE *f = fopen(device_id_path, "r");
    if (f) {
        char *device_id = malloc(64);
        if (device_id) {
            if (fgets(device_id, 64, f) != NULL) {
                // Trim newline
                size_t len = strlen(device_id);
                while (len > 0 && (device_id[len - 1] == '\n' ||
                                   device_id[len - 1] == '\r')) {
                    device_id[len - 1] = '\0';
                    len--;
                }
                fclose(f);
                if (len > 0) {
                    LOG_DEBUG("Loaded existing device ID: %s", device_id);
                    return device_id;
                }
            }
            free(device_id);
        }
        fclose(f);
    }

    // Generate new device ID
    char *device_id = generate_uuid();
    if (!device_id) {
        LOG_ERROR("Failed to generate device ID");
        return NULL;
    }

    // Save to file with 0600 permissions
    int fd = open(device_id_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        LOG_ERROR("Failed to create device ID file: %s", strerror(errno));
        free(device_id);
        return NULL;
    }

    f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        free(device_id);
        return NULL;
    }

    fprintf(f, "%s\n", device_id);
    fclose(f);

    LOG_INFO("Generated new device ID: %s", device_id);
    return device_id;
}

/**
 * Get device model string: "OS version arch"
 * e.g., "Linux 6.1.0 x86_64"
 */
static char* get_device_model(void) {
    struct utsname info;
    if (uname(&info) != 0) {
        return strdup("Unknown");
    }

    char *model = malloc(256);
    if (!model) return NULL;

    snprintf(model, 256, "%s %s %s", info.sysname, info.release, info.machine);
    return model;
}

/**
 * Get device name (hostname)
 */
static char* get_device_name(void) {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return strdup("unknown");
    }
    hostname[sizeof(hostname) - 1] = '\0';
    return strdup(hostname);
}

// ============================================================================
// Token Storage
// ============================================================================

/**
 * Load OAuth token from disk
 * Returns NULL if no valid token exists
 */
static KimiOAuthToken* load_token_from_disk(void) {
    char *creds_dir = get_credentials_dir();
    if (!creds_dir) return NULL;

    char token_path[PATH_MAX];
    if (snprintf(token_path, sizeof(token_path), "%s/oauth_token.json",
                 creds_dir) >= (int)sizeof(token_path)) {
        free(creds_dir);
        return NULL;
    }
    free(creds_dir);

    FILE *f = fopen(token_path, "r");
    if (!f) {
        LOG_DEBUG("No existing token file at %s", token_path);
        return NULL;
    }

    // Read file contents
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize <= 0 || fsize > 1024 * 1024) {  // Max 1MB
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *content = malloc((size_t)fsize + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(content, 1, (size_t)fsize, f);
    fclose(f);

    if (read_size != (size_t)fsize) {
        free(content);
        return NULL;
    }
    content[fsize] = '\0';

    // Parse JSON
    cJSON *json = cJSON_Parse(content);
    free(content);

    if (!json) {
        LOG_WARN("Failed to parse token file");
        return NULL;
    }

    // Extract fields
    KimiOAuthToken *token = calloc(1, sizeof(KimiOAuthToken));
    if (!token) {
        cJSON_Delete(json);
        return NULL;
    }

    cJSON *access_token = cJSON_GetObjectItem(json, "access_token");
    cJSON *refresh_token = cJSON_GetObjectItem(json, "refresh_token");
    cJSON *expires_at = cJSON_GetObjectItem(json, "expires_at");
    cJSON *token_type = cJSON_GetObjectItem(json, "token_type");
    cJSON *scope = cJSON_GetObjectItem(json, "scope");

    if (access_token && cJSON_IsString(access_token)) {
        token->access_token = strdup(access_token->valuestring);
    }
    if (refresh_token && cJSON_IsString(refresh_token)) {
        token->refresh_token = strdup(refresh_token->valuestring);
    }
    if (expires_at && cJSON_IsNumber(expires_at)) {
        token->expires_at = (time_t)expires_at->valuedouble;
    }
    if (token_type && cJSON_IsString(token_type)) {
        token->token_type = strdup(token_type->valuestring);
    }
    if (scope && cJSON_IsString(scope)) {
        token->scope = strdup(scope->valuestring);
    }

    cJSON_Delete(json);

    // Validate required fields
    if (!token->access_token || !token->refresh_token) {
        LOG_WARN("Token file missing required fields");
        kimi_oauth_token_free(token);
        return NULL;
    }

    LOG_DEBUG("Loaded token from disk (expires_at: %ld)", (long)token->expires_at);
    return token;
}

/**
 * Save OAuth token to disk
 * File is stored with 0600 permissions
 */
static int save_token_to_disk(const KimiOAuthToken *token) {
    if (!token || !token->access_token || !token->refresh_token) {
        return -1;
    }

    char *creds_dir = get_credentials_dir();
    if (!creds_dir) return -1;

    // Create credentials directory with 0700 permissions
    if (mkdir_p_with_mode(creds_dir, 0700) != 0) {
        LOG_ERROR("Failed to create credentials directory: %s", creds_dir);
        free(creds_dir);
        return -1;
    }

    char token_path[PATH_MAX];
    if (snprintf(token_path, sizeof(token_path), "%s/oauth_token.json",
                 creds_dir) >= (int)sizeof(token_path)) {
        free(creds_dir);
        return -1;
    }
    free(creds_dir);

    // Build JSON
    cJSON *json = cJSON_CreateObject();
    if (!json) return -1;

    cJSON_AddStringToObject(json, "access_token", token->access_token);
    cJSON_AddStringToObject(json, "refresh_token", token->refresh_token);
    cJSON_AddNumberToObject(json, "expires_at", (double)token->expires_at);
    if (token->token_type) {
        cJSON_AddStringToObject(json, "token_type", token->token_type);
    }
    if (token->scope) {
        cJSON_AddStringToObject(json, "scope", token->scope);
    }

    char *json_str = cJSON_Print(json);
    cJSON_Delete(json);

    if (!json_str) return -1;

    // Write to file with 0600 permissions
    int fd = open(token_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        LOG_ERROR("Failed to create token file: %s", strerror(errno));
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

    LOG_DEBUG("Saved token to disk: %s", token_path);
    return 0;
}

// ============================================================================
// HTTP Request Helpers
// ============================================================================

/**
 * Perform HTTP POST request with device headers
 * Returns response body or NULL on error
 * Caller must free the returned string
 */
static char* http_post_with_device_headers(KimiOAuthManager *manager,
                                           const char *url,
                                           const char *body,
                                           const char *content_type,
                                           long *http_status) {
    if (!manager || !url) return NULL;

    // Build headers
    struct curl_slist *headers = kimi_oauth_get_device_headers(manager);
    if (!headers) {
        LOG_ERROR("Failed to build device headers");
        return NULL;
    }

    // Add Content-Type
    if (content_type) {
        char header[256];
        snprintf(header, sizeof(header), "Content-Type: %s", content_type);
        headers = curl_slist_append(headers, header);
    }

    // Build request
    HttpRequest req = {0};
    req.url = url;
    req.method = "POST";
    req.body = body;
    req.headers = headers;
    req.connect_timeout_ms = 30000;
    req.total_timeout_ms = 60000;

    // Execute request
    HttpResponse *resp = http_client_execute(&req, NULL, NULL);
    curl_slist_free_all(headers);

    if (!resp) {
        LOG_ERROR("HTTP request failed (no response)");
        return NULL;
    }

    if (http_status) {
        *http_status = resp->status_code;
    }

    char *result = NULL;
    if (resp->body) {
        result = strdup(resp->body);
    }

    if (resp->error_message) {
        LOG_ERROR("HTTP error: %s", resp->error_message);
    }

    http_response_free(resp);
    return result;
}

// ============================================================================
// Device Authorization Flow
// ============================================================================

/**
 * Request device authorization from Kimi OAuth server
 */
static KimiDeviceAuth* request_device_authorization(KimiOAuthManager *manager) {
    const char *body = "client_id=" KIMI_OAUTH_CLIENT_ID;

    long http_status = 0;
    char *response = http_post_with_device_headers(
        manager,
        KIMI_DEVICE_AUTH_ENDPOINT,
        body,
        "application/x-www-form-urlencoded",
        &http_status
    );

    if (!response) {
        LOG_ERROR("Device authorization request failed");
        return NULL;
    }

    if (http_status != 200) {
        LOG_ERROR("Device authorization failed with status %ld: %s",
                  http_status, response);
        free(response);
        return NULL;
    }

    // Parse response
    cJSON *json = cJSON_Parse(response);
    free(response);

    if (!json) {
        LOG_ERROR("Failed to parse device authorization response");
        return NULL;
    }

    KimiDeviceAuth *auth = calloc(1, sizeof(KimiDeviceAuth));
    if (!auth) {
        cJSON_Delete(json);
        return NULL;
    }

    cJSON *user_code = cJSON_GetObjectItem(json, "user_code");
    cJSON *device_code = cJSON_GetObjectItem(json, "device_code");
    cJSON *verification_uri = cJSON_GetObjectItem(json, "verification_uri");
    cJSON *verification_uri_complete = cJSON_GetObjectItem(json, "verification_uri_complete");
    cJSON *expires_in = cJSON_GetObjectItem(json, "expires_in");
    cJSON *interval = cJSON_GetObjectItem(json, "interval");

    if (user_code && cJSON_IsString(user_code)) {
        auth->user_code = strdup(user_code->valuestring);
    }
    if (device_code && cJSON_IsString(device_code)) {
        auth->device_code = strdup(device_code->valuestring);
    }
    if (verification_uri && cJSON_IsString(verification_uri)) {
        auth->verification_uri = strdup(verification_uri->valuestring);
    }
    if (verification_uri_complete && cJSON_IsString(verification_uri_complete)) {
        auth->verification_uri_complete = strdup(verification_uri_complete->valuestring);
    }
    if (expires_in && cJSON_IsNumber(expires_in)) {
        auth->expires_in = expires_in->valueint;
    }
    if (interval && cJSON_IsNumber(interval)) {
        auth->interval = interval->valueint;
    }

    cJSON_Delete(json);

    // Validate required fields
    if (!auth->user_code || !auth->device_code) {
        LOG_ERROR("Device authorization response missing required fields");
        kimi_device_auth_free(auth);
        return NULL;
    }

    // Set defaults
    if (auth->expires_in <= 0) {
        auth->expires_in = KIMI_DEVICE_AUTH_TIMEOUT_SECONDS;
    }
    if (auth->interval <= 0) {
        auth->interval = 5;  // Default 5 seconds
    }

    LOG_DEBUG("Device authorization successful: user_code=%s, expires_in=%d",
              auth->user_code, auth->expires_in);
    return auth;
}

/**
 * Poll for token after user authorization
 */
static KimiOAuthToken* poll_for_token(KimiOAuthManager *manager,
                                       const KimiDeviceAuth *device_auth) {
    if (!manager || !device_auth || !device_auth->device_code) {
        return NULL;
    }

    int interval = device_auth->interval > 0 ? device_auth->interval : 5;
    time_t start_time = time(NULL);
    int expires_in = device_auth->expires_in > 0 ?
                     device_auth->expires_in : KIMI_DEVICE_AUTH_TIMEOUT_SECONDS;

    // Build request body
    char body[512];
    snprintf(body, sizeof(body),
             "grant_type=urn:ietf:params:oauth:grant-type:device_code"
             "&device_code=%s"
             "&client_id=%s",
             device_auth->device_code, KIMI_OAUTH_CLIENT_ID);

    while (1) {
        // Check timeout
        if (time(NULL) - start_time > expires_in) {
            LOG_ERROR("Device authorization timeout");
            return NULL;
        }

        // Wait before polling
        sleep((unsigned int)interval);

        LOG_DEBUG("Polling for token...");

        long http_status = 0;
        char *response = http_post_with_device_headers(
            manager,
            KIMI_TOKEN_ENDPOINT,
            body,
            "application/x-www-form-urlencoded",
            &http_status
        );

        if (!response) {
            LOG_WARN("Token poll request failed, retrying...");
            continue;
        }

        cJSON *json = cJSON_Parse(response);
        free(response);

        if (!json) {
            LOG_WARN("Failed to parse token response, retrying...");
            continue;
        }

        // Check for error
        cJSON *error = cJSON_GetObjectItem(json, "error");
        if (error && cJSON_IsString(error)) {
            const char *err_str = error->valuestring;

            if (strcmp(err_str, "authorization_pending") == 0) {
                // User hasn't authorized yet, keep polling
                LOG_DEBUG("Authorization pending, continuing to poll...");
                cJSON_Delete(json);
                continue;
            } else if (strcmp(err_str, "slow_down") == 0) {
                // Increase polling interval
                interval += 5;
                LOG_DEBUG("Received slow_down, increasing interval to %d", interval);
                cJSON_Delete(json);
                continue;
            } else if (strcmp(err_str, "expired_token") == 0) {
                LOG_ERROR("Device code expired");
                cJSON_Delete(json);
                return NULL;
            } else if (strcmp(err_str, "access_denied") == 0) {
                LOG_ERROR("Authorization denied by user");
                cJSON_Delete(json);
                return NULL;
            } else {
                LOG_ERROR("Token error: %s", err_str);
                cJSON_Delete(json);
                return NULL;
            }
        }

        // Success - parse token
        KimiOAuthToken *token = calloc(1, sizeof(KimiOAuthToken));
        if (!token) {
            cJSON_Delete(json);
            return NULL;
        }

        cJSON *access_token = cJSON_GetObjectItem(json, "access_token");
        cJSON *refresh_token = cJSON_GetObjectItem(json, "refresh_token");
        cJSON *expires_in_json = cJSON_GetObjectItem(json, "expires_in");
        cJSON *token_type = cJSON_GetObjectItem(json, "token_type");
        cJSON *scope = cJSON_GetObjectItem(json, "scope");

        if (access_token && cJSON_IsString(access_token)) {
            token->access_token = strdup(access_token->valuestring);
        }
        if (refresh_token && cJSON_IsString(refresh_token)) {
            token->refresh_token = strdup(refresh_token->valuestring);
        }
        if (expires_in_json && cJSON_IsNumber(expires_in_json)) {
            token->expires_at = time(NULL) + (time_t)expires_in_json->valueint;
        }
        if (token_type && cJSON_IsString(token_type)) {
            token->token_type = strdup(token_type->valuestring);
        }
        if (scope && cJSON_IsString(scope)) {
            token->scope = strdup(scope->valuestring);
        }

        cJSON_Delete(json);

        // Validate
        if (!token->access_token) {
            LOG_ERROR("Token response missing access_token");
            kimi_oauth_token_free(token);
            return NULL;
        }

        LOG_INFO("Successfully obtained access token");
        return token;
    }
}

/**
 * Refresh token using refresh_token
 * IMPORTANT: refresh_token rotates on each refresh - always save the new one!
 */
static KimiOAuthToken* refresh_token_internal(KimiOAuthManager *manager) {
    if (!manager || !manager->token || !manager->token->refresh_token) {
        LOG_ERROR("Cannot refresh: no refresh token available");
        return NULL;
    }

    // Build request body
    char body[2048];
    snprintf(body, sizeof(body),
             "grant_type=refresh_token"
             "&refresh_token=%s"
             "&client_id=%s",
             manager->token->refresh_token, KIMI_OAUTH_CLIENT_ID);

    long http_status = 0;
    char *response = http_post_with_device_headers(
        manager,
        KIMI_TOKEN_ENDPOINT,
        body,
        "application/x-www-form-urlencoded",
        &http_status
    );

    // Clear sensitive data from stack
    explicit_bzero(body, sizeof(body));

    if (!response) {
        LOG_ERROR("Token refresh request failed");
        return NULL;
    }

    if (http_status != 200) {
        LOG_ERROR("Token refresh failed with status %ld: %s",
                  http_status, response);
        free(response);
        return NULL;
    }

    cJSON *json = cJSON_Parse(response);
    free(response);

    if (!json) {
        LOG_ERROR("Failed to parse refresh response");
        return NULL;
    }

    // Check for error
    cJSON *error = cJSON_GetObjectItem(json, "error");
    if (error && cJSON_IsString(error)) {
        LOG_ERROR("Token refresh error: %s", error->valuestring);
        cJSON_Delete(json);
        return NULL;
    }

    // Parse new token
    KimiOAuthToken *token = calloc(1, sizeof(KimiOAuthToken));
    if (!token) {
        cJSON_Delete(json);
        return NULL;
    }

    cJSON *access_token = cJSON_GetObjectItem(json, "access_token");
    cJSON *refresh_token = cJSON_GetObjectItem(json, "refresh_token");
    cJSON *expires_in = cJSON_GetObjectItem(json, "expires_in");
    cJSON *token_type = cJSON_GetObjectItem(json, "token_type");
    cJSON *scope = cJSON_GetObjectItem(json, "scope");

    if (access_token && cJSON_IsString(access_token)) {
        token->access_token = strdup(access_token->valuestring);
    }
    if (refresh_token && cJSON_IsString(refresh_token)) {
        token->refresh_token = strdup(refresh_token->valuestring);
    }
    if (expires_in && cJSON_IsNumber(expires_in)) {
        token->expires_at = time(NULL) + (time_t)expires_in->valueint;
    }
    if (token_type && cJSON_IsString(token_type)) {
        token->token_type = strdup(token_type->valuestring);
    }
    if (scope && cJSON_IsString(scope)) {
        token->scope = strdup(scope->valuestring);
    }

    cJSON_Delete(json);

    // Validate
    if (!token->access_token || !token->refresh_token) {
        LOG_ERROR("Refresh response missing required fields");
        kimi_oauth_token_free(token);
        return NULL;
    }

    LOG_INFO("Token refreshed successfully (new expires_at: %ld)",
             (long)token->expires_at);
    return token;
}

// ============================================================================
// Background Refresh Thread
// ============================================================================

/**
 * Background thread that periodically checks and refreshes the token
 */
static void* refresh_thread_func(void *arg) {
    KimiOAuthManager *manager = (KimiOAuthManager *)arg;
    if (!manager) return NULL;

    LOG_DEBUG("Token refresh thread started");

    while (manager->refresh_thread_running) {
        // Sleep in small increments to allow quick shutdown
        for (int i = 0; i < KIMI_TOKEN_REFRESH_INTERVAL_SECONDS && manager->refresh_thread_running; i++) {
            sleep(1);
        }

        if (!manager->refresh_thread_running) break;

        // Check if token needs refresh
        pthread_mutex_lock(&manager->token_mutex);

        if (manager->token && manager->token->expires_at > 0) {
            time_t now = time(NULL);
            time_t remaining = manager->token->expires_at - now;

            if (remaining < KIMI_TOKEN_REFRESH_THRESHOLD_SECONDS) {
                LOG_INFO("Token expires in %ld seconds, refreshing...",
                         (long)remaining);

                KimiOAuthToken *new_token = refresh_token_internal(manager);
                if (new_token) {
                    // Swap tokens
                    KimiOAuthToken *old_token = manager->token;
                    manager->token = new_token;

                    // Save new token to disk
                    if (save_token_to_disk(new_token) != 0) {
                        LOG_WARN("Failed to save refreshed token to disk");
                    }

                    // Free old token
                    kimi_oauth_token_free(old_token);
                } else {
                    LOG_ERROR("Background token refresh failed");
                }
            }
        }

        pthread_mutex_unlock(&manager->token_mutex);
    }

    LOG_DEBUG("Token refresh thread stopped");
    return NULL;
}

// ============================================================================
// Public API Implementation
// ============================================================================

/**
 * Initialize OAuth manager
 */
KimiOAuthManager* kimi_oauth_manager_create(void) {
    KimiOAuthManager *manager = calloc(1, sizeof(KimiOAuthManager));
    if (!manager) {
        LOG_ERROR("Failed to allocate OAuth manager");
        return NULL;
    }

    // Initialize mutex
    if (pthread_mutex_init(&manager->token_mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize token mutex");
        free(manager);
        return NULL;
    }

    // Get or create device ID
    manager->device_id = get_or_create_device_id();
    if (!manager->device_id) {
        LOG_ERROR("Failed to get device ID");
        pthread_mutex_destroy(&manager->token_mutex);
        free(manager);
        return NULL;
    }

    // Try to load existing token from disk
    manager->token = load_token_from_disk();
    if (manager->token) {
        LOG_INFO("Loaded existing Kimi OAuth token");
    }

    LOG_DEBUG("OAuth manager created (device_id=%s)", manager->device_id);
    return manager;
}

/**
 * Destroy OAuth manager
 */
void kimi_oauth_manager_destroy(KimiOAuthManager *manager) {
    if (!manager) return;

    // Stop refresh thread first
    kimi_oauth_stop_refresh_thread(manager);

    // Clean up token
    pthread_mutex_lock(&manager->token_mutex);
    if (manager->token) {
        kimi_oauth_token_free(manager->token);
        manager->token = NULL;
    }
    pthread_mutex_unlock(&manager->token_mutex);

    pthread_mutex_destroy(&manager->token_mutex);

    free(manager->device_id);
    free(manager);

    LOG_DEBUG("OAuth manager destroyed");
}

/**
 * Check if authenticated
 */
int kimi_oauth_is_authenticated(KimiOAuthManager *manager) {
    if (!manager) return 0;

    pthread_mutex_lock(&manager->token_mutex);
    int authenticated = (manager->token != NULL &&
                        manager->token->access_token != NULL);

    // Token is valid if we have a refresh token (can refresh if expired)
    // or if the access token hasn't expired yet
    if (authenticated && manager->token->refresh_token == NULL) {
        // No refresh token, check if access token is still valid
        time_t now = time(NULL);
        if (manager->token->expires_at > 0 && manager->token->expires_at <= now) {
            authenticated = 0;
        }
    }

    pthread_mutex_unlock(&manager->token_mutex);
    return authenticated;
}

/**
 * Perform interactive device authorization flow
 */
int kimi_oauth_login(KimiOAuthManager *manager) {
    if (!manager) return -1;

    // Request device authorization
    KimiDeviceAuth *auth = request_device_authorization(manager);
    if (!auth) {
        LOG_ERROR("Failed to request device authorization");
        return -1;
    }

    // Display authorization message
    printf("\n");
    printf("========================================\n");
    printf("KIMI CODING PLAN AUTHORIZATION\n");
    printf("========================================\n");
    printf("\n");

    if (auth->verification_uri_complete) {
        printf("Please visit: %s\n", auth->verification_uri_complete);
    } else if (auth->verification_uri) {
        printf("Please visit: %s?user_code=%s\n",
               auth->verification_uri, auth->user_code);
    } else {
        printf("Please visit: https://kimi.com/device?user_code=%s\n",
               auth->user_code);
    }

    printf("\n");
    printf("User Code: %s\n", auth->user_code);
    printf("\n");
    printf("Opening browser automatically...\n");
    fflush(stdout);

    // Open browser
    char cmd[512];
    if (auth->verification_uri_complete) {
        snprintf(cmd, sizeof(cmd), "xdg-open '%s' 2>/dev/null || "
                 "open '%s' 2>/dev/null || "
                 "start '%s' 2>/dev/null",
                 auth->verification_uri_complete,
                 auth->verification_uri_complete,
                 auth->verification_uri_complete);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "xdg-open 'https://kimi.com/device?user_code=%s' 2>/dev/null || "
                 "open 'https://kimi.com/device?user_code=%s' 2>/dev/null",
                 auth->user_code, auth->user_code);
    }
    int sys_result = system(cmd);
    if (sys_result != 0) {
        LOG_DEBUG("Browser launch returned: %d", sys_result);
    }

    printf("Waiting for authorization...\n");
    printf("\n");
    fflush(stdout);

    // Poll for token
    KimiOAuthToken *token = poll_for_token(manager, auth);
    kimi_device_auth_free(auth);

    if (!token) {
        printf("Authorization failed or timed out.\n");
        return -1;
    }

    // Store token
    pthread_mutex_lock(&manager->token_mutex);
    if (manager->token) {
        kimi_oauth_token_free(manager->token);
    }
    manager->token = token;
    pthread_mutex_unlock(&manager->token_mutex);

    // Save to disk
    if (save_token_to_disk(token) != 0) {
        LOG_WARN("Failed to save token to disk");
    }

    printf("Authorization successful!\n");
    printf("\n");

    return 0;
}

/**
 * Get current access token (refreshes if needed)
 */
const char* kimi_oauth_get_access_token(KimiOAuthManager *manager) {
    if (!manager) return NULL;

    pthread_mutex_lock(&manager->token_mutex);

    if (!manager->token || !manager->token->access_token) {
        pthread_mutex_unlock(&manager->token_mutex);
        return NULL;
    }

    // Check if token needs refresh
    time_t now = time(NULL);
    if (manager->token->expires_at > 0 &&
        manager->token->expires_at - now < KIMI_TOKEN_REFRESH_THRESHOLD_SECONDS) {

        LOG_INFO("Access token expiring soon, refreshing...");
        KimiOAuthToken *new_token = refresh_token_internal(manager);

        if (new_token) {
            KimiOAuthToken *old_token = manager->token;
            manager->token = new_token;

            // Save to disk
            if (save_token_to_disk(new_token) != 0) {
                LOG_WARN("Failed to save refreshed token to disk");
            }

            kimi_oauth_token_free(old_token);
        } else {
            LOG_WARN("Token refresh failed, returning current token");
        }
    }

    const char *token = manager->token->access_token;
    pthread_mutex_unlock(&manager->token_mutex);

    return token;
}

/**
 * Force token refresh
 */
int kimi_oauth_refresh(KimiOAuthManager *manager) {
    if (!manager) return -1;

    pthread_mutex_lock(&manager->token_mutex);

    if (!manager->token || !manager->token->refresh_token) {
        pthread_mutex_unlock(&manager->token_mutex);
        LOG_ERROR("No refresh token available");
        return -1;
    }

    KimiOAuthToken *new_token = refresh_token_internal(manager);
    if (!new_token) {
        pthread_mutex_unlock(&manager->token_mutex);
        return -1;
    }

    KimiOAuthToken *old_token = manager->token;
    manager->token = new_token;

    if (save_token_to_disk(new_token) != 0) {
        LOG_WARN("Failed to save refreshed token to disk");
    }

    kimi_oauth_token_free(old_token);
    pthread_mutex_unlock(&manager->token_mutex);

    return 0;
}

/**
 * Start background token refresh thread
 */
int kimi_oauth_start_refresh_thread(KimiOAuthManager *manager) {
    if (!manager) return -1;

    if (manager->refresh_thread_started) {
        LOG_DEBUG("Refresh thread already started");
        return 0;
    }

    manager->refresh_thread_running = 1;

    if (pthread_create(&manager->refresh_thread, NULL,
                       refresh_thread_func, manager) != 0) {
        LOG_ERROR("Failed to create refresh thread");
        manager->refresh_thread_running = 0;
        return -1;
    }

    manager->refresh_thread_started = 1;
    LOG_INFO("Background token refresh thread started");
    return 0;
}

/**
 * Stop background token refresh thread
 */
void kimi_oauth_stop_refresh_thread(KimiOAuthManager *manager) {
    if (!manager || !manager->refresh_thread_started) return;

    LOG_DEBUG("Stopping refresh thread...");
    manager->refresh_thread_running = 0;

    pthread_join(manager->refresh_thread, NULL);
    manager->refresh_thread_started = 0;

    LOG_DEBUG("Refresh thread stopped");
}

/**
 * Logout - delete stored tokens
 */
void kimi_oauth_logout(KimiOAuthManager *manager) {
    if (!manager) return;

    // Stop refresh thread
    kimi_oauth_stop_refresh_thread(manager);

    // Clear token from memory
    pthread_mutex_lock(&manager->token_mutex);
    if (manager->token) {
        kimi_oauth_token_free(manager->token);
        manager->token = NULL;
    }
    pthread_mutex_unlock(&manager->token_mutex);

    // Delete token file
    char *creds_dir = get_credentials_dir();
    if (creds_dir) {
        char token_path[PATH_MAX];
        if (snprintf(token_path, sizeof(token_path), "%s/oauth_token.json",
                     creds_dir) < (int)sizeof(token_path)) {
            if (unlink(token_path) == 0) {
                LOG_INFO("Deleted token file: %s", token_path);
            } else if (errno != ENOENT) {
                LOG_WARN("Failed to delete token file: %s", strerror(errno));
            }
        }
        free(creds_dir);
    }

    LOG_INFO("Logged out from Kimi");
}

/**
 * Get device headers for API requests
 * Caller must free with curl_slist_free_all
 */
struct curl_slist* kimi_oauth_get_device_headers(KimiOAuthManager *manager) {
    if (!manager || !manager->device_id) return NULL;

    struct curl_slist *headers = NULL;

    // X-Msh-Platform
    headers = curl_slist_append(headers, "X-Msh-Platform: " KIMI_PLATFORM);

    // X-Msh-Version
    headers = curl_slist_append(headers, "X-Msh-Version: " KIMI_VERSION);

    // X-Msh-Device-Name (hostname)
    char *device_name = get_device_name();
    if (device_name) {
        char header[256];
        snprintf(header, sizeof(header), "X-Msh-Device-Name: %s", device_name);
        headers = curl_slist_append(headers, header);
        free(device_name);
    }

    // X-Msh-Device-Model (OS version arch)
    char *device_model = get_device_model();
    if (device_model) {
        char header[256];
        snprintf(header, sizeof(header), "X-Msh-Device-Model: %s", device_model);
        headers = curl_slist_append(headers, header);
        free(device_model);
    }

    // X-Msh-Os-Version (kernel version)
    struct utsname info;
    if (uname(&info) == 0) {
        char header[256];
        snprintf(header, sizeof(header), "X-Msh-Os-Version: %s", info.release);
        headers = curl_slist_append(headers, header);
    }

    // X-Msh-Device-Id
    {
        char header[256];
        snprintf(header, sizeof(header), "X-Msh-Device-Id: %s", manager->device_id);
        headers = curl_slist_append(headers, header);
    }

    return headers;
}

/**
 * Free device authorization response
 */
void kimi_device_auth_free(KimiDeviceAuth *auth) {
    if (!auth) return;

    free(auth->user_code);
    free(auth->device_code);
    free(auth->verification_uri);
    free(auth->verification_uri_complete);
    free(auth);
}

/**
 * Free OAuth token
 */
void kimi_oauth_token_free(KimiOAuthToken *token) {
    if (!token) return;

    // Securely wipe sensitive data before freeing
    if (token->access_token) {
        explicit_bzero(token->access_token, strlen(token->access_token));
        free(token->access_token);
    }
    if (token->refresh_token) {
        explicit_bzero(token->refresh_token, strlen(token->refresh_token));
        free(token->refresh_token);
    }

    free(token->token_type);
    free(token->scope);
    free(token);
}
