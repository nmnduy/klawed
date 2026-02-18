/*
 * retry_logic.c - Common retry logic for API calls
 *
 * Centralizes retryable error determination and context length error handling
 * to avoid duplication across provider implementations.
 */

#include "retry_logic.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
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
 * Performs case-insensitive matching to handle variations in error messages.
 *
 * @param error_message Error message text (may be NULL)
 * @param error_type Error type from API response (may be NULL)
 * @return 1 if context length error, 0 otherwise
 */
int is_context_length_error(const char *error_message, const char *error_type) {
    if (!error_message) {
        return 0;
    }

    // Create lowercase copy of error message for case-insensitive matching
    size_t msg_len = strlen(error_message);
    char *msg_lower = malloc(msg_len + 1);
    if (msg_lower) {
        for (size_t i = 0; i < msg_len; i++) {
            msg_lower[i] = (char)tolower((unsigned char)error_message[i]);
        }
        msg_lower[msg_len] = '\0';
    }

    // Use lowercased message if available, otherwise fall back to original
    const char *msg_to_check = msg_lower ? msg_lower : error_message;
    int is_context_error = 0;

    // Check for context length/token limit patterns in error message (lowercased)
    if (strstr(msg_to_check, "maximum context length") != NULL ||
        (strstr(msg_to_check, "context length") != NULL && strstr(msg_to_check, "tokens") != NULL) ||
        strstr(msg_to_check, "too many tokens") != NULL ||
        // OpenAI token limit exceeded pattern
        strstr(msg_to_check, "exceeded model token limit") != NULL ||
        strstr(msg_to_check, "token limit") != NULL ||
        // LiteLLM / Bedrock patterns
        strstr(msg_to_check, "contextwindowexceedederror") != NULL ||
        strstr(msg_to_check, "context window error") != NULL ||
        strstr(msg_to_check, "input is too long") != NULL) {
        is_context_error = 1;
    }

    // Check for invalid_request_error with token-related message
    if (!is_context_error && error_type &&
        strcasecmp(error_type, "invalid_request_error") == 0 &&
        strstr(msg_to_check, "tokens") != NULL) {
        is_context_error = 1;
    }

    // msg_lower is either allocated buffer or NULL (never error_message)
    // free(NULL) is safe (no-op), but check explicitly for clarity
    if (msg_lower) {
        free(msg_lower);
    }
    return is_context_error;
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
