/*
 * provider.c - API Provider initialization and selection
 */

#define _GNU_SOURCE         // For asprintf
#define _POSIX_C_SOURCE 200809L

#include "provider.h"
#include "openai_provider.h"
#include "bedrock_provider.h"
#include "anthropic_provider.h"
#include "deepseek_provider.h"
#include "moonshot_provider.h"
#include "zai_coding_provider.h"
#include "minimax_coding_provider.h"
#include "kimi_coding_plan_provider.h"
#include "openai_sub_provider.h"
#include "anthropic_sub_provider.h"
#include "openai_responses_provider.h"
#include "config.h"
#include "logger.h"
#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

// Default Anthropic API URL
#define DEFAULT_ANTHROPIC_URL "https://api.anthropic.com/v1/messages"

/**
 * Duplicate a string using arena allocation
 */
static char* arena_strdup(Arena *arena, const char *str) {
    if (!str || !arena) return NULL;
    size_t len = strlen(str) + 1;
    char *new_str = arena_alloc(arena, len);
    if (!new_str) return NULL;
    strlcpy(new_str, str, len);
    return new_str;
}

/**
 * Create a redacted version of an API key for logging
 * Shows first 4 and last 4 characters, rest is asterisks
 * Returns a static buffer - not thread safe but fine for logging
 */
static const char* redact_api_key(const char *api_key) {
    static char redacted[64];

    if (!api_key || api_key[0] == '\0') {
        return "(not set)";
    }

    size_t len = strlen(api_key);
    if (len <= 8) {
        // Too short to show any characters safely
        return "****";
    }

    // Show first 4 and last 4 characters
    snprintf(redacted, sizeof(redacted), "%.4s...%s", api_key, api_key + len - 4);
    return redacted;
}

/**
 * Log the effective provider configuration
 * Called at startup and when switching providers
 */
void provider_log_config(const char *context,
                         const char *provider_name,
                         const char *provider_type,
                         const char *model,
                         const char *api_base,
                         const char *api_key,
                         const char *api_key_source,
                         int use_bedrock) {
    LOG_INFO("[Provider] === %s ===", context ? context : "Provider Configuration");
    LOG_INFO("[Provider]   Provider: %s", provider_name ? provider_name : "(default)");
    LOG_INFO("[Provider]   Type: %s", provider_type ? provider_type : "auto");
    LOG_INFO("[Provider]   Model: %s", model ? model : "(not set)");
    LOG_INFO("[Provider]   API Base: %s", api_base ? api_base : "(not set)");
    LOG_INFO("[Provider]   API Key: %s (source: %s)",
             redact_api_key(api_key),
             api_key_source ? api_key_source : "unknown");
    if (use_bedrock) {
        LOG_INFO("[Provider]   Bedrock: enabled");
    }
}

/**
 * Get the provider configuration to use based on environment variable or active provider
 * Returns a pointer to the LLMProviderConfig to use, or NULL if using legacy config
 */
static const LLMProviderConfig* get_provider_config_to_use(const KlawedConfig *config) {
    if (!config) {
        LOG_DEBUG("[Provider] get_provider_config_to_use: config is NULL");
        return NULL;
    }

    LOG_DEBUG("[Provider] get_provider_config_to_use: active_provider='%s', provider_count=%d",
              config->active_provider[0] ? config->active_provider : "(not set)",
              config->provider_count);

    // Check if a specific provider is requested via environment variable
    const char *env_provider = getenv("KLAWED_LLM_PROVIDER");
    if (env_provider && env_provider[0] != '\0') {
        LOG_DEBUG("[Provider] KLAWED_LLM_PROVIDER is set to '%s'", env_provider);
        const NamedProviderConfig *named_provider = config_find_provider(config, env_provider);
        if (named_provider) {
            LOG_INFO("[Provider] Selected provider from KLAWED_LLM_PROVIDER: '%s'", env_provider);
            return &named_provider->config;
        } else {
            LOG_WARN("[Provider] Provider '%s' from KLAWED_LLM_PROVIDER not found in configuration, falling back", env_provider);
            // Note: This warning also appears in provider_validate_env() as an error
            // The warning is kept here for backwards compatibility in case provider_validate_env() is not called
        }
    } else {
        LOG_DEBUG("[Provider] KLAWED_LLM_PROVIDER is not set");
    }

    // Check for active provider in config
    const NamedProviderConfig *active_provider = config_get_active_provider(config);
    if (active_provider) {
        LOG_INFO("[Provider] Selected active provider from config: '%s'", config->active_provider);
        return &active_provider->config;
    }

    LOG_DEBUG("[Provider] No active provider found, checking legacy configuration");
    // Fall back to legacy llm_provider configuration
    if (config->llm_provider.model[0] != '\0') {
        LOG_INFO("[Provider] Using legacy llm_provider configuration");
        return NULL;
    }

    LOG_WARN("[Provider] No provider configuration found at all!");
    return NULL;
}

/**
 * Validate KLAWED_LLM_PROVIDER environment variable if set
 * Returns an error message if invalid, or NULL if valid/not set
 */
char *provider_validate_env(void) {
    const char *env_provider = getenv("KLAWED_LLM_PROVIDER");
    if (!env_provider || env_provider[0] == '\0') {
        // Not set, nothing to validate
        return NULL;
    }

    // Load configuration to check if provider exists
    KlawedConfig config;
    config_init_defaults(&config);
    if (config_load(&config) != 0) {
        // Config load failed, but we can't validate
        // Return NULL since we don't know if the provider is valid or not
        LOG_WARN("[Provider] Could not load configuration to validate KLAWED_LLM_PROVIDER");
        return NULL;
    }

    // Check if the provider exists in configuration
    const NamedProviderConfig *named_provider = config_find_provider(&config, env_provider);
    if (!named_provider) {
        // Provider not found - return error message
        char *error_msg = NULL;
        if (asprintf(&error_msg,
                     "Error: Provider '%s' specified in KLAWED_LLM_PROVIDER not found in configuration.\n"
                     "Available providers:",
                     env_provider) < 0) {
            return strdup("Error: Invalid KLAWED_LLM_PROVIDER (and failed to format error message)");
        }

        // Build list of available providers
        for (int i = 0; i < config.provider_count; i++) {
            char *new_msg = NULL;
            if (asprintf(&new_msg, "%s\n  - %s", error_msg, config.providers[i].key) < 0) {
                free(error_msg);
                return strdup("Error: Invalid KLAWED_LLM_PROVIDER (and failed to format error message)");
            }
            free(error_msg);
            error_msg = new_msg;
        }

        // Add suggestion to check configuration file
        char *final_msg = NULL;
        if (asprintf(&final_msg,
                     "%s\n\nPlease check your configuration file (.klawed/config.json or ~/.klawed/config.json)\n"
                     "or unset KLAWED_LLM_PROVIDER to use the default provider.",
                     error_msg) < 0) {
            free(error_msg);
            return strdup("Error: Invalid KLAWED_LLM_PROVIDER (and failed to format error message)");
        }
        free(error_msg);
        return final_msg;
    }

    // Provider found, validation passed
    return NULL;
}

/**
 * Check if the selected provider (via KLAWED_LLM_PROVIDER or active_provider) uses Bedrock
 *
 * This is used early in startup to determine if API key is required.
 * Checks KLAWED_LLM_PROVIDER first, then active_provider from config.
 *
 * @return 1 if selected provider uses bedrock, 0 otherwise
 */
