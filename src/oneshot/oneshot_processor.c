/*
 * Oneshot Response Processor
 *
 * Processes API responses in oneshot mode, handling tool execution
 * and recursive response processing until completion.
 */

#include "oneshot_processor.h"
#include "oneshot_output.h"
#include "../logger.h"
#include "../ui/tool_output_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * Process a single API response in oneshot mode
 * Recursively handles tool calls and follow-up responses
 */
int oneshot_process_response(ConversationState *state,
                              ApiResponse *response,
                              int output_format) {
    if (!state || !response) {
        LOG_ERROR("oneshot_process_response: NULL state or response");
        return 1;
    }

    // Print assistant's text content if present
    if (response->message.text && response->message.text[0] != '\0') {
        // Skip whitespace-only content
        const char *p = response->message.text;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        if (*p != '\0') {  // Has non-whitespace content
            printf("%s\n", p);
            fflush(stdout);
        }
    }

    // Add to conversation history
    cJSON *choices = cJSON_GetObjectItem(response->raw_response, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);

        // Check for finish_reason and log WARNING if it's 'length'
        cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
        if (finish_reason && cJSON_IsString(finish_reason) && finish_reason->valuestring) {
            if (strcmp(finish_reason->valuestring, "length") == 0) {
                LOG_WARN("API response stopped due to token limit (finish_reason: 'length')");
            }
        }

        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (message) {
            add_assistant_message_openai(state, message);
        }
    }

    // Process tool calls
    int tool_count = response->tool_count;
    ToolCall *tool_calls_array = response->tools;

    if (tool_count > 0) {
        LOG_INFO("Processing %d tool call(s) in single-command mode", tool_count);

        // Log details of each tool call
        for (int i = 0; i < tool_count; i++) {
            ToolCall *tool = &tool_calls_array[i];
            LOG_DEBUG("Tool call[%d]: id=%s, name=%s, has_params=%d",
                      i, tool->id ? tool->id : "NULL",
                      tool->name ? tool->name : "NULL",
                      tool->parameters != NULL);
        }

        InternalContent *results = calloc((size_t)tool_count, sizeof(InternalContent));
        if (!results) {
            LOG_ERROR("Failed to allocate tool result buffer");
            return 1;
        }

        int valid_tool_calls = 0;
        for (int i = 0; i < tool_count; i++) {
            ToolCall *tool = &tool_calls_array[i];
            if (tool->name && tool->id) {
                valid_tool_calls++;
            }
        }

        if (valid_tool_calls > 0) {
            // Execute tools in oneshot mode
            for (int i = 0; i < tool_count; i++) {
                ToolCall *tool = &tool_calls_array[i];
                if (!tool->name || !tool->id) {
                    continue;
                }

                LOG_DEBUG("Executing tool: %s", tool->name);

                // Convert ToolCall to execute_tool parameters
                cJSON *input = tool->parameters
                    ? cJSON_Duplicate(tool->parameters, /*recurse*/1)
                    : cJSON_CreateObject();

                // Get tool details for display (includes special handling for Subagent params)
                char *tool_details = get_tool_details(tool->name, input);
                // get_tool_details returns pointer to static buffer, don't free it

                // Execute tool synchronously
                cJSON *tool_result = execute_tool(tool->name, input, state);

                // Print tool output based on format
                if (output_format == ONESHOT_FORMAT_MACHINE) {
                    oneshot_print_machine_format(tool->name, tool_details, tool_result);
                } else {
                    oneshot_print_human_format(tool->name, tool_details, tool_result);
                }

                // Convert result to InternalContent
                results[i].type = INTERNAL_TOOL_RESPONSE;
                results[i].tool_id = strdup(tool->id);
                results[i].tool_name = strdup(tool->name);
                results[i].tool_output = tool_result;
                results[i].is_error = tool_result ? cJSON_HasObjectItem(tool_result, "error") : 1;

                cJSON_Delete(input);
            }

            // Log summary of all tool results before adding to conversation
            LOG_DEBUG("Single-command mode: Collected %d tool results", tool_count);
            for (int i = 0; i < tool_count; i++) {
                LOG_DEBUG("Result[%d]: tool_id=%s, tool_name=%s, is_error=%d",
                          i, results[i].tool_id ? results[i].tool_id : "NULL",
                          results[i].tool_name ? results[i].tool_name : "NULL",
                          results[i].is_error);
            }

            // Add tool results to conversation
            // Note: add_tool_results takes ownership of the results array and its contents
            if (add_tool_results(state, results, tool_count) != 0) {
                LOG_ERROR("Failed to add tool results to conversation - cannot proceed");
                // Results were already freed by add_tool_results, don't free again
                return 1;
            }

            // Call API again with tool results and process recursively
            ApiResponse *next_response = call_api_with_retries(state);
            if (next_response) {
                // Recursively process the next response (may contain more tool calls)
                int result = oneshot_process_response(state, next_response, output_format);
                api_response_free(next_response);
                return result;
            } else {
                LOG_ERROR("Failed to get response after tool execution");
                fprintf(stderr, "Error: Failed to get response after tool execution\\n");
                return 1;
            }

            // Do NOT free results here - add_tool_results() took ownership
        } else {
            // No valid tool calls, free the allocated results array
            free(results);
        }
    }

    // No tool calls - conversation is complete
    return 0;
}
