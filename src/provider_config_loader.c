/*
 * provider_config_loader.c - Unified Provider Configuration Loading
 *
 * Implements a robust, single-source-of-truth approach to loading
 * LLM provider configuration.
 */

#define _GNU_SOURCE

#include "provider_config_loader.h"
#include "config.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

// Default Anthropic API URL
#define DEFAULT_ANTHROPIC_URL "https://api.anthropic.com/v1/messages"
#define DEFAULT_OPENAI_URL "https://api.openai.com/v1/chat/completions"

/**
 * Initialize with defaults only (no file loading, no env var processing)
 */
void provider_config_init_defaults(UnifiedProviderConfig *config) {
    if (!config) return;

    memset(config, 0, sizeof(UnifiedProviderConfig));

    // Initialize base config with defaults
    config_init_defaults(&config->base_config);

    config->unified_provider_count = 0;
    config->effective_provider = NULL;
    config->effective_source = PROVIDER_SOURCE_NONE;
    config->is_valid = 0;
}

/**
 * Create a synthetic provider from environment variables
 *
 * This function reads OPENAI_*, ANTHROPIC_*, KLAWED_* env vars
 * and creates an LLMProviderConfig from them.
 */
static void create_synthetic_env_provider(LLMProviderConfig *provider) {
    if (!provider) return;

    memset(provider, 0, sizeof(LLMProviderConfig));

    // Set provider type based on environment
    const char *env_use_bedrock = getenv("KLAWED_USE_BEDROCK");
    if (env_use_bedrock && (strcmp(env_use_bedrock, "1") == 0 ||
                           strcasecmp(env_use_bedrock, "true") == 0 ||
                           strcasecmp(env_use_bedrock, "yes") == 0)) {
        provider->provider_type = PROVIDER_BEDROCK;
        provider->use_bedrock = 1;
    } else {
        // Auto-detect based on URL patterns and env vars
        const char *openai_base = getenv("OPENAI_API_BASE");
        const char *anthropic_url = getenv("ANTHROPIC_API_URL");
        if (!anthropic_url) anthropic_url = getenv("ANTHROPIC_BASE_URL");

        if (anthropic_url && anthropic_url[0] != '\0' &&
            (!openai_base || openai_base[0] == '\0')) {
            provider->provider_type = PROVIDER_ANTHROPIC;
        } else {
            provider->provider_type = PROVIDER_AUTO;
        }
    }

    strlcpy(provider->provider_name, "env", CONFIG_PROVIDER_NAME_MAX);

    // Get model from environment
    const char *env_model = getenv("OPENAI_MODEL");
    if (!env_model || env_model[0] == '\0') {
        env_model = getenv("ANTHROPIC_MODEL");
    }
    if (env_model && env_model[0] != '\0') {
        strlcpy(provider->model, env_model, CONFIG_MODEL_MAX);
    }

    // Get API base from environment
    const char *env_api_base = getenv("OPENAI_API_BASE");
    if (!env_api_base || env_api_base[0] == '\0') {
        env_api_base = getenv("ANTHROPIC_API_URL");
    }
    if (!env_api_base || env_api_base[0] == '\0') {
        env_api_base = getenv("ANTHROPIC_BASE_URL");
    }
    if (env_api_base && env_api_base[0] != '\0') {
        strlcpy(provider->api_base, env_api_base, CONFIG_API_BASE_MAX);
    }

    // API key is not stored directly - we use api_key_env or look up at runtime
    // The synthetic provider uses "OPENAI_API_KEY" as the env var name
    strlcpy(provider->api_key_env, "OPENAI_API_KEY", CONFIG_API_KEY_ENV_MAX);

    LOG_DEBUG("[ProviderConfig] Created synthetic env provider: type=%s, model=%s, api_base=%s",
              config_provider_type_to_string(provider->provider_type),
              provider->model[0] ? provider->model : "(not set)",
              provider->api_base[0] ? provider->api_base : "(not set)");
}

/**
 * Create a synthetic provider from legacy llm_provider config
 */
static void create_synthetic_legacy_provider(const LLMProviderConfig *legacy,
                                              LLMProviderConfig *provider) {
    if (!provider || !legacy) return;

    memcpy(provider, legacy, sizeof(LLMProviderConfig));
}

