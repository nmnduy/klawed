/*
 * provider_command.c - Provider Configuration Command
 *
 * Provides a /provider command to view and switch between configured LLM providers.
 */

#include "provider_command.h"
#include "commands.h"
#include "config.h"
#include "logger.h"
#include "tui.h"
#include "ui/ui_output.h"
#include "conversation/conversation_state.h"
#include "util/string_utils.h"
#include "provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <bsd/stdlib.h>

/**
 * Switch provider for the current session
 *
 * @param state Conversation state
 * @param provider_key Provider key to switch to
 * @return 0 on success, -1 on error
 */
static int switch_provider_for_session(ConversationState *state, const char *provider_key) {
    if (!state || !provider_key) {
        LOG_ERROR("[Provider] Invalid arguments for switch_provider_for_session");
        return -1;
    }

    LOG_INFO("[Provider] Switching to provider '%s' for current session", provider_key);

    // Load configuration
    KlawedConfig config;
    if (config_load(&config) != 0) {
        config_init_defaults(&config);
    }

    // Find the provider configuration
    const NamedProviderConfig *named_provider = config_find_provider(&config, provider_key);
    if (!named_provider) {
        LOG_ERROR("[Provider] Provider '%s' not found in configuration", provider_key);
        return -1;
    }

    const LLMProviderConfig *provider_config = &named_provider->config;

    // Clean up old provider if it exists
    if (state->provider) {
        state->provider->cleanup(state->provider);
        state->provider = NULL;
        LOG_DEBUG("[Provider] Old provider cleaned up");
    }

    // Free old API URL if it exists
    if (state->api_url) {
        free(state->api_url);
        state->api_url = NULL;
    }

    // Update model from provider config
    if (state->model) {
        free(state->model);
    }
    state->model = strdup(provider_config->model[0] != '\0' ? provider_config->model : "gpt-4");
    if (!state->model) {
        LOG_ERROR("[Provider] Failed to allocate memory for model");
        return -1;
    }

    // Initialize new provider directly from config (bypasses env var priority)
    ProviderInitResult provider_result;
    provider_init_from_config(provider_key, provider_config, &provider_result);

    if (!provider_result.provider) {
        const char *error_msg = provider_result.error_message ? provider_result.error_message : "unknown error";
        LOG_ERROR("[Provider] Failed to initialize provider '%s': %s", provider_key, error_msg);
        free(provider_result.error_message);
        free(provider_result.api_url);
        return -1;
    }

    // Update state with new provider and API URL
    state->provider = provider_result.provider;
    state->api_url = provider_result.api_url;
    free(provider_result.error_message);

    LOG_INFO("[Provider] Successfully switched to provider '%s' (model: %s, API URL: %s)",
             provider_key, state->model, state->api_url ? state->api_url : "(null)");

    return 0;
}

/**
 * Tab completion for /provider command arguments
 */
CompletionResult* provider_completer(const char *line, int cursor_pos, void *ctx) {
    (void)ctx;  // Unused

    if (!line || line[0] != '/') {
        return NULL;
    }

    // Find the space after the command name
    const char *space = strchr(line, ' ');
    if (!space) {
        return NULL;  // No space yet, nothing to complete
    }

    // Extract the argument prefix (from space to cursor)
    int arg_start = (int)(space - line) + 1;
    if (cursor_pos < arg_start) {
        return NULL;  // Cursor is before the argument
    }

    int prefix_len = cursor_pos - arg_start;
    if (prefix_len < 0) {
        prefix_len = 0;
    }

    char prefix[256];
    if (prefix_len >= (int)sizeof(prefix)) {
        prefix_len = sizeof(prefix) - 1;
    }
    if (prefix_len > 0) {
        memcpy(prefix, line + arg_start, (size_t)prefix_len);
    }
    prefix[prefix_len] = '\0';

    // Load configuration to get available providers
    KlawedConfig config;
    if (config_load(&config) != 0) {
        config_init_defaults(&config);
    }

    // Count matches
    int match_count = 0;

    // Always offer "list" as an option
    if (strncmp("list", prefix, (size_t)prefix_len) == 0) {
        match_count++;
    }

    // Check configured providers
    for (int i = 0; i < config.provider_count; i++) {
        const char *key = config.providers[i].key;
        if (strncmp(key, prefix, (size_t)prefix_len) == 0) {
            match_count++;
        }
    }

    if (match_count == 0) {
        return NULL;
    }

    // Allocate result structure
    CompletionResult *res = malloc(sizeof(CompletionResult));
    if (!res) {
        return NULL;
    }

    res->options = reallocarray(NULL, (size_t)match_count, sizeof(char*));
    if (!res->options) {
        free(res);
        return NULL;
    }

    res->count = 0;
    res->selected = 0;

    // Add "list" if it matches
    if (strncmp("list", prefix, (size_t)prefix_len) == 0) {
        res->options[res->count++] = strdup("list");
    }

    // Add matching provider names
    for (int i = 0; i < config.provider_count; i++) {
        const char *key = config.providers[i].key;
        if (strncmp(key, prefix, (size_t)prefix_len) == 0) {
            res->options[res->count++] = strdup(key);
        }
    }

    return res;
}

