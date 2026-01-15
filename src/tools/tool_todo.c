/*
 * tool_todo.c - TodoWrite tool implementation
 */

#include "tool_todo.h"
#include "../klawed_internal.h"
#include "../todo.h"
#include <string.h>
#include <stdlib.h>

cJSON* tool_todo_write(cJSON *params, ConversationState *state) {
    const cJSON *todos_json = cJSON_GetObjectItem(params, "todos");

    if (!todos_json || !cJSON_IsArray(todos_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'todos' parameter (must be array)");
        return error;
    }

    // Ensure todo_list is initialized
    if (!state->todo_list) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Todo list not initialized");
        return error;
    }

    // Clear existing todos
    todo_clear(state->todo_list);

    // Parse and add each todo
    int added = 0;
    int total = cJSON_GetArraySize(todos_json);

    for (int i = 0; i < total; i++) {
        cJSON *todo_item = cJSON_GetArrayItem(todos_json, i);
        if (!cJSON_IsObject(todo_item)) continue;

        const cJSON *content_json = cJSON_GetObjectItem(todo_item, "content");
        const cJSON *active_form_json = cJSON_GetObjectItem(todo_item, "activeForm");
        const cJSON *status_json = cJSON_GetObjectItem(todo_item, "status");

        if (!content_json || !cJSON_IsString(content_json) ||
            !active_form_json || !cJSON_IsString(active_form_json) ||
            !status_json || !cJSON_IsString(status_json)) {
            continue;  // Skip invalid todo items
        }

        const char *content = content_json->valuestring;
        const char *active_form = active_form_json->valuestring;
        const char *status_str = status_json->valuestring;

        // Parse status string to TodoStatus enum
        TodoStatus status;
        if (strcmp(status_str, "completed") == 0) {
            status = TODO_COMPLETED;
        } else if (strcmp(status_str, "in_progress") == 0) {
            status = TODO_IN_PROGRESS;
        } else if (strcmp(status_str, "pending") == 0) {
            status = TODO_PENDING;
        } else {
            continue;  // Invalid status, skip this item
        }

        // Add the todo item
        if (todo_add(state->todo_list, content, active_form, status) == 0) {
            added++;
        }
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddNumberToObject(result, "added", added);
    cJSON_AddNumberToObject(result, "total", total);

    if (state->todo_list && state->todo_list->count > 0) {
        char *rendered = todo_render_to_string(state->todo_list);
        if (rendered) {
            cJSON_AddStringToObject(result, "rendered", rendered);
            free(rendered);
        }
    }

    return result;
}
