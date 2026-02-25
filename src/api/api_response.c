/*
 * api_response.c - API Response Management
 */

#define _POSIX_C_SOURCE 200809L

#include "api_response.h"
#include "../arena.h"
#include <stdlib.h>
#include <cjson/cJSON.h>

/**
 * Free an ApiResponse structure and all its owned resources
 */
void api_response_free(ApiResponse *response) {
    if (!response) return;

    // If arena is present, destroy it (frees all arena-allocated memory)
    if (response->arena) {
        arena_destroy(response->arena);
        // Note: arena_destroy frees all memory allocated from the arena,
        // including the ApiResponse structure itself if it was allocated from the arena
        return;
    }

    // Free assistant message text
    free(response->message.text);

    // Free tool calls
    if (response->tools) {
        for (int i = 0; i < response->tool_count; i++) {
            free(response->tools[i].id);
            free(response->tools[i].name);
            if (response->tools[i].parameters) {
                cJSON_Delete(response->tools[i].parameters);
            }
        }
        free(response->tools);
    }

    // Free raw response
    if (response->raw_response) {
        cJSON_Delete(response->raw_response);
    }

    // Free error message
    free(response->error_message);

    free(response);
}
