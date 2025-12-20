/*
 * zmq_socket.h - ZeroMQ socket interface for Klawed
 *
 * Provides IPC communication via ZeroMQ sockets for external integration.
 * Supports both request-reply and publish-subscribe patterns.
 */

#ifndef ZMQ_SOCKET_H
#define ZMQ_SOCKET_H

#include <stdbool.h>
#include <stddef.h>

// Forward declarations
struct ConversationState;
struct TUIState;

// ZMQ socket context
typedef struct ZMQContext {
    void *context;      // ZMQ context
    void *socket;       // ZMQ socket
    char *endpoint;     // Socket endpoint (e.g., "tcp://127.0.0.1:5555")
    int socket_type;    // ZMQ socket type (ZMQ_REP, ZMQ_PUB, etc.)
    bool enabled;       // Whether ZMQ mode is enabled
    bool daemon_mode;   // Whether running in daemon mode (listen for requests)
} ZMQContext;

/**
 * Initialize ZMQ context and socket
 * @param endpoint Socket endpoint (e.g., "tcp://127.0.0.1:5555" or "ipc:///tmp/klawed.sock")
 * @param socket_type ZMQ socket type (ZMQ_REP for server, ZMQ_REQ for client)
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
 * @return 0 on success, -1 on failure
 */
int zmq_socket_send(ZMQContext *ctx, const char *message, size_t message_len);

/**
 * Receive message from ZMQ socket with timeout
 * @param ctx ZMQ context
 * @param buffer Buffer to store received message
 * @param buffer_size Size of buffer
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = blocking)
 * @return Length of received message, -1 on error or timeout
 */
int zmq_socket_receive(ZMQContext *ctx, char *buffer, size_t buffer_size, int timeout_ms);

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
 * @param ctx ZMQ context (should be ZMQ_REP socket)
 * @param state Conversation state
 * @return 0 on success, -1 on failure
 */
int zmq_socket_daemon_mode(ZMQContext *ctx, struct ConversationState *state);

/**
 * Check if ZMQ is available at compile time
 * @return true if ZMQ support is compiled in, false otherwise
 */
bool zmq_socket_available(void);

#endif // ZMQ_SOCKET_H
