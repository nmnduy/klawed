/**
 * sse_parser.h - Server-Sent Events (SSE) parser
 *
 * Parses SSE streams for AI API providers (OpenAI, Anthropic, etc.)
 * Supports both explicit event types (Anthropic) and implicit (OpenAI)
 */

#ifndef SSE_PARSER_H
#define SSE_PARSER_H

#include <stddef.h>
#include <stdbool.h>
#include <cjson/cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * SSE Event Types (must match http_client.h)
 * ============================================================================ */

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
    SSE_EVENT_OPENAI_DONE,      // OpenAI [DONE] marker

    SSE_EVENT_UNKNOWN           // Unknown event type
} StreamEventType;

/* ============================================================================
 * SSE Event Structure (must match http_client.h)
 * ============================================================================ */

typedef struct {
    StreamEventType type;
    const char *event_name;     // Original event name from stream (or "message" for OpenAI)
    cJSON *data;                // Parsed JSON data (owned by caller if retained)
    const char *raw_data;       // Raw data string (not owned by event)
} StreamEvent;

/* ============================================================================
 * SSE Callback Type (must match http_client.h)
 * ============================================================================ */

typedef int (*HttpStreamCallback)(StreamEvent *event, void *userdata);

/* ============================================================================
 * SSE Parser State
 * ============================================================================ */

typedef struct MemoryBuffer {
    char *data;
    size_t size;
    size_t capacity;
} MemoryBuffer;

typedef struct SSEParserState {
    char *event_type;           // Current event type being built
    MemoryBuffer *data_buffer;  // Current data being accumulated
    MemoryBuffer *line_buffer;  // Raw streaming bytes buffered across callbacks
    HttpStreamCallback callback;
    void *callback_data;
    bool abort_requested;
} SSEParserState;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Create a new SSE parser state
 */
SSEParserState* sse_parser_create(HttpStreamCallback callback, void *callback_data);

/**
 * Free an SSE parser state
 */
void sse_parser_free(SSEParserState *parser);

/**
 * Reset the current event being parsed (called when event is complete)
 */
void sse_parser_reset_event(SSEParserState *parser);

/**
 * Map event name to enum type
 */
StreamEventType sse_event_name_to_type(const char *name, const char *data);

/**
 * Map event type enum to string name
 */
const char* sse_event_type_to_name(StreamEventType event_type);

/**
 * Process a single SSE line
 * Returns 0 on success, non-zero to abort parsing
 */
int sse_parser_process_line(SSEParserState *parser, const char *line, size_t len);

/**
 * Process raw SSE data (may contain multiple lines)
 * Returns 0 on success, non-zero to abort parsing
 */
int sse_parser_process_data(SSEParserState *parser, const char *data, size_t len);

/**
 * Create a memory buffer
 */
MemoryBuffer* memory_buffer_create(void);

/**
 * Free a memory buffer
 */
void memory_buffer_free(MemoryBuffer *buf);

/**
 * Append data to a memory buffer
 * Returns 0 on success, -1 on failure
 */
int memory_buffer_append(MemoryBuffer *buf, const char *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // SSE_PARSER_H
