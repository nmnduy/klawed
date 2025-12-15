/*
 * aws_bedrock.c - AWS Bedrock provider implementation
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "aws_bedrock.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <bsd/string.h>

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Hex encode a buffer
 */
static char* hex_encode(const unsigned char *data, size_t len) {
    // Check for overflow before allocation: len * 2 + 1
    if (len > (SIZE_MAX - 1) / 2) return NULL;
    char *hex = malloc(len * 2 + 1);
    if (!hex) return NULL;

    for (size_t i = 0; i < len; i++) {
        snprintf(hex + (i * 2), 3, "%02x", data[i]);
    }
    hex[len * 2] = '\0';
    return hex;
}

/**
 * URL encode a string (for AWS SigV4)
 */
static char* url_encode(const char *str, int encode_slash) {
    if (!str) return NULL;

    size_t len = strlen(str);
    // Check for overflow before allocation: len * 3 + 1
    if (len > (SIZE_MAX - 1) / 3) return NULL;
    char *encoded = malloc(len * 3 + 1);  // Worst case: every char becomes %XX
    if (!encoded) return NULL;

    char *out = encoded;
    for (const char *p = str; *p; p++) {
        if (isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            *out++ = *p;
        } else if (*p == '/' && !encode_slash) {
            *out++ = *p;
        } else {
            snprintf(out, 4, "%%%02X", (unsigned char)*p);
            out += 3;
        }
    }
    *out = '\0';
    return encoded;
}

/**
 * Get current timestamp in ISO8601 format (YYYYMMDDTHHMMSSZ)
 */
static char* get_iso8601_timestamp(void) {
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    char *timestamp = malloc(17);  // YYYYMMDDTHHMMSSZ + null
    if (!timestamp) return NULL;

    strftime(timestamp, 17, "%Y%m%dT%H%M%SZ", tm);
    return timestamp;
}

/**
 * Get current date in YYYYMMDD format
 */
static char* get_date_stamp(void) {
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    char *date = malloc(9);  // YYYYMMDD + null
    if (!date) return NULL;

    strftime(date, 9, "%Y%m%d", tm);
    return date;
}

/**
 * HMAC-SHA256
 */
static unsigned char* hmac_sha256(const unsigned char *key, size_t key_len,
                                   const unsigned char *data, size_t data_len,
                                   unsigned char *output) {
    unsigned int len = 32;
    return HMAC(EVP_sha256(), key, (int)key_len, data, data_len, output, &len);
}

/**
 * SHA256 hash
 */
static char* sha256_hash(const char *data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)data, strlen(data), hash);
    return hex_encode(hash, SHA256_DIGEST_LENGTH);
}

/**
 * Execute a command and return its output
 */
// Wrapper for executing shell commands; implemented via function pointer for test overrides
static char* exec_command_impl(const char *command) {
    FILE *fp = popen(command, "r");
    if (!fp) {
        LOG_ERROR("Failed to execute command: %s", command);
        return NULL;
    }

    char *output = NULL;
    size_t size = 0;
    size_t capacity = 1024;
    output = malloc(capacity);
    if (!output) {
        pclose(fp);
        return NULL;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp)) {
        size_t len = strlen(buffer);
        if (size + len + 1 > capacity) {
            capacity *= 2;
            char *new_output = realloc(output, capacity);
            if (!new_output) {
                free(output);
                pclose(fp);
                return NULL;
            }
            output = new_output;
        }
        // Use strlcpy for safety
        size_t remaining_space = capacity - size;
        strlcpy(output + size, buffer, remaining_space);
        size += len;
    }

    pclose(fp);

    // Remove trailing newline
    if (size > 0 && output[size - 1] == '\n') {
        output[size - 1] = '\0';
    }

    return output;
}

static char* (*exec_command)(const char *cmd) = exec_command_impl;
static int (*system_fn)(const char *cmd) = system;

void aws_bedrock_set_exec_command_fn(char* (*fn)(const char *cmd)) {
    exec_command = fn ? fn : exec_command_impl;
}

void aws_bedrock_set_system_fn(int (*fn)(const char *cmd)) {
    system_fn = fn ? fn : system;
}

// ============================================================================
// Public API Implementation
// ============================================================================

int bedrock_is_enabled(void) {
    const char *enabled = getenv(ENV_USE_BEDROCK);
    return enabled && (strcmp(enabled, "true") == 0 || strcmp(enabled, "1") == 0);
}

BedrockConfig* bedrock_config_init(const char *model_id) {
    LOG_INFO("Bedrock config init requested (model=%s)",
             model_id ? model_id : "(null)");

    if (!bedrock_is_enabled()) {
        LOG_INFO("Bedrock config init aborted: %s is not enabled",
                 ENV_USE_BEDROCK);
        return NULL;
    }

    BedrockConfig *config = calloc(1, sizeof(BedrockConfig));
    if (!config) {
        LOG_ERROR("Failed to allocate BedrockConfig");
        return NULL;
    }

    config->enabled = 1;

    // Get region from environment
    const char *region = getenv(ENV_AWS_REGION);
    if (!region) {
        region = "us-west-2";  // Default region
        LOG_WARN("AWS_REGION not set, using default: %s", region);
    } else {
        LOG_INFO("Bedrock config: using AWS region from %s=%s",
                 ENV_AWS_REGION, region);
    }
    config->region = strdup(region);

    // Get model ID
    if (model_id) {
        config->model_id = strdup(model_id);
        LOG_INFO("Bedrock config: model id set to %s", config->model_id);
    } else {
        LOG_ERROR("Model ID is required for Bedrock");
        bedrock_config_free(config);
        return NULL;
    }

    // Build endpoint URL
    config->endpoint = bedrock_build_endpoint(region, model_id);
    if (!config->endpoint) {
        LOG_ERROR("Failed to build Bedrock endpoint");
        bedrock_config_free(config);
        return NULL;
    }
    LOG_INFO("Bedrock config: computed endpoint %s", config->endpoint);

    // Load credentials (if available - may be null, will authenticate on first API call)
    const char *profile = getenv(ENV_AWS_PROFILE);
    LOG_INFO("Bedrock config: loading credentials (profile=%s)",
             profile ? profile : "default");
    config->creds = bedrock_load_credentials(profile, region);
    if (!config->creds) {
        LOG_WARN("No AWS credentials found during init - will authenticate on first API call");
        LOG_INFO("Bedrock config initialized without credentials: region=%s, model=%s", region, model_id);
    } else {
        LOG_INFO("Bedrock config initialized with credentials: region=%s, model=%s", region, model_id);
    }

    return config;
}

void bedrock_config_free(BedrockConfig *config) {
    if (!config) return;

    free(config->region);
    free(config->model_id);
    free(config->endpoint);
    bedrock_creds_free(config->creds);
    free(config);
}

