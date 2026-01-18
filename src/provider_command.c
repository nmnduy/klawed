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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

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

    // Trim leading whitespace from args
    while (*args == ' ' || *args == '\t') args++;

    // If no arguments, show current provider and list available providers
    if (strlen(args) == 0) {
        // Show current provider
        const char *current_provider_name = "default (from environment variables)";
        const LLMProviderConfig *current_provider_config = NULL;
        
        // Check if we have an active provider from config
        if (config.active_provider[0] != '\0') {
            const NamedProviderConfig *named_provider = config_find_provider(&config, config.active_provider);
            if (named_provider) {
                current_provider_name = config.active_provider;
                current_provider_config = &named_provider->config;
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
            snprintf(error_msg, sizeof(error_msg), "Provider '%s' not found", args);
            tui_update_status(state->tui, error_msg);
        }
        fprintf(stderr, "Error: Provider '%s' not found\n", args);
        printf("Use /provider list to see available providers\n");
        return -1;
    }
    
    // Update active provider in config
    strlcpy(config.active_provider, args, sizeof(config.active_provider));
    
    // Save the configuration
    if (config_save(&config) != 0) {
        if (state->tui) {
            tui_update_status(state->tui, "Failed to save provider configuration");
        }
        fprintf(stderr, "Error: Failed to save provider configuration\n");
        return -1;
    }
    
    // Show success message
    if (state->tui) {
        char success_msg[256];
        snprintf(success_msg, sizeof(success_msg), "Switched to provider '%s'", args);
        tui_update_status(state->tui, success_msg);
    }
    
    printf("Switched to provider '%s'\n", args);
    printf("  Type: %s\n", config_provider_type_to_string(provider->config.provider_type));
    if (provider->config.model[0] != '\0') {
        printf("  Model: %s\n", provider->config.model);
    }
    printf("Configuration saved to .klawed/config.json\n");
    printf("Note: Environment variable KLAWED_LLM_PROVIDER overrides this setting\n");
    
    return 0;
}