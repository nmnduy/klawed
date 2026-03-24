/*
 * Config - User configuration persistence for klawed
 *
 * Stores user preferences in .klawed/config.json
 */

#include "config.h"
#include "logger.h"
#include "data_dir.h"
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <sys/stat.h>
#include <errno.h>

#define CONFIG_FILE_NAME "config.json"
#define GLOBAL_CONFIG_DIR_NAME ".klawed"
#define GLOBAL_CONFIG_FILE_NAME "config.json"

void config_init_defaults(KlawedConfig *config) {
    if (!config) return;
    config->input_box_style = INPUT_STYLE_HORIZONTAL;
    config->response_style = RESPONSE_STYLE_BORDER;
    config->thinking_style = THINKING_STYLE_WAVE;
    config->theme[0] = '\0';  // Empty means use default or KLAWED_THEME env var

    // Initialize LLM provider defaults (legacy single-provider)
    config->llm_provider.provider_type = PROVIDER_AUTO;
    strlcpy(config->llm_provider.provider_name, "auto", CONFIG_PROVIDER_NAME_MAX);
    config->llm_provider.model[0] = '\0';
    config->llm_provider.api_base[0] = '\0';
    config->llm_provider.api_key[0] = '\0';
    config->llm_provider.api_key_env[0] = '\0';
    config->llm_provider.extra_headers[0] = '\0';
    config->llm_provider.use_bedrock = 0;

    // Initialize multiple providers
    config->provider_count = 0;
    config->active_provider[0] = '\0';

    // Clear all provider slots
    for (int i = 0; i < CONFIG_MAX_PROVIDERS; i++) {
        config->providers[i].key[0] = '\0';
        config->providers[i].config.provider_type = PROVIDER_AUTO;
        config->providers[i].config.provider_name[0] = '\0';
        config->providers[i].config.model[0] = '\0';
        config->providers[i].config.api_base[0] = '\0';
        config->providers[i].config.api_key[0] = '\0';
        config->providers[i].config.api_key_env[0] = '\0';
        config->providers[i].config.extra_headers[0] = '\0';
        config->providers[i].config.use_bedrock = 0;
    }
}

const char* config_input_style_to_string(TUIInputBoxStyle style) {
    switch (style) {
        case INPUT_STYLE_BACKGROUND:
            return "background";
        case INPUT_STYLE_BORDER:
            return "border";
        case INPUT_STYLE_HORIZONTAL:
            return "horizontal";
        case INPUT_STYLE_BLAND:
        default:
            return "bland";
    }
}

TUIInputBoxStyle config_input_style_from_string(const char *str) {
    if (!str) return INPUT_STYLE_BLAND;

    if (strcmp(str, "background") == 0) {
        return INPUT_STYLE_BACKGROUND;
    } else if (strcmp(str, "border") == 0) {
        return INPUT_STYLE_BORDER;
    } else if (strcmp(str, "horizontal") == 0) {
        return INPUT_STYLE_HORIZONTAL;
    }
    return INPUT_STYLE_BLAND;
}

const char* config_response_style_to_string(TUIResponseStyle style) {
    switch (style) {
        case RESPONSE_STYLE_CARET:
            return "caret";
        case RESPONSE_STYLE_BORDER:
        default:
            return "border";
    }
}

TUIResponseStyle config_response_style_from_string(const char *str) {
    if (!str) return RESPONSE_STYLE_BORDER;

    if (strcmp(str, "caret") == 0) {
        return RESPONSE_STYLE_CARET;
    }
    return RESPONSE_STYLE_BORDER;
}

const char* config_thinking_style_to_string(TUIThinkingStyle style) {
    switch (style) {
        case THINKING_STYLE_PACMAN:
            return "pacman";
        case THINKING_STYLE_WAVE:
        default:
            return "wave";
    }
}

TUIThinkingStyle config_thinking_style_from_string(const char *str) {
    if (!str) return THINKING_STYLE_WAVE;

    if (strcmp(str, "pacman") == 0) {
        return THINKING_STYLE_PACMAN;
    }
    return THINKING_STYLE_WAVE;
}

/**
 * Get the path to the global config file (~/.klawed/config.json)
 *
 * @param buf Buffer to store the path
 * @param buf_size Size of the buffer
 * @return 0 on success, -1 on failure
 */
static int config_get_global_path(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return -1;

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        LOG_DEBUG("[Config] HOME environment variable not set");
        return -1;
    }

    // Build path: $HOME/.klawed/config.json
    size_t needed = strlcpy(buf, home, buf_size);
    if (needed >= buf_size) return -1;

    needed = strlcat(buf, "/", buf_size);
    if (needed >= buf_size) return -1;

    needed = strlcat(buf, GLOBAL_CONFIG_DIR_NAME, buf_size);
    if (needed >= buf_size) return -1;

    needed = strlcat(buf, "/", buf_size);
    if (needed >= buf_size) return -1;

    needed = strlcat(buf, GLOBAL_CONFIG_FILE_NAME, buf_size);
    if (needed >= buf_size) return -1;

    return 0;
}