int provider_is_bedrock_selected(void) {
    // Load configuration
    KlawedConfig config;
    config_init_defaults(&config);
    if (config_load(&config) != 0) {
        LOG_DEBUG("[Provider] Could not load config to check bedrock selection");
        return 0;
    }

    // Check KLAWED_LLM_PROVIDER first (highest priority)
    const char *env_provider = getenv("KLAWED_LLM_PROVIDER");
    if (env_provider && env_provider[0] != '\0') {
        const NamedProviderConfig *named_provider = config_find_provider(&config, env_provider);
        if (named_provider) {
            if (named_provider->config.provider_type == PROVIDER_BEDROCK ||
                named_provider->config.use_bedrock) {
                LOG_DEBUG("[Provider] Provider '%s' uses bedrock", env_provider);
                return 1;
            }
            LOG_DEBUG("[Provider] Provider '%s' does not use bedrock", env_provider);
            return 0;
        }
        // Provider not found, fall through to check active_provider
        LOG_DEBUG("[Provider] Provider '%s' from KLAWED_LLM_PROVIDER not found", env_provider);
    }

    // Check active_provider from config
    if (config.active_provider[0] != '\0') {
        const NamedProviderConfig *active = config_find_provider(&config, config.active_provider);
        if (active) {
            if (active->config.provider_type == PROVIDER_BEDROCK ||
                active->config.use_bedrock) {
                LOG_DEBUG("[Provider] Active provider '%s' uses bedrock", config.active_provider);
                return 1;
            }
            LOG_DEBUG("[Provider] Active provider '%s' does not use bedrock", config.active_provider);
            return 0;
        }
    }

    // Check legacy llm_provider
    if (config.llm_provider.provider_type == PROVIDER_BEDROCK ||
        config.llm_provider.use_bedrock) {
        LOG_DEBUG("[Provider] Legacy llm_provider uses bedrock");
        return 1;
    }

    LOG_DEBUG("[Provider] No bedrock provider selected");
    return 0;
}

/**
 * Get the model from the selected provider configuration
 *
 * Checks KLAWED_LLM_PROVIDER first, then active_provider from config.
 * Returns NULL if no provider is selected or if the provider has no model configured.
 *
 * @return Model name (caller must free) or NULL if not found
 */
char *provider_get_selected_model(void) {
    // Load configuration
    KlawedConfig config;
    config_init_defaults(&config);
    if (config_load(&config) != 0) {
        LOG_DEBUG("[Provider] Could not load config to get selected model");
        return NULL;
    }

    // Check KLAWED_LLM_PROVIDER first (highest priority)
    const char *env_provider = getenv("KLAWED_LLM_PROVIDER");
    if (env_provider && env_provider[0] != '\0') {
        const NamedProviderConfig *named_provider = config_find_provider(&config, env_provider);
        if (named_provider && named_provider->config.model[0] != '\0') {
            LOG_DEBUG("[Provider] Using model '%s' from provider '%s'",
                      named_provider->config.model, env_provider);
            return strdup(named_provider->config.model);
        }
    }

    // Check active_provider from config
    if (config.active_provider[0] != '\0') {
        const NamedProviderConfig *active = config_find_provider(&config, config.active_provider);
        if (active && active->config.model[0] != '\0') {
            LOG_DEBUG("[Provider] Using model '%s' from active provider '%s'",
                      active->config.model, config.active_provider);
            return strdup(active->config.model);
        }
    }

    // Check legacy llm_provider
    if (config.llm_provider.model[0] != '\0') {
        LOG_DEBUG("[Provider] Using model '%s' from legacy llm_provider",
                  config.llm_provider.model);
        return strdup(config.llm_provider.model);
    }

    LOG_DEBUG("[Provider] No model found in selected provider");
    return NULL;
}

/**
 * Helper function to check if a provider config has API key configured
 */
static int provider_config_has_api_key(const LLMProviderConfig *cfg) {
    if (!cfg) return 0;

    // Bedrock providers use AWS credentials, not API keys
    if (cfg->provider_type == PROVIDER_BEDROCK || cfg->use_bedrock) {
        return 1;
    }

    // Kimi Coding Plan uses OAuth, not API keys
    if (cfg->provider_type == PROVIDER_KIMI_CODING_PLAN) {
        return 1;
    }

    // OpenAI Subscription uses OAuth, not API keys
    if (cfg->provider_type == PROVIDER_OPENAI_SUB) {
        return 1;
    }

    // Anthropic Subscription uses OAuth, not API keys
    if (cfg->provider_type == PROVIDER_ANTHROPIC_SUB) {
        return 1;
    }

    // OpenAI Responses provider falls back to OPENAI_API_KEY env var
    if (cfg->provider_type == PROVIDER_OPENAI_RESPONSES) {
        const char *env_key = getenv("OPENAI_API_KEY");
        if (env_key && env_key[0] != '\0') {
            return 1;
        }
    }

    // Check if api_key_env is set and the env var exists
    if (cfg->api_key_env[0] != '\0') {
        const char *env_key = getenv(cfg->api_key_env);
        if (env_key && env_key[0] != '\0') {
            return 1;
        }
    }

    // Check if api_key is directly set
    if (cfg->api_key[0] != '\0') {
        return 1;
    }

    return 0;
}

/**
 * Check if the selected provider has an API key configured
 *
 * Checks if the provider selected via KLAWED_LLM_PROVIDER or active_provider
 * has api_key or api_key_env set, or if it's a bedrock provider (which uses AWS credentials).
 *
 * @return 1 if provider has API key configured (or uses bedrock), 0 otherwise
 */
int provider_has_api_key_configured(void) {
    // Load configuration
    KlawedConfig config;
    config_init_defaults(&config);
    if (config_load(&config) != 0) {
        LOG_DEBUG("[Provider] Could not load config to check API key");
        return 0;
    }

    // Check KLAWED_LLM_PROVIDER first (highest priority)
    const char *env_provider = getenv("KLAWED_LLM_PROVIDER");
    if (env_provider && env_provider[0] != '\0') {
        const NamedProviderConfig *named_provider = config_find_provider(&config, env_provider);
        if (named_provider) {
            int has_key = provider_config_has_api_key(&named_provider->config);
            LOG_DEBUG("[Provider] Provider '%s' has API key configured: %d", env_provider, has_key);
            return has_key;
        }
        // Provider not found, fall through
    }

    // Check active_provider from config
    if (config.active_provider[0] != '\0') {
        const NamedProviderConfig *active = config_find_provider(&config, config.active_provider);
        if (active) {
            int has_key = provider_config_has_api_key(&active->config);
            LOG_DEBUG("[Provider] Active provider '%s' has API key configured: %d",
                      config.active_provider, has_key);
            return has_key;
        }
    }

    // Check legacy llm_provider
    int has_key = provider_config_has_api_key(&config.llm_provider);
    LOG_DEBUG("[Provider] Legacy llm_provider has API key configured: %d", has_key);
    return has_key;
}

/**
 * Get provider configuration from config file or environment variables
 *
 * Priority when a named provider is selected (via active_provider, KLAWED_LLM_PROVIDER, or --provider CLI flag):
 *   - Named provider's configuration (model, api_base, api_key, use_bedrock) takes precedence
 *   - Environment variables (OPENAI_MODEL, OPENAI_API_BASE, OPENAI_API_KEY) are used as fallbacks
 *
 * Priority when no named provider is selected (legacy mode):
 *   - Environment variables (OPENAI_MODEL, OPENAI_API_BASE, OPENAI_API_KEY) take precedence
 *   - Legacy llm_provider config is used as fallback
 *
 * @param arena Arena allocator for all allocations
 * @param config Pointer to config struct to populate
 * @param model_out Pointer to store model name (arena-allocated, don't free)
 * @param api_key_out Pointer to store API key (arena-allocated, don't free)
 * @param api_base_out Pointer to store API base URL (arena-allocated, don't free)
 * @param use_bedrock_out Pointer to store use_bedrock flag
 * @param api_key_source_out Pointer to store API key source description (static string, don't free)
 * @param provider_name_out Pointer to store provider name (static string from config, don't free)
 */
