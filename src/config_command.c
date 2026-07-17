/*
 * config_command.c - Configuration Command
 *
 * Provides a /config command to view and modify configuration settings.
 */

#include "config_command.h"
#include "commands.h"
#include "compaction.h"
#include "config.h"
#include "logger.h"
#include "provider.h"
#include "tool_utils.h"
#include "tui.h"
#include "ui/ui_output.h"
#include "util/string_utils.h"
#include <bsd/string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void set_status_message(ConversationState *state, const char *message) {
    if (state && state->tui && message) {
        tui_update_status(state->tui, message);
    }
}

static void copy_status(char *status_out, size_t status_out_size, const char *message) {
    if (status_out && status_out_size > 0) {
        strlcpy(status_out, message ? message : "", status_out_size);
    }
}

static int parse_bool_value(const char *value, int *out) {
    if (!value || !out) {
        return -1;
    }

    if (strcmp(value, "1") == 0 ||
        strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0 ||
        strcasecmp(value, "enable") == 0 ||
        strcasecmp(value, "enabled") == 0) {
        *out = 1;
        return 0;
    }

    if (strcmp(value, "0") == 0 ||
        strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0 ||
        strcasecmp(value, "off") == 0 ||
        strcasecmp(value, "disable") == 0 ||
        strcasecmp(value, "disabled") == 0) {
        *out = 0;
        return 0;
    }

    return -1;
}

static void ensure_compaction_config(ConversationState *state, int enabled) {
    if (!state) {
        return;
    }

    if (!state->compaction_config) {
        state->compaction_config = malloc(sizeof(CompactionConfig));
        if (!state->compaction_config) {
            LOG_ERROR("[Config] Failed to allocate compaction config");
            return;
        }
        compaction_init_config(state->compaction_config, enabled, state->model);
    }

    state->compaction_config->enabled = enabled;
}

int switch_provider_for_session(ConversationState *state, const char *provider_key) {
    if (!state || !provider_key) {
        LOG_ERROR("[Config] Invalid arguments for switch_provider_for_session");
        return -1;
    }

    KlawedConfig config;
    if (config_load(&config) != 0) {
        config_init_defaults(&config);
    }

    const NamedProviderConfig *named_provider = config_find_provider(&config, provider_key);
    if (!named_provider) {
        LOG_ERROR("[Config] Provider '%s' not found in configuration", provider_key);
        return -1;
    }

    const LLMProviderConfig *provider_config = &named_provider->config;

    if (state->provider) {
        state->provider->cleanup(state->provider);
        state->provider = NULL;
    }

    free(state->api_url);
    state->api_url = NULL;

    free(state->model);
    state->model = strdup(provider_config->model[0] != '\0' ? provider_config->model : "gpt-4");
    if (!state->model) {
        LOG_ERROR("[Config] Failed to allocate memory for model");
        return -1;
    }

    ProviderInitResult provider_result;
    provider_init(state->model, state->api_key, &provider_result);

    if (!provider_result.provider) {
        LOG_ERROR("[Config] Failed to initialize provider '%s': %s",
                  provider_key,
                  provider_result.error_message ? provider_result.error_message : "unknown error");
        free(provider_result.error_message);
        free(provider_result.api_url);
        return -1;
    }

    state->provider = provider_result.provider;
    state->api_url = provider_result.api_url;
    free(provider_result.error_message);
    return 0;
}

static int save_and_report(KlawedConfig *config,
                           ConversationState *state,
                           const char *message,
                           char *status_out,
                           size_t status_out_size) {
    if (config_save(config) != 0) {
        set_status_message(state, "Failed to save configuration");
        copy_status(status_out, status_out_size, "Failed to save configuration");
        return -1;
    }

    set_status_message(state, message);
    copy_status(status_out, status_out_size, message);
    return 0;
}

