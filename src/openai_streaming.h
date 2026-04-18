/*
 * openai_streaming.h - OpenAI streaming response accumulator
 *
 * Accumulates streaming SSE events from OpenAI-compatible APIs
 * into a complete response, including text, reasoning_content, and tool calls.
 */

#ifndef OPENAI_STREAMING_H
#define OPENAI_STREAMING_H

#include <stddef.h>
#include <cjson/cJSON.h>
#include "sse_parser.h"  // For StreamEvent
#include "arena.h"       // For arena allocation
#include "streaming_tool_accumulator.h"  // For tool call accumulation

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Streaming Accumulator State
 * ============================================================================ */

typedef struct {
    // Accumulated text content
    char *accumulated_text;
    size_t accumulated_size;
    size_t accumulated_capacity;

    // Accumulated reasoning_content (for thinking models)
    char *accumulated_reasoning;
    size_t reasoning_size;
    size_t reasoning_capacity;

    // Metadata from stream
    char *finish_reason;
    char *model;
    char *message_id;

    // Tool calls accumulated using shared module
    ToolCallAccumulator *tool_accumulator;

    // Usage tokens from final streaming chunk (if provided by API)
    int prompt_tokens;
    int completion_tokens;
    int total_tokens;

    // Arena for allocations
    Arena *arena;

    // Flags for tracking first content (for TUI integration)
    int reasoning_line_added;
    int assistant_line_added;
} OpenAIStreamingAccumulator;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Initialize a streaming accumulator
 * @param acc - Pointer to accumulator struct (will be zeroed)
 * @return 0 on success, -1 on error
 */
int openai_streaming_accumulator_init(OpenAIStreamingAccumulator *acc);

/**
 * Free resources in a streaming accumulator
 * @param acc - Pointer to accumulator struct
 */
void openai_streaming_accumulator_free(OpenAIStreamingAccumulator *acc);

/**
 * Free a streaming context that contains an accumulator
 * Helper for OpenAIProviderStreamingContext cleanup
 * @param ctx - Pointer to context struct containing acc member
 */
void openai_streaming_context_free(void *ctx);

/**
 * Process a single SSE stream event
 * @param acc - Accumulator state
 * @param event - Stream event from SSE parser
 * @return 0 to continue, 1 to abort stream
 */
int openai_streaming_process_event(OpenAIStreamingAccumulator *acc, const StreamEvent *event);

/**
 * Build a complete OpenAI-style response JSON from accumulated data
 * @param acc - Accumulator state (must have been used during streaming)
 * @return cJSON object representing the response (caller must free), or NULL on error
 */
cJSON* openai_streaming_build_response(const OpenAIStreamingAccumulator *acc);

/**
 * Get the accumulated text content
 * @param acc - Accumulator state
 * @return Pointer to text (may be NULL if no text), owned by accumulator
 */
const char* openai_streaming_get_text(const OpenAIStreamingAccumulator *acc);

/**
 * Get the accumulated reasoning content
 * @param acc - Accumulator state
 * @return Pointer to reasoning (may be NULL), owned by accumulator
 */
const char* openai_streaming_get_reasoning(const OpenAIStreamingAccumulator *acc);

/**
 * Get the number of tool calls accumulated
 * @param acc - Accumulator state
 * @return Number of tool calls
 */
int openai_streaming_get_tool_call_count(const OpenAIStreamingAccumulator *acc);

/**
 * Get a specific tool call as cJSON object
 * @param acc - Accumulator state
 * @param index - Tool call index
 * @return cJSON object (owned by accumulator), or NULL if invalid index
 */
cJSON* openai_streaming_get_tool_call(const OpenAIStreamingAccumulator *acc, int index);

#ifdef __cplusplus
}
#endif

#endif // OPENAI_STREAMING_H
