/**
 * streaming_tool_accumulator.c - Testable tool call accumulation for streaming mode
 */

#include "streaming_tool_accumulator.h"
#include "logger.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *skip_ws(const char *s) {
    while (s && *s && isspace((unsigned char)*s)) s++;
    return s;
}

static char *escape_json_string(const char *s, size_t len) {
    size_t needed = 1;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)s[i];
        switch (ch) {
            case '\\':
            case '"':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                needed += 2;
                break;
            default:
                if (ch < 0x20) {
                    needed += 6;
                } else {
                    needed += 1;
                }
                break;
        }
    }

    char *out = malloc(needed);
    if (!out) return NULL;

    char *p = out;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)s[i];
        switch (ch) {
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '"':  *p++ = '\\'; *p++ = '"'; break;
            case '\b': *p++ = '\\'; *p++ = 'b'; break;
            case '\f': *p++ = '\\'; *p++ = 'f'; break;
            case '\n': *p++ = '\\'; *p++ = 'n'; break;
            case '\r': *p++ = '\\'; *p++ = 'r'; break;
            case '\t': *p++ = '\\'; *p++ = 't'; break;
            default:
                if (ch < 0x20) {
                    snprintf(p, 7, "\\u%04x", ch);
                    p += 6;
                } else {
                    *p++ = (char)ch;
                }
                break;
        }
    }
    *p = '\0';
    return out;
}

static char *repair_single_string_argument_json(const char *arguments_str) {
    if (!arguments_str || !arguments_str[0]) return NULL;

    const char *p = skip_ws(arguments_str);
    if (*p != '{') return NULL;
    p = skip_ws(p + 1);
    if (*p != '"') return NULL;

    const char *key_start = ++p;
    while (*p && *p != '"') p++;
    if (*p != '"') return NULL;

    size_t key_len = (size_t)(p - key_start);
    if (key_len == 0) return NULL;

    p = skip_ws(p + 1);
    if (*p != ':') return NULL;
    p = skip_ws(p + 1);
    if (*p == '\0') return NULL;

    const char *value_start = p;
    size_t value_len = strlen(value_start);
    while (value_len > 0 && isspace((unsigned char)value_start[value_len - 1])) {
        value_len--;
    }

    if (value_len == 0) return NULL;
    if (value_start[value_len - 1] == '}') {
        value_len--;
        while (value_len > 0 && isspace((unsigned char)value_start[value_len - 1])) {
            value_len--;
        }
    }
    if (value_len == 0) return NULL;

    if (value_start[0] == '"') return NULL;

    char *escaped_value = escape_json_string(value_start, value_len);
    if (!escaped_value) return NULL;

    size_t out_len = key_len + strlen(escaped_value) + 8;
    char *repaired = malloc(out_len);
    if (!repaired) {
        free(escaped_value);
        return NULL;
    }

    snprintf(repaired, out_len, "{\"%.*s\":\"%s\"}", (int)key_len, key_start, escaped_value);
    free(escaped_value);

    cJSON *parsed = cJSON_Parse(repaired);
    if (!parsed) {
        free(repaired);
        return NULL;
    }
    cJSON_Delete(parsed);
    return repaired;
}

static int tool_call_has_valid_arguments_json(cJSON *function_obj, char **repaired_arguments_out) {
    if (repaired_arguments_out) *repaired_arguments_out = NULL;
    if (!function_obj) return 0;

    cJSON *arguments_obj = cJSON_GetObjectItem(function_obj, "arguments");
    if (!arguments_obj) return 1;
    if (!cJSON_IsString(arguments_obj)) return 0;

    const char *arguments_str = arguments_obj->valuestring;
    if (!arguments_str || arguments_str[0] == '\0') return 1;

    cJSON *parsed = cJSON_Parse(arguments_str);
    if (parsed) {
        cJSON_Delete(parsed);
        return 1;
    }

    char *repaired = repair_single_string_argument_json(arguments_str);
    if (!repaired) {
        return 0;
    }

    if (repaired_arguments_out) {
        *repaired_arguments_out = repaired;
    } else {
        free(repaired);
    }

    return 1;
}

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

        if (id_str[0] && name_str[0] && tool_call_has_valid_arguments_json(function_obj, NULL)) {
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
        char *repaired_arguments = NULL;
        if (id_str[0] && name_str[0] && tool_call_has_valid_arguments_json(function_obj, &repaired_arguments)) {
            cJSON *tool_copy = cJSON_Duplicate(tool, 1);
            if (tool_copy && repaired_arguments) {
                cJSON *function_copy = cJSON_GetObjectItem(tool_copy, "function");
                if (function_copy) {
                    cJSON_ReplaceItemInObject(function_copy, "arguments", cJSON_CreateString(repaired_arguments));
                    LOG_WARN("Repaired malformed streamed tool arguments for id='%s', name='%s'", id_str, name_str);
                }
            }
            if (tool_copy) {
                cJSON_AddItemToArray(filtered, tool_copy);
            }
            free(repaired_arguments);
        } else {
            LOG_WARN("Filtering out invalid streamed tool call: id='%s', name='%s'", id_str, name_str);
        }
    }

    return filtered;
}
