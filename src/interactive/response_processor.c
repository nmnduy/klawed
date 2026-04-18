#include "response_processor.h"
#include "../logger.h"
#include "../model_capabilities.h"
#include "../ui/ui_output.h"
#include "../ui/print_helpers.h"
#include "../ui/tool_output_display.h"
#include "../conversation/message_builder.h"
#include "../conversation/message_parser.h"
#include "../conversation/conversation_processor.h"
#include "../tools/tool_executor.h"
#include "../tools/tool_registry.h"
#include "../api/api_client.h"
#include "../todo.h"
#include "../subagent_manager.h"
#include "../arena.h"
#include "../indicators.h"
#include "../window_manager.h"
#include "../compaction.h"
#include "../sqlite_queue.h"
#include "../tui.h"
#include "../perpetual/perpetual_mode.h"
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

// Thread-local variable for tool queue (used by tool execution)
extern _Thread_local TUIMessageQueue *g_active_tool_queue;

// Type Definitions for Tool Execution
// ============================================================================

typedef void (*ToolCompletionCallback)(const ToolCompletion *completion, void *user_data);

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int total;
    int completed;
    int error_count;
    int cancelled;
    ToolCompletionCallback callback;
    void *callback_user_data;
} ToolExecutionTracker;

typedef struct {
    TUIState *tui;
    TUIMessageQueue *queue;
    Spinner *spinner;
    AIWorkerContext *worker_ctx;
} ToolCallbackContext;

// Tool thread argument structure using per-thread arena allocation.
// Each thread owns its arena and all data allocated from it.
// When the thread finishes (normally or via cancellation), arena_destroy()
// frees everything in one shot - no reference counting needed.
typedef struct {
    char *tool_use_id;            // allocated from arena
    char *tool_name;              // allocated from arena
    cJSON *input;                 // NOT arena-managed (uses cJSON allocator)
    ConversationState *state;     // shared pointer (not owned)
    InternalContent *result_block;   // shared pointer (not owned)
    ToolExecutionTracker *tracker;  // shared pointer (not owned)
    int notified;                  // guard against double notification
    TUIMessageQueue *queue;        // shared pointer (not owned)
    Arena *arena;                 // arena that owns this arg and its strings
} ToolThreadArg;

// Arena size for per-thread allocation
// Holds: ToolThreadArg (~80 bytes) + tool_use_id (~50 bytes) + tool_name (~30 bytes)
// 512 bytes provides comfortable headroom
#define TOOL_THREAD_ARENA_SIZE 512
#define INTERACTIVE_TOOL_LOOP_MAX_ITERATIONS 10000

// Forward declarations
static char *arena_strdup(Arena *arena, const char *src);
static int tool_tracker_init(ToolExecutionTracker *tracker, int total, ToolCompletionCallback callback, void *user_data);
static void tool_tracker_destroy(ToolExecutionTracker *tracker);
static void tool_tracker_notify_completion(ToolThreadArg *arg);
static void tool_progress_callback(const ToolCompletion *completion, void *user_data);
static void tool_thread_cleanup(void *arg);
static void *tool_thread_func(void *arg);

// Helper Functions for Tool Execution
// ============================================================================

// Duplicate a string into an arena
static char *arena_strdup(Arena *arena, const char *src) {
    if (!arena || !src) return NULL;
    size_t len = strlen(src) + 1;
    char *dst = arena_alloc(arena, len);
    if (dst) {
        memcpy(dst, src, len);
    }
    return dst;
}

static int tool_tracker_init(ToolExecutionTracker *tracker,
                             int total,
                             ToolCompletionCallback callback,
                             void *user_data) {
    if (!tracker) {
        return -1;
    }

    if (pthread_mutex_init(&tracker->mutex, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&tracker->cond, NULL) != 0) {
        pthread_mutex_destroy(&tracker->mutex);
        return -1;
    }

    tracker->total = total;
    tracker->completed = 0;
    tracker->error_count = 0;
    tracker->cancelled = 0;
    tracker->callback = callback;
    tracker->callback_user_data = user_data;
    return 0;
}

static void tool_tracker_destroy(ToolExecutionTracker *tracker) {
    if (!tracker) {
        return;
    }
    pthread_cond_destroy(&tracker->cond);
    pthread_mutex_destroy(&tracker->mutex);
}

static void tool_tracker_notify_completion(ToolThreadArg *arg) {
    if (!arg || !arg->tracker) {
        return;
    }

    ToolExecutionTracker *tracker = arg->tracker;
    ToolCompletion completion = {0};
    ToolCompletionCallback cb = NULL;
    void *cb_data = NULL;

    pthread_mutex_lock(&tracker->mutex);
    if (!arg->notified) {
        arg->notified = 1;
        tracker->completed++;
        if (arg->result_block && arg->result_block->is_error) {
            tracker->error_count++;
        }

        completion.tool_name = arg->tool_name;
        completion.result = arg->result_block ? arg->result_block->tool_output : NULL;
        completion.is_error = arg->result_block ? arg->result_block->is_error : 1;
        completion.completed = tracker->completed;
        completion.total = tracker->total;
        cb = tracker->callback;
        cb_data = tracker->callback_user_data;

        pthread_cond_broadcast(&tracker->cond);
    }
    pthread_mutex_unlock(&tracker->mutex);

    if (cb) {
        cb(&completion, cb_data);
    }
}

