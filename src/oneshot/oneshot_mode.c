/*
 * Oneshot Mode
 *
 * Main entry point for single-command (oneshot) mode execution.
 * Executes a single prompt, processes tool calls recursively, and exits.
 */

#include "oneshot_mode.h"
#include "oneshot_output.h"
#include "oneshot_processor.h"
#include "../logger.h"
#include "../util/output_utils.h"
#include "../session/token_usage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Execute a single command and exit
 */
int oneshot_execute(ConversationState *state, const char *prompt) {
    if (!state || !prompt) {
        LOG_ERROR("oneshot_execute: NULL state or prompt");
        return 1;
    }

    LOG_INFO("Executing single command: %s", prompt);

    // Enable oneshot mode for structured tool output
    output_set_oneshot_mode(1);

    // Parse output format from environment
    OneshotFormat output_format = oneshot_get_output_format();

    // Add user message to conversation
    add_user_message(state, prompt);

    // Call API synchronously
    ApiResponse *response = call_api_with_retries(state);
    if (!response) {
        LOG_ERROR("Failed to get response from API");
        fprintf(stderr, "Error: Failed to get response from API\\n");
        return 1;
    }

    // Check if response contains an error message
    if (response->error_message) {
        LOG_ERROR("API error: %s", response->error_message);
        fprintf(stderr, "Error: %s\\n", response->error_message);
        api_response_free(response);
        return 1;
    }

    cJSON *error = cJSON_GetObjectItem(response->raw_response, "error");
    if (error && !cJSON_IsNull(error)) {
        cJSON *error_message = cJSON_GetObjectItem(error, "message");
        const char *error_msg = error_message ? error_message->valuestring : "Unknown error";
        LOG_ERROR("API error: %s", error_msg);
        fprintf(stderr, "Error: %s\\n", error_msg);
        api_response_free(response);
        return 1;
    }

    // Process response recursively (handles tool calls and follow-up responses)
    int result = oneshot_process_response(state, response, (int)output_format);
    api_response_free(response);

    // Print token usage summary at the end of single command mode
    // This includes subagent executions
    session_print_token_usage(state);

    return result;
}
