/*
 * Config - User configuration persistence for klawed
 *
 * Stores user preferences in .klawed/config.json
 */

#include "config.h"
#include "logger.h"
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <sys/stat.h>
#include <errno.h>

#define CONFIG_DIR ".klawed"
#define CONFIG_FILE ".klawed/config.json"

void config_init_defaults(KlawedConfig *config) {
    if (!config) return;
    config->input_box_style = INPUT_STYLE_BLAND;
    config->theme[0] = '\0';  // Empty means use default or KLAWED_THEME env var
    
    // Initialize LLM provider defaults (legacy single-provider)
    config->llm_provider.provider_type = PROVIDER_AUTO;
    strlcpy(config->llm_provider.provider_name, "auto", CONFIG_PROVIDER_NAME_MAX);
    config->llm_provider.model[0] = '\0';
    config->llm_provider.api_base[0] = '\0';
    config->llm_provider.api_key[0] = '\0';
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

int config_load(KlawedConfig *config) {
    if (!config) return -1;

    // Initialize with defaults first
    config_init_defaults(config);

    // Try to open config file
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        LOG_DEBUG("[Config] No config file found at %s, using defaults", CONFIG_FILE);
        return -1;
    }

    // Read file contents
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 1024 * 1024) {  // Max 1MB
        LOG_WARN("[Config] Invalid config file size: %ld", file_size);
        fclose(fp);
        return -1;
    }

    char *json_str = malloc((size_t)file_size + 1);
    if (!json_str) {
        LOG_ERROR("[Config] Failed to allocate memory for config file");
        fclose(fp);
        return -1;
    }

    size_t bytes_read = fread(json_str, 1, (size_t)file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        LOG_WARN("[Config] Failed to read config file completely");
        free(json_str);
        return -1;
    }
    json_str[file_size] = '\0';

    // Parse JSON
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (!root) {
        LOG_WARN("[Config] Failed to parse config file as JSON");
        return -1;
    }

    // Read input_box_style
    cJSON *style_item = cJSON_GetObjectItem(root, "input_box_style");
    if (style_item && cJSON_IsString(style_item)) {
        config->input_box_style = config_input_style_from_string(style_item->valuestring);
        LOG_DEBUG("[Config] Loaded input_box_style: %s", style_item->valuestring);
    }

    // Read theme
    cJSON *theme_item = cJSON_GetObjectItem(root, "theme");
    if (theme_item && cJSON_IsString(theme_item) && theme_item->valuestring) {
        strlcpy(config->theme, theme_item->valuestring, sizeof(config->theme));
        LOG_DEBUG("[Config] Loaded theme: %s", config->theme);
    }

    // Read LLM provider configuration
    cJSON *llm_item = cJSON_GetObjectItem(root, "llm_provider");
    if (llm_item && cJSON_IsObject(llm_item)) {
        // Read provider type
        cJSON *provider_type_item = cJSON_GetObjectItem(llm_item, "provider_type");
        if (provider_type_item && cJSON_IsString(provider_type_item)) {
            config->llm_provider.provider_type = config_provider_type_from_string(provider_type_item->valuestring);
            LOG_DEBUG("[Config] Loaded provider_type: %s", provider_type_item->valuestring);
        }

        // Read provider name
        cJSON *provider_name_item = cJSON_GetObjectItem(llm_item, "provider_name");
        if (provider_name_item && cJSON_IsString(provider_name_item) && provider_name_item->valuestring) {
            strlcpy(config->llm_provider.provider_name, provider_name_item->valuestring, CONFIG_PROVIDER_NAME_MAX);
            LOG_DEBUG("[Config] Loaded provider_name: %s", config->llm_provider.provider_name);
        }

        // Read model
        cJSON *model_item = cJSON_GetObjectItem(llm_item, "model");
        if (model_item && cJSON_IsString(model_item) && model_item->valuestring) {
            strlcpy(config->llm_provider.model, model_item->valuestring, CONFIG_MODEL_MAX);
            LOG_DEBUG("[Config] Loaded model: %s", config->llm_provider.model);
        }

        // Read API base
        cJSON *api_base_item = cJSON_GetObjectItem(llm_item, "api_base");
        if (api_base_item && cJSON_IsString(api_base_item) && api_base_item->valuestring) {
            strlcpy(config->llm_provider.api_base, api_base_item->valuestring, CONFIG_API_BASE_MAX);
            LOG_DEBUG("[Config] Loaded api_base: %s", config->llm_provider.api_base);
        }

        // Read API key (with security warning if present)
        cJSON *api_key_item = cJSON_GetObjectItem(llm_item, "api_key");
        if (api_key_item && cJSON_IsString(api_key_item) && api_key_item->valuestring) {
            strlcpy(config->llm_provider.api_key, api_key_item->valuestring, CONFIG_API_KEY_MAX);
            LOG_WARN("[Config] Loaded API key from config file - consider using environment variable for better security");
            LOG_DEBUG("[Config] Loaded api_key: [REDACTED]");
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
            LOG_DEBUG("[Config] Loaded use_bedrock: %d", config->llm_provider.use_bedrock);
        }
    }

    // Read multiple provider configurations
    cJSON *providers_item = cJSON_GetObjectItem(root, "providers");
    if (providers_item && cJSON_IsObject(providers_item)) {
        cJSON *provider_item = NULL;
        cJSON_ArrayForEach(provider_item, providers_item) {
            if (config->provider_count >= CONFIG_MAX_PROVIDERS) {
                LOG_WARN("[Config] Maximum number of providers (%d) reached, skipping additional providers", CONFIG_MAX_PROVIDERS);
                break;
            }
            
            const char *key = provider_item->string;
            if (!key || key[0] == '\0') {
                LOG_WARN("[Config] Skipping provider with empty key");
                continue;
            }
            
            if (!cJSON_IsObject(provider_item)) {
                LOG_WARN("[Config] Skipping provider '%s': not an object", key);
                continue;
            }
            
            NamedProviderConfig *named_provider = &config->providers[config->provider_count];
            strlcpy(named_provider->key, key, CONFIG_PROVIDER_KEY_MAX);
            
            // Load provider configuration
            LLMProviderConfig *provider_config = &named_provider->config;
            
            // Read provider type
            cJSON *provider_type_item = cJSON_GetObjectItem(provider_item, "provider_type");
            if (provider_type_item && cJSON_IsString(provider_type_item)) {
                provider_config->provider_type = config_provider_type_from_string(provider_type_item->valuestring);
                LOG_DEBUG("[Config] Loaded provider_type for '%s': %s", key, provider_type_item->valuestring);
            } else {
                provider_config->provider_type = PROVIDER_AUTO;
            }
            
            // Read provider name
            cJSON *provider_name_item = cJSON_GetObjectItem(provider_item, "provider_name");
            if (provider_name_item && cJSON_IsString(provider_name_item) && provider_name_item->valuestring) {
                strlcpy(provider_config->provider_name, provider_name_item->valuestring, CONFIG_PROVIDER_NAME_MAX);
                LOG_DEBUG("[Config] Loaded provider_name for '%s': %s", key, provider_config->provider_name);
            }
            
            // Read model
            cJSON *model_item = cJSON_GetObjectItem(provider_item, "model");
            if (model_item && cJSON_IsString(model_item) && model_item->valuestring) {
                strlcpy(provider_config->model, model_item->valuestring, CONFIG_MODEL_MAX);
                LOG_DEBUG("[Config] Loaded model for '%s': %s", key, provider_config->model);
            }
            
            // Read API base
            cJSON *api_base_item = cJSON_GetObjectItem(provider_item, "api_base");
            if (api_base_item && cJSON_IsString(api_base_item) && api_base_item->valuestring) {
                strlcpy(provider_config->api_base, api_base_item->valuestring, CONFIG_API_BASE_MAX);
                LOG_DEBUG("[Config] Loaded api_base for '%s': %s", key, provider_config->api_base);
            }
            
            // Read API key (with security warning if present)
            cJSON *api_key_item = cJSON_GetObjectItem(provider_item, "api_key");
            if (api_key_item && cJSON_IsString(api_key_item) && api_key_item->valuestring) {
                strlcpy(provider_config->api_key, api_key_item->valuestring, CONFIG_API_KEY_MAX);
                LOG_WARN("[Config] Loaded API key from config file for provider '%s' - consider using environment variable for better security", key);
                LOG_DEBUG("[Config] Loaded api_key for '%s': [REDACTED]", key);
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
                LOG_DEBUG("[Config] Loaded use_bedrock for '%s': %d", key, provider_config->use_bedrock);
            }
            
            config->provider_count++;
            LOG_DEBUG("[Config] Loaded provider '%s'", key);
        }
    }
    
    // Read active provider
    cJSON *active_provider_item = cJSON_GetObjectItem(root, "active_provider");
    if (active_provider_item && cJSON_IsString(active_provider_item) && active_provider_item->valuestring) {
        strlcpy(config->active_provider, active_provider_item->valuestring, CONFIG_PROVIDER_KEY_MAX);
        LOG_DEBUG("[Config] Loaded active_provider: %s", config->active_provider);
    }

    cJSON_Delete(root);
    LOG_INFO("[Config] Configuration loaded from %s", CONFIG_FILE);
    return 0;
}

