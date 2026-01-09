/*
 * uds_socket.h - Unix Domain Socket communication for Klawed
 *
 * Provides synchronous bidirectional communication via Unix domain sockets.
 * klawed acts as server, accepts single client connection.
 *
 * Features:
 * - SOCK_STREAM (reliable, TCP-like)
 * - Synchronous blocking send (blocks until peer receives response)
 * - 4-byte length header + JSON payload framing
 * - Automatic reconnection with configurable retries
 * - Message format matches existing ZMQ implementation
 */

#ifndef UDS_SOCKET_H
#define UDS_SOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations
struct ConversationState;

// Error codes
typedef enum {
    UDS_ERROR_NONE = 0,
    UDS_ERROR_INVALID_PARAM = -1,
    UDS_ERROR_SOCKET_CREATE = -2,
    UDS_ERROR_BIND_FAILED = -3,
    UDS_ERROR_LISTEN_FAILED = -4,
    UDS_ERROR_ACCEPT_FAILED = -5,
    UDS_ERROR_SEND_FAILED = -6,
    UDS_ERROR_RECEIVE_FAILED = -7,
    UDS_ERROR_RECEIVE_TIMEOUT = -8,
    UDS_ERROR_CONNECTION_CLOSED = -9,
    UDS_ERROR_NOT_SUPPORTED = -10,
    UDS_ERROR_NOMEM = -11,
    UDS_ERROR_PARTIAL_SEND = -12,
    UDS_ERROR_MESSAGE_TOO_LARGE = -13
} UDSErrorCode;

// Default configuration values
#define UDS_DEFAULT_RETRIES 5
#define UDS_DEFAULT_TIMEOUT_SEC 30
#define UDS_DEFAULT_BACKLOG 1        // Single client
#define UDS_MAX_MESSAGE_SIZE (64 * 1024 * 1024)  // 64MB max message
#define UDS_BUFFER_SIZE 65536        // 64KB read buffer

// Unix socket context
typedef struct UDSContext {
    int server_fd;           // Server socket file descriptor
    int client_fd;           // Connected client file descriptor
    char *socket_path;       // Socket file path
    bool enabled;            // Whether UDS mode is enabled

    // Configuration
    int max_retries;         // Max reconnection attempts
    int timeout_sec;         // Timeout for operations in seconds

    // State
    bool client_connected;   // Whether a client is currently connected
    uint32_t message_seq;    // Message sequence number for logging
} UDSContext;

/**
 * Initialize UDS context and create server socket
 * @param socket_path Unix socket file path (e.g., "/tmp/klawed.sock")
 * @return Initialized UDSContext or NULL on failure
 */
UDSContext* uds_socket_init(const char *socket_path);

/**
 * Clean up UDS resources, close sockets, unlink socket file
 * @param ctx UDS context to clean up
 */
void uds_socket_cleanup(UDSContext *ctx);

/**
 * Wait for and accept a client connection
 * Blocks until a client connects or timeout
 * @param ctx UDS context
 * @param timeout_sec Timeout in seconds (0 = infinite)
 * @return 0 on success, UDSErrorCode on failure
 */
int uds_socket_accept(UDSContext *ctx, int timeout_sec);

/**
 * Send message to connected client (blocking)
 * Blocks until peer sends a response back
 * Uses 4-byte length header + JSON payload framing
 * @param ctx UDS context
 * @param message JSON message string to send
 * @param message_len Length of message
 * @param response_buf Buffer to store peer's response
 * @param response_buf_size Size of response buffer
 * @return Length of response on success, UDSErrorCode on failure
 */
int uds_socket_send_receive(UDSContext *ctx, const char *message, size_t message_len,
                            char *response_buf, size_t response_buf_size);

/**
 * Send message to connected client (non-blocking response)
 * Does not wait for peer response, just ensures data is sent
 * @param ctx UDS context
 * @param message JSON message string to send
 * @param message_len Length of message
 * @return 0 on success, UDSErrorCode on failure
 */
int uds_socket_send(UDSContext *ctx, const char *message, size_t message_len);

/**
 * Receive message from client
 * Blocks until message received or timeout
 * @param ctx UDS context
 * @param buffer Buffer to store received message
 * @param buffer_size Size of buffer
 * @param timeout_sec Timeout in seconds (0 = infinite, -1 = non-blocking)
 * @return Length of received message on success, UDSErrorCode on failure
 */
int uds_socket_receive(UDSContext *ctx, char *buffer, size_t buffer_size, int timeout_sec);

/**
 * Check if UDS is available at compile time
 * @return true if UDS support is compiled in, false otherwise
 */
bool uds_socket_available(void);

/**
 * Run UDS daemon mode - listen for client, process messages
 * @param ctx UDS context
 * @param state Conversation state
 * @return 0 on success, -1 on failure
 */
int uds_socket_daemon_mode(UDSContext *ctx, struct ConversationState *state);

/**
 * Get file descriptor for UDS client socket (for use with select/poll)
 * @param ctx UDS context
 * @return File descriptor on success, -1 if no client connected
 */
int uds_socket_get_fd(UDSContext *ctx);

/**
 * Check if client is currently connected
 * @param ctx UDS context
 * @return true if connected, false otherwise
 */
bool uds_socket_is_connected(UDSContext *ctx);

/**
 * Disconnect current client (allows new client to connect)
 * @param ctx UDS context
 */
void uds_socket_disconnect_client(UDSContext *ctx);

#endif // UDS_SOCKET_H