AWSCredentials* bedrock_load_credentials(const char *profile, const char *region) {
    LOG_INFO("AWS credential load starting (profile=%s, region=%s)",
             profile ? profile : "default",
             region ? region : "default");
    LOG_DEBUG("=== AWS CREDENTIAL LOADING START ===");
    LOG_DEBUG("Requested profile: %s, region: %s", profile ? profile : "default", region ? region : "default");

    AWSCredentials *creds = calloc(1, sizeof(AWSCredentials));
    if (!creds) {
        LOG_ERROR("Failed to allocate AWSCredentials");
        return NULL;
    }

    // Try environment variables first
    LOG_DEBUG("=== CREDENTIAL SOURCE 1: ENVIRONMENT VARIABLES ===");
    LOG_DEBUG("Checking for AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY...");
    const char *access_key = getenv(ENV_AWS_ACCESS_KEY_ID);
    const char *secret_key = getenv(ENV_AWS_SECRET_ACCESS_KEY);
    const char *session_token = getenv(ENV_AWS_SESSION_TOKEN);
    LOG_INFO("Credential source env: access_key=%s secret_key=%s session_token=%s",
             access_key ? "present" : "missing",
             secret_key ? "present" : "missing",
             session_token ? "present" : "missing");

    LOG_DEBUG("  AWS_ACCESS_KEY_ID: %s", access_key ? "found" : "not found");
    LOG_DEBUG("  AWS_SECRET_ACCESS_KEY: %s", secret_key ? "found" : "not found");
    LOG_DEBUG("  AWS_SESSION_TOKEN: %s", session_token ? "found" : "not found");

    if (access_key && secret_key) {
        LOG_DEBUG("✓ Found credentials in environment variables");
        LOG_DEBUG("  Access key (first 10 chars): %.10s...", access_key);
        LOG_DEBUG("  Has session token: %s", session_token ? "yes" : "no");

        creds->access_key_id = strdup(access_key);
        creds->secret_access_key = strdup(secret_key);
        if (session_token) {
            creds->session_token = strdup(session_token);
        }
        creds->region = strdup(region ? region : "us-west-2");
        creds->profile = strdup(profile ? profile : "default");
        LOG_INFO("Loaded AWS credentials from environment variables (no validation)");
        LOG_DEBUG("=== AWS CREDENTIAL LOADING END (environment) ===");
        return creds;
    } else {
        LOG_INFO("Credential source env: required keys not both present, trying next source");
        LOG_DEBUG("✗ No credentials found in environment variables");
        if (!access_key) LOG_DEBUG("  Missing: AWS_ACCESS_KEY_ID");
        if (!secret_key) LOG_DEBUG("  Missing: AWS_SECRET_ACCESS_KEY");
    }

    // Try AWS CLI to get credentials (use export-credentials for temp creds support)
    LOG_DEBUG("=== CREDENTIAL SOURCE 2: AWS CLI EXPORT-CREDENTIALS ===");
    char command[512];
    const char *profile_arg = profile ? profile : "default";
    LOG_INFO("Credential source aws-cli export-credentials: profile=%s", profile_arg);
    LOG_DEBUG("Using profile: %s", profile_arg);

    // First try export-credentials which handles temporary credentials properly
    snprintf(command, sizeof(command),
             "aws configure export-credentials --profile %s --format env 2>/dev/null",
             profile_arg);
    LOG_INFO("Executing aws configure export-credentials for profile=%s", profile_arg);
    LOG_DEBUG("Executing command: %s", command);
    char *export_output = exec_command(command);

    if (export_output && strlen(export_output) > 0) {
        LOG_INFO("aws configure export-credentials returned data (length=%zu)", strlen(export_output));
        LOG_DEBUG("✓ Got output from export-credentials (length: %zu bytes)", strlen(export_output));
        LOG_DEBUG("  Output (first 200 chars): %.200s", export_output);

        // Parse the export output (format: export AWS_ACCESS_KEY_ID=xxx\nexport AWS_SECRET_ACCESS_KEY=yyy\n...)
        char *line = strtok(export_output, "\n");
        int found_access_key = 0, found_secret_key = 0, found_session_token = 0;
        while (line) {
            // Skip "export " prefix if present
            const char *value_start = line;
            if (strncmp(line, "export ", 7) == 0) {
                value_start = line + 7;
            }

            if (strncmp(value_start, "AWS_ACCESS_KEY_ID=", 18) == 0) {
                creds->access_key_id = strdup(value_start + 18);
                found_access_key = 1;
                LOG_DEBUG("  Found AWS_ACCESS_KEY_ID (first 10 chars): %.10s...", value_start + 18);
            } else if (strncmp(value_start, "AWS_SECRET_ACCESS_KEY=", 22) == 0) {
                creds->secret_access_key = strdup(value_start + 22);
                found_secret_key = 1;
                LOG_DEBUG("  Found AWS_SECRET_ACCESS_KEY");
            } else if (strncmp(value_start, "AWS_SESSION_TOKEN=", 18) == 0) {
                creds->session_token = strdup(value_start + 18);
                found_session_token = 1;
                LOG_DEBUG("  Found AWS_SESSION_TOKEN (length: %zu)", strlen(value_start + 18));
            }
            line = strtok(NULL, "\n");
        }
        free(export_output);

        LOG_DEBUG("Parsed credentials: access_key=%s, secret_key=%s, session_token=%s",
                  found_access_key ? "yes" : "no",
                  found_secret_key ? "yes" : "no",
                  found_session_token ? "yes" : "no");

        if (creds->access_key_id && creds->secret_access_key) {
            creds->region = strdup(region ? region : "us-west-2");
            creds->profile = strdup(profile_arg);
            LOG_INFO("Loaded AWS credentials from AWS CLI export-credentials (profile: %s, with_session_token: %s, no validation)",
                     profile_arg, creds->session_token ? "yes" : "no");
            LOG_DEBUG("=== AWS CREDENTIAL LOADING END (CLI export-credentials) ===");
            return creds;
        } else {
            LOG_INFO("aws configure export-credentials did not return a full credential set");
            LOG_DEBUG("✗ Incomplete credentials from export-credentials");
            if (!creds->access_key_id) LOG_DEBUG("  Missing: AWS_ACCESS_KEY_ID");
            if (!creds->secret_access_key) LOG_DEBUG("  Missing: AWS_SECRET_ACCESS_KEY");
        }
    } else {
        LOG_INFO("aws configure export-credentials returned no output; falling back to other sources");
        LOG_DEBUG("✗ No output from export-credentials command");
        free(export_output);
    }

    // Fallback: Try aws configure get for static credentials
    LOG_DEBUG("=== CREDENTIAL SOURCE 3: AWS CLI CONFIGURE GET ===");
    LOG_INFO("Credential source aws-cli configure get: profile=%s", profile_arg);
    LOG_DEBUG("Using profile: %s", profile_arg);

    snprintf(command, sizeof(command),
             "aws configure get aws_access_key_id --profile %s 2>/dev/null",
             profile_arg);
    LOG_DEBUG("Executing command: %s", command);
    char *key_id = exec_command(command);

    snprintf(command, sizeof(command),
             "aws configure get aws_secret_access_key --profile %s 2>/dev/null",
             profile_arg);
    LOG_DEBUG("Executing command: %s", command);
    char *secret = exec_command(command);

    LOG_DEBUG("Results: access_key=%s, secret_key=%s",
              (key_id && strlen(key_id) > 0) ? "found" : "not found",
              (secret && strlen(secret) > 0) ? "found" : "not found");

    if (key_id && secret && strlen(key_id) > 0 && strlen(secret) > 0) {
        LOG_DEBUG("✓ Found static credentials in AWS config");
        LOG_DEBUG("  Access key (first 10 chars): %.10s...", key_id);

        creds->access_key_id = key_id;
        creds->secret_access_key = secret;
        creds->region = strdup(region ? region : "us-west-2");
        creds->profile = strdup(profile_arg);
        LOG_INFO("Loaded AWS credentials from AWS CLI config (profile: %s, no validation)", profile_arg);
        LOG_DEBUG("=== AWS CREDENTIAL LOADING END (CLI config) ===");
        return creds;
    } else {
        LOG_INFO("AWS CLI config did not provide complete static credentials");
        LOG_DEBUG("✗ No static credentials found in AWS config");
        if (!key_id || strlen(key_id) == 0) LOG_DEBUG("  Missing: aws_access_key_id");
        if (!secret || strlen(secret) == 0) LOG_DEBUG("  Missing: aws_secret_access_key");
        free(key_id);
        free(secret);
    }

    // Try AWS SSO - only load cached credentials, don't authenticate
    LOG_DEBUG("=== CREDENTIAL SOURCE 4: AWS SSO (CACHED ONLY) ===");
    LOG_DEBUG("Checking if profile uses SSO...");
    LOG_INFO("Credential source AWS SSO: inspecting profile=%s", profile_arg);

    // Check if profile uses SSO
    snprintf(command, sizeof(command),
             "aws configure get sso_start_url --profile %s 2>/dev/null",
             profile_arg);
    LOG_DEBUG("Executing command: %s", command);
    char *sso_url = exec_command(command);

    if (sso_url && strlen(sso_url) > 0) {
        LOG_DEBUG("✓ Profile %s has SSO configuration", profile_arg);
        LOG_DEBUG("  SSO start URL: %s", sso_url);
        LOG_INFO("Profile %s uses AWS SSO, attempting to get cached credentials", profile_arg);
        free(sso_url);

        // Try to export credentials using AWS CLI (only from cache, don't authenticate)
        snprintf(command, sizeof(command),
                 "aws configure export-credentials --profile %s --format env 2>/dev/null",
                 profile_arg);
        LOG_DEBUG("Executing command: %s", command);
        char *sso_export_output = exec_command(command);

        if (sso_export_output && strlen(sso_export_output) > 0) {
            LOG_DEBUG("✓ Got SSO credentials from cache (length: %zu bytes)", strlen(sso_export_output));
            LOG_DEBUG("  Output (first 200 chars): %.200s", sso_export_output);

            // Parse the export output (format: AWS_ACCESS_KEY_ID=xxx\nAWS_SECRET_ACCESS_KEY=yyy\n...)
            char *line = strtok(sso_export_output, "\n");
            int found_access_key = 0, found_secret_key = 0, found_session_token = 0;
            while (line) {
                // Skip "export " prefix if present
                const char *value_start = line;
                if (strncmp(line, "export ", 7) == 0) {
                    value_start = line + 7;
                }

                if (strncmp(value_start, "AWS_ACCESS_KEY_ID=", 18) == 0) {
                    creds->access_key_id = strdup(value_start + 18);
                    found_access_key = 1;
                    LOG_DEBUG("  Found AWS_ACCESS_KEY_ID (first 10 chars): %.10s...", value_start + 18);
                } else if (strncmp(value_start, "AWS_SECRET_ACCESS_KEY=", 22) == 0) {
                    creds->secret_access_key = strdup(value_start + 22);
                    found_secret_key = 1;
                    LOG_DEBUG("  Found AWS_SECRET_ACCESS_KEY");
                } else if (strncmp(value_start, "AWS_SESSION_TOKEN=", 18) == 0) {
                    creds->session_token = strdup(value_start + 18);
                    found_session_token = 1;
                    LOG_DEBUG("  Found AWS_SESSION_TOKEN (length: %zu)", strlen(value_start + 18));
                }
                line = strtok(NULL, "\n");
            }
            free(sso_export_output);

            LOG_DEBUG("Parsed SSO credentials: access_key=%s, secret_key=%s, session_token=%s",
                      found_access_key ? "yes" : "no",
                      found_secret_key ? "yes" : "no",
                      found_session_token ? "yes" : "no");

            if (creds->access_key_id && creds->secret_access_key) {
                creds->region = strdup(region ? region : "us-west-2");
                creds->profile = strdup(profile_arg);
                LOG_INFO("Loaded AWS credentials from SSO cache (no validation)");
                LOG_DEBUG("=== AWS CREDENTIAL LOADING END (SSO cache) ===");
                return creds;
            } else {
                LOG_DEBUG("✗ Incomplete SSO credentials from cache");
                if (!creds->access_key_id) LOG_DEBUG("  Missing: AWS_ACCESS_KEY_ID");
                if (!creds->secret_access_key) LOG_DEBUG("  Missing: AWS_SECRET_ACCESS_KEY");
                LOG_INFO("Cached SSO credentials were incomplete");
            }
        } else {
            LOG_INFO("No cached SSO credentials available via export-credentials");
            LOG_DEBUG("✗ No SSO credentials in cache (empty output)");
            free(sso_export_output);
        }
    } else {
        LOG_DEBUG("✗ Profile %s does not use SSO (no sso_start_url)", profile_arg);
        LOG_INFO("Profile %s does not have SSO configuration", profile_arg);
        free(sso_url);
    }

    LOG_ERROR("Failed to load AWS credentials from any source");
    LOG_INFO("AWS credential loader exhausted all sources without success (profile=%s)", profile_arg);
    LOG_INFO("Note: Credentials will be obtained on first API call if authentication is needed");
    LOG_DEBUG("=== AWS CREDENTIAL LOADING FAILED ===");
    bedrock_creds_free(creds);
    return NULL;
}