static void tool_progress_callback(const ToolCompletion *completion, void *user_data) {
    if (!completion || !user_data) {
        return;
    }

    ToolCallbackContext *ctx = (ToolCallbackContext *)user_data;
    const char *tool_name = completion->tool_name ? completion->tool_name : "tool";
    const char *status_word = completion->is_error ? "failed" : "completed";

    char status[256];
    if (completion->total > 0) {
        snprintf(status, sizeof(status),
                 "Tool %s %s (%d/%d)",
                 tool_name,
                 status_word,
                 completion->completed,
                 completion->total);
    } else {
        snprintf(status, sizeof(status),
                 "Tool %s %s",
                 tool_name,
                 status_word);
    }

    if (ctx->spinner) {
        spinner_update(ctx->spinner, status);
    }

    if (ctx->worker_ctx) {
        ai_worker_handle_tool_completion(ctx->worker_ctx, completion);
    } else {
        ui_set_status(ctx->tui, ctx->queue, status);
    }
}

// Cleanup handler for tool thread cancellation
static void tool_thread_cleanup(void *arg) {
    ToolThreadArg *t = (ToolThreadArg *)arg;

    // Free input JSON if not already freed (cJSON uses its own allocator, not arena)
    if (t->input) {
        cJSON_Delete(t->input);
        t->input = NULL;
    }

    // CRITICAL FIX: Check if we've already written results under mutex
    // to prevent data race with normal completion path
    pthread_mutex_t *mutex = t->tracker ? &t->tracker->mutex : NULL;
    if (mutex) {
        pthread_mutex_lock(mutex);
    }

    int should_write_result = !t->notified;

    if (mutex) {
        pthread_mutex_unlock(mutex);
    }

    // Only write cancellation result if not already completed
    if (should_write_result && t->result_block) {
        t->result_block->type = INTERNAL_TOOL_RESPONSE;
        // Must strdup because result_block outlives our arena
        t->result_block->tool_id = strdup(t->tool_use_id);
        t->result_block->tool_name = strdup(t->tool_name);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Tool execution cancelled by user");
        t->result_block->tool_output = error;
        t->result_block->is_error = 1;
    }

    tool_tracker_notify_completion(t);

    // Destroy the arena - frees the ToolThreadArg and all its arena-allocated strings
    // This is safe even for detached threads because each thread owns its own arena
    arena_destroy(t->arena);
}

static void *tool_thread_func(void *arg) {
    ToolThreadArg *t = (ToolThreadArg *)arg;

    // Enable thread cancellation
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    // Register cleanup handler
    pthread_cleanup_push(tool_thread_cleanup, arg);

    // Execute the tool
    TUIMessageQueue *previous_queue = g_active_tool_queue;
    g_active_tool_queue = t->queue;
    cJSON *res = execute_tool(t->tool_name, t->input, t->state);
    g_active_tool_queue = previous_queue;
    // Free input JSON
    cJSON_Delete(t->input);
    t->input = NULL;  // Mark as freed for cleanup handler
    // Populate result block
    // Check if this is an image upload result
    const cJSON *content_type = cJSON_GetObjectItem(res, "content_type");
    if (content_type && cJSON_IsString(content_type) && strcmp(content_type->valuestring, "image") == 0) {
        // This is an image upload - create image content instead of tool response
        t->result_block->type = INTERNAL_IMAGE;
        // Must set tool_id so ensure_tool_results can match this to the tool call
        t->result_block->tool_id = strdup(t->tool_use_id);
        t->result_block->tool_name = strdup(t->tool_name);
        t->result_block->image_path = strdup(cJSON_GetObjectItem(res, "file_path")->valuestring);
        t->result_block->mime_type = strdup(cJSON_GetObjectItem(res, "mime_type")->valuestring);
        t->result_block->base64_data = strdup(cJSON_GetObjectItem(res, "base64_data")->valuestring);
        t->result_block->image_size = (size_t)cJSON_GetObjectItem(res, "file_size_bytes")->valueint;
        t->result_block->is_error = 0;
        // Free the result JSON since we've extracted the data
        cJSON_Delete(res);
    } else {
        // Regular tool response
        t->result_block->type = INTERNAL_TOOL_RESPONSE;
        // Must strdup because result_block outlives our arena
        t->result_block->tool_id = strdup(t->tool_use_id);
        t->result_block->tool_name = strdup(t->tool_name);
        t->result_block->tool_output = res;
        t->result_block->is_error = cJSON_HasObjectItem(res, "error");
    }

    tool_tracker_notify_completion(t);

    // Destroy the arena on normal exit - frees ToolThreadArg and all arena-allocated strings
    arena_destroy(t->arena);

    // Pop cleanup handler (execute=0 means don't run it on normal exit)
    pthread_cleanup_pop(0);

    return NULL;
}

