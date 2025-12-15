/*
 * ai_worker.c - Background worker for asynchronous API requests
 */

#include "ai_worker.h"
#include "logger.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static void* ai_worker_thread_main(void *arg) {
    AIWorkerContext *ctx = (AIWorkerContext *)arg;
    if (!ctx) {
        return NULL;
    }

    // Enable thread cancellation
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    while (ctx->running) {
        AIInstruction instruction = {0};
        int rc = dequeue_instruction(ctx->instruction_queue, &instruction);
        if (rc == 0) {
            /* Queue shutdown */
            break;
        }
        if (rc < 0) {
            LOG_ERROR("AI worker failed to dequeue instruction");
            continue;
        }

        if (!ctx->running) {
            free(instruction.text);
            break;
        }

        if (ctx->handler) {
            ctx->handler(ctx, &instruction);
        }

        free(instruction.text);
    }

    return NULL;
}

int ai_worker_start(AIWorkerContext *ctx,
                    ConversationState *state,
                    AIInstructionQueue *instruction_queue,
                    TUIMessageQueue *tui_queue,
                    AIWorkerHandler handler) {
    if (!ctx || !instruction_queue || !tui_queue || !state || !handler) {
        return -1;
    }

    ctx->instruction_queue = instruction_queue;
    ctx->tui_queue = tui_queue;
    ctx->state = state;
    ctx->handler = handler;
    ctx->running = 1;
    ctx->thread_started = 0;

    int rc = pthread_create(&ctx->thread, NULL, ai_worker_thread_main, ctx);
    if (rc != 0) {
        LOG_ERROR("Failed to create AI worker thread (rc=%d)", rc);
        ctx->running = 0;
        return -1;
    }

    ctx->thread_started = 1;
    return 0;
}

void ai_worker_stop(AIWorkerContext *ctx) {
    if (!ctx) {
        return;
    }

    if (!ctx->thread_started) {
        return;
    }

    ctx->running = 0;

    // Set interrupt flag to signal any ongoing API calls
    if (ctx->state) {
        ctx->state->interrupt_requested = 1;
    }

    ai_queue_shutdown(ctx->instruction_queue);

    // Give the thread a brief moment to exit gracefully
    struct timespec sleep_time = {0, 100000000};  // 100ms
    nanosleep(&sleep_time, NULL);

    // Cancel the thread forcefully (in case it's stuck in a blocking operation)
    // This is safe because we've already set the interrupt flag and shutdown the queue
    LOG_INFO("Cancelling worker thread");
    pthread_cancel(ctx->thread);

    // Wait for the thread to finish cleanup
    pthread_join(ctx->thread, NULL);

    ctx->thread_started = 0;
}

int ai_worker_submit(AIWorkerContext *ctx, const char *text) {
    if (!ctx || !text || !ctx->instruction_queue) {
        return -1;
    }
    return enqueue_instruction(ctx->instruction_queue, text, ctx->state);
}

void ai_worker_handle_tool_completion(AIWorkerContext *ctx, const ToolCompletion *completion) {
    if (!ctx || !completion) {
        return;
    }
    if (!ctx->tui_queue) {
        return;
    }

    const char *tool_name = completion->tool_name ? completion->tool_name : "tool";
    const char *status_word = completion->is_error ? "failed" : "completed";

    char status[256];
    if (completion->total > 0) {
        snprintf(status, sizeof(status),
                 "Tool %s %s (%d/%d)",
                 tool_name,
                 status_word,
                 completion->completed,
                 completion->total);
    } else {
        snprintf(status, sizeof(status),
                 "Tool %s %s",
                 tool_name,
                 status_word);
    }

    post_tui_message(ctx->tui_queue, TUI_MSG_STATUS, status);
}
