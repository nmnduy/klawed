/*
 * zmq_client.h - Thread-based ZMQ client for Klawed
 *
 * Implements a ZMQ client with separate receiver thread and main interactive thread.
 * Uses thread-safe message queues for communication between threads.
 * Provides reliable message delivery with ID/ACK system.
 *
 * Usage: ./klawed --zmq-client tcp://127.0.0.1:5555
 */

#ifndef ZMQ_CLIENT_H
#define ZMQ_CLIENT_H

#include "zmq_message_queue.h"
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stddef.h>

#ifdef HAVE_ZMQ
#include <zmq.h>

// Message ID constants (must match zmq_socket.h)
#define ZMQ_CLIENT_MESSAGE_ID_HEX_LENGTH 33  // 128 bits = 32 hex chars + null terminator

// Pending message waiting for ACK (client side)
typedef struct ZMQClientPendingMessage {
    char *message_id;           // Message ID (hex string)
    char *message_json;         // Full JSON message string
    int64_t sent_time_ms;       // Timestamp when message was sent (milliseconds)
    int retry_count;            // Number of retries attempted
    struct ZMQClientPendingMessage *next;
} ZMQClientPendingMessage;

// Pending message queue (client side)
typedef struct ZMQClientPendingQueue {
    ZMQClientPendingMessage *head;
    ZMQClientPendingMessage *tail;
    int count;
    int max_pending;             // Maximum number of pending messages
    int64_t timeout_ms;         // Time before resend (milliseconds)
    int max_retries;            // Maximum number of retries before giving up
} ZMQClientPendingQueue;

// Seen message tracking for duplicate detection
typedef struct {
    char *message_id;           // Message ID (hex string)
    int64_t timestamp_ms;       // When this message was seen
} ZMQClientSeenMessage;

/**
 * ZMQ client context (threaded version)
 */
typedef struct ZMQClientContextThreaded {
    // Thread management
    pthread_t receiver_thread;
    bool running;
    bool thread_started;

    // ZMQ components
    void *zmq_context;
    void *zmq_socket;
    char *endpoint;
    int socket_type;  // ZMQ_PAIR for client

    // Message queues (thread-safe)
    ZMQMessageQueue *incoming_queue;   // Daemon → Client (responses)

    // Message tracking (for reliable delivery)
    ZMQClientPendingQueue pending_queue;
    ZMQClientSeenMessage seen_messages[1000];  // Circular buffer of recently seen message IDs
    int seen_message_count;
    uint32_t salt;                         // Random salt for message ID generation
    int message_sequence;                  // Simple counter for debugging

    // User input buffer
    char input_buffer[4096];
    size_t input_pos;

    // Statistics
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t errors;
} ZMQClientContextThreaded;

/**
 * Initialize ZMQ client context (threaded version)
 * @param endpoint ZMQ endpoint (e.g., "tcp://127.0.0.1:5555")
 * @return Initialized client context or NULL on error
 */
ZMQClientContextThreaded* zmq_client_init(const char *endpoint);

/**
 * Start ZMQ client (starts receiver thread and enters main interactive loop)
 * @param ctx Client context
 * @return 0 on success, -1 on error
 */
int zmq_client_start(ZMQClientContextThreaded *ctx);

/**
 * Stop ZMQ client (stops receiver thread and cleans up)
 * @param ctx Client context
 */
void zmq_client_stop(ZMQClientContextThreaded *ctx);

/**
 * Clean up ZMQ client resources
 * @param ctx Client context to clean up
 */
void zmq_client_cleanup(ZMQClientContextThreaded *ctx);

/**
 * Check if client is running
 * @param ctx Client context
 * @return true if running, false otherwise
 */
bool zmq_client_is_running(ZMQClientContextThreaded *ctx);

/**
 * Send a text message to the daemon
 * @param ctx Client context
 * @param text Text message to send
 * @return 0 on success, -1 on error
 */
int zmq_client_send_text(ZMQClientContextThreaded *ctx, const char *text);

/**
 * Get client statistics
 * @param ctx Client context
 * @param messages_sent Output: total messages sent
 * @param messages_received Output: total messages received
 * @param errors Output: total errors
 */
void zmq_client_get_stats(ZMQClientContextThreaded *ctx, uint64_t *messages_sent,
                                   uint64_t *messages_received, uint64_t *errors);

/**
 * Print usage information for ZMQ client mode
 * @param program_name Name of the program
 */
void zmq_client_print_usage(const char *program_name);

