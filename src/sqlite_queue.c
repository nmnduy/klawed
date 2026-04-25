/*
 * sqlite_queue.c - SQLite message queue implementation for Klawed
 */

// Platform detection for pthread features
#if defined(__APPLE__)
#define USE_PTHREAD_JOIN_NO_TIMEOUT 1
#else
#define _GNU_SOURCE
#endif

#define COLORSCHEME_EXTERN  // Use extern declarations for colorscheme globals

#include "sqlite_queue.h"
#include "klawed_internal.h"
#include "conversation/conversation_processor.h"
#include "logger.h"
#include "compaction.h"
#include "background_init.h"
#include "colorscheme.h"
#include "fallback_colors.h"
#include "ui/tool_output_display.h"
#include "macos_sqlite_fix.h"
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

// Restore conversation from the last N message rows (sent=1), ordered chronologically.
// Loads TEXT, TOOL, and TOOL_RESULT types; other types are filtered in code.
#define SELECT_RESTORE_MESSAGES_SQL \
    "SELECT id, sender, message FROM (" \
    "  SELECT id, sender, message FROM messages " \
    "  WHERE sent = 1 " \
    "  ORDER BY id DESC LIMIT 800" \
    ") ORDER BY id ASC;"

// Check if the last message sent by klawed was an END_AI_TURN.
// Used on startup to detect an incomplete turn (klawed died before finishing).
#define SELECT_LAST_KLAWED_MESSAGE_SQL \
    "SELECT message FROM messages WHERE sender = ? AND sent = 1 ORDER BY id DESC LIMIT 1;"

