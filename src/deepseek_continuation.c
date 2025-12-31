/*
 * deepseek_continuation.c - DeepSeek API continuation handling
 * 
 * Handles making continuation API calls for incomplete Write tool payloads
 * when the DeepSeek API hits token limits (finish_reason: "length").
 */

#define _POSIX_C_SOURCE 200809L

#include "deepseek_continuation.h"
#include "klawed_internal.h"
#include "logger.h"
#include "openai_messages.h"
#include "json_repair.h"
#include <string.h>
#include <stdlib.h>
#include <bsd/string.h>

/**
 * Make a continuation API call for incomplete Write tool arguments
 * 
 * This function:
 * 1. Adds the continuation prompt as a user message
 * 2. Makes an API call
 * 3. Parses the continuation response
 * 4. Merges it with the original incomplete arguments
 * 
 * @param state The conversation state
 * @param tool The incomplete Write tool call
 * @param continuation_prompt The prompt asking for continuation
 * @return 1 on success, 0 on failure
 */
int deepseek_make_continuation_call(ConversationState *state, ToolCall *tool, const char *continuation_prompt) {
    if (!state || !tool || !continuation_prompt) {
        LOG_ERROR("deepseek_make_continuation_call: invalid parameters");
        return 0;
    }
    
    LOG_INFO("Making continuation API call for incomplete Write tool");
    
    // Save current conversation state to restore later
    int original_message_count = state->count;
    
    // Add continuation prompt as a user message
    add_user_message(state, continuation_prompt);
    
    // Make API call
    ApiResponse *continuation_response = call_api_with_retries(state);
    if (!continuation_response) {
        LOG_ERROR("Failed to get continuation response");
        // Remove the continuation prompt we added
        if (state->count > original_message_count) {
            state->count = original_message_count;
        }
        return 0;
    }
    
    // Check if we got text content in the continuation response
    if (!continuation_response->message.text || !continuation_response->message.text[0]) {
        LOG_ERROR("Continuation response has no text content");
        api_response_free(continuation_response);
        // Remove the continuation prompt we added
        if (state->count > original_message_count) {
            state->count = original_message_count;
        }
        return 0;
    }
    
    LOG_DEBUG("Continuation response text: %s", continuation_response->message.text);
    
    // Now we need to merge the continuation with the original incomplete arguments
    // The continuation should be the rest of the JSON that was cut off
    char *combined_args = NULL;
    size_t incomplete_len = strlen(tool->incomplete_args);
    size_t continuation_len = strlen(continuation_response->message.text);
    
    // Allocate buffer for combined arguments
    combined_args = malloc(incomplete_len + continuation_len + 1);
    if (!combined_args) {
        LOG_ERROR("Failed to allocate memory for combined arguments");
        api_response_free(continuation_response);
        // Remove the continuation prompt we added
        if (state->count > original_message_count) {
            state->count = original_message_count;
        }
        return 0;
    }
    
    // Copy incomplete args
    memcpy(combined_args, tool->incomplete_args, incomplete_len);
    
    // Copy continuation
    memcpy(combined_args + incomplete_len, continuation_response->message.text, continuation_len);
    combined_args[incomplete_len + continuation_len] = '\0';
    
    LOG_DEBUG("Combined arguments: %s", combined_args);
    
    // Try to parse the combined JSON
    cJSON *combined_params = cJSON_Parse(combined_args);
    if (!combined_params) {
        LOG_WARN("Failed to parse combined arguments, attempting repair");
        
        // Try to repair the JSON
        char repaired[65536];  // Large buffer for Write tool content
        if (repair_truncated_json(combined_args, sizeof(repaired), repaired)) {
            LOG_DEBUG("Attempting to parse repaired JSON");
            combined_params = cJSON_Parse(repaired);
        }
        
        if (!combined_params) {
            LOG_ERROR("Could not parse combined arguments even after repair");
            free(combined_args);
            api_response_free(continuation_response);
            // Remove the continuation prompt we added
            if (state->count > original_message_count) {
                state->count = original_message_count;
            }
            return 0;
        }
    }
    
    // Success! Replace the tool's parameters with the combined ones
    if (tool->parameters) {
        cJSON_Delete(tool->parameters);
    }
    tool->parameters = combined_params;
    
    // Free the incomplete_args since we now have complete parameters
    free(tool->incomplete_args);
    tool->incomplete_args = NULL;
    
    free(combined_args);
    api_response_free(continuation_response);
    
    LOG_INFO("Successfully merged continuation with incomplete Write tool arguments");
    
    // Remove the continuation prompt from conversation history
    // We don't want it to pollute the conversation
    if (state->count > original_message_count) {
        state->count = original_message_count;
    }
    
    return 1;
}
