/*
 * Dynamic Tools - Load tool definitions from JSON files
 *
 * Allows klawed to load custom tool definitions from external JSON files
 * without requiring recompilation.
 */

#include "dynamic_tools.h"
#include "logger.h"
#include "data_dir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <sys/stat.h>
#include <errno.h>

// Environment variable name for dynamic tools path
#define DYNAMIC_TOOLS_ENV_VAR "KLAWED_DYNAMIC_TOOLS"

// Default filenames
#define DYNAMIC_TOOLS_FILE_NAME "dynamic_tools.json"
#define GLOBAL_CONFIG_DIR_NAME ".klawed"

void dynamic_tools_init(DynamicToolsRegistry *registry) {
    if (!registry) return;

    registry->count = 0;
    registry->source_path[0] = '\0';
    registry->loaded = 0;

    for (int i = 0; i < DYNAMIC_TOOLS_MAX; i++) {
        registry->tools[i].name[0] = '\0';
        registry->tools[i].description[0] = '\0';
        registry->tools[i].parameters = NULL;
        registry->tools[i].cache_control = NULL;
    }
}

void dynamic_tools_cleanup(DynamicToolsRegistry *registry) {
    if (!registry) return;

    for (int i = 0; i < registry->count; i++) {
        if (registry->tools[i].parameters) {
            cJSON_Delete(registry->tools[i].parameters);
            registry->tools[i].parameters = NULL;
        }
        if (registry->tools[i].cache_control) {
            cJSON_Delete(registry->tools[i].cache_control);
            registry->tools[i].cache_control = NULL;
        }
    }

    registry->count = 0;
    registry->loaded = 0;
}

/**
 * Parse a single tool definition from JSON
 *
 * Expected format (OpenAI-compatible):
 * {
 *   "type": "function",
 *   "function": {
 *     "name": "tool_name",
 *     "description": "Tool description",
 *     "parameters": { ... JSON schema ... }
 *   }
 * }
 *
 * Or simplified format:
 * {
 *   "name": "tool_name",
 *   "description": "Tool description",
 *   "parameters": { ... JSON schema ... }
 * }
 *
 * @param tool_json cJSON object representing the tool
 * @param tool_def Pointer to tool definition struct to populate
 * @return 0 on success, -1 on failure
 */
static int parse_tool_definition(cJSON *tool_json, DynamicToolDef *tool_def) {
    if (!tool_json || !tool_def) return -1;

    // Try OpenAI format first: { type: "function", function: { name, description, parameters } }
    cJSON *func_obj = cJSON_GetObjectItem(tool_json, "function");

    if (func_obj && cJSON_IsObject(func_obj)) {
        // OpenAI format
        cJSON *name_item = cJSON_GetObjectItem(func_obj, "name");
        cJSON *desc_item = cJSON_GetObjectItem(func_obj, "description");
        cJSON *params_item = cJSON_GetObjectItem(func_obj, "parameters");

        if (!name_item || !cJSON_IsString(name_item)) {
            LOG_WARN("[DynamicTools] Tool missing required 'name' field in function object");
            return -1;
        }

        strlcpy(tool_def->name, name_item->valuestring, DYNAMIC_TOOL_NAME_MAX);

        if (desc_item && cJSON_IsString(desc_item)) {
            strlcpy(tool_def->description, desc_item->valuestring, DYNAMIC_TOOL_DESC_MAX);
        } else {
            tool_def->description[0] = '\0';
        }

        if (params_item && cJSON_IsObject(params_item)) {
            tool_def->parameters = cJSON_Duplicate(params_item, 1);
        } else {
            // Create empty parameters object
            tool_def->parameters = cJSON_CreateObject();
            cJSON_AddStringToObject(tool_def->parameters, "type", "object");
            cJSON *props = cJSON_CreateObject();
            cJSON_AddItemToObject(tool_def->parameters, "properties", props);
        }

        // Check for cache_control in the function object
        cJSON *cache_item = cJSON_GetObjectItem(func_obj, "cache_control");
        if (cache_item) {
            tool_def->cache_control = cJSON_Duplicate(cache_item, 1);
        }
    } else {
        // Simplified format: { name, description, parameters }
        cJSON *name_item = cJSON_GetObjectItem(tool_json, "name");
        cJSON *desc_item = cJSON_GetObjectItem(tool_json, "description");
        cJSON *params_item = cJSON_GetObjectItem(tool_json, "parameters");

        if (!name_item || !cJSON_IsString(name_item)) {
            LOG_WARN("[DynamicTools] Tool missing required 'name' field");
            return -1;
        }

        strlcpy(tool_def->name, name_item->valuestring, DYNAMIC_TOOL_NAME_MAX);

        if (desc_item && cJSON_IsString(desc_item)) {
            strlcpy(tool_def->description, desc_item->valuestring, DYNAMIC_TOOL_DESC_MAX);
        } else {
            tool_def->description[0] = '\0';
        }

        if (params_item && cJSON_IsObject(params_item)) {
            tool_def->parameters = cJSON_Duplicate(params_item, 1);
        } else {
            // Create empty parameters object
            tool_def->parameters = cJSON_CreateObject();
            cJSON_AddStringToObject(tool_def->parameters, "type", "object");
            cJSON *props = cJSON_CreateObject();
            cJSON_AddItemToObject(tool_def->parameters, "properties", props);
        }

        // Check for cache_control at root level
        cJSON *cache_item = cJSON_GetObjectItem(tool_json, "cache_control");
        if (cache_item) {
            tool_def->cache_control = cJSON_Duplicate(cache_item, 1);
        }
    }

    // Validate tool name (alphanumeric, underscore, hyphen only)
    for (size_t i = 0; i < strlen(tool_def->name); i++) {
        char c = tool_def->name[i];
        if (!(c >= 'a' && c <= 'z') &&
            !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') &&
            c != '_' && c != '-') {
            LOG_WARN("[DynamicTools] Invalid character '%c' in tool name '%s'",
                     c, tool_def->name);
            return -1;
        }
    }

    return 0;
}

