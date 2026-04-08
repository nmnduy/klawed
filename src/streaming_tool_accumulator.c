/**
 * streaming_tool_accumulator.c - Testable tool call accumulation for streaming mode
 */

#include "streaming_tool_accumulator.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>

ToolCallAccumulator* tool_accumulator_create(Arena *arena) {
    ToolCallAccumulator *acc = calloc(1, sizeof(ToolCallAccumulator));
    if (!acc) return NULL;

    acc->arena = arena;
    acc->tool_calls_array = cJSON_CreateArray();
    if (!acc->tool_calls_array) {
        free(acc);
        return NULL;
    }
    acc->tool_calls_count = 0;
    acc->has_reasoning = 0;

    return acc;
}

void tool_accumulator_destroy(ToolCallAccumulator *acc) {
    if (!acc) return;

    if (acc->tool_calls_array) {
        cJSON_Delete(acc->tool_calls_array);
    }

    free(acc);
}

void tool_accumulator_reset(ToolCallAccumulator *acc) {
    if (!acc) return;

    if (acc->tool_calls_array) {
        cJSON_Delete(acc->tool_calls_array);
    }
    acc->tool_calls_array = cJSON_CreateArray();
    acc->tool_calls_count = 0;
    acc->has_reasoning = 0;
}

int tool_accumulator_process_delta(ToolCallAccumulator *acc, cJSON *tool_calls) {
    if (!acc || !tool_calls || !cJSON_IsArray(tool_calls)) {
        return -1;
    }

    cJSON *tool_call = NULL;
    cJSON_ArrayForEach(tool_call, tool_calls) {
        cJSON *index_obj = cJSON_GetObjectItem(tool_call, "index");
        if (!index_obj || !cJSON_IsNumber(index_obj)) continue;

        int index = index_obj->valueint;

        // Ensure array has enough space
        while (cJSON_GetArraySize(acc->tool_calls_array) <= index) {
            cJSON *new_tool = cJSON_CreateObject();
            cJSON_AddStringToObject(new_tool, "id", "");
            cJSON_AddStringToObject(new_tool, "type", "function");
            cJSON *function = cJSON_CreateObject();
            cJSON_AddStringToObject(function, "name", "");
            cJSON_AddStringToObject(function, "arguments", "");
            cJSON_AddItemToObject(new_tool, "function", function);
            cJSON_AddItemToArray(acc->tool_calls_array, new_tool);
        }

        cJSON *existing_tool = cJSON_GetArrayItem(acc->tool_calls_array, index);
        if (!existing_tool) continue;

        // Update id if present
        cJSON *id_obj = cJSON_GetObjectItem(tool_call, "id");
        if (id_obj && cJSON_IsString(id_obj) && id_obj->valuestring[0]) {
            cJSON_ReplaceItemInObject(existing_tool, "id", cJSON_CreateString(id_obj->valuestring));
        }

        // Update function data
        cJSON *function_delta = cJSON_GetObjectItem(tool_call, "function");
        if (function_delta) {
            cJSON *existing_function = cJSON_GetObjectItem(existing_tool, "function");
            if (!existing_function) {
                existing_function = cJSON_CreateObject();
                cJSON_AddItemToObject(existing_tool, "function", existing_function);
            }

            cJSON *name_obj = cJSON_GetObjectItem(function_delta, "name");
            if (name_obj && cJSON_IsString(name_obj) && name_obj->valuestring[0]) {
                cJSON_ReplaceItemInObject(existing_function, "name", cJSON_CreateString(name_obj->valuestring));
            }

            cJSON *args_obj = cJSON_GetObjectItem(function_delta, "arguments");
            if (args_obj && cJSON_IsString(args_obj)) {
                // Append to existing arguments
                cJSON *existing_args = cJSON_GetObjectItem(existing_function, "arguments");
                if (existing_args && cJSON_IsString(existing_args)) {
                    size_t old_len = strlen(existing_args->valuestring);
                    size_t new_len = strlen(args_obj->valuestring);
                    char *combined = malloc(old_len + new_len + 1);
                    if (combined) {
                        memcpy(combined, existing_args->valuestring, old_len);
                        memcpy(combined + old_len, args_obj->valuestring, new_len);
                        combined[old_len + new_len] = '\0';
                        cJSON_ReplaceItemInObject(existing_function, "arguments", cJSON_CreateString(combined));
                        free(combined);
                    }
                } else {
                    cJSON_AddStringToObject(existing_function, "arguments", args_obj->valuestring);
                }
            }
        }
    }

    acc->tool_calls_count = cJSON_GetArraySize(acc->tool_calls_array);
    return 0;
}

cJSON* tool_accumulator_get_tool_calls(ToolCallAccumulator *acc) {
    if (!acc) return NULL;
    return acc->tool_calls_array;
}

int tool_accumulator_count_valid(ToolCallAccumulator *acc) {
    if (!acc || !acc->tool_calls_array) return 0;

    int valid_count = 0;
    int tool_count = cJSON_GetArraySize(acc->tool_calls_array);

    for (int i = 0; i < tool_count; i++) {
        cJSON *tool = cJSON_GetArrayItem(acc->tool_calls_array, i);
        if (!tool) continue;

        cJSON *id_obj = cJSON_GetObjectItem(tool, "id");
        cJSON *function_obj = cJSON_GetObjectItem(tool, "function");
        cJSON *name_obj = function_obj ? cJSON_GetObjectItem(function_obj, "name") : NULL;

        const char *id_str = (id_obj && cJSON_IsString(id_obj)) ? id_obj->valuestring : "";
        const char *name_str = (name_obj && cJSON_IsString(name_obj)) ? name_obj->valuestring : "";

        if (id_str[0] && name_str[0]) {
            valid_count++;
        }
    }

    return valid_count;
}

cJSON* tool_accumulator_filter_valid(ToolCallAccumulator *acc) {
    if (!acc || !acc->tool_calls_array) return cJSON_CreateArray();

    cJSON *filtered = cJSON_CreateArray();
    int tool_count = cJSON_GetArraySize(acc->tool_calls_array);

    for (int i = 0; i < tool_count; i++) {
        cJSON *tool = cJSON_GetArrayItem(acc->tool_calls_array, i);
        if (!tool) continue;

        cJSON *id_obj = cJSON_GetObjectItem(tool, "id");
        cJSON *function_obj = cJSON_GetObjectItem(tool, "function");
        cJSON *name_obj = function_obj ? cJSON_GetObjectItem(function_obj, "name") : NULL;

        const char *id_str = (id_obj && cJSON_IsString(id_obj)) ? id_obj->valuestring : "";
        const char *name_str = (name_obj && cJSON_IsString(name_obj)) ? name_obj->valuestring : "";

        // Only include tool calls with non-empty id and name
        if (id_str[0] && name_str[0]) {
            cJSON_AddItemToArray(filtered, cJSON_Duplicate(tool, 1));
        } else {
            LOG_WARN("Filtering out tool call with empty id='%s' or name='%s'", id_str, name_str);
        }
    }

    return filtered;
}
