/*
 * aws_bedrock.h - AWS Bedrock provider support
 *
 * Enables calling Claude models via AWS Bedrock with automatic authentication
 * and SigV4 request signing.
 */

#ifndef AWS_BEDROCK_H
#define AWS_BEDROCK_H

#include <curl/curl.h>
#include <cjson/cJSON.h>

// ============================================================================
// Configuration
// ============================================================================

// AWS Bedrock endpoint format
#define AWS_BEDROCK_ENDPOINT_FMT "https://bedrock-runtime.%s.amazonaws.com"
#define AWS_BEDROCK_SERVICE "bedrock"
#define AWS_BEDROCK_PATH "/model/%s/invoke"
#define AWS_BEDROCK_STREAM_PATH "/model/%s/invoke-with-response-stream"

// Environment variables
#define ENV_USE_BEDROCK "CLAUDE_CODE_USE_BEDROCK"
#define ENV_AWS_REGION "AWS_REGION"
#define ENV_AWS_PROFILE "AWS_PROFILE"
#define ENV_AWS_ACCESS_KEY_ID "AWS_ACCESS_KEY_ID"
#define ENV_AWS_SECRET_ACCESS_KEY "AWS_SECRET_ACCESS_KEY"
#define ENV_AWS_SESSION_TOKEN "AWS_SESSION_TOKEN"
#define ENV_AWS_AUTH_COMMAND "AWS_AUTH_COMMAND"  // Custom authentication command

// ============================================================================
// Structs
// ============================================================================

/**
 * AWS credentials for API authentication
 */
typedef struct {
    char *access_key_id;
    char *secret_access_key;
    char *session_token;  // Optional, for temporary credentials
    char *region;
    char *profile;
} AWSCredentials;

/**
 * AWS Bedrock configuration
 */
typedef struct BedrockConfigStruct {
    int enabled;           // Whether Bedrock mode is enabled
    char *region;          // AWS region (e.g., "us-west-2")
    char *model_id;        // Full Bedrock model ID
    char *endpoint;        // Computed endpoint URL
    AWSCredentials *creds; // AWS credentials
} BedrockConfig;

// ============================================================================
// Function Declarations
// ============================================================================

/**
 * Check if Bedrock mode is enabled via environment variable
 * Returns: 1 if enabled, 0 otherwise
 */
int bedrock_is_enabled(void);

/**
 * Initialize Bedrock configuration from environment variables
 * Returns: Configured BedrockConfig pointer, or NULL on error
 * Caller must free with bedrock_config_free()
 */
BedrockConfig* bedrock_config_init(const char *model_id);

/**
 * Free Bedrock configuration and all associated memory
 */
void bedrock_config_free(BedrockConfig *config);

/**
 * Load AWS credentials from environment or AWS config files
 * Order: env vars -> AWS config file -> AWS SSO
 * Returns: AWSCredentials pointer or NULL on error
 * Caller must free with bedrock_creds_free()
 */
AWSCredentials* bedrock_load_credentials(const char *profile, const char *region);

/**
 * Free AWS credentials
 */
void bedrock_creds_free(AWSCredentials *creds);

// Testing hooks: override command executors
void aws_bedrock_set_exec_command_fn(char* (*fn)(const char *cmd));
void aws_bedrock_set_system_fn(int (*fn)(const char *cmd));

/**
 * Check if AWS credentials are valid (not expired)
 * If invalid and SSO profile is configured, attempts to refresh
 * Returns: 1 if valid, 0 if invalid/expired, -1 on error
 */
int bedrock_validate_credentials(AWSCredentials *creds, const char *profile);

/**
 * Execute AWS authentication command (e.g., aws sso login)
 * Returns: 0 on success, -1 on error
 */
int bedrock_authenticate(const char *profile);

/**
 * Handle AWS authentication errors and attempt recovery
 * Detects auth-related errors, attempts re-authentication, and reloads credentials
 *
 * Parameters:
 *   config - BedrockConfig with current credentials (will be updated if successful)
 *   http_status - HTTP status code from failed API request
 *   error_message - Error message from API response
 *   response_body - Full response body from API (for detailed error analysis)
 *
 * Returns: 1 if credentials were refreshed (caller should retry request), 0 otherwise
 */
int bedrock_handle_auth_error(BedrockConfig *config, long http_status, const char *error_message, const char *response_body);

/**
 * Build AWS Bedrock API endpoint URL
 * Returns: Newly allocated string (caller must free), or NULL on error
 */
char* bedrock_build_endpoint(const char *region, const char *model_id);

/**
 * Build AWS Bedrock streaming API endpoint URL
 * Returns: Newly allocated string (caller must free), or NULL on error
 */
char* bedrock_build_streaming_endpoint(const char *region, const char *model_id);

/**
 * Convert OpenAI format request to AWS Bedrock format
 * Bedrock wraps the request in a specific structure
 * Returns: Newly allocated JSON string (caller must free), or NULL on error
 */
char* bedrock_convert_request(const char *openai_request);

/**
 * Convert AWS Bedrock response to OpenAI format
 * Returns: cJSON object (caller must delete), or NULL on error
 */
cJSON* bedrock_convert_response(const char *bedrock_response);

/**
 * Sign an AWS request using SigV4 algorithm
 * Adds Authorization header to the curl header list
 *
 * Parameters:
 *   headers - CURL header list (will be modified)
 *   method - HTTP method (e.g., "POST")
 *   url - Full request URL
 *   payload - Request body (JSON string)
 *   creds - AWS credentials
 *   region - AWS region
 *   service - AWS service name (e.g., "bedrock")
 *
 * Returns: Updated header list, or NULL on error
 */
struct curl_slist* bedrock_sign_request(
    struct curl_slist *headers,
    const char *method,
    const char *url,
    const char *payload,
    const AWSCredentials *creds,
    const char *region,
    const char *service
);

#endif // AWS_BEDROCK_H
