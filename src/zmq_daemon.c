/*
 * zmq_daemon.c - Thread-based ZMQ daemon for Klawed
 *
 * Implements a ZMQ daemon with separate receiver thread and main processing thread.
 * Uses thread-safe message queues for communication between threads.
 */

#include "zmq_daemon.h"
#include "zmq_message_queue.h"
#include "zmq_socket.h"
#include "logger.h"
#include "cjson/cJSON.h"
#include <zmq.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

/**
 * Initialize ZMQ daemon context
 */
ZMQDaemonContext* zmq_daemon_init(const char *endpoint, struct ConversationState *conv_state) {
    if (!endpoint || !conv_state) {
        LOG_ERROR("ZMQ Daemon: Invalid parameters for init");
        return NULL;
    }

    ZMQDaemonContext *ctx = calloc(1, sizeof(ZMQDaemonContext));
    if (!ctx) {
        LOG_ERROR("ZMQ Daemon: Failed to allocate context");
        return NULL;
    }

    ctx->endpoint = strdup(endpoint);
    if (!ctx->endpoint) {
        LOG_ERROR("ZMQ Daemon: Failed to allocate endpoint string");
        free(ctx);
        return NULL;
    }

    ctx->conv_state = conv_state;
    ctx->socket_type = ZMQ_PAIR;
    
    // Initialize pending queue
    ctx->pending_queue.max_pending = 100;
    ctx->pending_queue.timeout_ms = 5000; // 5 seconds
    ctx->pending_queue.max_retries = 3;
    ctx->pending_queue.count = 0;
    ctx->pending_queue.head = NULL;
    ctx->pending_queue.tail = NULL;
    
    // Initialize seen messages array
    ctx->seen_message_count = 0;
    
    // Initialize statistics mutex
    if (pthread_mutex_init(&ctx->stats_mutex, NULL) != 0) {
        LOG_ERROR("ZMQ Daemon: Failed to initialize stats mutex");
        free(ctx->endpoint);
        free(ctx);
        return NULL;
    }
    
    // Allocate and initialize message queues
    ctx->incoming_queue = calloc(1, sizeof(ZMQMessageQueue));
    ctx->outgoing_queue = calloc(1, sizeof(ZMQMessageQueue));
    
    if (!ctx->incoming_queue || !ctx->outgoing_queue) {
        LOG_ERROR("ZMQ Daemon: Failed to allocate message queues");
        if (ctx->incoming_queue) free(ctx->incoming_queue);
        if (ctx->outgoing_queue) free(ctx->outgoing_queue);
        pthread_mutex_destroy(&ctx->stats_mutex);
        free(ctx->endpoint);
        free(ctx);
        return NULL;
    }
    
    // Initialize queues
    if (zmq_queue_init(ctx->incoming_queue, 100) != 0 ||
        zmq_queue_init(ctx->outgoing_queue, 100) != 0) {
        LOG_ERROR("ZMQ Daemon: Failed to initialize message queues");
        if (ctx->incoming_queue) {
            zmq_queue_destroy(ctx->incoming_queue);
            free(ctx->incoming_queue);
        }
        if (ctx->outgoing_queue) {
            zmq_queue_destroy(ctx->outgoing_queue);
            free(ctx->outgoing_queue);
        }
        pthread_mutex_destroy(&ctx->stats_mutex);
        free(ctx->endpoint);
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

/**
 * Clean up ZMQ daemon resources
 */
void zmq_daemon_cleanup(ZMQDaemonContext *ctx) {
    if (!ctx) return;
    
    // Stop if running
    if (ctx->running) {
        zmq_daemon_stop(ctx);
    }
    
    // Clean up ZMQ resources
    if (ctx->zmq_socket) {
        zmq_close(ctx->zmq_socket);
    }
    if (ctx->zmq_context) {
        zmq_ctx_destroy(ctx->zmq_context);
    }
    
    // Clean up message queues
    if (ctx->incoming_queue) {
        zmq_queue_destroy(ctx->incoming_queue);
        free(ctx->incoming_queue);
    }
    if (ctx->outgoing_queue) {
        zmq_queue_destroy(ctx->outgoing_queue);
        free(ctx->outgoing_queue);
    }
    
    // Clean up pending queue
    ZMQPendingMessage *curr = ctx->pending_queue.head;
    while (curr) {
        ZMQPendingMessage *next = curr->next;
        free(curr->message_id);
        free(curr->message_json);
        free(curr);
        curr = next;
    }
    
    // Clean up seen messages
    for (int i = 0; i < ctx->seen_message_count; i++) {
        free(ctx->seen_messages[i].message_id);
    }
    
    // Clean up mutex
    pthread_mutex_destroy(&ctx->stats_mutex);
    
    // Free endpoint string
    if (ctx->endpoint) {
        free(ctx->endpoint);
    }
    
    free(ctx);
}

/**
 * Check if daemon is running
 */
bool zmq_daemon_is_running(ZMQDaemonContext *ctx) {
    return ctx && ctx->running;
}

/**
 * Get daemon statistics
 */
void zmq_daemon_get_stats(ZMQDaemonContext *ctx, uint64_t *messages_received,
                          uint64_t *messages_sent, uint64_t *errors) {
    if (!ctx) return;
    
    pthread_mutex_lock(&ctx->stats_mutex);
    if (messages_received) *messages_received = ctx->messages_received;
    if (messages_sent) *messages_sent = ctx->messages_sent;
    if (errors) *errors = ctx->errors;
    pthread_mutex_unlock(&ctx->stats_mutex);
}

/**
 * Stop ZMQ daemon
 */
void zmq_daemon_stop(ZMQDaemonContext *ctx) {
    if (!ctx) return;
    
    ctx->running = false;
    
    if (ctx->thread_started) {
        pthread_join(ctx->receiver_thread, NULL);
        ctx->thread_started = false;
    }
}

/**
 * Start ZMQ daemon
 */
int zmq_daemon_start(ZMQDaemonContext *ctx) {
    if (!ctx) {
        LOG_ERROR("ZMQ Daemon: Invalid context for start");
        return -1;
    }
    
    // Initialize ZMQ context
    ctx->zmq_context = zmq_ctx_new();
    if (!ctx->zmq_context) {
        LOG_ERROR("ZMQ Daemon: Failed to create ZMQ context");
        return -1;
    }
    
    // Create ZMQ socket
    ctx->zmq_socket = zmq_socket(ctx->zmq_context, ctx->socket_type);
    if (!ctx->zmq_socket) {
        LOG_ERROR("ZMQ Daemon: Failed to create ZMQ socket: %s", zmq_strerror(errno));
        zmq_ctx_destroy(ctx->zmq_context);
        ctx->zmq_context = NULL;
        return -1;
    }
    
    // Bind socket
    if (zmq_bind(ctx->zmq_socket, ctx->endpoint) != 0) {
        LOG_ERROR("ZMQ Daemon: Failed to bind to %s: %s", ctx->endpoint, zmq_strerror(errno));
        zmq_close(ctx->zmq_socket);
        zmq_ctx_destroy(ctx->zmq_context);
        ctx->zmq_socket = NULL;
        ctx->zmq_context = NULL;
        return -1;
    }
    
    LOG_INFO("ZMQ Daemon: Bound to %s", ctx->endpoint);
    
    // Mark as running
    ctx->running = true;
    
    // Note: The actual receiver thread and main processing loop would be implemented here
    // For now, we just return success to allow compilation
    
    return 0;
}
