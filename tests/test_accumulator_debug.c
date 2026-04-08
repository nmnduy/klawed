/*
 * test_accumulator_debug.c - Debug tool call accumulation
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

#include "../src/streaming_tool_accumulator.h"

int main(void) {
    printf("=== Testing Tool Call Accumulator ===\n\n");

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);

    /* Simulate chunks from the actual log */
    const char *chunks[] = {
        /* First tool call - initial chunk with id and name */
        "[{\"index\":0,\"id\":\"tool_u7j42aXZVuhshK4fznGMUuxh\",\"type\":\"function\",\"function\":{\"name\":\"Bash\",\"arguments\":\"\"}}]",
        /* First tool call - arguments */
        "[{\"index\":0,\"function\":{\"arguments\":\"{\\\"\"}}]",
        "[{\"index\":0,\"function\":{\"arguments\":\"command\"}}]",
        "[{\"index\":0,\"function\":{\"arguments\":\"\\\":\"}}]",
        "[{\"index\":0,\"function\":{\"arguments\":\" \\\"\"}}]",
        "[{\"index\":0,\"function\":{\"arguments\":\"echo\"}}]",
        "[{\"index\":0,\"function\":{\"arguments\":\" hello\"}}]",
        "[{\"index\":0,\"function\":{\"arguments\":\" world\"}}]",
        "[{\"index\":0,\"function\":{\"arguments\":\"\\\"}\"}}]",
        /* Second tool call - initial chunk with id and name */
        "[{\"index\":1,\"id\":\"tool_ey6rDs8TchxzgFN44MSNJmzB\",\"type\":\"function\",\"function\":{\"name\":\"Read\",\"arguments\":\"\"}}]",
        /* Second tool call - arguments */
        "[{\"index\":1,\"function\":{\"arguments\":\"{\\\"\"}}]",
        "[{\"index\":1,\"function\":{\"arguments\":\"file\"}}]",
        "[{\"index\":1,\"function\":{\"arguments\":\"_path\"}}]",
        "[{\"index\":1,\"function\":{\"arguments\":\"\\\":\"}}]",
        "[{\"index\":1,\"function\":{\"arguments\":\" \\\"\"}}]",
        "[{\"index\":1,\"function\":{\"arguments\":\"README\"}}]",
        "[{\"index\":1,\"function\":{\"arguments\":\".md\"}}]",
        "[{\"index\":1,\"function\":{\"arguments\":\"\\\"}\"}}]"
    };

    int num_chunks = sizeof(chunks) / sizeof(chunks[0]);

    for (int i = 0; i < num_chunks; i++) {
        cJSON *tool_calls = cJSON_Parse(chunks[i]);
        if (!tool_calls) {
            printf("Failed to parse chunk %d: %s\n", i, chunks[i]);
            continue;
        }

        printf("Processing chunk %d...\n", i);

        /* Print what we're processing */
        cJSON *tool = cJSON_GetArrayItem(tool_calls, 0);
        cJSON *idx = cJSON_GetObjectItem(tool, "index");
        cJSON *id = cJSON_GetObjectItem(tool, "id");
        cJSON *func = cJSON_GetObjectItem(tool, "function");

        printf("  Input: index=%d", idx ? idx->valueint : -1);
        if (id && cJSON_IsString(id)) {
            printf(", id='%s'", id->valuestring);
        }
        if (func) {
            cJSON *name = cJSON_GetObjectItem(func, "name");
            cJSON *args = cJSON_GetObjectItem(func, "arguments");
            if (name && cJSON_IsString(name)) {
                printf(", name='%s'", name->valuestring);
            }
            if (args && cJSON_IsString(args)) {
                printf(", args_fragment='%s'", args->valuestring);
            }
        }
        printf("\n");

        /* Process through accumulator */
        tool_accumulator_process_delta(acc, tool_calls);

        /* Show state after this chunk */
        cJSON *all_tools = tool_accumulator_get_tool_calls(acc);
        int count = cJSON_GetArraySize(all_tools);
        printf("  State after: %d tool slots\n", count);
        for (int j = 0; j < count; j++) {
            cJSON *t = cJSON_GetArrayItem(all_tools, j);
            cJSON *tid = cJSON_GetObjectItem(t, "id");
            cJSON *tfunc = cJSON_GetObjectItem(t, "function");
            cJSON *tname = tfunc ? cJSON_GetObjectItem(tfunc, "name") : NULL;

            const char *id_str = (tid && cJSON_IsString(tid)) ? tid->valuestring : "(null)";
            const char *name_str = (tname && cJSON_IsString(tname)) ? tname->valuestring : "(null)";

            printf("    [%d] id='%s', name='%s' %s\n", j, id_str, name_str,
                   (id_str[0] && name_str[0]) ? "[VALID]" : "[EMPTY]");
        }

        cJSON_Delete(tool_calls);
    }

    /* Final results */
    printf("\n=== Final Results ===\n");
    int valid_count = tool_accumulator_count_valid(acc);
    printf("Valid tool calls: %d\n", valid_count);

    cJSON *filtered = tool_accumulator_filter_valid(acc);
    if (filtered) {
        char *str = cJSON_Print(filtered);
        printf("Filtered result:\n%s\n", str);
        free(str);
        cJSON_Delete(filtered);
    }

    tool_accumulator_destroy(acc);

    return (valid_count == 2) ? 0 : 1;
}