/**
 * Build the unified provider array
 *
 * This creates a merged list of providers from:
 * 1. Config file providers
 * 2. Synthetic env provider (if env vars are set)
 * 3. Synthetic legacy provider (if legacy config has values)
 */
static void build_unified_providers(UnifiedProviderConfig *config) {
    if (!config) return;

    config->unified_provider_count = 0;

    // First, add all providers from config file
    for (int i = 0; i < config->base_config.provider_count; i++) {
        if (config->unified_provider_count >= PROVIDER_CONFIG_MAX_PROVIDERS) {
            LOG_WARN("[ProviderConfig] Maximum providers reached, skipping some");
            break;
        }
        memcpy(&config->unified_providers[config->unified_provider_count],
               &config->base_config.providers[i],
               sizeof(NamedProviderConfig));
        config->unified_provider_count++;
    }

    // Add synthetic env provider if environment variables are set
    LLMProviderConfig env_provider;
    create_synthetic_env_provider(&env_provider);

    // Check if we have any meaningful env var configuration
    int has_env_config = (env_provider.model[0] != '\0' ||
                          env_provider.api_base[0] != '\0' ||
                          getenv("OPENAI_API_KEY") != NULL ||
                          env_provider.use_bedrock);

    if (has_env_config) {
        if (config->unified_provider_count < PROVIDER_CONFIG_MAX_PROVIDERS) {
            NamedProviderConfig *env_named = &config->unified_providers[config->unified_provider_count];
            strlcpy(env_named->key, PROVIDER_CONFIG_ENV_NAME, CONFIG_PROVIDER_KEY_MAX);
            memcpy(&env_named->config, &env_provider, sizeof(LLMProviderConfig));
            config->unified_provider_count++;
            LOG_DEBUG("[ProviderConfig] Added synthetic 'env' provider");
        }
    }

    // Add synthetic legacy provider if legacy config has values
    if (config->base_config.llm_provider.model[0] != '\0' ||
        config->base_config.llm_provider.api_base[0] != '\0' ||
        config->base_config.llm_provider.api_key[0] != '\0') {

        if (config->unified_provider_count < PROVIDER_CONFIG_MAX_PROVIDERS) {
            NamedProviderConfig *legacy_named = &config->unified_providers[config->unified_provider_count];
            strlcpy(legacy_named->key, "legacy", CONFIG_PROVIDER_KEY_MAX);
            create_synthetic_legacy_provider(&config->base_config.llm_provider,
                                              &legacy_named->config);
            config->unified_provider_count++;
            LOG_DEBUG("[ProviderConfig] Added synthetic 'legacy' provider");
        }
    }

    LOG_DEBUG("[ProviderConfig] Built unified provider array with %d providers",
              config->unified_provider_count);
}

/**
 * Determine the effective provider based on priority
 *
 * Priority:
 * 1. KLAWED_LLM_PROVIDER env var
 * 2. active_provider from config file
 * 3. Synthetic "env" provider (if env vars are set)
 * 4. Legacy llm_provider
 */