int dynamic_tools_load_from_string(DynamicToolsRegistry *registry, const char *json_str) {
    if (!registry || !json_str) {
        LOG_ERROR("[DynamicTools] NULL registry or json_str");
        return -1;
    }

    // Cleanup any existing tools
    dynamic_tools_cleanup(registry);

    // Parse JSON
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_ERROR("[DynamicTools] Failed to parse JSON: %s", cJSON_GetErrorPtr());
        return -1;
    }

    // Handle both array and single object formats
    cJSON *tools_array = NULL;
    if (cJSON_IsArray(root)) {
        tools_array = root;
    } else if (cJSON_IsObject(root)) {
        // Single tool wrapped in object - check if it's a tools container
        cJSON *tools_item = cJSON_GetObjectItem(root, "tools");
        if (tools_item && cJSON_IsArray(tools_item)) {
            tools_array = tools_item;
        } else {
            // Treat the root object as a single tool
            tools_array = cJSON_CreateArray();
            cJSON_AddItemToArray(tools_array, cJSON_Duplicate(root, 1));
        }
    }

    if (!tools_array) {
        LOG_ERROR("[DynamicTools] Invalid JSON format: expected array or object with 'tools' field");
        cJSON_Delete(root);
        return -1;
    }

    // Parse each tool
    cJSON *tool_json = NULL;
    cJSON_ArrayForEach(tool_json, tools_array) {
        if (registry->count >= DYNAMIC_TOOLS_MAX) {
            LOG_WARN("[DynamicTools] Maximum number of tools (%d) reached, skipping remaining",
                     DYNAMIC_TOOLS_MAX);
            break;
        }

        if (!cJSON_IsObject(tool_json)) {
            LOG_WARN("[DynamicTools] Skipping non-object item in tools array");
            continue;
        }

        DynamicToolDef *tool_def = &registry->tools[registry->count];
        if (parse_tool_definition(tool_json, tool_def) == 0) {
            LOG_INFO("[DynamicTools] Loaded tool: %s", tool_def->name);
            registry->count++;
        } else {
            LOG_WARN("[DynamicTools] Failed to parse tool definition at index %d", registry->count);
        }
    }

    // If we created a temporary array for single tool, clean it up
    if (tools_array != root && tools_array != cJSON_GetObjectItem(root, "tools")) {
        cJSON_Delete(tools_array);
    }

    cJSON_Delete(root);

    registry->loaded = (registry->count > 0) ? 1 : 0;

    LOG_INFO("[DynamicTools] Successfully loaded %d dynamic tool(s)", registry->count);
    return registry->count;
}

int dynamic_tools_load_from_file(DynamicToolsRegistry *registry, const char *file_path) {
    if (!registry || !file_path) {
        LOG_ERROR("[DynamicTools] NULL registry or file_path");
        return -1;
    }

    // Check if file exists and is readable
    struct stat st;
    if (stat(file_path, &st) != 0) {
        LOG_DEBUG("[DynamicTools] File not found: %s", file_path);
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        LOG_WARN("[DynamicTools] Not a regular file: %s", file_path);
        return -1;
    }

    if (st.st_size == 0) {
        LOG_DEBUG("[DynamicTools] Empty file: %s", file_path);
        return 0; // Empty file is not an error, just no tools
    }

    if (st.st_size > 1024 * 1024) { // Max 1MB
        LOG_ERROR("[DynamicTools] File too large: %s (%ld bytes)", file_path, (long)st.st_size);
        return -1;
    }

    // Read file contents
    FILE *fp = fopen(file_path, "r");
    if (!fp) {
        LOG_ERROR("[DynamicTools] Failed to open file: %s (%s)", file_path, strerror(errno));
        return -1;
    }

    char *json_str = malloc((size_t)st.st_size + 1);
    if (!json_str) {
        LOG_ERROR("[DynamicTools] Failed to allocate memory for file contents");
        fclose(fp);
        return -1;
    }

    size_t bytes_read = fread(json_str, 1, (size_t)st.st_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)st.st_size) {
        LOG_ERROR("[DynamicTools] Failed to read complete file");
        free(json_str);
        return -1;
    }

    json_str[st.st_size] = '\0';

    // Load from string
    int result = dynamic_tools_load_from_string(registry, json_str);
    free(json_str);

    if (result >= 0) {
        strlcpy(registry->source_path, file_path, DYNAMIC_TOOLS_PATH_MAX);
        LOG_INFO("[DynamicTools] Loaded %d tool(s) from %s", result, file_path);
    }

    return result;
}