void bedrock_creds_free(AWSCredentials *creds) {
    if (!creds) return;

    free(creds->access_key_id);
    free(creds->secret_access_key);
    free(creds->session_token);
    free(creds->region);
    free(creds->profile);
    free(creds);
}

int bedrock_validate_credentials(AWSCredentials *creds, const char *profile) {
    LOG_DEBUG("=== AWS CREDENTIAL VALIDATION START ===");
    LOG_DEBUG("Profile: %s", profile ? profile : "default");

    if (!creds) {
        LOG_ERROR("Credentials structure is NULL");
        LOG_DEBUG("=== AWS CREDENTIAL VALIDATION END (null creds) ===");
        return 0;
    }

    if (!creds->access_key_id) {
        LOG_ERROR("Access key ID is NULL");
        LOG_DEBUG("=== AWS CREDENTIAL VALIDATION END (null access key) ===");
        return 0;
    }

    if (!creds->secret_access_key) {
        LOG_ERROR("Secret access key is NULL");
        LOG_DEBUG("=== AWS CREDENTIAL VALIDATION END (null secret key) ===");
        return 0;
    }

    LOG_DEBUG("Credentials to validate:");
    LOG_DEBUG("  Access key (first 10 chars): %.10s...", creds->access_key_id);
    LOG_DEBUG("  Has secret key: yes");
    LOG_DEBUG("  Has session token: %s", creds->session_token ? "yes" : "no");
    LOG_DEBUG("  Region: %s", creds->region ? creds->region : "us-west-2");
    LOG_DEBUG("  Profile: %s", creds->profile ? creds->profile : "default");

    (void)profile;  // Unused parameter

    // Build command properly with session token if present
    char env_cmd[2048];
    if (creds->session_token) {
        snprintf(env_cmd, sizeof(env_cmd),
                 "AWS_ACCESS_KEY_ID='%s' AWS_SECRET_ACCESS_KEY='%s' AWS_SESSION_TOKEN='%s' aws sts get-caller-identity --region %s 2>&1",
                 creds->access_key_id,
                 creds->secret_access_key,
                 creds->session_token,
                 creds->region ? creds->region : "us-west-2");
        LOG_DEBUG("Using session token in validation command");
    } else {
        snprintf(env_cmd, sizeof(env_cmd),
                 "AWS_ACCESS_KEY_ID='%s' AWS_SECRET_ACCESS_KEY='%s' aws sts get-caller-identity --region %s 2>&1",
                 creds->access_key_id,
                 creds->secret_access_key,
                 creds->region ? creds->region : "us-west-2");
        LOG_DEBUG("No session token in validation command");
    }

    LOG_DEBUG("Executing validation command: aws sts get-caller-identity --region %s",
              creds->region ? creds->region : "us-west-2");
    char *output = exec_command(env_cmd);
    int valid = 0;

    if (output) {
        LOG_DEBUG("Validation command returned output (length: %zu bytes)", strlen(output));
        LOG_DEBUG("Output (first 500 chars): %.500s", output);

        // Check if output contains error messages
        if (strstr(output, "ExpiredToken")) {
            LOG_WARN("✗ Credentials are invalid: ExpiredToken");
            valid = 0;
        } else if (strstr(output, "InvalidToken")) {
            LOG_WARN("✗ Credentials are invalid: InvalidToken");
            valid = 0;
        } else if (strstr(output, "InvalidClientTokenId")) {
            LOG_WARN("✗ Credentials are invalid: InvalidClientTokenId");
            valid = 0;
        } else if (strstr(output, "AccessDenied")) {
            LOG_WARN("✗ Credentials are invalid: AccessDenied");
            valid = 0;
        } else if (strstr(output, "UserId") || strstr(output, "Account")) {
            LOG_INFO("✓ AWS credentials validated successfully");
            LOG_DEBUG("Output contains UserId or Account, credentials are valid");
            valid = 1;
        } else {
            LOG_WARN("✗ Unexpected output from credential validation (no error, no success markers)");
            LOG_DEBUG("Full output: %s", output);
            valid = 0;
        }
        free(output);
    } else {
        LOG_ERROR("✗ Failed to execute credential validation command (exec_command returned NULL)");
        valid = 0;
    }

    LOG_DEBUG("Validation result: %s", valid ? "VALID" : "INVALID");
    LOG_DEBUG("=== AWS CREDENTIAL VALIDATION END ===");
    return valid;
}

