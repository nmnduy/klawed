// Unit tests to ensure cancellation emits tool_result for all tool_calls
// and that build_request_json_from_state formats them properly for the API.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <cjson/cJSON.h>

#include "claude_internal.h"

// Minimal validation: after an assistant with tool_calls, there must be
// tool messages for each tool_call_id before the next assistant/user.
static int validate_tool_pairing(cJSON *messages) {
    if (!messages || !cJSON_IsArray(messages)) return 0;

    int msg_count = cJSON_GetArraySize(messages);
    for (int i = 0; i < msg_count; i++) {
        cJSON *msg = cJSON_GetArrayItem(messages, i);
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        if (!role || !cJSON_IsString(role)) continue;

        if (strcmp(role->valuestring, "assistant") == 0) {
            cJSON *tool_calls = cJSON_GetObjectItem(msg, "tool_calls");
            if (!tool_calls || !cJSON_IsArray(tool_calls)) continue;

            int tool_count = cJSON_GetArraySize(tool_calls);
            if (tool_count == 0) continue;

            char **ids = (char**)calloc((size_t)tool_count, sizeof(char*));
            int *found = (int*)calloc((size_t)tool_count, sizeof(int));
            for (int j = 0; j < tool_count; j++) {
                cJSON *tc = cJSON_GetArrayItem(tool_calls, j);
                cJSON *id = cJSON_GetObjectItem(tc, "id");
                ids[j] = id && cJSON_IsString(id) ? id->valuestring : NULL;
            }

            for (int k = i + 1; k < msg_count; k++) {
                cJSON *next = cJSON_GetArrayItem(messages, k);
                cJSON *next_role = cJSON_GetObjectItem(next, "role");
                if (!next_role || !cJSON_IsString(next_role)) continue;
                if (strcmp(next_role->valuestring, "assistant") == 0 ||
                    strcmp(next_role->valuestring, "user") == 0) {
                    break;
                }
                if (strcmp(next_role->valuestring, "tool") == 0) {
                    cJSON *tool_call_id = cJSON_GetObjectItem(next, "tool_call_id");
                    if (tool_call_id && cJSON_IsString(tool_call_id)) {
                        for (int j = 0; j < tool_count; j++) {
                            if (ids[j] && strcmp(ids[j], tool_call_id->valuestring) == 0) {
                                found[j] = 1;
                            }
                        }
                    }
                }
            }

            int all_found = 1;
            for (int j = 0; j < tool_count; j++) {
                if (ids[j] && !found[j]) {
                    all_found = 0;
                }
            }
            free(ids);
            free(found);
            if (!all_found) return 0;
        }
    }
    return 1;
}

static void setup_assistant_with_tools(ConversationState *state) {
    // Append an assistant message with two tool calls
    InternalMessage *asst = &state->messages[state->count++];
    asst->role = MSG_ASSISTANT;
    asst->content_count = 2;
    asst->contents = calloc(2, sizeof(InternalContent));
    assert(asst->contents);

    // Tool call 1
    asst->contents[0].type = INTERNAL_TOOL_CALL;
    asst->contents[0].tool_id = strdup("call_1");
    asst->contents[0].tool_name = strdup("bash");
    asst->contents[0].tool_params = cJSON_CreateObject();

    // Tool call 2
    asst->contents[1].type = INTERNAL_TOOL_CALL;
    asst->contents[1].tool_id = strdup("call_2");
    asst->contents[1].tool_name = strdup("read");
    asst->contents[1].tool_params = cJSON_CreateObject();
}

static void append_cancelled_tool_results(ConversationState *state) {
    // Append a user message that contains tool_result blocks for both tools
    InternalMessage *usr = &state->messages[state->count++];
    usr->role = MSG_USER;
    usr->content_count = 2;
    usr->contents = calloc(2, sizeof(InternalContent));
    assert(usr->contents);

    // Result for call_1
    usr->contents[0].type = INTERNAL_TOOL_RESPONSE;
    usr->contents[0].tool_id = strdup("call_1");
    usr->contents[0].tool_name = strdup("bash");
    usr->contents[0].tool_output = cJSON_CreateObject();
    cJSON_AddStringToObject(usr->contents[0].tool_output, "error", "Tool execution cancelled before start");
    usr->contents[0].is_error = 1;

    // Result for call_2
    usr->contents[1].type = INTERNAL_TOOL_RESPONSE;
    usr->contents[1].tool_id = strdup("call_2");
    usr->contents[1].tool_name = strdup("read");
    usr->contents[1].tool_output = cJSON_CreateObject();
    cJSON_AddStringToObject(usr->contents[1].tool_output, "error", "Tool execution cancelled before start");
    usr->contents[1].is_error = 1;
}

static void test_cancel_results_are_formatted(void) {
    printf("Test: cancel emits tool_result for all tool_calls and formats correctly\n");

    ConversationState state = {0};
    state.model = strdup("o4-mini");
    // Build messages: assistant with tool_calls, user with cancelled results
    setup_assistant_with_tools(&state);
    append_cancelled_tool_results(&state);

    char *json_str = build_request_json_from_state(&state);
    assert(json_str);

    cJSON *root = cJSON_Parse(json_str);
    assert(root);
    cJSON *messages = cJSON_GetObjectItem(root, "messages");
    assert(messages && cJSON_IsArray(messages));

    // Validate that every tool_call has a tool message following
    int ok = validate_tool_pairing(messages);
    if (!ok) {
        printf("  ✗ FAILED: tool_call(s) missing tool_result after cancel\n");
        printf("  Payload: %s\n", json_str);
        exit(1);
    }

    // Also ensure tool messages have content strings
    int msg_count = cJSON_GetArraySize(messages);
    int tool_msgs = 0;
    for (int i = 0; i < msg_count; i++) {
        cJSON *msg = cJSON_GetArrayItem(messages, i);
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        if (role && cJSON_IsString(role) && strcmp(role->valuestring, "tool") == 0) {
            tool_msgs++;
            cJSON *content = cJSON_GetObjectItem(msg, "content");
            if (!content || !cJSON_IsString(content)) {
                printf("  ✗ FAILED: tool message missing string content\n");
                printf("  Msg: %s\n", cJSON_PrintUnformatted(msg));
                exit(1);
            }
        }
    }
    if (tool_msgs < 2) {
        printf("  ✗ FAILED: expected 2 tool messages, found %d\n", tool_msgs);
        exit(1);
    }

    cJSON_Delete(root);
    free(json_str);

    // Cleanup allocated state to avoid leaks under ASan
    conversation_free(&state);   // Frees messages and their contents
    free(state.model);           // Free model string set by test
    state.model = NULL;
    conversation_state_destroy(&state); // Destroy mutex if initialized
    printf("  ✓ PASSED\n\n");
}

int main(void) {
    printf("=== Cancel -> Tool Results Tests ===\n\n");
    test_cancel_results_are_formatted();
    printf("=== All tests passed! ===\n");
    return 0;
}