/**
 * Main ZMQ client mode entry point (replaces zmq_client_mode)
 * @param endpoint ZMQ endpoint to connect to
 * @return Exit code (0 for success, non-zero for error)
 */
int zmq_client_mode(const char *endpoint);

/**
 * Send a message with message ID for reliable delivery
 * @param ctx Client context
 * @param message JSON message to send
 * @param message_id_out Buffer to store message ID (optional)
 * @param message_id_out_size Size of message_id_out buffer
 * @return 0 on success, -1 on error
 */
int zmq_client_send_message_with_id(ZMQClientContextThreaded *ctx, const char *message,
                                           char *message_id_out, size_t message_id_out_size);

/**
 * Process an ACK message
 * @param ctx Client context
 * @param message_id Message ID to acknowledge
 * @return 0 on success, -1 on error
 */
int zmq_client_process_ack(ZMQClientContextThreaded *ctx, const char *message_id);

/**
 * Send an ACK message
 * @param ctx Client context
 * @param message_id Message ID to acknowledge
 * @return 0 on success, -1 on error
 */
int zmq_client_send_ack(ZMQClientContextThreaded *ctx, const char *message_id);

/**
 * Check and resend pending messages that have timed out
 * @param ctx Client context
 * @param current_time_ms Current time in milliseconds
 * @return Number of messages resent
 */
int zmq_client_check_and_resend_pending(ZMQClientContextThreaded *ctx, int64_t current_time_ms);

/**
 * Clean up pending message queue
 * @param ctx Client context
 */
void zmq_client_cleanup_pending_queue(ZMQClientContextThreaded *ctx);

/**
 * Generate a message ID for a message
 * @param ctx Client context
 * @param message Message content
 * @param message_len Length of message
 * @param out_id Buffer to store message ID
 * @param out_id_size Size of out_id buffer
 * @return 0 on success, -1 on error
 */
int zmq_client_generate_message_id(ZMQClientContextThreaded *ctx, const char *message,
                                          size_t message_len, char *out_id, size_t out_id_size);

/**
 * Process a received message
 * @param ctx Client context
 * @param response JSON response string
 */
void zmq_client_process_message(ZMQClientContextThreaded *ctx, const char *response);

/**
 * Check for user input (non-blocking)
 * @param buffer Buffer to store input
 * @param buffer_size Size of buffer
 * @param timeout_ms Timeout in milliseconds
 * @return 1 if input available, 0 if timeout, -1 on error
 */
int zmq_client_check_user_input(char *buffer, size_t buffer_size, int timeout_ms);

#else
// Forward declarations for stub compilation
typedef struct ZMQClientContextThreaded ZMQClientContextThreaded;
typedef struct ConversationState ConversationState;
typedef struct TUIState TUIState;

// Function declarations for stub compilation
ZMQClientContextThreaded* zmq_client_init(const char *endpoint);
int zmq_client_start(ZMQClientContextThreaded *ctx);
void zmq_client_stop(ZMQClientContextThreaded *ctx);
void zmq_client_cleanup(ZMQClientContextThreaded *ctx);
bool zmq_client_is_running(ZMQClientContextThreaded *ctx);
int zmq_client_send_text(ZMQClientContextThreaded *ctx, const char *text);
void zmq_client_get_stats(ZMQClientContextThreaded *ctx, uint64_t *messages_sent,
                          uint64_t *messages_received, uint64_t *errors);
void zmq_client_print_usage(const char *program_name);
int zmq_client_mode(const char *endpoint);
int zmq_client_send_message_with_id(ZMQClientContextThreaded *ctx, const char *message,
                                    size_t message_len, char *out_id, size_t out_id_size);
int zmq_client_process_ack(ZMQClientContextThreaded *ctx, const char *message_id);
int zmq_client_send_ack(ZMQClientContextThreaded *ctx, const char *message_id);
int zmq_client_check_and_resend_pending(ZMQClientContextThreaded *ctx, int64_t current_time_ms);
void zmq_client_cleanup_pending_queue(ZMQClientContextThreaded *ctx);
int zmq_client_generate_message_id(ZMQClientContextThreaded *ctx, const char *message,
                                   size_t message_len, char *out_id, size_t out_id_size);
void zmq_client_process_message(ZMQClientContextThreaded *ctx, const char *response);
int zmq_client_check_user_input(char *buffer, size_t buffer_size, int timeout_ms);
#endif // HAVE_ZMQ

#endif // ZMQ_CLIENT_H
