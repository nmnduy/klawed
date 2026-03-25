/*
 * Copyright (c) 2025 [Your Company Name]. All rights reserved.
 *
 * This software is proprietary and confidential. Unauthorized copying,
 * modification, distribution, or use of this software, in whole or in part,
 * is strictly prohibited without prior written permission from the copyright
 * holder.
 *
 * This software is licensed under the End User License Agreement (EULA).
 * See LICENSE file for complete terms.
 *
 * For licensing information, please contact:
 * [Your Company Name]
 * [Your Email]
 * [Your Website]
 *
 * This software incorporates third-party components. See THIRD_PARTY_NOTICES
 * for attribution and licensing information.
 *
 * Klawed - Professional Coding Agent
 * A high-performance AI coding assistant built in pure C
 */

#ifndef __APPLE__
    #define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <bsd/stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <errno.h>
#include <glob.h>
#include <fcntl.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <sqlite3.h>
#include <limits.h>
#include <libgen.h>
#include <dirent.h>
#include <ctype.h>
#include <strings.h>
#include <signal.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// Socket support removed - will be reimplemented with ZMQ
#include "colorscheme.h"
#include "fallback_colors.h"
#include "tool_utils.h"
#include "process_utils.h"
#include "http_client.h"  // For StreamEvent and HttpStreamCallback


#include "sqlite_queue.h"
#include "explore_tools.h"
#include "retry_logic.h"
#include "tools/tool_definitions.h"
#ifndef TEST_BUILD
#include "openai_messages.h"
#endif
#include "config.h"
#include "provider_config_loader.h"

#ifdef TEST_BUILD
// Disable unused function warnings for test builds since not all functions are used by tests
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
// Test build: stub out persistence (logger is linked via LOGGER_OBJ)
// Stub persistence types and functions
typedef struct PersistenceDB { int dummy; } PersistenceDB;
static PersistenceDB* persistence_init(const char *path) { (void)path; return NULL; }
static void persistence_close(PersistenceDB *db) { (void)db; }
static void persistence_log_api_call(
    PersistenceDB *db,
    const char *session_id,
    const char *url,
    const char *request,
    const char *headers,
    const char *response,
    const char *model,
    const char *status,
    int code,
    const char *error_msg,
    long duration_ms,
    int tool_count
) { (void)db; (void)session_id; (void)url; (void)request; (void)headers; (void)response; (void)model; (void)status; (void)code; (void)error_msg; (void)duration_ms; (void)tool_count; }

static int persistence_get_session_token_usage(
    PersistenceDB *db,
    const char *session_id,
    int *prompt_tokens,
    int *completion_tokens,
    int *cached_tokens
) { (void)db; (void)session_id; (void)prompt_tokens; (void)completion_tokens; (void)cached_tokens; return -1; }

// Stub Bedrock types and functions
typedef struct {
    char *access_key_id;
    char *secret_access_key;
    char *session_token;
    char *region;
    char *profile;
} AWSCredentials;

typedef struct BedrockConfigStruct {
    int enabled;
    char *region;
    char *model_id;
    char *endpoint;
    AWSCredentials *creds;
} BedrockConfig;

static int bedrock_is_enabled(void) { return 0; }
static BedrockConfig* bedrock_config_init(const char *model_id) { (void)model_id; return NULL; }
static void bedrock_config_free(BedrockConfig *config) { (void)config; }
static char* bedrock_convert_request(const char *openai_request) { (void)openai_request; return NULL; }
static cJSON* bedrock_convert_response(const char *bedrock_response) { (void)bedrock_response; return NULL; }
static struct curl_slist* bedrock_sign_request(
    struct curl_slist *headers,
    const char *method,
    const char *url,
    const char *payload,
    const AWSCredentials *creds,
    const char *region,
    const char *service
) { (void)method; (void)url; (void)payload; (void)creds; (void)region; (void)service; return headers; }
static int bedrock_handle_auth_error(BedrockConfig *config, long http_status, const char *error_message, const char *response_body) {
    (void)config; (void)http_status; (void)error_message; (void)response_body;
    return 0;
}

static void ensure_tool_results(struct ConversationState *state) {
    (void)state;
    // Stub for test builds
}
#else
// Normal build: use actual implementations
#include "logger.h"
#include "persistence.h"
#endif

// Visual indicators for interactive mode
#include "indicators.h"

// Internal API for module access
#include "klawed_internal.h"
#include "compaction.h"

// Session management
#include "session/token_usage.h"
#include "session/session_persistence.h"
#ifndef TEST_BUILD
#include "session.h"
#endif
#include "provider.h"  // For ApiCallResult and Provider definitions
#include "todo.h"

// API layer
#include "api/api_response.h"
#include "api/api_builder.h"
#include "api/api_client.h"

// Commands module
#include "commands.h"

// TUI module
#include "tui.h"
#include "message_queue.h"
#include "ai_worker.h"

// Subagent management
#ifdef TEST_BUILD
// Stub SubagentManager types and functions for test builds
typedef struct SubagentManager { int dummy; } SubagentManager;
static int subagent_manager_init(SubagentManager *manager) { (void)manager; return 0; }
static void subagent_manager_free(SubagentManager *manager) { (void)manager; }
static int subagent_manager_add(SubagentManager *manager, pid_t pid, const char *log_file, const char *prompt, int timeout_seconds) { (void)manager; (void)pid; (void)log_file; (void)prompt; (void)timeout_seconds; return -1; }
static int subagent_manager_terminate_all(SubagentManager *manager, int grace_period_ms) { (void)manager; (void)grace_period_ms; return 0; }
static int subagent_manager_get_running_count(SubagentManager *manager) { (void)manager; return 0; }
#else
#include "subagent_manager.h"
#endif

// AWS Bedrock support
#ifndef TEST_BUILD
#include "aws_bedrock.h"
#endif

// MCP (Model Context Protocol) support
#ifndef TEST_BUILD
#include "mcp.h"
#endif

// Base64 encoding/decoding for binary content
#include "base64.h"

// Arena allocator for per-thread memory management
#include "arena.h"

// Memory database support (SQLite-based, replaces memvid)
#include "memory_db.h"

// Background initialization for async resource loading
#include "background_init.h"

// Utility modules
#include "util/file_utils.h"

// Context modules
#include "context/system_prompt.h"
#include "context/environment.h"
#include "context/klawed_md.h"
#include "context/memory_injection.h"
#include "util/string_utils.h"
#include "util/timestamp_utils.h"
#include "util/format_utils.h"
#include "util/env_utils.h"
#include "util/output_utils.h"
#include "util/diff_utils.h"

// Tool modules
#include "tools/tool_sleep.h"
#include "tools/tool_todo.h"
#include "tools/tool_image.h"
#include "tools/tool_search.h"
#include "tools/tool_filesystem.h"
#include "tools/tool_bash.h"
#include "tools/tool_subagent.h"
#include "tools/tool_registry.h"
#include "tools/tool_executor.h"

// UI modules
#include "ui/ui_output.h"
#include "ui/print_helpers.h"
#include "ui/tool_output_display.h"

// Interactive mode modules
#include "interactive/interactive_loop.h"
#include "interactive/input_handler.h"
#include "interactive/response_processor.h"
#include "interactive/command_dispatch.h"

// Oneshot mode modules
#include "oneshot/oneshot_mode.h"

// Perpetual mode modules
#include "perpetual/perpetual_mode.h"

// Note: Conversation management functions are declared in klawed_internal.h
// We don't include conversation/*.h here to avoid redundant declarations

#ifdef TEST_BUILD
#define main klawed_main
#endif

// Version
// ============================================================================
// Socket Input Parsing
// ============================================================================

/**
 * Parse socket input (must be JSON with messageType: "TEXT" and content)
 * Strict mode: requires valid JSON, no plain text fallback
 *
 * Returns a newly allocated string with the extracted content.
 * Caller must free the returned string.
 *
 * Returns:
 * - Extracted content if valid JSON with messageType: "TEXT"
 * - NULL if:
 *   - Input is NULL or empty
 *   - Not valid JSON
 *   - JSON missing required fields (messageType or content)
 *   - messageType is not "TEXT"
 */
// UI functions moved to src/ui/ modules

// UI functions moved to src/ui/ modules

// =====================================================================
// Tool Output Helpers
// =====================================================================

// Global thread-local queue for tool output during parallel execution
// Exported for tool modules that need to post TUI messages
_Thread_local TUIMessageQueue *g_active_tool_queue = NULL;

// Oneshot/subagent mode flag - when enabled, tool outputs are wrapped in HTML-style tags
// for easier parsing by parent processes or scripts
// Tool output display functions moved to src/ui/tool_output_display.c

// =====================================================================
// Emergency Cleanup for Subagents
// =====================================================================

// Global reference to subagent manager for emergency cleanup on unexpected termination
static SubagentManager *g_subagent_manager_for_cleanup = NULL;
static pthread_mutex_t g_cleanup_mutex = PTHREAD_MUTEX_INITIALIZER;