int bedrock_authenticate(const char *profile) {
    // Log authentication attempt at the start
    LOG_DEBUG("=== AWS AUTHENTICATION START ===");
    LOG_INFO("Authenticating with AWS Bedrock (profile: %s)", profile ? profile : "default");

    // Check for custom authentication command first
    const char *custom_auth_cmd = getenv(ENV_AWS_AUTH_COMMAND);
    char command[1024];

    if (custom_auth_cmd && strlen(custom_auth_cmd) > 0) {
        // Use custom authentication command
        LOG_INFO("=== AWS AUTHENTICATION USING CUSTOM COMMAND ===");
        LOG_INFO("AWS_AUTH_COMMAND=%s", custom_auth_cmd);
        LOG_DEBUG("Using custom authentication command: %s", custom_auth_cmd);
        LOG_INFO("Using custom authentication command from AWS_AUTH_COMMAND");
        printf("\nAWS credentials not found or expired. Starting authentication...\n");
        printf("Running custom auth command...\n\n");

        snprintf(command, sizeof(command), "%s", custom_auth_cmd);
    } else {
        // Use default AWS SSO login
        if (!profile) {
            profile = getenv(ENV_AWS_PROFILE);
            if (!profile) profile = "default";
        }

        LOG_DEBUG("Using AWS SSO login for profile: %s", profile);
        LOG_INFO("Starting AWS SSO login for profile: %s", profile);
        printf("\nAWS credentials not found or expired. Starting authentication...\n");
        printf("Running: aws sso login --profile %s\n\n", profile);

        snprintf(command, sizeof(command), "aws sso login --profile %s", profile);
    }

    LOG_DEBUG("Executing authentication command...");
    int result = system_fn(command);

    if (result == 0) {
        LOG_DEBUG("Authentication command completed with exit code 0");
        LOG_INFO("Authentication completed successfully");
        printf("\nAuthentication successful! Continuing...\n\n");
        return 0;
    } else {
        LOG_DEBUG("Authentication command failed with exit code %d", result);
        LOG_ERROR("Authentication failed with exit code: %d", result);
        printf("\nAuthentication failed. Please check your AWS configuration.\n");
        return -1;
    }
}

int bedrock_handle_auth_error(BedrockConfig *config, long http_status, const char *error_message, const char *response_body) {
    LOG_DEBUG("=== BEDROCK AUTH ERROR HANDLER START ===");
    LOG_DEBUG("HTTP status: %ld", http_status);
    LOG_DEBUG("Error message: %s", error_message ? error_message : "(null)");
    LOG_DEBUG("Response body (first 500 chars): %.500s", response_body ? response_body : "(null)");

    if (!config) {
        LOG_ERROR("Config is NULL in auth error handler");
        LOG_DEBUG("=== BEDROCK AUTH ERROR HANDLER END (config null) ===");
        return 0;
    }

    // Check if this is a 400, 401, or 403 error that might indicate expired AWS credentials
    LOG_DEBUG("Checking HTTP status code...");
    if (http_status != 400 && http_status != 401 && http_status != 403) {
        LOG_DEBUG("HTTP status %ld is not an auth error (expected 400, 401, or 403)", http_status);
        LOG_DEBUG("=== BEDROCK AUTH ERROR HANDLER END (not auth status) ===");
        return 0;
    }
    LOG_DEBUG("HTTP status %ld matches auth error pattern", http_status);

    // Store current access key ID for comparison (to detect external rotation)
    char *old_access_key = NULL;
    if (config->creds && config->creds->access_key_id) {
        old_access_key = strdup(config->creds->access_key_id);
        LOG_DEBUG("Stored current access key ID (first 10 chars): %.10s...", old_access_key);
    } else {
        LOG_DEBUG("No current access key ID to store");
    }

    // Check for AWS authentication error patterns in both error_message and response_body
    LOG_DEBUG("Checking for auth error patterns in error message and response body...");
    int is_auth_error = 0;
    const char *sources[] = {error_message, response_body, NULL};
    for (int i = 0; sources[i] != NULL; i++) {
        if (!sources[i]) {
            LOG_DEBUG("  Source %d is NULL, skipping", i);
            continue;
        }

        LOG_DEBUG("  Checking source %d (length: %zu)...", i, strlen(sources[i]));

        if (strstr(sources[i], "ExpiredToken") ||
            strstr(sources[i], "InvalidToken") ||
            strstr(sources[i], "InvalidClientTokenId") ||
            strstr(sources[i], "AccessDenied") ||
            strstr(sources[i], "TokenExpired") ||
            strstr(sources[i], "SignatureDoesNotMatch") ||
            strstr(sources[i], "UnrecognizedClientException") ||
            strstr(sources[i], "No auth credentials found") ||
            strstr(sources[i], "credentials") ||
            strstr(sources[i], "unauthorized") ||
            strstr(sources[i], "authentication")) {
            is_auth_error = 1;
            LOG_DEBUG("  ✓ Found auth error pattern in source %d", i);
            break;
        } else {
            LOG_DEBUG("  ✗ No auth error pattern found in source %d", i);
        }
    }

    if (!is_auth_error) {
        LOG_DEBUG("No auth error patterns detected, this is not an authentication error");
        LOG_DEBUG("=== BEDROCK AUTH ERROR HANDLER END (not auth error) ===");
        free(old_access_key);
        return 0;
    }

    LOG_INFO("Detected authentication error, beginning credential refresh process");

    // Determine which profile to use for authentication
    const char *profile = NULL;
    if (config->creds && config->creds->profile) {
        profile = config->creds->profile;
        LOG_DEBUG("Using profile from config: %s", profile);
    } else {
        LOG_DEBUG("No profile in config, will use default");
    }

    // Store region before any operations
    const char *region = config->region;
    LOG_DEBUG("Using region: %s", region);

    // Check if credentials were rotated externally (e.g., by another process)
    LOG_DEBUG("=== CHECKING FOR EXTERNAL CREDENTIAL ROTATION ===");
    LOG_DEBUG("Loading fresh credentials from all sources...");
    AWSCredentials *fresh_creds = bedrock_load_credentials(profile, region);
    if (fresh_creds) {
        LOG_DEBUG("Successfully loaded fresh credentials");
        LOG_DEBUG("Fresh access key (first 10 chars): %.10s...", fresh_creds->access_key_id ? fresh_creds->access_key_id : "(null)");

        // Compare access keys to detect external rotation
        if (old_access_key && fresh_creds->access_key_id &&
            strcmp(old_access_key, fresh_creds->access_key_id) != 0) {
            // Credentials were rotated externally, use the new ones
            LOG_INFO("✓ Detected externally rotated credentials (access keys differ)");
            LOG_DEBUG("Old key (first 10 chars): %.10s...", old_access_key);
            LOG_DEBUG("New key (first 10 chars): %.10s...", fresh_creds->access_key_id);
            printf("\nDetected new AWS credentials from external source. Using updated credentials...\n\n");

            // Free old credentials and use new ones
            if (config->creds) {
                bedrock_creds_free(config->creds);
            }
            config->creds = fresh_creds;
            free(old_access_key);
            LOG_DEBUG("=== BEDROCK AUTH ERROR HANDLER END (external rotation) ===");
            return 1;  // Signal retry with new credentials
        }

        // Credentials are the same, need to validate them
        LOG_DEBUG("✗ Credentials unchanged (access keys match)");
        LOG_DEBUG("Validating current credentials...");
        if (bedrock_validate_credentials(fresh_creds, profile) == 1) {
            // Credentials are valid but request failed - might be a transient error
            LOG_INFO("✓ Current credentials are valid, this may be a transient error (not credential-related)");
            bedrock_creds_free(fresh_creds);
            free(old_access_key);
            LOG_DEBUG("=== BEDROCK AUTH ERROR HANDLER END (transient error) ===");
            return 0;  // Don't retry, not a credential issue
        }

        // Credentials are invalid/expired, need to re-authenticate
        LOG_DEBUG("✗ Credentials are invalid/expired");
        bedrock_creds_free(fresh_creds);
    } else {
        LOG_DEBUG("✗ Failed to load fresh credentials from any source");
    }

    // Try to refresh AWS credentials
    LOG_DEBUG("=== INITIATING CREDENTIAL REFRESH ===");
    LOG_WARN("AWS credentials expired or invalid (HTTP %ld), attempting to re-authenticate", http_status);
    printf("\nAWS credentials are expired or invalid. Starting re-authentication...\n");

    // Attempt to re-authenticate
    LOG_DEBUG("Calling bedrock_authenticate with profile: %s", profile ? profile : "default");
    int auth_result = bedrock_authenticate(profile);
    if (auth_result != 0) {
        LOG_ERROR("✗ AWS credential refresh failed (exit code: %d)", auth_result);
        free(old_access_key);
        LOG_DEBUG("=== BEDROCK AUTH ERROR HANDLER END (auth failed) ===");
        return 0;
    }

    LOG_DEBUG("✓ Authentication completed successfully");

    // Authentication successful, reload credentials
    LOG_DEBUG("=== RELOADING CREDENTIALS AFTER AUTH ===");
    LOG_INFO("Re-authentication successful, reloading AWS credentials");

    // Free old credentials
    if (config->creds) {
        bedrock_creds_free(config->creds);
        config->creds = NULL;
        LOG_DEBUG("Freed old credentials");
    }

    // Reload credentials
    LOG_DEBUG("Loading credentials from all sources...");
    AWSCredentials *new_creds = bedrock_load_credentials(profile, region);
    if (!new_creds) {
        LOG_ERROR("✗ Failed to reload AWS credentials after authentication");
        free(old_access_key);
        LOG_DEBUG("=== BEDROCK AUTH ERROR HANDLER END (reload failed) ===");
        return 0;
    }

    LOG_DEBUG("✓ Successfully reloaded credentials");
    LOG_DEBUG("New access key (first 10 chars): %.10s...", new_creds->access_key_id);

    // Update config with fresh credentials
    config->creds = new_creds;

    printf("Credentials refreshed successfully. Retrying request...\n\n");
    LOG_INFO("AWS credentials successfully refreshed and reloaded");
    free(old_access_key);
    LOG_DEBUG("=== BEDROCK AUTH ERROR HANDLER END (success) ===");
    return 1;  // Signal that retry is appropriate
}

