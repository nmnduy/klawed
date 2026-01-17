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

// Configuration structure
typedef struct {
    TUIInputBoxStyle input_box_style;
    char theme[CONFIG_THEME_MAX];
    // Add more settings here as needed
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

#endif /* CONFIG_H */
