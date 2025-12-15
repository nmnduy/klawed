/*
 * provider.h - API Provider abstraction layer
 *
 * Defines a common interface for different API providers (OpenAI, AWS Bedrock, etc.)
 * This abstraction separates provider-specific authentication, request formatting,
 * and error handling from the core conversation logic.
 */

#ifndef PROVIDER_H
#define PROVIDER_H

#include <curl/curl.h>
#include <cjson/cJSON.h>
#include "claude_internal.h"  // For ApiResponse typedef

// Forward declarations
struct Provider;
struct ConversationState;

/**
 * Result from a single API call attempt
 * Used by provider->call_api() to communicate success/error state to retry logic
 */
typedef struct {
    ApiResponse *response;   // Parsed vendor-agnostic response (NULL on error, caller must free)
    char *raw_response;      // Raw response body (for logging, caller must free)
    char *request_json;      // Raw request JSON (for logging, caller must free)
    char *headers_json;      // JSON representation of request headers (for logging, caller must free)
    long http_status;        // HTTP status code (0 if network error before response)
    char *error_message;     // Error message if call failed (caller must free)
    long duration_ms;        // Request duration in milliseconds
    int is_retryable;        // 1 if error can be retried, 0 otherwise
    int auth_refreshed;      // 1 if provider refreshed credentials (AWS only)
} ApiCallResult;

/**
 * Provider interface - abstraction for API providers
 *
 * Each provider implements call_api() to handle a single authenticated request.
 * The generic retry logic wraps this to handle transient failures.
 */
typedef struct Provider {
    // Provider metadata
    const char *name;           // "OpenAI", "Bedrock", etc.
    void *config;               // Provider-specific configuration (opaque pointer)

    /**
     * Execute a single API call attempt (no retries)
     */
    ApiCallResult (*call_api)(struct Provider *self, struct ConversationState *state);

    /**
     * Cleanup provider resources
     */
    void (*cleanup)(struct Provider *self);

} Provider;

/**
 * Provider initialization result
 */
typedef struct {
    Provider *provider;   // Initialized provider (NULL on error)
    char *api_url;        // Base API URL for this provider
    char *error_message;  // Error message if initialization failed (caller must free)
} ProviderInitResult;

/**
 * Initialize the appropriate provider based on environment configuration
 *
 * Checks environment variables to determine which provider to use:
 * - CLAUDE_CODE_USE_BEDROCK=1 -> AWS Bedrock
 * - Otherwise -> OpenAI-compatible API
 *
 * @param model - Model name (e.g., "claude-sonnet-4-20250514")
 * @param api_key - API key (for OpenAI provider, may be NULL for Bedrock)
 * @param[out] result - Pointer to ProviderInitResult to populate
 *         On success: result->provider and result->api_url are set (caller owns both)
 *         On failure: result->error_message is set (caller must free)
 */
void provider_init(const char *model, const char *api_key, ProviderInitResult *result);

#endif // PROVIDER_H
