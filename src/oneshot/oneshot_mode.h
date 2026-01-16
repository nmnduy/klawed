#ifndef ONESHOT_MODE_H
#define ONESHOT_MODE_H

#include "../klawed_internal.h"

/**
 * Oneshot Mode
 * 
 * Main entry point for single-command (oneshot) mode execution.
 * Executes a single prompt, processes tool calls recursively, and exits.
 */

/**
 * Execute a single command and exit
 * Used when klawed is invoked with a command-line argument
 * 
 * @param state Conversation state (must be initialized)
 * @param prompt User prompt/command to execute
 * @return 0 on success, 1 on error
 */
int oneshot_execute(ConversationState *state, const char *prompt);

#endif // ONESHOT_MODE_H
