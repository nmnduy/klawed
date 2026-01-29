/*
 * sqlite_queue.c - SQLite message queue implementation for Klawed
 */

#include "sqlite_queue.h"
#include "klawed_internal.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>
#include <ctype.h>
#include <bsd/string.h>
#include <bsd/stdlib.h>
#include <assert.h>

// Default buffer size for SQLite messages
#define SQLITE_QUEUE_BUFFER_SIZE 65536

// Default configuration values
#define DEFAULT_POLL_INTERVAL 300      // 300ms
#define DEFAULT_POLL_TIMEOUT 30000     // 30 seconds
#define DEFAULT_MAX_RETRIES 3
#define DEFAULT_MAX_MESSAGE_SIZE (1024 * 1024)  // 1MB
#define DEFAULT_MAX_QUEUE_SIZE 1000
#define DEFAULT_MAX_ITERATIONS 1000    // Increased from 50 for more complex tasks

// SQL statements
#define CREATE_MESSAGES_TABLE_SQL \
    "CREATE TABLE IF NOT EXISTS messages (" \
    "id INTEGER PRIMARY KEY AUTOINCREMENT," \
    "sender TEXT NOT NULL," \
    "receiver TEXT NOT NULL," \
    "message TEXT NOT NULL," \
    "sent INTEGER DEFAULT 0," \
    "created_at INTEGER DEFAULT (strftime('%s', 'now'))," \
    "updated_at INTEGER DEFAULT (strftime('%s', 'now'))" \
    ");"

#define CREATE_INDEX_SENDER_SQL \
    "CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender, sent);"
#define CREATE_INDEX_RECEIVER_SQL \
    "CREATE INDEX IF NOT EXISTS idx_messages_receiver ON messages(receiver, sent);"

// Insert message
#define INSERT_MESSAGE_SQL \
    "INSERT INTO messages (sender, receiver, message, sent) VALUES (?, ?, ?, 0);"

// Select messages for receiver
#define SELECT_MESSAGES_SQL \
    "SELECT id, message FROM messages WHERE receiver = ? AND sent = 0 ORDER BY created_at ASC LIMIT ?;"

// Acknowledge message
#define ACK_MESSAGE_SQL \
    "UPDATE messages SET sent = 1, updated_at = strftime('%s', 'now') WHERE id = ?;"

// Get statistics
#define COUNT_PENDING_SQL \
    "SELECT COUNT(*) FROM messages WHERE sent = 0;"
#define COUNT_TOTAL_SQL \
    "SELECT COUNT(*) FROM messages;"
#define COUNT_UNREAD_SQL \
    "SELECT COUNT(*) FROM messages WHERE sender = ? AND sent = 0;"

// Select seed messages for conversation history (most recent 100, ordered chronologically)
// Returns messages where sent=1 (already processed) for seeding conversation at boot
// Note: We now load all message types (TEXT, TOOL, TOOL_RESULT) and pair them properly
#define SELECT_SEED_MESSAGES_SQL \
    "SELECT sender, message FROM (" \
    "  SELECT sender, message, created_at FROM messages " \
    "  WHERE sent = 1 AND messageType_filter = 'TEXT' " \
    "  ORDER BY created_at DESC LIMIT 100" \
    ") ORDER BY created_at ASC;"

// Simpler version that works without messageType filtering (we filter in code)
// Loads all message types so we can properly pair TOOL with TOOL_RESULT
#define SELECT_SEED_MESSAGES_SIMPLE_SQL \
    "SELECT sender, message FROM (" \
    "  SELECT sender, message, created_at FROM messages " \
    "  WHERE sent = 1 " \
    "  ORDER BY created_at DESC LIMIT 100" \
    ") ORDER BY created_at ASC;"

// Forward declarations
#ifndef TEST_BUILD
static int sqlite_queue_process_interactive(SQLiteQueueContext *ctx, struct ConversationState *state, const char *user_input);
#endif
static void sqlite_queue_set_error(SQLiteQueueContext *ctx, SQLiteQueueErrorCode error_code, const char *format, ...);
static const char* sqlite_queue_error_to_string(SQLiteQueueErrorCode error_code);
static int sqlite_queue_open_db(SQLiteQueueContext *ctx);
static void sqlite_queue_close_db(SQLiteQueueContext *ctx);
static int sqlite_queue_prepare_statement(SQLiteQueueContext *ctx, sqlite3_stmt **stmt, const char *sql);

SQLiteQueueContext* sqlite_queue_init(const char *db_path, const char *sender_name) {
    // Critical invariant: database path must not be NULL
    assert(db_path != NULL);

    if (!db_path) {
        LOG_ERROR("SQLite Queue: Database path cannot be NULL");
        return NULL;
    }

    LOG_INFO("SQLite Queue: Initializing SQLite queue");
    LOG_DEBUG("SQLite Queue: Database path: %s", db_path);
    LOG_DEBUG("SQLite Queue: Sender name: %s", sender_name ? sender_name : "klawed");

    SQLiteQueueContext *ctx = reallocarray(NULL, 1, sizeof(SQLiteQueueContext));
    if (!ctx) {
        LOG_ERROR("SQLite Queue: Failed to allocate context memory");
        return NULL;
    }
    LOG_DEBUG("SQLite Queue: Allocated SQLite queue context structure");
    // Zero-initialize the context (calloc equivalent)
    memset(ctx, 0, sizeof(SQLiteQueueContext));

    // Copy database path
    ctx->db_path = strdup(db_path);
    if (!ctx->db_path) {
        LOG_ERROR("SQLite Queue: Failed to duplicate database path string");
        free(ctx);
        return NULL;
    }
    LOG_DEBUG("SQLite Queue: Duplicated database path: %s", ctx->db_path);

    // Copy sender name or use default
    ctx->sender_name = strdup(sender_name ? sender_name : "klawed");
    if (!ctx->sender_name) {
        LOG_ERROR("SQLite Queue: Failed to duplicate sender name string");
        free(ctx->db_path);
        free(ctx);
        return NULL;
    }
    LOG_DEBUG("SQLite Queue: Sender name: %s", ctx->sender_name);

    // Initialize timeout configuration from environment variables
    const char *poll_interval_env = getenv("KLAWED_SQLITE_POLL_INTERVAL");
    ctx->poll_interval = poll_interval_env ? atoi(poll_interval_env) : DEFAULT_POLL_INTERVAL;
    if (ctx->poll_interval < 10) ctx->poll_interval = 10; // Minimum 10ms

    const char *poll_timeout_env = getenv("KLAWED_SQLITE_POLL_TIMEOUT");
    ctx->poll_timeout = poll_timeout_env ? atoi(poll_timeout_env) : DEFAULT_POLL_TIMEOUT;

    const char *max_retries_env = getenv("KLAWED_SQLITE_MAX_RETRIES");
    ctx->max_retries = max_retries_env ? atoi(max_retries_env) : DEFAULT_MAX_RETRIES;

    // Initialize message configuration
    const char *max_message_size_env = getenv("KLAWED_SQLITE_MAX_MESSAGE_SIZE");
    ctx->max_message_size = max_message_size_env ? (size_t)atol(max_message_size_env) : DEFAULT_MAX_MESSAGE_SIZE;

    const char *max_queue_size_env = getenv("KLAWED_SQLITE_MAX_QUEUE_SIZE");
    ctx->max_queue_size = max_queue_size_env ? atoi(max_queue_size_env) : DEFAULT_MAX_QUEUE_SIZE;

    // Initialize iteration limit
    const char *max_iterations_env = getenv("KLAWED_SQLITE_MAX_ITERATIONS");
    ctx->max_iterations = max_iterations_env ? atoi(max_iterations_env) : DEFAULT_MAX_ITERATIONS;

    // Initialize state tracking
    ctx->last_poll = 0;
    ctx->retry_count = 0;
    ctx->initialized = false;
    ctx->db_handle = NULL;

    // Initialize error state
    ctx->last_error = SQLITE_QUEUE_ERROR_NONE;
    ctx->error_message[0] = '\0';
    ctx->error_time = 0;

    LOG_DEBUG("SQLite Queue: Poll interval: %dms, Poll timeout: %dms", ctx->poll_interval, ctx->poll_timeout);
    LOG_DEBUG("SQLite Queue: Max retries: %d, Max message size: %zu, Max queue size: %d, Max iterations: %d",
              ctx->max_retries, ctx->max_message_size, ctx->max_queue_size, ctx->max_iterations);

    // Open database and initialize schema
    if (sqlite_queue_open_db(ctx) != 0) {
        LOG_ERROR("SQLite Queue: Failed to open database");
        free(ctx->sender_name);
        free(ctx->db_path);
        free(ctx);
        return NULL;
    }

    if (sqlite_queue_init_schema(ctx) != 0) {
        LOG_ERROR("SQLite Queue: Failed to initialize database schema");
        sqlite_queue_close_db(ctx);
        free(ctx->sender_name);
        free(ctx->db_path);
        free(ctx);
        return NULL;
    }

    ctx->enabled = true;
    ctx->daemon_mode = false;
    ctx->initialized = true;

    LOG_INFO("SQLite Queue: Initialization completed successfully");
    LOG_DEBUG("SQLite Queue: Context enabled: %s", ctx->enabled ? "true" : "false");
    LOG_DEBUG("SQLite Queue: Daemon mode: %s", ctx->daemon_mode ? "true" : "false");

    return ctx;
}

