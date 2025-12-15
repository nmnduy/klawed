/*
 * test_openai_format.c - Unit tests for OpenAI message format validation
 *
 * Tests ensure that we correctly format messages according to OpenAI's API spec:
 * - Tool calls must have corresponding tool responses
 * - Tool messages must have role="tool" and tool_call_id
 * - Message ordering must be correct
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <cjson/cJSON.h>

// Test result tracking
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

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Check if a message array has proper tool call/response pairing
 * Returns 1 if valid, 0 if invalid
 */
static int validate_tool_call_responses(cJSON *messages) {
    if (!messages || !cJSON_IsArray(messages)) {
        return 0;
    }

    int msg_count = cJSON_GetArraySize(messages);

    for (int i = 0; i < msg_count; i++) {
        cJSON *msg = cJSON_GetArrayItem(messages, i);
        cJSON *role = cJSON_GetObjectItem(msg, "role");

        if (!role || !cJSON_IsString(role)) {
            continue;
        }

        // If this is an assistant message with tool_calls
        if (strcmp(role->valuestring, "assistant") == 0) {
            cJSON *tool_calls = cJSON_GetObjectItem(msg, "tool_calls");

            if (tool_calls && cJSON_IsArray(tool_calls)) {
                int tool_count = cJSON_GetArraySize(tool_calls);

                // Collect all tool_call_ids
                char **tool_call_ids = malloc(sizeof(char*) * (size_t)tool_count);
                for (int j = 0; j < tool_count; j++) {
                    cJSON *tool_call = cJSON_GetArrayItem(tool_calls, j);
                    cJSON *id = cJSON_GetObjectItem(tool_call, "id");
                    if (id && cJSON_IsString(id)) {
                        tool_call_ids[j] = strdup(id->valuestring);
                    } else {
                        tool_call_ids[j] = NULL;
                    }
                }

                // Check that following messages contain tool responses for each id
                int *found = calloc((size_t)tool_count, sizeof(int));

                for (int k = i + 1; k < msg_count; k++) {
                    cJSON *next_msg = cJSON_GetArrayItem(messages, k);
                    cJSON *next_role = cJSON_GetObjectItem(next_msg, "role");

                    if (!next_role || !cJSON_IsString(next_role)) {
                        continue;
                    }

                    // Stop at next assistant or user message
                    if (strcmp(next_role->valuestring, "assistant") == 0 ||
                        strcmp(next_role->valuestring, "user") == 0) {
                        break;
                    }

                    // Check if this is a tool message
                    if (strcmp(next_role->valuestring, "tool") == 0) {
                        cJSON *tool_call_id = cJSON_GetObjectItem(next_msg, "tool_call_id");
                        if (tool_call_id && cJSON_IsString(tool_call_id)) {
                            // Mark this tool_call_id as found
                            for (int j = 0; j < tool_count; j++) {
                                if (tool_call_ids[j] &&
                                    strcmp(tool_call_ids[j], tool_call_id->valuestring) == 0) {
                                    found[j] = 1;
                                }
                            }
                        }
                    }
                }

                // Check that all tool_call_ids were found
                int all_found = 1;
                for (int j = 0; j < tool_count; j++) {
                    if (tool_call_ids[j] && !found[j]) {
                        fprintf(stderr, "Missing response for tool_call_id: %s\n", tool_call_ids[j]);
                        all_found = 0;
                    }
                }

                // Cleanup
                for (int j = 0; j < tool_count; j++) {
                    free(tool_call_ids[j]);
                }
                free(tool_call_ids);
                free(found);

                if (!all_found) {
                    return 0;
                }
            }
        }
    }

    return 1;
}

/**
 * Check that tool messages have required fields
 */
static int validate_tool_message_format(cJSON *msg) {
    cJSON *role = cJSON_GetObjectItem(msg, "role");
    cJSON *tool_call_id = cJSON_GetObjectItem(msg, "tool_call_id");
    cJSON *content = cJSON_GetObjectItem(msg, "content");

    if (!role || !cJSON_IsString(role)) {
        return 0;
    }

    if (strcmp(role->valuestring, "tool") != 0) {
        return 1; // Not a tool message, skip validation
    }

    // Tool messages must have tool_call_id and content
    if (!tool_call_id || !cJSON_IsString(tool_call_id)) {
        fprintf(stderr, "Tool message missing tool_call_id\n");
        return 0;
    }

    if (!content || !cJSON_IsString(content)) {
        fprintf(stderr, "Tool message missing content\n");
        return 0;
    }

    return 1;
}

// ============================================================================
// Test Cases
// ============================================================================

/**
 * Test: Assistant message with tool_calls followed by tool responses
 */