char* bedrock_build_endpoint(const char *region, const char *model_id) {
    if (!region || !model_id) {
        return NULL;
    }

    // Bedrock endpoint: https://bedrock-runtime.{region}.amazonaws.com/model/{model-id}/invoke
    size_t len = strlen(region) + strlen(model_id) + 128;
    char *endpoint = malloc(len);
    if (!endpoint) {
        return NULL;
    }

    snprintf(endpoint, len, "https://bedrock-runtime.%s.amazonaws.com/model/%s/invoke",
             region, model_id);

    return endpoint;
}

char* bedrock_build_streaming_endpoint(const char *region, const char *model_id) {
    if (!region || !model_id) {
        return NULL;
    }

    // Bedrock streaming endpoint: https://bedrock-runtime.{region}.amazonaws.com/model/{model-id}/invoke-with-response-stream
    size_t len = strlen(region) + strlen(model_id) + 128;
    char *endpoint = malloc(len);
    if (!endpoint) {
        return NULL;
    }

    snprintf(endpoint, len, "https://bedrock-runtime.%s.amazonaws.com/model/%s/invoke-with-response-stream",
             region, model_id);

    return endpoint;
}

/**
 * Helper: Convert OpenAI image_url format to Anthropic image format
 *
 * OpenAI format:
 * {
 *   "type": "image_url",
 *   "image_url": {
 *     "url": "data:image/jpeg;base64,<base64_data>"
 *   }
 * }
 *
 * Anthropic format:
 * {
 *   "type": "image",
 *   "source": {
 *     "type": "base64",
 *     "media_type": "image/jpeg",
 *     "data": "<base64_data>"
 *   }
 * }
 *
 * Returns a new cJSON object in Anthropic format, or NULL on error.
 */
static cJSON* convert_image_url_to_anthropic(cJSON *image_url_block) {
    if (!image_url_block) return NULL;

    cJSON *type = cJSON_GetObjectItem(image_url_block, "type");
    if (!type || !cJSON_IsString(type) || strcmp(type->valuestring, "image_url") != 0) {
        return NULL;  // Not an image_url block
    }

    cJSON *image_url = cJSON_GetObjectItem(image_url_block, "image_url");
    if (!image_url) return NULL;

    cJSON *url = cJSON_GetObjectItem(image_url, "url");
    if (!url || !cJSON_IsString(url)) return NULL;

    const char *url_str = url->valuestring;

    // Parse data URL: "data:image/jpeg;base64,<base64_data>"
    if (strncmp(url_str, "data:", 5) != 0) {
        LOG_WARN("Image URL is not a data URL, skipping conversion: %s", url_str);
        return NULL;
    }

    // Find the media type
    const char *media_start = url_str + 5;  // Skip "data:"
    const char *semicolon = strchr(media_start, ';');
    if (!semicolon) {
        LOG_WARN("Invalid data URL format (no semicolon): %s", url_str);
        return NULL;
    }

    // Extract media type (e.g., "image/jpeg")
    size_t media_len = (size_t)(semicolon - media_start);
    char *media_type = malloc(media_len + 1);
    if (!media_type) return NULL;
    strlcpy(media_type, media_start, media_len + 1);

    // Find base64 data (after "base64,")
    const char *base64_marker = strstr(semicolon, "base64,");
    if (!base64_marker) {
        LOG_WARN("Invalid data URL format (no base64 marker): %s", url_str);
        free(media_type);
        return NULL;
    }

    const char *base64_data = base64_marker + 7;  // Skip "base64,"

    LOG_DEBUG("Converting image_url to Anthropic image format (media_type: %s)", media_type);

    // Create Anthropic format
    cJSON *anthropic_image = cJSON_CreateObject();
    cJSON_AddStringToObject(anthropic_image, "type", "image");

    cJSON *source = cJSON_CreateObject();
    cJSON_AddStringToObject(source, "type", "base64");
    cJSON_AddStringToObject(source, "media_type", media_type);
    cJSON_AddStringToObject(source, "data", base64_data);

    cJSON_AddItemToObject(anthropic_image, "source", source);

    free(media_type);

    return anthropic_image;
}

/**
 * Helper: Convert a content array from OpenAI format to Anthropic format
 * Handles image_url -> image conversion
 *
 * Returns a new cJSON array with converted content blocks.
 */
static cJSON* convert_content_array_to_anthropic(cJSON *openai_content) {
    if (!openai_content || !cJSON_IsArray(openai_content)) {
        return NULL;
    }

    cJSON *anthropic_content = cJSON_CreateArray();

    cJSON *block = NULL;
    cJSON_ArrayForEach(block, openai_content) {
        cJSON *type = cJSON_GetObjectItem(block, "type");
        if (!type || !cJSON_IsString(type)) {
            // Unknown block type, copy as-is
            cJSON_AddItemToArray(anthropic_content, cJSON_Duplicate(block, 1));
            continue;
        }

        if (strcmp(type->valuestring, "image_url") == 0) {
            // Convert image_url to Anthropic image format
            cJSON *anthropic_image = convert_image_url_to_anthropic(block);
            if (anthropic_image) {
                cJSON_AddItemToArray(anthropic_content, anthropic_image);
            } else {
                LOG_WARN("Failed to convert image_url block, skipping");
            }
        } else {
            // Other block types (text, etc.) - copy as-is
            cJSON_AddItemToArray(anthropic_content, cJSON_Duplicate(block, 1));
        }
    }

    return anthropic_content;
}