// Forward declarations
#ifndef TEST_BUILD
static int sqlite_queue_process_interactive(SQLiteQueueContext *ctx, struct ConversationState *state, const char *user_input);
static int sqlite_queue_handle_compact_trigger(SQLiteQueueContext *ctx, struct ConversationState *state);
static int sqlite_queue_resume_pending_turn(SQLiteQueueContext *ctx, struct ConversationState *state);
static int sqlite_queue_last_turn_complete(SQLiteQueueContext *ctx);
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

    // Initialize threading primitives
    if (pthread_mutex_init(&ctx->queue_mutex, NULL) != 0) {
        LOG_ERROR("SQLite Queue: Failed to initialize queue mutex");
        free(ctx);
        return NULL;
    }

    if (pthread_cond_init(&ctx->queue_cond, NULL) != 0) {
        LOG_ERROR("SQLite Queue: Failed to initialize queue condition variable");
        pthread_mutex_destroy(&ctx->queue_mutex);
        free(ctx);
        return NULL;
    }

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
    ctx->retry_count = 0;
    ctx->initialized = false;
    ctx->db_handle = NULL;

    // Initialize error state
    ctx->last_error = SQLITE_QUEUE_ERROR_NONE;
    ctx->error_message[0] = '\0';
    ctx->error_time = 0;

    LOG_DEBUG("SQLite Queue: Poll interval: %dms", ctx->poll_interval);
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

    // Signal worker thread to shutdown and wait for it
    pthread_mutex_lock(&ctx->queue_mutex);
    ctx->shutdown = 1;
    pthread_cond_broadcast(&ctx->queue_cond);
    pthread_mutex_unlock(&ctx->queue_mutex);

    // Wait for worker thread to finish (with timeout)
    if (ctx->worker_thread) {
#ifdef USE_PTHREAD_JOIN_NO_TIMEOUT
        // macOS doesn't have pthread_timedjoin_np, use regular pthread_join
        pthread_join(ctx->worker_thread, NULL);
#else
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 2;  // 2 second timeout
        pthread_timedjoin_np(ctx->worker_thread, NULL, &timeout);
#endif
    }

    // Free any pending messages
    pthread_mutex_lock(&ctx->queue_mutex);
    PendingMessage *pm = ctx->pending_messages;
    while (pm) {
        PendingMessage *next = pm->next;
        free(pm->content);
        free(pm);
        pm = next;
    }
    ctx->pending_messages = NULL;
    ctx->pending_tail = NULL;
    ctx->pending_count = 0;
    pthread_mutex_unlock(&ctx->queue_mutex);

    // Destroy threading primitives
    pthread_mutex_destroy(&ctx->queue_mutex);
    pthread_cond_destroy(&ctx->queue_cond);

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
                         char ***messages, int *message_count, long long **message_ids) {
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

    LOG_DEBUG("SQLite Queue: Checking for messages (sender_filter: %s)",
              sender_filter ? sender_filter : "any");

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

    if (count > 0) {
        *messages = msg_array;
        *message_count = count;
        *message_ids = id_array;
        LOG_INFO("SQLite Queue: Received %d message(s)", count);
        return SQLITE_QUEUE_ERROR_NONE;
    }

    free(msg_array);
    free(id_array);
    return SQLITE_QUEUE_ERROR_NO_MESSAGES;
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

    int result = sqlite_queue_receive(ctx, NULL, 1, &messages, &message_count, &message_ids);
    if (result != SQLITE_QUEUE_ERROR_NONE || message_count == 0) {
        LOG_DEBUG("SQLite Queue: No messages to process");
        return SQLITE_QUEUE_ERROR_NO_MESSAGES;
    }

    LOG_INFO("SQLite Queue: Received %d message(s), processing", message_count);

    for (int i = 0; i < message_count; i++) {
        char *message = messages[i];
        long long msg_id = message_ids[i];

        LOG_DEBUG("SQLite Queue: Processing message ID: %lld (length: %zu)", msg_id, strlen(message));
        if (message) {
            LOG_DEBUG("SQLite Queue: Raw message (first 500 chars): %.*s",
                     (int)(strlen(message) > 500 ? 500 : strlen(message)), message);
        }

        // Print to console with subtle formatting
        char status_color[32] = {0};
        const char *status_color_start = NULL;

        if (get_colorscheme_color(COLORSCHEME_STATUS, status_color, sizeof(status_color)) == 0) {
            status_color_start = status_color;
        } else {
            status_color_start = ANSI_FALLBACK_STATUS;
        }

        LOG_DEBUG("SQLite Queue: Received %zu bytes", strlen(message));
        // Only print processing info in verbose/debug mode to reduce noise
        // printf("%s[SQLite Queue]%s Received %zu bytes\n", status_color_start, ANSI_RESET, strlen(message));
        // fflush(stdout);

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

        if (message_type && cJSON_IsString(message_type)) {
            const char *msg_type = message_type->valuestring;

            if (strcmp(msg_type, "TEXT") == 0 &&
                content && cJSON_IsString(content)) {

                // Process text message with interactive tool call support
                LOG_INFO("SQLite Queue: Processing TEXT message (length: %zu)", strlen(content->valuestring));
                LOG_DEBUG("SQLite Queue: Message content: %.*s",
                        (int)(strlen(content->valuestring) > 200 ? 200 : strlen(content->valuestring)),
                        content->valuestring);

                // Print user message to console (like TUI shows user input)
                char user_color[32] = {0};
                char text_color[32] = {0};
                const char *user_color_start = NULL;
                const char *text_color_start = NULL;

                if (get_colorscheme_color(COLORSCHEME_USER, user_color, sizeof(user_color)) == 0) {
                    user_color_start = user_color;
                } else {
                    user_color_start = ANSI_FALLBACK_USER;
                }

                if (get_colorscheme_color(COLORSCHEME_FOREGROUND, text_color, sizeof(text_color)) == 0) {
                    text_color_start = text_color;
                } else {
                    text_color_start = ANSI_FALLBACK_FOREGROUND;
                }

                printf("\n%s[User]%s %s%s%s\n",
                       user_color_start, ANSI_RESET,
                       text_color_start, content->valuestring, ANSI_RESET);
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

            } else if (strcmp(msg_type, "TRIGGER_COMPACT") == 0) {
                // Handle manual compaction trigger from client
                LOG_INFO("SQLite Queue: Received TRIGGER_COMPACT message ID %lld", msg_id);
                printf("SQLite Queue: Processing compaction trigger request\n");
                fflush(stdout);

                int compact_result = sqlite_queue_handle_compact_trigger(ctx, state);
                if (compact_result == 0) {
                    LOG_INFO("SQLite Queue: Compaction trigger processed successfully");
                } else {
                    LOG_ERROR("SQLite Queue: Compaction trigger failed");
                }

            } else if (strcmp(msg_type, "INTERRUPT") == 0) {
                // Handle interrupt request from client
                LOG_INFO("SQLite Queue: Received INTERRUPT message ID %lld", msg_id);
                printf("SQLite Queue: Interrupt requested by client\n");
                fflush(stdout);

                int interrupt_result = sqlite_queue_interrupt(ctx);
                if (interrupt_result == 0) {
                    LOG_INFO("SQLite Queue: Interrupt processed successfully");
                } else {
                    LOG_ERROR("SQLite Queue: Interrupt failed");
                }

            } else {
                LOG_WARN("SQLite Queue: Unknown or invalid message type: %s", msg_type);
                LOG_DEBUG("SQLite Queue: Available fields - messageType: %s, content: %s",
                         message_type ? "present" : "missing",
                         content ? "present" : "missing");
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
                                          const char *text, const char *reasoning_content) {
    if (!ctx || !receiver || !text) {
        LOG_ERROR("SQLite Queue: Invalid parameters for send_text_response");
        return -1;
    }

    const char *display_text = text;
    const char *display_reasoning = reasoning_content;

    while (*display_text && isspace((unsigned char)*display_text)) {
        display_text++;
    }
    if (display_reasoning) {
        while (*display_reasoning && isspace((unsigned char)*display_reasoning)) {
            display_reasoning++;
        }
    }

    if (*display_text == '\0' && (!display_reasoning || *display_reasoning == '\0')) {
        LOG_DEBUG("SQLite Queue: Skipping empty assistant response");
        return 0;
    }

    LOG_INFO("SQLite Queue: Sending assistant text response");

    // Print to console with TUI-like formatting
    // Use colorscheme colors with ANSI fallback for console output
    char assistant_color[32] = {0};
    char text_color[32] = {0};
    const char *assistant_color_start = NULL;
    const char *text_color_start = NULL;

    // Get assistant color for the role name
    if (get_colorscheme_color(COLORSCHEME_ASSISTANT, assistant_color, sizeof(assistant_color)) == 0) {
        assistant_color_start = assistant_color;
    } else {
        assistant_color_start = ANSI_FALLBACK_ASSISTANT;
    }

    // Get foreground color for the text content
    if (get_colorscheme_color(COLORSCHEME_FOREGROUND, text_color, sizeof(text_color)) == 0) {
        text_color_start = text_color;
    } else {
        text_color_start = ANSI_FALLBACK_FOREGROUND;
    }

    if (display_reasoning && *display_reasoning) {
        printf("\n%s[Assistant thinking]%s %s%s%s\n",
               assistant_color_start, ANSI_RESET,
               text_color_start, display_reasoning, ANSI_RESET);
    }

    if (*display_text != '\0') {
        printf("\n%s[Assistant]%s %s%s%s\n",
               assistant_color_start, ANSI_RESET,
               text_color_start, display_text, ANSI_RESET);
    }
    fflush(stdout);

    // Create JSON with optional reasoning_content
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        LOG_ERROR("SQLite Queue: Failed to create TEXT JSON object");
        return -1;
    }

    cJSON_AddStringToObject(json, "messageType", "TEXT");
    cJSON_AddStringToObject(json, "content", display_text);

    // Add reasoning_content if present
    if (display_reasoning && display_reasoning[0] != '\0') {
        cJSON_AddStringToObject(json, "reasoningContent", display_reasoning);
        LOG_DEBUG("SQLite Queue: Added reasoning_content (%zu bytes) to TEXT message",
                  strlen(display_reasoning));
    }

    char *json_str = cJSON_PrintUnformatted(json);
    if (!json_str) {
        LOG_ERROR("SQLite Queue: Failed to serialize TEXT JSON");
        cJSON_Delete(json);
        return -1;
    }

    int result = sqlite_queue_send(ctx, receiver, json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(json);

    return result;
}

// Deprecated: reasoning_content is now bundled with TEXT and TOOL messages.
// Kept for reference, remove once REASONING message type is fully deprecated.
// Helper function to send assistant reasoning content (for thinking models)
// static int sqlite_queue_send_reasoning(SQLiteQueueContext *ctx, const char *receiver,
//                                        const char *reasoning_content) { ... }

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
                                         cJSON *tool_parameters, const char *reasoning_content) {
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

    // Add reasoning_content if present
    if (reasoning_content && reasoning_content[0] != '\0') {
        cJSON_AddStringToObject(request_json, "reasoningContent", reasoning_content);
        LOG_DEBUG("SQLite Queue: Added reasoning_content (%zu bytes) to TOOL message for %s",
                  strlen(reasoning_content), tool_name);
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

// Structure to hold callback context for unified processor
typedef struct {
    SQLiteQueueContext *ctx;
    const char *response_receiver;
} SQLiteQueueCallbackContext;

// Extended callbacks with full tool context for sending TOOL/TOOL_RESULT messages
static void sqlite_on_tool_start_ex(const char *tool_id, const char *tool_name,
                                    cJSON *tool_parameters, const char *tool_details,
                                    const char *reasoning_content, void *user_data) {
    SQLiteQueueCallbackContext *cb_ctx = (SQLiteQueueCallbackContext *)user_data;

    LOG_INFO("SQLite Queue: Starting tool: %s (id: %s)", tool_name, tool_id);

    // Get tool details from parameters if not provided
    const char *details = tool_details;
    char *generated_details = NULL;
    if (!details && tool_parameters) {
        generated_details = get_tool_details(tool_name, tool_parameters);
        details = generated_details;
    }

    // Print to console with TUI-like formatting
    // Use colorscheme colors with ANSI fallback for console output
    char tool_color[32] = {0};
    char text_color[32] = {0};
    const char *tool_color_start = NULL;
    const char *text_color_start = NULL;

    // Get tool color (use TOOL color for the bullet/name)
    if (get_colorscheme_color(COLORSCHEME_TOOL, tool_color, sizeof(tool_color)) == 0) {
        tool_color_start = tool_color;
    } else {
        tool_color_start = ANSI_FALLBACK_TOOL;
    }

    // Get foreground color for details
    if (get_colorscheme_color(COLORSCHEME_FOREGROUND, text_color, sizeof(text_color)) == 0) {
        text_color_start = text_color;
    } else {
        text_color_start = ANSI_FALLBACK_FOREGROUND;
    }

    // Print tool execution in TUI format: ● ToolName details
    printf("\n%s● %s%s", tool_color_start, tool_name, ANSI_RESET);
    if (details && strlen(details) > 0) {
        printf(" %s%s%s", text_color_start, details, ANSI_RESET);
    }
    printf("\n");
    fflush(stdout);

    if (generated_details) {
        // generated_details is a static buffer from get_tool_details, no need to free
    }

    // Send TOOL message to the queue
    sqlite_queue_send_tool_request(cb_ctx->ctx, cb_ctx->response_receiver,
                                   tool_name, tool_id, tool_parameters, reasoning_content);
}

static void sqlite_on_tool_complete_ex(const char *tool_id, const char *tool_name,
                                       cJSON *result, int is_error, void *user_data) {
    SQLiteQueueCallbackContext *cb_ctx = (SQLiteQueueCallbackContext *)user_data;

    LOG_INFO("SQLite Queue: Tool completed: %s (id: %s, error=%d)", tool_name, tool_id, is_error);

    // Print tool result to console using the same formatter as the TUI
    // Get tool details for display
    char *tool_details = NULL;
    if (result) {
        // Try to extract details from result if available
        cJSON *args = cJSON_GetObjectItem(result, "arguments");
        if (args) {
            tool_details = get_tool_details(tool_name, args);
        }
    }

    // Use the human-readable formatter for tool output (mirrors TUI display)
    // Skip header since tool name was already printed at tool start
    print_human_readable_tool_output(tool_name, tool_details ? tool_details : "", result, 1);

    // Send TOOL_RESULT message to the queue
    sqlite_queue_send_tool_result(cb_ctx->ctx, cb_ctx->response_receiver,
                                  tool_name, tool_id, result, is_error);
}

static void sqlite_on_assistant_text(const char *text, const char *reasoning_content, void *user_data) {
    SQLiteQueueCallbackContext *cb_ctx = (SQLiteQueueCallbackContext *)user_data;
    sqlite_queue_send_text_response(cb_ctx->ctx, cb_ctx->response_receiver,
                                    text ? text : "", reasoning_content);
}

static void sqlite_on_assistant_reasoning(const char *reasoning_content, void *user_data) {
    // Deprecated: reasoning_content is now handled via on_assistant_text or on_tool_start_ex
    // Keep this for backward compatibility but don't use it
    (void)reasoning_content;
    (void)user_data;
    LOG_DEBUG("SQLite Queue: sqlite_on_assistant_reasoning called (deprecated callback)");
}

static void sqlite_on_error(const char *error_message, void *user_data) {
    SQLiteQueueCallbackContext *cb_ctx = (SQLiteQueueCallbackContext *)user_data;
    sqlite_queue_send_json(cb_ctx->ctx, cb_ctx->response_receiver, "ERROR", error_message);
}

static int sqlite_should_interrupt(void *user_data) {
    SQLiteQueueCallbackContext *cb_ctx = (SQLiteQueueCallbackContext *)user_data;
    return cb_ctx->ctx->state && cb_ctx->ctx->state->interrupt_requested;
}

static void sqlite_on_status_update(const char *status, void *user_data) {
    (void)user_data;
    LOG_INFO("SQLite Queue: %s", status);
}

// Check for and inject pending messages
static void check_and_inject_pending_messages(SQLiteQueueContext *ctx,
                                               struct ConversationState *state,
                                               const char *response_receiver) {
    (void)response_receiver;
    pthread_mutex_lock(&ctx->queue_mutex);
    while (ctx->pending_messages != NULL) {
        PendingMessage *pm = ctx->pending_messages;
        ctx->pending_messages = pm->next;
        if (ctx->pending_messages == NULL) {
            ctx->pending_tail = NULL;
        }
        ctx->pending_count--;
        pthread_mutex_unlock(&ctx->queue_mutex);

        LOG_INFO("SQLite Queue: Injecting pending user message ID %lld", pm->msg_id);
        sqlite_queue_acknowledge(ctx, pm->msg_id);

        add_user_message(state, pm->content);

        free(pm->content);
        free(pm);

        pthread_mutex_lock(&ctx->queue_mutex);
    }
    pthread_mutex_unlock(&ctx->queue_mutex);
}

// Callback for real-time steering injection point.
// Called after tool results are added but before next API call.
// This is a safe point to inject user messages without breaking tool-result pairs.
static void sqlite_on_after_tool_results(struct ConversationState *state, void *user_data) {
    SQLiteQueueCallbackContext *cb_ctx = (SQLiteQueueCallbackContext *)user_data;
    SQLiteQueueContext *ctx = cb_ctx->ctx;

    // Check for pending messages and inject them into the conversation.
    // This enables real-time steering - user messages sent during tool execution
    // will be seen by the LLM in the next API call.
    pthread_mutex_lock(&ctx->queue_mutex);
    int pending_count = ctx->pending_count;
    pthread_mutex_unlock(&ctx->queue_mutex);

    if (pending_count > 0) {
        LOG_INFO("SQLite Queue: Real-time steering injection point - %d pending message(s)", pending_count);
        check_and_inject_pending_messages(ctx, state, cb_ctx->response_receiver);
    }
}

// Process SQLite message with unified conversation processor
/*
 * Returns 1 if the last message sent by klawed in the DB was END_AI_TURN,
 * 0 if the turn appears incomplete (or there are no prior messages at all),
 * -1 on error.
 */
static int sqlite_queue_last_turn_complete(SQLiteQueueContext *ctx) {
    if (!ctx) return -1;

    if (!ctx->db_handle && sqlite_queue_open_db(ctx) != 0) {
        return -1;
    }

    sqlite3 *db = (sqlite3 *)ctx->db_handle;
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, SELECT_LAST_KLAWED_MESSAGE_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("SQLite Queue: last_turn_complete: prepare failed: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, ctx->sender_name, -1, SQLITE_STATIC);

    int result = 0; /* default: no prior message → treat as incomplete */
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *message = (const char *)sqlite3_column_text(stmt, 0);
        if (message) {
            cJSON *json = cJSON_Parse(message);
            if (json) {
                cJSON *jtype = cJSON_GetObjectItem(json, "messageType");
                if (jtype && cJSON_IsString(jtype) &&
                    strcmp(jtype->valuestring, "END_AI_TURN") == 0) {
                    result = 1;
                }
                cJSON_Delete(json);
            }
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

/*
 * Resume a pending user turn that was left unanswered before klawed was last stopped.
 * Called on startup when conversation history ends with a user message (MSG_USER) that
 * has no assistant response yet. Calls the LLM directly without adding a new user message.
 */
static int sqlite_queue_resume_pending_turn(SQLiteQueueContext *ctx, struct ConversationState *state) {
    if (!ctx || !state) {
        LOG_ERROR("SQLite Queue: resume_pending_turn: invalid parameters");
        return -1;
    }

    LOG_INFO("SQLite Queue: Resuming pending user turn from previous session");
    printf("SQLite Queue: Resuming unanswered user message from previous session...\n");
    fflush(stdout);

    const char *response_receiver = "client";

    SQLiteQueueCallbackContext cb_ctx = {
        .ctx = ctx,
        .response_receiver = response_receiver
    };

    ProcessingContext proc_ctx = {0};
    processing_context_init(&proc_ctx);
    proc_ctx.execution_mode = EXEC_MODE_SERIAL;
    proc_ctx.output_format = OUTPUT_FORMAT_PLAIN;
    proc_ctx.max_iterations = ctx->max_iterations;
    proc_ctx.user_data = &cb_ctx;
    proc_ctx.on_tool_start_ex = sqlite_on_tool_start_ex;
    proc_ctx.on_tool_complete_ex = sqlite_on_tool_complete_ex;
    proc_ctx.on_assistant_text = sqlite_on_assistant_text;
    proc_ctx.on_assistant_reasoning = sqlite_on_assistant_reasoning;
    proc_ctx.on_error = sqlite_on_error;
    proc_ctx.should_interrupt = sqlite_should_interrupt;
    proc_ctx.on_status_update = sqlite_on_status_update;
    proc_ctx.on_after_tool_results = sqlite_on_after_tool_results;

    /* Call the LLM with the existing conversation state (last message is already MSG_USER). */
    if (proc_ctx.on_status_update) {
        proc_ctx.on_status_update("Calling AI...", proc_ctx.user_data);
    }

    ApiResponse *response = call_api_with_retries(state);
    if (!response) {
        LOG_ERROR("SQLite Queue: resume_pending_turn: API call failed");
        if (proc_ctx.on_error) {
            proc_ctx.on_error("Failed to get response from API", proc_ctx.user_data);
        }
        sqlite_queue_send_end_ai_turn(ctx, response_receiver);
        return -1;
    }

    if (response->error_message) {
        if (proc_ctx.on_error) {
            proc_ctx.on_error(response->error_message, proc_ctx.user_data);
        }
        api_response_free(response);
        sqlite_queue_send_end_ai_turn(ctx, response_receiver);
        return -1;
    }

    int result = process_response_unified(state, response, &proc_ctx);
    api_response_free(response);

    sqlite_queue_send_end_ai_turn(ctx, response_receiver);

    char status_color[32] = {0};
    const char *status_color_start = NULL;
    if (get_colorscheme_color(COLORSCHEME_STATUS, status_color, sizeof(status_color)) == 0) {
        status_color_start = status_color;
    } else {
        status_color_start = ANSI_FALLBACK_STATUS;
    }
    printf("\n%s[Ready]%s Waiting for next message...\n", status_color_start, ANSI_RESET);
    fflush(stdout);

    return result;
}

static int sqlite_queue_process_interactive(SQLiteQueueContext *ctx,
                                            struct ConversationState *state, const char *user_input) {
    if (!ctx || !state || !user_input) {
        LOG_ERROR("SQLite Queue: Invalid parameters for process_interactive");
        return -1;
    }

    // Clear any stale interrupt flag from a previous Stop before starting a new turn.
    // Without this, call_api_with_retries' pre-call check fires immediately and the
    // user's message is silently dropped (see docs/bug-interrupt-flag-not-cleared.md).
    if (state->interrupt_requested) {
        LOG_INFO("SQLite Queue: Clearing stale interrupt flag at start of new turn");
        state->interrupt_requested = 0;
    }

    LOG_INFO("SQLite Queue: Processing interactive message: %.*s",
             (int)(strlen(user_input) > 200 ? 200 : strlen(user_input)), user_input);

    const char *response_receiver = "client";

    // Set up callback context
    SQLiteQueueCallbackContext cb_ctx = {
        .ctx = ctx,
        .response_receiver = response_receiver
    };

    // Set up processing context
    ProcessingContext proc_ctx = {0};
    processing_context_init(&proc_ctx);
    proc_ctx.execution_mode = EXEC_MODE_SERIAL;  // SQLite queue uses serial execution
    proc_ctx.output_format = OUTPUT_FORMAT_PLAIN;
    proc_ctx.max_iterations = ctx->max_iterations;
    proc_ctx.user_data = &cb_ctx;
    // Use extended callbacks to send TOOL/TOOL_RESULT messages to the queue
    proc_ctx.on_tool_start_ex = sqlite_on_tool_start_ex;
    proc_ctx.on_tool_complete_ex = sqlite_on_tool_complete_ex;
    proc_ctx.on_assistant_text = sqlite_on_assistant_text;
    proc_ctx.on_assistant_reasoning = sqlite_on_assistant_reasoning;
    proc_ctx.on_error = sqlite_on_error;
    proc_ctx.should_interrupt = sqlite_should_interrupt;
    proc_ctx.on_status_update = sqlite_on_status_update;
    // Enable real-time steering by injecting pending messages at safe points
    proc_ctx.on_after_tool_results = sqlite_on_after_tool_results;

    // Check for pending messages before starting
    check_and_inject_pending_messages(ctx, state, response_receiver);

    // Process the user instruction using unified processor
    int result = process_user_instruction(state, user_input, &proc_ctx);

    // Check for any pending messages that arrived during processing
    check_and_inject_pending_messages(ctx, state, response_receiver);

    if (result != 0) {
        LOG_ERROR("SQLite Queue: Processing failed with result %d", result);
        // Error already sent via callback
        // Still send END_AI_TURN so client knows we're ready for next instruction
        sqlite_queue_send_end_ai_turn(ctx, response_receiver);
        return result;
    }

    LOG_INFO("SQLite Queue: Interactive processing completed successfully");

    // Send END_AI_TURN event to indicate klawed is waiting for further instruction
    sqlite_queue_send_end_ai_turn(ctx, response_receiver);

    // Print subtle prompt indicator for console mode
    char status_color[32] = {0};
    const char *status_color_start = NULL;

    if (get_colorscheme_color(COLORSCHEME_STATUS, status_color, sizeof(status_color)) == 0) {
        status_color_start = status_color;
    } else {
        status_color_start = ANSI_FALLBACK_STATUS;
    }

    printf("\n%s[Ready]%s Waiting for next message...\n", status_color_start, ANSI_RESET);
    fflush(stdout);

    return 0;
}
#endif // TEST_BUILD

// Helper function to send auto-compaction notice
// NOTE: This function is NOT wrapped in #ifndef TEST_BUILD because it's called from api_client.c
// which needs to work in both normal and test builds.
int sqlite_queue_send_compaction_notice(SQLiteQueueContext *ctx, const char *receiver,
                                       int messages_compacted, size_t tokens_before,
                                        size_t tokens_after, double usage_before_pct,
                                        double usage_after_pct, const char *summary) {
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

    // Add AI-generated summary if available
    if (summary && summary[0] != '\0') {
        cJSON_AddStringToObject(notice_json, "summary", summary);
    }

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

    // NOTE: We do NOT self-send a TEXT message here. The compaction notice is
    // already present in the conversation state as a MSG_AUTO_COMPACTION message
    // (added directly by compaction_perform). Self-sending it as a TEXT would
    // cause it to be processed as a user instruction, triggering a spurious LLM
    // API call with an invalid payload (no user message following the compaction
    // notice system message).

    return result;
}

#ifndef TEST_BUILD
/**
 * Handle TRIGGER_COMPACT message from client
 * Performs manual compaction on the current conversation state
 */
static int sqlite_queue_handle_compact_trigger(SQLiteQueueContext *ctx, struct ConversationState *state) {
    if (!ctx || !state) {
        LOG_ERROR("SQLite Queue: Invalid parameters for handle_compact_trigger");
        return -1;
    }

    const char *response_receiver = "client";

    // Check if we have a compaction config
    if (!state->compaction_config) {
        // Create a temporary config for manual compaction
        state->compaction_config = reallocarray(NULL, 1, sizeof(CompactionConfig));
        if (!state->compaction_config) {
            LOG_ERROR("SQLite Queue: Failed to allocate compaction config");
            sqlite_queue_send_json(ctx, response_receiver, "ERROR",
                                   "Failed to allocate memory for compaction config");
            return -1;
        }
        compaction_init_config(state->compaction_config, 1, state->model);
        LOG_DEBUG("SQLite Queue: Created temporary compaction config for manual trigger");
    }

    // Update token count before compaction
    compaction_update_token_count(state, state->compaction_config);

    LOG_INFO("SQLite Queue: Performing manual compaction (triggered by client)");

    // Perform compaction
    CompactionResult result = {0};
    int ret = compaction_perform(state, state->compaction_config, state->session_id, &result);

    if (ret == 0 && result.success) {
        LOG_INFO("SQLite Queue: Compaction successful - %d messages compacted, %.1f%% -> %.1f%%",
                 result.messages_compacted, result.usage_before_pct, result.usage_after_pct);

        // Send success response with details
        char response_msg[512];
        snprintf(response_msg, sizeof(response_msg),
                 "Compaction successful: %d messages stored to memory. "
                 "Tokens: %zu -> %zu (freed ~%zu). "
                 "Usage: %.1f%% -> %.1f%%.",
                 result.messages_compacted,
                 result.tokens_before, result.tokens_after,
                 result.tokens_before - result.tokens_after,
                 result.usage_before_pct, result.usage_after_pct);

        // Also send an AUTO_COMPACTION notice for consistency
        sqlite_queue_send_compaction_notice(ctx, response_receiver,
                                            result.messages_compacted,
                                            result.tokens_before,
                                            result.tokens_after,
                                            result.usage_before_pct,
                                            result.usage_after_pct,
                                            result.summary[0] != '\0' ? result.summary : NULL);

        // Send END_AI_TURN so that on restart last_turn_complete() correctly
        // sees the TRIGGER_COMPACT as fully handled and does not spuriously
        // call the LLM with no pending user message.
        sqlite_queue_send_end_ai_turn(ctx, response_receiver);

        return 0;

    } else if (ret == 0) {
        // Nothing to compact - not enough messages
        LOG_INFO("SQLite Queue: No compaction needed - not enough messages");
        sqlite_queue_send_json(ctx, response_receiver, "TEXT",
                               "Compaction skipped: Not enough messages to compact "
                               "(need at least keep_recent + system message)");
        return 0;

    } else {
        LOG_ERROR("SQLite Queue: Compaction failed");
        sqlite_queue_send_json(ctx, response_receiver, "ERROR",
                               "Compaction failed - see klawed logs for details");
        return -1;
    }
}
#endif // TEST_BUILD

/**
 * Pending assistant turn accumulator used during conversation restore.
 * Collects TEXT and TOOL_CALL content blocks belonging to one assistant
 * turn before committing them to ConversationState.
 */
typedef struct {
    InternalContent *contents;
    int count;
    int capacity;
} PendingAssistant;

/**
 * Buffer for user TEXT messages that arrived while a tool round-trip was
 * in flight.  These cannot be inserted immediately (the assistant turn has
 * open tool_use blocks without matching tool_results yet), so we hold them
 * here and inject them as user messages once all tool results have landed.
 * Mirrors what sqlite_on_after_tool_results does on the live path.
 */
typedef struct {
    char **texts;
    int count;
    int capacity;
} DeferredUserTexts;

static int deferred_user_texts_append(DeferredUserTexts *d, const char *text) {
    if (d->count >= d->capacity) {
        int new_cap = d->capacity == 0 ? 4 : d->capacity * 2;
        char **tmp = reallocarray(d->texts, (size_t)new_cap, sizeof(char *));
        if (!tmp) return -1;
        d->texts    = tmp;
        d->capacity = new_cap;
    }
    d->texts[d->count] = strdup(text);
    if (!d->texts[d->count]) return -1;
    d->count++;
    return 0;
}

static void deferred_user_texts_free(DeferredUserTexts *d) {
    for (int i = 0; i < d->count; i++) free(d->texts[i]);
    free(d->texts);
    d->texts    = NULL;
    d->count    = 0;
    d->capacity = 0;
}

/*
 * Flush all buffered deferred user texts into state as individual user
 * messages, then reset the buffer.  Safe to call when d->count == 0.
 */
static int flush_deferred_user_texts(ConversationState *state,
                                     DeferredUserTexts *d,
                                     int *restored_out) {
    for (int i = 0; i < d->count; i++) {
        if (state->count >= MAX_MESSAGES) {
            LOG_ERROR("SQLite Queue restore: MAX_MESSAGES reached, dropping deferred user text");
            deferred_user_texts_free(d);
            return -1;
        }
        add_user_message(state, d->texts[i]);
        if (restored_out) (*restored_out)++;
    }
    deferred_user_texts_free(d);
    return 0;
}

static int pending_assistant_append(PendingAssistant *pa, InternalContent item) {
    if (pa->count >= pa->capacity) {
        int new_cap = pa->capacity == 0 ? 4 : pa->capacity * 2;
        InternalContent *tmp = reallocarray(pa->contents, (size_t)new_cap, sizeof(InternalContent));
        if (!tmp) return -1;
        pa->contents = tmp;
        pa->capacity = new_cap;
    }
    pa->contents[pa->count++] = item;
    return 0;
}

static void pending_assistant_free(PendingAssistant *pa) {
    for (int i = 0; i < pa->count; i++) {
        free(pa->contents[i].text);
        free(pa->contents[i].reasoning_content);
        free(pa->contents[i].tool_id);
        free(pa->contents[i].tool_name);
        if (pa->contents[i].tool_params) cJSON_Delete(pa->contents[i].tool_params);
        if (pa->contents[i].tool_output) cJSON_Delete(pa->contents[i].tool_output);
    }
    free(pa->contents);
    pa->contents = NULL;
    pa->count = 0;
    pa->capacity = 0;
}

/*
 * Flush the pending assistant turn into state as a single MSG_ASSISTANT message.
 * Transfers ownership of contents to state on success; resets pa.
 * Returns 0 on success, -1 if MAX_MESSAGES reached (pa is freed on failure).
 */
static int flush_pending_assistant(ConversationState *state, PendingAssistant *pa) {
    if (pa->count == 0) return 0;

    if (state->count >= MAX_MESSAGES) {
        LOG_ERROR("SQLite Queue restore: MAX_MESSAGES reached, cannot flush assistant turn");
        pending_assistant_free(pa);
        return -1;
    }

    InternalMessage *msg = &state->messages[state->count];
    msg->role = MSG_ASSISTANT;
    msg->contents = pa->contents;
    msg->content_count = pa->count;
    state->count++;

    /* Transfer ownership: reset pa without freeing contents */
    pa->contents = NULL;
    pa->count = 0;
    pa->capacity = 0;

    return 0;
}

/**
 * Restore conversation history from an existing SQLite queue database.
 * Called automatically at daemon startup to resume after a crash or restart.
 * Loads up to 200 most recent processed messages (sent=1) and reconstructs
 * the conversation: user TEXT turns, assistant TEXT+TOOL turns, tool results.
 * Interrupted tool calls (no matching TOOL_RESULT) get synthetic error results.
 *
 * @param ctx SQLite queue context
 * @param state Conversation state to populate
 * @return Number of message blocks restored, or -1 on error
 */
int sqlite_queue_restore_conversation(SQLiteQueueContext *ctx, struct ConversationState *state) {
    if (!ctx || !state) {
        LOG_ERROR("SQLite Queue: restore_conversation: invalid parameters");
        return -1;
    }

    if (!ctx->db_handle && sqlite_queue_open_db(ctx) != 0) {
        return -1;
    }

    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, SELECT_RESTORE_MESSAGES_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("SQLite Queue: restore_conversation: prepare failed: %s", sqlite3_errmsg(db));
        return -1;
    }

    PendingAssistant pa = {0};
    DeferredUserTexts deferred = {0};
    int open_tool_calls = 0;  /* # of TOOL rows seen without a matching TOOL_RESULT */
    int restored = 0;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *sender  = (const char *)sqlite3_column_text(stmt, 1);
        const char *message = (const char *)sqlite3_column_text(stmt, 2);
        if (!sender || !message) continue;

        cJSON *json = cJSON_Parse(message);
        if (!json) continue;

        cJSON *jtype = cJSON_GetObjectItem(json, "messageType");
        if (!jtype || !cJSON_IsString(jtype)) { cJSON_Delete(json); continue; }
        const char *mt = jtype->valuestring;

        int from_klawed = (strcmp(sender, ctx->sender_name) == 0);

        if (strcmp(mt, "TEXT") == 0) {
            cJSON *jcontent = cJSON_GetObjectItem(json, "content");
            if (!jcontent || !cJSON_IsString(jcontent)) { cJSON_Delete(json); continue; }
            const char *text = jcontent->valuestring;

            /* Skip empty / whitespace-only content */
            const char *p = text;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '\0') { cJSON_Delete(json); continue; }

            if (from_klawed) {
                /* Assistant text: accumulate into pending assistant turn */
                InternalContent c = {0};
                c.type = INTERNAL_TEXT;
                c.text = strdup(text);
                if (!c.text || pending_assistant_append(&pa, c) != 0) {
                    free(c.text);
                    LOG_ERROR("SQLite Queue: restore: OOM appending assistant text");
                    cJSON_Delete(json);
                    break;
                }

                /* Extract reasoning_content from TEXT message if present */
                cJSON *jreasoning = cJSON_GetObjectItem(json, "reasoningContent");
                if (jreasoning && cJSON_IsString(jreasoning) && jreasoning->valuestring) {
                    const char *reasoning = jreasoning->valuestring;
                    /* Skip empty / whitespace-only reasoning */
                    const char *pr = reasoning;
                    while (*pr && isspace((unsigned char)*pr)) pr++;
                    if (*pr != '\0') {
                        pa.contents[pa.count - 1].reasoning_content = strdup(reasoning);
                        if (!pa.contents[pa.count - 1].reasoning_content) {
                            LOG_ERROR("SQLite Queue: restore: OOM storing reasoning_content");
                        } else {
                            LOG_DEBUG("SQLite Queue: restore: stored reasoning_content (%zu bytes) on TEXT content",
                                      strlen(reasoning));
                        }
                    }
                }
            } else {
                /* User text: if there are open (unresolved) tool calls in the
                 * pending assistant turn, we cannot flush yet — doing so would
                 * produce an illegal assistant:[tool_use] → user:[text] sequence
                 * without the required tool_result in between.  Buffer the text
                 * and inject it after the tool round-trip completes, mirroring
                 * what sqlite_on_after_tool_results does on the live path. */
                if (open_tool_calls > 0) {
                    if (deferred_user_texts_append(&deferred, text) != 0) {
                        LOG_ERROR("SQLite Queue: restore: OOM buffering deferred user text");
                        cJSON_Delete(json);
                        break;
                    }
                    LOG_INFO("SQLite Queue: restore: deferring user text (open_tool_calls=%d)", open_tool_calls);
                } else {
                    /* Normal case: flush pending assistant turn, then add user message */
                    if (flush_pending_assistant(state, &pa) != 0) {
                        cJSON_Delete(json);
                        break;
                    }
                    if (state->count < MAX_MESSAGES) {
                        add_user_message(state, text);
                        restored++;
                    }
                }
            }

        } else if (strcmp(mt, "REASONING") == 0 && from_klawed) {
            /* Assistant reasoning content (for thinking models): accumulate into pending assistant turn */
            cJSON *jreasoning = cJSON_GetObjectItem(json, "reasoningContent");
            if (!jreasoning || !cJSON_IsString(jreasoning)) { cJSON_Delete(json); continue; }
            const char *reasoning = jreasoning->valuestring;

            /* Skip empty / whitespace-only content */
            const char *p = reasoning;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '\0') { cJSON_Delete(json); continue; }

            /* Add reasoning_content to the most recent pending assistant content block
             * (either the last text block or the last tool call block).
             *
             * IMPORTANT: If the last content block is a TEXT block with non-empty text content,
             * this means the TEXT is a response from the previous turn, and the REASONING
             * belongs to a new turn. In this case, we should NOT attach the reasoning_content
             * to the existing TEXT block. Instead, create a new synthetic text block for the
             * reasoning. This prevents the "reasoning_content is missing in assistant tool call
             * message" error when multiple turns are merged (e.g., due to missing END_AI_TURN). */
            if (pa.count > 0) {
                int last_idx = pa.count - 1;
                int create_new_block = 0;

                /* Check if last block is TEXT with non-empty content - if so, don't attach reasoning to it */
                if (pa.contents[last_idx].type == INTERNAL_TEXT &&
                    pa.contents[last_idx].text &&
                    pa.contents[last_idx].text[0] != '\0') {
                    /* Last block is a text response, reasoning belongs to next turn */
                    create_new_block = 1;
                    LOG_DEBUG("SQLite Queue: restore: last block is text response, creating new block for reasoning");
                }

                if (create_new_block) {
                    /* Create a new synthetic text block to hold the reasoning for the new turn */
                    InternalContent c = {0};
                    c.type = INTERNAL_TEXT;
                    c.text = strdup("");  /* Empty text, reasoning_content holds the actual content */
                    c.reasoning_content = strdup(reasoning);
                    if (!c.text || !c.reasoning_content || pending_assistant_append(&pa, c) != 0) {
                        free(c.text);
                        free(c.reasoning_content);
                        LOG_ERROR("SQLite Queue: restore: OOM appending synthetic text with reasoning for new turn");
                        cJSON_Delete(json);
                        break;
                    }
                    LOG_DEBUG("SQLite Queue: restore: created synthetic text block for new turn with reasoning_content (%zu bytes)",
                              strlen(reasoning));
                } else {
                    /* Attach reasoning to existing block (tool call or empty text block) */
                    pa.contents[last_idx].reasoning_content = strdup(reasoning);
                    if (!pa.contents[last_idx].reasoning_content) {
                        LOG_ERROR("SQLite Queue: restore: OOM storing reasoning content");
                        cJSON_Delete(json);
                        break;
                    }
                    LOG_DEBUG("SQLite Queue: restore: stored reasoning_content (%zu bytes) on content block %d",
                              strlen(reasoning), last_idx);
                }
            } else {
                /* No pending assistant content - create a synthetic text block to hold the reasoning */
                InternalContent c = {0};
                c.type = INTERNAL_TEXT;
                c.text = strdup("");  /* Empty text, reasoning_content holds the actual content */
                c.reasoning_content = strdup(reasoning);
                if (!c.text || !c.reasoning_content || pending_assistant_append(&pa, c) != 0) {
                    free(c.text);
                    free(c.reasoning_content);
                    LOG_ERROR("SQLite Queue: restore: OOM appending synthetic text with reasoning");
                    cJSON_Delete(json);
                    break;
                }
                LOG_DEBUG("SQLite Queue: restore: created synthetic text block with reasoning_content (%zu bytes)",
                          strlen(reasoning));
            }

        } else if (strcmp(mt, "TOOL") == 0 && from_klawed) {
            /* Assistant tool call: add to pending assistant turn */
            cJSON *jtool_id     = cJSON_GetObjectItem(json, "toolId");
            cJSON *jtool_name   = cJSON_GetObjectItem(json, "toolName");
            cJSON *jtool_params = cJSON_GetObjectItem(json, "toolParameters");

            if (!jtool_id || !cJSON_IsString(jtool_id) ||
                !jtool_name || !cJSON_IsString(jtool_name)) {
                LOG_WARN("SQLite Queue: restore: TOOL missing toolId/toolName, skipping");
                cJSON_Delete(json);
                continue;
            }

            InternalContent c = {0};
            c.type        = INTERNAL_TOOL_CALL;
            c.tool_id     = strdup(jtool_id->valuestring);
            c.tool_name   = strdup(jtool_name->valuestring);
            c.tool_params = (jtool_params && cJSON_IsObject(jtool_params))
                            ? cJSON_Duplicate(jtool_params, 1)
                            : cJSON_CreateObject();

            /* Extract reasoning_content from TOOL message if present (for Moonshot/Kimi) */
            cJSON *jreasoning = cJSON_GetObjectItem(json, "reasoningContent");
            if (jreasoning && cJSON_IsString(jreasoning) && jreasoning->valuestring) {
                const char *reasoning = jreasoning->valuestring;
                /* Skip empty / whitespace-only reasoning */
                const char *pr = reasoning;
                while (*pr && isspace((unsigned char)*pr)) pr++;
                if (*pr != '\0') {
                    c.reasoning_content = strdup(reasoning);
                    if (c.reasoning_content) {
                        LOG_DEBUG("SQLite Queue: restore: stored reasoning_content (%zu bytes) on TOOL call",
                                  strlen(reasoning));
                    }
                }
            }

            if (!c.tool_id || !c.tool_name || !c.tool_params ||
                pending_assistant_append(&pa, c) != 0) {
                free(c.tool_id);
                free(c.tool_name);
                free(c.reasoning_content);
                if (c.tool_params) cJSON_Delete(c.tool_params);
                LOG_ERROR("SQLite Queue: restore: OOM appending tool call");
                cJSON_Delete(json);
                break;
            }
            open_tool_calls++;

        } else if (strcmp(mt, "TOOL_RESULT") == 0 && from_klawed) {
            /* Tool result: flush pending assistant turn, then add MSG_USER with result */
            if (flush_pending_assistant(state, &pa) != 0) {
                cJSON_Delete(json);
                break;
            }

            cJSON *jtool_id     = cJSON_GetObjectItem(json, "toolId");
            cJSON *jtool_name   = cJSON_GetObjectItem(json, "toolName");
            cJSON *jtool_output = cJSON_GetObjectItem(json, "toolOutput");
            cJSON *jis_error    = cJSON_GetObjectItem(json, "isError");

            if (!jtool_id || !cJSON_IsString(jtool_id)) {
                LOG_WARN("SQLite Queue: restore: TOOL_RESULT missing toolId, skipping");
                cJSON_Delete(json);
                continue;
            }

            if (state->count < MAX_MESSAGES) {
                InternalContent *result = calloc(1, sizeof(InternalContent));
                if (!result) {
                    LOG_ERROR("SQLite Queue: restore: OOM allocating tool result");
                    cJSON_Delete(json);
                    break;
                }
                result->type        = INTERNAL_TOOL_RESPONSE;
                result->tool_id     = strdup(jtool_id->valuestring);
                result->tool_name   = strdup(jtool_name && cJSON_IsString(jtool_name)
                                             ? jtool_name->valuestring : "unknown");
                result->is_error    = (jis_error && cJSON_IsBool(jis_error))
                                      ? jis_error->valueint : 0;
                result->tool_output = (jtool_output && !cJSON_IsNull(jtool_output))
                                      ? cJSON_Duplicate(jtool_output, 1)
                                      : cJSON_CreateObject();

                if (!result->tool_id || !result->tool_name || !result->tool_output) {
                    free(result->tool_id);
                    free(result->tool_name);
                    if (result->tool_output) cJSON_Delete(result->tool_output);
                    free(result);
                    LOG_ERROR("SQLite Queue: restore: OOM allocating tool result fields");
                    cJSON_Delete(json);
                    break;
                }

                InternalMessage *msg = &state->messages[state->count];
                msg->role          = MSG_USER;
                msg->contents      = result;
                msg->content_count = 1;
                state->count++;
                restored++;
            }

            /* Track that one tool call has been resolved. */
            if (open_tool_calls > 0) open_tool_calls--;

            /* Once all open tool calls in this turn are resolved, inject any
             * user messages that arrived during the tool round-trip. */
            if (open_tool_calls == 0 && deferred.count > 0) {
                LOG_INFO("SQLite Queue: restore: injecting %d deferred user message(s) after tool results",
                         deferred.count);
                if (flush_deferred_user_texts(state, &deferred, &restored) != 0) {
                    break;
                }
            }
        } else if (strcmp(mt, "END_AI_TURN") == 0 && from_klawed) {
            /* End of assistant turn: flush pending assistant content to conversation state.
             * This ensures each assistant turn is properly separated, which is critical
             * for preserving reasoning_content on the correct assistant message. */
            if (pa.count > 0) {
                if (flush_pending_assistant(state, &pa) != 0) {
                    cJSON_Delete(json);
                    break;
                }
                restored++;
                LOG_DEBUG("SQLite Queue: restore: flushed assistant turn on END_AI_TURN");
            }
        }
        /* Ignore API_CALL, ERROR, AUTO_COMPACTION, etc. */

        cJSON_Delete(json);
    }

    sqlite3_finalize(stmt);

    /* Flush any remaining pending assistant content (e.g. last text response) */
    if (pa.count > 0) {
        /* Check for tool calls without results (crash mid-tool-execution) */
        int tool_call_count = 0;
        for (int i = 0; i < pa.count; i++) {
            if (pa.contents[i].type == INTERNAL_TOOL_CALL) tool_call_count++;
        }

        /* Collect tool_id/tool_name before flush transfers ownership */
        char *interrupted_ids[16]   = {NULL};
        char *interrupted_names[16] = {NULL};
        int ni = 0;
        if (tool_call_count > 0 && tool_call_count <= 16) {
            for (int i = 0; i < pa.count && ni < 16; i++) {
                if (pa.contents[i].type == INTERNAL_TOOL_CALL) {
                    interrupted_ids[ni]   = strdup(pa.contents[i].tool_id
                                           ? pa.contents[i].tool_id   : "");
                    interrupted_names[ni] = strdup(pa.contents[i].tool_name
                                           ? pa.contents[i].tool_name : "unknown");
                    ni++;
                }
            }
        }

        if (flush_pending_assistant(state, &pa) == 0) {
            restored++;

            /* Inject synthetic error results for interrupted tool calls */
            if (tool_call_count > 0 && tool_call_count <= 16 && state->count < MAX_MESSAGES) {
                InternalContent *results = calloc((size_t)tool_call_count, sizeof(InternalContent));
                if (results) {
                    for (int i = 0; i < tool_call_count; i++) {
                        results[i].type        = INTERNAL_TOOL_RESPONSE;
                        results[i].tool_id     = interrupted_ids[i];
                        results[i].tool_name   = interrupted_names[i];
                        results[i].is_error    = 1;
                        results[i].tool_output = cJSON_CreateObject();
                        cJSON_AddStringToObject(results[i].tool_output, "error",
                            "Tool execution was interrupted - klawed restarted before result was received");
                        /* Ownership transferred to results array */
                        interrupted_ids[i]   = NULL;
                        interrupted_names[i] = NULL;
                    }
                    InternalMessage *msg = &state->messages[state->count];
                    msg->role          = MSG_USER;
                    msg->contents      = results;
                    msg->content_count = tool_call_count;
                    state->count++;
                    restored++;
                    LOG_WARN("SQLite Queue: restore: injected %d synthetic tool error(s) for interrupted calls",
                             tool_call_count);
                }
            }

            /* Inject any user messages that arrived during the interrupted
             * tool execution — same deferred-text logic as the normal path. */
            if (deferred.count > 0) {
                LOG_INFO("SQLite Queue: restore: injecting %d deferred user message(s) after interrupted tool results",
                         deferred.count);
                flush_deferred_user_texts(state, &deferred, &restored);
            }
        }

        /* Free any ids/names not transferred (OOM path) */
        for (int i = 0; i < ni; i++) {
            free(interrupted_ids[i]);
            free(interrupted_names[i]);
        }
    }

    /* Safety: free deferred buffer if we broke out of the loop early
     * (e.g. OOM) before open_tool_calls reached zero. */
    deferred_user_texts_free(&deferred);

    if (restored > 0) {
        LOG_INFO("SQLite Queue: Restored %d message block(s) from conversation history", restored);
        printf("SQLite Queue: Restored %d message block(s) from conversation history\n", restored);
        fflush(stdout);
    } else {
        LOG_INFO("SQLite Queue: No previous messages found, starting fresh");
    }

    return restored;
}


/**
 * Get the number of pending messages in the queue.
 * This allows detecting if user input was received during tool execution.
 */
int sqlite_queue_pending_count(SQLiteQueueContext *ctx) {
    if (!ctx) {
        return 0;
    }

    pthread_mutex_lock(&ctx->queue_mutex);
    int count = ctx->pending_count;
    pthread_mutex_unlock(&ctx->queue_mutex);

    return count;
}

/**
 * Interrupt the current message processing.
 * Sets the interrupt_requested flag in conversation state.
 */
int sqlite_queue_interrupt(SQLiteQueueContext *ctx) {
    if (!ctx || !ctx->state) {
        return -1;
    }

    LOG_INFO("SQLite Queue: Interrupt requested");
    ctx->state->interrupt_requested = 1;

    return 0;
}

#ifndef TEST_BUILD

// Forward declarations for async processing
static void *sqlite_queue_worker_thread(void *arg);
static int sqlite_queue_enqueue_message(SQLiteQueueContext *ctx, long long msg_id, const char *content);

/**
 * Worker thread function that processes messages from the pending queue.
 * This runs in a separate thread so the main daemon loop can continue
 * polling for new user input during tool execution.
 */
static void *sqlite_queue_worker_thread(void *arg) {
    SQLiteQueueContext *ctx = (SQLiteQueueContext *)arg;

    LOG_INFO("SQLite Queue: Worker thread started");

    for (;;) {
        // Wait for a message to process (mutex protects shutdown flag)
        pthread_mutex_lock(&ctx->queue_mutex);

        while (!ctx->shutdown && ctx->pending_messages == NULL) {
            pthread_cond_wait(&ctx->queue_cond, &ctx->queue_mutex);
        }

        if (ctx->shutdown) {
            pthread_mutex_unlock(&ctx->queue_mutex);
            break;
        }

        // Dequeue the next message
        PendingMessage *pm = ctx->pending_messages;
        if (pm) {
            ctx->pending_messages = pm->next;
            if (ctx->pending_messages == NULL) {
                ctx->pending_tail = NULL;
            }
            ctx->pending_count--;
        }
        ctx->processing = 1;
        pthread_mutex_unlock(&ctx->queue_mutex);

        if (pm) {
            LOG_INFO("SQLite Queue: Worker processing message ID %lld", pm->msg_id);
            // Only log processing at debug level to reduce console noise
            LOG_DEBUG("SQLite Queue: Processing message ID %lld", pm->msg_id);

            // Note: Message was already acknowledged by daemon loop before enqueueing
            // to prevent race conditions where the same message is found again.
            // Process the message interactively
            sqlite_queue_process_interactive(ctx, ctx->state, pm->content);

            LOG_INFO("SQLite Queue: Worker completed message ID %lld", pm->msg_id);
            LOG_DEBUG("SQLite Queue: Completed message ID %lld", pm->msg_id);

            free(pm->content);
            free(pm);
        }

        pthread_mutex_lock(&ctx->queue_mutex);
        ctx->processing = 0;
        // Signal that processing is complete (for potential waiters)
        pthread_cond_broadcast(&ctx->queue_cond);
        pthread_mutex_unlock(&ctx->queue_mutex);
    }

    LOG_INFO("SQLite Queue: Worker thread exiting");
    return NULL;
}

/**
 * Enqueue a message for processing by the worker thread.
 * Called from the main daemon loop when a new message is received.
 */
static int sqlite_queue_enqueue_message(SQLiteQueueContext *ctx, long long msg_id, const char *content) {
    if (!ctx || !content) {
        return -1;
    }

    pthread_mutex_lock(&ctx->queue_mutex);

    // Check if message is already in the pending queue (duplicate detection)
    PendingMessage *check = ctx->pending_messages;
    while (check) {
        if (check->msg_id == msg_id) {
            pthread_mutex_unlock(&ctx->queue_mutex);
            LOG_DEBUG("SQLite Queue: Message ID %lld already in queue, skipping duplicate", msg_id);
            return 0;  // Already queued, not an error
        }
        check = check->next;
    }

    pthread_mutex_unlock(&ctx->queue_mutex);

    // Acknowledge the message immediately to prevent re-receiving it
    int ack_result = sqlite_queue_acknowledge(ctx, msg_id);
    if (ack_result != SQLITE_QUEUE_ERROR_NONE) {
        LOG_WARN("SQLite Queue: Failed to acknowledge message ID %lld, may be reprocessed", msg_id);
        // Continue anyway - the message might still be processable
    }

    PendingMessage *pm = reallocarray(NULL, 1, sizeof(PendingMessage));
    if (!pm) {
        LOG_ERROR("SQLite Queue: Failed to allocate pending message");
        return -1;
    }

    pm->msg_id = msg_id;
    pm->content = strdup(content);
    if (!pm->content) {
        free(pm);
        LOG_ERROR("SQLite Queue: Failed to duplicate message content");
        return -1;
    }
    pm->next = NULL;

    pthread_mutex_lock(&ctx->queue_mutex);

    // Add to tail of queue
    if (ctx->pending_tail) {
        ctx->pending_tail->next = pm;
    } else {
        ctx->pending_messages = pm;
    }
    ctx->pending_tail = pm;
    ctx->pending_count++;

    LOG_INFO("SQLite Queue: Enqueued message ID %lld for processing (queue depth: %d)",
             msg_id, ctx->pending_count);

    pthread_cond_signal(&ctx->queue_cond);
    pthread_mutex_unlock(&ctx->queue_mutex);

    return 0;
}

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

    // Ensure persistence database is initialized before starting daemon mode
    // This is required for API call logging and session management
    if (!state->persistence_db) {
        LOG_INFO("SQLite Queue: Waiting for persistence database to be ready...");
        state->persistence_db = await_database_ready(state);
        if (!state->persistence_db) {
            LOG_ERROR("SQLite Queue: Persistence database not available, API calls will not be logged");
            // Continue anyway - the daemon can still function without persistence
        } else {
            LOG_INFO("SQLite Queue: Persistence database ready");
        }
    }

    // Print to console with status formatting
    char status_color[32] = {0};
    const char *status_color_start = NULL;

    if (get_colorscheme_color(COLORSCHEME_STATUS, status_color, sizeof(status_color)) == 0) {
        status_color_start = status_color;
    } else {
        status_color_start = ANSI_FALLBACK_STATUS;
    }

    printf("\n%s[SQLite Queue]%s Daemon started on %s\n", status_color_start, ANSI_RESET, ctx->db_path);
    printf("Sender name: %s\n", ctx->sender_name);

    /* Restore conversation from database history (crash recovery) */
    sqlite_queue_restore_conversation(ctx, state);

    /* If the last message klawed sent was not END_AI_TURN, the previous turn was
     * never completed (klawed was killed mid-response or before responding at all).
     * Resume it immediately rather than waiting for a new message. */
    if (state->count > 0 && sqlite_queue_last_turn_complete(ctx) == 0) {
        LOG_INFO("SQLite Queue: Last turn was not completed - resuming pending turn");
        sqlite_queue_resume_pending_turn(ctx, state);
    }

    // Store state in context for worker thread access
    ctx->state = state;

    // Start the worker thread
    if (pthread_create(&ctx->worker_thread, NULL, sqlite_queue_worker_thread, ctx) != 0) {
        LOG_ERROR("SQLite Queue: Failed to start worker thread");
        printf("SQLite Queue: Failed to start worker thread\n");
        return -1;
    }

    // Reuse the status_color variables already declared above
    if (get_colorscheme_color(COLORSCHEME_STATUS, status_color, sizeof(status_color)) == 0) {
        status_color_start = status_color;
    } else {
        status_color_start = ANSI_FALLBACK_STATUS;
    }

    printf("%s[Ready]%s Waiting for messages...\n", status_color_start, ANSI_RESET);
    fflush(stdout);

    int message_count = 0;
    int error_count = 0;

    // Main daemon loop - polls for messages and enqueues them for worker
    while (ctx->enabled) {
        LOG_DEBUG("SQLite Queue: Polling for messages (total processed: %d)", message_count);

        // Poll for messages from SQLite
        char **messages = NULL;
        int msg_count = 0;
        long long *message_ids = NULL;

        int result = sqlite_queue_receive(ctx, NULL, 1, &messages, &msg_count, &message_ids);

        if (result == SQLITE_QUEUE_ERROR_NO_MESSAGES || msg_count == 0) {
            // No messages, this is normal - sleep and poll again
            struct timespec sleep_time = {0, (ctx->poll_interval * 1000000L)};
            nanosleep(&sleep_time, NULL);
            continue;
        } else if (result != SQLITE_QUEUE_ERROR_NONE) {
            error_count++;
            LOG_WARN("SQLite Queue: Message receive failed (error #%d): %s",
                     error_count, sqlite_queue_last_error(ctx));

            if (error_count > 10) {
                LOG_ERROR("SQLite Queue: Too many consecutive errors (%d), stopping daemon", error_count);
                printf("SQLite Queue: Too many consecutive errors (%d), stopping daemon\n", error_count);
                break;
            }

            struct timespec sleep_time = {0, 100000000}; // 100ms
            nanosleep(&sleep_time, NULL);
            continue;
        }

        // Process received messages
        for (int i = 0; i < msg_count; i++) {
            char *message = messages[i];
            long long msg_id = message_ids[i];

            // Parse JSON to extract content
            cJSON *json = cJSON_Parse(message);
            if (!json) {
                LOG_ERROR("SQLite Queue: Failed to parse JSON message ID %lld", msg_id);
                sqlite_queue_acknowledge(ctx, msg_id); // Acknowledge to remove from queue
                free(message);
                continue;
            }

            cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
            cJSON *content = cJSON_GetObjectItem(json, "content");

            if (message_type && cJSON_IsString(message_type)) {
                const char *msg_type = message_type->valuestring;

                if (strcmp(msg_type, "TEXT") == 0 &&
                    content && cJSON_IsString(content)) {

                    // Check if this message is already in the pending queue (prevent duplicates)
                    pthread_mutex_lock(&ctx->queue_mutex);
                    int is_duplicate = 0;
                    PendingMessage *check = ctx->pending_messages;
                    while (check) {
                        if (check->msg_id == msg_id) {
                            is_duplicate = 1;
                            break;
                        }
                        check = check->next;
                    }
                    pthread_mutex_unlock(&ctx->queue_mutex);

                    if (is_duplicate) {
                        LOG_WARN("SQLite Queue: Message ID %lld already in queue, skipping duplicate", msg_id);
                        cJSON_Delete(json);
                        free(message);
                        continue;
                    }

                    // Enqueue the message for the worker thread
                    if (sqlite_queue_enqueue_message(ctx, msg_id, content->valuestring) == 0) {
                        message_count++;
                        error_count = 0;
                        LOG_INFO("SQLite Queue: Enqueued message ID %lld for processing (queue depth: %d)",
                                 msg_id, sqlite_queue_pending_count(ctx));
                        // Only log at debug level to reduce console noise
                        LOG_DEBUG("SQLite Queue: Received message ID %lld (queue depth: %d)",
                                  msg_id, sqlite_queue_pending_count(ctx));

                        // Acknowledge the message immediately to prevent race condition
                        // where the main loop finds it again before worker processes it
                        if (sqlite_queue_acknowledge(ctx, msg_id) != SQLITE_QUEUE_ERROR_NONE) {
                            LOG_WARN("SQLite Queue: Failed to acknowledge message ID %lld after enqueue", msg_id);
                        }
                    } else {
                        LOG_ERROR("SQLite Queue: Failed to enqueue message ID %lld", msg_id);
                        sqlite_queue_acknowledge(ctx, msg_id); // Acknowledge to prevent reprocessing
                        error_count++;
                    }

                } else if (strcmp(msg_type, "TRIGGER_COMPACT") == 0) {
                    // Handle manual compaction trigger from client (direct processing in daemon mode)
                    LOG_INFO("SQLite Queue: Received TRIGGER_COMPACT message ID %lld in daemon mode", msg_id);
                    printf("SQLite Queue: Processing compaction trigger request\n");
                    fflush(stdout);

                    // Acknowledge the message first
                    sqlite_queue_acknowledge(ctx, msg_id);

                    // Perform compaction directly (no need to enqueue - compaction is quick)
                    int compact_result = sqlite_queue_handle_compact_trigger(ctx, state);
                    if (compact_result == 0) {
                        LOG_INFO("SQLite Queue: Daemon mode compaction trigger processed successfully");
                    } else {
                        LOG_ERROR("SQLite Queue: Daemon mode compaction trigger failed");
                    }

                } else if (strcmp(msg_type, "INTERRUPT") == 0) {
                    // Handle interrupt request from client (direct processing in daemon mode)
                    LOG_INFO("SQLite Queue: Received INTERRUPT message ID %lld in daemon mode", msg_id);
                    printf("SQLite Queue: Interrupt requested by client\n");
                    fflush(stdout);

                    // Acknowledge the message first
                    sqlite_queue_acknowledge(ctx, msg_id);

                    // Process the interrupt
                    int interrupt_result = sqlite_queue_interrupt(ctx);
                    if (interrupt_result == 0) {
                        LOG_INFO("SQLite Queue: Daemon mode interrupt processed successfully");
                    } else {
                        LOG_ERROR("SQLite Queue: Daemon mode interrupt failed");
                    }

                } else {
                    // Unknown message type - acknowledge and skip
                    LOG_WARN("SQLite Queue: Ignoring unknown message type '%s' ID %lld", msg_type, msg_id);
                    sqlite_queue_acknowledge(ctx, msg_id);
                }
            } else {
                // Invalid message format - acknowledge and skip
                LOG_WARN("SQLite Queue: Ignoring invalid message format ID %lld", msg_id);
                sqlite_queue_acknowledge(ctx, msg_id);
            }

            cJSON_Delete(json);
            free(message);
        }

        free(message_ids);
        free(messages);

        fflush(stdout);
    }

    // Signal worker thread to shutdown (mutex protects shutdown flag)
    pthread_mutex_lock(&ctx->queue_mutex);
    ctx->shutdown = 1;
    pthread_cond_broadcast(&ctx->queue_cond);
    pthread_mutex_unlock(&ctx->queue_mutex);

    // Wait for worker thread to finish (with timeout)
#ifdef USE_PTHREAD_JOIN_NO_TIMEOUT
    // macOS doesn't have pthread_timedjoin_np, use regular pthread_join
    pthread_join(ctx->worker_thread, NULL);
#else
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;  // 5 second timeout
    pthread_timedjoin_np(ctx->worker_thread, NULL, &timeout);
#endif

    LOG_INFO("SQLite Queue: =========================================");
    LOG_INFO("SQLite Queue: SQLite queue daemon mode stopping");
    LOG_INFO("SQLite Queue: Total messages processed: %d", message_count);
    LOG_INFO("SQLite Queue: Total errors: %d", error_count);
    LOG_INFO("SQLite Queue: =========================================");

    // Print shutdown message with status formatting
    // Variables status_color and status_color_start already declared at function start
    if (get_colorscheme_color(COLORSCHEME_STATUS, status_color, sizeof(status_color)) == 0) {
        status_color_start = status_color;
    } else {
        status_color_start = ANSI_FALLBACK_STATUS;
    }

    printf("\n%s[SQLite Queue]%s Daemon stopped\n", status_color_start, ANSI_RESET);
    printf("Total messages processed: %d\n", message_count);
    if (error_count > 0) {
        printf("Total errors: %d\n", error_count);
    }
    fflush(stdout);

    return 0;
}
#endif // TEST_BUILD

/**
 * Send an AUTH_REQUIRED message to the queue client.
 *
 * Called by the OAuth message callback during device authorization so that
 * sqlite-queue clients receive the login URL and user code rather than
 * having them silently printed to a console that nobody is watching.
 *
 * The client should treat AUTH_REQUIRED messages as high-priority user-facing
 * prompts and display them before waiting for the next TEXT response.
 */
int sqlite_queue_send_auth_message(SQLiteQueueContext *ctx, const char *receiver,
                                   const char *message, int is_error) {
    if (!ctx || !receiver || !message) {
        LOG_ERROR("SQLite Queue: Invalid parameters for send_auth_message");
        return -1;
    }

    /* Echo to console so server-side logs always show the auth flow */
    if (is_error) {
        fprintf(stderr, "[OpenAI Auth] %s\n", message);
    } else {
        printf("[OpenAI Auth] %s\n", message);
    }
    fflush(stdout);

    cJSON *msg_json = cJSON_CreateObject();
    if (!msg_json) {
        LOG_ERROR("SQLite Queue: Failed to create auth message JSON object");
        return -1;
    }

    cJSON_AddStringToObject(msg_json, "messageType", "AUTH_REQUIRED");
    cJSON_AddStringToObject(msg_json, "content", message);
    cJSON_AddBoolToObject(msg_json, "isError", is_error ? cJSON_True : cJSON_False);

    char *msg_str = cJSON_PrintUnformatted(msg_json);
    cJSON_Delete(msg_json);

    if (!msg_str) {
        LOG_ERROR("SQLite Queue: Failed to serialize auth message JSON");
        return -1;
    }

    LOG_INFO("SQLite Queue: Sending AUTH_REQUIRED message (is_error=%d): %s",
             is_error, message);

    int result = sqlite_queue_send(ctx, receiver, msg_str, strlen(msg_str));
    free(msg_str);

    return result;
}
bool sqlite_queue_available(void) {
    // SQLite is always available (it's a required dependency)
    return true;
}

int sqlite_queue_get_status(SQLiteQueueContext *ctx, char *buffer, size_t buffer_size) {
    if (!ctx || !buffer || buffer_size == 0) {
        return -1;
    }

    // Format status string
    snprintf(buffer, buffer_size,
             "SQLite Queue Status:\n"
             "  Database Path: %s\n"
             "  Sender Name: %s\n"
             "  Daemon Mode: %s\n"
             "  Enabled: %s\n"
             "  Retry Count: %d/%d",
             ctx->db_path ? ctx->db_path : "unknown",
             ctx->sender_name ? ctx->sender_name : "unknown",
             ctx->daemon_mode ? "enabled" : "disabled",
             ctx->enabled ? "enabled" : "disabled",
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

    // Use macOS-specific open flags if needed
    int open_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
#ifdef __APPLE__
    open_flags = macos_sqlite_open_flags();
#endif

    int rc = sqlite3_open_v2(ctx->db_path, &db, open_flags, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("SQLite Queue: Failed to open database %s: %s", ctx->db_path, sqlite3_errmsg(db));
        sqlite_queue_set_error(ctx, SQLITE_QUEUE_ERROR_DB_OPEN_FAILED,
                              "Failed to open database: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return SQLITE_QUEUE_ERROR_DB_OPEN_FAILED;
    }

    // Apply macOS-specific fixes if needed
#ifdef __APPLE__
    if (macos_sqlite_needs_fixes()) {
        if (macos_sqlite_apply_fixes(db) != 0) {
            LOG_WARN("SQLite Queue: Failed to apply macOS SQLite fixes");
        }
        // Use macOS-specific busy timeout
        sqlite3_busy_timeout(db, macos_sqlite_busy_timeout_ms());
    } else {
        sqlite3_busy_timeout(db, 5000);
    }
#else
    sqlite3_busy_timeout(db, 5000);
#endif

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

    // Busy timeout is set above (with macOS-specific handling)
    LOG_DEBUG("SQLite Queue: Busy timeout configured");

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

/**
 * Send a streaming text chunk to the queue client.
 *
 * Used during streaming API responses to provide real-time partial text
 * to clients. Each chunk is written as a separate message that the client
 * can poll and display immediately.
 *
 * Message format sent to receiver:
 *   { "messageType": "TEXT_STREAM_CHUNK",
 *     "content": "<partial text chunk>",
 *     "isComplete": false }
 *
 * @param ctx       SQLite queue context
 * @param receiver  Receiver name (usually "client")
 * @param chunk     Partial text chunk (can be empty string for heartbeat)
 * @return 0 on success, -1 on failure
 */
int sqlite_queue_send_streaming_chunk(SQLiteQueueContext *ctx, const char *receiver,
                                      const char *chunk) {
    if (!ctx || !receiver) {
        LOG_ERROR("SQLite Queue: Invalid parameters for send_streaming_chunk");
        return -1;
    }

    if (!chunk) {
        chunk = "";
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        LOG_ERROR("SQLite Queue: Failed to create streaming chunk JSON object");
        return -1;
    }

    cJSON_AddStringToObject(json, "messageType", "TEXT_STREAM_CHUNK");
    cJSON_AddStringToObject(json, "content", chunk);
    cJSON_AddBoolToObject(json, "isComplete", cJSON_False);

    char *json_str = cJSON_PrintUnformatted(json);
    if (!json_str) {
        LOG_ERROR("SQLite Queue: Failed to serialize streaming chunk JSON");
        cJSON_Delete(json);
        return -1;
    }

    LOG_DEBUG("SQLite Queue: Sending streaming chunk (%zu bytes): %.50s...",
              strlen(chunk), chunk);

    int result = sqlite_queue_send(ctx, receiver, json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(json);

    return result;
}

/**
 * Streaming text callback function.
 *
 * This callback signature matches what providers expect for streaming updates.
 * It can be assigned to ConversationState.streaming_text_callback.
 *
 * Usage:
 *   state->streaming_text_callback = sqlite_queue_streaming_callback;
 *   state->streaming_callback_userdata = ctx;
 *
 * @param chunk    Partial text chunk received from the API
 * @param userdata User data (expected to be SQLiteQueueContext*)
 */
void sqlite_queue_streaming_callback(const char *chunk, void *userdata) {
    if (!userdata) {
        return;
    }

    SQLiteQueueContext *ctx = (SQLiteQueueContext *)userdata;

    // Send the chunk as a TEXT_STREAM_CHUNK message
    sqlite_queue_send_streaming_chunk(ctx, "client", chunk);
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