void sqlite_queue_cleanup(SQLiteQueueContext *ctx) {
    // Critical invariant: context must not be NULL (but we handle it gracefully)
    // assert(ctx != NULL);

    if (!ctx) return;

    LOG_INFO("SQLite Queue: Cleaning up SQLite queue context for database: %s",
             ctx->db_path ? ctx->db_path : "unknown");

    // Close database if open
    sqlite_queue_close_db(ctx);

    if (ctx->sender_name) {
        LOG_DEBUG("SQLite Queue: Freeing sender name: %s", ctx->sender_name);
        free(ctx->sender_name);
        ctx->sender_name = NULL;
    }

    if (ctx->db_path) {
        LOG_DEBUG("SQLite Queue: Freeing database path: %s", ctx->db_path);
        free(ctx->db_path);
        ctx->db_path = NULL;
    }

    LOG_DEBUG("SQLite Queue: Freeing SQLite queue context structure");
    free(ctx);
    LOG_INFO("SQLite Queue: Cleanup completed");
}

int sqlite_queue_send(SQLiteQueueContext *ctx, const char *receiver, const char *message, size_t message_len) {
    // Critical invariants
    assert(ctx != NULL);
    assert(receiver != NULL);
    assert(message != NULL);

    if (!ctx || !receiver || !message) {
        sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_INVALID_PARAM, "Invalid parameters for send");
        return SQLITE_QUEUE_ERROR_INVALID_PARAM;
    }

    // Check message size
    if (message_len > ctx->max_message_size) {
        sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_MESSAGE_TOO_LONG,
                              "Message too long: %zu bytes (max: %zu)",
                              message_len, ctx->max_message_size);
        return SQLITE_QUEUE_ERROR_MESSAGE_TOO_LONG;
    }

    // Open database if not already open
    if (!ctx->db_handle && sqlite_queue_open_db(ctx) != 0) {
        return ctx->last_error;
    }

    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    LOG_DEBUG("SQLite Queue: Sending %zu bytes to receiver: %s", message_len, receiver);

    // Prepare insert statement
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite_queue_prepare_statement(ctx, &stmt, INSERT_MESSAGE_SQL);
    if (rc != SQLITE_OK) {
        return rc;
    }

    // Bind parameters
    sqlite3_bind_text(stmt, 1, ctx->sender_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, receiver, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, message, (int)message_len, SQLITE_STATIC);

    // Execute
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_DB_EXECUTE_FAILED,
                              "Failed to insert message: %s", sqlite3_errmsg(db));
        LOG_ERROR("SQLite Queue: Failed to insert message: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return SQLITE_QUEUE_ERROR_DB_EXECUTE_FAILED;
    }

    sqlite3_finalize(stmt);

    LOG_DEBUG("SQLite Queue: Successfully sent %zu bytes to receiver: %s", message_len, receiver);
    return SQLITE_QUEUE_ERROR_NONE;
}

int sqlite_queue_receive(SQLiteQueueContext *ctx, const char *sender_filter, int max_messages,
                         char ***messages, int *message_count, long long **message_ids, int timeout_ms) {
    // Critical invariants
    assert(ctx != NULL);
    assert(messages != NULL);
    assert(message_count != NULL);
    assert(message_ids != NULL);

    if (!ctx || !messages || !message_count || !message_ids) {
        sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_INVALID_PARAM, "Invalid parameters for receive");
        return SQLITE_QUEUE_ERROR_INVALID_PARAM;
    }

    // Initialize outputs
    *messages = NULL;
    *message_count = 0;
    *message_ids = NULL;

    // Use default max_messages if not specified
    if (max_messages <= 0) {
        max_messages = 100; // Default limit
    }

    // Open database if not already open
    if (!ctx->db_handle && sqlite_queue_open_db(ctx) != 0) {
        return ctx->last_error;
    }

    // Poll for messages with timeout
    time_t start_time = time(NULL);
    time_t timeout_sec = (timeout_ms > 0) ? (timeout_ms / 1000) : 30;

    LOG_DEBUG("SQLite Queue: Polling for messages (timeout: %ld seconds, sender_filter: %s)",
              (long)timeout_sec, sender_filter ? sender_filter : "any");

    while (time(NULL) - start_time < timeout_sec) {
        // Prepare select statement
        sqlite3_stmt *stmt = NULL;
        int rc = sqlite_queue_prepare_statement(ctx, &stmt, SELECT_MESSAGES_SQL);
        if (rc != SQLITE_OK) {
            return rc;
        }

        // Bind parameters (receiver = our sender_name)
        sqlite3_bind_text(stmt, 1, ctx->sender_name, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, max_messages);

        // Fetch messages
        int count = 0;
        char **msg_array = reallocarray(NULL, (size_t)max_messages, sizeof(char *));
        long long *id_array = reallocarray(NULL, (size_t)max_messages, sizeof(long long));

        if (!msg_array || !id_array) {
            free(msg_array);
            free(id_array);
            sqlite3_finalize(stmt);
            sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_DB_EXECUTE_FAILED, "Memory allocation failed");
            return SQLITE_QUEUE_ERROR_DB_EXECUTE_FAILED;
        }

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && count < max_messages) {
            id_array[count] = sqlite3_column_int64(stmt, 0);
            const char *msg_text = (const char *)sqlite3_column_text(stmt, 1);

            if (msg_text) {
                msg_array[count] = strdup(msg_text);
                if (!msg_array[count]) {
                    // Free all allocated messages so far
                    for (int i = 0; i < count; i++) {
                        free(msg_array[i]);
                    }
                    free(msg_array);
                    free(id_array);
                    sqlite3_finalize(stmt);
                    sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_DB_EXECUTE_FAILED, "Memory allocation failed");
                    return SQLITE_QUEUE_ERROR_DB_EXECUTE_FAILED;
                }
                count++;
            }
        }

        sqlite3_finalize(stmt);

        // Check if we got any messages
        if (count > 0) {
            *messages = msg_array;
            *message_count = count;
            *message_ids = id_array;

            LOG_INFO("SQLite Queue: Received %d message(s)", count);
            return SQLITE_QUEUE_ERROR_NONE;
        } else {
            // Free temporary arrays
            free(msg_array);
            free(id_array);
        }

        // No messages, wait for poll interval
        usleep((useconds_t)(ctx->poll_interval * 1000));
    }

    LOG_DEBUG("SQLite Queue: No messages received within timeout");
    sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_TIMEOUT, "No messages received within timeout");
    return SQLITE_QUEUE_ERROR_TIMEOUT;
}

