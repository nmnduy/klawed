/*
 * sqlite_queue.h - SQLite message queue interface for Klawed
 *
 * Provides IPC communication via SQLite database for external integration.
 * Uses a simple message table with sender/receiver fields and sent flag.
 */

#ifndef SQLITE_QUEUE_H
#define SQLITE_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <pthread.h>

// Error codes
typedef enum {
    SQLITE_QUEUE_ERROR_NONE = 0,
    SQLITE_QUEUE_ERROR_INVALID_PARAM = -1,
    SQLITE_QUEUE_ERROR_DB_OPEN_FAILED = -2,
    SQLITE_QUEUE_ERROR_DB_PREPARE_FAILED = -3,
    SQLITE_QUEUE_ERROR_DB_EXECUTE_FAILED = -4,
    SQLITE_QUEUE_ERROR_DB_BUSY = -5,
    SQLITE_QUEUE_ERROR_NO_MESSAGES = -6,
    SQLITE_QUEUE_ERROR_MESSAGE_TOO_LONG = -7,
    SQLITE_QUEUE_ERROR_INVALID_MESSAGE = -8,
    SQLITE_QUEUE_ERROR_TIMEOUT = -9,
    SQLITE_QUEUE_ERROR_NOT_INITIALIZED = -10
} SQLiteQueueErrorCode;

// Forward declarations
struct ConversationState;
struct TUIState;

// Pending message entry for queued user input during tool execution
typedef struct PendingMessage {
    long long msg_id;        // Message ID from database
    char *content;           // Message content (TEXT type only)
    struct PendingMessage *next;
} PendingMessage;

// SQLite queue context
typedef struct SQLiteQueueContext {
    char *db_path;           // Path to SQLite database file
    char *sender_name;       // Name of this sender (default: "klawed")
    bool enabled;            // Whether SQLite queue mode is enabled
    bool daemon_mode;        // Whether running in daemon mode (poll for messages)

    // Polling configuration (in milliseconds)
    int poll_interval;
    int max_retries;

    // Message configuration
    size_t max_message_size;
    int max_queue_size;
    int max_iterations;      // Maximum iterations in interactive loop (0 = unlimited)

    // State tracking
    int retry_count;
    bool initialized;

    // Error tracking
    SQLiteQueueErrorCode last_error;
    char error_message[256];
    time_t error_time;

    // Internal database handle (opened on demand)
    void *db_handle;  // sqlite3* but we don't want to expose sqlite3.h in header

    // Threading support for async message processing
    pthread_t worker_thread;        // Worker thread for processing messages
    pthread_mutex_t queue_mutex;    // Protects pending_messages queue
    pthread_cond_t queue_cond;      // Signals when new messages are queued
    PendingMessage *pending_messages; // Linked list of pending messages
    PendingMessage *pending_tail;   // Tail of pending messages list
    int pending_count;              // Number of pending messages
    volatile int processing;        // 1 if worker is processing a message
    volatile int shutdown;          // 1 to signal worker thread to exit
    struct ConversationState *state; // Conversation state (for worker thread)
} SQLiteQueueContext;

/**
 * Initialize SQLite queue context
 * @param db_path Path to SQLite database file
 * @param sender_name Name of this sender (default: "klawed")
 * @return Initialized SQLiteQueueContext or NULL on failure
 */
SQLiteQueueContext* sqlite_queue_init(const char *db_path, const char *sender_name);

/**
 * Clean up SQLite queue resources
 * @param ctx SQLite queue context to clean up
 */
void sqlite_queue_cleanup(SQLiteQueueContext *ctx);

/**
 * Send message via SQLite queue
 * @param ctx SQLite queue context
 * @param receiver Receiver name
 * @param message Message to send
 * @param message_len Length of message
 * @return 0 on success, SQLiteQueueErrorCode on failure
 */
int sqlite_queue_send(SQLiteQueueContext *ctx, const char *receiver, const char *message, size_t message_len);

/**
 * Receive messages from SQLite queue with timeout
 * @param ctx SQLite queue context
 * @param sender_filter Optional sender name filter (NULL for any sender)
 * @param max_messages Maximum number of messages to retrieve (0 for default)
 * @param messages Array to store received messages (caller must free strings)
 * @param message_count Output parameter for number of messages received
 * @param message_ids Output parameter for message IDs (caller must free)
 * @return 0 on success, SQLiteQueueErrorCode on failure
 */