static void get_provider_config(Arena *arena,
                                KlawedConfig *config,
                                char **model_out,
                                char **api_key_out,
                                char **api_base_out,
                                int *use_bedrock_out,
                                const char **api_key_source_out,
                                const char **provider_name_out,
                                LLMProviderType *provider_type_out,
                                const LLMProviderConfig **provider_config_out) {
    // Initialize outputs
    *model_out = NULL;
    *api_key_out = NULL;
    *api_base_out = NULL;
    *use_bedrock_out = 0;
    *api_key_source_out = NULL;
    *provider_name_out = NULL;
    if (provider_type_out) {
        *provider_type_out = PROVIDER_AUTO;
    }
    if (provider_config_out) {
        *provider_config_out = NULL;
    }

    // Check for KLAWED_PROVIDER_TYPE (env-only provider configuration)
    // This allows using providers like zai_coding, kimi_coding_plan without config.json
    const char *env_provider_type = getenv("KLAWED_PROVIDER_TYPE");
    LLMProviderConfig synthetic_provider;
    memset(&synthetic_provider, 0, sizeof(synthetic_provider));
    int using_synthetic_provider = 0;

    if (env_provider_type && env_provider_type[0] != '\0') {
        synthetic_provider.provider_type = config_provider_type_from_string(env_provider_type);
        if (synthetic_provider.provider_type != PROVIDER_AUTO) {
            using_synthetic_provider = 1;
            strlcpy(synthetic_provider.provider_name, env_provider_type, CONFIG_PROVIDER_NAME_MAX);
            LOG_INFO("[Provider] Using provider type '%s' from KLAWED_PROVIDER_TYPE", env_provider_type);

            // Set appropriate API key env var for specific providers
            if (synthetic_provider.provider_type == PROVIDER_ZAI_CODING) {
                strlcpy(synthetic_provider.api_key_env, "ZAI_API_KEY_CODING_PLAN", CONFIG_API_KEY_ENV_MAX);
                // Default model for Z.AI - will be used if OPENAI_MODEL is not set
                strlcpy(synthetic_provider.model, "glm-4.5", CONFIG_MODEL_MAX);
            } else if (synthetic_provider.provider_type == PROVIDER_KIMI_CODING_PLAN) {
                // Kimi Coding Plan uses OAuth, no API key needed
                strlcpy(synthetic_provider.model, "kimi-for-coding", CONFIG_MODEL_MAX);
            } else if (synthetic_provider.provider_type == PROVIDER_OPENAI_SUB) {
                // OpenAI Subscription uses OAuth, no API key needed
                strlcpy(synthetic_provider.model, "gpt-5.3-codex", CONFIG_MODEL_MAX);
            } else if (synthetic_provider.provider_type == PROVIDER_ANTHROPIC_SUB) {
                // Anthropic Subscription uses OAuth, no API key needed
                strlcpy(synthetic_provider.model, "claude-opus-4", CONFIG_MODEL_MAX);
            } else if (synthetic_provider.provider_type == PROVIDER_MINIMAX_CODING) {
                // MiniMax Coding Plan uses MINIMAX_CODING_PLAN_API_KEY
                strlcpy(synthetic_provider.api_key_env, "MINIMAX_CODING_PLAN_API_KEY", CONFIG_API_KEY_ENV_MAX);
                strlcpy(synthetic_provider.model, "MiniMax-M2.1", CONFIG_MODEL_MAX);
            }
        }
    }

    // Load configuration from file
    KlawedConfig file_config;
    config_init_defaults(&file_config);
    int config_loaded = (config_load(&file_config) == 0);

    // Get the provider configuration to use (checks KLAWED_LLM_PROVIDER env and active_provider)
    const LLMProviderConfig *provider_config = NULL;
    const char *active_provider_key = NULL;

    // If using synthetic provider from KLAWED_PROVIDER_TYPE, use that
    if (using_synthetic_provider) {
        provider_config = &synthetic_provider;
        active_provider_key = env_provider_type;
        if (provider_type_out) {
            *provider_type_out = synthetic_provider.provider_type;
        }
        if (provider_config_out) {
            *provider_config_out = provider_config;
        }
    } else if (config_loaded) {
        provider_config = get_provider_config_to_use(&file_config);
        if (provider_config_out) {
            *provider_config_out = provider_config;
        }
        // Determine which provider key is active
        const char *env_provider = getenv("KLAWED_LLM_PROVIDER");
        if (env_provider && env_provider[0] != '\0') {
            active_provider_key = env_provider;
        } else if (file_config.active_provider[0] != '\0') {
            active_provider_key = file_config.active_provider;
        }
    }

    // Check environment variables (used as fallback when provider is selected, or primary when not)
    const char *env_model = getenv("OPENAI_MODEL");
    const char *env_api_key = getenv("OPENAI_API_KEY");
    const char *env_api_base = getenv("OPENAI_API_BASE");
    const char *env_use_bedrock = getenv("KLAWED_USE_BEDROCK");

    // When a named provider is explicitly selected via active_provider, KLAWED_LLM_PROVIDER,
    // or --provider CLI flag, the provider's configuration takes precedence.
    // Environment variables (OPENAI_MODEL, OPENAI_API_BASE, OPENAI_API_KEY) are used as fallbacks.
    if (provider_config) {
        LOG_DEBUG("[Provider] Named provider selected, using provider config as primary (env vars as fallback)");
        *provider_name_out = active_provider_key;

        // Get model (priority: env var > provider config for synthetic providers, provider config > env var otherwise)
        // This allows OPENAI_MODEL to override synthetic provider defaults
        if (using_synthetic_provider && env_model && env_model[0] != '\0') {
            *model_out = arena_strdup(arena, env_model);
        } else if (provider_config->model[0] != '\0') {
            *model_out = arena_strdup(arena, provider_config->model);
        } else if (env_model && env_model[0] != '\0') {
            *model_out = arena_strdup(arena, env_model);
        }

        // Get API base (priority: provider config > env var)
        if (provider_config->api_base[0] != '\0') {
            *api_base_out = arena_strdup(arena, provider_config->api_base);
        } else if (env_api_base && env_api_base[0] != '\0') {
            *api_base_out = arena_strdup(arena, env_api_base);
        }

        // Get API key (priority: api_key_env from config > api_key from config > OPENAI_API_KEY env)
        if (provider_config->api_key_env[0] != '\0') {
            const char *env_key_from_config = getenv(provider_config->api_key_env);
            if (env_key_from_config && env_key_from_config[0] != '\0') {
                *api_key_out = arena_strdup(arena, env_key_from_config);
                *api_key_source_out = provider_config->api_key_env;
                LOG_DEBUG("[Provider] Using API key from environment variable: %s", provider_config->api_key_env);
            } else {
                LOG_WARN("[Provider] api_key_env specified (%s) but environment variable is not set or empty", provider_config->api_key_env);
            }
        }
        if (!*api_key_out && provider_config->api_key[0] != '\0') {
            *api_key_out = arena_strdup(arena, provider_config->api_key);
            *api_key_source_out = "config file";
            LOG_WARN("[Provider] Using API key from config file - consider using environment variable for better security");
        }
        if (!*api_key_out && env_api_key && env_api_key[0] != '\0') {
            *api_key_out = arena_strdup(arena, env_api_key);
            *api_key_source_out = "OPENAI_API_KEY";
        }

        // Get use_bedrock flag (priority: provider config > env var for explicit override)
        if (provider_config->use_bedrock) {
            *use_bedrock_out = 1;
        } else if (env_use_bedrock && (strcmp(env_use_bedrock, "1") == 0 ||
                                      strcmp(env_use_bedrock, "true") == 0 ||
                                      strcmp(env_use_bedrock, "TRUE") == 0)) {
            *use_bedrock_out = 1;
        }
    } else {
        // Legacy mode: no named provider selected
        // Environment variables take precedence over legacy llm_provider config
        LOG_DEBUG("[Provider] No named provider, using legacy mode (env vars take precedence)");
        *provider_name_out = "(legacy)";

        // Get model (priority: env var > legacy config)
        if (env_model && env_model[0] != '\0') {
            *model_out = arena_strdup(arena, env_model);
        } else if (config_loaded && file_config.llm_provider.model[0] != '\0') {
            *model_out = arena_strdup(arena, file_config.llm_provider.model);
        }

        // Get API base (priority: env var > legacy config)
        if (env_api_base && env_api_base[0] != '\0') {
            *api_base_out = arena_strdup(arena, env_api_base);
        } else if (config_loaded && file_config.llm_provider.api_base[0] != '\0') {
            *api_base_out = arena_strdup(arena, file_config.llm_provider.api_base);
        }

        // Get API key (priority: OPENAI_API_KEY env > api_key_env from legacy config > api_key from legacy config)
        if (env_api_key && env_api_key[0] != '\0') {
            *api_key_out = arena_strdup(arena, env_api_key);
            *api_key_source_out = "OPENAI_API_KEY";
        } else if (config_loaded && file_config.llm_provider.api_key_env[0] != '\0') {
            const char *env_key_from_config = getenv(file_config.llm_provider.api_key_env);
            if (env_key_from_config && env_key_from_config[0] != '\0') {
                *api_key_out = arena_strdup(arena, env_key_from_config);
                *api_key_source_out = file_config.llm_provider.api_key_env;
                LOG_DEBUG("[Provider] Using API key from environment variable: %s", file_config.llm_provider.api_key_env);
            } else {
                LOG_WARN("[Provider] api_key_env specified (%s) but environment variable is not set or empty", file_config.llm_provider.api_key_env);
            }
        } else if (config_loaded && file_config.llm_provider.api_key[0] != '\0') {
            *api_key_out = arena_strdup(arena, file_config.llm_provider.api_key);
            *api_key_source_out = "config file";
            LOG_WARN("[Provider] Using API key from config file - consider using OPENAI_API_KEY environment variable for better security");
        }

        // Get use_bedrock flag
        if (env_use_bedrock && (strcmp(env_use_bedrock, "1") == 0 ||
                               strcmp(env_use_bedrock, "true") == 0 ||
                               strcmp(env_use_bedrock, "TRUE") == 0)) {
            *use_bedrock_out = 1;
        } else if (config_loaded) {
            *use_bedrock_out = file_config.llm_provider.use_bedrock;
        }
    }

    // If config was loaded, copy it to output
    if (config_loaded) {
        *config = file_config;
    } else {
        *config = file_config; // Defaults
    }
}