int sqlite_queue_acknowledge(SQLiteQueueContext *ctx, long long message_id) {
    if (!ctx || message_id <= 0) {
        sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_INVALID_PARAM, "Invalid message ID for acknowledge");
        return SQLITE_QUEUE_ERROR_INVALID_PARAM;
    }

    // Open database if not already open
    if (!ctx->db_handle && sqlite_queue_open_db(ctx) != 0) {
        return ctx->last_error;
    }

    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    LOG_DEBUG("SQLite Queue: Acknowledging message ID: %lld", message_id);

    // Prepare update statement
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite_queue_prepare_statement(ctx, &stmt, ACK_MESSAGE_SQL);
    if (rc != SQLITE_OK) {
        return rc;
    }

    // Bind parameters
    sqlite3_bind_int64(stmt, 1, message_id);

    // Execute
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_DB_EXECUTE_FAILED,
                              "Failed to acknowledge message: %s", sqlite3_errmsg(db));
        LOG_ERROR("SQLite Queue: Failed to acknowledge message: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return SQLITE_QUEUE_ERROR_DB_EXECUTE_FAILED;
    }

    sqlite3_finalize(stmt);

    int changes = sqlite3_changes(db);
    if (changes == 0) {
        LOG_WARN("SQLite Queue: No message found with ID: %lld", message_id);
    } else {
        LOG_DEBUG("SQLite Queue: Successfully acknowledged message ID: %lld", message_id);
    }

    return SQLITE_QUEUE_ERROR_NONE;
}

const char* sqlite_queue_last_error(SQLiteQueueContext *ctx) {
    if (!ctx || ctx->last_error == SQLITE_QUEUE_ERROR_NONE) {
        return sqlite_queue_error_to_string(SQLITE_QUEUE_ERROR_NONE);
    }

    return ctx->error_message;
}

void sqlite_queue_clear_error(SQLiteQueueContext *ctx) {
    if (!ctx) return;

    ctx->last_error = SQLITE_QUEUE_ERROR_NONE;
    ctx->error_message[0] = '\0';
    ctx->error_time = 0;
}

#ifndef TEST_BUILD
int sqlite_queue_process_message(SQLiteQueueContext *ctx, struct ConversationState *state, struct TUIState *tui) {
    if (!ctx || !state) {
        LOG_ERROR("SQLite Queue: Invalid parameters for process_message");
        return -1;
    }
    (void)tui; // Unused parameter for now

    LOG_DEBUG("SQLite Queue: Waiting for incoming message");

    // Poll for messages
    char **messages = NULL;
    int message_count = 0;
    long long *message_ids = NULL;

    int result = sqlite_queue_receive(ctx, NULL, 1, &messages, &message_count, &message_ids, -1);
    if (result != SQLITE_QUEUE_ERROR_NONE || message_count == 0) {
        LOG_DEBUG("SQLite Queue: No messages to process");
        return SQLITE_QUEUE_ERROR_NO_MESSAGES;
    }

    LOG_INFO("SQLite Queue: Received %d message(s), processing", message_count);

    for (int i = 0; i < message_count; i++) {
        char *message = messages[i];
        long long msg_id = message_ids[i];

        LOG_DEBUG("SQLite Queue: Processing message ID: %lld (length: %zu)", msg_id, strlen(message));
        LOG_DEBUG("SQLite Queue: Raw message (first 500 chars): %.*s",
                 (int)(strlen(message) > 500 ? 500 : strlen(message)), message);

        // Print to console
        printf("SQLite Queue: Received %zu bytes\n", strlen(message));
        fflush(stdout);

        // Parse JSON message
        LOG_DEBUG("SQLite Queue: Parsing JSON message");
        cJSON *json = cJSON_Parse(message);
        if (!json) {
            LOG_ERROR("SQLite Queue: Failed to parse JSON message");
            const char *error_ptr = cJSON_GetErrorPtr();
            if (error_ptr) {
                LOG_ERROR("SQLite Queue: JSON error near: %s", error_ptr);
            }

            // Still acknowledge to remove from queue
            sqlite_queue_acknowledge(ctx, msg_id);
            free(message);
            continue;
        }

        LOG_DEBUG("SQLite Queue: JSON parsed successfully");

        // Extract message type and content
        LOG_DEBUG("SQLite Queue: Extracting message fields from JSON");
        cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
        cJSON *content = cJSON_GetObjectItem(json, "content");

        int process_result = 0;

        if (message_type && cJSON_IsString(message_type) &&
            strcmp(message_type->valuestring, "TEXT") == 0 &&
            content && cJSON_IsString(content)) {

            // Process text message with interactive tool call support
            LOG_INFO("SQLite Queue: Processing TEXT message (length: %zu)", strlen(content->valuestring));
            LOG_DEBUG("SQLite Queue: Message content: %.*s",
                    (int)(strlen(content->valuestring) > 200 ? 200 : strlen(content->valuestring)),
                    content->valuestring);

            // Print to console
            printf("SQLite Queue: Processing TEXT message (length: %zu)\n", strlen(content->valuestring));
            // Print first 100 chars of the message
            int preview_len = (int)(strlen(content->valuestring) > 100 ? 100 : strlen(content->valuestring));
            printf("Message preview: %.*s%s\n", preview_len, content->valuestring,
                   strlen(content->valuestring) > 100 ? "..." : "");
            fflush(stdout);

            // Process interactively (handles tool calls recursively)
            // Make a copy of the content string to avoid potential use-after-free
            char *content_copy = strdup(content->valuestring);
            if (!content_copy) {
                LOG_ERROR("SQLite Queue: Failed to duplicate message content");
                cJSON_Delete(json);
                free((void *)message);
                continue;
            }
            process_result = sqlite_queue_process_interactive(ctx, state, content_copy);
            free(content_copy);

            if (process_result != 0) {
                LOG_ERROR("SQLite Queue: Interactive processing failed");
            }

        } else {
            LOG_WARN("SQLite Queue: Invalid message format received");
            LOG_DEBUG("SQLite Queue: Available fields - messageType: %s, content: %s",
                     message_type ? "present" : "missing",
                     content ? "present" : "missing");
        }

        // Acknowledge message (mark as sent)
        if (sqlite_queue_acknowledge(ctx, msg_id) != SQLITE_QUEUE_ERROR_NONE) {
            LOG_WARN("SQLite Queue: Failed to acknowledge message ID: %lld", msg_id);
        }

        cJSON_Delete(json);
        free((void *)message); // Cast away const for free

        if (process_result != 0) {
            continue; // Continue processing other messages
        }
    }

    free(message_ids);
    free(messages);

    LOG_INFO("SQLite Queue: Message processing completed");
    return 0;
}
#endif // TEST_BUILD

#ifndef TEST_BUILD
// Helper function to send a JSON response
static int sqlite_queue_send_json(SQLiteQueueContext *ctx, const char *receiver,
                                  const char *message_type, const char *content) {
    if (!ctx || !receiver || !message_type) {
        LOG_ERROR("SQLite Queue: Invalid parameters for send_json");
        return -1;
    }

    cJSON *response_json = cJSON_CreateObject();
    if (!response_json) {
        LOG_ERROR("SQLite Queue: Failed to create response JSON object");
        return -1;
    }

    cJSON_AddStringToObject(response_json, "messageType", message_type);
    if (content) {
        cJSON_AddStringToObject(response_json, "content", content);
    }

    char *response_str = cJSON_PrintUnformatted(response_json);
    if (!response_str) {
        LOG_ERROR("SQLite Queue: Failed to serialize response JSON");
        cJSON_Delete(response_json);
        return -1;
    }

    int result = sqlite_queue_send(ctx, receiver, response_str, strlen(response_str));
    free(response_str);
    cJSON_Delete(response_json);

    return result;
}

