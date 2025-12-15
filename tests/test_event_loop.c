/**
 * test_event_loop.c - Test non-blocking TUI event loop
 *
 * This test demonstrates that the TUI remains responsive even during
 * long-running operations. It simulates an AI processing delay.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "../src/tui.h"
#include "../src/message_queue.h"

typedef struct {
    TUIMessageQueue *queue;
    int running;
    pthread_t thread;
} SimulatedWorker;

// Simulated worker thread that processes "AI requests"
static void* worker_thread(void *arg) {
    SimulatedWorker *worker = (SimulatedWorker *)arg;

    while (worker->running) {
        usleep(100000);  // Check every 100ms

        // Simulate periodic status updates
        static int counter = 0;
        counter++;

        if (counter % 10 == 0) {  // Every second
            char status[128];
            snprintf(status, sizeof(status), "Worker alive (count: %d)", counter / 10);
            post_tui_message(worker->queue, TUI_MSG_STATUS, status);
        }
    }

    return NULL;
}

// Callback when user submits input
static int on_input_submit(const char *input, void *user_data) {
    SimulatedWorker *worker = (SimulatedWorker *)user_data;

    // Check for exit command
    if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
        post_tui_message(worker->queue, TUI_MSG_STATUS, "");
        post_tui_message(worker->queue, TUI_MSG_ADD_LINE, "[System] Goodbye!");
        return 1;  // Exit event loop
    }

    // Echo user input
    char msg[1024];
    snprintf(msg, sizeof(msg), "[User] %s", input);
    post_tui_message(worker->queue, TUI_MSG_ADD_LINE, msg);

    // Simulate AI "thinking"
    post_tui_message(worker->queue, TUI_MSG_STATUS, "AI thinking...");

    // Simulate processing delay (this happens in callback, TUI should stay responsive)
    usleep(500000);  // 500ms delay

    // Send response
    snprintf(msg, sizeof(msg), "[Assistant] You said: \"%s\"", input);
    post_tui_message(worker->queue, TUI_MSG_ADD_LINE, msg);

    // Clear status
    post_tui_message(worker->queue, TUI_MSG_STATUS, "");

    return 0;  // Continue event loop
}

int main(void) {
    // Initialize TUI
    TUIState tui = {0};
    if (tui_init(&tui) != 0) {
        fprintf(stderr, "Failed to initialize TUI\n");
        return 1;
    }

    // Add welcome message
    tui_add_conversation_line(&tui, "[System]", "Event Loop Test - Type 'quit' to exit", COLOR_PAIR_STATUS);
    tui_add_conversation_line(&tui, "[System]", "Try typing while AI is 'thinking' (500ms delay)", COLOR_PAIR_STATUS);

    // Initialize message queue
    TUIMessageQueue msg_queue = {0};
    if (tui_msg_queue_init(&msg_queue, 100) != 0) {
        fprintf(stderr, "Failed to initialize message queue\n");
        tui_cleanup(&tui);
        return 1;
    }

    // Start simulated worker thread
    SimulatedWorker worker = {
        .queue = &msg_queue,
        .running = 1
    };

    if (pthread_create(&worker.thread, NULL, worker_thread, &worker) != 0) {
        fprintf(stderr, "Failed to create worker thread\n");
        tui_msg_queue_shutdown(&msg_queue);
        tui_cleanup(&tui);
        return 1;
    }

    // Run event loop
    printf("\nStarting non-blocking event loop...\n");
    sleep(1);  // Give user time to see the message

    int result = tui_event_loop(&tui, "Input", on_input_submit, &worker, &msg_queue);

    // Cleanup
    worker.running = 0;
    pthread_join(worker.thread, NULL);

    tui_msg_queue_shutdown(&msg_queue);
    tui_cleanup(&tui);

    printf("Event loop exited with code: %d\n", result);
    return 0;
}