int dynamic_tools_add_to_array(const DynamicToolsRegistry *registry, cJSON *tool_array) {
    if (!registry || !tool_array || !cJSON_IsArray(tool_array)) {
        return 0;
    }

    int added = 0;
    for (int i = 0; i < registry->count; i++) {
        const DynamicToolDef *tool_def = &registry->tools[i];

        // Create OpenAI-compatible tool definition
        cJSON *tool = cJSON_CreateObject();
        if (!tool) continue;

        cJSON_AddStringToObject(tool, "type", "function");

        cJSON *func = cJSON_CreateObject();
        if (!func) {
            cJSON_Delete(tool);
            continue;
        }

        cJSON_AddStringToObject(func, "name", tool_def->name);
        cJSON_AddStringToObject(func, "description", tool_def->description);

        if (tool_def->parameters) {
            cJSON_AddItemToObject(func, "parameters", cJSON_Duplicate(tool_def->parameters, 1));
        }

        if (tool_def->cache_control) {
            cJSON_AddItemToObject(func, "cache_control", cJSON_Duplicate(tool_def->cache_control, 1));
        }

        cJSON_AddItemToObject(tool, "function", func);
        cJSON_AddItemToArray(tool_array, tool);

        added++;
        LOG_DEBUG("[DynamicTools] Added tool '%s' to array", tool_def->name);
    }

    return added;
}

/**
 * Get the path to the global dynamic tools file (~/.klawed/dynamic_tools.json)
 *
 * @param buf Buffer to store the path
 * @param buf_size Size of the buffer
 * @return 0 on success, -1 on failure
 */
static int get_global_path(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return -1;

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        LOG_DEBUG("[DynamicTools] HOME environment variable not set");
        return -1;
    }

    size_t needed = strlcpy(buf, home, buf_size);
    if (needed >= buf_size) return -1;

    needed = strlcat(buf, "/", buf_size);
    if (needed >= buf_size) return -1;

    needed = strlcat(buf, GLOBAL_CONFIG_DIR_NAME, buf_size);
    if (needed >= buf_size) return -1;

    needed = strlcat(buf, "/", buf_size);
    if (needed >= buf_size) return -1;

    needed = strlcat(buf, DYNAMIC_TOOLS_FILE_NAME, buf_size);
    if (needed >= buf_size) return -1;

    return 0;
}

int dynamic_tools_get_path(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return -1;

    // 1. Check environment variable first
    const char *env_path = getenv(DYNAMIC_TOOLS_ENV_VAR);
    if (env_path && env_path[0] != '\0') {
        strlcpy(buf, env_path, buf_size);
        LOG_DEBUG("[DynamicTools] Using path from environment: %s", buf);
        return 0;
    }

    // 2. Check for local project file (.klawed/dynamic_tools.json)
    if (data_dir_build_path(buf, buf_size, DYNAMIC_TOOLS_FILE_NAME) == 0) {
        struct stat st;
        if (stat(buf, &st) == 0 && S_ISREG(st.st_mode)) {
            LOG_DEBUG("[DynamicTools] Using local project file: %s", buf);
            return 0;
        }
    }

    // 3. Check for global file (~/.klawed/dynamic_tools.json)
    if (get_global_path(buf, buf_size) == 0) {
        struct stat st;
        if (stat(buf, &st) == 0 && S_ISREG(st.st_mode)) {
            LOG_DEBUG("[DynamicTools] Using global file: %s", buf);
            return 0;
        }
    }

    // No dynamic tools file found
    buf[0] = '\0';
    return -1;
}

int dynamic_tools_has_tool(const DynamicToolsRegistry *registry, const char *name) {
    if (!registry || !name || !registry->loaded) {
        return 0;
    }

    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->tools[i].name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

const DynamicToolDef* dynamic_tools_get_tool(const DynamicToolsRegistry *registry, const char *name) {
    if (!registry || !name || !registry->loaded) {
        return NULL;
    }

    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->tools[i].name, name) == 0) {
            return &registry->tools[i];
        }
    }

    return NULL;
}