// Helper function to send a tool result response
static int sqlite_queue_send_tool_result(SQLiteQueueContext *ctx, const char *receiver,
                                         const char *tool_name, const char *tool_id,
                                         cJSON *tool_output, int is_error) {
    if (!ctx || !receiver || !tool_name || !tool_id) {
        LOG_ERROR("SQLite Queue: Invalid parameters for send_tool_result");
        return -1;
    }

    cJSON *response_json = cJSON_CreateObject();
    if (!response_json) {
        LOG_ERROR("SQLite Queue: Failed to create tool result JSON object");
        return -1;
    }

    cJSON_AddStringToObject(response_json, "messageType", "TOOL_RESULT");
    cJSON_AddStringToObject(response_json, "toolName", tool_name);
    cJSON_AddStringToObject(response_json, "toolId", tool_id);

    if (tool_output) {
        cJSON_AddItemToObject(response_json, "toolOutput", cJSON_Duplicate(tool_output, 1));
    } else {
        cJSON_AddNullToObject(response_json, "toolOutput");
    }

    cJSON_AddBoolToObject(response_json, "isError", is_error ? 1 : 0);

    char *response_str = cJSON_PrintUnformatted(response_json);
    if (!response_str) {
        LOG_ERROR("SQLite Queue: Failed to serialize tool result JSON");
        cJSON_Delete(response_json);
        return -1;
    }

    int result = sqlite_queue_send(ctx, receiver, response_str, strlen(response_str));
    free(response_str);
    cJSON_Delete(response_json);

    return result;
}

// Helper function to send assistant text response
static int sqlite_queue_send_text_response(SQLiteQueueContext *ctx, const char *receiver,
                                          const char *text) {
    if (!ctx || !receiver || !text) {
        LOG_ERROR("SQLite Queue: Invalid parameters for send_text_response");
        return -1;
    }

    // Skip whitespace-only content
    const char *p = text;
    while (*p && isspace((unsigned char)*p)) p++;

    if (*p == '\0') {  // Only whitespace
        LOG_DEBUG("SQLite Queue: Skipping whitespace-only text response");
        return 0;
    }

    LOG_INFO("SQLite Queue: Sending assistant text response");

    // Print to console
    printf("\n--- AI Response ---\n");
    // Print first 200 chars of the response
    int preview_len = (int)(strlen(p) > 200 ? 200 : strlen(p));
    printf("%.*s%s\n", preview_len, p, strlen(p) > 200 ? "..." : "");
    printf("--- End of AI Response ---\n");
    fflush(stdout);

    return sqlite_queue_send_json(ctx, receiver, "TEXT", p);
}

// Helper function to send END_AI_TURN event
static int sqlite_queue_send_end_ai_turn(SQLiteQueueContext *ctx, const char *receiver) {
    if (!ctx || !receiver) {
        LOG_ERROR("SQLite Queue: Invalid parameters for send_end_ai_turn");
        return -1;
    }

    LOG_INFO("SQLite Queue: Sending END_AI_TURN event");
    return sqlite_queue_send_json(ctx, receiver, "END_AI_TURN", NULL);
}

// Helper function to send a tool execution request
static int sqlite_queue_send_tool_request(SQLiteQueueContext *ctx, const char *receiver,
                                         const char *tool_name, const char *tool_id,
                                         cJSON *tool_parameters) {
    if (!ctx || !receiver || !tool_name || !tool_id) {
        LOG_ERROR("SQLite Queue: Invalid parameters for send_tool_request");
        return -1;
    }

    cJSON *request_json = cJSON_CreateObject();
    if (!request_json) {
        LOG_ERROR("SQLite Queue: Failed to create tool request JSON object");
        return -1;
    }

    cJSON_AddStringToObject(request_json, "messageType", "TOOL");
    cJSON_AddStringToObject(request_json, "toolName", tool_name);
    cJSON_AddStringToObject(request_json, "toolId", tool_id);

    if (tool_parameters) {
        cJSON_AddItemToObject(request_json, "toolParameters", cJSON_Duplicate(tool_parameters, 1));
    } else {
        cJSON_AddNullToObject(request_json, "toolParameters");
    }

    char *request_str = cJSON_PrintUnformatted(request_json);
    if (!request_str) {
        LOG_ERROR("SQLite Queue: Failed to serialize tool request JSON");
        cJSON_Delete(request_json);
        return -1;
    }

    LOG_INFO("SQLite Queue: Sending TOOL request for %s (id: %s)", tool_name, tool_id);
    int result = sqlite_queue_send(ctx, receiver, request_str, strlen(request_str));
    free(request_str);
    cJSON_Delete(request_json);

    return result;
}

// Helper function to validate and execute a single tool
static int process_single_tool(SQLiteQueueContext *ctx, const char *response_receiver,
                              ToolCall *tool, ConversationState *state,
                              InternalContent *result) {
    if (!ctx || !response_receiver || !tool || !state || !result) {
        LOG_ERROR("SQLite Queue: Invalid parameters for process_single_tool");
        return -1;
    }

    // Initialize result
    memset(result, 0, sizeof(InternalContent));
    result->type = INTERNAL_TOOL_RESPONSE;

    // Check for missing tool name or ID
    if (!tool->name || !tool->id) {
        LOG_WARN("SQLite Queue: Tool call missing name or id, skipping");
        result->tool_id = tool->id ? strdup(tool->id) : strdup("unknown");
        result->tool_name = tool->name ? strdup(tool->name) : strdup("tool");
        result->tool_output = cJSON_CreateObject();
        cJSON_AddStringToObject(result->tool_output, "error", "Tool call missing name or id");
        result->is_error = 1;
        return 0;
    }

    LOG_INFO("SQLite Queue: Executing tool: %s (id: %s)", tool->name, tool->id);

    // Print to console
    printf("SQLite Queue: Executing tool: %s\n", tool->name);
    fflush(stdout);

    // Validate that the tool is in the allowed tools list (prevent hallucination)
    if (!is_tool_allowed(tool->name, state)) {
        LOG_ERROR("SQLite Queue: Tool validation failed: '%s' was not provided in tools list", tool->name);
        result->tool_id = strdup(tool->id);
        result->tool_name = strdup(tool->name);
        result->tool_output = cJSON_CreateObject();
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg),
                 "ERROR: Tool '%s' does not exist or was not provided to you. "
                 "Please check the list of available tools and try again with a valid tool name.",
                 tool->name);
        cJSON_AddStringToObject(result->tool_output, "error", error_msg);
        result->is_error = 1;

        // Send TOOL request message (even though it will fail)
        sqlite_queue_send_tool_request(ctx, response_receiver, tool->name, tool->id, NULL);

        // Send error response
        sqlite_queue_send_tool_result(ctx, response_receiver, tool->name, tool->id, result->tool_output, 1);
        return 0;
    }

    // Convert ToolCall to execute_tool parameters
    cJSON *input = tool->parameters
        ? cJSON_Duplicate(tool->parameters, /*recurse*/1)
        : cJSON_CreateObject();

    // Send TOOL request message before execution
    sqlite_queue_send_tool_request(ctx, response_receiver, tool->name, tool->id, input);

    // Execute tool synchronously
    cJSON *tool_result = execute_tool(tool->name, input, state);

    // Send tool result response
    sqlite_queue_send_tool_result(ctx, response_receiver, tool->name, tool->id, tool_result, 0);

    // Store tool result
    result->tool_id = strdup(tool->id);
    result->tool_name = strdup(tool->name);
    result->tool_output = tool_result ? cJSON_Duplicate(tool_result, 1) : cJSON_CreateObject();
    result->is_error = 0;

    // Clean up
    if (input) cJSON_Delete(input);
    if (tool_result) cJSON_Delete(tool_result);

    return 0;
}

