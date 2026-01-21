/*
 * provider_command.h - Provider Configuration Command
 *
 * Provides a /provider command to view and switch between configured LLM providers.
 */

#ifndef PROVIDER_COMMAND_H
#define PROVIDER_COMMAND_H

#include "klawed_internal.h"
#include "ncurses_input.h"

/**
 * Handle /provider command
 *
 * @param state  Conversation state
 * @param args   Command arguments (provider name or "list")
 * @return       0 on success, -1 on error
 */
int cmd_provider(ConversationState *state, const char *args);

/**
 * Tab completion for /provider command arguments
 *
 * @param line        Full input line
 * @param cursor_pos  Cursor position in line
 * @param ctx         Context (unused)
 * @return            CompletionResult* or NULL
 */
CompletionResult* provider_completer(const char *line, int cursor_pos, void *ctx);

#endif // PROVIDER_COMMAND_H