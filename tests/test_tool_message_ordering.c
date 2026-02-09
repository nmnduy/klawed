/*
 * test_tool_message_ordering.c - Test for tool result message ordering fix
 *
 * Tests that tool results are inserted immediately after the assistant message
 * that made the tool call, not at the end of the message array.
 *
 * This is a standalone test that doesn't link against klawed.c.
 */

#define _POSIX_C_SOURCE 200809L
#define TEST_BUILD 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <cjson/cJSON.h>
#include <bsd/stdlib.h>

// ============================================================================
// Inline copies of necessary types and functions for standalone testing
// ============================================================================

#define MAX_MESSAGES 10000

typedef enum {
    MSG_USER,
    MSG_ASSISTANT,
    MSG_SYSTEM
} MessageRole;

typedef enum {
    INTERNAL_TEXT,
    INTERNAL_TOOL_CALL,
    INTERNAL_TOOL_RESPONSE,
    INTERNAL_IMAGE
} InternalContentType;

typedef struct {
    InternalContentType type;
    char *text;
    char *tool_id;
    char *tool_name;
    cJSON *tool_params;
    cJSON *tool_output;
    int is_error;
    char *image_path;
    char *mime_type;
    char *base64_data;
    size_t image_size;
} InternalContent;

typedef struct {
    MessageRole role;
    InternalContent *contents;
    int content_count;
} InternalMessage;

typedef struct {
    InternalMessage messages[MAX_MESSAGES];
    int count;
    pthread_mutex_t lock;
    const char *model;
    int max_tokens;
} TestState;

// Minimal logger stubs - suppress unused warnings
__attribute__((unused))
static void LOG_DEBUG(const char *fmt, ...) { (void)fmt; }
__attribute__((unused))
static void LOG_INFO(const char *fmt, ...) { (void)fmt; }
__attribute__((unused))
static void LOG_WARN(const char *fmt, ...) { (void)fmt; }
__attribute__((unused))
static void LOG_ERROR(const char *fmt, ...) { (void)fmt; }

// ============================================================================
// Insert message helper (copy of the implementation from openai_messages.c)
// ============================================================================

static int insert_message_at(TestState *state, int insert_pos, InternalMessage *msg) {
    if (state->count >= MAX_MESSAGES) {
        LOG_ERROR("insert_message_at: Cannot insert - maximum message count reached");
        return -1;
    }

    if (insert_pos < 0 || insert_pos > state->count) {
        LOG_ERROR("insert_message_at: Invalid position %d (count=%d)", insert_pos, state->count);
        return -1;
    }

    // Shift messages from insert_pos to end using memmove to avoid pointer aliasing
    // This properly moves the InternalMessage structs without duplicating the contents pointers
    if (insert_pos < state->count) {
        memmove(&state->messages[insert_pos + 1],
                &state->messages[insert_pos],
                (size_t)(state->count - insert_pos) * sizeof(InternalMessage));
    }

    // Insert the new message (move, not copy - msg's contents pointer is transferred)
    state->messages[insert_pos] = *msg;
    state->count++;

    LOG_DEBUG("insert_message_at: Inserted message at position %d, new count=%d", insert_pos, state->count);
    return 0;
}

// ============================================================================
// Simplified ensure_tool_results (copy of the fixed implementation)
// ============================================================================