// Process SQLite message with interactive tool call support
static int sqlite_queue_process_interactive(SQLiteQueueContext *ctx,
                                            struct ConversationState *state, const char *user_input) {
    if (!ctx || !state || !user_input) {
        LOG_ERROR("SQLite Queue: Invalid parameters for process_interactive");
        return -1;
    }

    LOG_INFO("SQLite Queue: Processing interactive message: %.*s",
             (int)(strlen(user_input) > 200 ? 200 : strlen(user_input)), user_input);

    // Get the sender/receiver name for responses
    // For simplicity, we use "client" as the receiver for responses
    const char *response_receiver = "client";

    // Add user message to conversation
    add_user_message(state, user_input);

    // Main interactive loop
    int iteration = 0;
    const int max_iterations = ctx->max_iterations; // Configurable limit (0 = unlimited)

    while (max_iterations == 0 || iteration < max_iterations) {
        iteration++;
        LOG_DEBUG("SQLite Queue: Interactive loop iteration %d", iteration);

        // Call AI API
        LOG_INFO("SQLite Queue: Calling AI API");
        ApiResponse *api_response = call_api_with_retries(state);

        if (!api_response) {
            LOG_ERROR("SQLite Queue: Failed to get response from AI API");
            sqlite_queue_send_json(ctx, response_receiver, "ERROR", "AI inference failed");
            return -1;
        }

        if (api_response->error_message) {
            LOG_ERROR("SQLite Queue: AI API returned error: %s", api_response->error_message);
            sqlite_queue_send_json(ctx, response_receiver, "ERROR", api_response->error_message);
            api_response_free(api_response);
            return -1;
        }

        // Send assistant's text response if present
        if (api_response->message.text && api_response->message.text[0] != '\0') {
            sqlite_queue_send_text_response(ctx, response_receiver, api_response->message.text);
        }

        // Add assistant message to conversation history
        if (api_response->raw_response) {
            cJSON *choices = cJSON_GetObjectItem(api_response->raw_response, "choices");
            if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                cJSON *choice = cJSON_GetArrayItem(choices, 0);
                cJSON *message = cJSON_GetObjectItem(choice, "message");
                if (message) {
                    add_assistant_message_openai(state, message);
                }
            }
        }

        // Process tool calls
        int tool_count = api_response->tool_count;
        ToolCall *tool_calls_array = api_response->tools;

        if (tool_count > 0) {
            LOG_INFO("SQLite Queue: Processing %d tool call(s)", tool_count);

            // Allocate results array
            InternalContent *results = reallocarray(NULL, (size_t)tool_count, sizeof(InternalContent));
            if (results) {
                memset(results, 0, (size_t)tool_count * sizeof(InternalContent));
            }
            if (!results) {
                LOG_ERROR("SQLite Queue: Failed to allocate tool result buffer");
                sqlite_queue_send_json(ctx, response_receiver, "ERROR", "Failed to allocate tool result buffer");
                api_response_free(api_response);
                return -1;
            }

            // Execute tools synchronously
            for (int i = 0; i < tool_count; i++) {
                ToolCall *tool = &tool_calls_array[i];
                process_single_tool(ctx, response_receiver, tool, state, &results[i]);
            }

            // Add tool results to conversation
            if (add_tool_results(state, results, tool_count) != 0) {
                LOG_ERROR("SQLite Queue: Failed to add tool results to conversation");
                // Results were already freed by add_tool_results
                results = NULL;
                sqlite_queue_send_json(ctx, response_receiver, "ERROR", "Failed to add tool results to conversation");
                api_response_free(api_response);
                return -1;
            }

            // Continue loop to process next AI response with tool results
            api_response_free(api_response);
            continue;
        }

        // Check if we need user input (e.g., assistant is asking a question)
        // For now, we'll just finish after processing all tool calls
        // In the future, we could analyze the response to detect questions

        api_response_free(api_response);
        break;
    }

    if (max_iterations > 0 && iteration >= max_iterations) {
        LOG_WARN("SQLite Queue: Reached maximum iterations (%d), stopping interactive loop", max_iterations);
        sqlite_queue_send_json(ctx, response_receiver, "ERROR", "Maximum iteration limit reached");
        return -1;
    }

    LOG_INFO("SQLite Queue: Interactive processing completed successfully");

    // Send END_AI_TURN event to indicate klawed is waiting for further instruction
    sqlite_queue_send_end_ai_turn(ctx, response_receiver);

    return 0;
}
#endif // TEST_BUILD

// Helper function to send auto-compaction notice
// NOTE: This function is NOT wrapped in #ifndef TEST_BUILD because it's called from api_client.c
// which needs to work in both normal and test builds.
int sqlite_queue_send_compaction_notice(SQLiteQueueContext *ctx, const char *receiver,
                                       int messages_compacted, size_t tokens_before,
                                        size_t tokens_after, double usage_before_pct,
                                        double usage_after_pct) {
    if (!ctx || !receiver) {
        LOG_ERROR("SQLite Queue: Invalid parameters for send_compaction_notice");
        return -1;
    }

    cJSON *notice_json = cJSON_CreateObject();
    if (!notice_json) {
        LOG_ERROR("SQLite Queue: Failed to create compaction notice JSON object");
        return -1;
    }

    cJSON_AddStringToObject(notice_json, "messageType", "AUTO_COMPACTION");
    cJSON_AddNumberToObject(notice_json, "messagesCompacted", messages_compacted);
    cJSON_AddNumberToObject(notice_json, "tokensBefore", (double)tokens_before);
    cJSON_AddNumberToObject(notice_json, "tokensAfter", (double)tokens_after);
    cJSON_AddNumberToObject(notice_json, "tokensFreed", (double)(tokens_before - tokens_after));
    cJSON_AddNumberToObject(notice_json, "usageBeforePct", usage_before_pct);
    cJSON_AddNumberToObject(notice_json, "usageAfterPct", usage_after_pct);

    // Build human-readable content message
    char content_msg[512];
    snprintf(content_msg, sizeof(content_msg),
             "Context compaction: %d messages stored to memory. "
             "Tokens: %zu → %zu (freed ~%zu tokens). "
             "Usage: %.1f%% → %.1f%%.",
             messages_compacted,
             tokens_before, tokens_after, tokens_before - tokens_after,
             usage_before_pct, usage_after_pct);
    cJSON_AddStringToObject(notice_json, "content", content_msg);

    char *notice_str = cJSON_PrintUnformatted(notice_json);
    if (!notice_str) {
        LOG_ERROR("SQLite Queue: Failed to serialize compaction notice JSON");
        cJSON_Delete(notice_json);
        return -1;
    }

    LOG_INFO("SQLite Queue: Sending AUTO_COMPACTION notice (%d messages, %zu→%zu tokens)",
             messages_compacted, tokens_before, tokens_after);

    int result = sqlite_queue_send(ctx, receiver, notice_str, strlen(notice_str));
    free(notice_str);
    cJSON_Delete(notice_json);

    return result;
}

/**
 * Helper structure to track pending tool calls during seeding.
 * We need to pair TOOL messages with their TOOL_RESULT to maintain
 * conversation integrity for the LLM API.
 */
typedef struct {
    char *tool_id;
    char *tool_name;
    cJSON *tool_params;  // Owned, must be freed
} PendingToolCall;

/**
 * Seed conversation history from existing messages in the database.
 * Called automatically at daemon boot to restore conversation context.
 * Handles TEXT, TOOL, and TOOL_RESULT messages with proper pairing.
 *
 * @param ctx SQLite queue context
 * @param state Conversation state to seed
 * @return Number of messages seeded, or -1 on error
 */