/**
 * Load configuration from a specific file path into a config struct
 * This is a helper function that doesn't initialize defaults - caller should do that first.
 *
 * @param config Pointer to config struct to populate (should already be initialized)
 * @param file_path Path to the config file
 * @param label Label for logging (e.g., "global", "local")
 * @return 0 on success, -1 on failure
 */
static int config_load_from_file(KlawedConfig *config, const char *file_path, const char *label) {
    if (!config || !file_path) return -1;

    // Try to open config file
    FILE *fp = fopen(file_path, "r");
    if (!fp) {
        LOG_DEBUG("[Config] No %s config file found at %s", label, file_path);
        return -1;
    }

    // Read file contents
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 1024 * 1024) {  // Max 1MB
        LOG_WARN("[Config] Invalid %s config file size: %ld", label, file_size);
        fclose(fp);
        return -1;
    }

    char *json_str = malloc((size_t)file_size + 1);
    if (!json_str) {
        LOG_ERROR("[Config] Failed to allocate memory for %s config file", label);
        fclose(fp);
        return -1;
    }

    size_t bytes_read = fread(json_str, 1, (size_t)file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        LOG_WARN("[Config] Failed to read %s config file completely", label);
        free(json_str);
        return -1;
    }
    json_str[file_size] = '\0';

    // Parse JSON
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (!root) {
        LOG_WARN("[Config] Failed to parse %s config file as JSON", label);
        return -1;
    }

    // Read input_box_style
    cJSON *style_item = cJSON_GetObjectItem(root, "input_box_style");
    if (style_item && cJSON_IsString(style_item)) {
        config->input_box_style = config_input_style_from_string(style_item->valuestring);
        LOG_DEBUG("[Config] Loaded input_box_style from %s: %s", label, style_item->valuestring);
    }

    // Read response_style
    cJSON *resp_style_item = cJSON_GetObjectItem(root, "response_style");
    if (resp_style_item && cJSON_IsString(resp_style_item)) {
        config->response_style = config_response_style_from_string(resp_style_item->valuestring);
        LOG_DEBUG("[Config] Loaded response_style from %s: %s", label, resp_style_item->valuestring);
    }

    // Read thinking_style
    cJSON *thinking_style_item = cJSON_GetObjectItem(root, "thinking_style");
    if (thinking_style_item && cJSON_IsString(thinking_style_item)) {
        config->thinking_style = config_thinking_style_from_string(thinking_style_item->valuestring);
        LOG_DEBUG("[Config] Loaded thinking_style from %s: %s", label, thinking_style_item->valuestring);
    }

    // Read theme
    cJSON *theme_item = cJSON_GetObjectItem(root, "theme");
    if (theme_item && cJSON_IsString(theme_item) && theme_item->valuestring) {
        strlcpy(config->theme, theme_item->valuestring, sizeof(config->theme));
        LOG_DEBUG("[Config] Loaded theme from %s: %s", label, config->theme);
    }

    // Read LLM provider configuration
    cJSON *llm_item = cJSON_GetObjectItem(root, "llm_provider");
    if (llm_item && cJSON_IsObject(llm_item)) {
        // Read provider type
        cJSON *provider_type_item = cJSON_GetObjectItem(llm_item, "provider_type");
        if (provider_type_item && cJSON_IsString(provider_type_item)) {
            config->llm_provider.provider_type = config_provider_type_from_string(provider_type_item->valuestring);
            LOG_DEBUG("[Config] Loaded provider_type from %s: %s", label, provider_type_item->valuestring);
        }

        // Read provider name
        cJSON *provider_name_item = cJSON_GetObjectItem(llm_item, "provider_name");
        if (provider_name_item && cJSON_IsString(provider_name_item) && provider_name_item->valuestring) {
            strlcpy(config->llm_provider.provider_name, provider_name_item->valuestring, CONFIG_PROVIDER_NAME_MAX);
            LOG_DEBUG("[Config] Loaded provider_name from %s: %s", label, config->llm_provider.provider_name);
        }

        // Read model
        cJSON *model_item = cJSON_GetObjectItem(llm_item, "model");
        if (model_item && cJSON_IsString(model_item) && model_item->valuestring) {
            strlcpy(config->llm_provider.model, model_item->valuestring, CONFIG_MODEL_MAX);
            LOG_DEBUG("[Config] Loaded model from %s: %s", label, config->llm_provider.model);
        }

        // Read API base
        cJSON *api_base_item = cJSON_GetObjectItem(llm_item, "api_base");
        if (api_base_item && cJSON_IsString(api_base_item) && api_base_item->valuestring) {
            strlcpy(config->llm_provider.api_base, api_base_item->valuestring, CONFIG_API_BASE_MAX);
            LOG_DEBUG("[Config] Loaded api_base from %s: %s", label, config->llm_provider.api_base);
        }

        // Read API key (with security warning if present)
        cJSON *api_key_item = cJSON_GetObjectItem(llm_item, "api_key");
        if (api_key_item && cJSON_IsString(api_key_item) && api_key_item->valuestring) {
            strlcpy(config->llm_provider.api_key, api_key_item->valuestring, CONFIG_API_KEY_MAX);
            LOG_WARN("[Config] Loaded API key from %s config file - consider using environment variable for better security", label);
            LOG_DEBUG("[Config] Loaded api_key from %s: [REDACTED]", label);
        }

        // Read API key environment variable name
        cJSON *api_key_env_item = cJSON_GetObjectItem(llm_item, "api_key_env");
        if (api_key_env_item && cJSON_IsString(api_key_env_item) && api_key_env_item->valuestring) {
            strlcpy(config->llm_provider.api_key_env, api_key_env_item->valuestring, CONFIG_API_KEY_ENV_MAX);
            LOG_DEBUG("[Config] Loaded api_key_env from %s: %s", label, config->llm_provider.api_key_env);
        }

        // Read extra headers
        cJSON *extra_headers_item = cJSON_GetObjectItem(llm_item, "extra_headers");
        if (extra_headers_item && cJSON_IsString(extra_headers_item) && extra_headers_item->valuestring) {
            strlcpy(config->llm_provider.extra_headers, extra_headers_item->valuestring, CONFIG_EXTRA_HEADERS_MAX);
            LOG_DEBUG("[Config] Loaded extra_headers from %s: %s", label, config->llm_provider.extra_headers);
        }

        // Read use_bedrock (legacy flag) - can be boolean or number
        cJSON *use_bedrock_item = cJSON_GetObjectItem(llm_item, "use_bedrock");
        if (use_bedrock_item) {
            if (cJSON_IsBool(use_bedrock_item)) {
                config->llm_provider.use_bedrock = cJSON_IsTrue(use_bedrock_item) ? 1 : 0;
            } else if (cJSON_IsNumber(use_bedrock_item)) {
                double value = cJSON_GetNumberValue(use_bedrock_item);
                // Disable float-equal warning for this comparison
                #pragma GCC diagnostic push
                #pragma GCC diagnostic ignored "-Wfloat-equal"
                config->llm_provider.use_bedrock = (value != 0.0) ? 1 : 0;
                #pragma GCC diagnostic pop
            }
            LOG_DEBUG("[Config] Loaded use_bedrock from %s: %d", label, config->llm_provider.use_bedrock);
        }
    }

    // Read multiple provider configurations
    cJSON *providers_item = cJSON_GetObjectItem(root, "providers");
    if (providers_item && cJSON_IsObject(providers_item)) {
        cJSON *provider_item = NULL;
        cJSON_ArrayForEach(provider_item, providers_item) {
            const char *key = provider_item->string;
            if (!key || key[0] == '\0') {
                LOG_WARN("[Config] Skipping provider with empty key in %s", label);
                continue;
            }

            if (!cJSON_IsObject(provider_item)) {
                LOG_WARN("[Config] Skipping provider '%s' in %s: not an object", key, label);
                continue;
            }

            // Check if provider with this key already exists (from global config)
            int existing_idx = -1;
            for (int i = 0; i < config->provider_count; i++) {
                if (strcmp(config->providers[i].key, key) == 0) {
                    existing_idx = i;
                    break;
                }
            }

            NamedProviderConfig *named_provider = NULL;
            if (existing_idx >= 0) {
                // Update existing provider (local overrides global)
                named_provider = &config->providers[existing_idx];
                LOG_DEBUG("[Config] Overriding provider '%s' with %s config", key, label);
            } else {
                // Add new provider
                if (config->provider_count >= CONFIG_MAX_PROVIDERS) {
                    LOG_WARN("[Config] Maximum number of providers (%d) reached, skipping '%s' from %s", CONFIG_MAX_PROVIDERS, key, label);
                    continue;
                }
                named_provider = &config->providers[config->provider_count];
                strlcpy(named_provider->key, key, CONFIG_PROVIDER_KEY_MAX);
                config->provider_count++;
            }

            // Load provider configuration
            LLMProviderConfig *provider_config = &named_provider->config;

            // Read provider type
            cJSON *provider_type_item = cJSON_GetObjectItem(provider_item, "provider_type");
            if (provider_type_item && cJSON_IsString(provider_type_item)) {
                provider_config->provider_type = config_provider_type_from_string(provider_type_item->valuestring);
                LOG_DEBUG("[Config] Loaded provider_type for '%s' from %s: %s", key, label, provider_type_item->valuestring);
            } else if (existing_idx < 0) {
                provider_config->provider_type = PROVIDER_AUTO;
            }

            // Read provider name
            cJSON *provider_name_item = cJSON_GetObjectItem(provider_item, "provider_name");
            if (provider_name_item && cJSON_IsString(provider_name_item) && provider_name_item->valuestring) {
                strlcpy(provider_config->provider_name, provider_name_item->valuestring, CONFIG_PROVIDER_NAME_MAX);
                LOG_DEBUG("[Config] Loaded provider_name for '%s' from %s: %s", key, label, provider_config->provider_name);
            }

            // Read model
            cJSON *model_item = cJSON_GetObjectItem(provider_item, "model");
            if (model_item && cJSON_IsString(model_item) && model_item->valuestring) {
                strlcpy(provider_config->model, model_item->valuestring, CONFIG_MODEL_MAX);
                LOG_DEBUG("[Config] Loaded model for '%s' from %s: %s", key, label, provider_config->model);
            }

            // Read API base
            cJSON *api_base_item = cJSON_GetObjectItem(provider_item, "api_base");
            if (api_base_item && cJSON_IsString(api_base_item) && api_base_item->valuestring) {
                strlcpy(provider_config->api_base, api_base_item->valuestring, CONFIG_API_BASE_MAX);
                LOG_DEBUG("[Config] Loaded api_base for '%s' from %s: %s", key, label, provider_config->api_base);
            }

            // Read API key (with security warning if present)
            cJSON *api_key_item = cJSON_GetObjectItem(provider_item, "api_key");
            if (api_key_item && cJSON_IsString(api_key_item) && api_key_item->valuestring) {
                strlcpy(provider_config->api_key, api_key_item->valuestring, CONFIG_API_KEY_MAX);
                LOG_WARN("[Config] Loaded API key from %s config file for provider '%s' - consider using environment variable for better security", label, key);
                LOG_DEBUG("[Config] Loaded api_key for '%s' from %s: [REDACTED]", key, label);
            }

            // Read API key environment variable name
            cJSON *api_key_env_item = cJSON_GetObjectItem(provider_item, "api_key_env");
            if (api_key_env_item && cJSON_IsString(api_key_env_item) && api_key_env_item->valuestring) {
                strlcpy(provider_config->api_key_env, api_key_env_item->valuestring, CONFIG_API_KEY_ENV_MAX);
                LOG_DEBUG("[Config] Loaded api_key_env for '%s' from %s: %s", key, label, provider_config->api_key_env);
            }

            // Read extra headers
            cJSON *extra_headers_item = cJSON_GetObjectItem(provider_item, "extra_headers");
            if (extra_headers_item && cJSON_IsString(extra_headers_item) && extra_headers_item->valuestring) {
                strlcpy(provider_config->extra_headers, extra_headers_item->valuestring, CONFIG_EXTRA_HEADERS_MAX);
                LOG_DEBUG("[Config] Loaded extra_headers for '%s' from %s: %s", key, label, provider_config->extra_headers);
            }

            // Read use_bedrock (legacy flag) - can be boolean or number
            cJSON *use_bedrock_item = cJSON_GetObjectItem(provider_item, "use_bedrock");
            if (use_bedrock_item) {
                if (cJSON_IsBool(use_bedrock_item)) {
                    provider_config->use_bedrock = cJSON_IsTrue(use_bedrock_item) ? 1 : 0;
                } else if (cJSON_IsNumber(use_bedrock_item)) {
                    double value = cJSON_GetNumberValue(use_bedrock_item);
                    // Disable float-equal warning for this comparison
                    #pragma GCC diagnostic push
                    #pragma GCC diagnostic ignored "-Wfloat-equal"
                    provider_config->use_bedrock = (value != 0.0) ? 1 : 0;
                    #pragma GCC diagnostic pop
                }
                LOG_DEBUG("[Config] Loaded use_bedrock for '%s' from %s: %d", key, label, provider_config->use_bedrock);
            }

            LOG_DEBUG("[Config] Loaded provider '%s' from %s", key, label);
        }
    }

    // Read active provider
    cJSON *active_provider_item = cJSON_GetObjectItem(root, "active_provider");
    if (active_provider_item && cJSON_IsString(active_provider_item) && active_provider_item->valuestring) {
        strlcpy(config->active_provider, active_provider_item->valuestring, CONFIG_PROVIDER_KEY_MAX);
        LOG_DEBUG("[Config] Loaded active_provider from %s: '%s'", label, config->active_provider);
    } else {
        LOG_DEBUG("[Config] No active_provider found in %s config", label);
    }

    cJSON_Delete(root);
    LOG_INFO("[Config] Configuration loaded from %s (%s)", file_path, label);
    return 0;
}