// Emergency cleanup handler called on exit or signal
static void emergency_cleanup_subagents(void) {
    pthread_mutex_lock(&g_cleanup_mutex);

    if (g_subagent_manager_for_cleanup) {
        LOG_INFO("Emergency cleanup: Terminating all running subagents");
        subagent_manager_terminate_all(g_subagent_manager_for_cleanup, 1000);  // 1 second grace period
        g_subagent_manager_for_cleanup = NULL;
    }

    pthread_mutex_unlock(&g_cleanup_mutex);
}

// Signal handler for SIGINT and SIGTERM
static void signal_handler_cleanup(int sig) {
    (void)sig;  // Unused
    emergency_cleanup_subagents();
    // Re-raise signal to allow default handler to run
    signal(sig, SIG_DFL);
    raise(sig);
}

// Register/unregister subagent manager for emergency cleanup
void register_subagent_manager_for_cleanup(SubagentManager *manager) {
    pthread_mutex_lock(&g_cleanup_mutex);
    g_subagent_manager_for_cleanup = manager;
    pthread_mutex_unlock(&g_cleanup_mutex);

    if (manager) {
        // Register atexit handler (only once)
        static int atexit_registered = 0;
        if (!atexit_registered) {
            atexit(emergency_cleanup_subagents);
            atexit_registered = 1;
        }

        // Register signal handlers
        signal(SIGINT, signal_handler_cleanup);
        signal(SIGTERM, signal_handler_cleanup);
    }
}

// TEMPORARY: tool_emit_line moved to src/util/output_utils.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: emit_diff_line moved to src/util/output_utils.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: get_current_timestamp moved to src/util/timestamp_utils.c
// This duplicate definition will be removed in a later refactoring step

// Helper function to extract tool details from arguments
// Tool output display functions moved to src/ui/tool_output_display.c

// ============================================================================
// Data Structures
// ============================================================================

// Note: MessageRole, ContentType, ContentBlock, Message, and ConversationState
// are now defined in klawed_internal.h for sharing across modules


// ============================================================================
// ESC Key Interrupt Handling
// ============================================================================



// ============================================================================
// Utility Functions
// ============================================================================

// For testing, we need to export some functions
#ifdef TEST_BUILD
#define STATIC
// Tool forward declarations are now in their respective header files
// File utils forward declarations are now in src/util/file_utils.h
#else
#define STATIC static
#endif


// TEMPORARY: read_file moved to src/util/file_utils.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: mkdir_p moved to src/util/file_utils.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: write_file moved to src/util/file_utils.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: resolve_path moved to src/util/file_utils.c
// This duplicate definition will be removed in a later refactoring step

// Add a directory to the additional working directories list
// Returns: 0 on success, -1 on error
// add_directory - moved to src/conversation/conversation_state.c

// TEMPORARY: tool_upload_image moved to src/tools/tool_image.c
// This duplicate definition will be removed in a later refactoring step

// ============================================================================
// ANSI Escape Sequence Filtering
// ============================================================================

// TEMPORARY: strip_ansi_escapes moved to src/util/string_utils.c
// This duplicate definition will be removed in a later refactoring step

// ============================================================================
// Binary File Handling for MCP Tools
// ============================================================================

// TEMPORARY: save_binary_file moved to src/util/file_utils.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: generate_timestamped_filename moved to src/util/timestamp_utils.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: format_file_size moved to src/util/format_utils.c
// This duplicate definition will be removed in a later refactoring step

// ============================================================================
// Diff Functionality
// ============================================================================

// TEMPORARY: show_diff moved to src/util/diff_utils.c
// This duplicate definition will be removed in a later refactoring step

// ============================================================================
// Tool Implementations
// ============================================================================



// TEMPORARY: tool_bash moved to src/tools/tool_bash.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: tool_subagent moved to src/tools/tool_subagent.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: tool_check_subagent_progress moved to src/tools/tool_subagent.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: tool_interrupt_subagent moved to src/tools/tool_subagent.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: tool_read moved to src/tools/tool_filesystem.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: tool_write moved to src/tools/tool_filesystem.c
// This duplicate definition will be removed in a later refactoring step

// Helper function for simple string multi-replace

// ============================================================================
// Parallel tool execution support
// ============================================================================

// Forward declaration


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

// arena_strdup, tool_tracker_*, tool_progress_callback, tool_thread_func
// moved to src/interactive/response_processor.c

// Helper function for simple string multi-replace






// TEMPORARY: tool_edit moved to src/tools/tool_filesystem.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: tool_multiedit moved to src/tools/tool_filesystem.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: tool_glob moved to src/tools/tool_filesystem.c
// This duplicate definition will be removed in a later refactoring step

// Helper function to check if a command exists
// TEMPORARY: command_exists moved to src/tools/tool_search.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: tool_grep moved to src/tools/tool_search.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: tool_todo_write moved to src/tools/tool_todo.c
// This duplicate definition will be removed in a later refactoring step


// ============================================================================
// Sleep Tool Implementation
// ============================================================================

/**
 * tool_sleep - pauses execution for specified duration
 * params: { "duration": integer (seconds) }
 */
// TEMPORARY: tool_sleep moved to src/tools/tool_sleep.c
// This duplicate definition will be removed in a later refactoring step


// ============================================================================
// Memory Tools Implementation (Memvid)
// ============================================================================

/**
 * tool_memory_store - Store a memory card about the user or project
 * params: { entity, slot, value, kind, relation (optional), memory_file (optional) }
 */