int sqlite_queue_receive(SQLiteQueueContext *ctx, const char *sender_filter, int max_messages,
                         char ***messages, int *message_count, long long **message_ids);

/**
 * Acknowledge message as sent/read
 * @param ctx SQLite queue context
 * @param message_id Message ID to acknowledge
 * @return 0 on success, SQLiteQueueErrorCode on failure
 */
int sqlite_queue_acknowledge(SQLiteQueueContext *ctx, long long message_id);

/**
 * Get last error message
 * @param ctx SQLite queue context
 * @return Error message string (do not free)
 */
const char* sqlite_queue_last_error(SQLiteQueueContext *ctx);

/**
 * Clear error state
 * @param ctx SQLite queue context
 */
void sqlite_queue_clear_error(SQLiteQueueContext *ctx);

/**
 * Process incoming SQLite message and execute appropriate action
 * @param ctx SQLite queue context
 * @param state Conversation state
 * @param tui TUI state (can be NULL in daemon mode)
 * @return 0 on success, -1 on failure
 */
int sqlite_queue_process_message(SQLiteQueueContext *ctx, struct ConversationState *state, struct TUIState *tui);

/**
 * Run SQLite queue daemon mode - poll for messages and process them
 * @param ctx SQLite queue context
 * @param state Conversation state
 * @return 0 on success, -1 on failure
 */
int sqlite_queue_daemon_mode(SQLiteQueueContext *ctx, struct ConversationState *state);

/**
 * Restore conversation history from an existing SQLite queue database.
 * Called automatically at daemon startup to resume after a crash or restart.
 * Loads up to 800 most recent processed messages (sent=1) and reconstructs
 * the conversation: user TEXT turns, assistant TEXT+TOOL turns, tool results.
 * Interrupted tool calls (no matching TOOL_RESULT) get synthetic error results.
 *
 * @param ctx SQLite queue context
 * @param state Conversation state to populate
 * @return Number of message blocks restored, or -1 on error
 */
int sqlite_queue_restore_conversation(SQLiteQueueContext *ctx, struct ConversationState *state);

/**
 * Check if SQLite queue is available (always true since SQLite is required)
 * @return true if SQLite queue support is available, false otherwise
 */
bool sqlite_queue_available(void);

/**
 * Get queue status information
 * @param ctx SQLite queue context
 * @param buffer Buffer to store status string
 * @param buffer_size Size of buffer
 * @return 0 on success, -1 on failure
 */
int sqlite_queue_get_status(SQLiteQueueContext *ctx, char *buffer, size_t buffer_size);

/**
 * Get queue statistics
 * @param ctx SQLite queue context
 * @param pending_count Output for pending message count (can be NULL)
 * @param total_count Output for total message count (can be NULL)
 * @param unread_count Output for unread message count for this sender (can be NULL)
 * @return 0 on success, -1 on failure
 */
int sqlite_queue_get_stats(SQLiteQueueContext *ctx,
                          int *pending_count, int *total_count, int *unread_count);

/**
 * Initialize database schema (creates messages table if needed)
 * @param ctx SQLite queue context
 * @return 0 on success, SQLiteQueueErrorCode on failure
 */
int sqlite_queue_init_schema(SQLiteQueueContext *ctx);

/**
 * Send auto-compaction notice to queue reader
 * @param ctx SQLite queue context
 * @param receiver Receiver name
 * @param messages_compacted Number of messages compacted
 * @param tokens_before Token count before compaction
 * @param tokens_after Token count after compaction
 * @param usage_before_pct Context usage percentage before
 * @param usage_after_pct Context usage percentage after
 * @param summary AI-generated summary of compacted context (can be NULL)
 * @return 0 on success, -1 on failure
 */
int sqlite_queue_send_compaction_notice(SQLiteQueueContext *ctx, const char *receiver,
                                       int messages_compacted, size_t tokens_before,
                                        size_t tokens_after, double usage_before_pct,
                                        double usage_after_pct, const char *summary);

/**
 * Check if there are pending user messages in the queue.
 * This allows detecting if user input was received during tool execution.
 *
 * @param ctx SQLite queue context
 * @return Number of pending messages, 0 if none
 */
int sqlite_queue_pending_count(SQLiteQueueContext *ctx);

/**
 * Interrupt the current message processing.
 * Sets the interrupt_requested flag in conversation state.
 *
 * @param ctx SQLite queue context
 * @return 0 on success, -1 on failure
 */
int sqlite_queue_interrupt(SQLiteQueueContext *ctx);

#endif // SQLITE_QUEUE_H
