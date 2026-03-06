#include "interactive_loop.h"
#include "input_handler.h"
#include "response_processor.h"
#include "../background_init.h"
#include "../logger.h"
#include "../ui/ui_output.h"
#include "../tui.h"
#include "../tui_conversation.h"
#include "../vltrn_banner.h"
#include "../message_queue.h"
#include "../ai_worker.h"
#include "../commands.h"
#include <stdio.h>

void interactive_mode(ConversationState *state) {
    const char *prompt = ">>> ";

    // Initialize TUI first for responsive startup
    TUIState tui = {0};
    if (tui_init(&tui, state) != 0) {
        LOG_ERROR("Failed to initialize TUI");
        return;
    }

    // Await background-loaded resources
    // Database is needed for token usage tracking and session persistence
    if (!state->persistence_db) {
        state->persistence_db = await_database_ready(state);
    }

    // Set up database connection for token usage queries
    tui.persistence_db = state->persistence_db;
    tui.session_id = state->session_id;

    // Link TUI to state for streaming support
    state->tui = &tui;
    /* tui_queue will be set after queue initialization below */

    // Initialize command system
    commands_init();

    // Enable TUI mode for commands
    commands_set_tui_mode(1);

    // Build initial status line
    char status_msg[256];
    snprintf(status_msg, sizeof(status_msg), "Enter `:help` for list | Ctrl+D to exit");
    tui_update_status(&tui, status_msg);

    // Show VLTRN greeting screen (only if VLTRN_MODE=1)
    vltrn_show_greeting(&tui);

    // Display startup banner with mascot in the TUI
    tui_show_startup_banner(&tui, VERSION, state->model, state->working_dir, state->session_id);

    // If resuming a session, populate the TUI with conversation history
    if (state->count > 1) {  // > 1 to account for system message
        tui_populate_from_conversation(&tui, state);
    }

    const size_t TUI_QUEUE_CAPACITY = 256;
    const size_t AI_QUEUE_CAPACITY = 16;
    TUIMessageQueue tui_queue;
    AIInstructionQueue instruction_queue;
    AIWorkerContext worker_ctx = {0};
    int tui_queue_initialized = 0;
    int instruction_queue_initialized = 0;
    int worker_started = 0;
    int async_enabled = 1;

    if (tui_msg_queue_init(&tui_queue, TUI_QUEUE_CAPACITY) != 0) {
        ui_show_error(&tui, NULL, "Failed to initialize TUI message queue; running in synchronous mode.");
        async_enabled = 0;
    } else {
        tui_queue_initialized = 1;
        /* Route streaming TUI updates through the queue for thread safety */
        state->tui_queue = &tui_queue;
    }

    if (async_enabled) {
        if (ai_queue_init(&instruction_queue, AI_QUEUE_CAPACITY) != 0) {
            ui_show_error(&tui, NULL, "Failed to initialize instruction queue; running in synchronous mode.");
            async_enabled = 0;
        } else {
            instruction_queue_initialized = 1;
        }
    }

    if (async_enabled) {
        if (ai_worker_start(&worker_ctx, state, &instruction_queue, &tui_queue, ai_worker_handle_instruction) != 0) {
            ui_show_error(&tui, NULL, "Failed to start AI worker thread; running in synchronous mode.");
            async_enabled = 0;
        } else {
            worker_started = 1;
        }
    }

    if (!async_enabled) {
        if (worker_started) {
            ai_worker_stop(&worker_ctx);
            worker_started = 0;
        }
        if (instruction_queue_initialized) {
            ai_queue_free(&instruction_queue);
            instruction_queue_initialized = 0;
        }
        if (tui_queue_initialized) {
            tui_msg_queue_shutdown(&tui_queue);
            tui_msg_queue_free(&tui_queue);
            tui_queue_initialized = 0;
            state->tui_queue = NULL;
        }
    }

    // Socket IPC removed - will be reimplemented with ZMQ

    InteractiveContext ctx = {
        .state = state,
        .tui = &tui,
        .worker = worker_started ? &worker_ctx : NULL,
        .instruction_queue = instruction_queue_initialized ? &instruction_queue : NULL,
        .tui_queue = tui_queue_initialized ? &tui_queue : NULL,
        .instruction_queue_capacity = instruction_queue_initialized ? (int)AI_QUEUE_CAPACITY : 0,
    };

    void *event_loop_queue = tui_queue_initialized ? (void *)&tui_queue : NULL;
    tui_event_loop(&tui, prompt, submit_input_callback, interrupt_callback, NULL,
                   NULL,  // No socket external input callback
                   &ctx, event_loop_queue);

    if (worker_started) {
        ai_worker_stop(&worker_ctx);
    }
    if (tui_queue_initialized) {
        tui_drain_message_queue(&tui, prompt, &tui_queue);
    }
    if (instruction_queue_initialized) {
        ai_queue_free(&instruction_queue);
    }
    if (tui_queue_initialized) {
        tui_msg_queue_shutdown(&tui_queue);
        tui_msg_queue_free(&tui_queue);
        state->tui_queue = NULL;
    }

    // Socket IPC removed - will be reimplemented with ZMQ

    // Disable TUI mode for commands before cleanup
    commands_set_tui_mode(0);

    // Cleanup TUI
    tui_cleanup(&tui);
    printf("Goodbye!\n");
}