cJSON* tool_memory_store(cJSON *params, ConversationState *state) {
    (void)state;

    // Extract required parameters
    cJSON *entity_json = cJSON_GetObjectItem(params, "entity");
    cJSON *slot_json = cJSON_GetObjectItem(params, "slot");
    cJSON *value_json = cJSON_GetObjectItem(params, "value");
    cJSON *kind_json = cJSON_GetObjectItem(params, "kind");
    cJSON *relation_json = cJSON_GetObjectItem(params, "relation");
    cJSON *memory_file_json = cJSON_GetObjectItem(params, "memory_file");

    if (!entity_json || !cJSON_IsString(entity_json) ||
        !slot_json || !cJSON_IsString(slot_json) ||
        !value_json || !cJSON_IsString(value_json) ||
        !kind_json || !cJSON_IsString(kind_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing required parameters: entity, slot, value, kind");
        return error;
    }

    const char *entity = entity_json->valuestring;
    const char *slot = slot_json->valuestring;
    const char *value = value_json->valuestring;
    const char *kind_str = kind_json->valuestring;
    const char *relation_str = relation_json && cJSON_IsString(relation_json)
                               ? relation_json->valuestring : "sets";
    const char *memory_file = memory_file_json && cJSON_IsString(memory_file_json)
                              ? memory_file_json->valuestring : NULL;

    // Parse kind string to enum value
    MemoryKind kind = memory_db_string_to_kind(kind_str);

    // Parse relation string to enum value
    MemoryRelation relation = memory_db_string_to_relation(relation_str);

    // Get handle - either global or custom file
    int using_custom_file = (memory_file != NULL && memory_file[0] != '\0');
    MemoryDB *handle = memory_db_open_for_path(using_custom_file ? memory_file : NULL);
    if (handle == NULL) {
        // Try to initialize global if not already done
        if (!using_custom_file && memory_db_init_global(NULL) == 0) {
            handle = memory_db_get_global();
        }
        if (handle == NULL) {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Memory database not initialized");
            return error;
        }
    }

    int64_t card_id = memory_db_store(handle, entity, slot, value, kind, relation);
    if (card_id < 0) {
        const char *err_msg = memory_db_last_error(handle);
        if (using_custom_file) {
            memory_db_close(handle);
        }
        cJSON *error = cJSON_CreateObject();
        char error_buf[512];
        snprintf(error_buf, sizeof(error_buf), "Failed to store memory: %s",
                 err_msg ? err_msg : "unknown error");
        cJSON_AddStringToObject(error, "error", error_buf);
        return error;
    }

    // Close custom file handle if used
    if (using_custom_file) {
        memory_db_close(handle);
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddNumberToObject(result, "card_id", (double)card_id);
    cJSON_AddStringToObject(result, "entity", entity);
    cJSON_AddStringToObject(result, "slot", slot);
    cJSON_AddStringToObject(result, "kind", kind_str);
    cJSON_AddStringToObject(result, "relation", relation_str);
    return result;
}

/**
 * tool_memory_recall - Recall the current value for an entity's attribute
 * params: { entity, slot, memory_file (optional) }
 */
cJSON* tool_memory_recall(cJSON *params, ConversationState *state) {
    (void)state;

    cJSON *entity_json = cJSON_GetObjectItem(params, "entity");
    cJSON *slot_json = cJSON_GetObjectItem(params, "slot");
    cJSON *memory_file_json = cJSON_GetObjectItem(params, "memory_file");

    if (!entity_json || !cJSON_IsString(entity_json) ||
        !slot_json || !cJSON_IsString(slot_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing required parameters: entity, slot");
        return error;
    }

    const char *entity = entity_json->valuestring;
    const char *slot = slot_json->valuestring;
    const char *memory_file = memory_file_json && cJSON_IsString(memory_file_json)
                              ? memory_file_json->valuestring : NULL;

    // Get handle - either global or custom file
    int using_custom_file = (memory_file != NULL && memory_file[0] != '\0');
    MemoryDB *handle = memory_db_open_for_path(using_custom_file ? memory_file : NULL);
    if (handle == NULL) {
        // Try to initialize global if not already done
        if (!using_custom_file && memory_db_init_global(NULL) == 0) {
            handle = memory_db_get_global();
        }
        if (handle == NULL) {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Memory database not initialized");
            return error;
        }
    }

    // Query the memory database
    MemoryCard *card = memory_db_get_current(handle, entity, slot);

    // Close custom file handle if used
    if (using_custom_file) {
        memory_db_close(handle);
    }

    if (!card) {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "status", "not_found");
        cJSON_AddStringToObject(result, "entity", entity);
        cJSON_AddStringToObject(result, "slot", slot);
        cJSON_AddNullToObject(result, "value");
        return result;
    }

    // Build response with memory data
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "found");
    cJSON_AddStringToObject(result, "entity", entity);
    cJSON_AddStringToObject(result, "slot", slot);

    // Add value from card
    if (card->value) {
        cJSON *value_json = cJSON_Parse(card->value);
        if (value_json) {
            cJSON_AddItemToObject(result, "value", value_json);
        } else {
            cJSON_AddStringToObject(result, "value", card->value);
        }
    } else {
        cJSON_AddNullToObject(result, "value");
    }

    // Include card_id
    cJSON_AddNumberToObject(result, "card_id", (double)card->card_id);

    // Include kind if present
    if (card->kind) {
        cJSON_AddStringToObject(result, "kind", card->kind);
    }

    // Include timestamp if present
    if (card->timestamp) {
        cJSON_AddStringToObject(result, "timestamp", card->timestamp);
    }

    memory_db_free_card(card);
    return result;
}

/**
 * tool_memory_search - Search all memories by text query
 * params: { query, top_k (optional, default 10), memory_file (optional) }
 */
cJSON* tool_memory_search(cJSON *params, ConversationState *state) {
    (void)state;

    cJSON *query_json = cJSON_GetObjectItem(params, "query");
    cJSON *top_k_json = cJSON_GetObjectItem(params, "top_k");
    cJSON *memory_file_json = cJSON_GetObjectItem(params, "memory_file");

    if (!query_json || !cJSON_IsString(query_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing required parameter: query");
        return error;
    }

    const char *query = query_json->valuestring;
    uint32_t top_k = 10;  // default
    if (top_k_json && cJSON_IsNumber(top_k_json)) {
        int val = top_k_json->valueint;
        if (val > 0 && val <= 100) {
            top_k = (uint32_t)val;
        }
    }

    const char *memory_file = memory_file_json && cJSON_IsString(memory_file_json)
                              ? memory_file_json->valuestring : NULL;

    // Get handle - either global or custom file
    int using_custom_file = (memory_file != NULL && memory_file[0] != '\0');
    MemoryDB *handle = memory_db_open_for_path(using_custom_file ? memory_file : NULL);
    if (handle == NULL) {
        // Try to initialize global if not already done
        if (!using_custom_file && memory_db_init_global(NULL) == 0) {
            handle = memory_db_get_global();
        }
        if (handle == NULL) {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Memory database not initialized");
            return error;
        }
    }

    // Query the memory database using FTS5
    MemorySearchResult *search_result = memory_db_search(handle, query, top_k);

    // Close custom file handle if used
    if (using_custom_file) {
        memory_db_close(handle);
    }

    // Build results array
    cJSON *results_array = cJSON_CreateArray();
    if (search_result) {
        for (size_t i = 0; i < search_result->count; i++) {
            MemoryCard *card = &search_result->cards[i];
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "card_id", (double)card->card_id);
            cJSON_AddStringToObject(item, "entity", card->entity ? card->entity : "");
            cJSON_AddStringToObject(item, "slot", card->slot ? card->slot : "");
            cJSON_AddStringToObject(item, "value", card->value ? card->value : "");
            cJSON_AddStringToObject(item, "kind", card->kind ? card->kind : "fact");
            cJSON_AddStringToObject(item, "relation", card->relation ? card->relation : "sets");
            cJSON_AddStringToObject(item, "timestamp", card->timestamp ? card->timestamp : "");
            cJSON_AddNumberToObject(item, "score", card->score);
            cJSON_AddItemToArray(results_array, item);
        }
        memory_db_free_result(search_result);
    }

    // Build response
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddStringToObject(result, "query", query);
    cJSON_AddNumberToObject(result, "count", cJSON_GetArraySize(results_array));
    cJSON_AddItemToObject(result, "results", results_array);

    return result;
}

#ifndef TEST_BUILD
// MCP ListMcpResources tool handler
cJSON* tool_list_mcp_resources(cJSON *params, ConversationState *state) {
    LOG_DEBUG("tool_list_mcp_resources: Starting resource listing");

    if (!state || !state->mcp_config) {
        LOG_ERROR("tool_list_mcp_resources: MCP not configured");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "MCP not configured");
        return error;
    }

    // Extract optional server parameter
    const char *server_name = NULL;
    cJSON *server_json = cJSON_GetObjectItem(params, "server");
    if (server_json && cJSON_IsString(server_json)) {
        server_name = server_json->valuestring;
        LOG_DEBUG("tool_list_mcp_resources: Filtering by server '%s'", server_name);
    } else {
        LOG_DEBUG("tool_list_mcp_resources: No server filter specified, listing all servers");
    }

    // Call mcp_list_resources
    LOG_DEBUG("tool_list_mcp_resources: Calling mcp_list_resources");
    MCPResourceList *resource_list = mcp_list_resources(state->mcp_config, server_name);
    if (!resource_list) {
        LOG_ERROR("tool_list_mcp_resources: Failed to list resources");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to list resources");
        return error;
    }

    // Build result JSON
    cJSON *result = cJSON_CreateObject();

    if (resource_list->is_error) {
        LOG_ERROR("tool_list_mcp_resources: Resource listing error: %s",
                 resource_list->error_message ? resource_list->error_message : "Unknown error");
        cJSON_AddStringToObject(result, "error",
            resource_list->error_message ? resource_list->error_message : "Unknown error");
        mcp_free_resource_list(resource_list);
        return result;
    }

    LOG_DEBUG("tool_list_mcp_resources: Found %d resources", resource_list->count);

    // Create resources array
    cJSON *resources = cJSON_CreateArray();
    for (int i = 0; i < resource_list->count; i++) {
        MCPResource *res = resource_list->resources[i];
        if (!res) continue;

        LOG_DEBUG("tool_list_mcp_resources: Resource %d: server='%s', uri='%s', name='%s'",
                 i, res->server ? res->server : "null", res->uri ? res->uri : "null",
                 res->name ? res->name : "null");

        cJSON *res_obj = cJSON_CreateObject();
        if (res->server) cJSON_AddStringToObject(res_obj, "server", res->server);
        if (res->uri) cJSON_AddStringToObject(res_obj, "uri", res->uri);
        if (res->name) cJSON_AddStringToObject(res_obj, "name", res->name);
        if (res->description) cJSON_AddStringToObject(res_obj, "description", res->description);
        if (res->mime_type) cJSON_AddStringToObject(res_obj, "mimeType", res->mime_type);

        cJSON_AddItemToArray(resources, res_obj);
    }

    cJSON_AddItemToObject(result, "resources", resources);
    cJSON_AddNumberToObject(result, "count", resource_list->count);

    mcp_free_resource_list(resource_list);
    LOG_DEBUG("tool_list_mcp_resources: Completed successfully");
    return result;
}

// MCP ReadMcpResource tool handler
cJSON* tool_read_mcp_resource(cJSON *params, ConversationState *state) {
    LOG_DEBUG("tool_read_mcp_resource: Starting resource reading");

    if (!state || !state->mcp_config) {
        LOG_ERROR("tool_read_mcp_resource: MCP not configured");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "MCP not configured");
        return error;
    }

    // Extract required parameters
    cJSON *server_json = cJSON_GetObjectItem(params, "server");
    cJSON *uri_json = cJSON_GetObjectItem(params, "uri");

    if (!server_json || !cJSON_IsString(server_json)) {
        LOG_ERROR("tool_read_mcp_resource: Missing or invalid 'server' parameter");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'server' parameter");
        return error;
    }

    if (!uri_json || !cJSON_IsString(uri_json)) {
        LOG_ERROR("tool_read_mcp_resource: Missing or invalid 'uri' parameter");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'uri' parameter");
        return error;
    }

    const char *server_name = server_json->valuestring;
    const char *uri = uri_json->valuestring;

    LOG_DEBUG("tool_read_mcp_resource: Reading resource from server '%s', uri='%s'", server_name, uri);

    // Call mcp_read_resource
    LOG_DEBUG("tool_read_mcp_resource: Calling mcp_read_resource");
    MCPResourceContent *content = mcp_read_resource(state->mcp_config, server_name, uri);
    if (!content) {
        LOG_ERROR("tool_read_mcp_resource: Failed to read resource");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to read resource");
        return error;
    }

    // Build result JSON
    cJSON *result = cJSON_CreateObject();

    if (content->is_error) {
        LOG_ERROR("tool_read_mcp_resource: Resource reading error: %s",
                 content->error_message ? content->error_message : "Unknown error");
        cJSON_AddStringToObject(result, "error",
            content->error_message ? content->error_message : "Unknown error");
        mcp_free_resource_content(content);
        return result;
    }

    LOG_DEBUG("tool_read_mcp_resource: Resource read successfully, uri='%s', mime_type='%s', text_length=%zu",
             content->uri ? content->uri : "null",
             content->mime_type ? content->mime_type : "null",
             content->text ? strlen(content->text) : 0);

    if (content->uri) cJSON_AddStringToObject(result, "uri", content->uri);
    if (content->mime_type) cJSON_AddStringToObject(result, "mimeType", content->mime_type);
    if (content->text) cJSON_AddStringToObject(result, "text", content->text);

    // Note: Binary blob not yet supported

    mcp_free_resource_content(content);
    LOG_DEBUG("tool_read_mcp_resource: Completed successfully");
    return result;
}

