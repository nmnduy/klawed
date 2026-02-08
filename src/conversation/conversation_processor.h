/*
 * conversation_processor.h - Unified conversation processing for all input modes
 *
 * This module provides a unified way to process API responses and execute tools
 * across all input modes (interactive, oneshot, SQLite queue). It abstracts the
 * common patterns of:
 *   - Tool execution (serial or parallel)
 *   - Result collection and conversation updating
 *   - Recursive follow-up API calls
 *   - Progress reporting via callbacks
 */

#ifndef CONVERSATION_PROCESSOR_H
#define CONVERSATION_PROCESSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <cjson/cJSON.h>

// Include klawed_internal.h for type definitions (ApiResponse, ToolCall, InternalContent)
#include "../klawed_internal.h"

// Forward declaration
struct ConversationState;

// ============================================================================
// Execution Configuration
// ============================================================================

/**
 * Execution mode for tool processing
 */
typedef enum {
    EXEC_MODE_SERIAL,      // Execute tools one at a time
    EXEC_MODE_PARALLEL     // Execute tools in parallel (threaded)
} ToolExecutionMode;

/**
 * Output format for tool results
 */
typedef enum {
    OUTPUT_FORMAT_PLAIN,   // Human-readable plain text
    OUTPUT_FORMAT_MACHINE  // Machine-readable (HTML+JSON tags)
} ToolOutputFormat;

/**
 * Processing context configuration
 * Controls how tools are executed and how progress is reported
 */
typedef struct {
    // Execution mode
    ToolExecutionMode execution_mode;

    // Output formatting
    ToolOutputFormat output_format;

    // Maximum iterations for recursive tool processing (0 = unlimited)
    int max_iterations;

    // User data passed to all callbacks
    void *user_data;

    // =========================================================================
    // Callbacks for progress reporting (all optional)
    // =========================================================================

    /**
     * Called when a tool starts executing
     * @param tool_name Name of the tool being executed
     * @param tool_details Human-readable description of tool call
     * @param user_data Context user_data
     */
    void (*on_tool_start)(const char *tool_name, const char *tool_details, void *user_data);

    /**
     * Called when a tool completes
     * @param tool_name Name of the tool
     * @param result Tool result (JSON object)
     * @param is_error Whether the tool returned an error
     * @param user_data Context user_data
     */
    void (*on_tool_complete)(const char *tool_name, cJSON *result, int is_error, void *user_data);

    /**
     * Extended callback: Called when a tool starts executing (with full tool context)
     * @param tool_id Tool call ID for pairing with results
     * @param tool_name Name of the tool being executed
     * @param tool_parameters Tool input parameters (JSON object, may be NULL)
     * @param tool_details Human-readable description of tool call
     * @param user_data Context user_data
     * @note If set, this is called instead of on_tool_start
     */
    void (*on_tool_start_ex)(const char *tool_id, const char *tool_name, cJSON *tool_parameters,
                             const char *tool_details, void *user_data);

    /**
     * Extended callback: Called when a tool completes (with full tool context)
     * @param tool_id Tool call ID for pairing with the request
     * @param tool_name Name of the tool
     * @param result Tool result (JSON object)
     * @param is_error Whether the tool returned an error
     * @param user_data Context user_data
     * @note If set, this is called instead of on_tool_complete
     */
    void (*on_tool_complete_ex)(const char *tool_id, const char *tool_name, cJSON *result,
                                int is_error, void *user_data);

    /**
     * Called when assistant text is received
     * @param text Assistant's text response
     * @param user_data Context user_data
     */
    void (*on_assistant_text)(const char *text, void *user_data);

    /**
     * Called when an error occurs
     * @param error_message Error description
     * @param user_data Context user_data
     */
    void (*on_error)(const char *error_message, void *user_data);

    /**
     * Called to check if processing should be interrupted
     * @param user_data Context user_data
     * @return true if processing should stop, false to continue
     */
    int (*should_interrupt)(void *user_data);

    /**
     * Called for status updates during long operations
     * @param status Human-readable status message
     * @param user_data Context user_data
     */
    void (*on_status_update)(const char *status, void *user_data);

    /**
     * Called after tool results are added to conversation but before next API call.
     * This is a safe injection point for real-time steering - all tool-result pairs
     * are complete at this point.
     * @param state Conversation state (can be modified to inject messages)
     * @param user_data Context user_data
     */
    void (*on_after_tool_results)(struct ConversationState *state, void *user_data);

} ProcessingContext;

// ============================================================================
// Core Processing Functions
// ============================================================================

/**
 * Initialize a processing context with default values
 * @param ctx Context to initialize
 */
void processing_context_init(ProcessingContext *ctx);

/**
 * Process an API response, executing any tools and recursively handling
 * follow-up responses until the conversation completes or max_iterations is reached.
 *
 * This is the main entry point for unified response processing. It handles:
 *   - Displaying assistant text
 *   - Adding messages to conversation history
 *   - Executing tools (serial or parallel based on ctx->execution_mode)
 *   - Collecting results
 *   - Recursive follow-up API calls
 *
 * @param state Conversation state
 * @param response API response to process
 * @param ctx Processing context with callbacks and configuration
 * @return 0 on success, -1 on error
 */
int process_response_unified(struct ConversationState *state,
                              ApiResponse *response,
                              const ProcessingContext *ctx);

/**
 * Execute a single user instruction from start to finish.
 * This is a convenience wrapper that:
 *   1. Adds the user message to conversation
 *   2. Calls the API
 *   3. Processes the response (with tool execution)
 *   4. Continues until completion
 *
 * @param state Conversation state
 * @param user_input User's text input
 * @param ctx Processing context with callbacks and configuration
 * @return 0 on success, -1 on error
 */
int process_user_instruction(struct ConversationState *state,
                              const char *user_input,
                              const ProcessingContext *ctx);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Get a human-readable description of a tool call
 * @param tool_name Name of the tool
 * @param input Tool input parameters (JSON)
 * @return Static buffer with description (do not free, not thread-safe)
 */
const char* get_tool_description(const char *tool_name, cJSON *input);

/**
 * Execute tools and collect results (internal function, exposed for advanced use)
 *
 * @param state Conversation state
 * @param tools Array of tool calls to execute
 * @param tool_count Number of tools
 * @param ctx Processing context
 * @param results_out Output array of InternalContent results (caller must free with free_internal_contents)
 * @return Number of tools executed, or -1 on error
 */
int execute_tools_collect_results(struct ConversationState *state,
                                   ToolCall *tools,
                                   int tool_count,
                                   const ProcessingContext *ctx,
                                   InternalContent **results_out);

#endif // CONVERSATION_PROCESSOR_H
