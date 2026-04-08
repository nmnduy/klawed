/**
 * sse_parser.c - Server-Sent Events (SSE) parser implementation
 */

#include "sse_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* For cJSON_Parse - need to include cJSON header */
#include <cjson/cJSON.h>

/* Simple logging for errors - can be replaced with proper logger if needed */
#define LOG_WARN(...) fprintf(stderr, __VA_ARGS__)

/* ============================================================================
 * Memory Buffer Implementation
 * ============================================================================ */

MemoryBuffer* memory_buffer_create(void) {
    MemoryBuffer *buf = calloc(1, sizeof(MemoryBuffer));
    if (!buf) {
        return NULL;
    }

    /* Initial capacity */
    buf->capacity = 4096;
    buf->data = malloc(buf->capacity);
    if (!buf->data) {
        free(buf);
        return NULL;
    }

    buf->data[0] = '\0';
    return buf;
}

void memory_buffer_free(MemoryBuffer *buf) {
    if (buf) {
        free(buf->data);
        free(buf);
    }
}

int memory_buffer_append(MemoryBuffer *buf, const char *data, size_t len) {
    if (!buf || !data) return -1;

    size_t needed = buf->size + len + 1;
    if (needed > buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        if (new_capacity < needed) new_capacity = needed;
        char *new_data = realloc(buf->data, new_capacity);
        if (!new_data) return -1;
        buf->data = new_data;
        buf->capacity = new_capacity;
    }

    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
    buf->data[buf->size] = '\0';
    return 0;
}

/* ============================================================================
 * SSE Parser Implementation
 * ============================================================================ */

SSEParserState* sse_parser_create(HttpStreamCallback callback, void *callback_data) {
    SSEParserState *parser = calloc(1, sizeof(SSEParserState));
    if (!parser) return NULL;

    parser->data_buffer = memory_buffer_create();
    if (!parser->data_buffer) {
        free(parser);
        return NULL;
    }

    parser->line_buffer = memory_buffer_create();
    if (!parser->line_buffer) {
        memory_buffer_free(parser->data_buffer);
        free(parser);
        return NULL;
    }

    parser->callback = callback;
    parser->callback_data = callback_data;
    parser->abort_requested = false;
    return parser;
}

void sse_parser_free(SSEParserState *parser) {
    if (!parser) return;
    free(parser->event_type);
    memory_buffer_free(parser->data_buffer);
    memory_buffer_free(parser->line_buffer);
    free(parser);
}

void sse_parser_reset_event(SSEParserState *parser) {
    if (!parser) return;
    free(parser->event_type);
    parser->event_type = NULL;
    parser->data_buffer->size = 0;
    if (parser->data_buffer->data) {
        parser->data_buffer->data[0] = '\0';
    }
}

/* Map event name to enum */
StreamEventType sse_event_name_to_type(const char *name, const char *data) {
    /* Anthropic events have explicit event types */
    if (name) {
        if (strcmp(name, "message_start") == 0) return SSE_EVENT_MESSAGE_START;
        if (strcmp(name, "content_block_start") == 0) return SSE_EVENT_CONTENT_BLOCK_START;
        if (strcmp(name, "content_block_delta") == 0) return SSE_EVENT_CONTENT_BLOCK_DELTA;
        if (strcmp(name, "content_block_stop") == 0) return SSE_EVENT_CONTENT_BLOCK_STOP;
        if (strcmp(name, "message_delta") == 0) return SSE_EVENT_MESSAGE_DELTA;
        if (strcmp(name, "message_stop") == 0) return SSE_EVENT_MESSAGE_STOP;
        if (strcmp(name, "error") == 0) return SSE_EVENT_ERROR;
        if (strcmp(name, "ping") == 0) return SSE_EVENT_PING;
    }

    /* OpenAI uses implicit events (no event: field, just data:) */
    /* Check if data is "[DONE]" marker */
    if (data && strcmp(data, "[DONE]") == 0) {
        return SSE_EVENT_OPENAI_DONE;
    }

    /* If no event name specified and data exists, assume OpenAI chunk */
    if (!name && data && data[0] != '\0') {
        return SSE_EVENT_OPENAI_CHUNK;
    }

    return SSE_EVENT_PING;  /* Unknown/empty events treated as ping */
}

/* Map event type enum to string name */
const char* sse_event_type_to_name(StreamEventType event_type) {
    switch (event_type) {
        case SSE_EVENT_UNKNOWN:
            return "unknown";
        case SSE_EVENT_MESSAGE_START:
            return "message_start";
        case SSE_EVENT_CONTENT_BLOCK_START:
            return "content_block_start";
        case SSE_EVENT_CONTENT_BLOCK_DELTA:
            return "content_block_delta";
        case SSE_EVENT_CONTENT_BLOCK_STOP:
            return "content_block_stop";
        case SSE_EVENT_MESSAGE_DELTA:
            return "message_delta";
        case SSE_EVENT_MESSAGE_STOP:
            return "message_stop";
        case SSE_EVENT_ERROR:
            return "error";
        case SSE_EVENT_PING:
            return "ping";
        case SSE_EVENT_OPENAI_CHUNK:
            return "openai_chunk";
        case SSE_EVENT_OPENAI_DONE:
            return "openai_done";
        default:
            return "unknown";
    }
}