static void ensure_tool_results_test(TestState *state) {
    typedef struct {
        char *id;
        char *tool_name;
        int source_msg_idx;
        int has_result;
    } ToolCallInfo;

    ToolCallInfo *tool_calls = NULL;
    int tool_call_count = 0;
    int tool_call_capacity = 0;

    // Scan messages to collect tool calls and check for results
    for (int i = 0; i < state->count; i++) {
        InternalMessage *msg = &state->messages[i];

        if (msg->role == MSG_ASSISTANT) {
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];
                if (c->type == INTERNAL_TOOL_CALL && c->tool_id) {
                    if (tool_call_count >= tool_call_capacity) {
                        tool_call_capacity = tool_call_capacity == 0 ? 8 : tool_call_capacity * 2;
                        ToolCallInfo *new_calls = reallocarray(tool_calls, (size_t)tool_call_capacity, sizeof(ToolCallInfo));
                        if (!new_calls) {
                            free(tool_calls);
                            return;
                        }
                        tool_calls = new_calls;
                    }
                    tool_calls[tool_call_count].id = c->tool_id;
                    tool_calls[tool_call_count].tool_name = c->tool_name;
                    tool_calls[tool_call_count].source_msg_idx = i;
                    tool_calls[tool_call_count].has_result = 0;
                    tool_call_count++;
                }
            }
        } else if (msg->role == MSG_USER) {
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];
                if (c->type == INTERNAL_TOOL_RESPONSE && c->tool_id) {
                    for (int k = 0; k < tool_call_count; k++) {
                        if (tool_calls[k].id && strcmp(tool_calls[k].id, c->tool_id) == 0) {
                            tool_calls[k].has_result = 1;
                            break;
                        }
                    }
                }
            }
        }
    }

    // Find missing results and count per assistant message
    int missing_count = 0;
    for (int i = 0; i < tool_call_count; i++) {
        if (!tool_calls[i].has_result) {
            missing_count++;
        }
    }

    if (missing_count > 0) {
        // Group missing tool calls by their source assistant message index
        int *assistant_indices = NULL;
        int assistant_count = 0;
        int assistant_capacity = 0;

        for (int i = 0; i < tool_call_count; i++) {
            if (!tool_calls[i].has_result) {
                int idx = tool_calls[i].source_msg_idx;
                int found = 0;
                for (int j = 0; j < assistant_count; j++) {
                    if (assistant_indices[j] == idx) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    if (assistant_count >= assistant_capacity) {
                        assistant_capacity = assistant_capacity == 0 ? 4 : assistant_capacity * 2;
                        int *new_indices = reallocarray(assistant_indices, (size_t)assistant_capacity, sizeof(int));
                        if (!new_indices) {
                            free(assistant_indices);
                            free(tool_calls);
                            return;
                        }
                        assistant_indices = new_indices;
                    }
                    assistant_indices[assistant_count++] = idx;
                }
            }
        }

        // Sort assistant indices in descending order
        for (int i = 0; i < assistant_count - 1; i++) {
            for (int j = i + 1; j < assistant_count; j++) {
                if (assistant_indices[j] > assistant_indices[i]) {
                    int tmp = assistant_indices[i];
                    assistant_indices[i] = assistant_indices[j];
                    assistant_indices[j] = tmp;
                }
            }
        }

        // For each assistant message with missing results, insert synthetic results after it
        for (int a = 0; a < assistant_count; a++) {
            int asst_idx = assistant_indices[a];

            int missing_for_this = 0;
            for (int i = 0; i < tool_call_count; i++) {
                if (!tool_calls[i].has_result && tool_calls[i].source_msg_idx == asst_idx) {
                    missing_for_this++;
                }
            }

            if (missing_for_this == 0) continue;

            if (state->count >= MAX_MESSAGES) {
                free(assistant_indices);
                free(tool_calls);
                return;
            }

            InternalContent *synthetic_results = calloc((size_t)missing_for_this, sizeof(InternalContent));
            if (!synthetic_results) {
                free(assistant_indices);
                free(tool_calls);
                return;
            }

            int result_idx = 0;
            for (int i = 0; i < tool_call_count; i++) {
                if (!tool_calls[i].has_result && tool_calls[i].source_msg_idx == asst_idx) {
                    synthetic_results[result_idx].type = INTERNAL_TOOL_RESPONSE;
                    synthetic_results[result_idx].tool_id = strdup(tool_calls[i].id);
                    synthetic_results[result_idx].tool_name = strdup(tool_calls[i].tool_name ? tool_calls[i].tool_name : "unknown");
                    synthetic_results[result_idx].is_error = 1;
                    cJSON *error_output = cJSON_CreateObject();
                    cJSON_AddStringToObject(error_output, "error", "Tool execution was interrupted");
                    synthetic_results[result_idx].tool_output = error_output;
                    result_idx++;
                }
            }

            InternalMessage new_msg = {0};
            new_msg.role = MSG_USER;
            new_msg.contents = synthetic_results;
            new_msg.content_count = missing_for_this;

            int insert_pos = asst_idx + 1;
            if (insert_message_at(state, insert_pos, &new_msg) != 0) {
                for (int i = 0; i < missing_for_this; i++) {
                    free(synthetic_results[i].tool_id);
                    free(synthetic_results[i].tool_name);
                    if (synthetic_results[i].tool_output) {
                        cJSON_Delete(synthetic_results[i].tool_output);
                    }
                }
                free(synthetic_results);
                free(assistant_indices);
                free(tool_calls);
                return;
            }
        }

        free(assistant_indices);
    }

    free(tool_calls);
}

