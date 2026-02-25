/*
 * config_command.h - Configuration Command Interface
 *
 * Provides a /config command to view and modify configuration settings.
 */

#ifndef CONFIG_COMMAND_H
#define CONFIG_COMMAND_H

#include "klawed_internal.h"

/**
 * Handle /config command
 *
 * @param state Conversation state
 * @param args   Command arguments
 * @return 0 on success, -1 on error
 */
int cmd_config(ConversationState *state, const char *args);

#endif /* CONFIG_COMMAND_H */
