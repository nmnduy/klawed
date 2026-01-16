#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include "../klawed_internal.h"
#include "../tui.h"
#include "../message_queue.h"
#include "../ai_worker.h"

/**
 * Context structure for interactive callbacks
 */
typedef struct {
    ConversationState *state;
    TUIState *tui;
    AIWorkerContext *worker;
    AIInstructionQueue *instruction_queue;
    TUIMessageQueue *tui_queue;
    int instruction_queue_capacity;
} InteractiveContext;

/**
 * Submit input callback invoked by the TUI event loop when user presses Enter
 *
 * Handles:
 * - Vim-style commands (starting with ':')
 * - Slash commands (starting with '/')
 * - Regular user input (sent to API)
 *
 * @param input         User input string
 * @param user_data     InteractiveContext pointer
 * @return              1 to exit, 0 to continue, -1 to not clear input buffer
 */
int submit_input_callback(const char *input, void *user_data);

/**
 * Interrupt callback invoked by the TUI event loop when user presses Ctrl+C
 *
 * Sets the interrupt flag to cancel ongoing operations.
 * Never exits the application - use Ctrl+D or :q/:quit to exit.
 *
 * @param user_data     InteractiveContext pointer
 * @return              Always returns 0 (never exit on Ctrl+C)
 */
int interrupt_callback(void *user_data);

#endif // INPUT_HANDLER_H
