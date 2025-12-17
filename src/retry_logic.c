/*
 * retry_logic.c - Common retry logic for API calls
 *
 * Centralizes retryable error determination and context length error handling
 * to avoid duplication across provider implementations.
 */

#include "retry_logic.h"
#include <string.h>
#include <curl/curl.h>

/**
 * Determine if an HTTP error is retryable based on status code
 *
 * Retryable HTTP errors:
 * - 429: Rate limit (Too Many Requests)
 * - 408: Request timeout
 * - 5xx: Server errors
 *
 * @param http_status HTTP status code
 * @return 1 if retryable, 0 otherwise
 */
int is_http_error_retryable(long http_status) {
    return (http_status == 429 || http_status == 408 || http_status >= 500);
}


/**
 * Determine if a curl error is retryable
 *
 * Retryable curl errors are typically transient network issues:
 * - Connection issues (COULDNT_CONNECT, RECV_ERROR, SEND_ERROR, GOT_NOTHING)
 * - Timeouts (OPERATION_TIMEDOUT)
 * - SSL issues (SSL_CONNECT_ERROR)
 * - HTTP2/HTTP3 protocol layer issues (HTTP2, HTTP2_STREAM)
 *
 * @param curl_code curl error code
 * @return 1 if retryable, 0 otherwise
 */
int is_curl_error_retryable(CURLcode curl_code) {
    return (curl_code == CURLE_COULDNT_CONNECT ||
            curl_code == CURLE_OPERATION_TIMEDOUT ||
            curl_code == CURLE_RECV_ERROR ||
            curl_code == CURLE_SEND_ERROR ||
            curl_code == CURLE_SSL_CONNECT_ERROR ||
            curl_code == CURLE_GOT_NOTHING ||
            curl_code == CURLE_HTTP2 ||
            curl_code == CURLE_HTTP2_STREAM);
}

/**
 * Check if an error message indicates a context length error
 *
 * Detects various forms of context length/token limit errors from API providers.
 *
 * @param error_message Error message text (may be NULL)
 * @param error_type Error type from API response (may be NULL)
 * @return 1 if context length error, 0 otherwise
 */
int is_context_length_error(const char *error_message, const char *error_type) {
    if (!error_message) {
        return 0;
    }

    // Check for context length/token limit patterns in error message
    if (strstr(error_message, "maximum context length") != NULL ||
        (strstr(error_message, "context length") != NULL && strstr(error_message, "tokens") != NULL) ||
        strstr(error_message, "too many tokens") != NULL) {
        return 1;
    }

    // Check for invalid_request_error with token-related message
    if (error_type && strcmp(error_type, "invalid_request_error") == 0 &&
        strstr(error_message, "tokens") != NULL) {
        return 1;
    }

    return 0;
}

/**
 * Get the standard context length error message
 *
 * Returns a user-friendly explanation of context length errors with suggestions.
 * Caller must free the returned string.
 *
 * @return Newly allocated string with context length error message
 */
char *get_context_length_error_message(void) {
    return strdup(
        "Context length exceeded. The conversation has grown too large for the model's memory. "
        "Try starting a new conversation or reduce the amount of code/files being discussed."
    );
}
