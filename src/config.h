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

// LLM Provider types
typedef enum {
    PROVIDER_AUTO = 0,      // Auto-detect based on URL/model
    PROVIDER_OPENAI,        // OpenAI-compatible API
    PROVIDER_ANTHROPIC,     // Anthropic API
    PROVIDER_BEDROCK,       // AWS Bedrock
    PROVIDER_CUSTOM         // Custom provider
} LLMProviderType;

// LLM Provider configuration
typedef struct {
    LLMProviderType provider_type;          // Provider type
    char provider_name[CONFIG_PROVIDER_NAME_MAX]; // Provider name for display
    char model[CONFIG_MODEL_MAX];           // Model name
    char api_base[CONFIG_API_BASE_MAX];     // API base URL
    char api_key[CONFIG_API_KEY_MAX];       // API key (optional, prefer env var)
    int use_bedrock;                        // Use AWS Bedrock (legacy flag)
} LLMProviderConfig;

// Configuration structure
typedef struct {
    TUIInputBoxStyle input_box_style;
    char theme[CONFIG_THEME_MAX];
    LLMProviderConfig llm_provider;         // LLM provider configuration
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

#endif /* CONFIG_H */
