/*
 * content_types.c - InternalContent management implementation
 */

#include "content_types.h"
#include "../logger.h"
#include <stdlib.h>
#include <string.h>

void free_internal_contents(InternalContent *results, int count) {
    if (!results) {
        return;
    }

    for (int i = 0; i < count; i++) {
        InternalContent *cb = &results[i];
        free(cb->text);
        free(cb->tool_id);
        free(cb->tool_name);
        if (cb->tool_params) {
            cJSON_Delete(cb->tool_params);
        }
        if (cb->tool_output) {
            cJSON_Delete(cb->tool_output);
        }

        // Handle INTERNAL_IMAGE type
        if (cb->type == INTERNAL_IMAGE) {
            free(cb->image_path);
            free(cb->mime_type);
            free(cb->base64_data);
        }
    }
    free(results);
}

int check_todo_write_executed(InternalContent *results, int count) {
    if (!results) {
        return 0;
    }

    for (int i = 0; i < count; i++) {
        if (results[i].tool_name && strcmp(results[i].tool_name, "TodoWrite") == 0) {
            return 1;
        }
    }
    return 0;
}