int config_apply_setting(ConversationState *state,
                         const char *setting,
                         const char *value,
                         char *status_out,
                         size_t status_out_size) {
    if (!state || !setting || !value) {
        copy_status(status_out, status_out_size, "Invalid configuration request");
        return -1;
    }

    KlawedConfig config;
    if (config_load(&config) != 0) {
        config_init_defaults(&config);
    }

    if (strcmp(setting, "llm_provider") == 0 || strcmp(setting, "provider") == 0) {
        const NamedProviderConfig *provider = config_find_provider(&config, value);
        if (!provider) {
            copy_status(status_out, status_out_size, "Provider not found");
            set_status_message(state, "Provider not found");
            return -1;
        }

        strlcpy(config.active_provider, value, sizeof(config.active_provider));
        if (config_save(&config) != 0) {
            copy_status(status_out, status_out_size, "Failed to save provider configuration");
            set_status_message(state, "Failed to save provider configuration");
            return -1;
        }

        if (switch_provider_for_session(state, value) == 0) {
            char message[256];
            snprintf(message, sizeof(message), "Provider: %s", value);
            set_status_message(state, message);
            copy_status(status_out, status_out_size, message);
            return 0;
        }

        copy_status(status_out, status_out_size, "Saved provider, but session switch failed");
        set_status_message(state, "Saved provider, but session switch failed");
        return -1;
    }

    if (strcmp(setting, "streaming") == 0) {
        int enabled = 0;
        if (parse_bool_value(value, &enabled) != 0) {
            copy_status(status_out, status_out_size, "Streaming expects on/off");
            return -1;
        }

        config.streaming_enabled = enabled;
        state->streaming_enabled = enabled;
        return save_and_report(&config,
                               state,
                               enabled ? "Streaming enabled" : "Streaming disabled",
                               status_out,
                               status_out_size);
    }

    if (strcmp(setting, "auto_compact") == 0 || strcmp(setting, "autocompact") == 0) {
        int enabled = 0;
        if (parse_bool_value(value, &enabled) != 0) {
            copy_status(status_out, status_out_size, "Auto-compaction expects on/off");
            return -1;
        }

        config.auto_compact_enabled = enabled;
        ensure_compaction_config(state, enabled);
        if (!state->compaction_config) {
            copy_status(status_out, status_out_size, "Failed to allocate compaction config");
            return -1;
        }
        state->compaction_config->threshold_percent = config.compaction_threshold_percent;

        return save_and_report(&config,
                               state,
                               enabled ? "Auto-compaction enabled" : "Auto-compaction disabled",
                               status_out,
                               status_out_size);
    }

    if (strcmp(setting, "compaction_threshold") == 0 || strcmp(setting, "compaction_threshold_percent") == 0) {
        int threshold = atoi(value);
        if (threshold < 1 || threshold > 100) {
            copy_status(status_out, status_out_size, "Compaction threshold must be 1-100");
            return -1;
        }

        config.compaction_threshold_percent = threshold;
        ensure_compaction_config(state, config.auto_compact_enabled);
        if (state->compaction_config) {
            state->compaction_config->threshold_percent = threshold;
        }

        char message[256];
        snprintf(message, sizeof(message), "Compaction threshold: %d%%", threshold);
        return save_and_report(&config, state, message, status_out, status_out_size);
    }

    if (strcmp(setting, "thinking_style") == 0) {
        TUIThinkingStyle style = config_thinking_style_from_string(value);
        if (strcmp(value, "wave") != 0 && strcmp(value, "pacman") != 0) {
            copy_status(status_out, status_out_size, "Thinking style must be wave or pacman");
            return -1;
        }

        config.thinking_style = style;
        if (state->tui) {
            state->tui->thinking_style = style;
        }

        char message[256];
        snprintf(message, sizeof(message), "Thinking style: %s", config_thinking_style_to_string(style));
        return save_and_report(&config, state, message, status_out, status_out_size);
    }

    if (strcmp(setting, "disabled_tools") == 0) {
        strlcpy(config.disabled_tools, value, sizeof(config.disabled_tools));

        free(state->disabled_tools);
        state->disabled_tools = NULL;
        if (value[0] != '\0') {
            state->disabled_tools = strdup(value);
            if (!state->disabled_tools) {
                copy_status(status_out, status_out_size, "Failed to save disabled tools");
                return -1;
            }
        }
        set_runtime_disabled_tools(value);

        return save_and_report(&config,
                               state,
                               value[0] != '\0' ? "Disabled tools updated" : "Disabled tools cleared",
                               status_out,
                               status_out_size);
    }

    copy_status(status_out, status_out_size, "Unknown setting");
    return -1;
}

static void print_config_help(void) {
    printf("Usage: /config <setting> <value>\n");
    printf("Available settings:\n");
    printf("  llm_provider <name>\n");
    printf("  auto_compact <on|off>\n");
    printf("  compaction_threshold <1-100>\n");
    printf("  streaming <on|off>\n");
    printf("  thinking_style <wave|pacman>\n");
    printf("  disabled_tools <comma-separated tool names>\n");
}

int cmd_config(ConversationState *state, const char *args) {
    if (!state) {
        fprintf(stderr, "Error: No conversation state available\n");
        return -1;
    }

    while (*args == ' ' || *args == '\t') {
        args++;
    }

    if (*args == '\0') {
        print_config_help();
        return 0;
    }

    char setting[64];
    char value[CONFIG_DISABLED_TOOLS_MAX];
    int parsed = sscanf(args, "%63s %511[^\n]", setting, value);
    if (parsed < 2) {
        print_config_help();
        return -1;
    }

    char status[256];
    if (config_apply_setting(state, setting, value, status, sizeof(status)) != 0) {
        fprintf(stderr, "Error: %s\n", status[0] != '\0' ? status : "configuration update failed");
        return -1;
    }

    printf("%s\n", status);
    return 0;
}
