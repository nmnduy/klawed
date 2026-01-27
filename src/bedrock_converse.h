/*
 * bedrock_converse.h - AWS Bedrock Converse API support
 *
 * Implements the unified Converse API for AWS Bedrock, which provides
 * a consistent interface across different foundation models.
 *
 * Converse API endpoint: POST /model/{modelId}/converse
 *
 * Key differences from the Invoke API (aws_bedrock.h):
 *   - Uses unified message format across all models
 *   - System prompt is a separate top-level field
 *   - Tools use `toolSpec` wrapper with `inputSchema.json`
 *   - Response has `output.message` structure
 *   - Different stop reason values (end_turn, tool_use, max_tokens)
 */

#ifndef BEDROCK_CONVERSE_H
#define BEDROCK_CONVERSE_H

#include <curl/curl.h>
#include <cjson/cJSON.h>
#include "aws_bedrock.h"  /* Reuse AWSCredentials, BedrockConfig, bedrock_sign_request */

/* ============================================================================
 * Configuration
 * ============================================================================ */

/* Converse API endpoint path format */
#define AWS_BEDROCK_CONVERSE_PATH "/model/%s/converse"
#define AWS_BEDROCK_CONVERSE_STREAM_PATH "/model/%s/converse-stream"

/* ============================================================================
 * Function Declarations
 * ============================================================================ */

/**
 * Build AWS Bedrock Converse API endpoint URL
 *
 * Creates the full URL for the Converse API:
 *   https://bedrock-runtime.{region}.amazonaws.com/model/{model-id}/converse
 *
 * Parameters:
 *   region   - AWS region (e.g., "us-west-2")
 *   model_id - Full Bedrock model ID (e.g., "anthropic.claude-3-sonnet-20240229-v1:0")
 *
 * Returns: Newly allocated string (caller must free), or NULL on error
 */
char* bedrock_converse_build_endpoint(const char *region, const char *model_id);

/**
 * Build AWS Bedrock Converse streaming API endpoint URL
 *
 * Creates the full URL for the Converse streaming API:
 *   https://bedrock-runtime.{region}.amazonaws.com/model/{model-id}/converse-stream
 *
 * Parameters:
 *   region   - AWS region (e.g., "us-west-2")
 *   model_id - Full Bedrock model ID
 *
 * Returns: Newly allocated string (caller must free), or NULL on error
 */
char* bedrock_converse_build_streaming_endpoint(const char *region, const char *model_id);

/**
 * Convert OpenAI format request to AWS Bedrock Converse API format
 *
 * Conversion mapping:
 *   OpenAI                          -> Converse
 *   ─────────────────────────────────────────────────────────────
 *   messages[role="system"]         -> system[{text: "..."}]
 *   messages[role="user"]           -> messages[{role:"user", content:[{text:"..."}]}]
 *   messages[role="assistant"]      -> messages[{role:"assistant", content:[...]}]
 *   tool_calls                      -> content[{toolUse:{...}}]
 *   messages[role="tool"]           -> content[{toolResult:{...}}]
 *   tools                           -> toolConfig.tools[{toolSpec:{...}}]
 *   max_completion_tokens           -> inferenceConfig.maxTokens
 *   temperature                     -> inferenceConfig.temperature
 *   top_p                           -> inferenceConfig.topP
 *
 * Parameters:
 *   openai_request - JSON string in OpenAI chat completion format
 *
 * Returns: Newly allocated JSON string in Converse format (caller must free),
 *          or NULL on error
 */
char* bedrock_converse_convert_request(const char *openai_request);

/**
 * Convert AWS Bedrock Converse API response to OpenAI format
 *
 * Conversion mapping:
 *   Converse                        -> OpenAI
 *   ─────────────────────────────────────────────────────────────
 *   output.message                  -> choices[0].message
 *   output.message.content[text]    -> message.content
 *   output.message.content[toolUse] -> message.tool_calls
 *   stopReason "end_turn"           -> finish_reason "stop"
 *   stopReason "tool_use"           -> finish_reason "tool_calls"
 *   stopReason "max_tokens"         -> finish_reason "length"
 *   usage.inputTokens               -> usage.prompt_tokens
 *   usage.outputTokens              -> usage.completion_tokens
 *   usage.totalTokens               -> usage.total_tokens
 *   usage.cacheReadInputTokens      -> usage.cache_read_input_tokens
 *   usage.cacheWriteInputTokens     -> usage.cache_write_input_tokens
 *
 * Parameters:
 *   converse_response - JSON string in Converse API response format
 *
 * Returns: cJSON object in OpenAI format (caller must delete), or NULL on error
 */
cJSON* bedrock_converse_convert_response(const char *converse_response);

/**
 * URL-encode a model ID for use in the Converse API endpoint
 *
 * Model IDs may contain special characters (e.g., colons in version strings)
 * that need to be percent-encoded for HTTP paths.
 *
 * Parameters:
 *   model_id - Raw model ID string
 *
 * Returns: Newly allocated URL-encoded string (caller must free), or NULL on error
 */
char* bedrock_converse_encode_model_id(const char *model_id);

#endif /* BEDROCK_CONVERSE_H */