// ============================================================================
// Simplified add_tool_results (copy of the fixed implementation)
// ============================================================================

static int add_tool_results_test(TestState *state, InternalContent *results, int count) {
    if (state->count >= MAX_MESSAGES) {
        return -1;
    }

    // Find the assistant message that contains the tool calls we're responding to.
    int insert_pos = state->count;  // Default: append at end
    int found_assistant_idx = -1;

    const char *first_tool_id = NULL;
    for (int i = 0; i < count; i++) {
        if (results[i].tool_id) {
            first_tool_id = results[i].tool_id;
            break;
        }
    }

    if (first_tool_id) {
        // Search backwards for assistant message with this tool call
        for (int i = state->count - 1; i >= 0; i--) {
            InternalMessage *msg = &state->messages[i];
            if (msg->role == MSG_ASSISTANT) {
                for (int j = 0; j < msg->content_count; j++) {
                    InternalContent *c = &msg->contents[j];
                    if (c->type == INTERNAL_TOOL_CALL && c->tool_id &&
                        strcmp(c->tool_id, first_tool_id) == 0) {
                        found_assistant_idx = i;
                        insert_pos = i + 1;
                        break;
                    }
                }
                if (found_assistant_idx >= 0) break;
            }
        }
    }

    // If insert position is at the end, just append
    if (insert_pos == state->count) {
        InternalMessage *msg = &state->messages[state->count++];
        msg->role = MSG_USER;
        msg->contents = results;
        msg->content_count = count;
    } else {
        // Shift all messages from insert_pos onwards
        for (int i = state->count; i > insert_pos; i--) {
            state->messages[i] = state->messages[i - 1];
        }

        InternalMessage *msg = &state->messages[insert_pos];
        msg->role = MSG_USER;
        msg->contents = results;
        msg->content_count = count;
        state->count++;
    }

    return 0;
}

// ============================================================================
// Test helpers
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        printf("Running test: %s...", name); \
        tests_run++; \
    } while(0)

#define PASS() \
    do { \
        printf(" PASS\n"); \
        tests_passed++; \
    } while(0)

#define FAIL(msg) \
    do { \
        printf(" FAIL: %s\n", msg); \
        return; \
    } while(0)

static TestState* create_test_state(void) {
    TestState *state = calloc(1, sizeof(TestState));
    if (!state) return NULL;
    pthread_mutex_init(&state->lock, NULL);
    state->count = 0;
    state->model = "test-model";
    state->max_tokens = 1000;
    return state;
}

static void free_test_state(TestState *state) {
    if (!state) return;

    for (int i = 0; i < state->count; i++) {
        InternalMessage *msg = &state->messages[i];
        for (int j = 0; j < msg->content_count; j++) {
            InternalContent *c = &msg->contents[j];
            free(c->text);
            free(c->tool_id);
            free(c->tool_name);
            if (c->tool_params) cJSON_Delete(c->tool_params);
            if (c->tool_output) cJSON_Delete(c->tool_output);
        }
        free(msg->contents);
    }

    pthread_mutex_destroy(&state->lock);
    free(state);
}

