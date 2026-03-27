/*
 * Oneshot Response Processor
 *
 * Processes API responses in oneshot mode, handling tool execution
 * and recursive response processing until completion.
 */

#include "oneshot_processor.h"
#include "oneshot_output.h"
#include "../conversation/conversation_processor.h"
#include "../logger.h"
#include "../ui/tool_output_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Callback context for oneshot mode
typedef struct {
    int output_format;
    const char *current_tool_name;
    const char *current_tool_details;
} OneshotCallbackContext;

// Callback implementations for unified processor
static void oneshot_on_assistant_text(const char *text, const char *reasoning_content, void *user_data) {
    (void)user_data;
    (void)reasoning_content;  // Reasoning content handled by main text callback in oneshot
    // Skip whitespace-only content
    const char *p = text;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    if (*p != '\0') {  // Has non-whitespace content
        printf("%s\n", p);
        fflush(stdout);
    }
}

static void oneshot_on_tool_start(const char *tool_name, const char *tool_details, void *user_data) {
    OneshotCallbackContext *ctx = (OneshotCallbackContext *)user_data;
    ctx->current_tool_name = tool_name;
    ctx->current_tool_details = tool_details;
    LOG_DEBUG("Oneshot: Starting tool: %s", tool_name);
}

static void oneshot_on_tool_complete(const char *tool_name, cJSON *result, int is_error, void *user_data) {
    (void)tool_name;
    (void)is_error;
    OneshotCallbackContext *ctx = (OneshotCallbackContext *)user_data;

    // Print tool output based on format
    if (ctx->output_format == ONESHOT_FORMAT_MACHINE) {
        oneshot_print_machine_format(ctx->current_tool_name, ctx->current_tool_details, result);
    } else {
        oneshot_print_human_format(ctx->current_tool_name, ctx->current_tool_details, result);
    }

    // Clear context
    ctx->current_tool_name = NULL;
    ctx->current_tool_details = NULL;
}

static void oneshot_on_error(const char *error_message, void *user_data) {
    (void)user_data;
    LOG_ERROR("Oneshot: %s", error_message);
    fprintf(stderr, "Error: %s\n", error_message);
}

/**
 * Process a single API response in oneshot mode
 * Uses the unified conversation processor for tool execution
 */
int oneshot_process_response(ConversationState *state,
                              ApiResponse *response,
                              int output_format) {
    if (!state || !response) {
        LOG_ERROR("oneshot_process_response: NULL state or response");
        return 1;
    }

    // Set up callback context
    OneshotCallbackContext cb_ctx = {
        .output_format = output_format,
        .current_tool_name = NULL,
        .current_tool_details = NULL
    };

    // Set up processing context
    ProcessingContext proc_ctx = {0};
    processing_context_init(&proc_ctx);
    proc_ctx.execution_mode = EXEC_MODE_SERIAL;  // Oneshot uses serial execution
    proc_ctx.output_format = (output_format == ONESHOT_FORMAT_MACHINE) ? OUTPUT_FORMAT_MACHINE : OUTPUT_FORMAT_PLAIN;
    proc_ctx.user_data = &cb_ctx;
    proc_ctx.on_assistant_text = oneshot_on_assistant_text;
    proc_ctx.on_tool_start = oneshot_on_tool_start;
    proc_ctx.on_tool_complete = oneshot_on_tool_complete;
    proc_ctx.on_error = oneshot_on_error;

    // Process the response using unified processor
    int result = process_response_unified(state, response, &proc_ctx);

    return result;
}
