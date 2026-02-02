/*
 * conversation_processor.c - Unified conversation processing implementation
 */

#include "conversation_processor.h"
#include "message_builder.h"
#include "message_parser.h"
#include "../klawed_internal.h"
#include "../api/api_client.h"
#include "../api/api_response.h"
#include "../tools/tool_executor.h"
#include "../tools/tool_registry.h"
#include "../logger.h"
#include "../arena.h"

// Include for get_tool_details
#include "../ui/tool_output_display.h"

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <bsd/string.h>

// ============================================================================
// Tool Execution Structures
// ============================================================================

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int total;
    int completed;
    int error_count;
    int cancelled;
    const ProcessingContext *ctx;
} ToolTracker;

typedef struct {
    char *tool_use_id;
    char *tool_name;
    cJSON *input;
    struct ConversationState *state;
    InternalContent *result_block;
    ToolTracker *tracker;
    int notified;
    Arena *arena;
} ThreadToolArg;

#define TOOL_THREAD_ARENA_SIZE 512

// ============================================================================
// Static Helper Functions
// ============================================================================

static char *arena_strdup(Arena *arena, const char *src) {
    if (!arena || !src) return NULL;
    size_t len = strlen(src) + 1;
    char *dst = arena_alloc(arena, len);
    if (dst) {
        memcpy(dst, src, len);
    }
    return dst;
}

static int tool_tracker_init(ToolTracker *tracker, int total, const ProcessingContext *ctx) {
    if (!tracker) return -1;

    if (pthread_mutex_init(&tracker->mutex, NULL) != 0) return -1;
    if (pthread_cond_init(&tracker->cond, NULL) != 0) {
        pthread_mutex_destroy(&tracker->mutex);
        return -1;
    }

    tracker->total = total;
    tracker->completed = 0;
    tracker->error_count = 0;
    tracker->cancelled = 0;
    tracker->ctx = ctx;
    return 0;
}

static void tool_tracker_destroy(ToolTracker *tracker) {
    if (!tracker) return;
    pthread_cond_destroy(&tracker->cond);
    pthread_mutex_destroy(&tracker->mutex);
}

static void tool_tracker_notify_completion(ThreadToolArg *arg) {
    if (!arg || !arg->tracker) return;

    ToolTracker *tracker = arg->tracker;

    pthread_mutex_lock(&tracker->mutex);
    if (!arg->notified) {
        arg->notified = 1;
        tracker->completed++;
        if (arg->result_block && arg->result_block->is_error) {
            tracker->error_count++;
        }
        pthread_cond_broadcast(&tracker->cond);
    }
    pthread_mutex_unlock(&tracker->mutex);
}

static void tool_thread_cleanup(void *arg) {
    ThreadToolArg *t = (ThreadToolArg *)arg;

    if (t->input) {
        cJSON_Delete(t->input);
        t->input = NULL;
    }

    pthread_mutex_t *mutex = t->tracker ? &t->tracker->mutex : NULL;
    if (mutex) pthread_mutex_lock(mutex);
    int should_write_result = !t->notified;
    if (mutex) pthread_mutex_unlock(mutex);

    if (should_write_result && t->result_block) {
        t->result_block->type = INTERNAL_TOOL_RESPONSE;
        t->result_block->tool_id = strdup(t->tool_use_id);
        t->result_block->tool_name = strdup(t->tool_name);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Tool execution cancelled by user");
        t->result_block->tool_output = error;
        t->result_block->is_error = 1;
    }

    tool_tracker_notify_completion(t);
    arena_destroy(t->arena);
}