/* Dispatch event to callback */
static int sse_parser_dispatch_event(SSEParserState *parser) {
    if (!parser || !parser->callback) return 0;
    if (parser->abort_requested) return -1;  /* Don't call callback if abort requested */

    /* Build StreamEvent */
    StreamEvent event = {0};
    event.type = sse_event_name_to_type(parser->event_type, parser->data_buffer->data);
    event.event_name = parser->event_type ? parser->event_type : "message";  /* Default to "message" */
    event.raw_data = parser->data_buffer->data;

    /* Try to parse data as JSON (skip for OpenAI [DONE] marker) */
    if (parser->data_buffer->data && parser->data_buffer->size > 0 &&
        strcmp(parser->data_buffer->data, "[DONE]") != 0) {
        event.data = cJSON_Parse(parser->data_buffer->data);
        if (!event.data) {
            LOG_WARN("Failed to parse SSE data as JSON: %s", parser->data_buffer->data);
        }
    }

    /* Call user callback */
    int result = parser->callback(&event, parser->callback_data);

    /* Cleanup */
    if (event.data) {
        cJSON_Delete(event.data);
    }

    return result;
}

/* Process a single SSE line */
int sse_parser_process_line(SSEParserState *parser, const char *line, size_t len) {
    if (!parser) return -1;

    /* Empty line = end of event */
    if (len == 0 || (len == 1 && line[0] == '\r')) {
        if (parser->data_buffer->size > 0 || parser->event_type) {
            int result = sse_parser_dispatch_event(parser);
            sse_parser_reset_event(parser);
            if (result != 0) {
                parser->abort_requested = 1;
                return result;
            }
        }
        return 0;
    }

    /* Skip comments */
    if (line[0] == ':') return 0;

    /* Parse field */
    const char *colon = memchr(line, ':', len);
    if (!colon) {
        /* Line without colon, treat as data */
        MemoryBuffer *buf = parser->data_buffer;
        size_t needed = buf->size + len + 1;
        if (needed > buf->capacity) {
            size_t new_capacity = buf->capacity * 2;
            if (new_capacity < needed) new_capacity = needed;
            char *new_data = realloc(buf->data, new_capacity);
            if (!new_data) return 0;
            buf->data = new_data;
            buf->capacity = new_capacity;
        }
        memcpy(buf->data + buf->size, line, len);
        buf->size += len;
        buf->data[buf->size] = '\0';
        return 0;
    }

    size_t field_len = (size_t)(colon - line);
    const char *value = colon + 1;
    size_t value_len = len - field_len - 1;

    /* Skip leading space in value */
    if (value_len > 0 && value[0] == ' ') {
        value++;
        value_len--;
    }

    /* Handle fields */
    if (field_len == 5 && memcmp(line, "event", 5) == 0) {
        free(parser->event_type);
        parser->event_type = strndup(value, value_len);
    } else if (field_len == 4 && memcmp(line, "data", 4) == 0) {
        MemoryBuffer *buf = parser->data_buffer;
        size_t needed = buf->size + value_len + 1;
        if (needed > buf->capacity) {
            size_t new_capacity = buf->capacity * 2;
            if (new_capacity < needed) new_capacity = needed;
            char *new_data = realloc(buf->data, new_capacity);
            if (!new_data) return 0;
            buf->data = new_data;
            buf->capacity = new_capacity;
        }
        memcpy(buf->data + buf->size, value, value_len);
        buf->size += value_len;
        buf->data[buf->size] = '\0';
    }
    /* Ignore other fields (id, retry, etc.) */

    return 0;
}

/* Process raw SSE data (may contain multiple lines) */
int sse_parser_process_data(SSEParserState *parser, const char *data, size_t len) {
    if (!parser || !data) return -1;

    if (!parser->line_buffer) {
        return -1;
    }

    if (memory_buffer_append(parser->line_buffer, data, len) != 0) {
        return -1;
    }

    size_t pos = 0;
    while (pos < parser->line_buffer->size) {
        const char *base = parser->line_buffer->data;
        const char *eol = memchr(base + pos, '\n', parser->line_buffer->size - pos);
        if (!eol) {
            break;
        }

        size_t line_len = (size_t)(eol - (base + pos));
        if (line_len > 0 && base[pos + line_len - 1] == '\r') {
            line_len--;
        }

        int result = sse_parser_process_line(parser, base + pos, line_len);
        if (result != 0) {
            parser->abort_requested = 1;
            return result;
        }

        pos += (size_t)(eol - (base + pos)) + 1;
    }

    if (pos > 0) {
        size_t remaining = parser->line_buffer->size - pos;
        if (remaining > 0) {
            memmove(parser->line_buffer->data, parser->line_buffer->data + pos, remaining);
        }
        parser->line_buffer->size = remaining;
        parser->line_buffer->data[remaining] = '\0';
    }

    return 0;
}
