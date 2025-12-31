/*
 * deepseek_response_parser.h - DeepSeek API response parser
 * 
 * Handles DeepSeek-specific response parsing, including:
 * - Detecting incomplete tool call JSON payloads when finish_reason is "length"
 * - Requesting continuation of incomplete payloads
 */

#ifndef DEEPSEEK_RESPONSE_PARSER_H
#define DEEPSEEK_RESPONSE_PARSER_H

#include <cjson/cJSON.h>
#include "klawed_internal.h"  // For ApiResponse, ToolCall, etc.

/**
 * Check if a DeepSeek API response has incomplete tool call arguments
 * 
 * This function detects when:
 * 1. finish_reason is "length" (token limit reached)
 * 2. Tool call arguments JSON is incomplete (doesn't end with proper JSON structure)
 * 3. The tool call is a Write tool (most common case for large payloads)
 * 
 * @param raw_response The raw cJSON response from DeepSeek API
 * @param api_response The parsed ApiResponse structure
 * @return 1 if incomplete Write tool arguments detected, 0 otherwise
 */
int deepseek_has_incomplete_write_tool(const cJSON *raw_response, const ApiResponse *api_response);

/**
 * Build a continuation prompt for incomplete Write tool arguments
 * 
 * Creates a prompt that asks the API to continue the incomplete JSON payload
 * for a Write tool call.
 * 
 * @param tool_call The incomplete tool call
 * @param incomplete_args The incomplete arguments string
 * @return A prompt string (caller must free), or NULL on error
 */
char* deepseek_build_continuation_prompt(const ToolCall *tool_call, const char *incomplete_args);

/**
 * Parse a continuation response and merge it with the original incomplete tool call
 * 
 * @param continuation_response The API response containing the continuation
 * @param original_tool_call The original incomplete tool call (will be updated)
 * @return 1 on success, 0 on failure
 */
int deepseek_merge_continuation_response(const ApiResponse *continuation_response, 
                                        ToolCall *original_tool_call);

/**
 * Check if we should handle DeepSeek incomplete payloads
 * 
 * This checks if:
 * 1. We're using DeepSeek API
 * 2. The response has finish_reason "length"
 * 3. There are tool calls that might be incomplete
 * 
 * @param api_url The API URL to check for DeepSeek
 * @param raw_response The raw API response
 * @return 1 if should handle, 0 otherwise
 */
int deepseek_should_handle_incomplete_payload(const char *api_url, const cJSON *raw_response);

#endif // DEEPSEEK_RESPONSE_PARSER_H