static void *tool_thread_func(void *arg) {
    ThreadToolArg *t = (ThreadToolArg *)arg;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_cleanup_push(tool_thread_cleanup, arg);

    cJSON *res = execute_tool(t->tool_name, t->input, t->state);

    cJSON_Delete(t->input);
    t->input = NULL;

    // Check for image result
    const cJSON *content_type = cJSON_GetObjectItem(res, "content_type");
    if (content_type && cJSON_IsString(content_type) &&
        strcmp(content_type->valuestring, "image") == 0) {
        t->result_block->type = INTERNAL_IMAGE;
        t->result_block->image_path = strdup(cJSON_GetObjectItem(res, "file_path")->valuestring);
        t->result_block->mime_type = strdup(cJSON_GetObjectItem(res, "mime_type")->valuestring);
        t->result_block->base64_data = strdup(cJSON_GetObjectItem(res, "base64_data")->valuestring);
        t->result_block->image_size = (size_t)cJSON_GetObjectItem(res, "file_size_bytes")->valueint;
        t->result_block->is_error = 0;
        cJSON_Delete(res);
    } else {
        t->result_block->type = INTERNAL_TOOL_RESPONSE;
        t->result_block->tool_id = strdup(t->tool_use_id);
        t->result_block->tool_name = strdup(t->tool_name);
        t->result_block->tool_output = res;
        t->result_block->is_error = cJSON_HasObjectItem(res, "error");
    }

    // Call the completion callback if provided (prefer extended callback)
    if (t->tracker && t->tracker->ctx) {
        if (t->tracker->ctx->on_tool_complete_ex) {
            t->tracker->ctx->on_tool_complete_ex(t->tool_use_id, t->tool_name, res,
                                                 t->result_block->is_error, t->tracker->ctx->user_data);
        } else if (t->tracker->ctx->on_tool_complete) {
            t->tracker->ctx->on_tool_complete(t->tool_name, res, t->result_block->is_error, t->tracker->ctx->user_data);
        }
    }

    tool_tracker_notify_completion(t);
    arena_destroy(t->arena);

    pthread_cleanup_pop(0);
    return NULL;
}

// ============================================================================
// Tool Description Helper
// ============================================================================

const char* get_tool_description(const char *tool_name, cJSON *input) {
    // This delegates to the existing get_tool_details function in tool_registry
    // get_tool_details returns char* but we treat it as const since it's a static buffer
    return (const char *)get_tool_details(tool_name, input);
}

// ============================================================================
// Serial Tool Execution
// ============================================================================

static int execute_tools_serial(struct ConversationState *state,
                                 ToolCall *tools,
                                 int tool_count,
                                 const ProcessingContext *ctx,
                                 InternalContent **results_out) {
    if (!state || !tools || tool_count <= 0 || !results_out) {
        return -1;
    }

    InternalContent *results = calloc((size_t)tool_count, sizeof(InternalContent));
    if (!results) {
        LOG_ERROR("Failed to allocate tool results array");
        return -1;
    }

    for (int i = 0; i < tool_count; i++) {
        ToolCall *tool = &tools[i];
        InternalContent *result_slot = &results[i];
        result_slot->type = INTERNAL_TOOL_RESPONSE;

        // Check for interrupt
        if (ctx->should_interrupt && ctx->should_interrupt(ctx->user_data)) {
            LOG_INFO("Tool execution interrupted before starting tool %d", i);
            for (int k = i; k < tool_count; k++) {
                ToolCall *tcancel = &tools[k];
                InternalContent *slot = &results[k];
                slot->type = INTERNAL_TOOL_RESPONSE;
                slot->tool_id = tcancel->id ? strdup(tcancel->id) : strdup("unknown");
                slot->tool_name = tcancel->name ? strdup(tcancel->name) : strdup("tool");
                cJSON *err = cJSON_CreateObject();
                cJSON_AddStringToObject(err, "error", "Tool execution cancelled before start");
                slot->tool_output = err;
                slot->is_error = 1;
            }
            break;
        }

        if (!tool->name || !tool->id) {
            result_slot->tool_id = tool->id ? strdup(tool->id) : strdup("unknown");
            result_slot->tool_name = tool->name ? strdup(tool->name) : strdup("tool");
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Tool call missing name or id");
            result_slot->tool_output = error;
            result_slot->is_error = 1;
            continue;
        }

        // Validate tool is allowed
        if (!is_tool_allowed(tool->name, state)) {
            result_slot->tool_id = strdup(tool->id);
            result_slot->tool_name = strdup(tool->name);
            cJSON *error = cJSON_CreateObject();
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg),
                     "ERROR: Tool '%s' does not exist or was not provided to you.",
                     tool->name);
            cJSON_AddStringToObject(error, "error", error_msg);
            result_slot->tool_output = error;
            result_slot->is_error = 1;

            if (ctx->on_error) {
                ctx->on_error(error_msg, ctx->user_data);
            }
            continue;
        }

        // Prepare input
        cJSON *input = tool->parameters
            ? cJSON_Duplicate(tool->parameters, 1)
            : cJSON_CreateObject();

        // Get tool details for display
        const char *tool_details = get_tool_description(tool->name, input);

        // Notify start (prefer extended callback if available)
        if (ctx->on_tool_start_ex) {
            ctx->on_tool_start_ex(tool->id, tool->name, tool->parameters, tool_details, ctx->user_data);
        } else if (ctx->on_tool_start) {
            ctx->on_tool_start(tool->name, tool_details, ctx->user_data);
        }

        // Execute
        cJSON *tool_result = execute_tool(tool->name, input, state);

        // Notify complete (prefer extended callback if available)
        int is_err = tool_result ? cJSON_HasObjectItem(tool_result, "error") : 1;
        if (ctx->on_tool_complete_ex) {
            ctx->on_tool_complete_ex(tool->id, tool->name, tool_result, is_err, ctx->user_data);
        } else if (ctx->on_tool_complete) {
            ctx->on_tool_complete(tool->name, tool_result, is_err, ctx->user_data);
        }

        // Store result
        result_slot->tool_id = strdup(tool->id);
        result_slot->tool_name = strdup(tool->name);
        result_slot->tool_output = tool_result;
        result_slot->is_error = tool_result ? cJSON_HasObjectItem(tool_result, "error") : 1;

        cJSON_Delete(input);
    }

    *results_out = results;
    return tool_count;
}

