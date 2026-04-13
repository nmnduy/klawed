/*
 * test_tool_definition_parity.c - Ensure Messages vs Responses API tools stay in sync
 */

#define _POSIX_C_SOURCE 200809L
#define TEST_BUILD 1

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/stdlib.h>

#include <cjson/cJSON.h>

#include "../src/klawed_internal.h"
#include "../src/tools/tool_definitions.h"
#include "../src/openai_responses.h"

// Collect tool names into a malloc'd array of strings. Caller frees the array
// but not the strings (they point into cJSON objects managed by tool_defs).
static const char **collect_tool_names(cJSON *tool_defs, int *out_count) {
    if (!tool_defs || !cJSON_IsArray(tool_defs)) {
        *out_count = 0;
        return NULL;
    }

    int count = cJSON_GetArraySize(tool_defs);
    const char **names = calloc((size_t)count, sizeof(const char *));
    if (!names) {
        *out_count = 0;
        return NULL;
    }

    int idx = 0;
    cJSON *t = NULL;
    cJSON_ArrayForEach(t, tool_defs) {
        if (idx >= count) break;
        // Messages API format stores name under function.name
        cJSON *name = cJSON_GetObjectItem(t, "name");
        if (cJSON_IsString(name)) {
            names[idx++] = name->valuestring;
            continue;
        }
        cJSON *func = cJSON_GetObjectItem(t, "function");
        if (func) {
            cJSON *fname = cJSON_GetObjectItem(func, "name");
            if (cJSON_IsString(fname)) {
                names[idx++] = fname->valuestring;
            }
        }
    }

    *out_count = idx;
    return names;
}

static int contains(const char *const *names, int count, const char *needle) {
    for (int i = 0; i < count; i++) {
        if (names[i] && strcmp(names[i], needle) == 0) {
            return 1;
        }
    }
    return 0;
}

static void assert_same_sets(cJSON *msg_defs, cJSON *resp_defs) {
    int msg_count = 0, resp_count = 0;
    const char **msg_names = collect_tool_names(msg_defs, &msg_count);
    const char **resp_names = collect_tool_names(resp_defs, &resp_count);

    assert(msg_names != NULL);
    assert(resp_names != NULL);

    // Every messages tool must exist in responses
    for (int i = 0; i < msg_count; i++) {
        assert(contains(resp_names, resp_count, msg_names[i]));
    }
    // And vice-versa
    for (int i = 0; i < resp_count; i++) {
        assert(contains(msg_names, msg_count, resp_names[i]));
    }

    free(msg_names);
    free(resp_names);
}

static void run_parity_case(const char *case_name, ConversationState *state) {
    printf("Running parity case: %s... ", case_name);

    cJSON *msg_defs = get_tool_definitions(state, 0);
    cJSON *resp_defs = get_tool_definitions_for_responses_api(state, 0);

    assert(msg_defs && resp_defs);

    assert_same_sets(msg_defs, resp_defs);

    // Memory tools must always be present
    int msg_count = 0;
    const char **msg_names = collect_tool_names(msg_defs, &msg_count);
    assert(msg_names != NULL);
    assert(contains(msg_names, msg_count, "MemoryStore"));
    assert(contains(msg_names, msg_count, "MemoryRecall"));
    assert(contains(msg_names, msg_count, "MemorySearch"));
    free(msg_names);

    cJSON_Delete(msg_defs);
    cJSON_Delete(resp_defs);

    printf("OK\n");
}

static void run_subscription_case(const char *case_name, ConversationState *state) {
    printf("Running subscription case: %s... ", case_name);

    cJSON *resp_defs = get_tool_definitions_for_responses_api(state, 0);
    assert(resp_defs);

    int count = 0;
    const char **names = collect_tool_names(resp_defs, &count);
    assert(names != NULL);

    if (state->plan_mode) {
        assert(!contains(names, count, "Bash"));
        assert(!contains(names, count, "Write"));
        assert(!contains(names, count, "Edit"));
        assert(!contains(names, count, "MultiEdit"));
        assert(!contains(names, count, "Subagent"));
    } else {
        assert(contains(names, count, "Bash"));
        assert(contains(names, count, "Write"));
        assert(contains(names, count, "Edit"));
        assert(contains(names, count, "MultiEdit"));
    }

    free(names);
    cJSON_Delete(resp_defs);
    printf("OK\n");
}

int main(void) {
    // Default case (plan_mode off, not subagent)
    ConversationState state_default = {0};
    run_parity_case("default", &state_default);
    run_subscription_case("default", &state_default);

    // Plan mode: excludes write/exec tools
    ConversationState state_plan = {0};
    state_plan.plan_mode = 1;
    run_parity_case("plan_mode", &state_plan);
    run_subscription_case("plan_mode", &state_plan);

    // Subagent mode: excludes subagent recursion tools via env var
    setenv("KLAWED_IS_SUBAGENT", "1", 1);
    ConversationState state_sub = {0};
    run_parity_case("subagent", &state_sub);
    unsetenv("KLAWED_IS_SUBAGENT");

    printf("\nAll tool definition parity tests passed.\n");
    return 0;
}
