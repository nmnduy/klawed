/*
 * api_client.c - API Client with Retry Logic
 */

#define _POSIX_C_SOURCE 200809L

#include "api_client.h"
#include "api_response.h"
#include "../background_init.h"
#include "../logger.h"
#include "../provider.h"
#include "../retry_logic.h"
#include "../compaction.h"
#include "../persistence.h"
#include "../model_capabilities.h"
#include "../ui/print_helpers.h"

#include "../context/memory_injection.h"

#ifdef HAVE_ZMQ
#include "../zmq_socket.h"
#endif

#include "../sqlite_queue.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <bsd/stdlib.h>
#include <cjson/cJSON.h>



/**
 * Maximum number of step-backward recovery attempts before giving up.
 * Each recovery only removes a single tool result, which may be too small
 * to meaningfully reduce context size in extreme cases.
 * This limit applies to both context overflow recovery and bad-tool-output recovery.
 */
#define MAX_STEPBACK_RECOVERIES 5

/**
 * Find the most recent tool result in the conversation state.
 *
 * Searches backwards from the end for a USER message containing
 * an INTERNAL_TOOL_RESPONSE content item.
 *
 * @param state Conversation state
 * @param out_msg_idx Output: message index of found tool result
 * @param out_content_idx Output: content index within that message
 * @return 1 if found, 0 if not
 */
