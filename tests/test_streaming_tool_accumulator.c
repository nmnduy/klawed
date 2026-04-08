/*
 * test_streaming_tool_accumulator.c - Unit tests for streaming tool call accumulation
 *
 * Tests the tool call accumulation logic to ensure:
 * - Tool calls are accumulated correctly from streaming chunks
 * - Empty name/id are handled properly
 * - Partial tool calls are filtered out
 * - Multiple tool calls work correctly
 *
 * Compilation: make test-streaming-tool-accumulator
 * Usage: ./build/test_streaming_tool_accumulator
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

#include "../src/streaming_tool_accumulator.h"

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

// Helper to create a tool_calls delta
static cJSON* create_tool_delta(int index, const char *id, const char *name, const char *args) {
    cJSON *tool_calls = cJSON_CreateArray();
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddNumberToObject(tool, "index", index);
    if (id) {
        cJSON_AddStringToObject(tool, "id", id);
    }
    cJSON *function = cJSON_CreateObject();
    if (name) {
        cJSON_AddStringToObject(function, "name", name);
    }
    if (args) {
        cJSON_AddStringToObject(function, "arguments", args);
    }
    cJSON_AddItemToObject(tool, "function", function);
    cJSON_AddItemToArray(tool_calls, tool);
    return tool_calls;
}

// Helper to create a partial tool delta (only some fields)
static cJSON* create_partial_tool_delta(int index, const char *id, const char *name, const char *args) {
    cJSON *tool_calls = cJSON_CreateArray();
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddNumberToObject(tool, "index", index);
    if (id) {
        cJSON_AddStringToObject(tool, "id", id);
    }
    if (name || args) {
        cJSON *function = cJSON_CreateObject();
        if (name) {
            cJSON_AddStringToObject(function, "name", name);
        }
        if (args) {
            cJSON_AddStringToObject(function, "arguments", args);
        }
        cJSON_AddItemToObject(tool, "function", function);
    }
    cJSON_AddItemToArray(tool_calls, tool);
    return tool_calls;
}

// ============================================================================
// Test Cases - Basic accumulation
// ============================================================================

static void test_single_complete_tool_call(void) {
    printf(COLOR_YELLOW "\nTest: Single complete tool call in one chunk\n" COLOR_RESET);

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    TEST_ASSERT(acc != NULL, "Accumulator should be created");

    // Simulate a complete tool call in one chunk
    cJSON *delta = create_tool_delta(0, "call_123", "Bash", "{\"command\":\"ls -la\"}");
    int result = tool_accumulator_process_delta(acc, delta);
    TEST_ASSERT(result == 0, "Processing should succeed");
    TEST_ASSERT(acc->tool_calls_count == 1, "Should have 1 tool call");

    // Check validity
    int valid = tool_accumulator_count_valid(acc);
    TEST_ASSERT(valid == 1, "Should have 1 valid tool call");

    // Get filtered results
    cJSON *filtered = tool_accumulator_filter_valid(acc);
    TEST_ASSERT(filtered != NULL, "Filtered array should exist");
    TEST_ASSERT(cJSON_GetArraySize(filtered) == 1, "Filtered array should have 1 item");

    // Verify the tool call content
    cJSON *tool = cJSON_GetArrayItem(filtered, 0);
    cJSON *id = cJSON_GetObjectItem(tool, "id");
    cJSON *func = cJSON_GetObjectItem(tool, "function");
    cJSON *name = cJSON_GetObjectItem(func, "name");

    TEST_ASSERT(strcmp(id->valuestring, "call_123") == 0, "ID should match");
    TEST_ASSERT(strcmp(name->valuestring, "Bash") == 0, "Name should match");

    cJSON_Delete(delta);
    cJSON_Delete(filtered);
    tool_accumulator_destroy(acc);
}

static void test_incremental_tool_call_accumulation(void) {
    printf(COLOR_YELLOW "\nTest: Incremental tool call accumulation\n" COLOR_RESET);

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    TEST_ASSERT(acc != NULL, "Accumulator should be created");

    // First chunk: only id and name
    cJSON *delta1 = create_partial_tool_delta(0, "call_123", "Bash", NULL);
    int result = tool_accumulator_process_delta(acc, delta1);
    TEST_ASSERT(result == 0, "First delta should succeed");
    TEST_ASSERT(acc->tool_calls_count == 1, "Should have 1 tool call slot");

    // At this point, id and name are set but arguments is empty
    int valid = tool_accumulator_count_valid(acc);
    TEST_ASSERT(valid == 1, "Should have 1 valid tool call (id and name present)");

    // Second chunk: only arguments
    cJSON *delta2 = create_partial_tool_delta(0, NULL, NULL, "{\"command\"");
    result = tool_accumulator_process_delta(acc, delta2);
    TEST_ASSERT(result == 0, "Second delta should succeed");

    // Third chunk: more arguments
    cJSON *delta3 = create_partial_tool_delta(0, NULL, NULL, ":\"ls -la\"}");
    result = tool_accumulator_process_delta(acc, delta3);
    TEST_ASSERT(result == 0, "Third delta should succeed");

    // Verify final result
    cJSON *filtered = tool_accumulator_filter_valid(acc);
    TEST_ASSERT(cJSON_GetArraySize(filtered) == 1, "Should have 1 valid tool call");

    cJSON *tool = cJSON_GetArrayItem(filtered, 0);
    cJSON *func = cJSON_GetObjectItem(tool, "function");
    cJSON *args = cJSON_GetObjectItem(func, "arguments");

    TEST_ASSERT(strcmp(args->valuestring, "{\"command\":\"ls -la\"}") == 0,
                "Arguments should be accumulated correctly");

    cJSON_Delete(delta1);
    cJSON_Delete(delta2);
    cJSON_Delete(delta3);
    cJSON_Delete(filtered);
    tool_accumulator_destroy(acc);
}

static void test_multiple_tool_calls(void) {
    printf(COLOR_YELLOW "\nTest: Multiple tool calls\n" COLOR_RESET);

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    TEST_ASSERT(acc != NULL, "Accumulator should be created");

    // First tool call
    cJSON *delta1 = create_tool_delta(0, "call_1", "Read", "{\"file_path\":\"/etc/passwd\"}");
    tool_accumulator_process_delta(acc, delta1);

    // Second tool call
    cJSON *delta2 = create_tool_delta(1, "call_2", "Bash", "{\"command\":\"whoami\"}");
    tool_accumulator_process_delta(acc, delta2);

    TEST_ASSERT(acc->tool_calls_count == 2, "Should have 2 tool calls");

    int valid = tool_accumulator_count_valid(acc);
    TEST_ASSERT(valid == 2, "Should have 2 valid tool calls");

    cJSON *filtered = tool_accumulator_filter_valid(acc);
    TEST_ASSERT(cJSON_GetArraySize(filtered) == 2, "Filtered should have 2 items");

    cJSON_Delete(delta1);
    cJSON_Delete(delta2);
    cJSON_Delete(filtered);
    tool_accumulator_destroy(acc);
}

// ============================================================================
// Test Cases - Empty name/id handling
// ============================================================================

static void test_empty_name_filtered(void) {
    printf(COLOR_YELLOW "\nTest: Tool call with empty name is filtered\n" COLOR_RESET);

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    TEST_ASSERT(acc != NULL, "Accumulator should be created");

    // Create a tool call with empty name (simulating the bug)
    cJSON *delta = create_tool_delta(0, "call_123", "", "{\"command\":\"ls\"}");
    tool_accumulator_process_delta(acc, delta);

    // Should have the slot but not be valid
    TEST_ASSERT(acc->tool_calls_count == 1, "Should have 1 tool call slot");

    int valid = tool_accumulator_count_valid(acc);
    TEST_ASSERT(valid == 0, "Should have 0 valid tool calls (empty name)");

    cJSON *filtered = tool_accumulator_filter_valid(acc);
    TEST_ASSERT(cJSON_GetArraySize(filtered) == 0, "Filtered should be empty");

    cJSON_Delete(delta);
    cJSON_Delete(filtered);
    tool_accumulator_destroy(acc);
}

static void test_empty_id_filtered(void) {
    printf(COLOR_YELLOW "\nTest: Tool call with empty id is filtered\n" COLOR_RESET);

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    TEST_ASSERT(acc != NULL, "Accumulator should be created");

    // Create a tool call with empty id
    cJSON *delta = create_tool_delta(0, "", "Bash", "{\"command\":\"ls\"}");
    tool_accumulator_process_delta(acc, delta);

    TEST_ASSERT(acc->tool_calls_count == 1, "Should have 1 tool call slot");

    int valid = tool_accumulator_count_valid(acc);
    TEST_ASSERT(valid == 0, "Should have 0 valid tool calls (empty id)");

    cJSON *filtered = tool_accumulator_filter_valid(acc);
    TEST_ASSERT(cJSON_GetArraySize(filtered) == 0, "Filtered should be empty");

    cJSON_Delete(delta);
    cJSON_Delete(filtered);
    tool_accumulator_destroy(acc);
}

static void test_both_empty_filtered(void) {
    printf(COLOR_YELLOW "\nTest: Tool call with both empty name and id is filtered\n" COLOR_RESET);

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    TEST_ASSERT(acc != NULL, "Accumulator should be created");

    // This simulates the exact bug we saw
    cJSON *delta = create_tool_delta(0, "", "", "{\"command\":\"ls\"}");
    tool_accumulator_process_delta(acc, delta);

    TEST_ASSERT(acc->tool_calls_count == 1, "Should have 1 tool call slot");

    int valid = tool_accumulator_count_valid(acc);
    TEST_ASSERT(valid == 0, "Should have 0 valid tool calls");

    cJSON *filtered = tool_accumulator_filter_valid(acc);
    TEST_ASSERT(cJSON_GetArraySize(filtered) == 0, "Filtered should be empty");

    cJSON_Delete(delta);
    cJSON_Delete(filtered);
    tool_accumulator_destroy(acc);
}

// ============================================================================
// Test Cases - Mixed valid and invalid
// ============================================================================

static void test_mixed_valid_and_invalid(void) {
    printf(COLOR_YELLOW "\nTest: Mix of valid and invalid tool calls\n" COLOR_RESET);

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    TEST_ASSERT(acc != NULL, "Accumulator should be created");

    // First tool: valid
    cJSON *delta1 = create_tool_delta(0, "call_1", "Read", "{\"file_path\":\"/tmp\"}");
    tool_accumulator_process_delta(acc, delta1);

    // Second tool: invalid (empty name)
    cJSON *delta2 = create_tool_delta(1, "call_2", "", "{\"command\":\"ls\"}");
    tool_accumulator_process_delta(acc, delta2);

    // Third tool: valid
    cJSON *delta3 = create_tool_delta(2, "call_3", "Bash", "{\"command\":\"pwd\"}");
    tool_accumulator_process_delta(acc, delta3);

    TEST_ASSERT(acc->tool_calls_count == 3, "Should have 3 tool call slots");

    int valid = tool_accumulator_count_valid(acc);
    TEST_ASSERT(valid == 2, "Should have 2 valid tool calls");

    cJSON *filtered = tool_accumulator_filter_valid(acc);
    TEST_ASSERT(cJSON_GetArraySize(filtered) == 2, "Filtered should have 2 items");

    // Verify the valid ones are kept
    cJSON *tool0 = cJSON_GetArrayItem(filtered, 0);
    cJSON *id0 = cJSON_GetObjectItem(tool0, "id");
    TEST_ASSERT(strcmp(id0->valuestring, "call_1") == 0, "First valid should be call_1");

    cJSON *tool1 = cJSON_GetArrayItem(filtered, 1);
    cJSON *id1 = cJSON_GetObjectItem(tool1, "id");
    TEST_ASSERT(strcmp(id1->valuestring, "call_3") == 0, "Second valid should be call_3");

    cJSON_Delete(delta1);
    cJSON_Delete(delta2);
    cJSON_Delete(delta3);
    cJSON_Delete(filtered);
    tool_accumulator_destroy(acc);
}

// ============================================================================
// Test Cases - Real-world streaming scenarios
// ============================================================================

static void test_openai_streaming_format_simulation(void) {
    printf(COLOR_YELLOW "\nTest: OpenAI streaming format simulation\n" COLOR_RESET);

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    TEST_ASSERT(acc != NULL, "Accumulator should be created");

    // Simulate how OpenAI streams tool calls:
    // First: index only (no id, name, or args yet)
    cJSON *delta1 = cJSON_CreateArray();
    cJSON *tool1 = cJSON_CreateObject();
    cJSON_AddNumberToObject(tool1, "index", 0);
    cJSON_AddItemToArray(delta1, tool1);
    tool_accumulator_process_delta(acc, delta1);

    // Second: id appears
    cJSON *delta2 = cJSON_CreateArray();
    cJSON *tool2 = cJSON_CreateObject();
    cJSON_AddNumberToObject(tool2, "index", 0);
    cJSON_AddStringToObject(tool2, "id", "call_abc123");
    cJSON_AddItemToArray(delta2, tool2);
    tool_accumulator_process_delta(acc, delta2);

    // Third: function name appears
    cJSON *delta3 = cJSON_CreateArray();
    cJSON *tool3 = cJSON_CreateObject();
    cJSON_AddNumberToObject(tool3, "index", 0);
    cJSON *func3 = cJSON_CreateObject();
    cJSON_AddStringToObject(func3, "name", "Bash");
    cJSON_AddItemToObject(tool3, "function", func3);
    cJSON_AddItemToArray(delta3, tool3);
    tool_accumulator_process_delta(acc, delta3);

    // Fourth: arguments start appearing
    cJSON *delta4 = cJSON_CreateArray();
    cJSON *tool4 = cJSON_CreateObject();
    cJSON_AddNumberToObject(tool4, "index", 0);
    cJSON *func4 = cJSON_CreateObject();
    cJSON_AddStringToObject(func4, "arguments", "{\"comm");
    cJSON_AddItemToObject(tool4, "function", func4);
    cJSON_AddItemToArray(delta4, tool4);
    tool_accumulator_process_delta(acc, delta4);

    // Fifth: more arguments
    cJSON *delta5 = cJSON_CreateArray();
    cJSON *tool5 = cJSON_CreateObject();
    cJSON_AddNumberToObject(tool5, "index", 0);
    cJSON *func5 = cJSON_CreateObject();
    cJSON_AddStringToObject(func5, "arguments", "and\":\"ls -la\"}");
    cJSON_AddItemToObject(tool5, "function", func5);
    cJSON_AddItemToArray(delta5, tool5);
    tool_accumulator_process_delta(acc, delta5);

    // Now check the final result
    TEST_ASSERT(acc->tool_calls_count == 1, "Should have 1 tool call");

    int valid = tool_accumulator_count_valid(acc);
    TEST_ASSERT(valid == 1, "Should have 1 valid tool call");

    cJSON *filtered = tool_accumulator_filter_valid(acc);
    cJSON *tool = cJSON_GetArrayItem(filtered, 0);
    cJSON *id = cJSON_GetObjectItem(tool, "id");
    cJSON *func = cJSON_GetObjectItem(tool, "function");
    cJSON *name = cJSON_GetObjectItem(func, "name");
    cJSON *args = cJSON_GetObjectItem(func, "arguments");

    TEST_ASSERT(strcmp(id->valuestring, "call_abc123") == 0, "ID should be accumulated");
    TEST_ASSERT(strcmp(name->valuestring, "Bash") == 0, "Name should be accumulated");
    TEST_ASSERT(strcmp(args->valuestring, "{\"command\":\"ls -la\"}") == 0,
                "Arguments should be concatenated");

    cJSON_Delete(delta1);
    cJSON_Delete(delta2);
    cJSON_Delete(delta3);
    cJSON_Delete(delta4);
    cJSON_Delete(delta5);
    cJSON_Delete(filtered);
    tool_accumulator_destroy(acc);
}

static void test_incomplete_json_arguments_filtered(void) {
    printf(COLOR_YELLOW "\nTest: Incomplete JSON arguments are filtered\n" COLOR_RESET);

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    TEST_ASSERT(acc != NULL, "Accumulator should be created");

    cJSON *delta = create_tool_delta(0, "call_bad", "Read", "{\"file_path\":\"README.md\"");
    tool_accumulator_process_delta(acc, delta);

    TEST_ASSERT(acc->tool_calls_count == 1, "Should keep the tool call slot");
    TEST_ASSERT(tool_accumulator_count_valid(acc) == 0,
                "Malformed JSON arguments should not count as valid");

    cJSON *filtered = tool_accumulator_filter_valid(acc);
    TEST_ASSERT(cJSON_GetArraySize(filtered) == 0,
                "Malformed JSON arguments should be filtered out");

    cJSON_Delete(delta);
    cJSON_Delete(filtered);
    tool_accumulator_destroy(acc);
}

static void test_moonshot_malformed_single_string_argument_repaired(void) {
    printf(COLOR_YELLOW "\nTest: Moonshot malformed single string argument is repaired\n" COLOR_RESET);

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    TEST_ASSERT(acc != NULL, "Accumulator should be created");

    cJSON *delta = create_tool_delta(0, "tool_bad_glob", "Glob", "{\"pattern\":/*logo-v3.0-no-name*");
    tool_accumulator_process_delta(acc, delta);

    TEST_ASSERT(acc->tool_calls_count == 1, "Should keep the tool call slot");
    TEST_ASSERT(tool_accumulator_count_valid(acc) == 1,
                "Malformed single-string args should be recoverable");

    cJSON *filtered = tool_accumulator_filter_valid(acc);
    TEST_ASSERT(cJSON_GetArraySize(filtered) == 1,
                "Recovered tool call should survive filtering");

    cJSON *tool = cJSON_GetArrayItem(filtered, 0);
    cJSON *func = cJSON_GetObjectItem(tool, "function");
    cJSON *args = func ? cJSON_GetObjectItem(func, "arguments") : NULL;

    TEST_ASSERT(args && cJSON_IsString(args), "Recovered arguments should be a string");
    TEST_ASSERT(strcmp(args->valuestring, "{\"pattern\":\"/*logo-v3.0-no-name*\"}") == 0,
                "Recovered arguments should be valid JSON");

    cJSON_Delete(delta);
    cJSON_Delete(filtered);
    tool_accumulator_destroy(acc);
}

static void test_moonshot_malformed_command_argument_repaired(void) {
    printf(COLOR_YELLOW "\nTest: Moonshot malformed command argument is repaired\n" COLOR_RESET);

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    TEST_ASSERT(acc != NULL, "Accumulator should be created");

    cJSON *delta = create_tool_delta(0, "tool_bad_bash", "Bash",
                                     "{\"command\":ls -la /Users/puter/Downloads/filesurf-logo/");
    tool_accumulator_process_delta(acc, delta);

    TEST_ASSERT(tool_accumulator_count_valid(acc) == 1,
                "Malformed command args should be recoverable");

    cJSON *filtered = tool_accumulator_filter_valid(acc);
    TEST_ASSERT(cJSON_GetArraySize(filtered) == 1,
                "Recovered Bash tool call should survive filtering");

    cJSON *tool = cJSON_GetArrayItem(filtered, 0);
    cJSON *func = cJSON_GetObjectItem(tool, "function");
    cJSON *args = func ? cJSON_GetObjectItem(func, "arguments") : NULL;

    TEST_ASSERT(args && cJSON_IsString(args), "Recovered Bash arguments should be a string");
    TEST_ASSERT(strcmp(args->valuestring,
                       "{\"command\":\"ls -la /Users/puter/Downloads/filesurf-logo/\"}") == 0,
                "Recovered command should be quoted and closed");

    cJSON_Delete(delta);
    cJSON_Delete(filtered);
    tool_accumulator_destroy(acc);
}

// ============================================================================
// Test Cases - Edge cases
// ============================================================================

static void test_null_and_empty_inputs(void) {
    printf(COLOR_YELLOW "\nTest: NULL and empty inputs\n" COLOR_RESET);

    // NULL accumulator
    int result = tool_accumulator_process_delta(NULL, NULL);
    TEST_ASSERT(result == -1, "NULL accumulator should return error");

    // NULL delta with valid accumulator
    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    result = tool_accumulator_process_delta(acc, NULL);
    TEST_ASSERT(result == -1, "NULL delta should return error");

    // Non-array delta
    cJSON *not_array = cJSON_CreateObject();
    result = tool_accumulator_process_delta(acc, not_array);
    TEST_ASSERT(result == -1, "Non-array delta should return error");

    cJSON_Delete(not_array);
    tool_accumulator_destroy(acc);

    // Valid count with NULL
    int valid = tool_accumulator_count_valid(NULL);
    TEST_ASSERT(valid == 0, "NULL accumulator should have 0 valid");

    // Filter with NULL
    cJSON *filtered = tool_accumulator_filter_valid(NULL);
    TEST_ASSERT(filtered != NULL, "NULL accumulator should return empty array");
    TEST_ASSERT(cJSON_GetArraySize(filtered) == 0, "Empty array should have size 0");
    cJSON_Delete(filtered);
}

static void test_reset_functionality(void) {
    printf(COLOR_YELLOW "\nTest: Reset functionality\n" COLOR_RESET);

    ToolCallAccumulator *acc = tool_accumulator_create(NULL);
    TEST_ASSERT(acc != NULL, "Accumulator should be created");

    // Add some tool calls
    cJSON *delta = create_tool_delta(0, "call_1", "Bash", "{\"c\":\"1\"}");
    tool_accumulator_process_delta(acc, delta);
    cJSON_Delete(delta);

    TEST_ASSERT(acc->tool_calls_count == 1, "Should have 1 tool call before reset");

    // Reset
    tool_accumulator_reset(acc);

    TEST_ASSERT(acc->tool_calls_count == 0, "Should have 0 tool calls after reset");
    TEST_ASSERT(acc->tool_calls_array != NULL, "Array should exist after reset");
    TEST_ASSERT(cJSON_GetArraySize(acc->tool_calls_array) == 0, "Array should be empty after reset");

    // Can add again after reset
    delta = create_tool_delta(0, "call_2", "Read", "{\"f\":\"/tmp\"}");
    tool_accumulator_process_delta(acc, delta);
    cJSON_Delete(delta);

    TEST_ASSERT(acc->tool_calls_count == 1, "Should have 1 tool call after re-adding");

    tool_accumulator_destroy(acc);
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
    printf("=== Streaming Tool Accumulator Unit Tests ===\n");
    printf("Testing tool call accumulation from streaming SSE events\n\n");

    // Basic accumulation tests
    test_single_complete_tool_call();
    test_incremental_tool_call_accumulation();
    test_multiple_tool_calls();

    // Empty name/id handling
    test_empty_name_filtered();
    test_empty_id_filtered();
    test_both_empty_filtered();

    // Mixed scenarios
    test_mixed_valid_and_invalid();

    // Real-world scenarios
    test_openai_streaming_format_simulation();
    test_incomplete_json_arguments_filtered();
    test_moonshot_malformed_single_string_argument_repaired();
    test_moonshot_malformed_command_argument_repaired();

    // Edge cases
    test_null_and_empty_inputs();
    test_reset_functionality();

    TEST_SUMMARY();
}
