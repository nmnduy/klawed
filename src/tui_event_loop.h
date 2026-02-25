/*
 * TUI Event Processing
 *
 * Main event loop and input processing:
 * - Event loop with callbacks
 * - Input character dispatch
 * - External input handling
 * - Message queue processing
 * - Keyboard input polling
 */

#ifndef TUI_EVENT_LOOP_H
#define TUI_EVENT_LOOP_H

// Forward declarations
typedef struct TUIStateStruct TUIState;

// Submit callback signature
// Called when user submits input (Enter in INSERT mode)
// Returns 0 to continue loop, non-zero to exit
typedef int (*TUISubmitCallback)(const char *input, void *user_data);

// Interrupt callback signature
// Called when user presses Ctrl+C
// Returns 0 to continue loop, non-zero to exit
typedef int (*TUIInterruptCallback)(void *user_data);

// Keypress callback signature
// Called on each keypress for custom handling
// Returns 0 to continue loop, non-zero to exit
typedef int (*TUIKeypressCallback)(void *user_data);

// External input callback signature
// Called to check for external input sources (e.g., ZMQ socket)
// buffer: output buffer for external input
// buffer_size: size of output buffer
// Returns 1 if input received, 0 otherwise
typedef int (*TUIExternalInputCallback)(void *user_data, char *buffer, int buffer_size);

// Main TUI event loop
// Processes input events and dispatches to appropriate handlers
// Returns:
//   0 = normal exit
//   1 = exit request (Ctrl+D or :q command)
//   -1 = error
int tui_event_loop_run(TUIState *tui, const char *prompt,
                       TUISubmitCallback submit_callback,
                       TUIInterruptCallback interrupt_callback,
                       TUIKeypressCallback keypress_callback,
                       TUIExternalInputCallback external_input_callback,
                       void *user_data,
                       void *msg_queue_ptr);

// Process a single input character
// Returns:
//   0 = character processed
//   1 = submit input
//   2 = interrupt
//   3 = exit
//   -1 = error
int tui_event_loop_process_char(TUIState *tui, int ch, const char *prompt, void *user_data);

// Poll for input (non-blocking)
// Returns character code if input available, -1 otherwise
int tui_event_loop_poll_input(TUIState *tui);

// Process pending messages from message queue
// Returns number of messages processed
int tui_event_loop_process_messages(TUIState *tui,
                                   const char *prompt,
                                   void *msg_queue_ptr,
                                   int max_messages);

// Drain any remaining messages from queue (called after loop exits)
void tui_event_loop_drain_messages(TUIState *tui, const char *prompt, void *msg_queue_ptr);

// Dispatch a single TUI message
// Internal helper for message processing
void tui_event_loop_dispatch_message(TUIState *tui, void *msg);

#endif // TUI_EVENT_LOOP_H