int config_load(KlawedConfig *config) {
    if (!config) return -1;

    // Initialize with defaults first
    config_init_defaults(config);

    int global_loaded = -1;
    int local_loaded = -1;

    // First, try to load global config from ~/.klawed/config.json
    char global_path[1024];
    if (config_get_global_path(global_path, sizeof(global_path)) == 0) {
        global_loaded = config_load_from_file(config, global_path, "global");
    }

    // Then, load local config from data_dir/config.json (overrides global)
    char local_path[1024];
    if (data_dir_build_path(local_path, sizeof(local_path), CONFIG_FILE_NAME) == 0) {
        local_loaded = config_load_from_file(config, local_path, "local");
    }

    // Return 0 if at least one config was loaded successfully
    if (global_loaded == 0 || local_loaded == 0) {
        return 0;
    }

    LOG_DEBUG("[Config] No config files found, using defaults");
    return -1;
}

int config_save(const KlawedConfig *config) {
    if (!config) return -1;

    // Ensure data directory exists
    if (data_dir_ensure(NULL) != 0) {
        LOG_ERROR("[Config] Failed to create data directory");
        return -1;
    }

    // Build config file path
    char config_path[1024];
    if (data_dir_build_path(config_path, sizeof(config_path), CONFIG_FILE_NAME) != 0) {
        LOG_ERROR("[Config] Failed to build config path");
        return -1;
    }

    // Get default config for comparison - we only write fields that differ from defaults
    // or that already exist in the local config file
    KlawedConfig defaults;
    config_init_defaults(&defaults);

    // Try to read existing LOCAL config to preserve other fields
    // This is the local config only - we don't merge with global here
    cJSON *root = NULL;
    FILE *fp = fopen(config_path, "r");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (file_size > 0 && file_size <= 1024 * 1024) {
            char *json_str = malloc((size_t)file_size + 1);
            if (json_str) {
                size_t bytes_read = fread(json_str, 1, (size_t)file_size, fp);
                if (bytes_read == (size_t)file_size) {
                    json_str[file_size] = '\0';
                    root = cJSON_Parse(json_str);
                }
                free(json_str);
            }
        }
        fclose(fp);
    }

    // Create new root if none exists
    if (!root) {
        root = cJSON_CreateObject();
        if (!root) {
            LOG_ERROR("[Config] Failed to create JSON object");
            return -1;
        }
    }

    // Update input_box_style - only add if it differs from default or already exists
    cJSON *existing_style = cJSON_GetObjectItem(root, "input_box_style");
    if (existing_style) {
        // Already in local config - update it
        cJSON_SetValuestring(existing_style, config_input_style_to_string(config->input_box_style));
    } else if (config->input_box_style != defaults.input_box_style) {
        // Differs from default - add it
        cJSON_AddStringToObject(root, "input_box_style", config_input_style_to_string(config->input_box_style));
    }

    // Update response_style - only add if it differs from default or already exists
    cJSON *existing_resp_style = cJSON_GetObjectItem(root, "response_style");
    if (existing_resp_style) {
        cJSON_SetValuestring(existing_resp_style, config_response_style_to_string(config->response_style));
    } else if (config->response_style != defaults.response_style) {
        cJSON_AddStringToObject(root, "response_style", config_response_style_to_string(config->response_style));
    }

    // Update thinking_style - only add if it differs from default or already exists
    cJSON *existing_thinking_style = cJSON_GetObjectItem(root, "thinking_style");
    if (existing_thinking_style) {
        cJSON_SetValuestring(existing_thinking_style, config_thinking_style_to_string(config->thinking_style));
    } else if (config->thinking_style != defaults.thinking_style) {
        cJSON_AddStringToObject(root, "thinking_style", config_thinking_style_to_string(config->thinking_style));
    }

    // Update theme - only add if non-empty and (already exists or differs from default empty)
    cJSON *existing_theme = cJSON_GetObjectItem(root, "theme");
    if (config->theme[0] != '\0') {
        if (existing_theme) {
            cJSON_SetValuestring(existing_theme, config->theme);
        } else {
            // Theme is non-empty (differs from default empty) - add it
            cJSON_AddStringToObject(root, "theme", config->theme);
        }
    }

    // Update LLM provider configuration - only if llm_provider section already exists in local
    // or if it differs from defaults
    cJSON *existing_llm = cJSON_GetObjectItem(root, "llm_provider");
    int llm_differs_from_default =
        config->llm_provider.provider_type != defaults.llm_provider.provider_type ||
        strcmp(config->llm_provider.provider_name, defaults.llm_provider.provider_name) != 0 ||
        strcmp(config->llm_provider.model, defaults.llm_provider.model) != 0 ||
        strcmp(config->llm_provider.api_base, defaults.llm_provider.api_base) != 0 ||
        strcmp(config->llm_provider.api_key, defaults.llm_provider.api_key) != 0 ||
        strcmp(config->llm_provider.api_key_env, defaults.llm_provider.api_key_env) != 0 ||
        config->llm_provider.use_bedrock != defaults.llm_provider.use_bedrock;

    if (existing_llm || llm_differs_from_default) {
        cJSON *llm_obj = NULL;

        if (existing_llm && cJSON_IsObject(existing_llm)) {
            llm_obj = existing_llm;
        } else if (llm_differs_from_default) {
            llm_obj = cJSON_AddObjectToObject(root, "llm_provider");
        }

        if (llm_obj) {
            // Update provider type - only add if already exists or differs from default
            cJSON *existing_provider_type = cJSON_GetObjectItem(llm_obj, "provider_type");
            if (existing_provider_type) {
                cJSON_SetValuestring(existing_provider_type, config_provider_type_to_string(config->llm_provider.provider_type));
            } else if (config->llm_provider.provider_type != defaults.llm_provider.provider_type) {
                cJSON_AddStringToObject(llm_obj, "provider_type", config_provider_type_to_string(config->llm_provider.provider_type));
            }

            // Update provider name - only add if already exists or non-empty
            if (config->llm_provider.provider_name[0] != '\0') {
                cJSON *existing_provider_name = cJSON_GetObjectItem(llm_obj, "provider_name");
                if (existing_provider_name) {
                    cJSON_SetValuestring(existing_provider_name, config->llm_provider.provider_name);
                } else {
                    cJSON_AddStringToObject(llm_obj, "provider_name", config->llm_provider.provider_name);
                }
            }

            // Update model - only add if already exists or non-empty
            if (config->llm_provider.model[0] != '\0') {
                cJSON *existing_model = cJSON_GetObjectItem(llm_obj, "model");
                if (existing_model) {
                    cJSON_SetValuestring(existing_model, config->llm_provider.model);
                } else {
                    cJSON_AddStringToObject(llm_obj, "model", config->llm_provider.model);
                }
            }

            // Update API base - only add if already exists or non-empty
            if (config->llm_provider.api_base[0] != '\0') {
                cJSON *existing_api_base = cJSON_GetObjectItem(llm_obj, "api_base");
                if (existing_api_base) {
                    cJSON_SetValuestring(existing_api_base, config->llm_provider.api_base);
                } else {
                    cJSON_AddStringToObject(llm_obj, "api_base", config->llm_provider.api_base);
                }
            }

            // Update API key - only add if already exists or non-empty
            if (config->llm_provider.api_key[0] != '\0') {
                cJSON *existing_api_key = cJSON_GetObjectItem(llm_obj, "api_key");
                if (existing_api_key) {
                    cJSON_SetValuestring(existing_api_key, config->llm_provider.api_key);
                } else {
                    cJSON_AddStringToObject(llm_obj, "api_key", config->llm_provider.api_key);
                }
                LOG_WARN("[Config] Saving API key to config file - consider using environment variable for better security");
            }

            // Update API key environment variable name - only add if already exists or non-empty
            if (config->llm_provider.api_key_env[0] != '\0') {
                cJSON *existing_api_key_env = cJSON_GetObjectItem(llm_obj, "api_key_env");
                if (existing_api_key_env) {
                    cJSON_SetValuestring(existing_api_key_env, config->llm_provider.api_key_env);
                } else {
                    cJSON_AddStringToObject(llm_obj, "api_key_env", config->llm_provider.api_key_env);
                }
            }

            // Update use_bedrock - only add if already exists or differs from default (0)
            cJSON *existing_use_bedrock = cJSON_GetObjectItem(llm_obj, "use_bedrock");
            if (existing_use_bedrock) {
                // Remove and re-add to avoid type conversion warnings
                cJSON_DeleteItemFromObject(llm_obj, "use_bedrock");
                cJSON_AddNumberToObject(llm_obj, "use_bedrock", (double)(config->llm_provider.use_bedrock ? 1 : 0));
            } else if (config->llm_provider.use_bedrock != 0) {
                cJSON_AddNumberToObject(llm_obj, "use_bedrock", (double)(config->llm_provider.use_bedrock ? 1 : 0));
            }
        }
    }

    // Save multiple provider configurations - only if providers section exists in local config
    // or if there are providers that differ from defaults (i.e., provider_count > 0)
    cJSON *existing_providers = cJSON_GetObjectItem(root, "providers");
    if (existing_providers || config->provider_count > 0) {
        cJSON *providers_obj = NULL;

        if (existing_providers && cJSON_IsObject(existing_providers)) {
            // Clear existing providers to rebuild with current config
            cJSON_DeleteItemFromObject(root, "providers");
        }

        // Only create providers section if we have providers to save
        if (config->provider_count > 0) {
            providers_obj = cJSON_AddObjectToObject(root, "providers");

            if (providers_obj) {
                for (int i = 0; i < config->provider_count; i++) {
                    const NamedProviderConfig *named_provider = &config->providers[i];
                    const LLMProviderConfig *provider_config = &named_provider->config;

                    cJSON *provider_obj = cJSON_AddObjectToObject(providers_obj, named_provider->key);
                    if (!provider_obj) {
                        LOG_WARN("[Config] Failed to create provider object for '%s'", named_provider->key);
                        continue;
                    }

                    // Add provider type
                    cJSON_AddStringToObject(provider_obj, "provider_type", config_provider_type_to_string(provider_config->provider_type));

                    // Add provider name (if non-empty)
                    if (provider_config->provider_name[0] != '\0') {
                        cJSON_AddStringToObject(provider_obj, "provider_name", provider_config->provider_name);
                    }

                    // Add model (if non-empty)
                    if (provider_config->model[0] != '\0') {
                        cJSON_AddStringToObject(provider_obj, "model", provider_config->model);
                    }

                    // Add API base (if non-empty)
                    if (provider_config->api_base[0] != '\0') {
                        cJSON_AddStringToObject(provider_obj, "api_base", provider_config->api_base);
                    }

                    // Add API key (if non-empty, with security warning)
                    if (provider_config->api_key[0] != '\0') {
                        cJSON_AddStringToObject(provider_obj, "api_key", provider_config->api_key);
                        LOG_WARN("[Config] Saving API key to config file for provider '%s' - consider using environment variable for better security", named_provider->key);
                    }

                    // Add API key environment variable name (if non-empty)
                    if (provider_config->api_key_env[0] != '\0') {
                        cJSON_AddStringToObject(provider_obj, "api_key_env", provider_config->api_key_env);
                    }

                    // Add use_bedrock (legacy flag) - only if non-zero
                    if (provider_config->use_bedrock != 0) {
                        cJSON_AddNumberToObject(provider_obj, "use_bedrock", (double)(provider_config->use_bedrock ? 1 : 0));
                    }
                }
            }
        }
    }

    // Save active provider - only add if non-empty (already differs from default empty)
    cJSON *existing_active = cJSON_GetObjectItem(root, "active_provider");
    if (config->active_provider[0] != '\0') {
        LOG_DEBUG("[Config] config_save: saving active_provider='%s'", config->active_provider);
        if (existing_active) {
            cJSON_SetValuestring(existing_active, config->active_provider);
        } else {
            cJSON_AddStringToObject(root, "active_provider", config->active_provider);
        }
    } else {
        LOG_DEBUG("[Config] config_save: active_provider is empty, not saving");
    }

    // Write to file
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) {
        LOG_ERROR("[Config] Failed to serialize config to JSON");
        return -1;
    }

    fp = fopen(config_path, "w");
    if (!fp) {
        LOG_ERROR("[Config] Failed to open %s for writing: %s", config_path, strerror(errno));
        free(json_str);
        return -1;
    }

    size_t json_len = strlen(json_str);
    size_t written = fwrite(json_str, 1, json_len, fp);
    fclose(fp);
    free(json_str);

    if (written != json_len) {
        LOG_ERROR("[Config] Failed to write complete config file");
        return -1;
    }

    LOG_DEBUG("[Config] Configuration saved to %s", config_path);
    return 0;
}

