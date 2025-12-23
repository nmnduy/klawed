/*
 * zmq_socket.h - ZeroMQ socket interface for Klawed
 *
 * Provides IPC communication via ZeroMQ sockets for external integration.
 * Uses PAIR socket pattern for peer-to-peer communication.
 */

#ifndef ZMQ_SOCKET_H
#define ZMQ_SOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// Error codes
typedef enum {
    ZMQ_ERROR_NONE = 0,
    ZMQ_ERROR_INVALID_PARAM = -1,
    ZMQ_ERROR_NO_SOCKET = -2,
    ZMQ_ERROR_CONNECTION_FAILED = -3,
    ZMQ_ERROR_SEND_FAILED = -4,
    ZMQ_ERROR_RECEIVE_FAILED = -5,
    ZMQ_ERROR_TIMEOUT = -6,
    ZMQ_ERROR_QUEUE_FULL = -7,
    ZMQ_ERROR_NOT_CONNECTED = -8,
    ZMQ_ERROR_RECONNECT_FAILED = -9,
    ZMQ_ERROR_NOT_SUPPORTED = -10
} ZMQErrorCode;

// Forward declarations
struct ConversationState;
struct TUIState;

// Forward declaration for reliable queue
typedef struct ZMQMessageQueue ZMQMessageQueue;

// ZMQ socket context
typedef struct ZMQContext {
    void *context;      // ZMQ context
    void *socket;       // ZMQ socket
    char *endpoint;     // Socket endpoint (e.g., "tcp://127.0.0.1:5555")
    int socket_type;    // ZMQ socket type (ZMQ_PAIR only)
    bool enabled;       // Whether ZMQ mode is enabled
    bool daemon_mode;   // Whether running in daemon mode (listen for requests)

    // Timeout configuration (in milliseconds)
    int receive_timeout;
    int send_timeout;
    int connect_timeout;
    int heartbeat_interval;
    int reconnect_interval;
    int max_reconnect_attempts;

    // Queue sizes
    int send_queue_size;
    int receive_queue_size;

    // State tracking
    int reconnect_attempts;
    time_t last_heartbeat;
    time_t last_activity;
    bool heartbeat_enabled;
    bool reconnect_enabled;

    // Reliable message queues (reserved for future use)
    ZMQMessageQueue *send_queue;    // Queue for outgoing messages
    ZMQMessageQueue *receive_queue; // Queue for incoming messages (future use)

    // Error tracking
    ZMQErrorCode last_error;
    char error_message[256];
    time_t error_time;
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
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = blocking)
 * @return Length of received message on success, ZMQErrorCode on failure
 */
int zmq_socket_receive(ZMQContext *ctx, char *buffer, size_t buffer_size, int timeout_ms);

/**
 * Get last error message
 * @param ctx ZMQ context
 * @return Error message string (do not free)
 */
const char* zmq_socket_last_error(ZMQContext *ctx);

/**
 * Clear error state
 * @param ctx ZMQ context
 */
void zmq_socket_clear_error(ZMQContext *ctx);

/**
 * Process incoming ZMQ message and execute appropriate action
 * @param ctx ZMQ context
 * @param state Conversation state
 * @param tui TUI state (can be NULL in daemon mode)
 * @return 0 on success, -1 on failure
 */
int zmq_socket_process_message(ZMQContext *ctx, struct ConversationState *state, struct TUIState *tui);

/**
 * Run ZMQ daemon mode - listen for requests and process them
 * @param ctx ZMQ context (should be ZMQ_PAIR socket)
 * @param state Conversation state
 * @return 0 on success, -1 on failure
 */
int zmq_socket_daemon_mode(ZMQContext *ctx, struct ConversationState *state);

/**
 * Check if ZMQ is available at compile time
 * @return true if ZMQ support is compiled in, false otherwise
 */
bool zmq_socket_available(void);

/**
 * Get connection status information
 * @param ctx ZMQ context
 * @param buffer Buffer to store status string
 * @param buffer_size Size of buffer
 * @return 0 on success, -1 on failure
 */
int zmq_socket_get_status(ZMQContext *ctx, char *buffer, size_t buffer_size);

/**
 * Get queue statistics
 * @param ctx ZMQ context
 * @param send_queue_count Output for send queue message count (can be NULL)
 * @param send_queue_bytes Output for send queue byte count (can be NULL)
 * @param recv_queue_count Output for receive queue message count (can be NULL)
 * @param recv_queue_bytes Output for receive queue byte count (can be NULL)
 * @return 0 on success, -1 on failure
 */
int zmq_socket_get_queue_stats(ZMQContext *ctx,
                               size_t *send_queue_count, size_t *send_queue_bytes,
                               size_t *recv_queue_count, size_t *recv_queue_bytes);

#endif // ZMQ_SOCKET_H
