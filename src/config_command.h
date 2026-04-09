/*
 * config_command.h - Configuration Command Interface
 *
 * Provides a /config command to view and modify configuration settings.
 */

#ifndef CONFIG_COMMAND_H
#define CONFIG_COMMAND_H

#include "klawed_internal.h"
#include <stddef.h>

/**
 * Handle /config command
 *
 * @param state Conversation state
 * @param args   Command arguments
 * @return 0 on success, -1 on error
 */
int cmd_config(ConversationState *state, const char *args);

int switch_provider_for_session(ConversationState *state, const char *provider_key);

int config_apply_setting(ConversationState *state,
                         const char *setting,
                         const char *value,
                         char *status_out,
                         size_t status_out_size);

#endif /* CONFIG_COMMAND_H */