// MCP CallMcpTool tool handler
cJSON* tool_call_mcp_tool(cJSON *params, ConversationState *state) {
    LOG_DEBUG("tool_call_mcp_tool: Starting MCP tool call");

    if (!state || !state->mcp_config) {
        LOG_ERROR("tool_call_mcp_tool: MCP not configured");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "MCP not configured");
        return error;
    }

    // Extract required parameters
    cJSON *server_json = cJSON_GetObjectItem(params, "server");
    cJSON *tool_json = cJSON_GetObjectItem(params, "tool");
    cJSON *args_json = cJSON_GetObjectItem(params, "arguments");

    if (!server_json || !cJSON_IsString(server_json)) {
        LOG_ERROR("tool_call_mcp_tool: Missing or invalid 'server' parameter");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'server' parameter");
        return error;
    }

    if (!tool_json || !cJSON_IsString(tool_json)) {
        LOG_ERROR("tool_call_mcp_tool: Missing or invalid 'tool' parameter");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'tool' parameter");
        return error;
    }

    const char *server_name = server_json->valuestring;
    const char *tool_name = tool_json->valuestring;

    LOG_DEBUG("tool_call_mcp_tool: Looking for server '%s' to call tool '%s'", server_name, tool_name);

    // Find server by name
    MCPServer *target = NULL;
    for (int i = 0; i < state->mcp_config->server_count; i++) {
        MCPServer *srv = state->mcp_config->servers[i];
        if (srv && srv->name && strcmp(srv->name, server_name) == 0) {
            target = srv;
            LOG_DEBUG("tool_call_mcp_tool: Found server '%s' at index %d", server_name, i);
            break;
        }
    }

    if (!target) {
        LOG_ERROR("tool_call_mcp_tool: MCP server '%s' not found", server_name);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "MCP server not found");
        return error;
    }

    if (!target->connected) {
        LOG_ERROR("tool_call_mcp_tool: MCP server '%s' not connected", server_name);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "MCP server not connected");
        return error;
    }

    // Ensure args is an object or null
    cJSON *args_object = NULL;
    if (args_json && cJSON_IsObject(args_json)) {
        args_object = args_json;
        char *args_str = cJSON_PrintUnformatted(args_json);
        LOG_DEBUG("tool_call_mcp_tool: Calling tool '%s' on server '%s' with args: %s",
                 tool_name, server_name, args_str ? args_str : "null");
        if (args_str) free(args_str);
    } else {
        LOG_DEBUG("tool_call_mcp_tool: Calling tool '%s' on server '%s' with no arguments",
                 tool_name, server_name);
    }

    LOG_DEBUG("tool_call_mcp_tool: Calling mcp_call_tool");
    MCPToolResult *call_result = mcp_call_tool(target, tool_name, args_object);
    cJSON *result = cJSON_CreateObject();
    if (!call_result) {
        LOG_ERROR("tool_call_mcp_tool: MCP tool call failed for tool '%s' on server '%s' (memory allocation error)",
                 tool_name, server_name);
        cJSON_AddStringToObject(result, "error", "MCP tool call failed: memory allocation error");
        return result;
    }

    if (call_result->is_error) {
        LOG_ERROR("tool_call_mcp_tool: MCP tool returned error: %s",
                 call_result->result ? call_result->result : "MCP tool error");
        // Include the detailed error message for the user
        cJSON_AddStringToObject(result, "error", call_result->result ? call_result->result : "MCP tool error");
    } else {
        LOG_DEBUG("tool_call_mcp_tool: MCP tool call succeeded, result length: %zu, blob size: %zu, mime_type: %s",
                 call_result->result ? strlen(call_result->result) : 0,
                 call_result->blob_size,
                 call_result->mime_type ? call_result->mime_type : "none");

        // Handle different content types
        if (call_result->blob && call_result->blob_size > 0) {
            // Binary content (e.g., images) - auto-save to file
            const char *mime_type = call_result->mime_type ? call_result->mime_type : "application/octet-stream";

            // Generate appropriate filename based on tool and MIME type
            char filename[256];
            if (strncmp(tool_name, "screenshot", 10) == 0 ||
                strncmp(tool_name, "take_screenshot", 15) == 0) {
                generate_timestamped_filename(filename, sizeof(filename), "screenshot", mime_type);
            } else if (strncmp(mime_type, "image/", 6) == 0) {
                generate_timestamped_filename(filename, sizeof(filename), "image", mime_type);
            } else {
                generate_timestamped_filename(filename, sizeof(filename), "file", mime_type);
            }

            // Save binary data to file
            int save_result = save_binary_file(filename, call_result->blob, call_result->blob_size);

            if (save_result == 0) {
                // Success - encode base64 for image content (if it's an image)
                int is_image = (strncmp(mime_type, "image/", 6) == 0);

                if (is_image) {
                    // For images, encode to base64 and mark as image content
                    // This allows the TUI to display it properly like UploadImage
                    size_t encoded_size = 0;
                    char *encoded_data = base64_encode(call_result->blob, call_result->blob_size, &encoded_size);
                    if (encoded_data) {
                        cJSON_AddStringToObject(result, "content_type", "image");
                        cJSON_AddStringToObject(result, "file_path", filename);
                        cJSON_AddStringToObject(result, "mime_type", mime_type);
                        cJSON_AddStringToObject(result, "base64_data", encoded_data);
                        cJSON_AddNumberToObject(result, "file_size_bytes", (double)call_result->blob_size);
                        free(encoded_data);
                        LOG_INFO("tool_call_mcp_tool: Saved image to '%s' (%zu bytes)", filename, call_result->blob_size);
                    } else {
                        // Encoding failed, fall back to file info only
                        LOG_WARN("tool_call_mcp_tool: Failed to encode image to base64, returning file info only");
                        cJSON_AddStringToObject(result, "status", "success");
                        cJSON_AddStringToObject(result, "message", "Image saved to file");
                        cJSON_AddStringToObject(result, "file_path", filename);
                        cJSON_AddStringToObject(result, "file_type", mime_type);
                        cJSON_AddNumberToObject(result, "file_size_bytes", (double)call_result->blob_size);
                        cJSON_AddStringToObject(result, "file_size_human", format_file_size(call_result->blob_size));
                    }
                } else {
                    // For non-image binary content, return file info only
                    cJSON_AddStringToObject(result, "status", "success");
                    cJSON_AddStringToObject(result, "message", "Binary content saved to file");
                    cJSON_AddStringToObject(result, "file_path", filename);
                    cJSON_AddStringToObject(result, "file_type", mime_type);
                    cJSON_AddNumberToObject(result, "file_size_bytes", (double)call_result->blob_size);
                    cJSON_AddStringToObject(result, "file_size_human", format_file_size(call_result->blob_size));
                    LOG_INFO("tool_call_mcp_tool: Saved binary content to '%s' (%zu bytes)", filename, call_result->blob_size);
                }
            } else {
                // Failed to save - fall back to base64 (but this shouldn't happen)
                LOG_WARN("tool_call_mcp_tool: Failed to save binary content to file, falling back to base64");
                cJSON_AddStringToObject(result, "content_type", "binary");
                cJSON_AddStringToObject(result, "mime_type", mime_type);

                size_t encoded_size = 0;
                char *encoded_data = base64_encode(call_result->blob, call_result->blob_size, &encoded_size);
                if (encoded_data) {
                    cJSON_AddStringToObject(result, "content", encoded_data);
                    free(encoded_data);
                } else {
                    cJSON_AddStringToObject(result, "content", "[binary data received - saving and encoding failed]");
                }
            }
        } else {
            // Text content
            cJSON_AddStringToObject(result, "content_type", "text");
            if (call_result->mime_type) {
                cJSON_AddStringToObject(result, "mime_type", call_result->mime_type);
            }
            cJSON_AddStringToObject(result, "content", call_result->result ? call_result->result : "");
        }
    }

    mcp_free_tool_result(call_result);
    LOG_DEBUG("tool_call_mcp_tool: Completed successfully");
    return result;
}
#endif

// ============================================================================
// Tool Registry - MOVED to src/tools/tool_registry.c
// ============================================================================
// Tool structure, tools array, and wrapper functions have been extracted to:
//   src/tools/tool_registry.h
//   src/tools/tool_registry.c