char* bedrock_convert_request(const char *openai_request) {
    // AWS Bedrock uses Anthropic's native format, not OpenAI format
    // We need to convert from OpenAI to Anthropic format

    LOG_DEBUG("=== BEDROCK REQUEST CONVERSION START ===");
    LOG_DEBUG("OpenAI request length: %zu bytes", strlen(openai_request));

    cJSON *openai_json = cJSON_Parse(openai_request);
    if (!openai_json) {
        LOG_ERROR("Failed to parse OpenAI request");
        return NULL;
    }

    // Extract necessary fields
    cJSON *messages = cJSON_GetObjectItem(openai_json, "messages");
    cJSON *tools = cJSON_GetObjectItem(openai_json, "tools");
    cJSON *max_tokens = cJSON_GetObjectItem(openai_json, "max_completion_tokens");

    // Build Anthropic format request
    cJSON *anthropic_json = cJSON_CreateObject();

    // Add max_tokens (required by Anthropic)
    if (max_tokens && cJSON_IsNumber(max_tokens)) {
        cJSON_AddNumberToObject(anthropic_json, "max_tokens", max_tokens->valueint);
    } else {
        cJSON_AddNumberToObject(anthropic_json, "max_tokens", 8192);
    }

    // Convert messages from OpenAI to Anthropic format
    cJSON *anthropic_messages = cJSON_CreateArray();
    cJSON *system_prompt = NULL;

    if (messages && cJSON_IsArray(messages)) {
        cJSON *msg = NULL;
        cJSON_ArrayForEach(msg, messages) {
            cJSON *role = cJSON_GetObjectItem(msg, "role");
            cJSON *content = cJSON_GetObjectItem(msg, "content");

            if (!role || !cJSON_IsString(role)) continue;

            const char *role_str = role->valuestring;

            // Extract system message separately
            if (strcmp(role_str, "system") == 0) {
                if (cJSON_IsString(content)) {
                    system_prompt = cJSON_CreateString(content->valuestring);
                } else if (cJSON_IsArray(content)) {
                    // Extract text from content array
                    cJSON *item = cJSON_GetArrayItem(content, 0);
                    if (item) {
                        cJSON *text = cJSON_GetObjectItem(item, "text");
                        if (text && cJSON_IsString(text)) {
                            system_prompt = cJSON_CreateString(text->valuestring);
                        }
                    }
                }
                continue;
            }

            // Convert user/assistant messages
            cJSON *anthropic_msg = cJSON_CreateObject();

            // Map roles
            if (strcmp(role_str, "assistant") == 0) {
                cJSON_AddStringToObject(anthropic_msg, "role", "assistant");

                // Handle tool calls
                cJSON *tool_calls = cJSON_GetObjectItem(msg, "tool_calls");
                if (tool_calls && cJSON_IsArray(tool_calls)) {
                    cJSON *content_array = cJSON_CreateArray();

                    // Add text content if present
                    if (cJSON_IsString(content) && strlen(content->valuestring) > 0) {
                        cJSON *text_block = cJSON_CreateObject();
                        cJSON_AddStringToObject(text_block, "type", "text");
                        cJSON_AddStringToObject(text_block, "text", content->valuestring);
                        cJSON_AddItemToArray(content_array, text_block);
                    }

                    // Add tool use blocks
                    cJSON *tool_call = NULL;
                    cJSON_ArrayForEach(tool_call, tool_calls) {
                        cJSON *tool_use_block = cJSON_CreateObject();
                        cJSON_AddStringToObject(tool_use_block, "type", "tool_use");

                        cJSON *id = cJSON_GetObjectItem(tool_call, "id");
                        if (id && cJSON_IsString(id)) {
                            cJSON_AddStringToObject(tool_use_block, "id", id->valuestring);
                        }

                        cJSON *function = cJSON_GetObjectItem(tool_call, "function");
                        if (function) {
                            cJSON *name = cJSON_GetObjectItem(function, "name");
                            cJSON *arguments = cJSON_GetObjectItem(function, "arguments");

                            if (name && cJSON_IsString(name)) {
                                cJSON_AddStringToObject(tool_use_block, "name", name->valuestring);
                            }

                            if (arguments && cJSON_IsString(arguments)) {
                                cJSON *input = cJSON_Parse(arguments->valuestring);
                                if (input) {
                                    cJSON_AddItemToObject(tool_use_block, "input", input);
                                }
                            }
                        }

                        cJSON_AddItemToArray(content_array, tool_use_block);
                    }

                    // Only add content if array is non-empty
                    if (cJSON_GetArraySize(content_array) > 0) {
                        cJSON_AddItemToObject(anthropic_msg, "content", content_array);
                    } else {
                        // No content blocks - skip this message
                        cJSON_Delete(content_array);
                        cJSON_Delete(anthropic_msg);
                        anthropic_msg = NULL;
                        LOG_WARN("Skipping assistant message with no content blocks");
                        continue;
                    }
                } else {
                    // Simple text content - must be non-empty
                    if (cJSON_IsString(content) && strlen(content->valuestring) > 0) {
                        cJSON_AddStringToObject(anthropic_msg, "content", content->valuestring);
                    } else {
                        // No valid content - skip this message
                        cJSON_Delete(anthropic_msg);
                        anthropic_msg = NULL;
                        LOG_WARN("Skipping assistant message with null or empty content");
                        continue;
                    }
                }
            } else if (strcmp(role_str, "user") == 0) {
                cJSON_AddStringToObject(anthropic_msg, "role", "user");

                if (cJSON_IsString(content) && strlen(content->valuestring) > 0) {
                    cJSON_AddStringToObject(anthropic_msg, "content", content->valuestring);
                } else if (cJSON_IsArray(content) && cJSON_GetArraySize(content) > 0) {
                    // Convert content array (handles image_url -> image conversion)
                    cJSON *anthropic_content = convert_content_array_to_anthropic(content);
                    if (anthropic_content && cJSON_GetArraySize(anthropic_content) > 0) {
                        cJSON_AddItemToObject(anthropic_msg, "content", anthropic_content);
                    } else {
                        // Conversion failed or empty - skip this message
                        if (anthropic_content) cJSON_Delete(anthropic_content);
                        cJSON_Delete(anthropic_msg);
                        anthropic_msg = NULL;
                        LOG_WARN("Skipping user message with invalid content array");
                        continue;
                    }
                } else {
                    // No valid content - skip this message
                    cJSON_Delete(anthropic_msg);
                    anthropic_msg = NULL;
                    LOG_WARN("Skipping user message with null or empty content");
                    continue;
                }
            } else if (strcmp(role_str, "tool") == 0) {
                // Tool results are handled as user messages with tool_result blocks
                LOG_DEBUG("Converting tool result message to Anthropic format");
                cJSON *tool_call_id = cJSON_GetObjectItem(msg, "tool_call_id");

                if (tool_call_id && cJSON_IsString(tool_call_id)) {
                    LOG_DEBUG("  tool_call_id: %s", tool_call_id->valuestring);
                    cJSON *tool_msg = cJSON_CreateObject();
                    cJSON_AddStringToObject(tool_msg, "role", "user");

                    cJSON *content_array = cJSON_CreateArray();
                    cJSON *tool_result_block = cJSON_CreateObject();
                    cJSON_AddStringToObject(tool_result_block, "type", "tool_result");
                    cJSON_AddStringToObject(tool_result_block, "tool_use_id", tool_call_id->valuestring);

                    // AWS Bedrock/Anthropic requires tool_result.content to be a string,
                    // not a JSON object. Keep it as a string even if it contains JSON.
                    if (cJSON_IsString(content)) {
                        cJSON_AddStringToObject(tool_result_block, "content", content->valuestring);
                        LOG_DEBUG("  tool_result content length: %zu chars", strlen(content->valuestring));
                    } else if (cJSON_IsArray(content)) {
                        // If content is already an array of content blocks, use it directly
                        cJSON_AddItemToObject(tool_result_block, "content", cJSON_Duplicate(content, 1));
                        LOG_DEBUG("  tool_result content: array with %d blocks", cJSON_GetArraySize(content));
                    } else {
                        // Fallback: convert any other type to string
                        char *content_str = cJSON_PrintUnformatted(content);
                        if (content_str) {
                            cJSON_AddStringToObject(tool_result_block, "content", content_str);
                            free(content_str);
                        } else {
                            cJSON_AddStringToObject(tool_result_block, "content", "");
                        }
                        LOG_WARN("  tool_result content was not a string, converted to JSON string");
                    }

                    cJSON_AddItemToArray(content_array, tool_result_block);
                    cJSON_AddItemToObject(tool_msg, "content", content_array);
                    cJSON_AddItemToArray(anthropic_messages, tool_msg);
                }
                continue;
            }

            if (anthropic_msg && cJSON_GetObjectItem(anthropic_msg, "role")) {
                cJSON_AddItemToArray(anthropic_messages, anthropic_msg);
            } else {
                cJSON_Delete(anthropic_msg);
            }
        }
    }

    cJSON_AddItemToObject(anthropic_json, "messages", anthropic_messages);

    // Add system prompt if present
    if (system_prompt) {
        cJSON_AddItemToObject(anthropic_json, "system", system_prompt);
    }

    // Convert tools to Anthropic format
    if (tools && cJSON_IsArray(tools)) {
        cJSON *anthropic_tools = cJSON_CreateArray();

        cJSON *tool = NULL;
        cJSON_ArrayForEach(tool, tools) {
            cJSON *function = cJSON_GetObjectItem(tool, "function");
            if (function) {
                cJSON *anthropic_tool = cJSON_CreateObject();

                cJSON *name = cJSON_GetObjectItem(function, "name");
                cJSON *description = cJSON_GetObjectItem(function, "description");
                cJSON *parameters = cJSON_GetObjectItem(function, "parameters");

                if (name && cJSON_IsString(name)) {
                    cJSON_AddStringToObject(anthropic_tool, "name", name->valuestring);
                }
                if (description && cJSON_IsString(description)) {
                    cJSON_AddStringToObject(anthropic_tool, "description", description->valuestring);
                }
                if (parameters) {
                    cJSON_AddItemToObject(anthropic_tool, "input_schema", cJSON_Duplicate(parameters, 1));
                }

                cJSON_AddItemToArray(anthropic_tools, anthropic_tool);
            }
        }

        if (cJSON_GetArraySize(anthropic_tools) > 0) {
            cJSON_AddItemToObject(anthropic_json, "tools", anthropic_tools);
        } else {
            cJSON_Delete(anthropic_tools);
        }
    }

    // Add anthropic_version
    cJSON_AddStringToObject(anthropic_json, "anthropic_version", "bedrock-2023-05-31");

    char *result = cJSON_PrintUnformatted(anthropic_json);

    LOG_DEBUG("Anthropic request created, length: %zu bytes", result ? strlen(result) : 0);
    LOG_DEBUG("Messages in request: %d", cJSON_GetArraySize(anthropic_messages));
    LOG_DEBUG("=== BEDROCK REQUEST CONVERSION END ===");

    cJSON_Delete(openai_json);
    cJSON_Delete(anthropic_json);

    return result;
}

