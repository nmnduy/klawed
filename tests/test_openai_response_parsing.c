/*
 * test_openai_response_parsing.c - Unit tests for OpenAI response parsing
 *
 * Tests parse_openai_response() and free_internal_message() to ensure:
 * - Early returns properly initialize message struct (no garbage pointers)
 * - Valid responses are parsed correctly
 * - free_internal_message handles all edge cases
 *
 * Compilation: make test-openai-response-parsing
 * Usage: ./build/test_openai_response_parsing
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>
#include <bsd/stdlib.h>

// Include internal headers for testing
#include "../src/openai_messages.h"
#include "../src/logger.h"

// Test framework colors
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf(COLOR_GREEN "  ✓ " COLOR_RESET "%s\n", message); \
        } else { \
            tests_failed++; \
            printf(COLOR_RED "  ✗ " COLOR_RESET "%s\n", message); \
        } \
    } while (0)

#define TEST_SUMMARY() \
    do { \
        printf("\n" COLOR_CYAN "Test Summary:" COLOR_RESET "\n"); \
        printf("  Total:  %d\n", tests_run); \
        printf("  Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed); \
        printf("  Failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed); \
        if (tests_failed == 0) { \
            printf(COLOR_GREEN "\n✓ All tests passed!" COLOR_RESET "\n"); \
            return 0; \
        } else { \
            printf(COLOR_RED "\n✗ Some tests failed!" COLOR_RESET "\n"); \
            return 1; \
        } \
    } while (0)

// ============================================================================
// Test Fixtures (JSON strings for testing)
// ============================================================================

// Valid response with text content only
static const char *valid_text_response =
    "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Hello, world!\"}}]}";

// Valid response with tool calls
static const char *valid_tool_call_response =
    "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[{\"id\":\"call_123\",\"type\":\"function\",\"function\":{\"name\":\"Bash\",\"arguments\":\"{\\\"command\\\":\\\"ls -la\\\"}\"}}]}}]}";

// Valid response with multiple tool calls
static const char *valid_multiple_tool_calls_response =
    "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[{\"id\":\"tc1\",\"type\":\"function\",\"function\":{\"name\":\"Read\",\"arguments\":\"{}\"}},{\"id\":\"tc2\",\"type\":\"function\",\"function\":{\"name\":\"Grep\",\"arguments\":\"{\\\"pattern\\\":\\\"TODO\\\"}\"}}]}}]}";

// Valid response with text AND tool calls
static const char *valid_text_and_tool_calls_response =
    "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Let me check that for you.\",\"tool_calls\":[{\"id\":\"call_456\",\"type\":\"function\",\"function\":{\"name\":\"Bash\",\"arguments\":\"{\\\"command\\\":\\\"pwd\\\"}\"}}]}}]}";

// Missing choices array
static const char *missing_choices_response =
    "{\"message\":{\"role\":\"assistant\",\"content\":\"No choices here\"}}";

// Empty choices array
static const char *empty_choices_response =
    "{\"choices\":[]}";

// Missing message object in choice
static const char *missing_message_response =
    "{\"choices\":[{\"status\":\"completed\"}]}";

// Response with no content or tool_calls
static const char *no_content_response =
    "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":null}}]}";

// Invalid JSON
static const char *invalid_json = "{this is not valid json";

// ============================================================================
// Test Cases - parse_openai_response() edge cases
// ============================================================================

static void test_null_response(void) {
    printf(COLOR_YELLOW "\nTest: NULL response handling\n" COLOR_RESET);

    InternalMessage msg = parse_openai_response(NULL);

    TEST_ASSERT(msg.contents == NULL, "contents should be NULL for NULL response");
    TEST_ASSERT(msg.content_count == 0, "content_count should be 0 for NULL response");

    // free_internal_message should not crash on this
    free_internal_message(&msg);
    TEST_ASSERT(true, "free_internal_message handles NULL response");
}

static void test_missing_choices_array(void) {
    printf(COLOR_YELLOW "\nTest: Missing choices array\n" COLOR_RESET);

    cJSON *json = cJSON_Parse(missing_choices_response);
    TEST_ASSERT(json != NULL, "Should parse JSON successfully");

    InternalMessage msg = parse_openai_response(json);

    TEST_ASSERT(msg.contents == NULL, "contents should be NULL when choices missing");
    TEST_ASSERT(msg.content_count == 0, "content_count should be 0 when choices missing");

    free_internal_message(&msg);
    cJSON_Delete(json);
    TEST_ASSERT(true, "free_internal_message handles missing choices");
}

static void test_empty_choices_array(void) {
    printf(COLOR_YELLOW "\nTest: Empty choices array\n" COLOR_RESET);

    cJSON *json = cJSON_Parse(empty_choices_response);
    TEST_ASSERT(json != NULL, "Should parse JSON successfully");

    InternalMessage msg = parse_openai_response(json);

    TEST_ASSERT(msg.contents == NULL, "contents should be NULL for empty choices");
    TEST_ASSERT(msg.content_count == 0, "content_count should be 0 for empty choices");

    free_internal_message(&msg);
    cJSON_Delete(json);
    TEST_ASSERT(true, "free_internal_message handles empty choices");
}

static void test_missing_message_object(void) {
    printf(COLOR_YELLOW "\nTest: Missing message object\n" COLOR_RESET);

    cJSON *json = cJSON_Parse(missing_message_response);
    TEST_ASSERT(json != NULL, "Should parse JSON successfully");

    InternalMessage msg = parse_openai_response(json);

    TEST_ASSERT(msg.contents == NULL, "contents should be NULL when message missing");
    TEST_ASSERT(msg.content_count == 0, "content_count should be 0 when message missing");

    free_internal_message(&msg);
    cJSON_Delete(json);
    TEST_ASSERT(true, "free_internal_message handles missing message");
}

static void test_no_content_or_tool_calls(void) {
    printf(COLOR_YELLOW "\nTest: No content or tool_calls\n" COLOR_RESET);

    cJSON *json = cJSON_Parse(no_content_response);
    TEST_ASSERT(json != NULL, "Should parse JSON successfully");

    InternalMessage msg = parse_openai_response(json);

    TEST_ASSERT(msg.contents == NULL, "contents should be NULL when no content");
    TEST_ASSERT(msg.content_count == 0, "content_count should be 0 when no content");

    free_internal_message(&msg);
    cJSON_Delete(json);
    TEST_ASSERT(true, "free_internal_message handles no content");
}

static void test_invalid_json(void) {
    printf(COLOR_YELLOW "\nTest: Invalid JSON\n" COLOR_RESET);

    cJSON *json = cJSON_Parse(invalid_json);
    TEST_ASSERT(json == NULL, "Should fail to parse invalid JSON");

    // parse_openai_response with NULL JSON should be handled gracefully
    InternalMessage msg = parse_openai_response(NULL);

    TEST_ASSERT(msg.contents == NULL, "contents should be NULL for NULL JSON");
    TEST_ASSERT(msg.content_count == 0, "content_count should be 0 for NULL JSON");

    free_internal_message(&msg);
    TEST_ASSERT(true, "free_internal_message handles invalid JSON");
}

// ============================================================================
// Test Cases - parse_openai_response() valid parsing
// ============================================================================

static void test_valid_text_response(void) {
    printf(COLOR_YELLOW "\nTest: Valid text-only response parsing\n" COLOR_RESET);

    cJSON *json = cJSON_Parse(valid_text_response);
    TEST_ASSERT(json != NULL, "Should parse JSON successfully");

    InternalMessage msg = parse_openai_response(json);

    TEST_ASSERT(msg.contents != NULL, "contents should not be NULL for valid response");
    TEST_ASSERT(msg.content_count == 1, "Should have 1 content block");
    TEST_ASSERT(msg.role == MSG_ASSISTANT, "Role should be ASSISTANT");
    TEST_ASSERT(msg.contents[0].type == INTERNAL_TEXT, "Content should be TEXT type");
    TEST_ASSERT(strstr(msg.contents[0].text, "Hello") != NULL, "Text should contain greeting");

    free_internal_message(&msg);
    cJSON_Delete(json);
    TEST_ASSERT(true, "free_internal_message cleans up text response");
}

static void test_valid_tool_call_response(void) {
    printf(COLOR_YELLOW "\nTest: Valid tool call response parsing\n" COLOR_RESET);

    cJSON *json = cJSON_Parse(valid_tool_call_response);
    TEST_ASSERT(json != NULL, "Should parse JSON successfully");

    InternalMessage msg = parse_openai_response(json);

    TEST_ASSERT(msg.contents != NULL, "contents should not be NULL");
    TEST_ASSERT(msg.content_count == 1, "Should have 1 content block");
    TEST_ASSERT(msg.contents[0].type == INTERNAL_TOOL_CALL, "Content should be TOOL_CALL type");
    TEST_ASSERT(strcmp(msg.contents[0].tool_id, "call_123") == 0, "Tool ID should match");
    TEST_ASSERT(strcmp(msg.contents[0].tool_name, "Bash") == 0, "Tool name should match");
    TEST_ASSERT(msg.contents[0].tool_params != NULL, "Tool params should not be NULL");

    // Print tool params to verify content
    char *params_str = cJSON_PrintUnformatted(msg.contents[0].tool_params);
    TEST_ASSERT(params_str != NULL, "Should be able to print tool params");
    TEST_ASSERT(strstr(params_str, "ls -la") != NULL, "Tool params should contain command");

    free(params_str);
    free_internal_message(&msg);
    cJSON_Delete(json);
    TEST_ASSERT(true, "free_internal_message cleans up tool call response");
}

static void test_valid_multiple_tool_calls(void) {
    printf(COLOR_YELLOW "\nTest: Valid multiple tool calls response parsing\n" COLOR_RESET);

    cJSON *json = cJSON_Parse(valid_multiple_tool_calls_response);
    TEST_ASSERT(json != NULL, "Should parse JSON successfully");

    InternalMessage msg = parse_openai_response(json);

    TEST_ASSERT(msg.contents != NULL, "contents should not be NULL");
    TEST_ASSERT(msg.content_count == 2, "Should have 2 content blocks");
    TEST_ASSERT(msg.contents[0].type == INTERNAL_TOOL_CALL, "First should be TOOL_CALL");
    TEST_ASSERT(msg.contents[1].type == INTERNAL_TOOL_CALL, "Second should be TOOL_CALL");
    TEST_ASSERT(strcmp(msg.contents[0].tool_id, "tc1") == 0, "First tool ID should be tc1");
    TEST_ASSERT(strcmp(msg.contents[1].tool_id, "tc2") == 0, "Second tool ID should be tc2");
    TEST_ASSERT(strcmp(msg.contents[0].tool_name, "Read") == 0, "First tool name should be Read");
    TEST_ASSERT(strcmp(msg.contents[1].tool_name, "Grep") == 0, "Second tool name should be Grep");

    free_internal_message(&msg);
    cJSON_Delete(json);
    TEST_ASSERT(true, "free_internal_message cleans up multiple tool calls");
}

static void test_valid_text_and_tool_calls(void) {
    printf(COLOR_YELLOW "\nTest: Valid text AND tool calls response parsing\n" COLOR_RESET);

    cJSON *json = cJSON_Parse(valid_text_and_tool_calls_response);
    TEST_ASSERT(json != NULL, "Should parse JSON successfully");

    InternalMessage msg = parse_openai_response(json);

    TEST_ASSERT(msg.contents != NULL, "contents should not be NULL");
    TEST_ASSERT(msg.content_count == 2, "Should have 2 content blocks (text + tool)");

    // Find text content
    int found_text = 0, found_tool = 0;
    for (int i = 0; i < msg.content_count; i++) {
        if (msg.contents[i].type == INTERNAL_TEXT &&
            strstr(msg.contents[i].text, "check") != NULL) {
            found_text = 1;
        }
        if (msg.contents[i].type == INTERNAL_TOOL_CALL &&
            strcmp(msg.contents[i].tool_id, "call_456") == 0) {
            found_tool = 1;
        }
    }
    TEST_ASSERT(found_text, "Should have text content about checking");
    TEST_ASSERT(found_tool, "Should have tool call with id call_456");

    free_internal_message(&msg);
    cJSON_Delete(json);
    TEST_ASSERT(true, "free_internal_message cleans up mixed response");
}

// ============================================================================
// Test Cases - free_internal_message() edge cases
// ============================================================================

static void test_free_null_message(void) {
    printf(COLOR_YELLOW "\nTest: free_internal_message with NULL pointer\n" COLOR_RESET);

    // Should not crash
    free_internal_message(NULL);
    TEST_ASSERT(true, "free_internal_message handles NULL pointer");
}

static void test_free_empty_message(void) {
    printf(COLOR_YELLOW "\nTest: free_internal_message with empty message\n" COLOR_RESET);

    InternalMessage msg = {0};
    msg.contents = NULL;
    msg.content_count = 0;

    // Should not crash
    free_internal_message(&msg);
    TEST_ASSERT(true, "free_internal_message handles empty message");
}

static void test_free_already_freed(void) {
    printf(COLOR_YELLOW "\nTest: free_internal_message with already freed pointers\n" COLOR_RESET);

    // Create a message with allocated contents
    cJSON *json = cJSON_Parse(valid_text_response);
    InternalMessage msg = parse_openai_response(json);

    // Free it
    free_internal_message(&msg);
    TEST_ASSERT(msg.contents == NULL, "contents should be NULL after free");
    TEST_ASSERT(msg.content_count == 0, "content_count should be 0 after free");

    // Free again - should not crash
    free_internal_message(&msg);
    TEST_ASSERT(true, "free_internal_message handles double-free");

    cJSON_Delete(json);
}

// ============================================================================
// Regression Test - Session loading scenario
// ============================================================================

static void test_session_loading_scenario(void) {
    printf(COLOR_YELLOW "\nTest: Simulated session loading scenario\n" COLOR_RESET);

    // Simulate what session_load_from_db does: parse multiple responses
    const char *responses[] = {
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Step 1\"}}]}",
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Step 2\"}}]}",
        NULL  // Simulates what might happen with malformed data
    };

    int messages_freed = 0;

    for (int i = 0; responses[i] != NULL; i++) {
        cJSON *json = cJSON_Parse(responses[i]);
        if (!json) {
            // This simulates what happens when we get invalid data from DB
            InternalMessage empty_msg = {0};
            free_internal_message(&empty_msg);
            messages_freed++;
            continue;
        }

        InternalMessage msg = parse_openai_response(json);

        // Simulate adding to conversation (like session.c does)
        if (msg.content_count > 0) {
            // Would add to state->messages[state->count++] = msg;
            // Then set assistant_msg.content_count = 0 to prevent double-free
            msg.content_count = 0;
            msg.contents = NULL;
        }

        free_internal_message(&msg);
        cJSON_Delete(json);
        messages_freed++;
    }

    TEST_ASSERT(messages_freed == 3, "Should handle all 3 response scenarios");

    // Now test with malformed response at the end
    InternalMessage msg = {0};
    free_internal_message(&msg);
    TEST_ASSERT(true, "Can free empty message after valid messages");
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
    printf("=== OpenAI Response Parsing Tests ===\n");
    printf("Testing parse_openai_response() and free_internal_message()\n");
    printf("Regression tests for segfault on 'pointer being freed was not allocated'\n");

    // Edge case tests (these are the critical ones for the bug fix)
    test_null_response();
    test_missing_choices_array();
    test_empty_choices_array();
    test_missing_message_object();
    test_no_content_or_tool_calls();
    test_invalid_json();

    // Valid parsing tests
    test_valid_text_response();
    test_valid_tool_call_response();
    test_valid_multiple_tool_calls();
    test_valid_text_and_tool_calls();

    // free_internal_message edge cases
    test_free_null_message();
    test_free_empty_message();
    test_free_already_freed();

    // Regression test
    test_session_loading_scenario();

    TEST_SUMMARY();
}