static void test_valid_tool_call_response_pairing(void) {
    TEST("valid_tool_call_response_pairing");

    const char *json =
        "["
        "  {\"role\": \"user\", \"content\": \"Hello\"},"
        "  {\"role\": \"assistant\", \"content\": null, \"tool_calls\": ["
        "    {\"id\": \"call_1\", \"type\": \"function\", \"function\": {\"name\": \"bash\", \"arguments\": \"{}\"}}"
        "  ]},"
        "  {\"role\": \"tool\", \"tool_call_id\": \"call_1\", \"content\": \"result\"}"
        "]";

    cJSON *messages = cJSON_Parse(json);
    if (!messages) {
        FAIL("Failed to parse JSON");
    }

    if (!validate_tool_call_responses(messages)) {
        cJSON_Delete(messages);
        FAIL("Valid format rejected");
    }

    cJSON_Delete(messages);
    PASS();
}

/**
 * Test: Multiple tool calls with all responses
 */
static void test_multiple_tool_calls_all_responded(void) {
    TEST("multiple_tool_calls_all_responded");

    const char *json =
        "["
        "  {\"role\": \"assistant\", \"content\": null, \"tool_calls\": ["
        "    {\"id\": \"call_1\", \"type\": \"function\", \"function\": {\"name\": \"bash\", \"arguments\": \"{}\"}},"
        "    {\"id\": \"call_2\", \"type\": \"function\", \"function\": {\"name\": \"read\", \"arguments\": \"{}\"}}"
        "  ]},"
        "  {\"role\": \"tool\", \"tool_call_id\": \"call_1\", \"content\": \"result1\"},"
        "  {\"role\": \"tool\", \"tool_call_id\": \"call_2\", \"content\": \"result2\"}"
        "]";

    cJSON *messages = cJSON_Parse(json);
    if (!messages) {
        FAIL("Failed to parse JSON");
    }

    if (!validate_tool_call_responses(messages)) {
        cJSON_Delete(messages);
        FAIL("Valid format rejected");
    }

    cJSON_Delete(messages);
    PASS();
}

/**
 * Test: Missing tool response should fail validation
 */
static void test_missing_tool_response(void) {
    TEST("missing_tool_response");

    const char *json =
        "["
        "  {\"role\": \"assistant\", \"content\": null, \"tool_calls\": ["
        "    {\"id\": \"call_1\", \"type\": \"function\", \"function\": {\"name\": \"bash\", \"arguments\": \"{}\"}},"
        "    {\"id\": \"call_2\", \"type\": \"function\", \"function\": {\"name\": \"read\", \"arguments\": \"{}\"}}"
        "  ]},"
        "  {\"role\": \"tool\", \"tool_call_id\": \"call_1\", \"content\": \"result1\"}"
        "]";

    cJSON *messages = cJSON_Parse(json);
    if (!messages) {
        FAIL("Failed to parse JSON");
    }

    // Should FAIL validation because call_2 has no response
    if (validate_tool_call_responses(messages)) {
        cJSON_Delete(messages);
        FAIL("Invalid format accepted (missing tool response)");
    }

    cJSON_Delete(messages);
    PASS();
}

/**
 * Test: Tool message must have tool_call_id
 */
static void test_tool_message_requires_tool_call_id(void) {
    TEST("tool_message_requires_tool_call_id");

    const char *json = "{\"role\": \"tool\", \"content\": \"result\"}";

    cJSON *msg = cJSON_Parse(json);
    if (!msg) {
        FAIL("Failed to parse JSON");
    }

    // Should fail because tool_call_id is missing
    if (validate_tool_message_format(msg)) {
        cJSON_Delete(msg);
        FAIL("Invalid tool message accepted");
    }

    cJSON_Delete(msg);
    PASS();
}

/**
 * Test: Tool message must have content
 */
static void test_tool_message_requires_content(void) {
    TEST("tool_message_requires_content");

    const char *json = "{\"role\": \"tool\", \"tool_call_id\": \"call_1\"}";

    cJSON *msg = cJSON_Parse(json);
    if (!msg) {
        FAIL("Failed to parse JSON");
    }

    // Should fail because content is missing
    if (validate_tool_message_format(msg)) {
        cJSON_Delete(msg);
        FAIL("Invalid tool message accepted");
    }

    cJSON_Delete(msg);
    PASS();
}

/**
 * Test: Valid tool message format
 */
static void test_valid_tool_message(void) {
    TEST("valid_tool_message");

    const char *json = "{\"role\": \"tool\", \"tool_call_id\": \"call_1\", \"content\": \"result\"}";

    cJSON *msg = cJSON_Parse(json);
    if (!msg) {
        FAIL("Failed to parse JSON");
    }

    if (!validate_tool_message_format(msg)) {
        cJSON_Delete(msg);
        FAIL("Valid tool message rejected");
    }

    cJSON_Delete(msg);
    PASS();
}