cJSON* bedrock_convert_response(const char *bedrock_response) {
    // AWS Bedrock returns Anthropic's native format
    // We need to convert it to OpenAI format

    LOG_DEBUG("=== BEDROCK RESPONSE CONVERSION START ===");
    LOG_DEBUG("Bedrock response length: %zu bytes", strlen(bedrock_response));
    LOG_DEBUG("First 200 chars of response: %.200s", bedrock_response);

    cJSON *anthropic_json = cJSON_Parse(bedrock_response);
    if (!anthropic_json) {
        LOG_ERROR("Failed to parse Bedrock response");
        return NULL;
    }

    // Build OpenAI format response
    cJSON *openai_json = cJSON_CreateObject();

    // Add id (generate if not present)
    cJSON *id = cJSON_GetObjectItem(anthropic_json, "id");
    if (id && cJSON_IsString(id)) {
        cJSON_AddStringToObject(openai_json, "id", id->valuestring);
    } else {
        cJSON_AddStringToObject(openai_json, "id", "bedrock-request");
    }

    // Add object type
    cJSON_AddStringToObject(openai_json, "object", "chat.completion");

    // Add created timestamp
    time_t now = time(NULL);
    cJSON_AddNumberToObject(openai_json, "created", (double)now);

    // Add model
    cJSON *model = cJSON_GetObjectItem(anthropic_json, "model");
    if (model && cJSON_IsString(model)) {
        cJSON_AddStringToObject(openai_json, "model", model->valuestring);
    } else {
        cJSON_AddStringToObject(openai_json, "model", "claude-bedrock");
    }

    // Add choices array
    cJSON *choices = cJSON_CreateArray();
    cJSON *choice = cJSON_CreateObject();
    cJSON_AddNumberToObject(choice, "index", 0);

    // Add message
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "assistant");

    // Convert content blocks
    cJSON *content = cJSON_GetObjectItem(anthropic_json, "content");
    cJSON *tool_calls = NULL;
    char *text_content = NULL;

    int content_block_count = content && cJSON_IsArray(content) ? cJSON_GetArraySize(content) : 0;
    LOG_DEBUG("Content blocks in response: %d", content_block_count);

    if (content && cJSON_IsArray(content)) {
        int block_index = 0;
        cJSON *block = NULL;
        cJSON_ArrayForEach(block, content) {
            cJSON *type = cJSON_GetObjectItem(block, "type");
            if (!type || !cJSON_IsString(type)) {
                LOG_WARN("Content block %d has no type", block_index);
                block_index++;
                continue;
            }

            const char *type_str = type->valuestring;
            LOG_DEBUG("Processing content block %d: type=%s", block_index, type_str);

            if (strcmp(type_str, "text") == 0) {
                cJSON *text = cJSON_GetObjectItem(block, "text");
                if (text && cJSON_IsString(text)) {
                    text_content = text->valuestring;
                    LOG_DEBUG("  Text content length: %zu chars", strlen(text_content));
                }
            } else if (strcmp(type_str, "tool_use") == 0) {
                LOG_DEBUG("  Found tool_use block!");
                if (!tool_calls) {
                    tool_calls = cJSON_CreateArray();
                }

                cJSON *tool_call = cJSON_CreateObject();

                cJSON *tool_id = cJSON_GetObjectItem(block, "id");
                if (tool_id && cJSON_IsString(tool_id)) {
                    cJSON_AddStringToObject(tool_call, "id", tool_id->valuestring);
                    LOG_DEBUG("    tool_use id: %s", tool_id->valuestring);
                }

                cJSON_AddStringToObject(tool_call, "type", "function");

                cJSON *function = cJSON_CreateObject();
                cJSON *name = cJSON_GetObjectItem(block, "name");
                if (name && cJSON_IsString(name)) {
                    cJSON_AddStringToObject(function, "name", name->valuestring);
                    LOG_DEBUG("    tool_use name: %s", name->valuestring);
                }

                cJSON *input = cJSON_GetObjectItem(block, "input");
                if (input) {
                    char *input_str = cJSON_PrintUnformatted(input);
                    cJSON_AddStringToObject(function, "arguments", input_str);
                    LOG_DEBUG("    tool_use arguments length: %zu chars", strlen(input_str));
                    free(input_str);
                }

                cJSON_AddItemToObject(tool_call, "function", function);
                cJSON_AddItemToArray(tool_calls, tool_call);
                LOG_DEBUG("    tool_call added to array");
            }
            block_index++;
        }
    }

    // Add content to message
    if (text_content) {
        cJSON_AddStringToObject(message, "content", text_content);
    } else {
        cJSON_AddNullToObject(message, "content");
    }

    // Add tool_calls if present
    if (tool_calls) {
        cJSON_AddItemToObject(message, "tool_calls", tool_calls);
    }

    cJSON_AddItemToObject(choice, "message", message);

    // Add finish_reason
    cJSON *stop_reason = cJSON_GetObjectItem(anthropic_json, "stop_reason");
    const char *final_finish_reason = "stop";  // default
    if (stop_reason && cJSON_IsString(stop_reason)) {
        const char *reason = stop_reason->valuestring;
        LOG_DEBUG("Anthropic stop_reason: %s", reason);
        if (strcmp(reason, "end_turn") == 0) {
            final_finish_reason = "stop";
        } else if (strcmp(reason, "tool_use") == 0) {
            final_finish_reason = "tool_calls";
        } else if (strcmp(reason, "max_tokens") == 0) {
            final_finish_reason = "length";
        } else {
            final_finish_reason = reason;
        }
    } else {
        LOG_WARN("No stop_reason in Anthropic response, defaulting to 'stop'");
    }

    cJSON_AddStringToObject(choice, "finish_reason", final_finish_reason);
    LOG_DEBUG("OpenAI finish_reason: %s", final_finish_reason);

    int tool_calls_count = tool_calls ? cJSON_GetArraySize(tool_calls) : 0;
    LOG_DEBUG("Tool calls in converted response: %d", tool_calls_count);

    cJSON_AddItemToArray(choices, choice);
    cJSON_AddItemToObject(openai_json, "choices", choices);

    // Add usage if present
    cJSON *usage_anthropic = cJSON_GetObjectItem(anthropic_json, "usage");
    if (usage_anthropic) {
        cJSON *usage = cJSON_CreateObject();

        cJSON *input_tokens = cJSON_GetObjectItem(usage_anthropic, "input_tokens");
        cJSON *output_tokens = cJSON_GetObjectItem(usage_anthropic, "output_tokens");

        if (input_tokens && cJSON_IsNumber(input_tokens)) {
            cJSON_AddNumberToObject(usage, "prompt_tokens", input_tokens->valueint);
        }
        if (output_tokens && cJSON_IsNumber(output_tokens)) {
            cJSON_AddNumberToObject(usage, "completion_tokens", output_tokens->valueint);
        }

        int total = 0;
        if (input_tokens && cJSON_IsNumber(input_tokens)) total += input_tokens->valueint;
        if (output_tokens && cJSON_IsNumber(output_tokens)) total += output_tokens->valueint;
        cJSON_AddNumberToObject(usage, "total_tokens", total);

        // Preserve cache-related fields for token usage tracking
        // Anthropic-style: cache_read_input_tokens
        cJSON *cache_read_input_tokens = cJSON_GetObjectItem(usage_anthropic, "cache_read_input_tokens");
        if (cache_read_input_tokens && cJSON_IsNumber(cache_read_input_tokens)) {
            cJSON_AddNumberToObject(usage, "cache_read_input_tokens", cache_read_input_tokens->valueint);
            LOG_DEBUG("Preserved cache_read_input_tokens: %d", cache_read_input_tokens->valueint);
        }

        // DeepSeek/Moonshot-style: cached_tokens
        cJSON *cached_tokens = cJSON_GetObjectItem(usage_anthropic, "cached_tokens");
        if (cached_tokens && cJSON_IsNumber(cached_tokens)) {
            cJSON_AddNumberToObject(usage, "cached_tokens", cached_tokens->valueint);
            LOG_DEBUG("Preserved cached_tokens: %d", cached_tokens->valueint);
        }

        // DeepSeek-style: prompt_cache_hit_tokens and prompt_cache_miss_tokens
        cJSON *prompt_cache_hit_tokens = cJSON_GetObjectItem(usage_anthropic, "prompt_cache_hit_tokens");
        cJSON *prompt_cache_miss_tokens = cJSON_GetObjectItem(usage_anthropic, "prompt_cache_miss_tokens");

        if (prompt_cache_hit_tokens && cJSON_IsNumber(prompt_cache_hit_tokens)) {
            cJSON_AddNumberToObject(usage, "prompt_cache_hit_tokens", prompt_cache_hit_tokens->valueint);
            LOG_DEBUG("Preserved prompt_cache_hit_tokens: %d", prompt_cache_hit_tokens->valueint);
        }

        if (prompt_cache_miss_tokens && cJSON_IsNumber(prompt_cache_miss_tokens)) {
            cJSON_AddNumberToObject(usage, "prompt_cache_miss_tokens", prompt_cache_miss_tokens->valueint);
            LOG_DEBUG("Preserved prompt_cache_miss_tokens: %d", prompt_cache_miss_tokens->valueint);
        }

        // DeepSeek-style: prompt_tokens_details with cached_tokens inside
        cJSON *prompt_tokens_details = cJSON_GetObjectItem(usage_anthropic, "prompt_tokens_details");
        if (prompt_tokens_details) {
            cJSON *prompt_tokens_details_copy = cJSON_Duplicate(prompt_tokens_details, 1);
            if (prompt_tokens_details_copy) {
                cJSON_AddItemToObject(usage, "prompt_tokens_details", prompt_tokens_details_copy);
                LOG_DEBUG("Preserved prompt_tokens_details");
            }
        }

        cJSON_AddItemToObject(openai_json, "usage", usage);
    }

    LOG_DEBUG("=== BEDROCK RESPONSE CONVERSION END ===");
    LOG_DEBUG("Converted response - finish_reason: %s, tool_calls: %d, has_text: %s",
              final_finish_reason, tool_calls_count, text_content ? "yes" : "no");

    cJSON_Delete(anthropic_json);
    return openai_json;
}

