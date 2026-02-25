/*
 * provider_config_loader.h - Unified Provider Configuration Loading
 *
 * This module provides a robust, single-source-of-truth approach to loading
 * LLM provider configuration. It eliminates the confusion between:
 * - Method A: Environment variables (OPENAI_API_KEY, OPENAI_MODEL, etc.)
 * - Method B: Provider configuration from config.json files
 *
 * The solution:
 * 1. Load config files once (global + local)
 * 2. Convert environment variables into a synthetic "env" provider config
 * 3. Merge into a unified provider array
 * 4. Use provider config as the single source of truth
 *
 * Priority for selecting which provider to use:
 * 1. KLAWED_LLM_PROVIDER env var (selects a named provider from config)
 * 2. active_provider from config file
 * 3. "env" provider (synthesized from OPENAI_*, ANTHROPIC_*, KLAWED_* env vars)
 * 4. Legacy llm_provider from config file
 */

#ifndef PROVIDER_CONFIG_LOADER_H
#define PROVIDER_CONFIG_LOADER_H

#include "config.h"
#include <stddef.h>

// Name of the synthetic provider created from environment variables
#define PROVIDER_CONFIG_ENV_NAME "env"

// Maximum number of providers after adding synthetic env provider
#define PROVIDER_CONFIG_MAX_PROVIDERS (CONFIG_MAX_PROVIDERS + 2)

// Unified provider configuration structure
typedef struct {
    KlawedConfig base_config;                    // Base config from files

    // Unified provider array (includes file providers + synthetic env provider)
    NamedProviderConfig unified_providers[PROVIDER_CONFIG_MAX_PROVIDERS];
    int unified_provider_count;

    // The effective provider to use (points into unified_providers or is NULL)
    const NamedProviderConfig *effective_provider;

    // Source of the effective provider selection
    enum {
        PROVIDER_SOURCE_NONE = 0,       // No provider configured
        PROVIDER_SOURCE_ENV_VAR,        // Selected via KLAWED_LLM_PROVIDER
        PROVIDER_SOURCE_ACTIVE_CONFIG,  // Selected via active_provider in config
        PROVIDER_SOURCE_ENV_SYNTHETIC,  // Using synthetic "env" provider
        PROVIDER_SOURCE_LEGACY          // Using legacy llm_provider from config
    } effective_source;

    // Whether the configuration is valid and ready to use
    int is_valid;
} UnifiedProviderConfig;

/**
 * Initialize and load unified provider configuration
 *
 * This function:
 * 1. Loads configuration from global and local config files
 * 2. Creates a synthetic "env" provider from environment variables
 * 3. Determines the effective provider to use based on priority
 *
 * @param config Pointer to UnifiedProviderConfig to populate
 * @return 0 on success, -1 on failure (config will still have defaults)
 */
int provider_config_load(UnifiedProviderConfig *config);

/**
 * Initialize with defaults only (no file loading, no env var processing)
 *
 * @param config Pointer to UnifiedProviderConfig to initialize
 */
void provider_config_init_defaults(UnifiedProviderConfig *config);

/**
 * Get the effective provider configuration
 *
 * Returns the provider that should be used based on:
 * 1. KLAWED_LLM_PROVIDER env var
 * 2. active_provider from config
 * 3. Synthetic "env" provider
 * 4. Legacy llm_provider
 *
 * @param config Pointer to UnifiedProviderConfig
 * @return Pointer to effective provider config, or NULL if none configured
 */
const LLMProviderConfig* provider_config_get_effective(const UnifiedProviderConfig *config);

/**
 * Get the name/key of the effective provider
 *
 * @param config Pointer to UnifiedProviderConfig
 * @return Provider key/name (static string, don't free), or NULL
 */
const char* provider_config_get_effective_name(const UnifiedProviderConfig *config);

/**
 * Get the source of the effective provider selection
 *
 * @param config Pointer to UnifiedProviderConfig
 * @return Human-readable string describing the source (e.g., "KLAWED_LLM_PROVIDER", "active_provider", "environment variables")
 */
const char* provider_config_get_effective_source(const UnifiedProviderConfig *config);

/**
 * Find a provider by key in the unified configuration
 *
 * @param config Pointer to UnifiedProviderConfig
 * @param key Provider key to find
 * @return Pointer to provider config, or NULL if not found
 */
const NamedProviderConfig* provider_config_find_provider(const UnifiedProviderConfig *config,
                                                          const char *key);

/**
 * Check if the effective provider uses Bedrock
 *
 * @param config Pointer to UnifiedProviderConfig
 * @return 1 if bedrock, 0 otherwise
 */
int provider_config_is_bedrock(const UnifiedProviderConfig *config);

/**
 * Check if the effective provider has an API key configured
 *
 * @param config Pointer to UnifiedProviderConfig
 * @return 1 if API key is configured, 0 otherwise
 */
int provider_config_has_api_key(const UnifiedProviderConfig *config);

/**
 * Get the model from the effective provider
 *
 * @param config Pointer to UnifiedProviderConfig
 * @return Model name (static string from config, don't free), or NULL
 */
const char* provider_config_get_model(const UnifiedProviderConfig *config);

/**
 * Validate the provider configuration
 *
 * Checks if the effective provider configuration is valid and complete.
 * Returns an error message if invalid, or NULL if valid.
 *
 * @param config Pointer to UnifiedProviderConfig
 * @return Error message (caller must free), or NULL if valid
 */
char* provider_config_validate(const UnifiedProviderConfig *config);

/**
 * Get a provider by key, creating it if it doesn't exist
 *
 * This is used for runtime provider management (e.g., /provider command).
 *
 * @param config Pointer to UnifiedProviderConfig
 * @param key Provider key
 * @return Pointer to provider config, or NULL if at max providers
 */
LLMProviderConfig* provider_config_get_or_create_provider(UnifiedProviderConfig *config,
                                                           const char *key);

/**
 * Set the active provider
 *
 * @param config Pointer to UnifiedProviderConfig
 * @param key Provider key to set as active
 * @return 0 on success, -1 if provider not found
 */
int provider_config_set_active(UnifiedProviderConfig *config, const char *key);

/**
 * Get the API key for the effective provider
 *
 * This resolves api_key_env if set, otherwise returns api_key.
 * For Bedrock providers, returns a placeholder.
 *
 * @param config Pointer to UnifiedProviderConfig
 * @param source_out Optional output for key source description
 * @return API key (static string, don't free), or NULL if not available
 */
const char* provider_config_resolve_api_key(const UnifiedProviderConfig *config,
                                             const char **source_out);

/**
 * Get the API base URL for the effective provider
 *
 * @param config Pointer to UnifiedProviderConfig
 * @return API base URL (static string, don't free), or NULL if not set
 */
const char* provider_config_get_api_base(const UnifiedProviderConfig *config);

/**
 * Get the provider type for the effective provider
 *
 * @param config Pointer to UnifiedProviderConfig
 * @return Provider type enum value
 */
LLMProviderType provider_config_get_provider_type(const UnifiedProviderConfig *config);

/**
 * Check if a provider with the given key exists
 *
 * @param config Pointer to UnifiedProviderConfig
 * @param key Provider key to check
 * @return 1 if exists, 0 otherwise
 */
int provider_config_provider_exists(const UnifiedProviderConfig *config, const char *key);

#endif // PROVIDER_CONFIG_LOADER_H
