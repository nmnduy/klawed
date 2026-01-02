/*
 * openai_responses.h - OpenAI Responses API format conversion
 *
 * Converts between internal vendor-agnostic message format
 * and OpenAI's Responses API format (/v1/responses endpoint)
 *
 * Also provides separated HTTP request building, submission, and response parsing
 * functions for the Responses API endpoint.
 */

#ifndef OPENAI_RESPONSES_H
#define OPENAI_RESPONSES_H

#include <cjson/cJSON.h>
#include "klawed_internal.h"
#include "http_client.h"
#include "openai_provider.h"  // For OpenAIConfig

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
 * Build HTTP request for OpenAI Responses API
 *
 * Constructs a complete HttpRequest struct including:
 * - URL (from config->base_url)
 * - Headers (Content-Type, Authorization, extra headers)
 * - Body (JSON request from build_openai_responses_request)
 *
 * @param state - Conversation state with messages
 * @param config - OpenAI provider configuration
 * @param enable_caching - Whether to enable prompt caching
 * @return HttpRequest struct (caller must free request->headers and request->body on success), or empty struct on error
 */
HttpRequest build_responses_http_request(ConversationState *state, OpenAIConfig *config, int enable_caching);

/**
 * Submit HTTP request to OpenAI Responses API
 *
 * Executes the HTTP request using the HTTP client with progress callback
 * for interrupt handling.
 *
 * @param request - Pre-built HTTP request
 * @param state - Conversation state (for interrupt checking)
 * @return HttpResponse (caller must free with http_response_free()), or NULL on error
 */
HttpResponse* submit_responses_http_request(HttpRequest *request, ConversationState *state);

/**
 * Parse OpenAI Responses API response into ApiResponse
 *
 * Converts raw HTTP response body to ApiResponse:
 * - Extracts text content from output array
 * - Parses tool calls if present
 * - Extracts usage statistics
 *
 * @param raw_response - Raw HTTP response body (JSON string)
 * @return ApiResponse (caller must free with api_response_free()), or NULL on error
 */
ApiResponse* parse_responses_http_response(const char *raw_response);

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