static int find_last_tool_result(ConversationState *state,
                                  int *out_msg_idx,
                                  int *out_content_idx) {
    if (!state) return 0;

    for (int i = state->count - 1; i >= 0; i--) {
        InternalMessage *msg = &state->messages[i];
        if (msg->role == MSG_USER) {
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *content = &msg->contents[j];
                if (content->type == INTERNAL_TOOL_RESPONSE) {
                    if (out_msg_idx) *out_msg_idx = i;
                    if (out_content_idx) *out_content_idx = j;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/**
 * Replace a tool result with an error message.
 *
 * Keeps the tool_id and tool_name on the content slot, replaces
 * the tool_output with an error JSON object, and marks is_error.
 *
 * @param content The tool response content to replace
 * @param error_text The error message to set
 * @return 1 on success, 0 on failure
 */
static int replace_tool_result_with_error(InternalContent *content,
                                           const char *error_text) {
    if (!content || !error_text) return 0;

    cJSON *error_output = cJSON_CreateObject();
    if (!error_output) {
        LOG_ERROR("Failed to create error JSON for tool result replacement");
        return 0;
    }

    cJSON_AddStringToObject(error_output, "error", error_text);

    if (content->tool_output) {
        cJSON_Delete(content->tool_output);
    }
    content->tool_output = error_output;
    content->is_error = 1;

    return 1;
}

/**
 * Handle context overflow error by replacing last tool result
 * Only called when auto-compaction is enabled
 *
 * When a tool output causes context overflow:
 * - Keep the assistant's tool call
 * - Replace the tool result with an error message
 * - Include the size of the failed output
 * - Let the AI retry with a smarter approach
 *
 * Returns: 1 if recovery was attempted, 0 if not applicable
 */
static int handle_context_overflow_recovery(ConversationState *state, const char *error_msg) {
    if (!state || !error_msg) {
        return 0;
    }

    // Only recover if auto-compaction is enabled
    // Otherwise we might be hitting true model limit
    if (!state->compaction_config) {
        LOG_DEBUG("Context overflow detected but auto-compaction disabled - not recovering");
        return 0;
    }

    // Check if this is a context length error
    if (!is_context_length_error(error_msg, "invalid_request_error")) {
        return 0;
    }

    LOG_INFO("Context overflow error detected with auto-compaction enabled");

    // Find the last tool result
    int found_msg_idx = -1, found_content_idx = -1;
    if (!find_last_tool_result(state, &found_msg_idx, &found_content_idx)) {
        LOG_WARN("Context overflow recovery: No tool result found to replace");
        return 0;
    }

    InternalContent *content = &state->messages[found_msg_idx].contents[found_content_idx];

    // Calculate the size of the original tool output
    size_t original_size = 0;
    size_t estimated_tokens = 0;

    if (content->tool_output) {
        char *output_str = cJSON_PrintUnformatted(content->tool_output);
        if (output_str) {
            original_size = strlen(output_str);
            // Estimate tokens: ~4 chars per token
            estimated_tokens = (original_size + 3) / 4;
            free(output_str);
        }
    }

    LOG_INFO("Context overflow recovery: Replacing tool result (tool=%s, id=%s, size=%zu bytes, ~%zu tokens)",
             content->tool_name ? content->tool_name : "unknown",
             content->tool_id ? content->tool_id : "unknown",
             original_size, estimated_tokens);

    // Build error message with size information
    char error_text[512];
    snprintf(error_text, sizeof(error_text),
            "Error: Context limit exceeded. Tool output was %zu bytes "
            "(approximately %zu tokens). Please try a different approach: "
            "use smaller ranges, apply filters, or use a different tool "
            "that produces less output.",
            original_size, estimated_tokens);

    if (!replace_tool_result_with_error(content, error_text)) {
        return 0;
    }

    LOG_INFO("Context overflow recovery: Successfully replaced tool result with error message");
    return 1;  // Recovery performed
}

/**
 * Handle API validation error (HTTP 400) caused by tool output
 * containing binary/invalid content by replacing the last tool result
 * with an error message and retrying.
 *
 * When a tool output contains binary/non-UTF-8 data (e.g., from
 * `cat` on a binary file), the serialized JSON sent to the API
 * may be invalid, resulting in HTTP 400 from providers like DeepSeek
 * that strictly validate JSON content.
 *
 * This function steps backward by:
 * - Keeping the assistant's tool call
 * - Replacing the tool result with an error message explaining the issue
 * - Letting the AI retry with a smarter approach (e.g., using `file`,
 *   `head -c`, `xxd`, or `hexdump` instead of `cat`)
 *
 * Returns: 1 if recovery was attempted, 0 if not applicable
 */
static int handle_bad_tool_output_recovery(ConversationState *state,
                                            long http_status,
                                            const char *error_msg) {
    if (!state) {
        return 0;
    }

    // Only handle HTTP 400/422 (Bad Request / Unprocessable Entity) — these
    // are what providers like DeepSeek return for invalid JSON content (binary
    // data in tool output being serialized as non-UTF-8 bytes)
    if (http_status != 400 && http_status != 422) {
        return 0;
    }

    // Avoid false positives: check error message for binary/encoding-related
    // keywords. If the message is available and doesn't suggest binary content,
    // don't trigger recovery (it's likely a different kind of 400/422).
    if (error_msg && error_msg[0] != '\0') {
        // Make lowercase copy for case-insensitive matching
        size_t msg_len = strlen(error_msg);
        char *msg_lower = malloc(msg_len + 1);
        if (msg_lower) {
            for (size_t i = 0; i < msg_len; i++) {
                msg_lower[i] = (char)tolower((unsigned char)error_msg[i]);
            }
            msg_lower[msg_len] = '\0';

            int is_binary_related = (
                strstr(msg_lower, "character") != NULL ||
                strstr(msg_lower, "utf") != NULL ||
                strstr(msg_lower, "encoding") != NULL ||
                strstr(msg_lower, "invalid") != NULL ||
                strstr(msg_lower, "binary") != NULL ||
                strstr(msg_lower, "parse") != NULL ||
                strstr(msg_lower, "malformed") != NULL
            );
            free(msg_lower);

            if (!is_binary_related) {
                LOG_DEBUG("Bad tool output recovery: HTTP %ld error message does not "
                          "suggest binary content issue — not recovering: %s",
                          http_status, error_msg);
                return 0;
            }
        }
    }

    // Find the last tool result
    int found_msg_idx = -1, found_content_idx = -1;
    if (!find_last_tool_result(state, &found_msg_idx, &found_content_idx)) {
        LOG_DEBUG("Bad tool output recovery: No tool result found to replace (HTTP %ld)",
                  http_status);
        return 0;
    }

    InternalContent *content = &state->messages[found_msg_idx].contents[found_content_idx];

    // Guard against replacing the same tool result repeatedly
    if (content->is_error) {
        LOG_DEBUG("Bad tool output recovery: Last tool result is already an error — "
                  "not recovering again to avoid loop (tool=%s, id=%s)",
                  content->tool_name ? content->tool_name : "unknown",
                  content->tool_id ? content->tool_id : "unknown");
        return 0;
    }

    LOG_INFO("Bad tool output recovery: HTTP %ld detected — replacing tool result "
             "(tool=%s, id=%s, error_body: %s)",
             http_status,
             content->tool_name ? content->tool_name : "unknown",
             content->tool_id ? content->tool_id : "unknown",
             error_msg ? error_msg : "(null)");

    // Build error message explaining the issue
    char error_text[512];
    snprintf(error_text, sizeof(error_text),
            "Error: The API rejected the request (HTTP %ld). This is likely because "
            "the tool output contained binary or non-UTF-8 content (e.g., from `cat` on "
            "a binary file). Use a different approach to inspect this file: "
            "use `file` to check the file type, `head -c` to read a small portion, "
            "or `xxd`/`hexdump` to view binary content.",
            http_status);

    if (!replace_tool_result_with_error(content, error_text)) {
        return 0;
    }

    LOG_INFO("Bad tool output recovery: Successfully replaced tool result with error message");
    return 1;  // Recovery performed
}

/**
 * Call API with retry logic (generic wrapper around provider->call_api)
 * Handles exponential backoff for retryable errors
 * Returns: ApiResponse or NULL on error
 */
ApiResponse* call_api_with_retries(ConversationState *state) {
    if (!state) {
        LOG_ERROR("Invalid conversation state");
        return NULL;
    }

    // Ensure system prompt is ready before API call
    await_system_prompt_ready(state);

    // Safety guard: validate conversation ends with a user/tool message.
    // An API call where the last meaningful message is a system, compaction,
    // or assistant message (no user/tool turn initiated) is always wrong.
    {
        int last_meaningful = -1;
        for (int i = state->count - 1; i >= 0; i--) {
            MessageRole r = state->messages[i].role;
            if (r != MSG_SYSTEM && r != MSG_AUTO_COMPACTION) {
                last_meaningful = i;
                break;
            }
        }
        if (last_meaningful < 0) {
            LOG_ERROR("call_api_with_retries: no user/assistant message in state "
                      "(count=%d) — refusing to send invalid request", state->count);
            return NULL;
        }
        /* Refuse if the last meaningful message is an assistant message.
         * An API call should only be triggered by a user message or tool
         * results — never by the assistant's own output.  This prevents
         * spurious continuation after the assistant has finished its turn
         * (e.g. after auto-compaction when no goal is active). */
        if (state->messages[last_meaningful].role == MSG_ASSISTANT) {
            LOG_ERROR("call_api_with_retries: last meaningful message is "
                      "assistant (role=%d at idx=%d, count=%d) — refusing "
                      "to send request without a user/tool turn",
                      state->messages[last_meaningful].role,
                      last_meaningful, state->count);
            return NULL;
        }
    }

    // Log plan mode before API call
    LOG_DEBUG("[API] call_api_with_retries: plan_mode=%d", state->plan_mode);

    // Lazy-initialize provider to avoid blocking initial TUI render
    if (!state->provider) {
        LOG_INFO("Initializing API provider in background context...");
        ProviderInitResult provider_result;
        provider_init(state->model, state->api_key, &provider_result);
        if (!provider_result.provider) {
            const char *msg = provider_result.error_message ? provider_result.error_message : "unknown error";
            LOG_ERROR("Provider initialization failed: %s", msg);
            print_error("Failed to initialize API provider. Check configuration.");
            free(provider_result.error_message);
            free(provider_result.api_url);
            return NULL;
        }

        // Transfer ownership to state and update API URL and model
        if (state->api_url) {
            free(state->api_url);
        }
        state->api_url = provider_result.api_url;
        state->provider = provider_result.provider;

        // Update model to the one selected by provider_init (respects config file)
        if (provider_result.model) {
            if (state->model) {
                free(state->model);
            }
            state->model = provider_result.model;
            LOG_DEBUG("Updated state->model from provider config: %s", state->model);
        }

        free(provider_result.error_message);

        LOG_INFO("Provider initialized: %s, API URL: %s, Model: %s",
                 state->provider->name, state->api_url ? state->api_url : "(null)",
                 state->model ? state->model : "(null)");
    }

    int attempt_num = 1;
    int backoff_ms = INITIAL_BACKOFF_MS;
    char *last_error = NULL;
    long last_http_status = 0;

    struct timespec call_start, call_end, retry_start;
    clock_gettime(CLOCK_MONOTONIC, &call_start);
    retry_start = call_start;

    LOG_DEBUG("Starting API call (provider: %s, model: %s)",
              state->provider->name, state->model);

    while (1) {
        // Check for interrupt request
        if (state->interrupt_requested) {
            LOG_INFO("API call interrupted by user request");
            print_error("Operation interrupted by user");
            free(last_error);
            return NULL;
        }

        // Check if we've exceeded max retry duration
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - retry_start.tv_sec) * 1000 +
                         (now.tv_nsec - retry_start.tv_nsec) / 1000000;

        if (attempt_num > 1 && elapsed_ms >= state->max_retry_duration_ms) {
            LOG_ERROR("Maximum retry duration (%d ms) exceeded after %d attempts",
                     state->max_retry_duration_ms, attempt_num - 1);

            // Include the last error details for user context
            char error_msg[1024];
            if (last_error && last_http_status > 0) {
                snprintf(error_msg, sizeof(error_msg),
                        "Maximum retry duration exceeded. Last error: %s (HTTP %ld)",
                        last_error, last_http_status);
            } else {
                snprintf(error_msg, sizeof(error_msg),
                        "Maximum retry duration exceeded");
            }
            print_error(error_msg);
            free(last_error);
            return NULL;
        }

        // Call provider's single-attempt API call
        LOG_DEBUG("API call attempt %d (elapsed: %ld ms)", attempt_num, elapsed_ms);

        // Inject/refresh memory context before each API call
        if (inject_memory_context(state) == 0) {
            LOG_DEBUG("Memory context injection/refresh completed");
        } else {
            LOG_WARN("Memory context injection/refresh failed");
        }

        ApiCallResult result = {0};
        state->provider->call_api(state->provider, state, &result);

        // Success case
        if (result.response) {
            clock_gettime(CLOCK_MONOTONIC, &call_end);
            long total_ms = (call_end.tv_sec - call_start.tv_sec) * 1000 +
                           (call_end.tv_nsec - call_start.tv_nsec) / 1000000;

            LOG_INFO("API call succeeded (duration: %ld ms, provider duration: %ld ms, attempts: %d, auth_refreshed: %s, plan_mode: %s)",
                     total_ms, result.duration_ms, attempt_num,
                     result.auth_refreshed ? "yes" : "no",
                     state->plan_mode ? "yes" : "no");



            // Log success to persistence
            if (state->persistence_db && result.raw_response) {
                // Tool count is already available in the ApiResponse
                int tool_count = result.response->tool_count;

                persistence_log_api_call(
                    state->persistence_db,
                    state->session_id,
                    state->api_url,
                    result.request_json ? result.request_json : "(request not available)",
                    result.headers_json,
                    result.raw_response,
                    state->model,
                    "success",
                    (int)result.http_status,
                    NULL,
                    result.duration_ms,
                    tool_count
                );

                // Update token count in compaction config after successful API call
                if (state->compaction_config) {
                    compaction_update_token_count(state, state->compaction_config);
                }

                // Update context-aware max_tokens tracking for next call
                // Extract prompt_tokens from API response usage
                int found_prompt_tokens = 0;
                if (result.raw_response) {
                    cJSON *parsed_response = cJSON_Parse(result.raw_response);
                    if (parsed_response) {
                        cJSON *usage = cJSON_GetObjectItem(parsed_response, "usage");
                        if (usage) {
                            // Try OpenAI format first
                            cJSON *prompt_tokens_json = cJSON_GetObjectItem(usage, "prompt_tokens");
                            if (!prompt_tokens_json) {
                                // Try Anthropic format (input_tokens)
                                prompt_tokens_json = cJSON_GetObjectItem(usage, "input_tokens");
                            }
                            if (prompt_tokens_json && cJSON_IsNumber(prompt_tokens_json)) {
                                state->last_prompt_tokens = (int)prompt_tokens_json->valueint;
                                found_prompt_tokens = 1;
                                LOG_DEBUG("Updated last_prompt_tokens: %d", state->last_prompt_tokens);
                            }
                        }
                        cJSON_Delete(parsed_response);
                    }

                    /* Fireworks provider fallback: check response headers for
                     * fireworks-prompt-tokens when body usage has zero prompt tokens */
                    if (!found_prompt_tokens && result.headers_json) {
                        cJSON *headers_array = cJSON_Parse(result.headers_json);
                        if (headers_array && cJSON_IsArray(headers_array)) {
                            int array_size = cJSON_GetArraySize(headers_array);
                            for (int i = 0; i < array_size; i++) {
                                cJSON *header_item = cJSON_GetArrayItem(headers_array, i);
                                if (!header_item) continue;
                                cJSON *name_item = cJSON_GetObjectItem(header_item, "name");
                                cJSON *value_item = cJSON_GetObjectItem(header_item, "value");
                                if (!name_item || !value_item) continue;
                                if (!cJSON_IsString(name_item) || !cJSON_IsString(value_item)) continue;
                                if (strcmp(name_item->valuestring, "fireworks-prompt-tokens") == 0) {
                                    char *endptr;
                                    long val = strtol(value_item->valuestring, &endptr, 10);
                                    if (*endptr == '\0' && val >= 0) {
                                        state->last_prompt_tokens = (int)val;
                                        found_prompt_tokens = 1;
                                        LOG_DEBUG("Fireworks fallback: last_prompt_tokens=%d from header",
                                                 state->last_prompt_tokens);
                                    }
                                    break;
                                }
                            }
                            cJSON_Delete(headers_array);
                        }
                    }

                    // Update context_limit if not already set
                    if (state->context_limit == 0 && state->model) {
                        ModelCapabilities caps = get_model_capabilities(state->model, 128000, 16384);
                        state->context_limit = caps.context_limit;
                        LOG_DEBUG("Set context_limit from model capabilities: %d", state->context_limit);
                    }
                }
            }

            // Reset recovery attempts on success
            state->stepback_recovery_attempts = 0;

            // Cleanup and return
            free(result.raw_response);
            free(result.request_json);
            free(result.error_message);
            if (last_error) {
                free(last_error);
                last_error = NULL;
            }
            return result.response;
        }

        // Error case - check if retryable
        LOG_WARN("API call failed (attempt %d): %s (HTTP %ld, retryable: %s)",
                 attempt_num,
                 result.error_message ? result.error_message : "(unknown)",
                 result.http_status,
                 result.is_retryable ? "yes" : "no");

        // Log error to persistence
        if (state->persistence_db) {
            persistence_log_api_call(
                state->persistence_db,
                state->session_id,
                state->api_url,
                result.request_json ? result.request_json : "(request not available)",
                result.headers_json,
                result.raw_response,
                state->model,
                "error",
                (int)result.http_status,
                result.error_message,
                result.duration_ms,
                0
            );
        }

        // Save last error details for potential timeout message
        if (last_error) {
            free(last_error);
        }
        last_error = result.error_message ? strdup(result.error_message) : NULL;
        last_http_status = result.http_status;

        // Check if we should retry
        if (!result.is_retryable) {
            // Non-retryable error
            int recovery_performed = 0;

            // Step backward 1: Try context overflow recovery (requires auto-compaction)
            if (!recovery_performed && handle_context_overflow_recovery(state, result.error_message)) {
                recovery_performed = 1;
            }

            // Step backward 2: Try bad tool output recovery (HTTP 400 from binary content)
            // This works independently of auto-compaction setting
            if (!recovery_performed && handle_bad_tool_output_recovery(state, result.http_status, result.error_message)) {
                recovery_performed = 1;
            }

            if (recovery_performed) {
                state->stepback_recovery_attempts++;

                if (state->stepback_recovery_attempts > MAX_STEPBACK_RECOVERIES) {
                    LOG_ERROR("Step-backward recovery failed after %d attempts - giving up",
                              MAX_STEPBACK_RECOVERIES);

                    char error_msg[512];
                    snprintf(error_msg, sizeof(error_msg),
                             "API call recovery failed after %d attempts. "
                             "The conversation may have unrecoverable state. "
                             "Please start a new conversation.",
                             MAX_STEPBACK_RECOVERIES);
                    print_error(error_msg);

                    // Create an error response
                    ApiResponse *error_response = calloc(1, sizeof(ApiResponse));
                    if (error_response) {
                        error_response->error_message = strdup(error_msg);
                    }

                    free(last_error);
                    free(result.raw_response);
                    free(result.request_json);
                    free(result.error_message);
                    return error_response;
                }

                LOG_INFO("Step-backward recovery applied (attempt %d/%d) - retrying API call",
                         state->stepback_recovery_attempts, MAX_STEPBACK_RECOVERIES);
                free(result.raw_response);
                free(result.request_json);
                free(result.error_message);
                // Don't increment attempt_num for recovery retry
                continue;  // Retry the API call with modified conversation
            }

            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg),
                    "API call failed: %s (HTTP %ld)",
                    result.error_message ? result.error_message : "unknown error",
                    result.http_status);
            print_error(error_msg);

            // Create an error response instead of returning NULL
            ApiResponse *error_response = calloc(1, sizeof(ApiResponse));
            if (error_response) {
                error_response->error_message = strdup(result.error_message ? result.error_message : "unknown error");
            }

            free(last_error);
            free(result.raw_response);
            free(result.request_json);
            free(result.error_message);
            return error_response;
        }

        // Calculate backoff with jitter (0-25% reduction)
        uint32_t jitter = arc4random_uniform((uint32_t)(backoff_ms / 4));
        int delay_ms = backoff_ms - (int)jitter;

        // Check if this delay would exceed max retry duration
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed_ms = (now.tv_sec - retry_start.tv_sec) * 1000 +
                    (now.tv_nsec - retry_start.tv_nsec) / 1000000;
        long remaining_ms = state->max_retry_duration_ms - elapsed_ms;

        if (delay_ms > remaining_ms) {
            delay_ms = (int)remaining_ms;
            if (delay_ms <= 0) {
                LOG_ERROR("Maximum retry duration (%d ms) exceeded", state->max_retry_duration_ms);

                // Include the error details for user context
                char error_msg[1024];
                if (result.error_message && result.http_status > 0) {
                    snprintf(error_msg, sizeof(error_msg),
                            "Maximum retry duration exceeded. Last error: %s (HTTP %ld)",
                            result.error_message, result.http_status);
                } else {
                    snprintf(error_msg, sizeof(error_msg),
                            "Maximum retry duration exceeded");
                }
                print_error(error_msg);

                free(last_error);
                free(result.raw_response);
                free(result.request_json);
                free(result.error_message);
                return NULL;
            }
        }

        // Display retry message to user
        char retry_msg[512];
        const char *error_type = (result.http_status == 429) ? "Rate limit" :
                                (result.http_status == 408) ? "Request timeout" :
                                (result.http_status >= 500) ? "Server error" : "Error";
        snprintf(retry_msg, sizeof(retry_msg),
                "%s - retrying in %d ms... (attempt %d)",
                error_type, delay_ms, attempt_num + 1);
        print_error(retry_msg);

        LOG_INFO("Retrying after %d ms (elapsed: %ld ms, remaining: %ld ms)",
                delay_ms, elapsed_ms, remaining_ms);

        // Sleep and retry
        usleep((useconds_t)(delay_ms * 1000));
        backoff_ms = (int)(backoff_ms * BACKOFF_MULTIPLIER);
        if (backoff_ms > MAX_BACKOFF_MS) {
            backoff_ms = MAX_BACKOFF_MS;
        }

        free(result.raw_response);
        free(result.request_json);
        free(result.error_message);
        attempt_num++;
    }
}
