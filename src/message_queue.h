/**
 * message_queue.h - Thread-safe message queues for async TUI communication
 *
 * Provides two types of queues:
 * 1. TUI Message Queue: Worker -> Main thread (UI updates)
 * 2. AI Instruction Queue: Main -> Worker thread (user commands)
 */

#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <pthread.h>
#include <stdbool.h>

/* ========================================================================
 * TUI Message Queue (Worker -> Main Thread)
 * ======================================================================== */

/**
 * Types of messages that can be posted to the TUI
 */
typedef enum {
    TUI_MSG_ADD_LINE,       /* Add a line to conversation display */
    TUI_MSG_STATUS,         /* Update status line */
    TUI_MSG_CLEAR,          /* Clear conversation display */
    TUI_MSG_ERROR,          /* Display error message */
    TUI_MSG_TODO_UPDATE    /* Update TODO list */
} TUIMessageType;

/**
 * Message structure for TUI updates
 * Main thread reads these and updates ncurses display
 */
typedef struct {
    TUIMessageType type;
    char *text;             /* Owned by queue, freed after processing */
    int priority;           /* Higher = more urgent (reserved for future) */


} TUIMessage;

/**
 * Thread-safe circular buffer for TUI messages
 * Overflow policy: Drop oldest messages (FIFO eviction)
 */
typedef struct {
    TUIMessage *messages;   /* Circular buffer */
    size_t capacity;        /* Max messages before dropping oldest */
    size_t head;            /* Next write position */
    size_t tail;            /* Next read position */
    size_t count;           /* Current number of messages */

    pthread_mutex_t mutex;
    pthread_cond_t not_empty; /* Signals when messages available */

    bool shutdown;          /* Set to true to wake up blocked readers */
} TUIMessageQueue;

/**
 * Initialize TUI message queue
 *
 * @param queue Queue to initialize
 * @param capacity Maximum number of messages before overflow
 * @return 0 on success, -1 on error
 */
int tui_msg_queue_init(TUIMessageQueue *queue, size_t capacity);

/**
 * Post a message to the TUI queue
 * Non-blocking. If queue is full, drops oldest message.
 *
 * @param queue Queue to post to
 * @param type Message type
 * @param text Message text (will be copied, caller retains ownership)
 * @return 0 on success, -1 on error
 */
int post_tui_message(TUIMessageQueue *queue, TUIMessageType type, const char *text);





/**
 * Poll for a message from the TUI queue (non-blocking)
 *
 * @param queue Queue to poll
 * @param msg Output parameter for message (caller must free msg->text)
 * @return 1 if message retrieved, 0 if empty, -1 on error
 */
int poll_tui_message(TUIMessageQueue *queue, TUIMessage *msg);

/**
 * Wait for a message from the TUI queue (blocking)
 *
 * @param queue Queue to wait on
 * @param msg Output parameter for message (caller must free msg->text)
 * @return 1 if message retrieved, 0 if shutdown, -1 on error
 */
int wait_tui_message(TUIMessageQueue *queue, TUIMessage *msg);

/**
 * Shutdown TUI message queue and wake blocked readers
 *
 * @param queue Queue to shutdown
 */
void tui_msg_queue_shutdown(TUIMessageQueue *queue);

/**
 * Free TUI message queue resources
 * Must be called after all threads have stopped using it
 *
 * @param queue Queue to free
 */
void tui_msg_queue_free(TUIMessageQueue *queue);


/* ========================================================================
 * AI Instruction Queue (Main Thread -> Worker)
 * ======================================================================== */

/**
 * Instruction for the AI worker thread
 * Contains user prompt and context
 */
typedef struct {
    char *text;             /* User instruction text (owned by instruction) */
    void *conversation_state; /* Pointer to ConversationState (shared, needs locking) */
    int priority;           /* Higher = process first (reserved for future) */
} AIInstruction;

/**
 * Thread-safe queue for AI instructions
 * Overflow policy: Block sender until space available
 */
typedef struct {
    AIInstruction *instructions; /* Circular buffer */
    size_t capacity;        /* Max queued instructions */
    size_t head;            /* Next write position */
    size_t tail;            /* Next read position */
    size_t count;           /* Current number of instructions */

    pthread_mutex_t mutex;
    pthread_cond_t not_empty; /* Signals when instructions available */
    pthread_cond_t not_full;  /* Signals when space available */

    bool shutdown;          /* Set to true to stop worker */
} AIInstructionQueue;

/**
 * Initialize AI instruction queue
 *
 * @param queue Queue to initialize
 * @param capacity Maximum number of queued instructions
 * @return 0 on success, -1 on error
 */
int ai_queue_init(AIInstructionQueue *queue, size_t capacity);

/**
 * Enqueue an instruction for the AI worker
 * Blocks if queue is full.
 *
 * @param queue Queue to enqueue to
 * @param text Instruction text (will be copied, caller retains ownership)
 * @param conversation_state Pointer to ConversationState (borrowed reference)
 * @return 0 on success, -1 on error or shutdown
 */
int enqueue_instruction(AIInstructionQueue *queue, const char *text, void *conversation_state);

/**
 * Dequeue an instruction for processing
 * Blocks until instruction available or shutdown.
 *
 * @param queue Queue to dequeue from
 * @param instr Output parameter for instruction (caller must free instr->text)
 * @return 1 if instruction retrieved, 0 if shutdown, -1 on error
 */
int dequeue_instruction(AIInstructionQueue *queue, AIInstruction *instr);

/**
 * Shutdown AI instruction queue and wake blocked threads
 *
 * @param queue Queue to shutdown
 */
void ai_queue_shutdown(AIInstructionQueue *queue);

/**
 * Free AI instruction queue resources
 * Must be called after all threads have stopped using it
 *
 * @param queue Queue to free
 */
void ai_queue_free(AIInstructionQueue *queue);

/**
 * Get current queue depth (number of pending instructions)
 * Useful for UI status display
 *
 * @param queue Queue to query
 * @return Number of pending instructions, or -1 on error
 */
int ai_queue_depth(AIInstructionQueue *queue);

#endif /* MESSAGE_QUEUE_H */