int config_save(const KlawedConfig *config) {
    if (!config) return -1;

    // Ensure .klawed directory exists
    struct stat st;
    if (stat(CONFIG_DIR, &st) != 0) {
        if (mkdir(CONFIG_DIR, 0755) != 0 && errno != EEXIST) {
            LOG_ERROR("[Config] Failed to create directory %s: %s", CONFIG_DIR, strerror(errno));
            return -1;
        }
    }

    // Try to read existing config to preserve other fields
    cJSON *root = NULL;
    FILE *fp = fopen(CONFIG_FILE, "r");
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

    // Update input_box_style
    cJSON *existing_style = cJSON_GetObjectItem(root, "input_box_style");
    if (existing_style) {
        cJSON_SetValuestring(existing_style, config_input_style_to_string(config->input_box_style));
    } else {
        cJSON_AddStringToObject(root, "input_box_style", config_input_style_to_string(config->input_box_style));
    }

    // Update theme (only if non-empty)
    if (config->theme[0] != '\0') {
        cJSON *existing_theme = cJSON_GetObjectItem(root, "theme");
        if (existing_theme) {
            cJSON_SetValuestring(existing_theme, config->theme);
        } else {
            cJSON_AddStringToObject(root, "theme", config->theme);
        }
    }

    // Update LLM provider configuration
    cJSON *existing_llm = cJSON_GetObjectItem(root, "llm_provider");
    cJSON *llm_obj = NULL;
    
    if (existing_llm && cJSON_IsObject(existing_llm)) {
        llm_obj = existing_llm;
    } else {
        llm_obj = cJSON_AddObjectToObject(root, "llm_provider");
    }
    
    if (llm_obj) {
        // Update provider type
        cJSON *existing_provider_type = cJSON_GetObjectItem(llm_obj, "provider_type");
        if (existing_provider_type) {
            cJSON_SetValuestring(existing_provider_type, config_provider_type_to_string(config->llm_provider.provider_type));
        } else {
            cJSON_AddStringToObject(llm_obj, "provider_type", config_provider_type_to_string(config->llm_provider.provider_type));
        }
        
        // Update provider name (if non-empty)
        if (config->llm_provider.provider_name[0] != '\0') {
            cJSON *existing_provider_name = cJSON_GetObjectItem(llm_obj, "provider_name");
            if (existing_provider_name) {
                cJSON_SetValuestring(existing_provider_name, config->llm_provider.provider_name);
            } else {
                cJSON_AddStringToObject(llm_obj, "provider_name", config->llm_provider.provider_name);
            }
        }
        
        // Update model (if non-empty)
        if (config->llm_provider.model[0] != '\0') {
            cJSON *existing_model = cJSON_GetObjectItem(llm_obj, "model");
            if (existing_model) {
                cJSON_SetValuestring(existing_model, config->llm_provider.model);
            } else {
                cJSON_AddStringToObject(llm_obj, "model", config->llm_provider.model);
            }
        }
        
        // Update API base (if non-empty)
        if (config->llm_provider.api_base[0] != '\0') {
            cJSON *existing_api_base = cJSON_GetObjectItem(llm_obj, "api_base");
            if (existing_api_base) {
                cJSON_SetValuestring(existing_api_base, config->llm_provider.api_base);
            } else {
                cJSON_AddStringToObject(llm_obj, "api_base", config->llm_provider.api_base);
            }
        }
        
        // Update API key (if non-empty, but warn about security)
        if (config->llm_provider.api_key[0] != '\0') {
            cJSON *existing_api_key = cJSON_GetObjectItem(llm_obj, "api_key");
            if (existing_api_key) {
                cJSON_SetValuestring(existing_api_key, config->llm_provider.api_key);
            } else {
                cJSON_AddStringToObject(llm_obj, "api_key", config->llm_provider.api_key);
            }
            LOG_WARN("[Config] Saving API key to config file - consider using environment variable for better security");
        }
        
        // Update use_bedrock (legacy flag) - store as integer for compatibility
        cJSON *existing_use_bedrock = cJSON_GetObjectItem(llm_obj, "use_bedrock");
        if (existing_use_bedrock) {
            // Remove and re-add to avoid type conversion warnings
            cJSON_DeleteItemFromObject(llm_obj, "use_bedrock");
            cJSON_AddNumberToObject(llm_obj, "use_bedrock", (double)(config->llm_provider.use_bedrock ? 1 : 0));
        } else {
            cJSON_AddNumberToObject(llm_obj, "use_bedrock", (double)(config->llm_provider.use_bedrock ? 1 : 0));
        }
    }

    // Save multiple provider configurations
    if (config->provider_count > 0) {
        cJSON *existing_providers = cJSON_GetObjectItem(root, "providers");
        cJSON *providers_obj = NULL;
        
        if (existing_providers && cJSON_IsObject(existing_providers)) {
            providers_obj = existing_providers;
            // Clear existing providers to avoid duplicates
            cJSON_DeleteItemFromObject(root, "providers");
            providers_obj = cJSON_AddObjectToObject(root, "providers");
        } else {
            providers_obj = cJSON_AddObjectToObject(root, "providers");
        }
        
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
                
                // Add use_bedrock (legacy flag)
                cJSON_AddNumberToObject(provider_obj, "use_bedrock", (double)(provider_config->use_bedrock ? 1 : 0));
            }
        }
    }
    
    // Save active provider (if set)
    if (config->active_provider[0] != '\0') {
        cJSON *existing_active = cJSON_GetObjectItem(root, "active_provider");
        if (existing_active) {
            cJSON_SetValuestring(existing_active, config->active_provider);
        } else {
            cJSON_AddStringToObject(root, "active_provider", config->active_provider);
        }
    }

    // Write to file
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) {
        LOG_ERROR("[Config] Failed to serialize config to JSON");
        return -1;
    }

    fp = fopen(CONFIG_FILE, "w");
    if (!fp) {
        LOG_ERROR("[Config] Failed to open %s for writing: %s", CONFIG_FILE, strerror(errno));
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

    LOG_DEBUG("[Config] Configuration saved to %s", CONFIG_FILE);
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
    } else if (strcmp(str, "custom") == 0) {
        return PROVIDER_CUSTOM;
    }
    return PROVIDER_AUTO;  // "auto" or unknown
}

const NamedProviderConfig* config_find_provider(const KlawedConfig *config, const char *key) {
    if (!config || !key || key[0] == '\0') {
        return NULL;
    }
    
    for (int i = 0; i < config->provider_count; i++) {
        if (strcmp(config->providers[i].key, key) == 0) {
            return &config->providers[i];
        }
    }
    
    return NULL;
}

const NamedProviderConfig* config_get_active_provider(const KlawedConfig *config) {
    if (!config || config->active_provider[0] == '\0') {
        return NULL;
    }
    
    return config_find_provider(config, config->active_provider);
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