const char* config_provider_type_to_string(LLMProviderType type) {
    switch (type) {
        case PROVIDER_AUTO:
            return "auto";
        case PROVIDER_OPENAI:
            return "openai";
        case PROVIDER_ANTHROPIC:
            return "anthropic";
        case PROVIDER_BEDROCK:
            return "bedrock";
        case PROVIDER_DEEPSEEK:
            return "deepseek";
        case PROVIDER_MOONSHOT:
            return "moonshot";
        case PROVIDER_KIMI_CODING_PLAN:
            return "kimi_coding_plan";
        case PROVIDER_OPENAI_SUB:
            return "openai_sub";
        case PROVIDER_ANTHROPIC_SUB:
            return "anthropic_sub";
        case PROVIDER_OPENAI_RESPONSES:
            return "openai_responses";
        case PROVIDER_ZAI_CODING:
            return "zai_coding";
        case PROVIDER_CUSTOM:
            return "custom";
        default:
            return "auto";
    }
}

LLMProviderType config_provider_type_from_string(const char *str) {
    if (!str) return PROVIDER_AUTO;

    if (strcmp(str, "openai") == 0) {
        return PROVIDER_OPENAI;
    } else if (strcmp(str, "anthropic") == 0) {
        return PROVIDER_ANTHROPIC;
    } else if (strcmp(str, "bedrock") == 0) {
        return PROVIDER_BEDROCK;
    } else if (strcmp(str, "deepseek") == 0) {
        return PROVIDER_DEEPSEEK;
    } else if (strcmp(str, "moonshot") == 0 || strcmp(str, "kimi") == 0) {
        return PROVIDER_MOONSHOT;
    } else if (strcmp(str, "kimi_coding_plan") == 0) {
        return PROVIDER_KIMI_CODING_PLAN;
    } else if (strcmp(str, "openai_sub") == 0 || strcmp(str, "chatgpt") == 0) {
        return PROVIDER_OPENAI_SUB;
    } else if (strcmp(str, "anthropic_sub") == 0 || strcmp(str, "claude_sub") == 0) {
        return PROVIDER_ANTHROPIC_SUB;
    } else if (strcmp(str, "openai_responses") == 0) {
        return PROVIDER_OPENAI_RESPONSES;
    } else if (strcmp(str, "zai_coding") == 0 || strcmp(str, "glm_coding") == 0 || strcmp(str, "zai") == 0) {
        return PROVIDER_ZAI_CODING;
    } else if (strcmp(str, "custom") == 0) {
        return PROVIDER_CUSTOM;
    }
    return PROVIDER_AUTO;  // "auto" or unknown
}