/**
 * Test: Assistant message with tool_calls can have null content
 */
static void test_tool_calls_allow_null_content(void) {
    TEST("tool_calls_allow_null_content");

    const char *json =
        "["
        "  {\"role\": \"assistant\", \"content\": null, \"tool_calls\": ["
        "    {\"id\": \"call_1\", \"type\": \"function\", \"function\": {\"name\": \"bash\", \"arguments\": \"{}\"}}"
        "  ]},"
        "  {\"role\": \"tool\", \"tool_call_id\": \"call_1\", \"content\": \"result\"}"
        "]";

    cJSON *messages = cJSON_Parse(json);
    if (!messages) {
        FAIL("Failed to parse JSON");
    }

    // Assistant with tool_calls and null content is valid
    cJSON *assistant_msg = cJSON_GetArrayItem(messages, 0);
    cJSON *content = cJSON_GetObjectItem(assistant_msg, "content");
    if (!content || !cJSON_IsNull(content)) {
        cJSON_Delete(messages);
        FAIL("content should be null");
    }

    if (!validate_tool_call_responses(messages)) {
        cJSON_Delete(messages);
        FAIL("Valid format rejected");
    }

    cJSON_Delete(messages);
    PASS();
}

/**
 * Test: Error response should still be valid tool message
 */
static void test_error_response_is_valid_tool_message(void) {
    TEST("error_response_is_valid_tool_message");

    const char *json =
        "["
        "  {\"role\": \"assistant\", \"content\": null, \"tool_calls\": ["
        "    {\"id\": \"call_1\", \"type\": \"function\", \"function\": {\"name\": \"unknown\", \"arguments\": \"{}\"}}"
        "  ]},"
        "  {\"role\": \"tool\", \"tool_call_id\": \"call_1\", \"content\": \"{\\\"error\\\": \\\"Tool call missing 'function' object\\\"}\"}"
        "]";

    cJSON *messages = cJSON_Parse(json);
    if (!messages) {
        FAIL("Failed to parse JSON");
    }

    // Error responses should still have proper format
    cJSON *tool_msg = cJSON_GetArrayItem(messages, 1);
    if (!validate_tool_message_format(tool_msg)) {
        cJSON_Delete(messages);
        FAIL("Error response has invalid format");
    }

    if (!validate_tool_call_responses(messages)) {
        cJSON_Delete(messages);
        FAIL("Valid format rejected");
    }

    cJSON_Delete(messages);
    PASS();
}

/**
 * Test: Conversation with multiple turns
 */
static void test_multi_turn_conversation(void) {
    TEST("multi_turn_conversation");

    const char *json =
        "["
        "  {\"role\": \"user\", \"content\": \"Run ls\"},"
        "  {\"role\": \"assistant\", \"content\": null, \"tool_calls\": ["
        "    {\"id\": \"call_1\", \"type\": \"function\", \"function\": {\"name\": \"bash\", \"arguments\": \"{}\"}}"
        "  ]},"
        "  {\"role\": \"tool\", \"tool_call_id\": \"call_1\", \"content\": \"file1.txt\"},"
        "  {\"role\": \"assistant\", \"content\": \"Found file1.txt\"},"
        "  {\"role\": \"user\", \"content\": \"Read it\"},"
        "  {\"role\": \"assistant\", \"content\": null, \"tool_calls\": ["
        "    {\"id\": \"call_2\", \"type\": \"function\", \"function\": {\"name\": \"read\", \"arguments\": \"{}\"}}"
        "  ]},"
        "  {\"role\": \"tool\", \"tool_call_id\": \"call_2\", \"content\": \"contents\"}"
        "]";

    cJSON *messages = cJSON_Parse(json);
    if (!messages) {
        FAIL("Failed to parse JSON");
    }

    if (!validate_tool_call_responses(messages)) {
        cJSON_Delete(messages);
        FAIL("Valid multi-turn conversation rejected");
    }

    cJSON_Delete(messages);
    PASS();
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
    printf("=== OpenAI Message Format Validation Tests ===\n\n");

    // Run all tests
    test_valid_tool_call_response_pairing();
    test_multiple_tool_calls_all_responded();
    test_missing_tool_response();
    test_tool_message_requires_tool_call_id();
    test_tool_message_requires_content();
    test_valid_tool_message();
    test_tool_calls_allow_null_content();
    test_error_response_is_valid_tool_message();
    test_multi_turn_conversation();

    // Print summary
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