static void determine_effective_provider(UnifiedProviderConfig *config) {
    if (!config) return;

    config->effective_provider = NULL;
    config->effective_source = PROVIDER_SOURCE_NONE;

    // Priority 1: KLAWED_LLM_PROVIDER environment variable
    const char *env_provider = getenv("KLAWED_LLM_PROVIDER");
    if (env_provider && env_provider[0] != '\0') {
        const NamedProviderConfig *named = provider_config_find_provider(config, env_provider);
        if (named) {
            config->effective_provider = named;
            config->effective_source = PROVIDER_SOURCE_ENV_VAR;
            LOG_INFO("[ProviderConfig] Selected provider '%s' from KLAWED_LLM_PROVIDER",
                     env_provider);
            return;
        } else {
            LOG_WARN("[ProviderConfig] KLAWED_LLM_PROVIDER='%s' not found in providers",
                     env_provider);
        }
    }

    // Priority 2: active_provider from config file
    if (config->base_config.active_provider[0] != '\0') {
        const NamedProviderConfig *named = provider_config_find_provider(
            config, config->base_config.active_provider);
        if (named) {
            config->effective_provider = named;
            config->effective_source = PROVIDER_SOURCE_ACTIVE_CONFIG;
            LOG_INFO("[ProviderConfig] Selected provider '%s' from active_provider config",
                     config->base_config.active_provider);
            return;
        } else {
            LOG_WARN("[ProviderConfig] active_provider='%s' not found in providers",
                     config->base_config.active_provider);
        }
    }

    // Priority 3: Synthetic "env" provider
    const NamedProviderConfig *env_named = provider_config_find_provider(config, PROVIDER_CONFIG_ENV_NAME);
    if (env_named) {
        config->effective_provider = env_named;
        config->effective_source = PROVIDER_SOURCE_ENV_SYNTHETIC;
        LOG_INFO("[ProviderConfig] Selected synthetic 'env' provider from environment variables");
        return;
    }

    // Priority 4: Legacy llm_provider
    const NamedProviderConfig *legacy_named = provider_config_find_provider(config, "legacy");
    if (legacy_named) {
        config->effective_provider = legacy_named;
        config->effective_source = PROVIDER_SOURCE_LEGACY;
        LOG_INFO("[ProviderConfig] Selected legacy provider from config");
        return;
    }

    LOG_WARN("[ProviderConfig] No effective provider configured");
}

/**
 * Load unified provider configuration
 */
int provider_config_load(UnifiedProviderConfig *config) {
    if (!config) return -1;

    // Initialize with defaults
    provider_config_init_defaults(config);

    // Load base config from files
    int load_result = config_load(&config->base_config);
    if (load_result != 0) {
        LOG_DEBUG("[ProviderConfig] No config files loaded, using defaults and env vars");
    }

    // Build unified provider array
    build_unified_providers(config);

    // Determine effective provider
    determine_effective_provider(config);

    // Mark as valid if we have an effective provider
    if (config->effective_provider != NULL) {
        config->is_valid = 1;
        LOG_INFO("[ProviderConfig] Configuration loaded successfully");
        LOG_INFO("[ProviderConfig] Effective provider: %s (source: %s)",
                 config->effective_provider->key,
                 provider_config_get_effective_source(config));
    } else {
        LOG_WARN("[ProviderConfig] No effective provider configured");
        config->is_valid = 0;
    }

    return 0;
}

/**
 * Get the effective provider configuration
 */
const LLMProviderConfig* provider_config_get_effective(const UnifiedProviderConfig *config) {
    if (!config || !config->effective_provider) return NULL;
    return &config->effective_provider->config;
}

/**
 * Get the name/key of the effective provider
 */
const char* provider_config_get_effective_name(const UnifiedProviderConfig *config) {
    if (!config || !config->effective_provider) return NULL;
    return config->effective_provider->key;
}

/**
 * Get the source of the effective provider selection
 */
const char* provider_config_get_effective_source(const UnifiedProviderConfig *config) {
    if (!config) return "unknown";

    switch (config->effective_source) {
        case PROVIDER_SOURCE_ENV_VAR:
            return "KLAWED_LLM_PROVIDER";
        case PROVIDER_SOURCE_ACTIVE_CONFIG:
            return "active_provider";
        case PROVIDER_SOURCE_ENV_SYNTHETIC:
            return "environment variables";
        case PROVIDER_SOURCE_LEGACY:
            return "legacy config";
        case PROVIDER_SOURCE_NONE:
        default:
            return "none";
    }
}

/**
 * Find a provider by key in the unified configuration
 */
const NamedProviderConfig* provider_config_find_provider(const UnifiedProviderConfig *config,
                                                          const char *key) {
    if (!config || !key || key[0] == '\0') return NULL;

    for (int i = 0; i < config->unified_provider_count; i++) {
        if (strcmp(config->unified_providers[i].key, key) == 0) {
            return &config->unified_providers[i];
        }
    }

    return NULL;
}

/**
 * Check if the effective provider uses Bedrock
 */
int provider_config_is_bedrock(const UnifiedProviderConfig *config) {
    const LLMProviderConfig *effective = provider_config_get_effective(config);
    if (!effective) return 0;

    return (effective->provider_type == PROVIDER_BEDROCK || effective->use_bedrock);
}

/**
 * Check if the effective provider uses OAuth device flow (no API key required).
 */
