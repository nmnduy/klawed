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
#include "../ui/print_helpers.h"

#ifdef HAVE_MEMVID
#include "../context/memory_injection.h"
#endif

#ifdef HAVE_ZMQ
#include "../zmq_socket.h"
#endif

#include "../sqlite_queue.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <bsd/stdlib.h>
#include <cjson/cJSON.h>

/**
 * Send API_CALL message to indicate waiting for API response
 */
static void send_api_call_message(ConversationState *state, const char *model, const char *provider) {
    if (!state) return;

    // Get current timestamp
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long timestamp_ms = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    // Create API_CALL message JSON
    cJSON *message_json = cJSON_CreateObject();
    if (!message_json) return;

    cJSON_AddStringToObject(message_json, "messageType", "API_CALL");
    cJSON_AddNumberToObject(message_json, "timestamp", (double)ts.tv_sec);
    cJSON_AddNumberToObject(message_json, "timestampMs", (double)timestamp_ms);

    // Add optional fields
    if (model) {
        cJSON_AddStringToObject(message_json, "model", model);
    }
    if (provider) {
        cJSON_AddStringToObject(message_json, "provider", provider);
    }

    // Estimate duration (default 5 seconds for most API calls)
    cJSON_AddNumberToObject(message_json, "estimatedDurationMs", 5000);

    char *message_str = cJSON_PrintUnformatted(message_json);
    if (!message_str) {
        cJSON_Delete(message_json);
        return;
    }

    // Send via ZMQ if enabled
#ifdef HAVE_ZMQ
    if (state->zmq_context) {
        zmq_socket_send(state->zmq_context, message_str, strlen(message_str));
    }
#endif

    // Send via SQLite queue if enabled
    if (state->sqlite_queue_context) {
        // Get receiver name from context
        const char *receiver = "client"; // Default receiver
        sqlite_queue_send(state->sqlite_queue_context, receiver, message_str, strlen(message_str));
    }

    free(message_str);
    cJSON_Delete(message_json);
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

    // Find the last USER message with INTERNAL_TOOL_RESPONSE content
    // Search backwards from the end
    int found_msg_idx = -1;
    int found_content_idx = -1;

    for (int i = state->count - 1; i >= 0; i--) {
        InternalMessage *msg = &state->messages[i];
        if (msg->role == MSG_USER) {
            // Check if this message contains tool responses
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *content = &msg->contents[j];
                if (content->type == INTERNAL_TOOL_RESPONSE) {
                    found_msg_idx = i;
                    found_content_idx = j;
                    break;
                }
            }
            if (found_msg_idx >= 0) {
                break;  // Found the most recent tool response
            }
        }
    }

    if (found_msg_idx < 0 || found_content_idx < 0) {
        LOG_WARN("Context overflow recovery: No tool result found to replace");
        return 0;
    }

    InternalMessage *msg = &state->messages[found_msg_idx];
    InternalContent *content = &msg->contents[found_content_idx];

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

    // Create error message JSON
    cJSON *error_output = cJSON_CreateObject();
    if (!error_output) {
        LOG_ERROR("Context overflow recovery: Failed to create error JSON");
        return 0;
    }

    // Build error message with size information
    char error_text[512];
    snprintf(error_text, sizeof(error_text),
            "Error: Context limit exceeded. Tool output was %zu bytes "
            "(approximately %zu tokens). Please try a different approach: "
            "use smaller ranges, apply filters, or use a different tool "
            "that produces less output.",
            original_size, estimated_tokens);

    cJSON_AddStringToObject(error_output, "error", error_text);

    // Replace the tool output (preserve tool_id and tool_name)
    if (content->tool_output) {
        cJSON_Delete(content->tool_output);
    }
    content->tool_output = error_output;
    content->is_error = 1;  // Mark as error

    LOG_INFO("Context overflow recovery: Successfully replaced tool result with error message");
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

        // Send API_CALL message to indicate waiting for API response
        send_api_call_message(state, state->model, state->provider->name);

        // Check and perform compaction if needed (before API call)
        if (state->compaction_config && compaction_should_trigger(state, state->compaction_config)) {
            LOG_INFO("Context compaction triggered before API call");
            CompactionResult compaction_result = {0};
            if (compaction_perform(state, state->compaction_config, state->session_id, &compaction_result) == 0) {
                // Send compaction notice to sqlite-queue reader if in queue mode
                if (compaction_result.success && state->sqlite_queue_context && state->sqlite_queue_context->enabled) {
                    sqlite_queue_send_compaction_notice(
                        state->sqlite_queue_context,
                        "client",  // Send to client
                        compaction_result.messages_compacted,
                        compaction_result.tokens_before,
                        compaction_result.tokens_after,
                        compaction_result.usage_before_pct,
                        compaction_result.usage_after_pct,
                        compaction_result.summary[0] != '\0' ? compaction_result.summary : NULL
                    );
                }
            } else {
                LOG_WARN("Compaction failed, continuing with API call");
            }
        }

#ifdef HAVE_MEMVID
        // Inject/refresh memory context before each API call
        if (inject_memory_context(state) == 0) {
            LOG_DEBUG("Memory context injection/refresh completed");
        } else {
            LOG_WARN("Memory context injection/refresh failed");
        }
#endif

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
            }

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

            // Try context overflow recovery if applicable
            if (handle_context_overflow_recovery(state, result.error_message)) {
                LOG_INFO("Context overflow recovery applied - retrying API call");
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