// ============================================================================
// Parallel Tool Execution
// ============================================================================

static int execute_tools_parallel(struct ConversationState *state,
                                   ToolCall *tools,
                                   int tool_count,
                                   const ProcessingContext *ctx,
                                   InternalContent **results_out) {
    if (!state || !tools || tool_count <= 0 || !results_out) {
        return -1;
    }

    InternalContent *results = calloc((size_t)tool_count, sizeof(InternalContent));
    if (!results) {
        LOG_ERROR("Failed to allocate tool results array");
        return -1;
    }

    pthread_t *threads = calloc((size_t)tool_count, sizeof(pthread_t));
    if (!threads) {
        free(results);
        return -1;
    }

    ToolTracker tracker;
    if (tool_tracker_init(&tracker, tool_count, ctx) != 0) {
        free(threads);
        free(results);
        return -1;
    }

    int started_threads = 0;
    int interrupted = 0;

    for (int i = 0; i < tool_count; i++) {
        if (ctx->should_interrupt && ctx->should_interrupt(ctx->user_data)) {
            interrupted = 1;
            for (int k = i; k < tool_count; k++) {
                ToolCall *tcancel = &tools[k];
                InternalContent *slot = &results[k];
                slot->type = INTERNAL_TOOL_RESPONSE;
                slot->tool_id = tcancel->id ? strdup(tcancel->id) : strdup("unknown");
                slot->tool_name = tcancel->name ? strdup(tcancel->name) : strdup("tool");
                cJSON *err = cJSON_CreateObject();
                cJSON_AddStringToObject(err, "error", "Tool execution cancelled before start");
                slot->tool_output = err;
                slot->is_error = 1;
            }
            break;
        }

        ToolCall *tool = &tools[i];
        InternalContent *result_slot = &results[i];
        result_slot->type = INTERNAL_TOOL_RESPONSE;

        if (!tool->name || !tool->id) {
            result_slot->tool_id = tool->id ? strdup(tool->id) : strdup("unknown");
            result_slot->tool_name = tool->name ? strdup(tool->name) : strdup("tool");
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Tool call missing name or id");
            result_slot->tool_output = error;
            result_slot->is_error = 1;
            continue;
        }

        if (!is_tool_allowed(tool->name, state)) {
            result_slot->tool_id = strdup(tool->id);
            result_slot->tool_name = strdup(tool->name);
            cJSON *error = cJSON_CreateObject();
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg),
                     "ERROR: Tool '%s' does not exist or was not provided to you.",
                     tool->name);
            cJSON_AddStringToObject(error, "error", error_msg);
            result_slot->tool_output = error;
            result_slot->is_error = 1;

            if (ctx->on_error) {
                ctx->on_error(error_msg, ctx->user_data);
            }
            continue;
        }

        cJSON *input = tool->parameters
            ? cJSON_Duplicate(tool->parameters, 1)
            : cJSON_CreateObject();

        // Notify start (prefer extended callback if available)
        const char *tool_details = get_tool_description(tool->name, input);
        if (ctx->on_tool_start_ex) {
            ctx->on_tool_start_ex(tool->id, tool->name, tool->parameters, tool_details, ctx->user_data);
        } else if (ctx->on_tool_start) {
            ctx->on_tool_start(tool->name, tool_details, ctx->user_data);
        }

        // Create arena and thread arg
        Arena *thread_arena = arena_create(TOOL_THREAD_ARENA_SIZE);
        if (!thread_arena) {
            cJSON_Delete(input);
            result_slot->tool_id = strdup(tool->id);
            result_slot->tool_name = strdup(tool->name);
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Failed to allocate memory for tool thread");
            result_slot->tool_output = error;
            result_slot->is_error = 1;
            continue;
        }

        ThreadToolArg *thread_arg = arena_alloc(thread_arena, sizeof(ThreadToolArg));
        if (!thread_arg) {
            arena_destroy(thread_arena);
            cJSON_Delete(input);
            result_slot->tool_id = strdup(tool->id);
            result_slot->tool_name = strdup(tool->name);
            cJSON_AddStringToObject(result_slot->tool_output = cJSON_CreateObject(), "error", "Memory allocation failed");
            result_slot->is_error = 1;
            continue;
        }

        thread_arg->tool_use_id = arena_strdup(thread_arena, tool->id);
        thread_arg->tool_name = arena_strdup(thread_arena, tool->name);
        thread_arg->input = input;
        thread_arg->state = state;
        thread_arg->result_block = result_slot;
        thread_arg->tracker = &tracker;
        thread_arg->notified = 0;
        thread_arg->arena = thread_arena;

        if (pthread_create(&threads[started_threads], NULL, tool_thread_func, thread_arg) != 0) {
            LOG_ERROR("Failed to create tool thread for %s", tool->name);
            for (int cancel_idx = 0; cancel_idx < started_threads; cancel_idx++) {
                pthread_cancel(threads[cancel_idx]);
            }
            arena_destroy(thread_arena);
            result_slot->tool_id = strdup(tool->id);
            result_slot->tool_name = strdup(tool->name);
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Failed to start tool thread");
            result_slot->tool_output = error;
            result_slot->is_error = 1;
            continue;
        }

        started_threads++;
    }

    // Wait for completion
    while (started_threads > 0) {
        if (ctx->should_interrupt && ctx->should_interrupt(ctx->user_data)) {
            interrupted = 1;
            pthread_mutex_lock(&tracker.mutex);
            tracker.cancelled = 1;
            pthread_cond_broadcast(&tracker.cond);
            pthread_mutex_unlock(&tracker.mutex);

            for (int t = 0; t < started_threads; t++) {
                pthread_cancel(threads[t]);
            }
            break;
        }

        pthread_mutex_lock(&tracker.mutex);
        if (tracker.cancelled || tracker.completed >= started_threads) {
            pthread_mutex_unlock(&tracker.mutex);
            break;
        }

        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += 100000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec += 1;
            deadline.tv_nsec -= 1000000000L;
        }

        pthread_cond_timedwait(&tracker.cond, &tracker.mutex, &deadline);
        pthread_mutex_unlock(&tracker.mutex);
    }

    // Join threads
    if (interrupted) {
        for (int t = 0; t < started_threads; t++) {
            pthread_join(threads[t], NULL);
        }
    } else {
        for (int t = 0; t < started_threads; t++) {
            pthread_join(threads[t], NULL);
        }
    }

    tool_tracker_destroy(&tracker);
    free(threads);

    *results_out = results;
    return tool_count;
}

