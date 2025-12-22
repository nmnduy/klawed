/*
 * zmq_socket_improved.h - Improved ZeroMQ socket interface for Klawed
 *
 * Enhanced version with better reliability, reconnection, and pub/sub support.
 */

#ifndef ZMQ_SOCKET_IMPROVED_H
#define ZMQ_SOCKET_IMPROVED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations
struct ConversationState;
struct TUIState;

// ZMQ socket modes
typedef enum {
    ZMQ_MODE_DAEMON = 0,    // Peer-to-peer server (ZMQ_PAIR)
    ZMQ_MODE_PUBLISHER,     // Publish events (ZMQ_PUB)
    ZMQ_MODE_SUBSCRIBER,    // Subscribe to events (ZMQ_SUB)
    ZMQ_MODE_CLIENT         // Peer-to-peer client (ZMQ_PAIR)
} ZMQMode;

// Connection state
typedef enum {
    ZMQ_CONNECTION_DISCONNECTED = 0,
    ZMQ_CONNECTION_CONNECTING,
    ZMQ_CONNECTION_CONNECTED,
    ZMQ_CONNECTION_RECONNECTING
} ZMQConnectionState;

// Message buffer for reliable delivery
typedef struct ZMQMessageBuffer {
    char *data;
    size_t size;
    uint64_t timestamp;
    int retry_count;
    struct ZMQMessageBuffer *next;
} ZMQMessageBuffer;

// Improved ZMQ socket context
typedef struct ZMQImprovedContext {
    void *context;                  // ZMQ context
    void *socket;                   // ZMQ socket
    char *endpoint;                 // Socket endpoint
    ZMQMode mode;                   // Socket mode
    ZMQConnectionState state;       // Current connection state
    
    // Reconnection settings
    int max_reconnect_attempts;     // Maximum reconnection attempts (0 = infinite)
    int reconnect_delay_ms;         // Base delay between reconnections
    int reconnect_attempts;         // Current reconnection attempt count
    time_t last_connection_time;    // Last successful connection time
    
    // Heartbeat settings
    bool enable_heartbeat;          // Enable heartbeat mechanism
    int heartbeat_interval_ms;      // Heartbeat interval
    time_t last_heartbeat_time;     // Last heartbeat sent/received
    
    // Message buffering (for pub/sub)
    ZMQMessageBuffer *message_queue; // Queue of unsent messages
    size_t max_queue_size;          // Maximum queue size in bytes
    size_t current_queue_size;      // Current queue size in bytes
    
    // Statistics
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t messages_dropped;
    uint64_t reconnections;
    
    // Callbacks (optional)
    void (*on_connect)(struct ZMQImprovedContext *ctx);
    void (*on_disconnect)(struct ZMQImprovedContext *ctx);
    void (*on_message)(struct ZMQImprovedContext *ctx, const char *message, size_t size);
    void (*on_error)(struct ZMQImprovedContext *ctx, const char *error);
    
    // User data pointer
    void *user_data;
} ZMQImprovedContext;

/**
 * Initialize improved ZMQ context with enhanced reliability features
 * @param endpoint Socket endpoint (e.g., "tcp://127.0.0.1:5555")
 * @param mode ZMQ operation mode
 * @param enable_buffering Enable message buffering for pub/sub
 * @return Initialized context or NULL on failure
 */
ZMQImprovedContext* zmq_improved_init(const char *endpoint, ZMQMode mode, bool enable_buffering);

/**
 * Clean up improved ZMQ resources
 * @param ctx ZMQ context to clean up
 */
void zmq_improved_cleanup(ZMQImprovedContext *ctx);

/**
 * Send message with reliability features
 * @param ctx ZMQ context
 * @param message Message to send
 * @param message_len Length of message
 * @param require_ack Whether to require acknowledgment (for reliable delivery)
 * @return 0 on success, -1 on failure (message may be queued if buffering enabled)
 */
int zmq_improved_send(ZMQImprovedContext *ctx, const char *message, size_t message_len, bool require_ack);

/**
 * Receive message with timeout and reliability features
 * @param ctx ZMQ context
 * @param buffer Buffer to store received message
 * @param buffer_size Size of buffer
 * @param timeout_ms Timeout in milliseconds (-1 = blocking, 0 = non-blocking)
 * @return Length of received message, -1 on error or timeout
 */
int zmq_improved_receive(ZMQImprovedContext *ctx, char *buffer, size_t buffer_size, int timeout_ms);

/**
 * Process incoming messages and handle reconnection/heartbeat
 * @param ctx ZMQ context
 * @param timeout_ms Timeout for processing loop
 * @return 0 on success, -1 on fatal error
 */
int zmq_improved_process(ZMQImprovedContext *ctx, int timeout_ms);

/**
 * Check if connection is healthy
 * @param ctx ZMQ context
 * @return true if connection is healthy, false otherwise
 */
bool zmq_improved_is_connected(ZMQImprovedContext *ctx);

/**
 * Force reconnection attempt
 * @param ctx ZMQ context
 * @return 0 on success, -1 on failure
 */
int zmq_improved_reconnect(ZMQImprovedContext *ctx);

/**
 * Get connection statistics
 * @param ctx ZMQ context
 * @param stats Structure to fill with statistics (can be NULL)
 */
void zmq_improved_get_stats(ZMQImprovedContext *ctx, struct {
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t messages_dropped;
    uint64_t reconnections;
    ZMQConnectionState state;
} *stats);

/**
 * Set callback functions
 * @param ctx ZMQ context
 * @param on_connect Callback when connected (can be NULL)
 * @param on_disconnect Callback when disconnected (can be NULL)
 * @param on_message Callback when message received (can be NULL)
 * @param on_error Callback when error occurs (can be NULL)
 */
void zmq_improved_set_callbacks(
    ZMQImprovedContext *ctx,
    void (*on_connect)(ZMQImprovedContext *ctx),
    void (*on_disconnect)(ZMQImprovedContext *ctx),
    void (*on_message)(ZMQImprovedContext *ctx, const char *message, size_t size),
    void (*on_error)(ZMQImprovedContext *ctx, const char *error)
);

/**
 * Set user data pointer
 * @param ctx ZMQ context
 * @param user_data User data pointer
 */
void zmq_improved_set_user_data(ZMQImprovedContext *ctx, void *user_data);

/**
 * Flush message queue (send all buffered messages)
 * @param ctx ZMQ context
 * @return Number of messages successfully sent
 */
int zmq_improved_flush(ZMQImprovedContext *ctx);

/**
 * Clear message queue (discard all buffered messages)
 * @param ctx ZMQ context
 * @return Number of messages cleared
 */
int zmq_improved_clear_queue(ZMQImprovedContext *ctx);

#endif // ZMQ_SOCKET_IMPROVED_H