int sqlite_queue_seed_conversation(SQLiteQueueContext *ctx, struct ConversationState *state) {
    if (!ctx || !state) {
        LOG_ERROR("SQLite Queue: Invalid parameters for seed_conversation");
        return -1;
    }

    // Open database if not already open
    if (!ctx->db_handle && sqlite_queue_open_db(ctx) != 0) {
        return -1;
    }

    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    LOG_INFO("SQLite Queue: Checking for seed messages to restore conversation history");

    // Prepare select statement
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, SELECT_SEED_MESSAGES_SIMPLE_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("SQLite Queue: Failed to prepare seed query: %s", sqlite3_errmsg(db));
        return -1;
    }

    int seeded_count = 0;

    // Track pending tool calls that need results
    // We use a simple array since we expect few pending calls at any time
    PendingToolCall pending_tools[16] = {0};
    int pending_count = 0;
    const int max_pending = 16;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *sender = (const char *)sqlite3_column_text(stmt, 0);
        const char *message = (const char *)sqlite3_column_text(stmt, 1);

        if (!sender || !message) {
            continue;
        }

        // Parse JSON message
        cJSON *json = cJSON_Parse(message);
        if (!json) {
            LOG_DEBUG("SQLite Queue: Skipping invalid JSON message during seeding");
            continue;
        }

        cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
        if (!message_type || !cJSON_IsString(message_type)) {
            cJSON_Delete(json);
            continue;
        }

        const char *msg_type = message_type->valuestring;
        int is_from_klawed = (strcmp(sender, ctx->sender_name) == 0);

        // Handle TEXT messages
        if (strcmp(msg_type, "TEXT") == 0) {
            cJSON *content = cJSON_GetObjectItem(json, "content");
            if (!content || !cJSON_IsString(content)) {
                cJSON_Delete(json);
                continue;
            }

            const char *text = content->valuestring;

            // Skip empty or whitespace-only content
            const char *p = text;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '\0') {
                cJSON_Delete(json);
                continue;
            }

            if (is_from_klawed) {
                // This is a message FROM klawed (assistant response with text)
                if (state->count < MAX_MESSAGES) {
                    InternalMessage *msg = &state->messages[state->count];
                    msg->role = MSG_ASSISTANT;
                    msg->content_count = 1;
                    msg->contents = reallocarray(NULL, 1, sizeof(InternalContent));
                    if (msg->contents) {
                        memset(msg->contents, 0, sizeof(InternalContent));
                        msg->contents[0].type = INTERNAL_TEXT;
                        msg->contents[0].text = strdup(text);
                        if (msg->contents[0].text) {
                            state->count++;
                            seeded_count++;
                            LOG_DEBUG("SQLite Queue: Seeded assistant TEXT message");
                        } else {
                            free(msg->contents);
                            msg->contents = NULL;
                            msg->content_count = 0;
                        }
                    }
                }
            } else {
                // This is a message TO klawed (user input)
                // First, check if we have any pending tool calls without results.
                // If so, this means the conversation was interrupted before the tool
                // results were sent. We must inject synthetic error results to
                // maintain API validity, then clear the pending list.
                if (pending_count > 0) {
                    LOG_WARN("SQLite Queue: %d pending tool call(s) without results found before user message, injecting synthetic errors", pending_count);

                    if (state->count < MAX_MESSAGES && pending_count <= max_pending) {
                        InternalContent *results = reallocarray(NULL, (size_t)pending_count, sizeof(InternalContent));
                        if (results) {
                            memset(results, 0, (size_t)pending_count * sizeof(InternalContent));

                            for (int i = 0; i < pending_count; i++) {
                                results[i].type = INTERNAL_TOOL_RESPONSE;
                                results[i].tool_id = pending_tools[i].tool_id;
                                results[i].tool_name = pending_tools[i].tool_name;
                                results[i].is_error = 1;
                                results[i].tool_output = cJSON_CreateObject();
                                cJSON_AddStringToObject(results[i].tool_output, "error",
                                    "Tool execution was interrupted - no result received before next user message");
                                // Clear the entry so we don't free the strings we just transferred
                                pending_tools[i].tool_id = NULL;
                                pending_tools[i].tool_name = NULL;
                                pending_tools[i].tool_params = NULL;
                            }

                            InternalMessage *msg = &state->messages[state->count];
                            msg->role = MSG_USER;
                            msg->contents = results;
                            msg->content_count = pending_count;
                            state->count++;
                            seeded_count++;
                            LOG_DEBUG("SQLite Queue: Added %d synthetic tool error results", pending_count);
                        }
                    }

                    // Free any remaining pending tool data
                    for (int i = 0; i < pending_count; i++) {
                        free(pending_tools[i].tool_id);
                        free(pending_tools[i].tool_name);
                        if (pending_tools[i].tool_params) {
                            cJSON_Delete(pending_tools[i].tool_params);
                        }
                    }
                    pending_count = 0;
                }

                add_user_message(state, text);
                seeded_count++;
                LOG_DEBUG("SQLite Queue: Seeded user TEXT message");
            }
        }
        // Handle TOOL messages (assistant requesting tool execution)
        else if (strcmp(msg_type, "TOOL") == 0) {
            if (!is_from_klawed) {
                // TOOL messages should only come from klawed (assistant)
                LOG_WARN("SQLite Queue: Ignoring TOOL message from non-klawed sender: %s", sender);
                cJSON_Delete(json);
                continue;
            }

            if (pending_count >= max_pending) {
                LOG_WARN("SQLite Queue: Too many pending tool calls, dropping oldest");
                // Drop the oldest pending tool
                free(pending_tools[0].tool_id);
                free(pending_tools[0].tool_name);
                if (pending_tools[0].tool_params) {
                    cJSON_Delete(pending_tools[0].tool_params);
                }
                // Shift remaining
                for (int i = 1; i < pending_count; i++) {
                    pending_tools[i - 1] = pending_tools[i];
                }
                pending_count--;
            }

            cJSON *tool_id = cJSON_GetObjectItem(json, "toolId");
            cJSON *tool_name = cJSON_GetObjectItem(json, "toolName");
            cJSON *tool_params = cJSON_GetObjectItem(json, "toolParameters");

            if (!tool_id || !cJSON_IsString(tool_id) ||
                !tool_name || !cJSON_IsString(tool_name)) {
                LOG_WARN("SQLite Queue: TOOL message missing required fields");
                cJSON_Delete(json);
                continue;
            }

            pending_tools[pending_count].tool_id = strdup(tool_id->valuestring);
            pending_tools[pending_count].tool_name = strdup(tool_name->valuestring);
            if (tool_params && cJSON_IsObject(tool_params)) {
                pending_tools[pending_count].tool_params = cJSON_Duplicate(tool_params, 1);
            } else {
                pending_tools[pending_count].tool_params = cJSON_CreateObject();
            }
            pending_count++;

            LOG_DEBUG("SQLite Queue: Queued pending TOOL call: %s (id=%s)",
                     tool_name->valuestring, tool_id->valuestring);
        }
        // Handle TOOL_RESULT messages (result of tool execution)
        else if (strcmp(msg_type, "TOOL_RESULT") == 0) {
            if (is_from_klawed) {
                // TOOL_RESULT messages should come from client (via klawed forwarding)
                // but they represent user-side content
                LOG_WARN("SQLite Queue: Ignoring TOOL_RESULT message from klawed sender (should be from client)");
                cJSON_Delete(json);
                continue;
            }

            cJSON *tool_id = cJSON_GetObjectItem(json, "toolId");
            cJSON *tool_name = cJSON_GetObjectItem(json, "toolName");
            cJSON *tool_output = cJSON_GetObjectItem(json, "toolOutput");
            cJSON *is_error = cJSON_GetObjectItem(json, "isError");

            if (!tool_id || !cJSON_IsString(tool_id)) {
                LOG_WARN("SQLite Queue: TOOL_RESULT message missing toolId");
                cJSON_Delete(json);
                continue;
            }

            // Find matching pending tool call
            int found_idx = -1;
            for (int i = 0; i < pending_count; i++) {
                if (pending_tools[i].tool_id &&
                    strcmp(pending_tools[i].tool_id, tool_id->valuestring) == 0) {
                    found_idx = i;
                    break;
                }
            }

            if (found_idx < 0) {
                // Orphaned TOOL_RESULT - no matching TOOL call found
                // This can happen if the conversation was interrupted or cleaned up
                LOG_WARN("SQLite Queue: TOOL_RESULT with id=%s has no matching TOOL call, ignoring",
                        tool_id->valuestring);
                cJSON_Delete(json);
                continue;
            }

            // Build tool result content
            InternalContent result = {0};
            result.type = INTERNAL_TOOL_RESPONSE;
            result.tool_id = strdup(tool_id->valuestring);
            result.tool_name = strdup(tool_name && cJSON_IsString(tool_name) ?
                                      tool_name->valuestring : "unknown");
            result.is_error = (is_error && cJSON_IsBool(is_error)) ? is_error->valueint : 0;

            if (tool_output && !cJSON_IsNull(tool_output)) {
                result.tool_output = cJSON_Duplicate(tool_output, 1);
            } else {
                result.tool_output = cJSON_CreateObject();
            }

            // Remove this tool from pending and shift remaining
            free(pending_tools[found_idx].tool_id);
            free(pending_tools[found_idx].tool_name);
            if (pending_tools[found_idx].tool_params) {
                cJSON_Delete(pending_tools[found_idx].tool_params);
            }

            for (int i = found_idx + 1; i < pending_count; i++) {
                pending_tools[i - 1] = pending_tools[i];
            }
            pending_count--;

            // Add the tool result as a user message
            if (state->count < MAX_MESSAGES) {
                InternalContent *results = reallocarray(NULL, 1, sizeof(InternalContent));
                if (results) {
                    *results = result;

                    InternalMessage *msg = &state->messages[state->count];
                    msg->role = MSG_USER;
                    msg->contents = results;
                    msg->content_count = 1;
                    state->count++;
                    seeded_count++;
                    LOG_DEBUG("SQLite Queue: Seeded TOOL_RESULT for %s", result.tool_name);
                } else {
                    // Free result on allocation failure
                    free(result.tool_id);
                    free(result.tool_name);
                    if (result.tool_output) cJSON_Delete(result.tool_output);
                }
            } else {
                // Free result if we can't add it
                free(result.tool_id);
                free(result.tool_name);
                if (result.tool_output) cJSON_Delete(result.tool_output);
            }
        }
        // Ignore other message types (END_AI_TURN, ERROR, AUTO_COMPACTION, API_CALL, etc.)

        cJSON_Delete(json);
    }

    sqlite3_finalize(stmt);

    // Handle any remaining pending tool calls without results
    // These represent interrupted tool executions - we need to inject synthetic
    // error results to maintain API validity
    if (pending_count > 0) {
        LOG_WARN("SQLite Queue: %d pending tool call(s) without results at end of seeding, injecting synthetic errors", pending_count);

        if (state->count < MAX_MESSAGES && pending_count <= max_pending) {
            InternalContent *results = reallocarray(NULL, (size_t)pending_count, sizeof(InternalContent));
            if (results) {
                memset(results, 0, (size_t)pending_count * sizeof(InternalContent));

                for (int i = 0; i < pending_count; i++) {
                    results[i].type = INTERNAL_TOOL_RESPONSE;
                    results[i].tool_id = pending_tools[i].tool_id;
                    results[i].tool_name = pending_tools[i].tool_name;
                    results[i].is_error = 1;
                    results[i].tool_output = cJSON_CreateObject();
                    cJSON_AddStringToObject(results[i].tool_output, "error",
                        "Tool execution was interrupted - no result received before conversation ended");
                    // Clear the entry so we don't free the strings we just transferred
                    pending_tools[i].tool_id = NULL;
                    pending_tools[i].tool_name = NULL;
                }

                InternalMessage *msg = &state->messages[state->count];
                msg->role = MSG_USER;
                msg->contents = results;
                msg->content_count = pending_count;
                state->count++;
                seeded_count++;
                LOG_DEBUG("SQLite Queue: Added %d synthetic tool error results at end", pending_count);
            }
        }

        // Free any remaining pending tool data
        for (int i = 0; i < pending_count; i++) {
            free(pending_tools[i].tool_id);
            free(pending_tools[i].tool_name);
            if (pending_tools[i].tool_params) {
                cJSON_Delete(pending_tools[i].tool_params);
            }
        }
    }

    if (seeded_count > 0) {
        LOG_INFO("SQLite Queue: Seeded %d message(s) from database history", seeded_count);
        printf("SQLite Queue: Restored %d message(s) from conversation history\n", seeded_count);
        fflush(stdout);
    } else {
        LOG_INFO("SQLite Queue: No previous messages found to seed conversation");
    }

    return seeded_count;
}

