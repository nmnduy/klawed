#ifndef INTERACTIVE_LOOP_H
#define INTERACTIVE_LOOP_H

#include "../klawed_internal.h"

/**
 * Main interactive mode entry point
 * 
 * Initializes TUI, message queues, and AI worker thread.
 * Enters the TUI event loop to handle user input interactively.
 * Cleans up all resources before exit.
 * 
 * @param state     Conversation state
 */
void interactive_mode(ConversationState *state);

#endif // INTERACTIVE_LOOP_H