// ============================================================================
// Tool Executor - MOVED to src/tools/tool_executor.c
// ============================================================================
// execute_tool() and is_tool_allowed() have been extracted to:
//   src/tools/tool_executor.h
//   src/tools/tool_executor.c


// ============================================================================
// Context Building - Environment and Git Information
// ============================================================================

// Get current date in YYYY-MM-DD format
// TEMPORARY: get_current_date moved to src/util/timestamp_utils.c
// This duplicate definition will be removed in a later refactoring step

// Check if current directory is a git repository
// REMOVED: is_git_repo, exec_git_command, get_git_status
// Now in src/context/environment.c

// Get OS/Platform information
// TEMPORARY: get_os_version moved to src/util/env_utils.c
// This duplicate definition will be removed in a later refactoring step

// TEMPORARY: get_platform moved to src/util/env_utils.c
// This duplicate definition will be removed in a later refactoring step

// REMOVED: read_klawed_md
// Now in src/context/klawed_md.c

// REMOVED: build_system_prompt
// Now in src/context/system_prompt.c

// REMOVED: Memory Context Injection functions
// Now in src/context/memory_injection.c

// ============================================================================
// Message Management
// ============================================================================

// conversation_state_init, conversation_state_destroy, conversation_state_lock,
// conversation_state_unlock - moved to src/conversation/conversation_state.c

// add_system_message, add_user_message - moved to src/conversation/message_builder.c

// add_assistant_message_openai - moved to src/conversation/message_parser.c

// process_response() moved to src/interactive/response_processor.c
// ai_worker_handle_instruction() moved to src/interactive/response_processor.c

// ============================================================================
// Advanced Input Handler (readline-like)
// ============================================================================














// ============================================================================
// Interactive Mode
// ============================================================================

// InteractiveContext, interrupt_callback, handle_vim_command, submit_input_callback,
// and interactive_mode() moved to src/interactive/

// ============================================================================
// Single Command Mode (Oneshot)
// ============================================================================

// ============================================================================
// Session ID Generation
// ============================================================================

// ============================================================================
// Session ID Generation
// ============================================================================

// Generate a unique session ID using timestamp and random data
// Helper function to get integer value from environment variable with default
// TEMPORARY: get_env_int_retry moved to src/util/env_utils.c
// This duplicate definition will be removed in a later refactoring step

#ifndef TEST_BUILD
#include "dump_utils.h"
// Dump conversation from database by session ID
// MOVED TO: src/session/session_persistence.c (session_dump_conversation)
#endif // TEST_BUILD

// Format: sess_<timestamp>_<random>
// Returns: Newly allocated string (caller must free)
// TEMPORARY: generate_session_id moved to src/util/timestamp_utils.c
// This duplicate definition will be removed in a later refactoring step


// Main Entry Point
#ifndef TEST_BUILD
// ============================================================================

int main(int argc, char *argv[]) {
    // Handle version flag first (no API key needed)
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("Klawed version %s\n", KLAWED_VERSION_FULL);
        return 0;
    }

    // Handle help flag first (no API key needed)
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("Klawed - Pure C Implementation (OpenAI Compatible)\n");
        printf("Version: %s\n\n", KLAWED_VERSION_FULL);
        printf("Usage:\n");
        printf("  %s                               Start interactive mode\n", argv[0]);
        printf("  %s \"PROMPT\"                       Execute single command and exit\n", argv[0]);
        printf("  %s -d, --dump-conversation [ID]  Dump conversation to console and exit\n", argv[0]);
        printf("                                      (defaults to most recent session if no ID given)\n");
        printf("  %s -dj, --dump-json [ID]         Dump conversation as JSON\n", argv[0]);
        printf("  %s -dm, --dump-md [ID]           Dump conversation as Markdown\n", argv[0]);
        printf("  %s -r, --resume [ID]             Resume a previous conversation session\n", argv[0]);
        printf("                                      (defaults to most recent session if no ID given)\n");
        printf("  %s -l, --list-sessions [N]       List available sessions (N = max to show)\n", argv[0]);
        printf("  %s -p, --provider NAME           Use named provider configuration (from .klawed/config.json)\n", argv[0]);

        printf("  %s -h, --help                     Show this help message\n", argv[0]);
        printf("  %s --auto-compact               Enable automatic context compaction\n", argv[0]);
        printf("  %s --perpetual                  Run in perpetual mode (stateless + persistent log)\n", argv[0]);
        printf("  %s --version                      Show version information\n\n", argv[0]);
        printf("Environment Variables:\n");
        printf("  API Configuration:\n");
        printf("    OPENAI_API_KEY       Required: Your OpenAI API key (not needed for Bedrock)\n");
        printf("    OPENAI_API_BASE      Optional: API base URL (default: %s)\n", API_BASE_URL);
        printf("    OPENAI_MODEL         Optional: Model name (default: %s)\n", DEFAULT_MODEL);
        printf("    KLAWED_LLM_PROVIDER  Optional: Select named provider from config (e.g., \"anthropic-sonnet\")\n");
        printf("                            Takes precedence over active_provider in config\n");
        /* printf("    DISABLE_PROMPT_CACHING  Optional: Set to 1 to disable prompt caching\n\n"); */
        printf("  AWS Bedrock Configuration:\n");
        printf("    KLAWED_USE_BEDROCK  Set to 1 to use AWS Bedrock instead of OpenAI\n");
        printf("    ANTHROPIC_MODEL         Required for Bedrock: Claude model ID\n");
        printf("                            Examples: anthropic.claude-3-sonnet-20240229-v1:0\n");
        printf("                                      us.anthropic.claude-sonnet-4-5-20250929-v1:0\n");
        printf("    AWS credentials        Required: Configure via AWS CLI or environment\n\n");
        printf("  Logging and Persistence:\n");
        printf("    KLAWED_LOG_PATH    Optional: Full path to log file\n");
        printf("    KLAWED_LOG_DIR     Optional: Directory for logs (uses klawed.log filename)\n");
        printf("    KLAWED_LOG_LEVEL     Optional: Log level (FINE, DEBUG, INFO, WARN, ERROR)\n");
        printf("    KLAWED_DB_PATH     Optional: Path to SQLite database for API history\n");
        printf("                         Default: ~/.local/share/klawed/api_calls.db\n");
        printf("    KLAWED_MAX_RETRY_DURATION_MS  Optional: Maximum retry duration in milliseconds\n");
        printf("                                     Default: 600000 (10 minutes)\n");
        printf("    KLAWED_NO_STORAGE  Optional: Set to 1 to disable all storage (SQLite databases\n");
        printf("                         and history file). Disables API history, token tracking,\n");
        printf("                         and memory database. Diagnostic mode for TUI hangs\n\n");
        printf("  UI Customization:\n");
        printf("    KLAWED_THEME       Optional: Path to Kitty theme file\n\n");
        printf("  Tools and Output:\n");
        printf("    KLAWED_ONESHOT_FORMAT  Optional: Output format for one-shot mode\n");
        printf("                            human (clean, default) or json/machine (HTML+JSON)\n");
        printf("    KLAWED_BASH_TIMEOUT    Optional: Timeout for bash commands in seconds\n");
        printf("                            (default: 30, 0=no timeout)\n");
        printf("    KLAWED_GREP_MAX_RESULTS Optional: Max grep results (default: 100)\n");
        printf("    KLAWED_GREP_DISPLAY_LIMIT Optional: Max grep results to display (default: 20)\n");
        printf("    KLAWED_GLOB_DISPLAY_LIMIT Optional: Max glob results to display (default: 10)\n");
        printf("    KLAWED_AUTO_COMPACT       Optional: Enable automatic context compaction (1=true, 0=false)\n");
        printf("    KLAWED_COMPACT_THRESHOLD  Optional: Trigger compaction at this %% of max messages (default: 75)\n");
        printf("    KLAWED_COMPACT_KEEP_RECENT Optional: Number of recent messages to keep (default: 100)\n\n");
        printf("    KLAWED_PERPETUAL          Optional: Set to 1 to enable perpetual mode\n");
        printf("    KLAWED_PERPETUAL_FILE     Optional: Path to perpetual log (default: .klawed/perpetual.md)\n\n");

        printf("  SQLite Queue Mode:\n");
        printf("    KLAWED_SQLITE_DB_PATH  Optional: Path to SQLite database for message queue\n");
        printf("    KLAWED_SQLITE_SENDER   Optional: Sender name for messages (default: klawed)\n\n");

        printf("Interactive Tips:\n");
        printf("  Esc/Ctrl+[ to enter Normal mode (vim-style), 'i' to insert\n");
        printf("  Scroll with j/k (line), Ctrl+D/U (half page), gg/G (top/bottom)\n");
        printf("  Or use PageUp/PageDown or Arrow keys to scroll\n");
        printf("  Type /help for commands (e.g., /clear, /exit, /add-dir, /voice)\n");
        printf("  Press Ctrl+C to cancel a running API/tool action\n\n");
        return 0;
    }

    // Check for provider flag
    const char *provider_from_cli = NULL;
    int provider_flag_argc = 0;  // Track how many args consumed by -p/--provider
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--provider") == 0) {
            if (i + 1 < argc) {
                provider_from_cli = argv[i + 1];
                provider_flag_argc = 2;  // -p and the provider name
                // Set the environment variable so provider_init picks it up
                if (setenv("KLAWED_LLM_PROVIDER", provider_from_cli, 1) != 0) {
                    LOG_WARN("Failed to set KLAWED_LLM_PROVIDER from --provider flag: %s", strerror(errno));
                } else {
                    LOG_INFO("Using provider from --provider flag: %s", provider_from_cli);
                }
                break;
            } else {
                LOG_ERROR("--provider/-p flag requires a provider name argument");
                fprintf(stderr, "Error: --provider/-p flag requires a provider name\n");
                return 1;
            }
        }
    }

    // Load unified provider configuration ONCE at startup
    // This converts environment variables into provider config and determines
    // the effective provider to use. All subsequent provider lookups use this.
    UnifiedProviderConfig unified_config;
    provider_config_load(&unified_config);

    // Check for dump conversation flags (can appear anywhere in argv)
