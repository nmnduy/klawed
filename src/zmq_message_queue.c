/*
 * zmq_message_queue.c - Thread-safe message queue implementation
 */

#include "zmq_message_queue.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <bsd/string.h>

/**
 * Initialize a message queue
 */
int zmq_queue_init(ZMQMessageQueue *queue, size_t max_capacity) {
    if (!queue) {
        LOG_ERROR("ZMQ Queue: Cannot initialize NULL queue");
        return -1;
    }
    
    // Initialize queue structure
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->max_capacity = max_capacity;
    queue->total_enqueued = 0;
    queue->total_dequeued = 0;
    queue->overflow_count = 0;
    
    // Initialize mutex
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        LOG_ERROR("ZMQ Queue: Failed to initialize mutex: %s", strerror(errno));
        return -1;
    }
    
    // Initialize condition variables
    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        LOG_ERROR("ZMQ Queue: Failed to initialize not_empty condition: %s", strerror(errno));
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    
    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        LOG_ERROR("ZMQ Queue: Failed to initialize not_full condition: %s", strerror(errno));
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    
    LOG_DEBUG("ZMQ Queue: Initialized with capacity %zu", max_capacity);
    return 0;
}

/**
 * Destroy a message queue and free all messages
 */
void zmq_queue_destroy(ZMQMessageQueue *queue) {
    if (!queue) return;
    
    LOG_DEBUG("ZMQ Queue: Destroying queue with %zu messages", queue->count);
    
    // Clear all messages
    zmq_queue_clear(queue);
    
    // Destroy synchronization primitives
    pthread_cond_destroy(&queue->not_full);
    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
    
    LOG_DEBUG("ZMQ Queue: Destroyed");
}

/**
 * Enqueue a message (thread-safe)
 */
int zmq_queue_enqueue(ZMQMessageQueue *queue, const ZMQMessage *msg, int timeout_ms) {
    if (!queue || !msg) {
        LOG_ERROR("ZMQ Queue: Invalid parameters for enqueue");
        return -2;
    }
    
    int result = 0;
    struct timespec timeout_ts;
    
    // Calculate absolute timeout if needed
    if (timeout_ms > 0) {
        clock_gettime(CLOCK_REALTIME, &timeout_ts);
        timeout_ts.tv_sec += timeout_ms / 1000;
        timeout_ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (timeout_ts.tv_nsec >= 1000000000) {
            timeout_ts.tv_sec += 1;
            timeout_ts.tv_nsec -= 1000000000;
        }
    }
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait for space if queue is full (with timeout if specified)
    while (queue->max_capacity > 0 && queue->count >= queue->max_capacity) {
        if (timeout_ms == 0) {
            // Non-blocking mode
            pthread_mutex_unlock(&queue->mutex);
            queue->overflow_count++;
            LOG_DEBUG("ZMQ Queue: Queue full (capacity %zu), enqueue failed (non-blocking)",
                     queue->max_capacity);
            return -1;
        } else if (timeout_ms > 0) {
            // Timed wait
            int rc = pthread_cond_timedwait(&queue->not_full, &queue->mutex, &timeout_ts);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&queue->mutex);
                queue->overflow_count++;
                LOG_DEBUG("ZMQ Queue: Queue full, enqueue timeout after %d ms", timeout_ms);
                return -1;
            } else if (rc != 0) {
                pthread_mutex_unlock(&queue->mutex);
                LOG_ERROR("ZMQ Queue: pthread_cond_timedwait error: %s", strerror(rc));
                return -2;
            }
        } else {
            // Infinite wait
            pthread_cond_wait(&queue->not_full, &queue->mutex);
        }
    }
    
    // Create a copy of the message
    ZMQMessage *new_msg = zmq_message_copy(msg);
    if (!new_msg) {
        pthread_mutex_unlock(&queue->mutex);
        LOG_ERROR("ZMQ Queue: Failed to copy message for enqueue");
        return -2;
    }
    
    // Add to queue
    new_msg->next = NULL;
    if (queue->tail) {
        queue->tail->next = new_msg;
        queue->tail = new_msg;
    } else {
        queue->head = new_msg;
        queue->tail = new_msg;
    }
    queue->count++;
    queue->total_enqueued++;
    
    LOG_DEBUG("ZMQ Queue: Enqueued message (type: %s, id: %s, queue size: %zu)",
              new_msg->message_type ? new_msg->message_type : "unknown",
              new_msg->message_id[0] ? new_msg->message_id : "none",
              queue->count);
    
    // Signal that queue is not empty
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return result;
}

/**
 * Dequeue a message (thread-safe)
 */