// ============================================================================
// Public API Implementation
// ============================================================================

void processing_context_init(ProcessingContext *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(ProcessingContext));
    ctx->execution_mode = EXEC_MODE_SERIAL;
    ctx->output_format = OUTPUT_FORMAT_PLAIN;
    ctx->max_iterations = 100;  // Default limit
}

int execute_tools_collect_results(struct ConversationState *state,
                                   ToolCall *tools,
                                   int tool_count,
                                   const ProcessingContext *ctx,
                                   InternalContent **results_out) {
    if (!ctx) return -1;

    if (ctx->execution_mode == EXEC_MODE_PARALLEL) {
        return execute_tools_parallel(state, tools, tool_count, ctx, results_out);
    } else {
        return execute_tools_serial(state, tools, tool_count, ctx, results_out);
    }
}

int process_response_unified(struct ConversationState *state,
                              ApiResponse *response,
                              const ProcessingContext *ctx) {
    if (!state || !response || !ctx) {
        LOG_ERROR("process_response_unified: NULL argument");
        return -1;
    }

    // Handle error responses
    if (response->error_message) {
        LOG_DEBUG("process_response_unified: Error response: %s", response->error_message);
        if (ctx->on_error) {
            ctx->on_error(response->error_message, ctx->user_data);
        }
        return -1;
    }

    int iteration = 0;
    int max_iterations = ctx->max_iterations > 0 ? ctx->max_iterations : 1000;

    // Process the current response and any follow-ups
    ApiResponse *current_response = response;
    int own_response = 0;  // Track if we allocated the response

    while (iteration < max_iterations) {
        iteration++;

        // Check for interrupt at start of iteration
        if (ctx->should_interrupt && ctx->should_interrupt(ctx->user_data)) {
            LOG_INFO("Processing interrupted at iteration %d", iteration);
            break;
        }

        // Display assistant text
        if (current_response->message.text && current_response->message.text[0] != '\0') {
            const char *p = current_response->message.text;
            while (*p && isspace((unsigned char)*p)) p++;

            if (*p != '\0' && ctx->on_assistant_text) {
                ctx->on_assistant_text(p, ctx->user_data);
            }
        }

        // Add to conversation history
        if (current_response->raw_response) {
            cJSON *choices = cJSON_GetObjectItem(current_response->raw_response, "choices");
            if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                cJSON *choice = cJSON_GetArrayItem(choices, 0);

                // Check for token limit warning
                cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
                if (finish_reason && cJSON_IsString(finish_reason) &&
                    finish_reason->valuestring &&
                    strcmp(finish_reason->valuestring, "length") == 0) {
                    LOG_WARN("API response stopped due to token limit");
                }

                cJSON *message = cJSON_GetObjectItem(choice, "message");
                if (message) {
                    add_assistant_message_openai(state, message);
                }
            }
        }

        // Process tool calls
        int tool_count = current_response->tool_count;
        ToolCall *tools = current_response->tools;

        if (tool_count > 0 && tools) {
            LOG_INFO("Processing %d tool call(s) in iteration %d", tool_count, iteration);

            // Execute tools
            InternalContent *results = NULL;
            int exec_result = execute_tools_collect_results(state, tools, tool_count, ctx, &results);

            if (exec_result < 0 || !results) {
                LOG_ERROR("Tool execution failed");
                if (ctx->on_error) {
                    ctx->on_error("Tool execution failed", ctx->user_data);
                }
                if (own_response) {
                    api_response_free(current_response);
                }
                return -1;
            }

            // Add results to conversation
            if (add_tool_results(state, results, tool_count) != 0) {
                LOG_ERROR("Failed to add tool results to conversation");
                // Results freed by add_tool_results
                if (own_response) {
                    api_response_free(current_response);
                }
                return -1;
            }

            // Call API again with tool results
            if (ctx->on_status_update) {
                ctx->on_status_update("Processing tool results...", ctx->user_data);
            }

            ApiResponse *next_response = call_api_with_retries(state);

            // Free previous response if we own it
            if (own_response) {
                api_response_free(current_response);
            }

            if (!next_response) {
                LOG_ERROR("API call failed after tool execution");
                if (ctx->on_error) {
                    ctx->on_error("API call failed after tool execution", ctx->user_data);
                }
                return -1;
            }

            // Check for errors in next response
            if (next_response->error_message) {
                if (ctx->on_error) {
                    ctx->on_error(next_response->error_message, ctx->user_data);
                }
                api_response_free(next_response);
                return -1;
            }

            current_response = next_response;
            own_response = 1;
            continue;  // Process the new response
        }

        // No tools - we're done
        break;
    }

    if (iteration >= max_iterations) {
        LOG_WARN("Reached maximum iterations (%d)", max_iterations);
        if (ctx->on_error) {
            ctx->on_error("Maximum iteration limit reached", ctx->user_data);
        }
        if (own_response) {
            api_response_free(current_response);
        }
        return -1;
    }

    // Clean up if we allocated the response
    if (own_response) {
        api_response_free(current_response);
    }

    return 0;
}

int process_user_instruction(struct ConversationState *state,
                              const char *user_input,
                              const ProcessingContext *ctx) {
    if (!state || !user_input || !ctx) {
        return -1;
    }

    // Add user message
    add_user_message(state, user_input);

    // Call API
    if (ctx->on_status_update) {
        ctx->on_status_update("Calling AI...", ctx->user_data);
    }

    ApiResponse *response = call_api_with_retries(state);
    if (!response) {
        LOG_ERROR("API call failed");
        if (ctx->on_error) {
            ctx->on_error("Failed to get response from API", ctx->user_data);
        }
        return -1;
    }

    if (response->error_message) {
        if (ctx->on_error) {
            ctx->on_error(response->error_message, ctx->user_data);
        }
        api_response_free(response);
        return -1;
    }

    // Process response
    int result = process_response_unified(state, response, ctx);
    api_response_free(response);

    return result;
}
