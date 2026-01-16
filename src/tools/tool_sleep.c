/*
 * tool_sleep.c - Sleep tool implementation
 */

#include "tool_sleep.h"
#include "../klawed_internal.h"
#include <time.h>

cJSON* tool_sleep(cJSON *params, ConversationState *state) {
    (void)state;
    cJSON *duration_json = cJSON_GetObjectItem(params, "duration");
    if (!duration_json || !cJSON_IsNumber(duration_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'duration' parameter (must be number of seconds)");
        return error;
    }
    int duration = duration_json->valueint;
    if (duration < 0) duration = 0;
    struct timespec req = { .tv_sec = duration, .tv_nsec = 0 };
    // Sleep for the duration (seconds)
    nanosleep(&req, NULL);
    // Return success result
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddNumberToObject(result, "duration", duration);
    return result;
}
