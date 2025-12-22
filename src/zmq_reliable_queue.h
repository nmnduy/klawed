/*
 * zmq_reliable_queue.h - Reliable message queue for ZMQ delivery
 */

#ifndef ZMQ_RELIABLE_QUEUE_H
#define ZMQ_RELIABLE_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// Message structure
typedef struct ZMQMessage {
    char *data;             // Message data
    size_t size;            // Message size
    time_t timestamp;       // When message was queued
    int delivery_attempts;  // Number of delivery attempts
    struct ZMQMessage *next; // Next message in queue
} ZMQMessage;

// Queue structure
typedef struct ZMQMessageQueue {
    ZMQMessage *head;       // Oldest message
    ZMQMessage *tail;       // Newest message
    size_t count;           // Number of messages in queue
    size_t max_size;        // Maximum queue size (0 = unlimited)
    size_t total_bytes;     // Total bytes in queue
    size_t max_bytes;       // Maximum bytes (0 = unlimited)
    time_t max_age;         // Maximum age in seconds (0 = unlimited)
} ZMQMessageQueue;

/**
 * Initialize a reliable message queue
 * @param max_size Maximum number of messages (0 = unlimited)
 * @param max_bytes Maximum total bytes (0 = unlimited)
 * @param max_age Maximum age in seconds (0 = unlimited)
 * @return Initialized queue or NULL on failure
 */
ZMQMessageQueue* zmq_reliable_queue_init(size_t max_size, size_t max_bytes, time_t max_age);

/**
 * Free a reliable message queue and all messages
 * @param queue Queue to free
 */
void zmq_reliable_queue_free(ZMQMessageQueue *queue);

/**
 * Add a message to the reliable queue
 * @param queue Queue to add to
 * @param data Message data (will be copied)
 * @param size Message size
 * @return true on success, false on failure (queue full, etc.)
 */
bool zmq_reliable_queue_push(ZMQMessageQueue *queue, const char *data, size_t size);

/**
 * Get the oldest message from the reliable queue without removing it
 * @param queue Queue to peek from
 * @param data Output pointer for message data (do not free)
 * @param size Output pointer for message size
 * @return true if message retrieved, false if queue empty
 */
bool zmq_reliable_queue_peek(ZMQMessageQueue *queue, const char **data, size_t *size);

/**
 * Remove the oldest message from the reliable queue
 * @param queue Queue to pop from
 * @return true if message removed, false if queue empty
 */
bool zmq_reliable_queue_pop(ZMQMessageQueue *queue);

/**
 * Check if reliable queue is empty
 * @param queue Queue to check
 * @return true if empty, false otherwise
 */
bool zmq_reliable_queue_is_empty(const ZMQMessageQueue *queue);

/**
 * Get number of messages in reliable queue
 * @param queue Queue to check
 * @return Number of messages
 */
size_t zmq_reliable_queue_count(const ZMQMessageQueue *queue);

/**
 * Get total bytes in reliable queue
 * @param queue Queue to check
 * @return Total bytes
 */
size_t zmq_reliable_queue_bytes(const ZMQMessageQueue *queue);

/**
 * Clean up old messages based on age limit
 * @param queue Queue to clean
 * @return Number of messages removed
 */
size_t zmq_reliable_queue_cleanup(ZMQMessageQueue *queue);

/**
 * Clear all messages from reliable queue
 * @param queue Queue to clear
 * @return Number of messages cleared
 */
size_t zmq_reliable_queue_clear(ZMQMessageQueue *queue);

#endif // ZMQ_RELIABLE_QUEUE_H