const NamedProviderConfig* config_find_provider(const KlawedConfig *config, const char *key) {
    if (!config || !key || key[0] == '\0') {
        LOG_DEBUG("[Config] config_find_provider: NULL config or NULL/empty key");
        return NULL;
    }

    LOG_DEBUG("[Config] config_find_provider: looking for key='%s' in %d providers",
              key, config->provider_count);

    for (int i = 0; i < config->provider_count; i++) {
        LOG_DEBUG("[Config] config_find_provider: checking provider[%d].key='%s'", i, config->providers[i].key);
        if (strcmp(config->providers[i].key, key) == 0) {
            LOG_DEBUG("[Config] config_find_provider: found match at index %d", i);
            return &config->providers[i];
        }
    }

    LOG_WARN("[Config] config_find_provider: key='%s' not found in any provider", key);
    return NULL;
}

const NamedProviderConfig* config_get_active_provider(const KlawedConfig *config) {
    if (!config) {
        LOG_DEBUG("[Config] config_get_active_provider: config is NULL");
        return NULL;
    }

    if (config->active_provider[0] == '\0') {
        LOG_DEBUG("[Config] config_get_active_provider: active_provider is not set (empty string)");
        return NULL;
    }

    LOG_DEBUG("[Config] config_get_active_provider: looking for provider '%s'", config->active_provider);
    const NamedProviderConfig *provider = config_find_provider(config, config->active_provider);
    if (provider) {
        LOG_DEBUG("[Config] config_get_active_provider: found provider '%s'", config->active_provider);
    } else {
        LOG_WARN("[Config] config_get_active_provider: provider '%s' not found in config providers", config->active_provider);
    }

    return provider;
}