/**
 * Handle /provider command
 */
int cmd_provider(ConversationState *state, const char *args) {
    if (!state) {
        // In non-TUI mode, print to stderr
        fprintf(stderr, "Error: No conversation state available\n");
        return -1;
    }

    // Load configuration
    KlawedConfig config;
    if (config_load(&config) != 0) {
        config_init_defaults(&config);
    }

    LOG_DEBUG("[Provider Command] cmd_provider called with args='%s', active_provider='%s', provider_count=%d",
              args, config.active_provider[0] ? config.active_provider : "(not set)", config.provider_count);

    // Trim leading whitespace from args
    while (*args == ' ' || *args == '\t') args++;

    // If no arguments, show current provider and list available providers
    if (strlen(args) == 0) {
        // Show current provider
        const char *current_provider_name = "default (from environment variables)";
        const LLMProviderConfig *current_provider_config = NULL;

        // Check if we have an active provider from config
        if (config.active_provider[0] != '\0') {
            LOG_DEBUG("[Provider Command] Found active_provider in config: '%s'", config.active_provider);
            const NamedProviderConfig *named_provider = config_find_provider(&config, config.active_provider);
            if (named_provider) {
                current_provider_name = config.active_provider;
                current_provider_config = &named_provider->config;
            } else {
                LOG_WARN("[Provider Command] active_provider '%s' not found in providers", config.active_provider);
            }
        } else if (config.llm_provider.model[0] != '\0') {
            // Check legacy llm_provider configuration
            current_provider_name = "legacy configuration";
            current_provider_config = &config.llm_provider;
        }

        // Get environment variable for provider selection
        const char *env_provider = getenv("KLAWED_LLM_PROVIDER");
        if (env_provider && env_provider[0] != '\0') {
            current_provider_name = env_provider;
            const NamedProviderConfig *named_provider = config_find_provider(&config, env_provider);
            if (named_provider) {
                current_provider_config = &named_provider->config;
            }
        }

        // Display current provider info
        if (state->tui) {
            // In TUI mode, update status line
            char status_msg[256];
            snprintf(status_msg, sizeof(status_msg), "Current provider: %s", current_provider_name);
            tui_update_status(state->tui, status_msg);
        }

        // Always print to stdout (in TUI mode, this goes to log)
        printf("Current LLM Provider: %s\n", current_provider_name);
        if (current_provider_config) {
            printf("  Type: %s\n", config_provider_type_to_string(current_provider_config->provider_type));
            if (current_provider_config->model[0] != '\0') {
                printf("  Model: %s\n", current_provider_config->model);
            }
            if (current_provider_config->api_base[0] != '\0') {
                printf("  API Base: %s\n", current_provider_config->api_base);
            }
        }

        printf("\nAvailable providers:\n");
        if (config.provider_count > 0) {
            for (int i = 0; i < config.provider_count; i++) {
                const NamedProviderConfig *provider = &config.providers[i];
                printf("  %s - %s (%s)\n",
                       provider->key,
                       provider->config.provider_name[0] != '\0' ? provider->config.provider_name : "unnamed",
                       provider->config.model[0] != '\0' ? provider->config.model : "no model");
            }
        } else if (config.llm_provider.model[0] != '\0') {
            printf("  Legacy provider configuration available\n");
        } else {
            printf("  No provider configurations found\n");
        }

        printf("\nUsage: /provider <name> to switch provider\n");
        printf("       /provider list to show available providers\n");

        return 0;
    }

    // Handle "list" argument
    if (strcmp(args, "list") == 0) {
        if (state->tui) {
            tui_update_status(state->tui, "Provider list shown");
        }

        printf("Available LLM Providers:\n");
        if (config.provider_count > 0) {
            for (int i = 0; i < config.provider_count; i++) {
                const NamedProviderConfig *provider = &config.providers[i];
                printf("  %s - %s (%s)\n",
                       provider->key,
                       provider->config.provider_name[0] != '\0' ? provider->config.provider_name : "unnamed",
                       provider->config.model[0] != '\0' ? provider->config.model : "no model");
            }
        } else {
            printf("  No provider configurations found\n");
            printf("  Configure providers in .klawed/config.json\n");
        }
        return 0;
    }

    // Handle provider switching
    // Check if the provider exists
    const NamedProviderConfig *provider = config_find_provider(&config, args);
    if (!provider) {
        if (state->tui) {
            char error_msg[256];
            // Truncate args if too long for error message
            char truncated_args[64];
            strlcpy(truncated_args, args, sizeof(truncated_args));
            snprintf(error_msg, sizeof(error_msg), "Provider '%s' not found", truncated_args);
            tui_update_status(state->tui, error_msg);
        }
        fprintf(stderr, "Error: Provider '%s' not found\n", args);
        printf("Use /provider list to see available providers\n");
        return -1;
    }

    // Update active provider in config
    LOG_INFO("[Provider Command] Setting active_provider to '%s'", args);
    strlcpy(config.active_provider, args, sizeof(config.active_provider));
    LOG_DEBUG("[Provider Command] active_provider is now '%s'", config.active_provider);

    // Save the configuration
    if (config_save(&config) != 0) {
        if (state->tui) {
            tui_update_status(state->tui, "Failed to save provider configuration");
        }
        fprintf(stderr, "Error: Failed to save provider configuration\n");
        LOG_ERROR("[Provider Command] Failed to save config with active_provider='%s'", args);
        return -1;
    }

    LOG_INFO("[Provider Command] Configuration saved with active_provider='%s'", config.active_provider);

    // Also switch provider for the current session
    int session_switch_result = switch_provider_for_session(state, args);

    // Show success message
    if (state->tui) {
        char success_msg[256];
        // Truncate args if too long for success message
        char truncated_args[64];
        strlcpy(truncated_args, args, sizeof(truncated_args));
        if (session_switch_result == 0) {
            snprintf(success_msg, sizeof(success_msg), "Switched to provider '%s' (current session)", truncated_args);

            // Display new model in mascot-style banner to show the change visually
            char model_line[256];
            snprintf(model_line, sizeof(model_line), " ( o.o )  %s", state->model ? state->model : "unknown");
            tui_add_conversation_line(state->tui, NULL, "", COLOR_PAIR_FOREGROUND);
            tui_add_conversation_line(state->tui, "[System]", "Provider switched:", COLOR_PAIR_STATUS);
            tui_add_conversation_line(state->tui, NULL, model_line, COLOR_PAIR_ASSISTANT);
            tui_add_conversation_line(state->tui, NULL, "", COLOR_PAIR_FOREGROUND);
        } else {
            snprintf(success_msg, sizeof(success_msg), "Switched to provider '%s' (config only)", truncated_args);
        }
        tui_update_status(state->tui, success_msg);
    }

    printf("Switched to provider '%s'\n", args);
    printf("  Type: %s\n", config_provider_type_to_string(provider->config.provider_type));
    if (provider->config.model[0] != '\0') {
        printf("  Model: %s\n", provider->config.model);
    }
    printf("Configuration saved to .klawed/config.json\n");
    if (session_switch_result == 0) {
        printf("Provider switched for current session\n");
    } else {
        printf("Note: Provider configuration saved but could not switch for current session\n");
    }
    printf("Note: KLAWED_LLM_PROVIDER env var will override this at next startup\n");

    return 0;
}
