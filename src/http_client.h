/*
 * http_client.h - Unified HTTP client abstraction for API providers
 *
 * This provides a common interface for making HTTP requests, abstracting
 * away the libcurl details that are currently duplicated across providers.
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <curl/curl.h>
#include <cjson/cJSON.h>

// ============================================================================
// Types
// ============================================================================

/**
 * HTTP request configuration
 */
typedef struct {
    const char *url;           // Target URL
    const char *method;        // HTTP method (default: "POST")
    const char *body;          // Request body (NULL for no body)
    struct curl_slist *headers; // Request headers (caller owns, will be copied)
    long connect_timeout_ms;   // Connection timeout in milliseconds (default: 30000)
    long total_timeout_ms;     // Total timeout in milliseconds (default: 300000)
    int follow_redirects;      // Whether to follow redirects (default: 0)
    int verbose;               // Enable curl verbose logging (default: 0)
    int enable_streaming;      // Enable Server-Sent Events (SSE) streaming (default: 0)
} HttpRequest;

/**
 * HTTP response
 */
typedef struct {
    char *body;                // Response body (owned by struct, must be freed)
    long status_code;          // HTTP status code (0 if network error)
    struct curl_slist *headers; // Response headers (owned by struct, must be freed)
    long duration_ms;          // Request duration in milliseconds
    char *error_message;       // Error message if request failed (owned by struct, must be freed)
    int is_retryable;          // Whether the error is retryable (for network errors)
} HttpResponse;

/**
 * Callback for tracking request progress (can be used for interrupt handling)
 * Return non-zero to abort the request.
 */
typedef int (*HttpProgressCallback)(void *userdata,
                                   curl_off_t dltotal, curl_off_t dlnow,
                                   curl_off_t ultotal, curl_off_t ulnow);

/**
 * Streaming event types for Server-Sent Events (SSE)
 * Supports both Anthropic and OpenAI streaming formats
 */
typedef enum {
    // Anthropic Messages API events
    SSE_EVENT_MESSAGE_START,    // message_start event
    SSE_EVENT_CONTENT_BLOCK_START, // content_block_start event
    SSE_EVENT_CONTENT_BLOCK_DELTA, // content_block_delta event (text streaming)
    SSE_EVENT_CONTENT_BLOCK_STOP,  // content_block_stop event
    SSE_EVENT_MESSAGE_DELTA,    // message_delta event (stop_reason, etc.)
    SSE_EVENT_MESSAGE_STOP,     // message_stop event
    SSE_EVENT_ERROR,            // error event
    SSE_EVENT_PING,             // ping event (keepalive)

    // OpenAI Chat Completions API events (data events without explicit event type)
    SSE_EVENT_OPENAI_CHUNK,     // OpenAI chunk (default "data:" event)
    SSE_EVENT_OPENAI_DONE       // OpenAI [DONE] marker
} StreamEventType;

/**
 * Streaming event data
 */
typedef struct {
    StreamEventType type;
    const char *event_name; // Raw event name from SSE (e.g., "content_block_delta") - not owned
    cJSON *data;            // Parsed JSON data from the event (owned by callback, freed after)
    const char *raw_data;   // Raw data string (not owned, points to parser buffer)
} StreamEvent;

/**
 * Callback for handling streaming events
 * Called for each Server-Sent Event received
 * Return non-zero to abort the stream.
 */
typedef int (*HttpStreamCallback)(StreamEvent *event, void *userdata);

// ============================================================================
// Core Functions
// ============================================================================

/**
 * Initialize HTTP client (call once at program startup)
 * Returns: 0 on success, non-zero on error
 */
int http_client_init(void);

/**
 * Cleanup HTTP client (call once at program shutdown)
 */
void http_client_cleanup(void);

/**
 * Execute an HTTP request
 *
 * @param req - Request configuration (will be copied internally)
 * @param progress_cb - Optional progress callback (can be NULL)
 * @param progress_data - User data passed to progress callback
 * @return HttpResponse* - Response object (caller must free with http_response_free)
 *                        Returns NULL on memory allocation failure
 */
HttpResponse* http_client_execute(const HttpRequest *req,
                                 HttpProgressCallback progress_cb,
                                 void *progress_data);

/**
 * Execute an HTTP request with streaming support (Server-Sent Events)
 *
 * @param req - Request configuration (must have enable_streaming=1)
 * @param stream_cb - Callback for each streaming event (required)
 * @param stream_data - User data passed to stream callback
 * @param progress_cb - Optional progress callback (can be NULL)
 * @param progress_data - User data passed to progress callback
 * @return HttpResponse* - Response object with final state (caller must free)
 *                        Returns NULL on memory allocation failure
 *                        Note: body may be empty for streaming requests
 */
HttpResponse* http_client_execute_stream(const HttpRequest *req,
                                        HttpStreamCallback stream_cb,
                                        void *stream_data,
                                        HttpProgressCallback progress_cb,
                                        void *progress_data);

/**
 * Free an HTTP response
 */
void http_response_free(HttpResponse *resp);

/**
 * Convert curl_slist headers to JSON string for logging
 * Returns: JSON string (caller must free) or NULL on error
 */
char* http_headers_to_json(struct curl_slist *headers);

/**
 * Create a deep copy of curl_slist headers
 * Returns: New headers list (caller must free with curl_slist_free_all) or NULL
 */
struct curl_slist* http_copy_headers(struct curl_slist *headers);

/**
 * Add a header to a curl_slist
 * Returns: Updated headers list (may be new if input was NULL)
 */
struct curl_slist* http_add_header(struct curl_slist *headers, const char *header);

#endif // HTTP_CLIENT_H