static void add_user_text(TestState *state, const char *text) {
    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_USER;
    msg->contents = calloc(1, sizeof(InternalContent));
    msg->content_count = 1;
    msg->contents[0].type = INTERNAL_TEXT;
    msg->contents[0].text = strdup(text);
}

static void add_assistant_with_tool_call(TestState *state, const char *tool_id, const char *tool_name) {
    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_ASSISTANT;
    msg->contents = calloc(1, sizeof(InternalContent));
    msg->content_count = 1;
    msg->contents[0].type = INTERNAL_TOOL_CALL;
    msg->contents[0].tool_id = strdup(tool_id);
    msg->contents[0].tool_name = strdup(tool_name);
    msg->contents[0].tool_params = cJSON_CreateObject();
}

// ============================================================================
// Tests
// ============================================================================

static void test_tool_results_inserted_after_assistant(void) {
    TEST("tool_results_inserted_after_assistant");

    TestState *state = create_test_state();
    if (!state) FAIL("Failed to create test state");

    // Step 1: Add assistant message with tool call
    add_assistant_with_tool_call(state, "call_123", "Bash");

    // Step 2: Simulate user sending message while tool executes
    add_user_text(state, "Cancel that");

    // Step 3: Tool result comes in via add_tool_results
    InternalContent *results = calloc(1, sizeof(InternalContent));
    results[0].type = INTERNAL_TOOL_RESPONSE;
    results[0].tool_id = strdup("call_123");
    results[0].tool_name = strdup("Bash");
    results[0].tool_output = cJSON_CreateObject();
    cJSON_AddStringToObject(results[0].tool_output, "output", "command output");
    results[0].is_error = 0;

    int ret = add_tool_results_test(state, results, 1);
    if (ret != 0) {
        free_test_state(state);
        FAIL("add_tool_results failed");
    }

    // Verify: [Assistant(tool_call), User(tool_result), User("Cancel that")]
    if (state->count != 3) {
        free_test_state(state);
        FAIL("Expected 3 messages");
    }

    if (state->messages[0].role != MSG_ASSISTANT) {
        free_test_state(state);
        FAIL("Message 0 should be assistant");
    }

    if (state->messages[1].role != MSG_USER ||
        state->messages[1].contents[0].type != INTERNAL_TOOL_RESPONSE) {
        free_test_state(state);
        FAIL("Message 1 should be tool result");
    }

    if (state->messages[2].role != MSG_USER ||
        state->messages[2].contents[0].type != INTERNAL_TEXT ||
        strcmp(state->messages[2].contents[0].text, "Cancel that") != 0) {
        free_test_state(state);
        FAIL("Message 2 should be user text 'Cancel that'");
    }

    free_test_state(state);
    PASS();
}

static void test_ensure_tool_results_inserts_at_correct_position(void) {
    TEST("ensure_tool_results_inserts_at_correct_position");

    TestState *state = create_test_state();
    if (!state) FAIL("Failed to create test state");

    add_assistant_with_tool_call(state, "call_1", "Read");
    add_user_text(state, "User message 1");
    add_assistant_with_tool_call(state, "call_2", "Bash");

    ensure_tool_results_test(state);

    // Expected: [Asst(call_1), Synthetic(call_1), User("User message 1"), Asst(call_2), Synthetic(call_2)]
    if (state->count != 5) {
        printf("\nExpected 5 messages, got %d\n", state->count);
        free_test_state(state);
        FAIL("Expected 5 messages after inserting synthetic results");
    }

    if (state->messages[0].role != MSG_ASSISTANT) {
        free_test_state(state);
        FAIL("Message 0 should be assistant");
    }

    if (state->messages[1].role != MSG_USER ||
        state->messages[1].contents[0].type != INTERNAL_TOOL_RESPONSE ||
        strcmp(state->messages[1].contents[0].tool_id, "call_1") != 0) {
        free_test_state(state);
        FAIL("Message 1 should be synthetic result for call_1");
    }

    if (state->messages[2].role != MSG_USER ||
        state->messages[2].contents[0].type != INTERNAL_TEXT) {
        free_test_state(state);
        FAIL("Message 2 should be user text");
    }

    if (state->messages[3].role != MSG_ASSISTANT) {
        free_test_state(state);
        FAIL("Message 3 should be assistant");
    }

    if (state->messages[4].role != MSG_USER ||
        state->messages[4].contents[0].type != INTERNAL_TOOL_RESPONSE ||
        strcmp(state->messages[4].contents[0].tool_id, "call_2") != 0) {
        free_test_state(state);
        FAIL("Message 4 should be synthetic result for call_2");
    }

    free_test_state(state);
    PASS();
}

