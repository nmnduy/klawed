/**
 * streaming_tool_accumulator.h - Testable tool call accumulation for streaming mode
 *
 * This module extracts the tool call accumulation logic from the OpenAI provider
 * to make it testable in isolation.
 */

#ifndef STREAMING_TOOL_ACCUMULATOR_H
#define STREAMING_TOOL_ACCUMULATOR_H

#include <cjson/cJSON.h>
#include "sse_parser.h"
#include "arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tool call accumulator context
 * Manages accumulation of tool calls from streaming SSE events
 */
typedef struct {
    cJSON *tool_calls_array;         // Array of accumulated tool calls
    int tool_calls_count;            // Number of tool calls
    Arena *arena;                    // Arena for allocations (optional)
    int has_reasoning;               // Whether reasoning_content was seen
} ToolCallAccumulator;

/**
 * Create a new tool call accumulator
 * @param arena Optional arena for allocations (NULL for heap)
 * @return New accumulator instance
 */
ToolCallAccumulator* tool_accumulator_create(Arena *arena);

/**
 * Destroy a tool call accumulator
 * @param acc The accumulator to destroy
 */
void tool_accumulator_destroy(ToolCallAccumulator *acc);

/**
 * Reset the accumulator to empty state
 * @param acc The accumulator to reset
 */
void tool_accumulator_reset(ToolCallAccumulator *acc);

/**
 * Process a tool_calls delta from a streaming chunk
 * @param acc The accumulator
 * @param tool_calls The tool_calls array from the delta (cJSON array)
 * @return 0 on success, -1 on error
 */
int tool_accumulator_process_delta(ToolCallAccumulator *acc, cJSON *tool_calls);

/**
 * Get the accumulated tool calls as a cJSON array
 * @param acc The accumulator
 * @return The tool_calls array (owned by accumulator, don't free)
 */
cJSON* tool_accumulator_get_tool_calls(ToolCallAccumulator *acc);

/**
 * Check if all tool calls have valid (non-empty) id and name
 * @param acc The accumulator
 * @return Number of valid tool calls
 */
int tool_accumulator_count_valid(ToolCallAccumulator *acc);

/**
 * Filter out tool calls with empty id or name
 * @param acc The accumulator
 * @return New cJSON array with only valid tool calls (caller must free)
 */
cJSON* tool_accumulator_filter_valid(ToolCallAccumulator *acc);

#ifdef __cplusplus
}
#endif

#endif // STREAMING_TOOL_ACCUMULATOR_H
