/*
 * Config - User configuration persistence for klawed
 *
 * Stores user preferences in .klawed/config.json
 * Designed to be extended with additional settings.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "tui.h"  // For TUIInputBoxStyle enum

// Maximum length for theme string (including NUL)
#define CONFIG_THEME_MAX 256

// Maximum length for provider configuration strings
#define CONFIG_PROVIDER_NAME_MAX 32
#define CONFIG_MODEL_MAX 128
#define CONFIG_API_BASE_MAX 256
#define CONFIG_API_KEY_MAX 256
#define CONFIG_API_KEY_ENV_MAX 128

// Maximum number of provider configurations
#define CONFIG_MAX_PROVIDERS 15
// Maximum length for provider key/name
#define CONFIG_PROVIDER_KEY_MAX 64

// LLM Provider types
typedef enum {
    PROVIDER_AUTO = 0,      // Auto-detect based on URL/model
    PROVIDER_OPENAI,        // OpenAI-compatible API
    PROVIDER_ANTHROPIC,     // Anthropic API
    PROVIDER_BEDROCK,       // AWS Bedrock
    PROVIDER_DEEPSEEK,      // DeepSeek API (OpenAI-compatible, discards reasoning_content)
    PROVIDER_MOONSHOT,      // Moonshot/Kimi API (OpenAI-compatible, preserves reasoning_content)
    PROVIDER_KIMI_CODING_PLAN, // Kimi Coding Plan (OAuth device flow, preserves reasoning_content)
    PROVIDER_CUSTOM         // Custom provider
} LLMProviderType;

// LLM Provider configuration
typedef struct {
    LLMProviderType provider_type;          // Provider type
    char provider_name[CONFIG_PROVIDER_NAME_MAX]; // Provider name for display
    char model[CONFIG_MODEL_MAX];           // Model name
    char api_base[CONFIG_API_BASE_MAX];     // API base URL
    char api_key[CONFIG_API_KEY_MAX];       // API key (optional, prefer env var)
    char api_key_env[CONFIG_API_KEY_ENV_MAX]; // Environment variable name for API key
    int use_bedrock;                        // Use AWS Bedrock (legacy flag)
} LLMProviderConfig;

// Named provider configuration (for multiple providers)
typedef struct {
    char key[CONFIG_PROVIDER_KEY_MAX];      // Unique key/name for this provider config
    LLMProviderConfig config;               // Provider configuration
} NamedProviderConfig;

// Configuration structure
typedef struct {
    TUIInputBoxStyle input_box_style;
    TUIResponseStyle response_style;
    char theme[CONFIG_THEME_MAX];
    LLMProviderConfig llm_provider;         // LLM provider configuration (legacy single-provider)
    NamedProviderConfig providers[CONFIG_MAX_PROVIDERS]; // Multiple named providers
    int provider_count;                     // Number of providers in the array
    char active_provider[CONFIG_PROVIDER_KEY_MAX]; // Key of active provider
} KlawedConfig;

/**
 * Load configuration from .klawed/config.json
 *
 * @param config Pointer to config struct to populate
 * @return 0 on success, -1 on failure (config will have defaults)
 */
int config_load(KlawedConfig *config);

/**
 * Save configuration to .klawed/config.json
 *
 * @param config Pointer to config struct to save
 * @return 0 on success, -1 on failure
 */
int config_save(const KlawedConfig *config);

/**
 * Initialize config with default values
 *
 * @param config Pointer to config struct to initialize
 */
void config_init_defaults(KlawedConfig *config);

/**
 * Get input box style name as string
 *
 * @param style The input box style enum value
 * @return String name of the style
 */
const char* config_input_style_to_string(TUIInputBoxStyle style);

/**
 * Parse input box style from string
 *
 * @param str String name of the style
 * @return The style enum value, or INPUT_STYLE_BLAND if unknown
 */
TUIInputBoxStyle config_input_style_from_string(const char *str);

/**
 * Get response style name as string
 *
 * @param style The response style enum value
 * @return String name of the style
 */
const char* config_response_style_to_string(TUIResponseStyle style);

/**
 * Parse response style from string
 *
 * @param str String name of the style
 * @return The style enum value, or RESPONSE_STYLE_BORDER if unknown
 */
TUIResponseStyle config_response_style_from_string(const char *str);

/**
 * Get provider type name as string
 *
 * @param type The provider type enum value
 * @return String name of the provider type
 */
const char* config_provider_type_to_string(LLMProviderType type);

/**
 * Parse provider type from string
 *
 * @param str String name of the provider type
 * @return The provider type enum value, or PROVIDER_AUTO if unknown
 */
LLMProviderType config_provider_type_from_string(const char *str);

/**
 * Find a provider configuration by key
 *
 * @param config Pointer to config struct
 * @param key Provider key to find
 * @return Pointer to provider config if found, NULL otherwise
 */
const NamedProviderConfig* config_find_provider(const KlawedConfig *config, const char *key);

/**
 * Get the active provider configuration
 *
 * @param config Pointer to config struct
 * @return Pointer to active provider config, or NULL if not found
 */
const NamedProviderConfig* config_get_active_provider(const KlawedConfig *config);

/**
 * Add or update a provider configuration
 *
 * @param config Pointer to config struct
 * @param key Provider key
 * @param provider_config Provider configuration to add/update
 * @return 0 on success, -1 on failure (e.g., max providers reached)
 */
int config_set_provider(KlawedConfig *config, const char *key, const LLMProviderConfig *provider_config);

/**
 * Remove a provider configuration
 *
 * @param config Pointer to config struct
 * @param key Provider key to remove
 * @return 0 on success, -1 if not found
 */
int config_remove_provider(KlawedConfig *config, const char *key);

/**
 * Set the active provider
 *
 * @param config Pointer to config struct
 * @param key Provider key to set as active
 * @return 0 on success, -1 if provider not found
 */
int config_set_active_provider(KlawedConfig *config, const char *key);

#endif /* CONFIG_H */