struct curl_slist* bedrock_sign_request(
    struct curl_slist *headers,
    const char *method,
    const char *url,
    const char *payload,
    const AWSCredentials *creds,
    const char *region,
    const char *service
) {
    if (!method || !url || !payload || !creds || !region || !service) {
        LOG_ERROR("Invalid parameters for bedrock_sign_request");
        return NULL;
    }

    LOG_DEBUG("=== AWS SIGV4 REQUEST SIGNING START ===");
    LOG_DEBUG("Signing request: %s %s (region=%s, service=%s, has_session_token=%s)",
              method, url, region, service, creds->session_token ? "yes" : "no");

    // Get timestamps
    char *timestamp = get_iso8601_timestamp();
    char *datestamp = get_date_stamp();

    if (!timestamp || !datestamp) {
        free(timestamp);
        free(datestamp);
        return NULL;
    }

    // Parse URL to extract host and path
    const char *host_start = strstr(url, "://");
    if (!host_start) {
        free(timestamp);
        free(datestamp);
        return NULL;
    }
    host_start += 3;  // Skip "://"

    const char *path_start = strchr(host_start, '/');
    char *host = NULL;
    char *path = NULL;

    if (path_start) {
        size_t host_len = (size_t)(path_start - host_start);
        host = malloc(host_len + 1);
        if (host) {
            memcpy(host, host_start, host_len);
            host[host_len] = '\0';
        }
        path = strdup(path_start);
    } else {
        host = strdup(host_start);
        path = strdup("/");
    }

    if (!host || !path) {
        free(timestamp);
        free(datestamp);
        free(host);
        free(path);
        return NULL;
    }

    // Create canonical request
    char *payload_hash = sha256_hash(payload);

    // URL-encode the path for canonical request (per AWS SigV4 spec)
    char *encoded_path = url_encode(path, 0);  // 0 = don't encode slashes
    if (!encoded_path) {
        free(timestamp);
        free(datestamp);
        free(host);
        free(path);
        free(payload_hash);
        return NULL;
    }

    // Canonical headers (must be sorted)
    char canonical_headers[2048];
    snprintf(canonical_headers, sizeof(canonical_headers),
             "host:%s\nx-amz-date:%s\n",
             host, timestamp);

    const char *signed_headers = "host;x-amz-date";

    // Build canonical request
    char canonical_request[8192];
    snprintf(canonical_request, sizeof(canonical_request),
             "%s\n%s\n\n%s\n%s\n%s",
             method, encoded_path, canonical_headers, signed_headers, payload_hash);

    char *canonical_request_hash = sha256_hash(canonical_request);

    // Create string to sign
    char string_to_sign[1024];
    snprintf(string_to_sign, sizeof(string_to_sign),
             "AWS4-HMAC-SHA256\n%s\n%s/%s/%s/aws4_request\n%s",
             timestamp, datestamp, region, service, canonical_request_hash);

    // Calculate signing key
    char key_buffer[256];
    snprintf(key_buffer, sizeof(key_buffer), "AWS4%s", creds->secret_access_key);

    unsigned char k_date[32];
    hmac_sha256((const unsigned char*)key_buffer, strlen(key_buffer),
                (const unsigned char*)datestamp, strlen(datestamp), k_date);

    unsigned char k_region[32];
    hmac_sha256(k_date, 32, (const unsigned char*)region, strlen(region), k_region);

    unsigned char k_service[32];
    hmac_sha256(k_region, 32, (const unsigned char*)service, strlen(service), k_service);

    unsigned char signing_key[32];
    hmac_sha256(k_service, 32, (const unsigned char*)"aws4_request", 12, signing_key);

    // Calculate signature
    unsigned char signature_bytes[32];
    hmac_sha256(signing_key, 32,
                (unsigned char*)string_to_sign, strlen(string_to_sign),
                signature_bytes);

    char *signature = hex_encode(signature_bytes, 32);

    // Build authorization header
    char auth_header[2048];
    snprintf(auth_header, sizeof(auth_header),
             "Authorization: AWS4-HMAC-SHA256 Credential=%s/%s/%s/%s/aws4_request, SignedHeaders=%s, Signature=%s",
             creds->access_key_id, datestamp, region, service,
             signed_headers, signature);

    // Add headers
    char date_header[128];
    snprintf(date_header, sizeof(date_header), "x-amz-date: %s", timestamp);

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, date_header);
    headers = curl_slist_append(headers, auth_header);

    // Add session token if present
    if (creds->session_token) {
        char token_header[512];
        snprintf(token_header, sizeof(token_header), "x-amz-security-token: %s", creds->session_token);
        headers = curl_slist_append(headers, token_header);
    }

    LOG_DEBUG("Request signed successfully with AWS SigV4");

    // Cleanup
    free(timestamp);
    free(datestamp);
    free(host);
    free(path);
    free(encoded_path);
    free(payload_hash);
    free(canonical_request_hash);
    free(signature);

    return headers;
}