int provider_config_is_oauth(const UnifiedProviderConfig *config) {
    const LLMProviderConfig *effective = provider_config_get_effective(config);
    if (!effective) return 0;

    return (effective->provider_type == PROVIDER_OPENAI_SUB ||
            effective->provider_type == PROVIDER_KIMI_CODING_PLAN ||
            effective->provider_type == PROVIDER_ANTHROPIC_SUB);
}

/**
 * Check if the effective provider has an API key configured
 */
int provider_config_has_api_key(const UnifiedProviderConfig *config) {
    const LLMProviderConfig *effective = provider_config_get_effective(config);
    if (!effective) return 0;

    // Bedrock providers use AWS credentials
    if (effective->provider_type == PROVIDER_BEDROCK || effective->use_bedrock) {
        return 1;
    }

    // Check if api_key_env is set and the env var exists
    if (effective->api_key_env[0] != '\0') {
        const char *env_key = getenv(effective->api_key_env);
        if (env_key && env_key[0] != '\0') {
            return 1;
        }
    }

    // Check if api_key is directly set
    if (effective->api_key[0] != '\0') {
        return 1;
    }

    // Check for OPENAI_API_KEY in environment as fallback
    const char *openai_key = getenv("OPENAI_API_KEY");
    if (openai_key && openai_key[0] != '\0') {
        return 1;
    }

    return 0;
}

/**
 * Get the model from the effective provider
 */
const char* provider_config_get_model(const UnifiedProviderConfig *config) {
    const LLMProviderConfig *effective = provider_config_get_effective(config);
    if (!effective) return NULL;

    if (effective->model[0] != '\0') {
        return effective->model;
    }

    return NULL;
}

/**
 * Validate the provider configuration
 */
char* provider_config_validate(const UnifiedProviderConfig *config) {
    if (!config) {
        return strdup("Configuration is NULL");
    }

    if (!config->is_valid) {
        return strdup("No provider configuration found. Set OPENAI_API_KEY environment variable or configure a provider in .klawed/config.json");
    }

    const LLMProviderConfig *effective = provider_config_get_effective(config);
    if (!effective) {
        return strdup("No effective provider configured");
    }

    // Check if model is set
    if (effective->model[0] == '\0') {
        return strdup("No model configured. Set OPENAI_MODEL environment variable or configure a model in .klawed/config.json");
    }

    // Check if API key is available (unless bedrock or oauth)
    if (!provider_config_has_api_key(config) &&
        !provider_config_is_oauth(config)) {
        return strdup("No API key configured. Set OPENAI_API_KEY environment variable or configure api_key/api_key_env in .klawed/config.json");
    }

    // If KLAWED_LLM_PROVIDER is set but provider not found, return error
    const char *env_provider = getenv("KLAWED_LLM_PROVIDER");
    if (env_provider && env_provider[0] != '\0') {
        if (!provider_config_find_provider(config, env_provider)) {
            char *error_msg = NULL;
            if (asprintf(&error_msg,
                         "Provider '%s' specified in KLAWED_LLM_PROVIDER not found in configuration.\n"
                         "Available providers:", env_provider) < 0) {
                return strdup("Invalid KLAWED_LLM_PROVIDER");
            }

            // Build list of available providers
            for (int i = 0; i < config->unified_provider_count; i++) {
                char *new_msg = NULL;
                if (asprintf(&new_msg, "%s\n  - %s", error_msg, config->unified_providers[i].key) < 0) {
                    free(error_msg);
                    return strdup("Invalid KLAWED_LLM_PROVIDER");
                }
                free(error_msg);
                error_msg = new_msg;
            }

            char *final_msg = NULL;
            if (asprintf(&final_msg,
                         "%s\n\nPlease check your configuration file or unset KLAWED_LLM_PROVIDER.",
                         error_msg) < 0) {
                free(error_msg);
                return strdup("Invalid KLAWED_LLM_PROVIDER");
            }
            free(error_msg);
            return final_msg;
        }
    }

    return NULL;  // Valid
}

/**
 * Get a provider by key, creating it if it doesn't exist
 */
