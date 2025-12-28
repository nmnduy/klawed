/*
 * zmq_daemon.h - Thread-based ZMQ daemon for Klawed
 *
 * Implements a ZMQ daemon with separate receiver thread and main processing thread.
 * Uses thread-safe message queues for communication between threads.
 */

#ifndef ZMQ_DAEMON_H
#define ZMQ_DAEMON_H

#include "zmq_message_queue.h"
#include "zmq_socket.h"
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

// Forward declarations
struct ConversationState;

/**
 * ZMQ daemon context
 */
typedef struct ZMQDaemonContext {
    // Thread management
    pthread_t receiver_thread;
    bool running;
    bool thread_started;
    
    // ZMQ components
    void *zmq_context;
    void *zmq_socket;
    char *endpoint;
    int socket_type;  // ZMQ_PAIR for daemon
    
    // Message queues (thread-safe)
    ZMQMessageQueue *incoming_queue;   // Client → Daemon (user input)
    ZMQMessageQueue *outgoing_queue;   // Daemon → Client (responses)
    
    // Conversation state
    struct ConversationState *conv_state;
    
    // Message tracking (from existing zmq_socket.h)
    ZMQPendingQueue pending_queue;     // For reliable delivery
    ZMQSeenMessage seen_messages[1000];  // Duplicate detection
    int seen_message_count;
    
    // Statistics
    uint64_t messages_received;
    uint64_t messages_sent;
    uint64_t errors;
    
    // Synchronization
    pthread_mutex_t stats_mutex;
} ZMQDaemonContext;

/**
 * Initialize ZMQ daemon context
 * @param endpoint ZMQ endpoint (e.g., "tcp://127.0.0.1:5555")
 * @param conv_state Conversation state for AI processing
 * @return Initialized daemon context or NULL on error
 */
ZMQDaemonContext* zmq_daemon_init(const char *endpoint, struct ConversationState *conv_state);

/**
 * Start ZMQ daemon (starts receiver thread and enters main processing loop)
 * @param ctx Daemon context
 * @return 0 on success, -1 on error
 */
int zmq_daemon_start(ZMQDaemonContext *ctx);

/**
 * Stop ZMQ daemon (stops receiver thread and cleans up)
 * @param ctx Daemon context
 */
void zmq_daemon_stop(ZMQDaemonContext *ctx);

/**
 * Clean up ZMQ daemon resources
 * @param ctx Daemon context to clean up
 */
void zmq_daemon_cleanup(ZMQDaemonContext *ctx);

/**
 * Check if daemon is running
 * @param ctx Daemon context
 * @return true if running, false otherwise
 */
bool zmq_daemon_is_running(ZMQDaemonContext *ctx);

/**
 * Get daemon statistics
 * @param ctx Daemon context
 * @param messages_received Output: total messages received
 * @param messages_sent Output: total messages sent
 * @param errors Output: total errors
 */
void zmq_daemon_get_stats(ZMQDaemonContext *ctx, uint64_t *messages_received,
                          uint64_t *messages_sent, uint64_t *errors);

#endif // ZMQ_DAEMON_H