#ifndef TEST_BUILD
int sqlite_queue_daemon_mode(SQLiteQueueContext *ctx, struct ConversationState *state) {
    if (!ctx || !state) {
        LOG_ERROR("SQLite Queue: Invalid parameters for daemon_mode");
        return -1;
    }

    LOG_INFO("SQLite Queue: =========================================");
    LOG_INFO("SQLite Queue: Starting SQLite queue daemon mode");
    LOG_INFO("SQLite Queue: Database path: %s", ctx->db_path);
    LOG_INFO("SQLite Queue: Sender name: %s", ctx->sender_name);
    LOG_INFO("SQLite Queue: =========================================");

    // Print to console as well
    printf("SQLite Queue daemon started on %s\n", ctx->db_path);
    printf("Sender name: %s\n", ctx->sender_name);

    // Seed conversation from existing messages (once at boot)
    int seeded = sqlite_queue_seed_conversation(ctx, state);
    if (seeded < 0) {
        LOG_WARN("SQLite Queue: Failed to seed conversation from history, continuing anyway");
    }

    printf("Waiting for messages...\n");
    fflush(stdout);

    int message_count = 0;
    int error_count = 0;

    while (ctx->enabled) {
        LOG_DEBUG("SQLite Queue: Waiting for next message (message #%d)", message_count + 1);

        int result = sqlite_queue_process_message(ctx, state, NULL);
        if (result == SQLITE_QUEUE_ERROR_NO_MESSAGES) {
            // No messages, this is normal - continue polling
            // Small delay to avoid tight loop
            struct timespec sleep_time = {0, (ctx->poll_interval * 1000000L)};
            nanosleep(&sleep_time, NULL);
            continue;
        } else if (result != 0) {
            error_count++;
            LOG_WARN("SQLite Queue: Message processing failed (error #%d)", error_count);

            // Check if we should continue or exit
            if (error_count > 10) {
                LOG_ERROR("SQLite Queue: Too many consecutive errors (%d), stopping daemon", error_count);
                printf("SQLite Queue: Too many consecutive errors (%d), stopping daemon\n", error_count);
                break;
            }

            // Small delay before retrying to avoid tight loop on errors
            struct timespec sleep_time = {0, 100000000}; // 100ms
            nanosleep(&sleep_time, NULL);
            continue;
        }

        message_count++;
        error_count = 0; // Reset error count on successful processing
        LOG_DEBUG("SQLite Queue: Successfully processed message #%d", message_count);
        printf("SQLite Queue: Successfully processed message #%d\n", message_count);
        fflush(stdout);
    }

    LOG_INFO("SQLite Queue: =========================================");
    LOG_INFO("SQLite Queue: SQLite queue daemon mode stopping");
    LOG_INFO("SQLite Queue: Total messages processed: %d", message_count);
    LOG_INFO("SQLite Queue: Total errors: %d", error_count);
    LOG_INFO("SQLite Queue: =========================================");

    // Print to console as well
    printf("\nSQLite Queue daemon stopped\n");
    printf("Total messages processed: %d\n", message_count);
    printf("Total errors: %d\n", error_count);
    fflush(stdout);

    return 0;
}
#endif // TEST_BUILD

bool sqlite_queue_available(void) {
    // SQLite is always available (it's a required dependency)
    return true;
}

int sqlite_queue_get_status(SQLiteQueueContext *ctx, char *buffer, size_t buffer_size) {
    if (!ctx || !buffer || buffer_size == 0) {
        return -1;
    }

    time_t now = time(NULL);

    // Format status string
    snprintf(buffer, buffer_size,
             "SQLite Queue Status:\n"
             "  Database Path: %s\n"
             "  Sender Name: %s\n"
             "  Daemon Mode: %s\n"
             "  Enabled: %s\n"
             "  Last Poll: %ld seconds ago\n"
             "  Retry Count: %d/%d",
             ctx->db_path ? ctx->db_path : "unknown",
             ctx->sender_name ? ctx->sender_name : "unknown",
             ctx->daemon_mode ? "enabled" : "disabled",
             ctx->enabled ? "enabled" : "disabled",
             ctx->last_poll ? (now - ctx->last_poll) : 0,
             ctx->retry_count, ctx->max_retries);

    return 0;
}

