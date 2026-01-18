/*
 * config_command.c - Configuration Command
 *
 * Provides a /config command to view and modify configuration settings.
 * Supports: /config llm_provider <name> to switch LLM providers
 */

#include "config_command.h"
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

/**
 * Switch provider for the current session
 * 
 * @param state Conversation state
 * @param provider_key Provider key to switch to
 * @return 0 on success, -1 on error
 */
static int switch_provider_for_session(ConversationState *state, const char *provider_key) {
    if (!state || !provider_key) {
        LOG_ERROR("[Config] Invalid arguments for switch_provider_for_session");
        return -1;
    }
    
    LOG_INFO("[Config] Switching to provider '%s' for current session", provider_key);
    
    // Load configuration
    KlawedConfig config;
    if (config_load(&config) != 0) {
        config_init_defaults(&config);
    }
    
    // Find the provider configuration
    const NamedProviderConfig *named_provider = config_find_provider(&config, provider_key);
    if (!named_provider) {
        LOG_ERROR("[Config] Provider '%s' not found in configuration", provider_key);
        return -1;
    }
    
    const LLMProviderConfig *provider_config = &named_provider->config;
    
    // Clean up old provider if it exists
    if (state->provider) {
        state->provider->cleanup(state->provider);
        state->provider = NULL;
        LOG_DEBUG("[Config] Old provider cleaned up");
    }
    
    // Free old API URL if it exists
    if (state->api_url) {
        free(state->api_url);
        state->api_url = NULL;
    }
    
    // Update model
    if (state->model) {
        free(state->model);
    }
    state->model = strdup(provider_config->model[0] != '\0' ? provider_config->model : "gpt-4");
    if (!state->model) {
        LOG_ERROR("[Config] Failed to allocate memory for model");
        return -1;
    }
    
    // Initialize new provider
    ProviderInitResult provider_result;
    provider_init(state->model, state->api_key, &provider_result);
    
    if (!provider_result.provider) {
        const char *error_msg = provider_result.error_message ? provider_result.error_message : "unknown error";
        LOG_ERROR("[Config] Failed to initialize provider '%s': %s", provider_key, error_msg);
        free(provider_result.error_message);
        free(provider_result.api_url);
        return -1;
    }
    
    // Update state with new provider and API URL
    state->provider = provider_result.provider;
    state->api_url = provider_result.api_url;
    free(provider_result.error_message);
    
    LOG_INFO("[Config] Successfully switched to provider '%s' (model: %s, API URL: %s)",
             provider_key, state->model, state->api_url ? state->api_url : "(null)");
    
    return 0;
}

/**
 * Handle /config command
 */
int cmd_config(ConversationState *state, const char *args) {
    if (!state) {
        // In non-TUI mode, print to stderr
        fprintf(stderr, "Error: No conversation state available\n");
        return -1;
    }
    
    // Trim leading whitespace from args
    while (*args == ' ' || *args == '\t') args++;
    
    // If no arguments, show help
    if (strlen(args) == 0) {
        printf("Usage: /config <setting> <value>\n");
        printf("Available settings:\n");
        printf("  llm_provider <name> - Switch LLM provider (use /provider list to see available)\n");
        printf("\nSee also: /provider - View and switch LLM providers\n");
        return 0;
    }
    
    // Parse command: /config <setting> <value>
    char setting[64];
    char value[CONFIG_PROVIDER_KEY_MAX];  // Provider keys are limited to 64 chars
    
    // Try to parse setting and value
    int parsed = sscanf(args, "%63s %63[^\n]", setting, value);
    
    if (parsed < 1) {
        fprintf(stderr, "Error: Invalid syntax. Use: /config <setting> <value>\n");
        return -1;
    }
    
    // Handle different settings
    if (strcmp(setting, "llm_provider") == 0) {
        if (parsed < 2) {
            fprintf(stderr, "Error: Provider name required. Use: /config llm_provider <name>\n");
            printf("Use /provider list to see available providers\n");
            return -1;
        }
        
        // Load configuration
        KlawedConfig config;
        if (config_load(&config) != 0) {
            config_init_defaults(&config);
        }
        
        // Check if the provider exists
        const NamedProviderConfig *provider = config_find_provider(&config, value);
        if (!provider) {
            if (state->tui) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Provider '%s' not found", value);
                tui_update_status(state->tui, error_msg);
            }
            fprintf(stderr, "Error: Provider '%s' not found\n", value);
            printf("Use /provider list to see available providers\n");
            return -1;
        }
        
        // Update active provider in config
        strlcpy(config.active_provider, value, sizeof(config.active_provider));
        
        // Save the configuration
        if (config_save(&config) != 0) {
            if (state->tui) {
                tui_update_status(state->tui, "Failed to save provider configuration");
            }
            fprintf(stderr, "Error: Failed to save provider configuration\n");
            return -1;
        }
        
        // Switch provider for the current session
        int session_switch_result = switch_provider_for_session(state, value);
        
        // Show success message
        if (state->tui) {
            char success_msg[256];
            if (session_switch_result == 0) {
                snprintf(success_msg, sizeof(success_msg), "Switched to provider '%s'", value);
            } else {
                snprintf(success_msg, sizeof(success_msg), "Saved provider '%s' to config", value);
            }
            tui_update_status(state->tui, success_msg);
        }
        
        printf("Configuration updated:\n");
        printf("  llm_provider = %s\n", value);
        printf("  Type: %s\n", config_provider_type_to_string(provider->config.provider_type));
        if (provider->config.model[0] != '\0') {
            printf("  Model: %s\n", provider->config.model);
        }
        printf("Configuration saved to .klawed/config.json\n");
        if (session_switch_result == 0) {
            printf("Provider switched for current session\n");
        } else {
            printf("Note: Could not switch provider for current session\n");
        }
        
        return 0;
    } else {
        fprintf(stderr, "Error: Unknown setting '%s'\n", setting);
        printf("Available settings: llm_provider\n");
        return -1;
    }
}