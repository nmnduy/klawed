/*
 * openai_chat_parser.c - Shared parsing helpers for OpenAI chat-completions responses
 */

#define _POSIX_C_SOURCE 200809L

#include "openai_chat_parser.h"
#include "logger.h"
#include "arena.h"
#include "util/string_utils.h"

#include <bsd/string.h>
#include <string.h>

static char *arena_strdup_local(Arena *arena, const char *str) {
    if (!arena || !str) return NULL;

    size_t len = strlen(str) + 1;
    char *out = arena_alloc(arena, len);
    if (!out) return NULL;

    strlcpy(out, str, len);
    return out;
}

int openai_tool_arguments_are_valid_json(cJSON *arguments) {
    if (!arguments) return 1;
    if (!cJSON_IsString(arguments)) return 0;
    if (!arguments->valuestring || arguments->valuestring[0] == '\0') return 1;

    cJSON *parsed = cJSON_Parse(arguments->valuestring);
    if (!parsed) return 0;

    cJSON_Delete(parsed);
    return 1;
}

int openai_fill_api_response_from_message(ApiResponse *api_response, cJSON *message,
                                          const char *log_prefix) {
    const char *prefix = log_prefix ? log_prefix : "OpenAI";

    if (!api_response || !api_response->arena || !message) {
        return -1;
    }

    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (content && cJSON_IsString(content) && content->valuestring) {
        api_response->message.text = arena_strdup_local(api_response->arena, content->valuestring);
        if (api_response->message.text) {
            trim_whitespace(api_response->message.text);
        }
    } else {
        api_response->message.text = NULL;
    }

    cJSON *reasoning_content = cJSON_GetObjectItem(message, "reasoning_content");
    if (reasoning_content && cJSON_IsString(reasoning_content) && reasoning_content->valuestring) {
        api_response->message.reasoning_content =
            arena_strdup_local(api_response->arena, reasoning_content->valuestring);
        if (api_response->message.reasoning_content) {
            LOG_DEBUG("%s: extracted reasoning_content from response (%zu bytes)",
                      prefix, strlen(api_response->message.reasoning_content));
        }
    }

    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    if (!tool_calls || !cJSON_IsArray(tool_calls)) {
        return 0;
    }

    int raw_tool_count = cJSON_GetArraySize(tool_calls);
    int valid_count = 0;

    for (int i = 0; i < raw_tool_count; i++) {
        cJSON *tool_call = cJSON_GetArrayItem(tool_calls, i);
        cJSON *id = cJSON_GetObjectItem(tool_call, "id");
        cJSON *function = cJSON_GetObjectItem(tool_call, "function");

        if (!id || !cJSON_IsString(id) || !id->valuestring[0]) {
            LOG_WARN("%s: skipping tool_call at index %d: missing or empty 'id'", prefix, i);
            continue;
        }
        if (!function) {
            LOG_WARN("%s: skipping tool_call at index %d: missing 'function' field", prefix, i);
            continue;
        }

        cJSON *name = cJSON_GetObjectItem(function, "name");
        if (!name || !cJSON_IsString(name) || !name->valuestring[0]) {
            LOG_WARN("%s: skipping tool_call at index %d: missing or empty 'name'", prefix, i);
            continue;
        }

        cJSON *arguments = cJSON_GetObjectItem(function, "arguments");
        if (!openai_tool_arguments_are_valid_json(arguments)) {
            LOG_WARN("%s: skipping tool_call at index %d: invalid JSON in 'arguments'", prefix, i);
            continue;
        }

        valid_count++;
    }

    if (valid_count == 0) {
        return 0;
    }

    api_response->tools = arena_alloc(api_response->arena, (size_t)valid_count * sizeof(ToolCall));
    if (!api_response->tools) {
        return -1;
    }
    memset(api_response->tools, 0, (size_t)valid_count * sizeof(ToolCall));

    int tool_idx = 0;
    for (int i = 0; i < raw_tool_count; i++) {
        cJSON *tool_call = cJSON_GetArrayItem(tool_calls, i);
        cJSON *id = cJSON_GetObjectItem(tool_call, "id");
        cJSON *function = cJSON_GetObjectItem(tool_call, "function");
        cJSON *name = function ? cJSON_GetObjectItem(function, "name") : NULL;
        cJSON *arguments = function ? cJSON_GetObjectItem(function, "arguments") : NULL;

        if (!id || !cJSON_IsString(id) || !id->valuestring[0]) continue;
        if (!function) continue;
        if (!name || !cJSON_IsString(name) || !name->valuestring[0]) continue;
        if (!openai_tool_arguments_are_valid_json(arguments)) continue;

        api_response->tools[tool_idx].id =
            arena_strdup_local(api_response->arena, id->valuestring);
        api_response->tools[tool_idx].name =
            arena_strdup_local(api_response->arena, name->valuestring);

        if (arguments && cJSON_IsString(arguments)) {
            api_response->tools[tool_idx].parameters = cJSON_Parse(arguments->valuestring);
            if (!api_response->tools[tool_idx].parameters) {
                LOG_WARN("%s: skipping tool_call at index %d: invalid JSON in 'arguments'", prefix, i);
                continue;
            }
        } else {
            api_response->tools[tool_idx].parameters = cJSON_CreateObject();
        }

        tool_idx++;
    }

    api_response->tool_count = tool_idx;
    return 0;
}

ApiResponse* openai_parse_chat_completion_response(cJSON *raw_json, const char *log_prefix) {
    const char *prefix = log_prefix ? log_prefix : "OpenAI";

    if (!raw_json) return NULL;

    Arena *arena = arena_create(16384);
    if (!arena) return NULL;

    ApiResponse *api_response = arena_alloc(arena, sizeof(ApiResponse));
    if (!api_response) {
        arena_destroy(arena);
        return NULL;
    }

    memset(api_response, 0, sizeof(ApiResponse));
    api_response->arena = arena;
    api_response->raw_response = raw_json;

    cJSON *choices = cJSON_GetObjectItem(raw_json, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        LOG_WARN("%s: response has no choices", prefix);
        return api_response;
    }

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");
    if (!message) {
        LOG_WARN("%s: response choice has no message", prefix);
        return api_response;
    }

    if (openai_fill_api_response_from_message(api_response, message, prefix) != 0) {
        api_response_free(api_response);
        return NULL;
    }

    return api_response;
}