int sqlite_queue_get_stats(SQLiteQueueContext *ctx,
                          int *pending_count, int *total_count, int *unread_count) {
    if (!ctx) {
        return -1;
    }

    // Open database if not already open
    if (!ctx->db_handle && sqlite_queue_open_db(ctx) != 0) {
        return -1;
    }

    // Initialize outputs
    if (pending_count) *pending_count = 0;
    if (total_count) *total_count = 0;
    if (unread_count) *unread_count = 0;

    // Get pending count
    if (pending_count) {
        sqlite3_stmt *stmt = NULL;
        if (sqlite_queue_prepare_statement(ctx, &stmt, COUNT_PENDING_SQL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                *pending_count = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }

    // Get total count
    if (total_count) {
        sqlite3_stmt *stmt = NULL;
        if (sqlite_queue_prepare_statement(ctx, &stmt, COUNT_TOTAL_SQL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                *total_count = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }

    // Get unread count (messages from other senders)
    if (unread_count) {
        sqlite3_stmt *stmt = NULL;
        if (sqlite_queue_prepare_statement(ctx, &stmt, COUNT_UNREAD_SQL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, ctx->sender_name, -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                *unread_count = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }

    return 0;
}

int sqlite_queue_init_schema(SQLiteQueueContext *ctx) {
    if (!ctx) {
        return SQLITE_QUEUE_ERROR_INVALID_PARAM;
    }

    // Open database if not already open
    if (!ctx->db_handle && sqlite_queue_open_db(ctx) != 0) {
        return ctx->last_error;
    }

    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    LOG_INFO("SQLite Queue: Initializing database schema");

    // Create messages table
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, CREATE_MESSAGES_TABLE_SQL, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("SQLite Queue: Failed to create messages table: %s", errmsg ? errmsg : sqlite3_errmsg(db));
        sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_DB_EXECUTE_FAILED,
                              "Failed to create messages table: %s", errmsg ? errmsg : sqlite3_errmsg(db));
        if (errmsg) sqlite3_free(errmsg);
        return SQLITE_QUEUE_ERROR_DB_EXECUTE_FAILED;
    }
    LOG_DEBUG("SQLite Queue: Messages table created or already exists");

    // Create indexes
    rc = sqlite3_exec(db, CREATE_INDEX_SENDER_SQL, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        LOG_WARN("SQLite Queue: Failed to create sender index: %s", errmsg ? errmsg : sqlite3_errmsg(db));
        if (errmsg) sqlite3_free(errmsg);
    } else {
        LOG_DEBUG("SQLite Queue: Sender index created or already exists");
    }

    rc = sqlite3_exec(db, CREATE_INDEX_RECEIVER_SQL, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        LOG_WARN("SQLite Queue: Failed to create receiver index: %s", errmsg ? errmsg : sqlite3_errmsg(db));
        if (errmsg) sqlite3_free(errmsg);
    } else {
        LOG_DEBUG("SQLite Queue: Receiver index created or already exists");
    }

    LOG_INFO("SQLite Queue: Database schema initialized successfully");
    return SQLITE_QUEUE_ERROR_NONE;
}

// Helper functions

static int sqlite_queue_open_db(SQLiteQueueContext *ctx) {
    if (!ctx || !ctx->db_path) {
        return SQLITE_QUEUE_ERROR_INVALID_PARAM;
    }

    if (ctx->db_handle) {
        return SQLITE_QUEUE_ERROR_NONE; // Already open
    }

    sqlite3 *db = NULL;
    int rc = sqlite3_open(ctx->db_path, &db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("SQLite Queue: Failed to open database %s: %s", ctx->db_path, sqlite3_errmsg(db));
        sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_DB_OPEN_FAILED,
                              "Failed to open database: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return SQLITE_QUEUE_ERROR_DB_OPEN_FAILED;
    }

    // Apply pragma settings for better concurrency and performance
    char *errmsg = NULL;

    // 1. Enable WAL mode for better concurrency (CRITICAL)
    rc = sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        LOG_WARN("SQLite Queue: Failed to enable WAL mode: %s", errmsg ? errmsg : sqlite3_errmsg(db));
        if (errmsg) sqlite3_free(errmsg);
        errmsg = NULL;
        // Non-fatal, continue with default journal mode
    } else {
        LOG_DEBUG("SQLite Queue: WAL mode enabled");
    }

    // 2. Set synchronous mode to NORMAL (RECOMMENDED)
    rc = sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        LOG_WARN("SQLite Queue: Failed to set synchronous mode: %s", errmsg ? errmsg : sqlite3_errmsg(db));
        if (errmsg) sqlite3_free(errmsg);
        errmsg = NULL;
    } else {
        LOG_DEBUG("SQLite Queue: Synchronous mode set to NORMAL");
    }

    // 3. Set busy timeout to 5 seconds (ESSENTIAL)
    sqlite3_busy_timeout(db, 5000);
    LOG_DEBUG("SQLite Queue: Busy timeout set to 5000ms");

    // Optional performance pragmas
    // Set cache size to ~2MB (2000 pages of 1KB each)
    rc = sqlite3_exec(db, "PRAGMA cache_size=-2000;", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK && errmsg) {
        sqlite3_free(errmsg);
        errmsg = NULL;
    }

    // Store temporary tables/indexes in memory
    rc = sqlite3_exec(db, "PRAGMA temp_store=MEMORY;", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK && errmsg) {
        sqlite3_free(errmsg);
        errmsg = NULL;
    }

    // Limit WAL file size to prevent unbounded growth
    rc = sqlite3_exec(db, "PRAGMA journal_size_limit=67108864;", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK && errmsg) {
        sqlite3_free(errmsg);
        errmsg = NULL;
    }

    ctx->db_handle = db;
    LOG_DEBUG("SQLite Queue: Database opened with pragma settings: %s", ctx->db_path);

    return SQLITE_QUEUE_ERROR_NONE;
}

static void sqlite_queue_close_db(SQLiteQueueContext *ctx) {
    if (!ctx) return;

    if (ctx->db_handle) {
        LOG_DEBUG("SQLite Queue: Closing database");
        sqlite3 *db = (sqlite3 *)ctx->db_handle;
        sqlite3_close(db);
        ctx->db_handle = NULL;
    }
}

static int sqlite_queue_prepare_statement(SQLiteQueueContext *ctx, sqlite3_stmt **stmt, const char *sql) {
    if (!ctx || !stmt || !sql) {
        return SQLITE_QUEUE_ERROR_INVALID_PARAM;
    }

    // Open database if not already open
    if (!ctx->db_handle && sqlite_queue_open_db(ctx) != 0) {
        return ctx->last_error;
    }

    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    int rc = sqlite3_prepare_v2(db, sql, -1, stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("SQLite Queue: Failed to prepare statement: %s", sqlite3_errmsg(db));
        sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_DB_PREPARE_FAILED,
                              "Failed to prepare statement: %s", sqlite3_errmsg(db));
        return SQLITE_QUEUE_ERROR_DB_PREPARE_FAILED;
    }

    return SQLITE_OK;
}

__attribute__((format(printf, 3, 4)))
static void sqlite_queue_set_error(SQLiteQueueContext *ctx, SQLiteQueueErrorCode error_code, const char *format, ...) {
    if (!ctx) return;

    ctx->last_error = error_code;

    va_list args;
    va_start(args, format);
    vsnprintf(ctx->error_message, sizeof(ctx->error_message), format, args);
    va_end(args);

    ctx->error_time = time(NULL);

    LOG_ERROR("SQLite Queue Error [%d]: %s", error_code, ctx->error_message);
}

static const char* sqlite_queue_error_to_string(SQLiteQueueErrorCode error_code) {
    switch (error_code) {
        case SQLITE_QUEUE_ERROR_NONE: return "No error";
        case SQLITE_QUEUE_ERROR_INVALID_PARAM: return "Invalid parameter";
        case SQLITE_QUEUE_ERROR_DB_OPEN_FAILED: return "Database open failed";
        case SQLITE_QUEUE_ERROR_DB_PREPARE_FAILED: return "Statement prepare failed";
        case SQLITE_QUEUE_ERROR_DB_EXECUTE_FAILED: return "Statement execute failed";
        case SQLITE_QUEUE_ERROR_DB_BUSY: return "Database busy";
        case SQLITE_QUEUE_ERROR_NO_MESSAGES: return "No messages available";
        case SQLITE_QUEUE_ERROR_MESSAGE_TOO_LONG: return "Message too long";
        case SQLITE_QUEUE_ERROR_INVALID_MESSAGE: return "Invalid message";
        case SQLITE_QUEUE_ERROR_TIMEOUT: return "Timeout";
        case SQLITE_QUEUE_ERROR_NOT_INITIALIZED: return "Not initialized";
        default: return "Unknown error";
    }
}

