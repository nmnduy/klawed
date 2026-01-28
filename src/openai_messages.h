/*
 * openai_messages.h - OpenAI message format conversion
 *
 * Converts between internal vendor-agnostic message format
 * and OpenAI's API message format (with tool_calls)
 */

#ifndef OPENAI_MESSAGES_H
#define OPENAI_MESSAGES_H

#include <cjson/cJSON.h>
#include "klawed_internal.h"

/**
 * Build OpenAI request JSON from internal message format
 *
 * Converts InternalMessage[] to OpenAI's message format:
 * - Assistant messages: { role: "assistant", content: "...", tool_calls: [...] }
 * - User messages: { role: "user", content: "..." }
 * - Tool responses: { role: "tool", tool_call_id: "...", content: "..." }
 *
 * @param state - Conversation state with internal messages
 * @param enable_caching - Whether to add Anthropic cache_control markers (for compatible APIs)
 * @return JSON object with OpenAI request (caller must free), or NULL on error
 */
cJSON* build_openai_request(ConversationState *state, int enable_caching);

/**
 * Build OpenAI request JSON with reasoning_content support
 *
 * Same as build_openai_request but with option to include reasoning_content
 * in assistant messages. Used by providers that require reasoning_content
 * to be preserved (e.g., Moonshot/Kimi).
 *
 * @param state - Conversation state with internal messages
 * @param enable_caching - Whether to add Anthropic cache_control markers
 * @param include_reasoning_content - Whether to include reasoning_content in assistant messages
 * @return JSON object with OpenAI request (caller must free), or NULL on error
 */
cJSON* build_openai_request_with_reasoning(ConversationState *state,
                                            int enable_caching,
                                            int include_reasoning_content);

/**
 * Parse OpenAI response into internal message format
 *
 * Converts OpenAI API response to InternalMessage:
 * - content: "..." -> INTERNAL_TEXT
 * - tool_calls: [...] -> INTERNAL_TOOL_CALL blocks
 *
 * @param response - OpenAI API response JSON
 * @param out - Output: InternalMessage (caller must free contents), or empty message on error
 */
void parse_openai_response(cJSON *response, InternalMessage *out);

/**
 * Free internal message contents
 *
 * @param msg - Message to free (struct itself is not freed)
 */
void free_internal_message(InternalMessage *msg);

/**
 * Ensure all tool calls have matching tool results
 *
 * Scans the conversation state for assistant messages with tool_calls,
 * and verifies each has a corresponding tool_result in a following message.
 * If any tool calls are missing results (e.g., due to interrupted execution),
 * injects synthetic "interrupted" error results to maintain API consistency.
 *
 * Must be called with state locked.
 *
 * @param state - Conversation state to validate and fix
 */
void ensure_tool_results(ConversationState *state);

#endif // OPENAI_MESSAGES_H