/**
 * Get API URL from environment, or return default
 */
static char *get_api_url_from_env(void) {
    // Prefer OpenAI-compatible env first, then Anthropic URLs as fallback
    const char *env_url = getenv("OPENAI_API_BASE");
    if (!env_url || env_url[0] == '\0') {
        env_url = getenv("ANTHROPIC_API_URL");
    }
    if (!env_url || env_url[0] == '\0') {
        // Some SDKs and docs use ANTHROPIC_BASE_URL; accept it too
        env_url = getenv("ANTHROPIC_BASE_URL");
    }

    if (env_url && env_url[0] != '\0') {
        char *url = strdup(env_url);
        if (url) LOG_INFO("Using API URL from environment: %s", url);
        return url;
    }
    char *default_url = strdup(DEFAULT_ANTHROPIC_URL);
    if (default_url) LOG_INFO("Using default API URL: %s", default_url);
    return default_url;
}

/**
 * Initialize the appropriate provider based on environment configuration.
 * Populates the provided result struct.
 */
void provider_init(const char *model,
                   const char *api_key,
                   ProviderInitResult *result) {
    // Initialize output
    result->provider = NULL;
    result->api_url = NULL;
    result->error_message = NULL;

    LOG_DEBUG("Initializing provider (model: %s)...", model ? model : "(null)");

    // Create arena for all allocations in this function
    Arena *arena = arena_create(4096);
    if (!arena) {
        result->error_message = strdup("Failed to create memory arena");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        return;
    }

    // Get configuration from file/env vars
    KlawedConfig effective_config;
    char *config_model = NULL;
    char *config_api_key = NULL;
    char *config_api_base = NULL;
    int config_use_bedrock = 0;
    const char *api_key_source = NULL;
    const char *provider_name = NULL;

    LLMProviderType provider_type = PROVIDER_AUTO;
    const LLMProviderConfig *provider_config = NULL;
    get_provider_config(arena, &effective_config, &config_model, &config_api_key,
                        &config_api_base, &config_use_bedrock, &api_key_source, &provider_name,
                        &provider_type, &provider_config);

    LOG_DEBUG("[Provider Init] Loaded config: active_provider='%s', provider_count=%d, legacy model='%s'",
              effective_config.active_provider[0] ? effective_config.active_provider : "(not set)",
              effective_config.provider_count,
              effective_config.llm_provider.model[0] ? effective_config.llm_provider.model : "(not set)");

    // If provider_type wasn't set by get_provider_config (e.g., from KLAWED_PROVIDER_TYPE),
    // fall back to checking config file
    int named_provider_selected = 0;
    if (provider_type == PROVIDER_AUTO) {
        provider_config = get_provider_config_to_use(&effective_config);
        if (provider_config) {
            named_provider_selected = 1;
            provider_type = provider_config->provider_type;
        } else {
            provider_type = effective_config.llm_provider.provider_type;
        }
        LOG_DEBUG("[Provider Init] Selected provider_config=%s, provider_type=%d",
                  provider_config ? provider_config->provider_name : "(NULL)", provider_type);
    } else {
        LOG_DEBUG("[Provider Init] Using provider_type=%d from KLAWED_PROVIDER_TYPE or env config", provider_type);
    }

    // Determine which model to use
    // Priority:
    // 1. Named provider's model (when a valid named provider is selected)
    // 2. Config file's model from get_provider_config (handles env vars for non-named configs)
    // 3. Passed model parameter (only used in legacy mode when no named provider)
    // 4. Environment variable (OPENAI_MODEL)
    char *model_to_use = NULL;

    if (named_provider_selected && provider_config && provider_config->model[0] != '\0') {
        // Named provider is selected - use its configured model
        // This takes precedence over the passed model parameter
        model_to_use = arena_strdup(arena, provider_config->model);
        LOG_DEBUG("[Provider] Using model from named provider '%s': %s",
                provider_config->provider_name, model_to_use);
    } else if (config_model && config_model[0] != '\0') {
        // Use model from get_provider_config (handles env vars, provider defaults, etc.)
        model_to_use = config_model;
        LOG_DEBUG("[Provider] Using model from get_provider_config: %s", model_to_use);
    } else if (model && model[0] != '\0') {
        // Legacy mode: use passed parameter when no named provider selected
        model_to_use = arena_strdup(arena, model);
        LOG_DEBUG("[Provider] Using model from passed parameter (legacy mode): %s", model_to_use);
    } else {
        // Fall back to environment variable
        const char *env_model = getenv("OPENAI_MODEL");
        if (env_model && env_model[0] != '\0') {
            model_to_use = arena_strdup(arena, env_model);
            LOG_DEBUG("[Provider] Using model from environment variable: %s", model_to_use);
        }
    }

    if (!model_to_use || model_to_use[0] == '\0') {
        result->error_message = strdup("Model name is required. Set OPENAI_MODEL environment variable or configure in .klawed/config.json");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        arena_destroy(arena);
        return;
    }

    // Determine which API key to use for logging (priority: passed parameter > config)
    const char *api_key_for_log = (api_key && api_key[0] != '\0') ? api_key : config_api_key;
    if (api_key && api_key[0] != '\0') {
        api_key_source = "parameter";
    }

    // Log the effective provider configuration at startup
    provider_log_config("Startup",
                        provider_name,
                        config_provider_type_to_string(provider_type),
                        model_to_use,
                        config_api_base,
                        api_key_for_log,
                        api_key_source,
                        config_use_bedrock);

    // Set the selected model in the result (will be used to update state->model)
    result->model = strdup(model_to_use);
    if (!result->model) {
        result->error_message = strdup("Failed to allocate memory for model");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        arena_destroy(arena);
        return;
    }

    LOG_DEBUG("Initializing provider (model: %s, provider_type: %s)...",
              model_to_use, config_provider_type_to_string(provider_type));

    // Bedrock provider selection
    if (config_use_bedrock || provider_type == PROVIDER_BEDROCK) {
        const char *use_bedrock_env = getenv("KLAWED_USE_BEDROCK");
        const char *aws_profile = getenv("AWS_PROFILE");
        const char *aws_region = getenv("AWS_REGION");
        const char *aws_config_file = getenv("AWS_CONFIG_FILE");
        const char *aws_shared_creds = getenv("AWS_SHARED_CREDENTIALS_FILE");
        const char *aws_access_key = getenv("AWS_ACCESS_KEY_ID");
        const char *aws_secret_key = getenv("AWS_SECRET_ACCESS_KEY");
        const char *aws_session_token = getenv("AWS_SESSION_TOKEN");

        LOG_INFO("Bedrock mode is enabled, creating Bedrock provider...");
        LOG_INFO("Bedrock env summary: KLAWED_USE_BEDROCK=%s",
                 use_bedrock_env ? use_bedrock_env : "(not set)");
        LOG_INFO("Bedrock env summary: AWS_PROFILE=%s, AWS_REGION=%s",
                 aws_profile ? aws_profile : "(not set)",
                 aws_region ? aws_region : "(not set)");
        LOG_INFO("Bedrock env summary: AWS_CONFIG_FILE=%s, AWS_SHARED_CREDENTIALS_FILE=%s",
                 aws_config_file ? aws_config_file : "(not set)",
                 aws_shared_creds ? aws_shared_creds : "(not set)");
        LOG_INFO("Bedrock env summary: Credentials present? access_key=%s secret_key=%s session_token=%s",
                 aws_access_key ? "yes" : "no",
                 aws_secret_key ? "yes" : "no",
                 aws_session_token ? "yes" : "no");

        Provider *prov = bedrock_provider_create(model_to_use);
        if (!prov) {
            result->error_message = strdup(
                "Failed to initialize Bedrock provider (check logs for details)");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            arena_destroy(arena);
            return;
        }
        BedrockConfig *cfg = (BedrockConfig *)prov->config;
        if (!cfg || !cfg->endpoint) {
            result->error_message = strdup(
                "Bedrock provider initialized but endpoint is missing");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            prov->cleanup(prov);
            arena_destroy(arena);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(cfg->endpoint);  // This needs to survive arena destruction
        if (!result->api_url) {
            result->error_message = strdup(
                "Failed to allocate memory for Bedrock endpoint");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            prov->cleanup(prov);
            arena_destroy(arena);
            return;
        }
        LOG_INFO("Provider initialization successful: Bedrock (endpoint: %s)",
                 result->api_url);
        arena_destroy(arena);
        return;
    }

    // Non-Bedrock: pick provider based on configuration
    // Determine which API key to use (priority: passed parameter > config file > env var)
    char *api_key_to_use = NULL;
    if (api_key && api_key[0] != '\0') {
        api_key_to_use = arena_strdup(arena, api_key);
    } else if (config_api_key && config_api_key[0] != '\0') {
        api_key_to_use = config_api_key;  // Already arena-allocated
    } else {
        // Fall back to environment variable
        const char *env_api_key = getenv("OPENAI_API_KEY");
        if (env_api_key && env_api_key[0] != '\0') {
            api_key_to_use = arena_strdup(arena, env_api_key);
        }
    }

    if (!api_key_to_use || api_key_to_use[0] == '\0') {
        /* OAuth providers (Kimi Coding Plan, OpenAI Sub, Anthropic Sub) don't need an API key */
        if (provider_type != PROVIDER_KIMI_CODING_PLAN &&
            provider_type != PROVIDER_OPENAI_SUB &&
            provider_type != PROVIDER_ANTHROPIC_SUB) {
            result->error_message = strdup("API key is required for API provider. Set OPENAI_API_KEY environment variable or configure in .klawed/config.json");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            arena_destroy(arena);
            return;
        }
    }

    // Determine which API base URL to use
    char *base_url = NULL;
    if (config_api_base && config_api_base[0] != '\0') {
        base_url = arena_strdup(arena, config_api_base);
    } else {
        base_url = get_api_url_from_env();  // This uses strdup, not arena
        if (base_url) {
            // Convert to arena-allocated string
            char *arena_base_url = arena_strdup(arena, base_url);
            free(base_url);
            base_url = arena_base_url;
        }
    }

    if (!base_url) {
        result->error_message = strdup("Failed to allocate memory for API URL");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        arena_destroy(arena);
        return;
    }

    // Determine provider based on configuration, environment variables, and URL
    // Priority: 1. Explicit provider_type in config, 2. Environment indicators, 3. URL patterns
    int use_anthropic = 0;

    // Check explicit provider type from configuration
    if (provider_type != PROVIDER_AUTO) {
        if (provider_type == PROVIDER_ANTHROPIC) {
            use_anthropic = 1;
            LOG_INFO("Using Anthropic provider (explicitly configured)");
        } else if (provider_type == PROVIDER_OPENAI) {
            use_anthropic = 0;
            LOG_INFO("Using OpenAI provider (explicitly configured)");
        } else if (provider_type == PROVIDER_DEEPSEEK) {
            LOG_INFO("Using DeepSeek provider (explicitly configured)");
            Provider *prov = deepseek_provider_create(api_key_to_use, base_url);
            if (!prov) {
                result->error_message = strdup("Failed to initialize DeepSeek provider (check logs for details)");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                arena_destroy(arena);
                return;
            }
            OpenAIConfig *cfg = (OpenAIConfig *)prov->config;
            if (!cfg || !cfg->base_url) {
                result->error_message = strdup("DeepSeek provider initialized but base URL is missing");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            result->provider = prov;
            result->api_url = strdup(cfg->base_url);
            if (!result->api_url) {
                result->error_message = strdup("Failed to allocate memory for API URL");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            LOG_INFO("Provider initialization successful: DeepSeek (base URL: %s)", result->api_url);
            arena_destroy(arena);
            return;
        } else if (provider_type == PROVIDER_MOONSHOT) {
            LOG_INFO("Using Moonshot provider (explicitly configured)");
            // Use create_with_headers if extra_headers are present in config
            Provider *prov;
            if (provider_config && provider_config->extra_headers[0] != '\0') {
                prov = moonshot_provider_create_with_headers(api_key_to_use, base_url,
                                                              provider_config->extra_headers);
            } else {
                prov = moonshot_provider_create(api_key_to_use, base_url);
            }
            if (!prov) {
                result->error_message = strdup("Failed to initialize Moonshot provider (check logs for details)");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                arena_destroy(arena);
                return;
            }
            OpenAIConfig *cfg = (OpenAIConfig *)prov->config;
            if (!cfg || !cfg->base_url) {
                result->error_message = strdup("Moonshot provider initialized but base URL is missing");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            result->provider = prov;
            result->api_url = strdup(cfg->base_url);
            if (!result->api_url) {
                result->error_message = strdup("Failed to allocate memory for API URL");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            LOG_INFO("Provider initialization successful: Moonshot (base URL: %s)", result->api_url);
            arena_destroy(arena);
            return;
        } else if (provider_type == PROVIDER_ZAI_CODING) {
            LOG_INFO("Using Z.AI GLM Coding Plan provider (explicitly configured)");
            // Use create_with_headers if extra_headers are present in config
            Provider *prov;
            if (provider_config && provider_config->extra_headers[0] != '\0') {
                prov = zai_coding_provider_create_with_headers(api_key_to_use, base_url,
                                                                provider_config->extra_headers);
            } else {
                prov = zai_coding_provider_create(api_key_to_use, base_url);
            }
            if (!prov) {
                result->error_message = strdup("Failed to initialize Z.AI Coding Plan provider (check logs for details)");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                arena_destroy(arena);
                return;
            }
            OpenAIConfig *cfg = (OpenAIConfig *)prov->config;
            if (!cfg || !cfg->base_url) {
                result->error_message = strdup("Z.AI Coding Plan provider initialized but base URL is missing");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            result->provider = prov;
            result->api_url = strdup(cfg->base_url);
            if (!result->api_url) {
                result->error_message = strdup("Failed to allocate memory for API URL");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            LOG_INFO("Provider initialization successful: Z.AI GLM Coding (base URL: %s)", result->api_url);
            arena_destroy(arena);
            return;
        } else if (provider_type == PROVIDER_KIMI_CODING_PLAN) {
            LOG_INFO("Using Kimi Coding Plan provider (explicitly configured)");
            Provider *prov = kimi_coding_plan_provider_create(model_to_use);
            if (!prov) {
                result->error_message = strdup("Failed to initialize Kimi Coding Plan provider (check logs for details)");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                arena_destroy(arena);
                return;
            }
            KimiCodingPlanConfig *cfg = (KimiCodingPlanConfig *)prov->config;
            if (!cfg || !cfg->api_base) {
                result->error_message = strdup("Kimi Coding Plan provider initialized but API base is missing");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            result->provider = prov;
            result->api_url = strdup(cfg->api_base);
            if (!result->api_url) {
                result->error_message = strdup("Failed to allocate memory for API URL");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            LOG_INFO("Provider initialization successful: Kimi Coding Plan (API base: %s)", result->api_url);
            arena_destroy(arena);
            return;
        } else if (provider_type == PROVIDER_MINIMAX_CODING) {
            LOG_INFO("Using MiniMax Coding Plan provider (explicitly configured)");
            Provider *prov = minimax_coding_provider_create(api_key_to_use, base_url);
            if (!prov) {
                result->error_message = strdup("Failed to initialize MiniMax Coding Plan provider (check logs for details)");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                arena_destroy(arena);
                return;
            }
            AnthropicConfig *cfg = (AnthropicConfig *)prov->config;
            if (!cfg || !cfg->base_url) {
                result->error_message = strdup("MiniMax Coding Plan provider initialized but base URL is missing");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            result->provider = prov;
            result->api_url = strdup(cfg->base_url);
            if (!result->api_url) {
                result->error_message = strdup("Failed to allocate memory for API URL");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            LOG_INFO("Provider initialization successful: MiniMax Coding Plan (base URL: %s)", result->api_url);
            arena_destroy(arena);
            return;
        } else if (provider_type == PROVIDER_OPENAI_SUB) {
            LOG_INFO("Using OpenAI Subscription provider (explicitly configured)");
            const char *api_base_to_use = (provider_config && provider_config->api_base[0] != '\0')
                                          ? provider_config->api_base : NULL;
            Provider *prov = openai_sub_provider_create(model_to_use, api_base_to_use);
            if (!prov) {
                result->error_message = strdup("Failed to initialize OpenAI Subscription provider (check logs for details)");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                arena_destroy(arena);
                return;
            }
            OpenAISubConfig *sub_cfg = (OpenAISubConfig *)prov->config;
            if (!sub_cfg || !sub_cfg->api_base) {
                result->error_message = strdup("OpenAI Subscription provider initialized but API base is missing");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            result->provider = prov;
            result->api_url = strdup(sub_cfg->api_base);
            if (!result->api_url) {
                result->error_message = strdup("Failed to allocate memory for API URL");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            LOG_INFO("Provider initialization successful: OpenAI Subscription (API base: %s)", result->api_url);
            arena_destroy(arena);
            return;
        } else if (provider_type == PROVIDER_ANTHROPIC_SUB) {
            LOG_INFO("Using Anthropic Subscription provider (explicitly configured)");
            const char *api_base_to_use = (provider_config && provider_config->api_base[0] != '\0')
                                          ? provider_config->api_base : NULL;
            Provider *prov = anthropic_sub_provider_create(model_to_use, api_base_to_use);
            if (!prov) {
                result->error_message = strdup("Failed to initialize Anthropic Subscription provider "
                                               "(ensure ~/.claude/.credentials.json exists — run 'claude auth login')");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                arena_destroy(arena);
                return;
            }
            AnthropicSubConfig *sub_cfg = (AnthropicSubConfig *)prov->config;
            if (!sub_cfg || !sub_cfg->api_base) {
                result->error_message = strdup("Anthropic Subscription provider initialized but API base is missing");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            result->provider = prov;
            result->api_url = strdup(sub_cfg->api_base);
            if (!result->api_url) {
                result->error_message = strdup("Failed to allocate memory for API URL");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            LOG_INFO("Provider initialization successful: Anthropic Subscription (API base: %s)",
                     result->api_url);
            arena_destroy(arena);
            return;
        } else if (provider_type == PROVIDER_OPENAI_RESPONSES) {
            LOG_INFO("Using OpenAI Responses API provider (explicitly configured)");
            const char *responses_api_base = (provider_config && provider_config->api_base[0] != '\0')
                                              ? provider_config->api_base : NULL;
            const char *responses_api_key = (provider_config && provider_config->api_key[0] != '\0')
                                             ? provider_config->api_key : NULL;
            Provider *prov = openai_responses_provider_create(responses_api_key,
                                                               responses_api_base,
                                                               model_to_use);
            if (!prov) {
                result->error_message = strdup(
                    "Failed to initialize OpenAI Responses provider "
                    "(ensure OPENAI_API_KEY is set or api_key is in the provider config)");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                arena_destroy(arena);
                return;
            }
            OpenAIResponsesProviderConfig *resp_cfg =
                (OpenAIResponsesProviderConfig *)prov->config;
            if (!resp_cfg || !resp_cfg->api_base) {
                result->error_message = strdup(
                    "OpenAI Responses provider initialized but API base is missing");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            result->provider = prov;
            result->api_url = strdup(resp_cfg->api_base);
            if (!result->api_url) {
                result->error_message = strdup("Failed to allocate memory for API URL");
                LOG_ERROR("Provider init failed: %s", result->error_message);
                prov->cleanup(prov);
                arena_destroy(arena);
                return;
            }
            LOG_INFO("Provider initialization successful: OpenAI Responses API (url: %s)",
                     result->api_url);
            arena_destroy(arena);
            return;
        } else if (provider_type == PROVIDER_CUSTOM) {
            // For custom, we need to check URL patterns
            use_anthropic = 0; // Default to OpenAI-compatible for custom
            LOG_INFO("Using custom provider (defaulting to OpenAI-compatible)");
        }
    } else {
        // Auto-detect based on environment variables and URL patterns
        // Check if OpenAI-specific variables are set (indicates OpenAI preference)
        const char *openai_base = getenv("OPENAI_API_BASE");

        // Check if Anthropic-specific variables are set
        const char *anth_env = getenv("ANTHROPIC_API_URL");
        if (!anth_env || anth_env[0] == '\0') {
            anth_env = getenv("ANTHROPIC_BASE_URL");
        }

        // If OPENAI_API_BASE is explicitly set, prefer OpenAI provider
        if (openai_base && openai_base[0] != '\0') {
            use_anthropic = 0;
            LOG_INFO("OPENAI_API_BASE is set, using OpenAI-compatible provider");
        }
        // Only use Anthropic if Anthropic-specific URLs are set and OpenAI base is not
        else if (anth_env && anth_env[0] != '\0') {
            use_anthropic = 1;
            LOG_INFO("Anthropic-specific URL set, using Anthropic provider");
        }
        // Check URL patterns as final fallback
        else if (strstr(base_url, "anthropic.com") != NULL || strstr(base_url, "/anthropic") != NULL) {
            use_anthropic = 1;
            LOG_INFO("Anthropic URL detected, using Anthropic provider");
        }
        else {
            // Default to OpenAI if no clear indicators
            use_anthropic = 0;
            LOG_INFO("No specific provider indicators, defaulting to OpenAI-compatible provider");
        }
    }

    if (use_anthropic) {
        LOG_INFO("Using Anthropic provider (direct API)...");
        Provider *prov = anthropic_provider_create(api_key_to_use, base_url);
        // base_url is arena-allocated, don't free it here
        if (!prov) {
            result->error_message = strdup("Failed to initialize Anthropic provider (check logs for details)");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            arena_destroy(arena);
            return;
        }
        AnthropicConfig *cfg = (AnthropicConfig *)prov->config;
        if (!cfg || !cfg->base_url) {
            result->error_message = strdup("Anthropic provider initialized but base URL is missing");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            prov->cleanup(prov);
            arena_destroy(arena);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(cfg->base_url);  // This needs to survive arena destruction
        if (!result->api_url) {
            result->error_message = strdup("Failed to allocate memory for API URL");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            prov->cleanup(prov);
            arena_destroy(arena);
            return;
        }
        LOG_INFO("Provider initialization successful: Anthropic (endpoint: %s)", result->api_url);
        arena_destroy(arena);
        return;
    }

    LOG_INFO("Using OpenAI-compatible provider...");

    Provider *prov = openai_provider_create(api_key_to_use, base_url);
    // base_url is arena-allocated, openai_provider_create makes its own copy
    if (!prov) {
        result->error_message = strdup(
            "Failed to initialize OpenAI provider (check logs for details)");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        arena_destroy(arena);
        return;
    }
    OpenAIConfig *cfg = (OpenAIConfig *)prov->config;

    // Apply extra_headers from provider config if present
    if (cfg && provider_config && provider_config->extra_headers[0] != '\0') {
        LOG_INFO("[Provider] Loading extra_headers from config: %s", provider_config->extra_headers);

        // Parse comma-separated headers
        char *extra_headers_copy = strdup(provider_config->extra_headers);
        if (extra_headers_copy) {
            // Count headers
            int count = 0;
            char *temp = strdup(provider_config->extra_headers);
            if (temp) {
                char *token = strtok(temp, ",");
                while (token) {
                    count++;
                    token = strtok(NULL, ",");
                }
                free(temp);
            }

            // Allocate array
            cfg->extra_headers = calloc((size_t)count + 1, sizeof(char*));
            if (cfg->extra_headers) {
                cfg->extra_headers_count = count;
                int idx = 0;
                char *token = strtok(extra_headers_copy, ",");
                while (token && idx < count) {
                    cfg->extra_headers[idx] = strdup(token);
                    LOG_DEBUG("[Provider] Added extra header: %s", token);
                    idx++;
                    token = strtok(NULL, ",");
                }
            }
            free(extra_headers_copy);
        }
    }
    if (!cfg || !cfg->base_url) {
        result->error_message = strdup(
            "OpenAI provider initialized but base URL is missing");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        prov->cleanup(prov);
        arena_destroy(arena);
        return;
    }
    result->provider = prov;
    result->api_url = strdup(cfg->base_url);  // This needs to survive arena destruction
    if (!result->api_url) {
        result->error_message = strdup(
            "Failed to allocate memory for API URL");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        prov->cleanup(prov);
        arena_destroy(arena);
        return;
    }
    LOG_INFO("Provider initialization successful: OpenAI (base URL: %s)",
             result->api_url);
    arena_destroy(arena);
}

/**
 * Initialize a provider directly from a LLMProviderConfig
 *
 * This bypasses the normal env var > config file priority logic and creates
 * a provider directly from the given configuration. Used for runtime provider
 * switching via /provider command.
 */
void provider_init_from_config(const char *provider_key,
                               const LLMProviderConfig *config,
                               ProviderInitResult *result) {
    // Initialize output
    result->provider = NULL;
    result->api_url = NULL;
    result->error_message = NULL;

    if (!config) {
        result->error_message = strdup("Provider configuration is required");
        LOG_ERROR("Provider init from config failed: %s", result->error_message);
        return;
    }

    LOG_DEBUG("Initializing provider from config (provider_key: %s, model: %s)...",
              provider_key ? provider_key : "(null)",
              config->model[0] != '\0' ? config->model : "(null)");

    // Get model from config
    const char *model = config->model[0] != '\0' ? config->model : NULL;
    if (!model) {
        result->error_message = strdup("Model name is required in provider configuration");
        LOG_ERROR("Provider init from config failed: %s", result->error_message);
        return;
    }

    // Get API key: try api_key_env first, then api_key, then fall back to OPENAI_API_KEY
    const char *api_key = NULL;
    const char *api_key_source = NULL;

    if (config->api_key_env[0] != '\0') {
        api_key = getenv(config->api_key_env);
        if (api_key && api_key[0] != '\0') {
            api_key_source = config->api_key_env;
            LOG_DEBUG("[Provider] Using API key from environment variable: %s", config->api_key_env);
        } else {
            LOG_WARN("[Provider] api_key_env specified (%s) but environment variable is not set or empty",
                     config->api_key_env);
        }
    }
    if (!api_key && config->api_key[0] != '\0') {
        api_key = config->api_key;
        api_key_source = "config file";
        LOG_WARN("[Provider] Using API key from config file - consider using environment variable for better security");
    }
    if (!api_key) {
        api_key = getenv("OPENAI_API_KEY");
        if (api_key && api_key[0] != '\0') {
            api_key_source = "OPENAI_API_KEY";
        }
    }

    // Get API base URL from config
    const char *api_base = config->api_base[0] != '\0' ? config->api_base : NULL;

    // Determine provider type
    LLMProviderType provider_type = config->provider_type;

    // Log the configuration
    provider_log_config("Provider Switch",
                        provider_key,
                        config_provider_type_to_string(provider_type),
                        model,
                        api_base,
                        api_key,
                        api_key_source,
                        config->use_bedrock);

    // Handle Bedrock provider
    if (config->use_bedrock || provider_type == PROVIDER_BEDROCK) {
        LOG_INFO("Creating Bedrock provider from config...");

        Provider *prov = bedrock_provider_create(model);
        if (!prov) {
            result->error_message = strdup(
                "Failed to initialize Bedrock provider (check logs for details)");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            return;
        }
        BedrockConfig *cfg = (BedrockConfig *)prov->config;
        if (!cfg || !cfg->endpoint) {
            result->error_message = strdup(
                "Bedrock provider initialized but endpoint is missing");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(cfg->endpoint);
        if (!result->api_url) {
            result->error_message = strdup(
                "Failed to allocate memory for Bedrock endpoint");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        LOG_INFO("Provider initialization successful: Bedrock (endpoint: %s)", result->api_url);
        return;
    }

    // Non-Bedrock providers require API key (except OAuth-based providers)
    if (!api_key || api_key[0] == '\0') {
        /* OAuth providers don't need an API key */
        if (provider_type != PROVIDER_KIMI_CODING_PLAN &&
            provider_type != PROVIDER_OPENAI_SUB &&
            provider_type != PROVIDER_ANTHROPIC_SUB) {
            result->error_message = strdup(
                "API key is required. Set api_key_env in provider config or OPENAI_API_KEY environment variable");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            return;
        }
    }

    // Determine base URL - use config or fall back to defaults
    char *base_url = NULL;
    if (api_base) {
        base_url = strdup(api_base);
    } else if (provider_type == PROVIDER_ANTHROPIC) {
        base_url = strdup(DEFAULT_ANTHROPIC_URL);
    } else {
        // Fall back to environment or default OpenAI URL
        base_url = get_api_url_from_env();
    }

    if (!base_url) {
        result->error_message = strdup("Failed to determine API base URL");
        LOG_ERROR("Provider init from config failed: %s", result->error_message);
        return;
    }

    // Create provider based on type
    if (provider_type == PROVIDER_DEEPSEEK) {
        LOG_INFO("Creating DeepSeek provider from config...");
        Provider *prov = deepseek_provider_create(api_key, base_url);
        free(base_url);
        if (!prov) {
            result->error_message = strdup(
                "Failed to initialize DeepSeek provider (check logs for details)");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            return;
        }
        OpenAIConfig *cfg = (OpenAIConfig *)prov->config;
        if (!cfg || !cfg->base_url) {
            result->error_message = strdup(
                "DeepSeek provider initialized but base URL is missing");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(cfg->base_url);
        if (!result->api_url) {
            result->error_message = strdup("Failed to allocate memory for API URL");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        LOG_INFO("Provider initialization successful: DeepSeek (base URL: %s)", result->api_url);
        return;
    }

    if (provider_type == PROVIDER_MOONSHOT) {
        LOG_INFO("Creating Moonshot provider from config...");
        // Use create_with_headers if extra_headers are present in config
        Provider *prov;
        if (config && config->extra_headers[0] != '\0') {
            prov = moonshot_provider_create_with_headers(api_key, base_url, config->extra_headers);
        } else {
            prov = moonshot_provider_create(api_key, base_url);
        }
        free(base_url);
        if (!prov) {
            result->error_message = strdup(
                "Failed to initialize Moonshot provider (check logs for details)");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            return;
        }
        OpenAIConfig *cfg = (OpenAIConfig *)prov->config;
        if (!cfg || !cfg->base_url) {
            result->error_message = strdup(
                "Moonshot provider initialized but base URL is missing");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(cfg->base_url);
        if (!result->api_url) {
            result->error_message = strdup("Failed to allocate memory for API URL");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        LOG_INFO("Provider initialization successful: Moonshot (base URL: %s)", result->api_url);
        return;
    }

    if (provider_type == PROVIDER_ZAI_CODING) {
        LOG_INFO("Creating Z.AI GLM Coding Plan provider from config...");
        // Use create_with_headers if extra_headers are present in config
        Provider *prov;
        if (config && config->extra_headers[0] != '\0') {
            prov = zai_coding_provider_create_with_headers(api_key, base_url, config->extra_headers);
        } else {
            prov = zai_coding_provider_create(api_key, base_url);
        }
        free(base_url);
        if (!prov) {
            result->error_message = strdup(
                "Failed to initialize Z.AI Coding Plan provider (check logs for details)");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            return;
        }
        OpenAIConfig *cfg = (OpenAIConfig *)prov->config;
        if (!cfg || !cfg->base_url) {
            result->error_message = strdup(
                "Z.AI Coding Plan provider initialized but base URL is missing");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(cfg->base_url);
        if (!result->api_url) {
            result->error_message = strdup("Failed to allocate memory for API URL");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        LOG_INFO("Provider initialization successful: Z.AI GLM Coding (base URL: %s)", result->api_url);
        return;
    }

    if (provider_type == PROVIDER_MINIMAX_CODING) {
        LOG_INFO("Creating MiniMax Coding Plan provider from config...");
        Provider *prov = minimax_coding_provider_create(api_key, base_url);
        free(base_url);
        if (!prov) {
            result->error_message = strdup(
                "Failed to initialize MiniMax Coding Plan provider (check logs for details)");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            return;
        }
        AnthropicConfig *cfg = (AnthropicConfig *)prov->config;
        if (!cfg || !cfg->base_url) {
            result->error_message = strdup(
                "MiniMax Coding Plan provider initialized but base URL is missing");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(cfg->base_url);
        if (!result->api_url) {
            result->error_message = strdup("Failed to allocate memory for API URL");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        LOG_INFO("Provider initialization successful: MiniMax Coding Plan (base URL: %s)", result->api_url);
        return;
    }

    if (provider_type == PROVIDER_KIMI_CODING_PLAN) {
        LOG_INFO("Creating Kimi Coding Plan provider from config...");
        free(base_url);  // Kimi Coding Plan provider manages its own URL
        Provider *prov = kimi_coding_plan_provider_create(model);
        if (!prov) {
            result->error_message = strdup(
                "Failed to initialize Kimi Coding Plan provider (check logs for details)");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            return;
        }
        KimiCodingPlanConfig *cfg = (KimiCodingPlanConfig *)prov->config;
        if (!cfg || !cfg->api_base) {
            result->error_message = strdup(
                "Kimi Coding Plan provider initialized but API base is missing");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(cfg->api_base);
        if (!result->api_url) {
            result->error_message = strdup("Failed to allocate memory for API URL");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        LOG_INFO("Provider initialization successful: Kimi Coding Plan (API base: %s)", result->api_url);
        return;
    }

    if (provider_type == PROVIDER_OPENAI_SUB) {
        LOG_INFO("Creating OpenAI Subscription provider from config...");
        const char *api_base_to_use = (config->api_base[0] != '\0') ? config->api_base : NULL;
        free(base_url);  /* OpenAI Sub provider manages its own URL */
        Provider *prov = openai_sub_provider_create(model, api_base_to_use);
        if (!prov) {
            result->error_message = strdup(
                "Failed to initialize OpenAI Subscription provider (check logs for details)");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            return;
        }
        OpenAISubConfig *sub_cfg = (OpenAISubConfig *)prov->config;
        if (!sub_cfg || !sub_cfg->api_base) {
            result->error_message = strdup(
                "OpenAI Subscription provider initialized but API base is missing");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(sub_cfg->api_base);
        if (!result->api_url) {
            result->error_message = strdup("Failed to allocate memory for API URL");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        LOG_INFO("Provider initialization successful: OpenAI Subscription (API base: %s)", result->api_url);
        return;
    }

    if (provider_type == PROVIDER_ANTHROPIC_SUB) {
        LOG_INFO("Creating Anthropic Subscription provider from config...");
        const char *api_base_to_use = (config->api_base[0] != '\0') ? config->api_base : NULL;
        free(base_url);  /* Anthropic Sub provider manages its own URL */
        Provider *prov = anthropic_sub_provider_create(model, api_base_to_use);
        if (!prov) {
            result->error_message = strdup(
                "Failed to initialize Anthropic Subscription provider "
                "(ensure ~/.claude/.credentials.json exists — run 'claude auth login')");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            return;
        }
        AnthropicSubConfig *sub_cfg = (AnthropicSubConfig *)prov->config;
        if (!sub_cfg || !sub_cfg->api_base) {
            result->error_message = strdup(
                "Anthropic Subscription provider initialized but API base is missing");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(sub_cfg->api_base);
        if (!result->api_url) {
            result->error_message = strdup("Failed to allocate memory for API URL");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        LOG_INFO("Provider initialization successful: Anthropic Subscription (API base: %s)",
                 result->api_url);
        return;
    }

    if (provider_type == PROVIDER_OPENAI_RESPONSES) {
        LOG_INFO("Creating OpenAI Responses API provider from config...");
        const char *api_base_to_use = (config->api_base[0] != '\0') ? config->api_base : NULL;
        const char *api_key_to_use  = (config->api_key[0]  != '\0') ? config->api_key  : NULL;
        free(base_url);  /* Responses provider manages its own URL */
        Provider *prov = openai_responses_provider_create(api_key_to_use, api_base_to_use, model);
        if (!prov) {
            result->error_message = strdup(
                "Failed to initialize OpenAI Responses provider "
                "(ensure OPENAI_API_KEY is set or api_key is in the provider config)");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            return;
        }
        OpenAIResponsesProviderConfig *resp_cfg =
            (OpenAIResponsesProviderConfig *)prov->config;
        if (!resp_cfg || !resp_cfg->api_base) {
            result->error_message = strdup(
                "OpenAI Responses provider initialized but API base is missing");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(resp_cfg->api_base);
        if (!result->api_url) {
            result->error_message = strdup("Failed to allocate memory for API URL");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        LOG_INFO("Provider initialization successful: OpenAI Responses API (url: %s)",
                 result->api_url);
        return;
    }

    int use_anthropic = 0;
    if (provider_type == PROVIDER_ANTHROPIC) {
        use_anthropic = 1;
    } else if (provider_type == PROVIDER_AUTO) {
        // Auto-detect based on URL
        if (strstr(base_url, "anthropic.com") != NULL || strstr(base_url, "/anthropic") != NULL) {
            use_anthropic = 1;
        }
    }

    if (use_anthropic) {
        LOG_INFO("Creating Anthropic provider from config...");
        Provider *prov = anthropic_provider_create(api_key, base_url);
        free(base_url);
        if (!prov) {
            result->error_message = strdup(
                "Failed to initialize Anthropic provider (check logs for details)");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            return;
        }
        AnthropicConfig *cfg = (AnthropicConfig *)prov->config;
        if (!cfg || !cfg->base_url) {
            result->error_message = strdup(
                "Anthropic provider initialized but base URL is missing");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(cfg->base_url);
        if (!result->api_url) {
            result->error_message = strdup("Failed to allocate memory for API URL");
            LOG_ERROR("Provider init from config failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        LOG_INFO("Provider initialization successful: Anthropic (endpoint: %s)", result->api_url);
        return;
    }

    // Default to OpenAI-compatible provider
    LOG_INFO("Creating OpenAI-compatible provider from config...");
    Provider *prov = openai_provider_create(api_key, base_url);
    free(base_url);
    if (!prov) {
        result->error_message = strdup(
            "Failed to initialize OpenAI provider (check logs for details)");
        LOG_ERROR("Provider init from config failed: %s", result->error_message);
        return;
    }
    OpenAIConfig *cfg = (OpenAIConfig *)prov->config;

    // Apply extra_headers from provider config if present
    if (cfg && config && config->extra_headers[0] != '\0') {
        LOG_INFO("[Provider] Loading extra_headers from config: %s", config->extra_headers);

        // Parse comma-separated headers
        char *extra_headers_copy = strdup(config->extra_headers);
        if (extra_headers_copy) {
            // Count headers
            int count = 0;
            char *temp = strdup(config->extra_headers);
            if (temp) {
                char *token = strtok(temp, ",");
                while (token) {
                    count++;
                    token = strtok(NULL, ",");
                }
                free(temp);
            }

            // Allocate array
            cfg->extra_headers = calloc((size_t)count + 1, sizeof(char*));
            if (cfg->extra_headers) {
                cfg->extra_headers_count = count;
                int idx = 0;
                char *token = strtok(extra_headers_copy, ",");
                while (token && idx < count) {
                    cfg->extra_headers[idx] = strdup(token);
                    LOG_DEBUG("[Provider] Added extra header: %s", token);
                    idx++;
                    token = strtok(NULL, ",");
                }
            }
            free(extra_headers_copy);
        }
    }

    if (!cfg || !cfg->base_url) {
        result->error_message = strdup(
            "OpenAI provider initialized but base URL is missing");
        LOG_ERROR("Provider init from config failed: %s", result->error_message);
        prov->cleanup(prov);
        return;
    }
    result->provider = prov;
    result->api_url = strdup(cfg->base_url);
    if (!result->api_url) {
        result->error_message = strdup("Failed to allocate memory for API URL");
        LOG_ERROR("Provider init from config failed: %s", result->error_message);
        prov->cleanup(prov);
        return;
    }
    LOG_INFO("Provider initialization successful: OpenAI (base URL: %s)", result->api_url);
}