#ifndef TEST_BUILD
    const char *dump_session_id = NULL;
    const char *dump_format = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dump-conversation") == 0) {
            dump_format = "default";
            // Check if next arg is a session ID (not a flag)
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                dump_session_id = argv[i + 1];
            }
            break;
        } else if (strcmp(argv[i], "-dj") == 0 || strcmp(argv[i], "--dump-json") == 0) {
            dump_format = "json";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                dump_session_id = argv[i + 1];
            }
            break;
        } else if (strcmp(argv[i], "-dm") == 0 || strcmp(argv[i], "--dump-md") == 0) {
            dump_format = "markdown";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                dump_session_id = argv[i + 1];
            }
            break;
        }
    }
    if (dump_format) {
        return session_dump_conversation(dump_session_id, dump_format);
    }
#endif

    // Check for resume session flag (can appear anywhere in argv)
#ifndef TEST_BUILD
    int resume_session = 0;
    const char *resume_session_id = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--resume") == 0) {
            resume_session = 1;
            // Check if next arg is a session ID (not a flag)
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                resume_session_id = argv[i + 1];
            }
            // NULL = most recent session
            LOG_INFO("Resume session mode enabled, session_id: %s", resume_session_id ? resume_session_id : "most recent");
            break;
        }
    }
#endif

    // Check for list sessions flag (can appear anywhere in argv)
#ifndef TEST_BUILD
    int list_sessions = 0;
    int session_limit = 10;  // Default limit
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list-sessions") == 0) {
            list_sessions = 1;
            // Check if next arg is a limit number (not a flag)
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                session_limit = atoi(argv[i + 1]);
                if (session_limit <= 0) {
                    session_limit = 0;  // 0 means no limit
                }
            }
            LOG_INFO("List sessions mode enabled, limit: %d", session_limit);
            break;
        }
    }
#endif

    // Check for ZMQ daemon mode


    // Check for auto-compact flag (can appear anywhere in argv)
    int auto_compact_enabled = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "--auto-compact") == 0) {
            auto_compact_enabled = 1;
            LOG_INFO("Auto-compact mode enabled via command line flag");
            break;
        }
    }
    // Also check environment variable
    if (!auto_compact_enabled) {
        const char *auto_compact_env = getenv("KLAWED_AUTO_COMPACT");
        if (auto_compact_env && (strcmp(auto_compact_env, "1") == 0 ||
                               strcasecmp(auto_compact_env, "true") == 0 ||
                               strcasecmp(auto_compact_env, "yes") == 0)) {
            auto_compact_enabled = 1;
            LOG_INFO("Auto-compact mode enabled via environment variable");
        }
    }

    // Check for perpetual mode flag (can appear anywhere in argv)
    int is_perpetual_mode = 0;
    int perpetual_flag_argc = 0;  // Track how many args consumed by --perpetual
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "--perpetual") == 0) {
            is_perpetual_mode = 1;
            perpetual_flag_argc = 1;
            LOG_INFO("Perpetual mode enabled via command line flag");
            break;
        }
    }
    // Also check environment variable
    if (!is_perpetual_mode) {
        const char *perpetual_env = getenv("KLAWED_PERPETUAL");
        if (perpetual_env && (strcmp(perpetual_env, "1") == 0 ||
                              strcasecmp(perpetual_env, "true") == 0 ||
                              strcasecmp(perpetual_env, "yes") == 0)) {
            is_perpetual_mode = 1;
            LOG_INFO("Perpetual mode enabled via environment variable");
        }
    }

    // Check for SQLite queue daemon mode
    int sqlite_daemon_mode = 0;
    const char *sqlite_db_path = NULL;
    const char *sqlite_sender_name = NULL;
    int sqlite_queue_flag_argc = 0;  // Track how many args consumed by --sqlite-queue

    // Search for --sqlite-queue anywhere in argv (not just position 1)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sqlite-queue") == 0) {
            if (i + 1 < argc) {
                sqlite_daemon_mode = 1;
                sqlite_db_path = argv[i + 1];
                sqlite_queue_flag_argc = 2;  // --sqlite-queue and the db path
                LOG_INFO("SQLite queue daemon mode enabled, database: %s", sqlite_db_path);
            } else {
                LOG_ERROR("--sqlite-queue flag requires a database path argument");
                fprintf(stderr, "Error: --sqlite-queue flag requires a database path\n");
                return 1;
            }
            break;
        }
    }

    // Also check environment variable for SQLite queue
    if (!sqlite_db_path) {
        sqlite_db_path = getenv("KLAWED_SQLITE_DB_PATH");
        if (sqlite_db_path) {
            // If KLAWED_SQLITE_DB_PATH is set, automatically run in daemon mode
            sqlite_daemon_mode = 1;
        }
    }

    // Get sender name (default: klawed)
    if (!sqlite_sender_name) {
        sqlite_sender_name = getenv("KLAWED_SQLITE_SENDER");
    }
    if (!sqlite_sender_name) {
        sqlite_sender_name = "klawed";
    }

    // Handle list sessions mode
#ifndef TEST_BUILD
    if (list_sessions) {
        // Initialize persistence database
        PersistenceDB *db = persistence_init(NULL);
        if (!db) {
            fprintf(stderr, "Error: Failed to open persistence database\n");
            return 1;
        }

        int result = session_list_sessions(db, session_limit);
        persistence_close(db);
        return result == 0 ? 0 : 1;
    }
#endif

    // Check for single command mode: ./klawed "prompt"
    // Also support: ./klawed -p provider "prompt"
    int is_single_command_mode = 0;
    char *single_command = NULL;
    int socket_ipc_enabled = 0;
    if (sqlite_daemon_mode) {
        socket_ipc_enabled = 1;
    }

    // Compute effective argc after removing flag arguments
    // --provider/-p and --sqlite-queue consume 2 args each; --perpetual consumes 1
    int effective_argc = argc - provider_flag_argc - sqlite_queue_flag_argc - perpetual_flag_argc;

    if (effective_argc == 2 && !resume_session && !list_sessions && !socket_ipc_enabled) {
        // Single argument provided - treat as prompt for single command mode
        // (but not if it's a resume flag without session ID)
        // Find the prompt argument (the one that's not part of -p flag)
        is_single_command_mode = 1;
        // Find the prompt by scanning argv and skipping all known flags and their args.
        // This handles any combination of --perpetual, -p/--provider, --sqlite-queue, etc.
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--provider") == 0 ||
                strcmp(argv[i], "--sqlite-queue") == 0) {
                i++;  // Skip the associated value argument too
                continue;
            }
            if (strcmp(argv[i], "--perpetual") == 0 ||
                strcmp(argv[i], "--auto-compact") == 0) {
                continue;
            }
            single_command = argv[i];
            break;
        }
        LOG_INFO("Single command mode enabled with prompt: %s", single_command);
    } else if (effective_argc > 2 && !resume_session && !list_sessions && !socket_ipc_enabled) {
        LOG_ERROR("Unexpected arguments provided");
        printf("Try '%s --help' for usage information.\n", argv[0]);
        return 1;
    }

#ifndef TEST_BUILD
    // Check if Bedrock mode is enabled using unified config
    int use_bedrock = provider_config_is_bedrock(&unified_config);
#else
    int use_bedrock = 0;
