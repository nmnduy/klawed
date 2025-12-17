/*
 * retry_logic.h - Common retry logic for API calls
 *
 * Centralizes retryable error determination and context length error handling
 * to avoid duplication across provider implementations.
 */

#ifndef RETRY_LOGIC_H
#define RETRY_LOGIC_H

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
int is_http_error_retryable(long http_status);

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
int is_curl_error_retryable(CURLcode curl_code);

/**
 * Check if an error message indicates a context length error
 *
 * Detects various forms of context length/token limit errors from API providers.
 *
 * @param error_message Error message text (may be NULL)
 * @param error_type Error type from API response (may be NULL)
 * @return 1 if context length error, 0 otherwise
 */
int is_context_length_error(const char *error_message, const char *error_type);

/**
 * Get the standard context length error message
 *
 * Returns a user-friendly explanation of context length errors with suggestions.
 * Caller must free the returned string.
 *
 * @return Newly allocated string with context length error message
 */
char *get_context_length_error_message(void);

#endif // RETRY_LOGIC_H