int config_set_provider(KlawedConfig *config, const char *key, const LLMProviderConfig *provider_config) {
    if (!config || !key || key[0] == '\0' || !provider_config) {
        return -1;
    }

    // Check if provider already exists
    for (int i = 0; i < config->provider_count; i++) {
        if (strcmp(config->providers[i].key, key) == 0) {
            // Update existing provider
            config->providers[i].config = *provider_config;
            return 0;
        }
    }

    // Add new provider
    if (config->provider_count >= CONFIG_MAX_PROVIDERS) {
        LOG_ERROR("[Config] Maximum number of providers (%d) reached", CONFIG_MAX_PROVIDERS);
        return -1;
    }

    NamedProviderConfig *new_provider = &config->providers[config->provider_count];
    strlcpy(new_provider->key, key, CONFIG_PROVIDER_KEY_MAX);
    new_provider->config = *provider_config;
    config->provider_count++;

    return 0;
}

int config_remove_provider(KlawedConfig *config, const char *key) {
    if (!config || !key || key[0] == '\0') {
        return -1;
    }

    for (int i = 0; i < config->provider_count; i++) {
        if (strcmp(config->providers[i].key, key) == 0) {
            // Shift remaining providers down
            for (int j = i; j < config->provider_count - 1; j++) {
                config->providers[j] = config->providers[j + 1];
            }

            // Clear the last slot
            config->providers[config->provider_count - 1].key[0] = '\0';
            config->providers[config->provider_count - 1].config.provider_type = PROVIDER_AUTO;
            config->providers[config->provider_count - 1].config.provider_name[0] = '\0';
            config->providers[config->provider_count - 1].config.model[0] = '\0';
            config->providers[config->provider_count - 1].config.api_base[0] = '\0';
            config->providers[config->provider_count - 1].config.api_key[0] = '\0';
            config->providers[config->provider_count - 1].config.use_bedrock = 0;

            config->provider_count--;

            // If we removed the active provider, clear it
            if (strcmp(config->active_provider, key) == 0) {
                config->active_provider[0] = '\0';
            }

            return 0;
        }
    }

    return -1;  // Provider not found
}

int config_set_active_provider(KlawedConfig *config, const char *key) {
    if (!config || !key || key[0] == '\0') {
        return -1;
    }

    // Check if provider exists
    if (!config_find_provider(config, key)) {
        LOG_ERROR("[Config] Provider '%s' not found", key);
        return -1;
    }

    strlcpy(config->active_provider, key, CONFIG_PROVIDER_KEY_MAX);
    return 0;
}
