#include "dump_utils.h"

#include <string.h>
#include <stdlib.h>
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

int dump_api_call_json(
    const char *timestamp,
    const char *request_json,
    const char *response_json,
    const char *model,
    const char *status,
    const char *error_msg,
    int call_num,
    FILE *out
) {
    (void)call_num;  // Unused parameter
    if (!out) {
        return 0;
    }

    cJSON *call_obj = cJSON_CreateObject();
    if (!call_obj) {
        return 0;
    }

    // Add basic fields
    if (timestamp) {
        cJSON_AddStringToObject(call_obj, "timestamp", timestamp);
    }
    if (model) {
        cJSON_AddStringToObject(call_obj, "model", model);
    }
    if (status) {
        cJSON_AddStringToObject(call_obj, "status", status);
    }
    if (error_msg) {
        cJSON_AddStringToObject(call_obj, "error_message", error_msg);
    }

    // Parse and add request
    if (request_json) {
        cJSON *request = cJSON_Parse(request_json);
        if (request) {
            cJSON_AddItemToObject(call_obj, "request", request);
        }
    }

    // Parse and add response
    if (response_json) {
        cJSON *response = cJSON_Parse(response_json);
        if (response) {
            cJSON_AddItemToObject(call_obj, "response", response);
        }
    }

    char *json_str = cJSON_Print(call_obj);
    if (!json_str) {
        cJSON_Delete(call_obj);
        return 0;
    }

    fprintf(out, "%s", json_str);
    free(json_str);
    cJSON_Delete(call_obj);
    return 1;
}

int dump_api_call_markdown(
    const char *timestamp,
    const char *request_json,
    const char *response_json,
    const char *model,
    const char *status,
    const char *error_msg,
    int call_num,
    FILE *out
) {
    if (!out) {
        return 0;
    }

    fprintf(out, "## Call %d - %s\n\n", call_num, timestamp ? timestamp : "unknown");
    fprintf(out, "**Model:** %s  \n", model ? model : "unknown");
    fprintf(out, "**Status:** %s  \n\n", status ? status : "unknown");

    if (error_msg) {
        fprintf(out, "**Error:** %s  \n\n", error_msg);
    }

    // Parse and display request
    if (request_json) {
        fprintf(out, "### Request\n\n");
        cJSON *request = cJSON_Parse(request_json);
        if (request) {
            cJSON *messages = cJSON_GetObjectItem(request, "messages");
            if (messages && cJSON_IsArray(messages)) {
                int msg_count = cJSON_GetArraySize(messages);
                for (int i = 0; i < msg_count; i++) {
                    cJSON *msg = cJSON_GetArrayItem(messages, i);
                    cJSON *role = cJSON_GetObjectItem(msg, "role");
                    cJSON *content = cJSON_GetObjectItem(msg, "content");

                    if (role && cJSON_IsString(role)) {
                        fprintf(out, "#### %s\n\n", role->valuestring);
                    }

                    if (content) {
                        if (cJSON_IsString(content)) {
                            fprintf(out, "%s\n\n", content->valuestring);
                        } else if (cJSON_IsArray(content)) {
                            int content_count = cJSON_GetArraySize(content);
                            for (int j = 0; j < content_count; j++) {
                                cJSON *block = cJSON_GetArrayItem(content, j);
                                cJSON *type = cJSON_GetObjectItem(block, "type");

                                if (type && cJSON_IsString(type)) {
                                    if (strcmp(type->valuestring, "text") == 0) {
                                        cJSON *text = cJSON_GetObjectItem(block, "text");
                                        if (text && cJSON_IsString(text)) {
                                            fprintf(out, "%s\n\n", text->valuestring);
                                        }
                                    } else if (strcmp(type->valuestring, "tool_use") == 0) {
                                        cJSON *name = cJSON_GetObjectItem(block, "name");
                                        cJSON *id = cJSON_GetObjectItem(block, "id");
                                        fprintf(out, "**[TOOL_USE: %s", name && cJSON_IsString(name) ? name->valuestring : "unknown");
                                        if (id && cJSON_IsString(id)) {
                                            fprintf(out, " (id: %s)", id->valuestring);
                                        }
                                        fprintf(out, "]**\n\n");
                                    } else if (strcmp(type->valuestring, "tool_result") == 0) {
                                        cJSON *tool_use_id = cJSON_GetObjectItem(block, "tool_use_id");
                                        fprintf(out, "**[TOOL_RESULT");
                                        if (tool_use_id && cJSON_IsString(tool_use_id)) {
                                            fprintf(out, " for %s", tool_use_id->valuestring);
                                        }
                                        fprintf(out, "]**\n\n");
                                    }
                                }
                            }
                        }
                    }
                }
            }
            cJSON_Delete(request);
        }
    }

    // Parse and display response
    if (response_json) {
        fprintf(out, "### Response\n\n");
        if (status && strcmp(status, "error") == 0 && error_msg) {
            fprintf(out, "**ERROR:** %s\n\n", error_msg);
        } else {
            // Use existing dump_response_content for consistency
            dump_response_content(response_json, out);
            fprintf(out, "\n");
        }
    }

    fprintf(out, "---\n\n");
    return 1;
}
