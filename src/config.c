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
