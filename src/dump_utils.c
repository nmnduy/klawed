#include "dump_utils.h"

#include <string.h>
#include <cjson/cJSON.h>

int dump_response_content(const char *response_json, FILE *out) {
    if (!response_json || !out) {
        return 0;
    }

    cJSON *response = cJSON_Parse(response_json);
    if (!response) {
        return 0;
    }

    int printed_response = 0;

    // Anthropic-style: top-level content array
    cJSON *content = cJSON_GetObjectItem(response, "content");
    if (content && cJSON_IsArray(content)) {
        int content_count = cJSON_GetArraySize(content);
        for (int i = 0; i < content_count; i++) {
            cJSON *block = cJSON_GetArrayItem(content, i);
            cJSON *type = cJSON_GetObjectItem(block, "type");

            if (!type || !cJSON_IsString(type)) {
                continue;
            }

            if (strcmp(type->valuestring, "text") == 0) {
                cJSON *text = cJSON_GetObjectItem(block, "text");
                if (text && cJSON_IsString(text)) {
                    fprintf(out, "\n  %s\n", text->valuestring);
                    printed_response = 1;
                }
            } else if (strcmp(type->valuestring, "tool_use") == 0) {
                cJSON *name = cJSON_GetObjectItem(block, "name");
                cJSON *id = cJSON_GetObjectItem(block, "id");
                fprintf(out, "\n  [TOOL_USE: %s", name && cJSON_IsString(name) ? name->valuestring : "unknown");
                if (id && cJSON_IsString(id)) {
                    fprintf(out, " (id: %s)", id->valuestring);
                }
                fprintf(out, "]\n");
                printed_response = 1;
            }
        }
    }

    // OpenAI-style: choices[0].message.content (+ tool_calls)
    if (!printed_response) {
        cJSON *choices = cJSON_GetObjectItem(response, "choices");
        if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
            cJSON *choice = cJSON_GetArrayItem(choices, 0);
            cJSON *message = cJSON_GetObjectItem(choice, "message");
            if (message && cJSON_IsObject(message)) {
                cJSON *msg_content = cJSON_GetObjectItem(message, "content");
                if (msg_content && cJSON_IsString(msg_content)) {
                    fprintf(out, "\n  %s\n", msg_content->valuestring);
                    printed_response = 1;
                } else if (msg_content && cJSON_IsArray(msg_content)) {
                    int msg_part_count = cJSON_GetArraySize(msg_content);
                    for (int k = 0; k < msg_part_count; k++) {
                        cJSON *part = cJSON_GetArrayItem(msg_content, k);
                        cJSON *part_type = cJSON_GetObjectItem(part, "type");
                        if (part_type && cJSON_IsString(part_type) && strcmp(part_type->valuestring, "text") == 0) {
                            cJSON *text = cJSON_GetObjectItem(part, "text");
                            if (text && cJSON_IsString(text)) {
                                fprintf(out, "\n  %s\n", text->valuestring);
                                printed_response = 1;
                            }
                        }
                    }
                }

                cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
                if (tool_calls && cJSON_IsArray(tool_calls)) {
                    int tool_count = cJSON_GetArraySize(tool_calls);
                    for (int t = 0; t < tool_count; t++) {
                        cJSON *tool = cJSON_GetArrayItem(tool_calls, t);
                        cJSON *tool_type = cJSON_GetObjectItem(tool, "type");
                        fprintf(out, "\n  [TOOL_USE");
                        if (tool_type && cJSON_IsString(tool_type)) {
                            fprintf(out, ": %s", tool_type->valuestring);
                        }
                        cJSON *function = cJSON_GetObjectItem(tool, "function");
                        if (function && cJSON_IsObject(function)) {
                            cJSON *func_name = cJSON_GetObjectItem(function, "name");
                            if (func_name && cJSON_IsString(func_name)) {
                                fprintf(out, " %s", func_name->valuestring);
                            }
                        }
                        fprintf(out, "]\n");
                        printed_response = 1;
                    }
                }
            }
        }
    }

    cJSON_Delete(response);
    return printed_response;
}