#endif

    const char *api_key = NULL;
    const char *api_key_source = NULL;
    const char *api_base = NULL;
    const char *model = NULL;

    // Get model from unified config
    model = provider_config_get_model(&unified_config);
    if (!model) {
        // Fallback to environment variables
        model = getenv("OPENAI_MODEL");
        if (!model) {
            model = getenv("ANTHROPIC_MODEL");
        }
    }

    if (use_bedrock) {
        // Bedrock mode: API key not required, credentials loaded separately
        if (!model) {
            LOG_ERROR("ANTHROPIC_MODEL environment variable required when using AWS Bedrock");
            fprintf(stderr, "Error: ANTHROPIC_MODEL environment variable not set\n");
            fprintf(stderr, "Example: export ANTHROPIC_MODEL=us.anthropic.claude-sonnet-4-5-20250929-v1:0\n");
            return 1;
        }
        LOG_INFO("Bedrock mode enabled, using model: %s", model);
        // API key and base URL will be handled by Bedrock module
        api_key = "bedrock";  // Placeholder
        api_base = "bedrock"; // Will be overridden by Bedrock endpoint
    } else {
        // Standard mode: check for API key
        api_key = provider_config_resolve_api_key(&unified_config, &api_key_source);
        if (!api_key) {
            // Try OPENAI_API_KEY directly
            api_key = getenv("OPENAI_API_KEY");
            if (api_key) {
                api_key_source = "OPENAI_API_KEY";
            }
        }

        if (!api_key) {
            // OAuth providers (openai_sub, kimi_coding_plan, anthropic_sub) authenticate via
            // device/PKCE flow and do not require an API key at startup.
            if (provider_config_is_oauth(&unified_config)) {
                api_key = "oauth";  /* Placeholder; real auth happens at first API call */
                api_key_source = "oauth";
            } else {
                LOG_ERROR("OPENAI_API_KEY environment variable not set");
                fprintf(stderr, "Error: OPENAI_API_KEY environment variable not set\n");
                fprintf(stderr, "\nTo use AWS Bedrock instead, set:\n");
                fprintf(stderr, "  export KLAWED_USE_BEDROCK=true\n");
                fprintf(stderr, "  export ANTHROPIC_MODEL=us.anthropic.claude-sonnet-4-5-20250929-v1:0\n");
                fprintf(stderr, "  export AWS_REGION=us-west-2\n");
                fprintf(stderr, "  export AWS_PROFILE=your-profile\n");
                return 1;
            }
        }

        // Use placeholder if provider has its own key configured (resolved later in provider_init)
        if (strcmp(api_key_source, "config file") == 0) {
            api_key = "provider-config";  // Placeholder, actual key resolved in provider_init
        }

        // Get API base from unified config or environment
        api_base = provider_config_get_api_base(&unified_config);
        if (!api_base) {
            api_base = getenv("OPENAI_API_BASE");
        }
        if (!api_base) {
            api_base = API_BASE_URL;
        }

        // Ensure we have a model
        if (!model) {
            model = DEFAULT_MODEL;
        }
    }

    // Initialize CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // arc4random is self-seeding, no initialization needed

    // Initialize logging system
    if (log_init() != 0) {
        LOG_ERROR("Warning: Failed to initialize logging system");
    }

    // Configure log rotation: 10MB max size, keep 5 backups
    log_set_rotation(10, 5);

    // Set log level from environment or default to INFO
    const char *log_level_env = getenv("KLAWED_LOG_LEVEL");
    if (log_level_env) {
        if (strcmp(log_level_env, "FINE") == 0) {
            log_set_level(LOG_LEVEL_FINE);
        } else if (strcmp(log_level_env, "DEBUG") == 0) {
            log_set_level(LOG_LEVEL_DEBUG);
        } else if (strcmp(log_level_env, "WARN") == 0) {
            log_set_level(LOG_LEVEL_WARN);
        } else if (strcmp(log_level_env, "ERROR") == 0) {
            log_set_level(LOG_LEVEL_ERROR);
        }
    }

    // Export KLAWED_PID for child processes (e.g., web_browse_agent driver)
    // This allows browser sessions to monitor klawed's lifetime and clean up when it exits
    {
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%d", getpid());
        setenv("KLAWED_PID", pid_str, 1);
    }

    LOG_INFO("Application started");
    LOG_INFO("API URL: %s", api_base);
    LOG_INFO("Model: %s", model);

    // Validate provider configuration
    char *provider_error = provider_config_validate(&unified_config);
    if (provider_error) {
        // Error message format depends on run mode
        if (is_single_command_mode) {
            // One-shot mode: print to stderr and exit
            fprintf(stderr, "%s\n", provider_error);
            free(provider_error);
            return 1;
        } else {
            // Interactive mode: will show error in TUI
            // For now, print to stderr before TUI starts
            fprintf(stderr, "%s\n", provider_error);
            fprintf(stderr, "Press Enter to continue anyway or Ctrl+C to exit...\n");
            getchar();  // Wait for user acknowledgment
            free(provider_error);
        }
    }

    // Initialize colorscheme EARLY (before any colored output/spinners)
    // Priority: KLAWED_THEME env var > config file theme > default "tender"
    const char *theme = getenv("KLAWED_THEME");
    if (!theme || strlen(theme) == 0) {
        // Use theme from unified config (already loaded from files)
        if (unified_config.base_config.theme[0] != '\0') {
            theme = unified_config.base_config.theme;
            LOG_DEBUG("[Config] Using theme from config file: %s", theme);
        }
    }
    if (theme && strlen(theme) > 0) {
        // Theme can be:
        // 1. Built-in theme name (e.g., "dracula", "gruvbox-dark")
        // 2. Built-in with .conf extension (e.g., "dracula.conf")
        // 3. Absolute/relative path to external .conf file
        if (init_colorscheme(theme) != 0) {
            LOG_WARN("Failed to load colorscheme '%s', will use ANSI fallback colors", theme);
        } else {
            LOG_DEBUG("Colorscheme loaded successfully: %s", theme);
        }
    } else {
        // Try to load default built-in theme
        if (init_colorscheme("tender") != 0) {
            LOG_DEBUG("No default colorscheme found, using ANSI fallback colors");
        } else {
            LOG_DEBUG("Default colorscheme loaded: tender");
        }
    }

    // DISABLED: Blocking initialization moved to background loading
    // Initialize persistence layer
    // PersistenceDB *persistence_db = persistence_init(NULL);  // NULL = use default path
    // if (persistence_db) {
    //     LOG_INFO("Persistence layer initialized");
    // } else {
    //     LOG_WARN("Failed to initialize persistence layer - API calls will not be logged");
    // }
    // Note: Will be initialized by background loader and set in state.persistence_db

#ifndef TEST_BUILD
    // Initialize MCP (Model Context Protocol) subsystem
    if (mcp_init() == 0) {
        LOG_INFO("MCP subsystem initialized");
    } else {
        LOG_WARN("Failed to initialize MCP subsystem");
    }
#endif

    // Start background loading of expensive resources (persistence DB, memory_db, system prompt)
    // This allows the TUI to start immediately while these resources load asynchronously
    LOG_DEBUG("Starting background resource loaders");

    // Generate unique session ID for this conversation
    char *session_id = generate_session_id();
    if (!session_id) {
        LOG_WARN("Failed to generate session ID");
    }
    LOG_INFO("Session ID: %s", session_id ? session_id : "none");

    // Set session ID for logging
    if (session_id) {
        log_set_session_id(session_id);
    }

    // Initialize conversation state
    ConversationState state = {0};
    int exit_code = 0;  // Declared early for use in error paths

    if (conversation_state_init(&state) != 0) {
        LOG_ERROR("Failed to initialize conversation state synchronization");
        fprintf(stderr, "Error: Unable to initialize conversation state\n");
        free(session_id);
        // Note: persistence_db and memory_db are initialized by background loaders
        // which haven't started yet at this point
        curl_global_cleanup();
        log_shutdown();
        return 1;
    }
    state.api_key = strdup(api_key);
    state.api_url = strdup(api_base);
    state.model = strdup(model);
    state.max_tokens = get_env_int_retry("KLAWED_MAX_TOKENS", MAX_TOKENS);

    // Note: model is now a pointer to unified_config data or env var, no need to free

    // Note: DeepSeek API max_tokens override removed - no longer limiting to 4096

    // Get current working directory - use PATH_MAX to satisfy static analyzer
    char cwd_buf[PATH_MAX];
    char *cwd = getcwd(cwd_buf, sizeof(cwd_buf));
    state.working_dir = cwd ? strdup(cwd) : NULL;

    state.session_id = session_id;
    state.persistence_db = NULL;  // Will be set by background loader
    state.max_retry_duration_ms = get_env_int_retry("KLAWED_MAX_RETRY_DURATION_MS", MAX_RETRY_DURATION_MS);
    state.bg_loaders = NULL;  // Will be set by start_background_loaders()

    // Initialize todo list
    state.todo_list = calloc(1, sizeof(TodoList));  // Use calloc to zero-initialize

    // Initialize compaction configuration
    if (auto_compact_enabled) {
        state.compaction_config = malloc(sizeof(CompactionConfig));
        if (state.compaction_config) {
            compaction_init_config(state.compaction_config, auto_compact_enabled, state.model);
            LOG_INFO("Compaction configuration initialized (model: %s, limit: %d tokens)",
                     state.model ? state.model : "(unknown)",
                     state.compaction_config->model_token_limit);
        } else {
            LOG_ERROR("Failed to allocate memory for compaction config");
        }
    }
    if (state.todo_list) {
        if (todo_init(state.todo_list) == 0) {
            LOG_DEBUG("Todo list initialized");
        } else {
            LOG_ERROR("Failed to initialize todo list");
            // todo_list is in a safe state (zero-initialized from calloc)
            // but we should probably exit or handle this error
        }
    } else {
        LOG_ERROR("Failed to allocate memory for todo list");
    }

    // Start background loading of expensive resources (persistence DB, memory_db, system prompt)
    // This must happen after state initialization but before session resume or execution modes
    if (start_background_loaders(&state) != 0) {
        LOG_WARN("Failed to start background loaders, resources will be loaded synchronously");
    }

