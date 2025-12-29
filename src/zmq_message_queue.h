/*
 * zmq_message_queue.h - Thread-safe message queue for ZMQ daemon and client
 *
 * Provides thread-safe FIFO message queues for communication between
 * receiver threads and main processing threads in the ZMQ architecture.
 */

#ifndef ZMQ_MESSAGE_QUEUE_H
#define ZMQ_MESSAGE_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

// Forward declaration
typedef struct ZMQMessage ZMQMessage;

/**
 * Thread-safe FIFO message queue
 */
typedef struct ZMQMessageQueue {
    ZMQMessage *head;           // First message in queue
    ZMQMessage *tail;           // Last message in queue
    size_t count;               // Number of messages in queue
    size_t max_capacity;        // Maximum capacity (0 = unlimited)

    // Synchronization
    pthread_mutex_t mutex;      // Mutex for thread-safe operations
    pthread_cond_t not_empty;   // Condition variable for waiting consumers
    pthread_cond_t not_full;    // Condition variable for waiting producers

    // Statistics
    uint64_t total_enqueued;    // Total messages enqueued
    uint64_t total_dequeued;    // Total messages dequeued
    uint64_t overflow_count;    // Number of times queue was full
} ZMQMessageQueue;

/**
 * ZMQ message structure
 */
struct ZMQMessage {
    char *data;                 // Message data (JSON string)
    size_t length;              // Length of message data
    char message_id[33];        // Message ID (32 hex chars + null terminator)
    char *message_type;         // Message type (e.g., "TEXT", "TOOL", "ACK")
    int64_t timestamp_ms;       // Timestamp when message was created

    ZMQMessage *next;           // Next message in queue
};

/**
 * Initialize a message queue
 * @param queue Queue to initialize
 * @param max_capacity Maximum capacity (0 = unlimited)
 * @return 0 on success, -1 on error
 */
int zmq_queue_init(ZMQMessageQueue *queue, size_t max_capacity);

/**
 * Destroy a message queue and free all messages
 * @param queue Queue to destroy
 */
void zmq_queue_destroy(ZMQMessageQueue *queue);

/**
 * Enqueue a message (thread-safe)
 * @param queue Queue to enqueue to
 * @param msg Message to enqueue (will be copied)
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = infinite)
 * @return 0 on success, -1 on timeout, -2 on error
 */
int zmq_queue_enqueue(ZMQMessageQueue *queue, const ZMQMessage *msg, int timeout_ms);

/**
 * Dequeue a message (thread-safe)
 * @param queue Queue to dequeue from
 * @param msg Output message (must be freed by caller)
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = infinite)
 * @return 0 on success, -1 on timeout, -2 on error
 */
int zmq_queue_dequeue(ZMQMessageQueue *queue, ZMQMessage *msg, int timeout_ms);

/**
 * Try to dequeue a message without blocking
 * @param queue Queue to dequeue from
 * @param msg Output message (must be freed by caller)
 * @return 0 on success, -1 if queue is empty
 */
int zmq_queue_try_dequeue(ZMQMessageQueue *queue, ZMQMessage *msg);

/**
 * Check if queue is empty (thread-safe)
 * @param queue Queue to check
 * @return true if empty, false otherwise
 */
bool zmq_queue_is_empty(ZMQMessageQueue *queue);

/**
 * Get current queue size (thread-safe)
 * @param queue Queue to check
 * @return Number of messages in queue
 */
size_t zmq_queue_size(ZMQMessageQueue *queue);

/**
 * Clear all messages from queue (thread-safe)
 * @param queue Queue to clear
 */
void zmq_queue_clear(ZMQMessageQueue *queue);

/**
 * Get queue statistics (thread-safe)
 * @param queue Queue to get statistics from
 * @param total_enqueued Output: total messages enqueued
 * @param total_dequeued Output: total messages dequeued
 * @param overflow_count Output: number of overflow events
 */
void zmq_queue_get_stats(ZMQMessageQueue *queue, uint64_t *total_enqueued,
                         uint64_t *total_dequeued, uint64_t *overflow_count);

/**
 * Create a new ZMQ message
 * @param data Message data (will be copied)
 * @param length Length of message data
 * @param message_id Message ID (optional, can be empty string)
 * @param message_type Message type (optional, can be NULL)
 * @return New message or NULL on error
 */
ZMQMessage* zmq_message_create(const char *data, size_t length,
                               const char *message_id, const char *message_type);

/**
 * Free a ZMQ message
 * @param msg Message to free
 */
void zmq_message_free(ZMQMessage *msg);

/**
 * Copy a ZMQ message
 * @param src Source message to copy
 * @return New copy of message or NULL on error
 */
ZMQMessage* zmq_message_copy(const ZMQMessage *src);

#endif // ZMQ_MESSAGE_QUEUE_H
