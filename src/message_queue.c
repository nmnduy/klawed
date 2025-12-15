/**
 * message_queue.c - Thread-safe message queue implementation
 */

#include "message_queue.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ========================================================================
 * TUI Message Queue Implementation
 * ======================================================================== */

int tui_msg_queue_init(TUIMessageQueue *queue, size_t capacity) {
    if (!queue || capacity == 0) {
        return -1;
    }

    memset(queue, 0, sizeof(*queue));

    queue->messages = calloc(capacity, sizeof(TUIMessage));
    if (!queue->messages) {
        return -1;
    }

    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->shutdown = false;

    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        free(queue->messages);
        return -1;
    }

    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        free(queue->messages);
        return -1;
    }

    return 0;
}

int post_tui_message(TUIMessageQueue *queue, TUIMessageType type, const char *text) {
    if (!queue) {
        return -1;
    }

    /* Copy text if provided */
    char *text_copy = NULL;
    if (text) {
        text_copy = strdup(text);
        if (!text_copy) {
            return -1;
        }
    }

    pthread_mutex_lock(&queue->mutex);

    /* If queue is full, drop oldest message (FIFO eviction) */
    if (queue->count == queue->capacity) {
        TUIMessage *oldest = &queue->messages[queue->tail];
        LOG_DEBUG("[TUI] Message queue at capacity (%zu) - dropping oldest message (type=%d)",
                  queue->capacity,
                  oldest->type);
        free(oldest->text);
        oldest->text = NULL;
        queue->tail = (queue->tail + 1) % queue->capacity;
        queue->count--;
    }

    /* Add new message at head */
    TUIMessage *msg = &queue->messages[queue->head];
    msg->type = type;
    msg->text = text_copy;
    msg->priority = 0; /* Reserved for future use */

    queue->head = (queue->head + 1) % queue->capacity;
    queue->count++;

    /* Signal waiting readers */
    pthread_cond_signal(&queue->not_empty);

    pthread_mutex_unlock(&queue->mutex);

    return 0;
}





int poll_tui_message(TUIMessageQueue *queue, TUIMessage *msg) {
    if (!queue || !msg) {
        return -1;
    }

    pthread_mutex_lock(&queue->mutex);

    /* Check if queue is empty */
    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    /* Retrieve message from tail */
    TUIMessage *src = &queue->messages[queue->tail];
    msg->type = src->type;
    msg->text = src->text; /* Transfer ownership to caller */
    msg->priority = src->priority;

    src->text = NULL; /* Clear so we don't double-free */
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count--;

    pthread_mutex_unlock(&queue->mutex);

    return 1;
}

int wait_tui_message(TUIMessageQueue *queue, TUIMessage *msg) {
    if (!queue || !msg) {
        return -1;
    }

    pthread_mutex_lock(&queue->mutex);

    /* Wait until message available or shutdown */
    while (queue->count == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    /* Check shutdown flag */
    if (queue->shutdown && queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    /* Retrieve message from tail */
    TUIMessage *src = &queue->messages[queue->tail];
    msg->type = src->type;
    msg->text = src->text; /* Transfer ownership to caller */
    msg->priority = src->priority;

    src->text = NULL; /* Clear so we don't double-free */
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count--;

    pthread_mutex_unlock(&queue->mutex);

    return 1;
}

void tui_msg_queue_shutdown(TUIMessageQueue *queue) {
    if (!queue) {
        return;
    }

    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = true;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
}

void tui_msg_queue_free(TUIMessageQueue *queue) {
    if (!queue) {
        return;
    }

    /* Free any remaining messages */
    if (queue->messages) {
        for (size_t i = 0; i < queue->capacity; i++) {
            free(queue->messages[i].text);
        }
        free(queue->messages);
        queue->messages = NULL;
    }

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
}

/* ========================================================================
 * AI Instruction Queue Implementation
 * ======================================================================== */

int ai_queue_init(AIInstructionQueue *queue, size_t capacity) {
    if (!queue || capacity == 0) {
        return -1;
    }

    memset(queue, 0, sizeof(*queue));

    queue->instructions = calloc(capacity, sizeof(AIInstruction));
    if (!queue->instructions) {
        return -1;
    }

    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->shutdown = false;

    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        free(queue->instructions);
        return -1;
    }

    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        free(queue->instructions);
        return -1;
    }

    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        free(queue->instructions);
        return -1;
    }

    return 0;
}

int enqueue_instruction(AIInstructionQueue *queue, const char *text, void *conversation_state) {
    if (!queue || !text) {
        return -1;
    }

    /* Copy text */
    char *text_copy = strdup(text);
    if (!text_copy) {
        return -1;
    }

    pthread_mutex_lock(&queue->mutex);

    /* Wait until space available or shutdown */
    while (queue->count == queue->capacity && !queue->shutdown) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }

    /* Check shutdown flag */
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        free(text_copy);
        return -1;
    }

    /* Add instruction at head */
    AIInstruction *instr = &queue->instructions[queue->head];
    instr->text = text_copy;
    instr->conversation_state = conversation_state;
    instr->priority = 0; /* Reserved for future use */

    queue->head = (queue->head + 1) % queue->capacity;
    queue->count++;

    /* Signal waiting readers */
    pthread_cond_signal(&queue->not_empty);

    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

int dequeue_instruction(AIInstructionQueue *queue, AIInstruction *instr) {
    if (!queue || !instr) {
        return -1;
    }

    pthread_mutex_lock(&queue->mutex);

    /* Wait until instruction available or shutdown */
    while (queue->count == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    /* Check shutdown flag */
    if (queue->shutdown && queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    /* Retrieve instruction from tail */
    AIInstruction *src = &queue->instructions[queue->tail];
    instr->text = src->text; /* Transfer ownership to caller */
    instr->conversation_state = src->conversation_state;
    instr->priority = src->priority;

    src->text = NULL; /* Clear so we don't double-free */
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count--;

    /* Signal waiting writers */
    pthread_cond_signal(&queue->not_full);

    pthread_mutex_unlock(&queue->mutex);

    return 1;
}

void ai_queue_shutdown(AIInstructionQueue *queue) {
    if (!queue) {
        return;
    }

    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = true;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}

void ai_queue_free(AIInstructionQueue *queue) {
    if (!queue) {
        return;
    }

    /* Free any remaining instructions */
    if (queue->instructions) {
        for (size_t i = 0; i < queue->capacity; i++) {
            free(queue->instructions[i].text);
        }
        free(queue->instructions);
        queue->instructions = NULL;
    }

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
}

int ai_queue_depth(AIInstructionQueue *queue) {
    if (!queue) {
        return -1;
    }

    pthread_mutex_lock(&queue->mutex);
    int depth = (int)queue->count;
    pthread_mutex_unlock(&queue->mutex);

    return depth;
}
