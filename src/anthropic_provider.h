/*
 * anthropic_provider.h - Direct Anthropic Messages API provider
 *
 * Implements the Provider interface for calling Anthropic's Messages API
 * using x-api-key authentication and Anthropic-native request/response format.
 */

#ifndef ANTHROPIC_PROVIDER_H
#define ANTHROPIC_PROVIDER_H

#include "provider.h"
#include "klawed_internal.h"  /* ConversationState */
#include "arena.h"
#include "http_client.h"  /* StreamEvent */
#include <cjson/cJSON.h>

/**
 * Anthropic provider configuration
 */
typedef struct {
    char *api_key;        // API key for x-api-key authentication
    char *base_url;       // Anthropic Messages endpoint (e.g., "https://api.anthropic.com/v1/messages")
    char *auth_header_template;  // Custom auth header template (default: "x-api-key: %s")
    char **extra_headers;  // Additional curl headers (NULL-terminated array)
    int extra_headers_count;  // Number of extra headers
} AnthropicConfig;

/* ============================================================================
 * Streaming context — shared with anthropic_sub_provider
 * ============================================================================ */

/**
 * Streaming context passed to the SSE event callback.
 * Allocated on the stack by the caller, initialised via
 * anthropic_streaming_context_init(), freed via anthropic_streaming_context_free().
 */
typedef struct {
    ConversationState *state;
    char *accumulated_text;
    size_t accumulated_size;
    size_t accumulated_capacity;
    int content_block_index;
    char *content_block_type;
    char *tool_use_id;
    char *tool_use_name;
    char *tool_input_json;
    size_t tool_input_size;
    size_t tool_input_capacity;
    cJSON *message_start_data;
    char *stop_reason;
    Arena *arena;
} AnthropicStreamingContext;

/**
 * Initialise a streaming context.  Must be paired with
 * anthropic_streaming_context_free() once streaming is complete.
 */
void anthropic_streaming_context_init(AnthropicStreamingContext *ctx,
                                      ConversationState *state);

/**
 * Free all resources held by a streaming context.
 */
void anthropic_streaming_context_free(AnthropicStreamingContext *ctx);

/**
 * SSE event handler compatible with http_client_execute_stream().
 * Pass as the event_handler argument; pass an AnthropicStreamingContext *
 * as userdata.
 */
int anthropic_streaming_event_handler(StreamEvent *event, void *userdata);

/**
 * Convert an OpenAI-format request JSON string to Anthropic Messages API format.
 * Returns a newly allocated string (caller must free), or NULL on error.
 */
char* anthropic_convert_openai_to_anthropic_request(const char *openai_req);

/**
 * Convert an Anthropic Messages API response JSON string to OpenAI format.
 * Returns a newly allocated cJSON object (caller must cJSON_Delete), or NULL on error.
 */
cJSON* anthropic_convert_response_to_openai(const char *anthropic_raw);

/**
 * Create an Anthropic provider instance
 *
 * @param api_key - Anthropic API key (required)
 * @param base_url - Messages endpoint URL (if NULL uses default)
 * @return Provider instance (caller must cleanup via provider->cleanup()), or NULL on error
 */
Provider* anthropic_provider_create(const char *api_key, const char *base_url);

#endif // ANTHROPIC_PROVIDER_H