#ifndef TEST_BUILD
    // Resume session if requested - must await database first
    if (resume_session) {
        // Wait for database to be ready (required for session resume)
        state.persistence_db = await_database_ready(&state);

        if (state.persistence_db) {
            LOG_INFO("Attempting to resume session: %s", resume_session_id ? resume_session_id : "most recent");

            // Load session from database
            // Note: session_load_from_db takes ownership of state->session_id (frees the old one)
            // so we must set local session_id to NULL to avoid double-free
            if (session_load_from_db(state.persistence_db, resume_session_id, &state) == 0) {
                LOG_INFO("Successfully resumed session: %s", state.session_id);
                session_id = NULL;  // state.session_id is now the active session ID (old one was freed)

                // Update session ID for logging
                if (state.session_id) {
                    log_set_session_id(state.session_id);
                }
            } else {
                LOG_ERROR("Failed to resume session");
                if (resume_session_id) {
                    fprintf(stderr, "Error: Failed to resume session '%s'. Session may not exist.\n", resume_session_id);
                } else {
                    fprintf(stderr, "Error: Failed to resume most recent session. No sessions found in database.\n");
                }
                exit_code = 1;
            }
        } else {
            LOG_ERROR("Database not available for session resume");
            fprintf(stderr, "Error: Database not available. Cannot resume session.\n");
            exit_code = 1;
        }
    }
#endif

    // Continue with initialization if session resume succeeded (or wasn't requested)
    if (exit_code == 0) {
#ifndef TEST_BUILD
        // Defer provider initialization to first API call to avoid blocking TUI startup.
        // state.api_url currently points at base URL from env; it will be replaced on init.
        state.provider = NULL;

        // Load MCP configuration if enabled
        if (mcp_is_enabled()) {
            LOG_DEBUG("MCP: MCP is enabled, loading configuration");
            const char *mcp_config_path = getenv("KLAWED_MCP_CONFIG");
            LOG_DEBUG("MCP: Using config path: %s", mcp_config_path ? mcp_config_path : "(default)");
            state.mcp_config = mcp_load_config(mcp_config_path);

            if (state.mcp_config) {
                LOG_INFO("MCP: Loaded %d server(s) from config", state.mcp_config->server_count);

                // Connect to all configured servers
                for (int i = 0; i < state.mcp_config->server_count; i++) {
                    MCPServer *server = state.mcp_config->servers[i];
                    LOG_DEBUG("MCP: Attempting to connect to server '%s'", server->name);
                    if (mcp_connect_server(server) == 0) {
                        LOG_DEBUG("MCP: Connected to server '%s', discovering tools", server->name);
                        // Discover tools from connected server
                        int tool_count = mcp_discover_tools(server);
                        if (tool_count > 0) {
                            LOG_INFO("MCP: Server '%s' provides %d tool(s)", server->name, tool_count);
                            // Log individual tool names
                            for (int j = 0; j < tool_count; j++) {
                                if (server->tools[j]) {
                                    LOG_DEBUG("MCP: Server '%s' tool %d: '%s'", server->name, j, server->tools[j]);
                                }
                            }
                        } else if (tool_count == 0) {
                            LOG_DEBUG("MCP: Server '%s' provides no tools", server->name);
                        } else {
                            LOG_WARN("MCP: Failed to discover tools from server '%s'", server->name);
                        }
                    } else {
                        LOG_WARN("MCP: Failed to connect to server '%s'", server->name);
                    }
                }

                // Log status
                char *status = mcp_get_status(state.mcp_config);
                if (status) {
                    LOG_INFO("MCP Status: %s", status);
                    free(status);
                }
            } else {
                LOG_DEBUG("MCP: No servers configured or failed to load config");
            }
        } else {
            state.mcp_config = NULL;
            LOG_DEBUG("MCP: Disabled (set KLAWED_MCP_ENABLED=1 to enable)");
        }
#else
        state.provider = NULL;
        state.mcp_config = NULL;
#endif

        // Check for allocation failures
        if (exit_code == 0 && (!state.api_key || !state.api_url || !state.model || !state.todo_list)) {
            LOG_ERROR("Failed to allocate memory for conversation state");
            fprintf(stderr, "Error: Memory allocation failed\n");
            exit_code = 1;
        }

        if (exit_code == 0 && !state.working_dir) {
            LOG_ERROR("Failed to get current working directory");
            exit_code = 1;
        }

        if (exit_code == 0) {
            LOG_INFO("API URL initialized: %s", state.api_url);
        }

        // DISABLED: System prompt building moved to background loading
        // This expensive operation (scanning git, environment, directories) is now done asynchronously
        // Build and add system prompt with environment context
        // char *system_prompt = build_system_prompt(&state);
        // if (system_prompt) {
        //     add_system_message(&state, system_prompt);
        //     free(system_prompt);
        //     LOG_DEBUG("System prompt added with environment context");
        //
        //     // Note: Memory context injection is now done before each API call in api_client.c
        //     // This ensures the memory context is always fresh and up-to-date
        //
        //     // Debug: print system prompt if DEBUG_PROMPT environment variable is set
        //     if (getenv("DEBUG_PROMPT")) {
        //         // Print the final system prompt (may include injected memory context)
        //         if (state.count > 0 && state.messages[0].role == MSG_SYSTEM &&
        //             state.messages[0].content_count > 0 && state.messages[0].contents[0].text) {
        //             printf("\n=== SYSTEM PROMPT (DEBUG) ===\n%s\n=== END SYSTEM PROMPT ===\n\n",
        //                    state.messages[0].contents[0].text);
        //         }
        //     }
        // } else {
        //     LOG_WARN("Failed to build system prompt");
        // }
        // Note: System prompt will be added by background loader when ready

        // Run in appropriate mode
        if (sqlite_daemon_mode) {
            // SQLite queue daemon mode
            LOG_INFO("Starting SQLite queue daemon mode on %s", sqlite_db_path);
            SQLiteQueueContext *sqlite_ctx = sqlite_queue_init(sqlite_db_path, sqlite_sender_name);
            if (!sqlite_ctx) {
                LOG_ERROR("Failed to initialize SQLite queue");
                exit_code = 1;
            } else {
                sqlite_ctx->daemon_mode = true;
                state.sqlite_queue_context = sqlite_ctx;
                exit_code = sqlite_queue_daemon_mode(sqlite_ctx, &state);
                state.sqlite_queue_context = NULL;
                sqlite_queue_cleanup(sqlite_ctx);
            }
        }

        // Skip main execution if there was an error
        if (exit_code == 0) {
            if (is_perpetual_mode && is_single_command_mode) {
                // One-shot perpetual: prompt given on CLI, run once and exit
                exit_code = perpetual_mode_run(&state, single_command, NULL);
            } else if (is_perpetual_mode) {
                // Interactive perpetual: TUI loop where each submission runs a
                // fresh perpetual_mode_run via the worker thread
                state.is_perpetual_mode = 1;
                interactive_mode(&state);
            } else if (is_single_command_mode) {
                exit_code = oneshot_execute(&state, single_command);
            } else {
                interactive_mode(&state);
            }
        }
    }

    // Cleanup background loaders (wait for threads to complete)
    cleanup_background_loaders(&state);

    // Cleanup conversation messages
    conversation_free(&state);

    // Free additional directories
    for (int i = 0; i < state.additional_dirs_count; i++) {
        free(state.additional_dirs[i]);
    }
    free(state.additional_dirs);

    // Cleanup todo list
    if (state.todo_list) {
        todo_free(state.todo_list);
        free(state.todo_list);
        state.todo_list = NULL;
        LOG_DEBUG("Todo list cleaned up");
    }

#ifndef TEST_BUILD
    // Cleanup provider
    if (state.provider) {
        state.provider->cleanup(state.provider);
        LOG_DEBUG("Provider cleaned up");
    }

    // Cleanup MCP configuration
    if (state.mcp_config) {
        mcp_free_config(state.mcp_config);
        state.mcp_config = NULL;
        LOG_DEBUG("MCP configuration cleaned up");
    }
#endif

    // Print session ID to console on exit
    if (state.session_id) {
        fprintf(stderr, "\nSession ID: %s\n", state.session_id);
    }

    free(state.api_key);
    free(state.api_url);
    free(state.model);
    free(state.working_dir);
    free(state.session_id);
    conversation_state_destroy(&state);

    // Close persistence layer
    if (state.persistence_db) {
        persistence_close(state.persistence_db);
        LOG_INFO("Persistence layer closed");
    }

#ifndef TEST_BUILD
    // Clean up MCP subsystem
    mcp_cleanup();
    LOG_INFO("MCP subsystem cleaned up");
#endif

    // Clean up memory database
    memory_db_cleanup_global();
    LOG_INFO("Memory database cleaned up");

    curl_global_cleanup();

    LOG_INFO("Application terminated");
    log_shutdown();

    return exit_code;
}

#endif // TEST_BUILD

#ifdef TEST_BUILD
#pragma GCC diagnostic pop
#endif