static void test_correct_order_unchanged(void) {
    TEST("correct_order_unchanged");

    TestState *state = create_test_state();
    if (!state) FAIL("Failed to create test state");

    add_assistant_with_tool_call(state, "call_1", "Read");

    // Add tool result manually in correct position
    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_USER;
    msg->contents = calloc(1, sizeof(InternalContent));
    msg->content_count = 1;
    msg->contents[0].type = INTERNAL_TOOL_RESPONSE;
    msg->contents[0].tool_id = strdup("call_1");
    msg->contents[0].tool_name = strdup("Read");
    msg->contents[0].tool_output = cJSON_CreateObject();
    msg->contents[0].is_error = 0;

    add_user_text(state, "User message");

    int original_count = state->count;

    ensure_tool_results_test(state);

    if (state->count != original_count) {
        free_test_state(state);
        FAIL("Message count changed when it shouldn't");
    }

    free_test_state(state);
    PASS();
}

static void test_multiple_tool_calls_same_message(void) {
    TEST("multiple_tool_calls_same_message");

    TestState *state = create_test_state();
    if (!state) FAIL("Failed to create test state");

    // Add assistant message with multiple tool calls
    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_ASSISTANT;
    msg->contents = calloc(2, sizeof(InternalContent));
    msg->content_count = 2;
    msg->contents[0].type = INTERNAL_TOOL_CALL;
    msg->contents[0].tool_id = strdup("call_a");
    msg->contents[0].tool_name = strdup("Read");
    msg->contents[0].tool_params = cJSON_CreateObject();
    msg->contents[1].type = INTERNAL_TOOL_CALL;
    msg->contents[1].tool_id = strdup("call_b");
    msg->contents[1].tool_name = strdup("Bash");
    msg->contents[1].tool_params = cJSON_CreateObject();

    add_user_text(state, "User message");

    ensure_tool_results_test(state);

    // Expected: [Asst(call_a, call_b), Synthetic(call_a, call_b), User("User message")]
    if (state->count != 3) {
        printf("\nExpected 3 messages, got %d\n", state->count);
        free_test_state(state);
        FAIL("Expected 3 messages");
    }

    if (state->messages[1].role != MSG_USER) {
        free_test_state(state);
        FAIL("Message 1 should be user (tool results)");
    }
    if (state->messages[1].content_count != 2) {
        printf("\nExpected 2 tool results, got %d\n", state->messages[1].content_count);
        free_test_state(state);
        FAIL("Message 1 should contain 2 tool results");
    }

    if (state->messages[2].role != MSG_USER ||
        state->messages[2].contents[0].type != INTERNAL_TEXT) {
        free_test_state(state);
        FAIL("Message 2 should be user text");
    }

    free_test_state(state);
    PASS();
}

int main(void) {
    printf("=== Tool Message Ordering Tests ===\n\n");
    printf("Testing fix for tool result message ordering bug\n");
    printf("(Tool results should be inserted after assistant, not at end)\n\n");

    test_tool_results_inserted_after_assistant();
    test_ensure_tool_results_inserts_at_correct_position();
    test_correct_order_unchanged();
    test_multiple_tool_calls_same_message();

    printf("\n=== Test Summary ===\n");
    printf("Total tests: %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_run - tests_passed);

    if (tests_passed == tests_run) {
        printf("\n✓ All tests passed!\n");
        return 0;
    } else {
        printf("\n✗ Some tests failed!\n");
        return 1;
    }
}
