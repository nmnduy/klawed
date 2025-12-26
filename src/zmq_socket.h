/*
 * zmq_socket.h - Simple ZeroMQ socket implementation for Klawed
 *
 * Provides IPC communication via ZeroMQ sockets for external integration.
 * Uses PAIR socket pattern for peer-to-peer communication.
 *
 * Simplified implementation:
 * - ZMQ_PAIR socket with bind/connect
 * - Blocking receive (zmq_recv with infinite timeout)
 * - LINGER option for clean shutdown
 * - TCP keepalive enabled
 * - Basic error handling (exit on fatal errors)
 */

#ifndef ZMQ_SOCKET_H
#define ZMQ_SOCKET_H

#include <stdbool.h>
#include <stddef.h>

// Error codes
typedef enum {
    ZMQ_ERROR_NONE = 0,
    ZMQ_ERROR_INVALID_PARAM = -1,
    ZMQ_ERROR_SEND_FAILED = -2,
    ZMQ_ERROR_RECEIVE_FAILED = -3,
    ZMQ_ERROR_NOT_SUPPORTED = -4
} ZMQErrorCode;

// Forward declarations
struct ConversationState;
struct TUIState;

// Simple ZMQ socket context
typedef struct ZMQContext {
    void *context;      // ZMQ context
    void *socket;       // ZMQ socket
    char *endpoint;     // Socket endpoint (e.g., "tcp://127.0.0.1:5555")
    int socket_type;    // ZMQ socket type (ZMQ_PAIR only)
    bool enabled;       // Whether ZMQ mode is enabled
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
 * Run ZMQ daemon mode - listen for requests and process them
 * @param ctx ZMQ context (should be ZMQ_PAIR socket)
 * @param state Conversation state
 * @return 0 on success, -1 on failure
 */
int zmq_socket_daemon_mode(ZMQContext *ctx, struct ConversationState *state);

#endif // ZMQ_SOCKET_H
