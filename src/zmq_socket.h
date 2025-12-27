/*
 * zmq_socket.h - Simple ZeroMQ socket implementation for Klawed
 *
 * Provides IPC communication via ZeroMQ sockets for external integration.
 * Uses PAIR socket pattern for peer-to-peer communication.
 *
 * Features:
 * - ZMQ_PAIR socket with bind/connect
 * - Message ID/ACK system for reliability
 * - Time-sharing loop (check user input, check incoming messages)
 * - LINGER option for clean shutdown
 * - TCP keepalive enabled
 * - Basic error handling (exit on fatal errors)
 */

#ifndef ZMQ_SOCKET_H
#define ZMQ_SOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Error codes
typedef enum {
    ZMQ_ERROR_NONE = 0,
    ZMQ_ERROR_INVALID_PARAM = -1,
    ZMQ_ERROR_SEND_FAILED = -2,
    ZMQ_ERROR_RECEIVE_FAILED = -3,
    ZMQ_ERROR_NOT_SUPPORTED = -4,
    ZMQ_ERROR_NOMEM = -5
} ZMQErrorCode;

// Forward declarations
struct ConversationState;
struct TUIState;

// Pending message waiting for ACK
typedef struct ZMQPendingMessage {
    char *message_id;           // Message ID (hex string)
    char *message_json;         // Full JSON message string
    int64_t sent_time_ms;       // Timestamp when message was sent (milliseconds)
    int retry_count;            // Number of retries attempted
    struct ZMQPendingMessage *next;
} ZMQPendingMessage;

// Pending message queue
typedef struct ZMQPendingQueue {
    ZMQPendingMessage *head;
    ZMQPendingMessage *tail;
    int count;
    int max_pending;             // Maximum number of pending messages
    int64_t timeout_ms;         // Time before resend (milliseconds)
    int max_retries;            // Maximum number of retries before giving up
} ZMQPendingQueue;

// Simple ZMQ socket context
typedef struct ZMQContext {
    void *context;      // ZMQ context
    void *socket;       // ZMQ socket
    char *endpoint;     // Socket endpoint (e.g., "tcp://127.0.0.1:5555")
    int socket_type;    // ZMQ socket type (ZMQ_PAIR only)
    bool enabled;       // Whether ZMQ mode is enabled
    
    // Message ID/ACK system
    ZMQPendingQueue pending_queue;   // Queue of messages waiting for ACK
    uint32_t salt;                   // Random salt for message ID generation
    int message_sequence;            // Simple counter for debugging
} ZMQContext;

/**
 * Initialize ZMQ context and socket
 * @param endpoint Socket endpoint (e.g., "tcp://127.0.0.1:5555" or "ipc:///tmp/klawed.sock")
 * @param socket_type ZMQ socket type (ZMQ_PAIR for peer-to-peer communication)
 * @return Initialized ZMQContext or NULL on failure
 */
ZMQContext* zmq_socket_init(const char *endpoint, int socket_type);

/**
 * Clean up ZMQ resources
 * @param ctx ZMQ context to clean up
 */
void zmq_socket_cleanup(ZMQContext *ctx);

/**
 * Send message via ZMQ socket
 * @param ctx ZMQ context
 * @param message Message to send
 * @param message_len Length of message
 * @return 0 on success, ZMQErrorCode on failure
 */
int zmq_socket_send(ZMQContext *ctx, const char *message, size_t message_len);

/**
 * Receive message from ZMQ socket with timeout
 * @param ctx ZMQ context
 * @param buffer Buffer to store received message
 * @param buffer_size Size of buffer
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = blocking forever)
 * @return Length of received message on success, ZMQErrorCode on failure
 */
int zmq_socket_receive(ZMQContext *ctx, char *buffer, size_t buffer_size, int timeout_ms);

/**
 * Check if ZMQ is available at compile time
 * @return true if ZMQ support is compiled in, false otherwise
 */
bool zmq_socket_available(void);

/**
 * Process incoming ZMQ message and execute appropriate action
 * @param ctx ZMQ context
 * @param state Conversation state
 * @param tui TUI state (can be NULL in daemon mode)
 * @return 0 on success, -1 on failure
 */
int zmq_socket_process_message(ZMQContext *ctx, struct ConversationState *state, struct TUIState *tui);

/**
 * Run ZMQ daemon mode - listen for requests and process them with time-sharing
 * @param ctx ZMQ context (should be ZMQ_PAIR socket)
 * @param state Conversation state
 * @return 0 on success, -1 on failure
 */
int zmq_socket_daemon_mode(ZMQContext *ctx, struct ConversationState *state);

/**
 * Generate a unique message ID based on timestamp, partial content, and random salt
 * @param ctx ZMQ context (contains salt)
 * @param message Message content (partial content used for hash)
 * @param message_len Length of message content
 * @param out_id Buffer to store the message ID (hex string)
 * @param out_id_size Size of output buffer (minimum 33 bytes for 128-bit ID)
 * @return 0 on success, ZMQErrorCode on failure
 */
int zmq_generate_message_id(ZMQContext *ctx, const char *message, size_t message_len,
                           char *out_id, size_t out_id_size);

/**
 * Send message with ID and track for ACK
 * @param ctx ZMQ context
 * @param message Message to send (JSON string)
 * @param message_len Length of message
 * @param message_id_out Optional: buffer to receive the generated message ID
 * @param message_id_out_size Size of message_id_out buffer (minimum 33 bytes)
 * @return 0 on success, ZMQErrorCode on failure
 */
int zmq_socket_send_with_id(ZMQContext *ctx, const char *message, size_t message_len,
                           char *message_id_out, size_t message_id_out_size);

/**
 * Send ACK for a received message
 * @param ctx ZMQ context
 * @param message_id ID of the message being acknowledged
 * @return 0 on success, ZMQErrorCode on failure
 */
int zmq_send_ack(ZMQContext *ctx, const char *message_id);

/**
 * Process ACK message and remove from pending queue
 * @param ctx ZMQ context
 * @param message_id ID of the message being acknowledged
 * @return 0 on success (message removed from queue), -1 if message not found
 */
int zmq_process_ack(ZMQContext *ctx, const char *message_id);

/**
 * Check and resend pending messages that have timed out
 * @param ctx ZMQ context
 * @param current_time_ms Current time in milliseconds
 * @return Number of messages resent, -1 on error
 */
int zmq_check_and_resend_pending(ZMQContext *ctx, int64_t current_time_ms);

/**
 * Check for user input with timeout using select()
 * @param buffer Buffer to store user input
 * @param buffer_size Size of buffer
 * @param timeout_ms Timeout in milliseconds
 * @return 1 if input available, 0 if timeout, -1 on error
 */
int zmq_check_user_input(char *buffer, size_t buffer_size, int timeout_ms);

/**
 * Clean up pending message queue
 * @param ctx ZMQ context
 */
void zmq_cleanup_pending_queue(ZMQContext *ctx);

#endif // ZMQ_SOCKET_H
