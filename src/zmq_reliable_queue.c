/*
 * zmq_reliable_queue.c - Reliable message queue implementation for ZMQ delivery
 */

#include "zmq_reliable_queue.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

ZMQMessageQueue* zmq_reliable_queue_init(size_t max_size, size_t max_bytes, time_t max_age) {
    ZMQMessageQueue *queue = calloc(1, sizeof(ZMQMessageQueue));
    if (!queue) {
        LOG_ERROR("ZMQ: Failed to allocate reliable queue");
        return NULL;
    }
    
    queue->max_size = max_size;
    queue->max_bytes = max_bytes;
    queue->max_age = max_age;
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->total_bytes = 0;
    
    LOG_DEBUG("ZMQ: Reliable queue initialized (max_size: %zu, max_bytes: %zu, max_age: %ld)",
              max_size, max_bytes, max_age);
    
    return queue;
}

void zmq_reliable_queue_free(ZMQMessageQueue *queue) {
    if (!queue) return;
    
    // Clear all messages
    zmq_reliable_queue_clear(queue);
    
    // Free queue structure
    free(queue);
    
    LOG_DEBUG("ZMQ: Reliable queue freed");
}

bool zmq_reliable_queue_push(ZMQMessageQueue *queue, const char *data, size_t size) {
    if (!queue || !data || size == 0) {
        LOG_ERROR("ZMQ: Invalid parameters for queue push");
        return false;
    }
    
    // Clean up old messages first
    zmq_reliable_queue_cleanup(queue);
    
    // Check if queue is full by message count
    if (queue->max_size > 0 && queue->count >= queue->max_size) {
        LOG_WARN("ZMQ: Reliable queue full (count: %zu, max: %zu)", queue->count, queue->max_size);
        return false;
    }
    
    // Check if queue is full by byte size
    if (queue->max_bytes > 0 && (queue->total_bytes + size) > queue->max_bytes) {
        LOG_WARN("ZMQ: Reliable queue full by size (bytes: %zu, max: %zu, new: %zu)",
                 queue->total_bytes, queue->max_bytes, size);
        return false;
    }
    
    // Create new message
    ZMQMessage *msg = calloc(1, sizeof(ZMQMessage));
    if (!msg) {
        LOG_ERROR("ZMQ: Failed to allocate message");
        return false;
    }
    
    // Copy message data
    msg->data = malloc(size);
    if (!msg->data) {
        LOG_ERROR("ZMQ: Failed to allocate message data");
        free(msg);
        return false;
    }
    
    memcpy(msg->data, data, size);
    msg->size = size;
    msg->timestamp = time(NULL);
    msg->delivery_attempts = 0;
    msg->next = NULL;
    
    // Add to queue
    if (queue->tail) {
        queue->tail->next = msg;
        queue->tail = msg;
    } else {
        // First message
        queue->head = msg;
        queue->tail = msg;
    }
    
    queue->count++;
    queue->total_bytes += size;
    
    LOG_DEBUG("ZMQ: Message queued (size: %zu, count: %zu, total_bytes: %zu)",
              size, queue->count, queue->total_bytes);
    
    return true;
}

bool zmq_reliable_queue_peek(ZMQMessageQueue *queue, const char **data, size_t *size) {
    if (!queue || !data || !size) {
        LOG_ERROR("ZMQ: Invalid parameters for queue peek");
        return false;
    }
    
    // Clean up old messages first
    zmq_reliable_queue_cleanup(queue);
    
    if (!queue->head) {
        return false; // Queue empty
    }
    
    *data = queue->head->data;
    *size = queue->head->size;
    
    return true;
}

bool zmq_reliable_queue_pop(ZMQMessageQueue *queue) {
    if (!queue) {
        LOG_ERROR("ZMQ: Invalid parameters for queue pop");
        return false;
    }
    
    // Clean up old messages first
    zmq_reliable_queue_cleanup(queue);
    
    if (!queue->head) {
        return false; // Queue empty
    }
    
    ZMQMessage *msg = queue->head;
    
    // Update queue pointers
    queue->head = msg->next;
    if (!queue->head) {
        queue->tail = NULL; // Queue is now empty
    }
    
    // Update statistics
    queue->count--;
    queue->total_bytes -= msg->size;
    
    // Free message
    free(msg->data);
    free(msg);
    
    LOG_DEBUG("ZMQ: Message popped (remaining count: %zu, total_bytes: %zu)",
              queue->count, queue->total_bytes);
    
    return true;
}

bool zmq_reliable_queue_is_empty(const ZMQMessageQueue *queue) {
    return !queue || !queue->head;
}

size_t zmq_reliable_queue_count(const ZMQMessageQueue *queue) {
    return queue ? queue->count : 0;
}

size_t zmq_reliable_queue_bytes(const ZMQMessageQueue *queue) {
    return queue ? queue->total_bytes : 0;
}

size_t zmq_reliable_queue_cleanup(ZMQMessageQueue *queue) {
    if (!queue || queue->max_age == 0) {
        return 0;
    }
    
    time_t now = time(NULL);
    size_t removed = 0;
    
    // Remove messages older than max_age
    while (queue->head && (now - queue->head->timestamp) > queue->max_age) {
        ZMQMessage *msg = queue->head;
        queue->head = msg->next;
        
        queue->count--;
        queue->total_bytes -= msg->size;
        removed++;
        
        free(msg->data);
        free(msg);
    }
    
    // Update tail if needed
    if (!queue->head) {
        queue->tail = NULL;
    }
    
    if (removed > 0) {
        LOG_DEBUG("ZMQ: Cleaned up %zu old messages (max_age: %ld seconds)",
                  removed, queue->max_age);
    }
    
    return removed;
}

size_t zmq_reliable_queue_clear(ZMQMessageQueue *queue) {
    if (!queue) return 0;
    
    size_t cleared = 0;
    
    while (queue->head) {
        ZMQMessage *msg = queue->head;
        queue->head = msg->next;
        
        free(msg->data);
        free(msg);
        cleared++;
    }
    
    queue->tail = NULL;
    queue->count = 0;
    queue->total_bytes = 0;
    
    LOG_DEBUG("ZMQ: Cleared %zu messages from reliable queue", cleared);
    
    return cleared;
}
