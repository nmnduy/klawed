/*
 * openai_responses.h - OpenAI Responses API format conversion
 *
 * Converts between internal vendor-agnostic message format
 * and OpenAI's Responses API format (/v1/responses endpoint)
 */

#ifndef OPENAI_RESPONSES_H
#define OPENAI_RESPONSES_H

#include <cjson/cJSON.h>
#include "klawed_internal.h"

/**
 * Build OpenAI Responses API request JSON from internal message format
 *
 * Converts InternalMessage[] to OpenAI's Responses API format:
 * - input: array of items with types like "input_text", "input_image", etc.
 * - output: array of items with types like "output_text", "refusal", etc.
 *
 * @param state - Conversation state with internal messages
 * @param enable_caching - Whether to add cache_control markers
 * @return JSON object with OpenAI Responses API request (caller must free), or NULL on error
 */
cJSON* build_openai_responses_request(ConversationState *state, int enable_caching);

/**
 * Parse OpenAI Responses API response into internal message format
 *
 * Converts OpenAI Responses API response to InternalMessage:
 * - output array with items -> INTERNAL_TEXT or INTERNAL_TOOL_CALL blocks
 *
 * @param response - OpenAI Responses API response JSON
 * @return InternalMessage (caller must free contents), or empty message on error
 */
InternalMessage parse_openai_responses_response(cJSON *response);

/**
 * Build OpenAI Responses API tool definitions
 *
 * Returns tool definitions in the Responses API format:
 * - Uses a flat format with type: "function" for each tool
 * - Each tool has a "function" object with name, description, parameters
 *
 * @param state - Conversation state
 * @param enable_caching - Whether to add cache_control markers
 * @return JSON array of tool definitions (caller must free), or NULL on error
 */
cJSON* get_tool_definitions_for_responses_api(ConversationState *state, int enable_caching);

#endif // OPENAI_RESPONSES_H