LLMProviderConfig* provider_config_get_or_create_provider(UnifiedProviderConfig *config,
                                                           const char *key) {
    if (!config || !key || key[0] == '\0') return NULL;

    // Check if provider exists in unified providers (we return non-const pointer to allow modification)
    for (int i = 0; i < config->unified_provider_count; i++) {
        if (strcmp(config->unified_providers[i].key, key) == 0) {
            return &config->unified_providers[i].config;
        }
    }

    // Check if we can add a new provider
    if (config->unified_provider_count >= PROVIDER_CONFIG_MAX_PROVIDERS) {
        LOG_ERROR("[ProviderConfig] Maximum providers reached");
        return NULL;
    }

    // Also add to base config for persistence
    if (config->base_config.provider_count < CONFIG_MAX_PROVIDERS) {
        NamedProviderConfig *base_named = &config->base_config.providers[config->base_config.provider_count];
        strlcpy(base_named->key, key, CONFIG_PROVIDER_KEY_MAX);
        memset(&base_named->config, 0, sizeof(LLMProviderConfig));
        base_named->config.provider_type = PROVIDER_AUTO;
        config->base_config.provider_count++;

        // Mirror in unified providers
        NamedProviderConfig *unified_named = &config->unified_providers[config->unified_provider_count];
        memcpy(unified_named, base_named, sizeof(NamedProviderConfig));
        config->unified_provider_count++;

        return &unified_named->config;
    }

    return NULL;
}

/**
 * Set the active provider
 */
int provider_config_set_active(UnifiedProviderConfig *config, const char *key) {
    if (!config || !key || key[0] == '\0') return -1;

    // Verify provider exists
    if (!provider_config_find_provider(config, key)) {
        LOG_ERROR("[ProviderConfig] Provider '%s' not found", key);
        return -1;
    }

    strlcpy(config->base_config.active_provider, key, CONFIG_PROVIDER_KEY_MAX);

    // Re-determine effective provider
    determine_effective_provider(config);

    return 0;
}

/**
 * Get the API key for the effective provider
 */
const char* provider_config_resolve_api_key(const UnifiedProviderConfig *config,
                                             const char **source_out) {
    const LLMProviderConfig *effective = provider_config_get_effective(config);
    if (!effective) {
        if (source_out) *source_out = NULL;
        return NULL;
    }

    // Bedrock providers use AWS credentials
    if (effective->provider_type == PROVIDER_BEDROCK || effective->use_bedrock) {
        if (source_out) *source_out = "AWS credentials";
        return "bedrock";
    }

    // Check api_key_env first
    if (effective->api_key_env[0] != '\0') {
        const char *env_key = getenv(effective->api_key_env);
        if (env_key && env_key[0] != '\0') {
            if (source_out) *source_out = effective->api_key_env;
            return env_key;
        }
    }

    // Check directly configured api_key
    if (effective->api_key[0] != '\0') {
        if (source_out) *source_out = "config file";
        return effective->api_key;
    }

    // Fall back to OPENAI_API_KEY
    const char *openai_key = getenv("OPENAI_API_KEY");
    if (openai_key && openai_key[0] != '\0') {
        if (source_out) *source_out = "OPENAI_API_KEY";
        return openai_key;
    }

    if (source_out) *source_out = NULL;
    return NULL;
}

/**
 * Get the API base URL for the effective provider
 */
const char* provider_config_get_api_base(const UnifiedProviderConfig *config) {
    const LLMProviderConfig *effective = provider_config_get_effective(config);
    if (!effective) return NULL;

    if (effective->api_base[0] != '\0') {
        return effective->api_base;
    }

    // Return defaults based on provider type
    if (effective->provider_type == PROVIDER_ANTHROPIC) {
        return DEFAULT_ANTHROPIC_URL;
    }

    return DEFAULT_OPENAI_URL;
}

/**
 * Get the provider type for the effective provider
 */
LLMProviderType provider_config_get_provider_type(const UnifiedProviderConfig *config) {
    const LLMProviderConfig *effective = provider_config_get_effective(config);
    if (!effective) return PROVIDER_AUTO;

    return effective->provider_type;
}

/**
 * Check if a provider with the given key exists
 */
int provider_config_provider_exists(const UnifiedProviderConfig *config, const char *key) {
    return provider_config_find_provider(config, key) != NULL;
}