int zmq_queue_dequeue(ZMQMessageQueue *queue, ZMQMessage *msg, int timeout_ms) {
    if (!queue || !msg) {
        LOG_ERROR("ZMQ Queue: Invalid parameters for dequeue");
        return -2;
    }
    
    int result = 0;
    struct timespec timeout_ts;
    
    // Calculate absolute timeout if needed
    if (timeout_ms > 0) {
        clock_gettime(CLOCK_REALTIME, &timeout_ts);
        timeout_ts.tv_sec += timeout_ms / 1000;
        timeout_ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (timeout_ts.tv_nsec >= 1000000000) {
            timeout_ts.tv_sec += 1;
            timeout_ts.tv_nsec -= 1000000000;
        }
    }
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait for message if queue is empty (with timeout if specified)
    while (queue->count == 0) {
        if (timeout_ms == 0) {
            // Non-blocking mode
            pthread_mutex_unlock(&queue->mutex);
            return -1;
        } else if (timeout_ms > 0) {
            // Timed wait
            int rc = pthread_cond_timedwait(&queue->not_empty, &queue->mutex, &timeout_ts);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&queue->mutex);
                return -1;
            } else if (rc != 0) {
                pthread_mutex_unlock(&queue->mutex);
                LOG_ERROR("ZMQ Queue: pthread_cond_timedwait error: %s", strerror(rc));
                return -2;
            }
        } else {
            // Infinite wait
            pthread_cond_wait(&queue->not_empty, &queue->mutex);
        }
    }
    
    // Remove message from queue
    ZMQMessage *old_msg = queue->head;
    queue->head = old_msg->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    queue->count--;
    queue->total_dequeued++;
    
    // Copy message data to output
    msg->data = old_msg->data;  // Transfer ownership
    msg->length = old_msg->length;
    strlcpy(msg->message_id, old_msg->message_id, sizeof(msg->message_id));
    msg->message_type = old_msg->message_type;  // Transfer ownership
    msg->timestamp_ms = old_msg->timestamp_ms;
    msg->next = NULL;
    
    // Free the queue node (but not the data)
    free(old_msg);
    
    LOG_DEBUG("ZMQ Queue: Dequeued message (type: %s, id: %s, queue size: %zu)",
              msg->message_type ? msg->message_type : "unknown",
              msg->message_id[0] ? msg->message_id : "none",
              queue->count);
    
    // Signal that queue is not full (if capacity limited)
    if (queue->max_capacity > 0) {
        pthread_cond_signal(&queue->not_full);
    }
    pthread_mutex_unlock(&queue->mutex);
    
    return result;
}

/**
 * Try to dequeue a message without blocking
 */
int zmq_queue_try_dequeue(ZMQMessageQueue *queue, ZMQMessage *msg) {
    return zmq_queue_dequeue(queue, msg, 0);
}

/**
 * Check if queue is empty (thread-safe)
 */
bool zmq_queue_is_empty(ZMQMessageQueue *queue) {
    if (!queue) return true;
    
    pthread_mutex_lock(&queue->mutex);
    bool empty = (queue->count == 0);
    pthread_mutex_unlock(&queue->mutex);
    
    return empty;
}

/**
 * Get current queue size (thread-safe)
 */
size_t zmq_queue_size(ZMQMessageQueue *queue) {
    if (!queue) return 0;
    
    pthread_mutex_lock(&queue->mutex);
    size_t count = queue->count;
    pthread_mutex_unlock(&queue->mutex);
    
    return count;
}

/**
 * Clear all messages from queue (thread-safe)
 */
void zmq_queue_clear(ZMQMessageQueue *queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    
    ZMQMessage *curr = queue->head;
    while (curr) {
        ZMQMessage *next = curr->next;
        zmq_message_free(curr);
        curr = next;
    }
    
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    
    // Wake up any waiting threads
    pthread_cond_broadcast(&queue->not_full);
    
    pthread_mutex_unlock(&queue->mutex);
    
    LOG_DEBUG("ZMQ Queue: Cleared all messages");
}

/**
 * Get queue statistics (thread-safe)
 */
void zmq_queue_get_stats(ZMQMessageQueue *queue, uint64_t *total_enqueued,
                         uint64_t *total_dequeued, uint64_t *overflow_count) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    
    if (total_enqueued) *total_enqueued = queue->total_enqueued;
    if (total_dequeued) *total_dequeued = queue->total_dequeued;
    if (overflow_count) *overflow_count = queue->overflow_count;
    
    pthread_mutex_unlock(&queue->mutex);
}

/**
 * Create a new ZMQ message
 */
ZMQMessage* zmq_message_create(const char *data, size_t length,
                               const char *message_id, const char *message_type) {
    if (!data || length == 0) {
        LOG_ERROR("ZMQ Queue: Cannot create message with NULL data or zero length");
        return NULL;
    }
    
    ZMQMessage *msg = calloc(1, sizeof(ZMQMessage));
    if (!msg) {
        LOG_ERROR("ZMQ Queue: Failed to allocate message");
        return NULL;
    }
    
    // Copy message data
    msg->data = malloc(length + 1);
    if (!msg->data) {
        LOG_ERROR("ZMQ Queue: Failed to allocate message data");
        free(msg);
        return NULL;
    }
    memcpy(msg->data, data, length);
    msg->data[length] = '\0';
    msg->length = length;
    
    // Copy message ID if provided
    if (message_id && message_id[0]) {
        strlcpy(msg->message_id, message_id, sizeof(msg->message_id));
    } else {
        msg->message_id[0] = '\0';
    }
    
    // Copy message type if provided
    if (message_type && message_type[0]) {
        msg->message_type = strdup(message_type);
        if (!msg->message_type) {
            LOG_ERROR("ZMQ Queue: Failed to allocate message type");
            free(msg->data);
            free(msg);
            return NULL;
        }
    }
    
    // Set timestamp
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    msg->timestamp_ms = (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
    
    LOG_DEBUG("ZMQ Queue: Created message (type: %s, id: %s, length: %zu)",
              msg->message_type ? msg->message_type : "unknown",
              msg->message_id[0] ? msg->message_id : "none",
              length);
    
    return msg;
}

/**
 * Free a ZMQ message
 */
void zmq_message_free(ZMQMessage *msg) {
    if (!msg) return;
    
    if (msg->data) {
        free(msg->data);
    }
    if (msg->message_type) {
        free(msg->message_type);
    }
    free(msg);
}

/**
 * Copy a ZMQ message
 */
ZMQMessage* zmq_message_copy(const ZMQMessage *src) {
    if (!src) return NULL;
    
    return zmq_message_create(src->data, src->length,
                              src->message_id, src->message_type);
}
