/*
 * ai_worker.h - Background worker for asynchronous API processing
 *
 * Provides an abstraction for a dedicated worker thread that consumes
 * AI instructions, invokes a caller-provided handler, and posts updates
 * back to the TUI message queue.
 */

#ifndef AI_WORKER_H
#define AI_WORKER_H

#include <pthread.h>
#include "message_queue.h"
#include "claude_internal.h"

struct AIWorkerContext;

typedef struct AIWorkerContext AIWorkerContext;

/**
 * Callback invoked on the worker thread for each instruction.
 * The implementation is responsible for processing the instruction
 * and posting any UI updates via ctx->tui_queue.
 */
typedef void (*AIWorkerHandler)(AIWorkerContext *ctx, const AIInstruction *instruction);

struct AIWorkerContext {
    pthread_t thread;                   /* Worker thread handle */
    AIInstructionQueue *instruction_queue;
    TUIMessageQueue *tui_queue;
    ConversationState *state;
    volatile int running;
    int thread_started;
    AIWorkerHandler handler;
};

/**
 * Information about a completed tool execution.
 * Used to stream progress updates back to the TUI.
 */
typedef struct {
    const char *tool_name;      /* Tool identifier (not owned) */
    const cJSON *result;        /* Tool result payload (not owned) */
    int is_error;               /* Non-zero if tool completed with error */
    int completed;              /* Number of tools finished so far */
    int total;                  /* Total number of tools launched */
} ToolCompletion;

/**
 * Start the worker thread.
 *
 * @param ctx           Context structure to initialize
 * @param state         Shared conversation state pointer
 * @param instruction_queue Queue containing pending instructions
 * @param tui_queue     Queue for posting UI updates
 * @param handler       Callback to execute for each instruction
 * @return 0 on success, -1 on failure
 */
int ai_worker_start(AIWorkerContext *ctx,
                    ConversationState *state,
                    AIInstructionQueue *instruction_queue,
                    TUIMessageQueue *tui_queue,
                    AIWorkerHandler handler);

/**
 * Stop the worker thread and wait for it to finish.
 * Safe to call multiple times.
 */
void ai_worker_stop(AIWorkerContext *ctx);

/**
 * Submit a new instruction to the worker.
 *
 * @param ctx   Worker context
 * @param text  Instruction text (will be copied by the queue)
 * @return 0 on success, -1 on error
 */
int ai_worker_submit(AIWorkerContext *ctx, const char *text);

/**
 * Post a status update for a completed tool.
 *
 * @param ctx          Worker context (must own a valid TUI queue)
 * @param completion   Completion details (fields are not copied)
 */
void ai_worker_handle_tool_completion(AIWorkerContext *ctx, const ToolCompletion *completion);

#endif /* AI_WORKER_H */