// API Response Processing
// ============================================================================

void process_response(ConversationState *state,
                     ApiResponse *response,
                     TUIState *tui,
                     TUIMessageQueue *queue,
                     AIWorkerContext *worker_ctx) {
    // Time the entire response processing
    struct timespec proc_start, proc_end;
    clock_gettime(CLOCK_MONOTONIC, &proc_start);
    long total_tool_exec_ms = 0;
    int iteration = 0;
    ApiResponse *current_response = response;
    int owns_current_response = 0;

    // Check for error response first - callers should display errors before calling process_response
    // but we check here as defense in depth. Error responses don't have raw_response populated.
    if (response->error_message) {
        LOG_DEBUG("process_response: Error response encountered, exiting early: %s", response->error_message);
        return;
    }

    while (current_response && iteration < INTERACTIVE_TOOL_LOOP_MAX_ITERATIONS) {
        iteration++;

        // Display assistant's text content if present
        if (!current_response->ui_streamed &&
            current_response->message.text &&
            current_response->message.text[0] != '\0') {
            // Skip whitespace-only content
            const char *p = current_response->message.text;
            while (*p && isspace((unsigned char)*p)) p++;

            if (*p != '\0') {  // Has non-whitespace content
                ui_append_line(tui, queue, "[Assistant]", p, COLOR_PAIR_ASSISTANT);
            }
        }

        // Add to conversation history (using raw response for now)
        // Extract message from raw_response for backward compatibility
        // Defense in depth: check for NULL raw_response
        if (!current_response->raw_response) {
            LOG_WARN("process_response: iteration %d has NULL raw_response, cannot add to conversation history",
                     iteration);
            break;
        }

        cJSON *choices = cJSON_GetObjectItem(current_response->raw_response, "choices");
        const char *finish_reason_str = NULL;
        cJSON *message = NULL;
        int raw_tool_call_count = 0;
        int parsed_text_present = 0;
        if (current_response->message.text) {
            const char *text = current_response->message.text;
            while (*text && isspace((unsigned char)*text)) text++;
            parsed_text_present = (*text != '\0');
        }

        if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
            cJSON *choice = cJSON_GetArrayItem(choices, 0);

            // Check for finish_reason and log WARNING if it's 'length'
            cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
            if (finish_reason && cJSON_IsString(finish_reason) && finish_reason->valuestring) {
                finish_reason_str = finish_reason->valuestring;
                if (strcmp(finish_reason->valuestring, "length") == 0) {
                    LOG_WARN("API response stopped due to token limit (finish_reason: 'length')");
                }
            }

            message = cJSON_GetObjectItem(choice, "message");
            if (message) {
                cJSON *raw_tool_calls = cJSON_GetObjectItem(message, "tool_calls");
                if (raw_tool_calls && cJSON_IsArray(raw_tool_calls)) {
                    raw_tool_call_count = cJSON_GetArraySize(raw_tool_calls);
                }
                add_assistant_message_openai(state, message);
            }
        }

        // Process tool calls from vendor-agnostic structure
        int tool_count = current_response->tool_count;
        ToolCall *tool_calls_array = current_response->tools;

        LOG_INFO("process_response: iteration=%d finish_reason=%s parsed_tools=%d raw_tool_calls=%d text_present=%d ui_streamed=%d",
                 iteration,
                 finish_reason_str ? finish_reason_str : "(null)",
                 tool_count,
                 raw_tool_call_count,
                 parsed_text_present,
                 current_response->ui_streamed);

        if (raw_tool_call_count != tool_count) {
            LOG_WARN("process_response: iteration=%d raw/parsed tool call count mismatch (raw=%d parsed=%d)",
                     iteration, raw_tool_call_count, tool_count);
        }

        if (raw_tool_call_count > 0 && message) {
            cJSON *raw_tool_calls = cJSON_GetObjectItem(message, "tool_calls");
            if (raw_tool_calls && cJSON_IsArray(raw_tool_calls)) {
                for (int i = 0; i < raw_tool_call_count; i++) {
                    cJSON *raw_tool_call = cJSON_GetArrayItem(raw_tool_calls, i);
                    cJSON *raw_id = raw_tool_call ? cJSON_GetObjectItem(raw_tool_call, "id") : NULL;
                    cJSON *raw_fn = raw_tool_call ? cJSON_GetObjectItem(raw_tool_call, "function") : NULL;
                    cJSON *raw_name = raw_fn ? cJSON_GetObjectItem(raw_fn, "name") : NULL;
                    LOG_DEBUG("process_response: raw_tool_call[%d]: id=%s name=%s has_function=%d",
                              i,
                              (raw_id && cJSON_IsString(raw_id) && raw_id->valuestring) ? raw_id->valuestring : "NULL",
                              (raw_name && cJSON_IsString(raw_name) && raw_name->valuestring) ? raw_name->valuestring : "NULL",
                              raw_fn != NULL);
                }
            }
        }

        if (tool_count == 0 && finish_reason_str && strcmp(finish_reason_str, "tool_calls") == 0) {
            LOG_ERROR("process_response: finish_reason=tool_calls but no valid tool calls were parsed (raw_tool_calls=%d)",
                      raw_tool_call_count);
            ui_show_error(tui, queue,
                          "Provider returned tool_calls but no valid tool call payload was reconstructed.");
            break;
        }

        if (tool_count > 0) {

            LOG_INFO("Processing %d tool call(s) in iteration %d", tool_count, iteration);

            // Log details of each tool call
            for (int i = 0; i < tool_count; i++) {
                ToolCall *tool = &tool_calls_array[i];
                LOG_DEBUG("Tool call[%d]: id=%s, name=%s, has_params=%d",
                          i, tool->id ? tool->id : "NULL",
                          tool->name ? tool->name : "NULL",
                          tool->parameters != NULL);
            }

            struct timespec tool_start, tool_end;
            clock_gettime(CLOCK_MONOTONIC, &tool_start);

            InternalContent *results = calloc((size_t)tool_count, sizeof(InternalContent));
            if (!results) {
                ui_show_error(tui, queue, "Failed to allocate tool result buffer");
                break;
            }

            int valid_tool_calls = 0;
            for (int i = 0; i < tool_count; i++) {
                ToolCall *tool = &tool_calls_array[i];
                if (tool->name && tool->id) {
                    valid_tool_calls++;
                }
            }

            pthread_t *threads = NULL;
            if (valid_tool_calls > 0) {
                threads = calloc((size_t)valid_tool_calls, sizeof(pthread_t));
                if (!threads) {
                    ui_show_error(tui, queue, "Failed to allocate tool thread array");
                    free_internal_contents(results, tool_count);
                    break;
                }
            }

            ToolCallbackContext callback_ctx = {
                .tui = tui,
                .queue = queue,
                .spinner = NULL,
                .worker_ctx = worker_ctx
            };

            Spinner *tool_spinner = NULL;
            if (!tui && !queue) {
                // Use varied message for spinner in non-TUI mode
                const char *varied_msg = spinner_random_msg_for_context(SPINNER_CONTEXT_TOOL_RUNNING);
                tool_spinner = spinner_start(varied_msg, SPINNER_YELLOW);
            } else {
                // Use varied message for TUI status
                ui_set_status_varied(tui, queue, SPINNER_CONTEXT_TOOL_RUNNING);
            }
            callback_ctx.spinner = tool_spinner;

            ToolExecutionTracker tracker;
            int tracker_initialized = 0;
            if (valid_tool_calls > 0) {
                if (tool_tracker_init(&tracker, valid_tool_calls, tool_progress_callback, &callback_ctx) != 0) {
                    ui_show_error(tui, queue, "Failed to initialize tool tracker");
                    if (tool_spinner) {
                        spinner_stop(tool_spinner, "Tool execution failed to start", 0);
                    }
                    free(threads);
                    free_internal_contents(results, tool_count);
                    break;
                }
                tracker_initialized = 1;
            }

            int started_threads = 0;
            int interrupted = 0;  // Track if user requested interruption during scheduling/waiting

            for (int i = 0; i < tool_count; i++) {
                // Check for interrupt before starting each tool
                if (state->interrupt_requested) {
                    LOG_INFO("Tool execution interrupted by user request (before starting remaining tools)");
                    ui_show_error(tui, queue, "Tool execution interrupted by user");
                    interrupted = 1;

                    // For any tools not yet started, emit a cancelled tool_result so the
                    // conversation remains consistent (every tool_call gets a tool_result)
                    for (int k = i; k < tool_count; k++) {
                        ToolCall *tcancel = &tool_calls_array[k];
                        InternalContent *slot = &results[k];
                        slot->type = INTERNAL_TOOL_RESPONSE;
                        slot->tool_id = tcancel->id ? strdup(tcancel->id) : strdup("unknown");
                        slot->tool_name = tcancel->name ? strdup(tcancel->name) : strdup("tool");
                        cJSON *err = cJSON_CreateObject();
                        cJSON_AddStringToObject(err, "error", "Tool execution cancelled before start");
                        slot->tool_output = err;
                        slot->is_error = 1;
                    }
                    break;  // Stop launching new tools
                }

                ToolCall *tool = &tool_calls_array[i];
                InternalContent *result_slot = &results[i];
                result_slot->type = INTERNAL_TOOL_RESPONSE;

                if (!tool->name || !tool->id || !tool->name[0] || !tool->id[0]) {
                    LOG_ERROR("Tool call missing name or id (provider validation failed)");
                    result_slot->tool_id = (tool->id && tool->id[0]) ? strdup(tool->id) : strdup("unknown");
                    result_slot->tool_name = (tool->name && tool->name[0]) ? strdup(tool->name) : strdup("tool");
                    cJSON *error = cJSON_CreateObject();
                    cJSON_AddStringToObject(error, "error", "Tool call missing name or id");
                    result_slot->tool_output = error;
                    result_slot->is_error = 1;
                    continue;
                }

                // Validate that the tool is in the allowed tools list (prevent hallucination)
                if (!is_tool_allowed(tool->name, state)) {
                    LOG_ERROR("Tool validation failed: '%s' was not provided in tools list (model hallucination)", tool->name);
                    result_slot->tool_id = strdup(tool->id);
                    result_slot->tool_name = strdup(tool->name);
                    cJSON *error = cJSON_CreateObject();
                    char error_msg[512];

                    // Check if this is a plan mode restriction
                    int is_plan_mode_restriction = state->plan_mode &&
                        (strcmp(tool->name, "Bash") == 0 ||
                         strcmp(tool->name, "Subagent") == 0 ||
                         strcmp(tool->name, "Write") == 0 ||
                         strcmp(tool->name, "Edit") == 0);

                    if (is_plan_mode_restriction) {
                        snprintf(error_msg, sizeof(error_msg),
                                 "ERROR: Tool '%s' is not available in planning mode. "
                                 "You are currently in PLANNING MODE which only allows read-only tools: "
                                 "Read, Glob, Grep, Sleep, UploadImage, TodoWrite, ListMcpResources, ReadMcpResource. "
                                 "Please reformulate your request using only these available tools.",
                                 tool->name);
                    } else {
                        snprintf(error_msg, sizeof(error_msg),
                                 "ERROR: Tool '%s' does not exist or was not provided to you. "
                                 "Please check the list of available tools and try again with a valid tool name.",
                                 tool->name);
                    }

                    cJSON_AddStringToObject(error, "error", error_msg);
                    result_slot->tool_output = error;
                    result_slot->is_error = 1;

                    // Display error to user
                    char prefix_with_tool[128];
                    snprintf(prefix_with_tool, sizeof(prefix_with_tool), "● %s", tool->name);
                    char display_error[512];
                    if (is_plan_mode_restriction) {
                        snprintf(display_error, sizeof(display_error),
                                 "Error: Tool '%s' not available in planning mode (read-only mode)",
                                 tool->name);
                    } else {
                        snprintf(display_error, sizeof(display_error),
                                 "Error: Tool '%s' not available (not in provided tools list)",
                                 tool->name);
                    }
                    ui_append_line(tui, queue, prefix_with_tool, display_error, COLOR_PAIR_ERROR);
                    continue;
                }

                cJSON *input = tool->parameters
                    ? cJSON_Duplicate(tool->parameters, /*recurse*/1)
                    : cJSON_CreateObject();

                // Get tool details (includes special handling for Subagent to show all params)
                char *tool_details = get_tool_details(tool->name, input);
                char prefix_with_tool[128];
                snprintf(prefix_with_tool, sizeof(prefix_with_tool), "● %s", tool->name);
                ui_append_line(tui, queue, prefix_with_tool, tool_details, COLOR_PAIR_TOOL);

                if (!tracker_initialized) {
                    cJSON *error = cJSON_CreateObject();
                    cJSON_AddStringToObject(error, "error", "Internal error initializing tool tracker");
                    result_slot->tool_id = strdup(tool->id);
                    result_slot->tool_name = strdup(tool->name);
                    result_slot->tool_output = error;
                    result_slot->is_error = 1;
                    cJSON_Delete(input);
                    continue;
                }

                // Create per-thread arena for this tool's arguments
                Arena *thread_arena = arena_create(TOOL_THREAD_ARENA_SIZE);
                if (!thread_arena) {
                    LOG_ERROR("Failed to create arena for tool thread %s", tool->name);
                    cJSON_Delete(input);
                    result_slot->tool_id = strdup(tool->id);
                    result_slot->tool_name = strdup(tool->name);
                    cJSON *error = cJSON_CreateObject();
                    cJSON_AddStringToObject(error, "error", "Failed to allocate memory for tool thread");
                    result_slot->tool_output = error;
                    result_slot->is_error = 1;
                    continue;
                }

                // Allocate ToolThreadArg from the arena
                ToolThreadArg *current = arena_alloc(thread_arena, sizeof(ToolThreadArg));
                if (!current) {
                    LOG_ERROR("Failed to allocate ToolThreadArg from arena for %s", tool->name);
                    arena_destroy(thread_arena);
                    cJSON_Delete(input);
                    result_slot->tool_id = strdup(tool->id);
                    result_slot->tool_name = strdup(tool->name);
                    cJSON *error = cJSON_CreateObject();
                    cJSON_AddStringToObject(error, "error", "Failed to allocate memory for tool thread");
                    result_slot->tool_output = error;
                    result_slot->is_error = 1;
                    continue;
                }

                // Clone strings into the arena
                current->tool_use_id = arena_strdup(thread_arena, tool->id);
                current->tool_name = arena_strdup(thread_arena, tool->name);
                if (!current->tool_use_id || !current->tool_name) {
                    LOG_ERROR("Failed to clone strings into arena for %s", tool->name);
                    arena_destroy(thread_arena);
                    cJSON_Delete(input);
                    result_slot->tool_id = strdup(tool->id);
                    result_slot->tool_name = strdup(tool->name);
                    cJSON *error = cJSON_CreateObject();
                    cJSON_AddStringToObject(error, "error", "Failed to allocate memory for tool thread");
                    result_slot->tool_output = error;
                    result_slot->is_error = 1;
                    continue;
                }

                // Set up remaining fields
                current->input = input;  // Ownership transferred
                current->state = state;
                current->result_block = result_slot;
                current->tracker = &tracker;
                current->notified = 0;
                current->queue = queue;
                current->arena = thread_arena;  // Thread owns the arena

                int rc = pthread_create(&threads[started_threads], NULL, tool_thread_func, current);
                if (rc != 0) {
                    LOG_ERROR("Failed to create tool thread for %s (rc=%d)", tool->name, rc);

                    // CRITICAL FIX: Cancel already-started threads on failure
                    // This prevents zombie threads if we fail mid-creation
                    for (int cancel_idx = 0; cancel_idx < started_threads; cancel_idx++) {
                        pthread_cancel(threads[cancel_idx]);
                    }
                    // Threads will be joined later in the cleanup path

                    // Clean up this thread's resources
                    cJSON_Delete(input);
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

            if (tracker_initialized && started_threads > 0) {
                while (1) {
                    // Check for interrupt request
                    if (state->interrupt_requested) {
                        LOG_INFO("Tool execution interrupted by user request");
                        interrupted = 1;

                        // CRITICAL FIX: Actually cancel the threads, not just the tracker
                        // Setting tracker.cancelled alone doesn't stop running threads
                        pthread_mutex_lock(&tracker.mutex);
                        tracker.cancelled = 1;
                        pthread_cond_broadcast(&tracker.cond);
                        pthread_mutex_unlock(&tracker.mutex);

                        // Cancel all running threads - this triggers cleanup handlers
                        for (int t = 0; t < started_threads; t++) {
                            pthread_cancel(threads[t]);
                        }
                        // Reset interrupt flag immediately after cancelling threads
                        state->interrupt_requested = 0;
                        break;
                    }

                    pthread_mutex_lock(&tracker.mutex);
                    if (tracker.cancelled || tracker.completed >= tracker.total) {
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

                    // Wait for condition variable with timeout
                    (void)pthread_cond_timedwait(&tracker.cond, &tracker.mutex, &deadline);
                    if (tracker.cancelled || tracker.completed >= tracker.total) {
                        pthread_mutex_unlock(&tracker.mutex);
                        break;
                    }
                    pthread_mutex_unlock(&tracker.mutex);

                    // Interactive interrupt handling (Ctrl+C) is done by the TUI event loop
                    // Non-TUI mode doesn't have interactive interrupt support here
                }
            }

            // Join or detach threads based on whether we were interrupted
            // If interrupted, use timed waiting to prevent deadlock from stuck threads
            if (interrupted) {
                // When interrupted, threads may be stuck in blocking calls that don't
                // respond to pthread_cancel. We wait for a short period for threads to
                // terminate via the tracker, then detach any that are still running.

                // Wait up to 500ms for all threads to complete (100ms polls)
                const int max_wait_ms = 500;
                int waited_ms = 0;
                while (waited_ms < max_wait_ms) {
                    pthread_mutex_lock(&tracker.mutex);
                    int all_completed = (tracker.completed >= started_threads);
                    pthread_mutex_unlock(&tracker.mutex);

                    if (all_completed) {
                        break;
                    }

                    usleep(100 * 1000);  // 100ms
                    waited_ms += 100;
                }

                // Check final completion status
                pthread_mutex_lock(&tracker.mutex);
                int final_completed = tracker.completed;
                pthread_mutex_unlock(&tracker.mutex);

                if (final_completed >= started_threads) {
                    // All threads completed - safe to join
                    for (int t = 0; t < started_threads; t++) {
                        pthread_join(threads[t], NULL);
                    }
                } else {
                    // Some threads still running after cancellation timeout.
                    // With the arena approach, each thread owns its own memory, so
                    // detaching is safe - the thread will destroy its arena when done.
                    LOG_WARN("Only %d/%d tool threads responded to cancellation within %dms, detaching remaining",
                             final_completed, started_threads, max_wait_ms);

                    // We can't easily tell which threads completed vs which are still running
                    // without per-thread state. Use pthread_tryjoin_np on Linux or just
                    // detach all remaining threads. Detaching is safe because each thread
                    // owns its arena and will clean up when it eventually exits.
                    for (int t = 0; t < started_threads; t++) {
                        pthread_detach(threads[t]);
                    }
                }
            } else {
                // Normal completion - join all threads
                for (int t = 0; t < started_threads; t++) {
                    pthread_join(threads[t], NULL);
                }
            }

            clock_gettime(CLOCK_MONOTONIC, &tool_end);
            long tool_exec_ms = (tool_end.tv_sec - tool_start.tv_sec) * 1000 +
                                (tool_end.tv_nsec - tool_start.tv_nsec) / 1000000;
            total_tool_exec_ms += tool_exec_ms;
            LOG_INFO("All %d tool(s) processed in %ld ms", started_threads, tool_exec_ms);

            if (tracker_initialized) {
                tool_tracker_destroy(&tracker);
            }

            // Log summary of all tool results
            LOG_DEBUG("Tool execution summary: %d results collected", tool_count);
            for (int i = 0; i < tool_count; i++) {
                LOG_DEBUG("Result[%d]: tool_id=%s, tool_name=%s, is_error=%d",
                          i, results[i].tool_id ? results[i].tool_id : "NULL",
                          results[i].tool_name ? results[i].tool_name : "NULL",
                          results[i].is_error);
            }

            int has_error = 0;
            for (int i = 0; i < tool_count; i++) {
                if (results[i].is_error) {
                    has_error = 1;

                    cJSON *error_obj = results[i].tool_output
                        ? cJSON_GetObjectItem(results[i].tool_output, "error")
                        : NULL;
                    const char *error_msg = (error_obj && cJSON_IsString(error_obj))
                        ? error_obj->valuestring
                        : "Unknown error";
                    const char *tool_name = results[i].tool_name ? results[i].tool_name : "tool";

                    char error_display[512];
                    snprintf(error_display, sizeof(error_display), "%s failed: %s", tool_name, error_msg);
                    ui_show_error(tui, queue, error_display);
                }
            }

            // If interrupted at any point, ensure UI reflects it but continue to add
            // tool_result messages so the conversation stays consistent.
            if (interrupted) {
                if (!tui && !queue) {
                    if (tool_spinner) {
                        spinner_stop(tool_spinner, "Interrupted by user (Ctrl+C) - tools terminated", 0);
                    }
                } else {
                    ui_set_status(tui, queue, "Interrupted by user (Ctrl+C) - tools terminated");
                }
                // Reset interrupt flag after tool execution is interrupted
                state->interrupt_requested = 0;
            }

            if (!tui && !queue) {
                if (tool_spinner) {
                    if (has_error) {
                        spinner_stop(tool_spinner, "Tool execution completed with errors", 0);
                    } else {
                        spinner_stop(tool_spinner, "Tool execution completed successfully", 1);
                    }
                }
            } else {
                if (has_error) {
                    ui_set_status(tui, queue, "Tool execution completed with errors");
                } else {
                    ui_set_status(tui, queue, "");
                }
            }

            free(threads);
            // With the arena approach, each thread owns and destroys its own arena.
            // No shared args context to release - memory management is fully distributed.

            // Extract TodoWrite information BEFORE transferring ownership to add_tool_results
            int todo_write_executed = check_todo_write_executed(results, tool_count);

            // Record tool results even in the interrupt path so that every tool_call
            // has a corresponding tool_result. This prevents 400s due to missing results.
            if (add_tool_results(state, results, tool_count) != 0) {
                LOG_ERROR("Failed to add tool results to conversation state");
                // Results were already freed by add_tool_results
                results = NULL;
            }

            if (todo_write_executed && state->todo_list && state->todo_list->count > 0) {
                // Skip rendering todo list in interactive TUI mode - the todo banner
                // at the bottom of the screen already shows the current status.
                // Only render for non-TUI mode (when both tui and queue are NULL).
                if (!tui && !queue) {
                    char *todo_text = todo_render_to_string(state->todo_list);
                    if (todo_text) {
                        ui_append_line(tui, queue, "[Assistant]", todo_text, COLOR_PAIR_ASSISTANT);
                        free(todo_text);
                    }
                }
            }

            // Render active subagents after tool execution (if any are running)
            if (tui && state->subagent_manager) {
                int running_count = subagent_manager_get_running_count(state->subagent_manager);
                if (running_count > 0) {
                    tui_render_active_subagents(tui);
                }
            }

            ApiResponse *next_response = NULL;
            if (!interrupted) {
                Spinner *followup_spinner = NULL;
                if (!tui && !queue) {
                    // Use the same color as other status messages to reduce color variance
                    followup_spinner = spinner_start("Processing tool results...", SPINNER_YELLOW);
                } else {
                    ui_set_status_varied(tui, queue, SPINNER_CONTEXT_PROCESSING);
                }
                next_response = call_api_with_retries(state);
                if (!tui && !queue) {
                    spinner_stop(followup_spinner, NULL, 1);
                } else {
                    ui_set_status(tui, queue, "");
                }
            }

            if (next_response) {
                // Check if response contains an error message
                if (next_response->error_message) {
                    ui_show_error(tui, queue, next_response->error_message);
                    api_response_free(next_response);
                    break;
                }

                if (owns_current_response) {
                    api_response_free(current_response);
                }
                current_response = next_response;
                owns_current_response = 1;
                continue;
            } else if (state->interrupt_requested) {
                // User interrupted the tool results processing
                LOG_INFO("Tool results processing interrupted by user");
                state->interrupt_requested = 0;  // Clear for next operation
                break;
            } else if (!interrupted) {
                const char *error_msg = "API call failed after executing tools. Check logs for details.";
                ui_show_error(tui, queue, error_msg);
                LOG_ERROR("API call returned NULL after tool execution");
            }
            break;
        }

        if (owns_current_response) {
            api_response_free(current_response);
            current_response = NULL;
            owns_current_response = 0;
        }
        break;
    }

    if (current_response && iteration >= INTERACTIVE_TOOL_LOOP_MAX_ITERATIONS) {
        LOG_ERROR("process_response: exceeded maximum tool follow-up iterations (%d)",
                  INTERACTIVE_TOOL_LOOP_MAX_ITERATIONS);
        ui_show_error(tui, queue,
                      "Maximum tool follow-up iteration limit reached. Aborting recursive tool loop.");
    }

    if (owns_current_response && current_response) {
        api_response_free(current_response);
    }

    // AI turn completed - hide todo banner in TUI mode
    if (tui) {
        window_manager_hide_todo_window(&tui->wm);
    } else if (queue) {
        // Worker thread: post message to main thread to hide banner
        post_tui_message(queue, TUI_MSG_TODO_HIDE, NULL);
    }

    // Check if compaction is needed at end of AI turn
    if (state->compaction_config && compaction_should_trigger(state, state->compaction_config)) {
        LOG_INFO("Context compaction triggered at end of AI turn");
        CompactionResult compaction_result = {0};
        if (compaction_perform(state, state->compaction_config, state->session_id, &compaction_result) == 0) {
            if (compaction_result.success && state->sqlite_queue_context && state->sqlite_queue_context->enabled) {
                sqlite_queue_send_compaction_notice(
                    state->sqlite_queue_context,
                    "client",
                    compaction_result.messages_compacted,
                    compaction_result.tokens_before,
                    compaction_result.tokens_after,
                    compaction_result.usage_before_pct,
                    compaction_result.usage_after_pct,
                    compaction_result.summary[0] != '\0' ? compaction_result.summary : NULL
                );
            }
        } else {
            LOG_WARN("Compaction failed at end of AI turn");
        }
    }

    // Scroll to the last assistant message at end of AI turn
    if (tui) {
        tui_scroll_to_last_assistant(tui);
    }

    clock_gettime(CLOCK_MONOTONIC, &proc_end);
    long proc_ms = (proc_end.tv_sec - proc_start.tv_sec) * 1000 +
                   (proc_end.tv_nsec - proc_start.tv_nsec) / 1000000;
    LOG_INFO("Response processing completed in %ld ms (iterations=%d, total_tool_ms=%ld)",
             proc_ms, iteration, total_tool_exec_ms);
}

void ai_worker_handle_instruction(AIWorkerContext *ctx, const AIInstruction *instruction) {
    if (!ctx || !instruction) {
        return;
    }

    /* Perpetual mode: each turn is a fully fresh run. */
    if (ctx->state && ctx->state->is_perpetual_mode) {
        const char *query = instruction->text ? instruction->text : "";
        LOG_DEBUG("ai_worker_handle_instruction: perpetual run, query='%s'", query);

        ui_set_status_varied(NULL, ctx->tui_queue, SPINNER_CONTEXT_API_CALL);

        /* Reset state so perpetual_mode_run starts from a clean slate. */
        clear_conversation(ctx->state);

        int rc = perpetual_mode_run(ctx->state, query, ctx->tui_queue);
        if (rc != 0) {
            ui_show_error(NULL, ctx->tui_queue, "Perpetual mode run failed");
        }

        /* Clear again so the next turn starts clean. */
        clear_conversation(ctx->state);

        ui_set_status(NULL, ctx->tui_queue, "");
        return;
    }



    ui_set_status_varied(NULL, ctx->tui_queue, SPINNER_CONTEXT_API_CALL);
    ApiResponse *response = call_api_with_retries(ctx->state);

    ui_set_status(NULL, ctx->tui_queue, "");

    if (!response) {
        // Check if this was due to an interrupt - if so, show appropriate message
        // and ensure the interrupt flag is cleared for the next instruction
        if (ctx->state && ctx->state->interrupt_requested) {
            ui_set_status(NULL, ctx->tui_queue, "Operation interrupted by user");
            ctx->state->interrupt_requested = 0;  // Clear for next instruction
        } else {
            ui_show_error(NULL, ctx->tui_queue, "Failed to get response from API");
        }
        return;
    }

    // Check if response contains an error message (e.g., context length exceeded)
    if (response->error_message) {
        ui_show_error(NULL, ctx->tui_queue, response->error_message);
        api_response_free(response);
        return;
    }

    process_response(ctx->state, response, NULL, ctx->tui_queue, ctx);
    api_response_free(response);
}
