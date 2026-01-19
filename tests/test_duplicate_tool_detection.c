/*
 * test_duplicate_tool_detection.c - Unit tests for detect_duplicate_tool_names()
 *
 * Tests the duplicate tool detection function to ensure it correctly identifies
 * duplicate tool names in both Messages and Responses API formats.
 *
 * Compilation: make test-duplicate-tool-detection
 * Usage: ./build/test_duplicate_tool_detection
 */

#define _POSIX_C_SOURCE 200809L
#define TEST_BUILD 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/stdlib.h>

#include <cjson/cJSON.h>

// Include internal header to get access to detect_duplicate_tool_names
#include "../src/tools/tool_definitions.h"

// Test framework colors
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void assert_null(const char *test_name, const void *ptr, const char *message) {
    tests_run++;
    if (ptr == NULL) {
        tests_passed++;
        printf("  ✓ %s\n", test_name);
    } else {
        tests_failed++;
        printf("  ✗ %s: expected NULL but got %p - %s\n", test_name, ptr, message);
    }
}

static void assert_not_null(const char *test_name, const void *ptr, const char *message) {
    tests_run++;
    if (ptr != NULL) {
        tests_passed++;
        printf("  ✓ %s\n", test_name);
    } else {
        tests_failed++;
        printf("  ✗ %s: expected non-NULL but got NULL - %s\n", test_name, message);
    }
}

static void assert_string_equal(const char *test_name, const char *expected, const char *actual) {
    tests_run++;
    if (expected == NULL && actual == NULL) {
        tests_passed++;
        printf("  ✓ %s\n", test_name);
    } else if (expected && actual && strcmp(expected, actual) == 0) {
        tests_passed++;
        printf("  ✓ %s\n", test_name);
    } else {
        tests_failed++;
        printf("  ✗ %s: expected '%s' but got '%s'\n", test_name,
               expected ? expected : "(null)",
               actual ? actual : "(null)");
    }
}

// ============================================================================
// Test cases
// ============================================================================

static void test_null_and_empty_inputs(void) {
    printf("\n  [Null and Empty Input Tests]\n");

    // NULL input
    assert_null("NULL tool_array returns NULL", detect_duplicate_tool_names(NULL), "");

    // Non-array input
    cJSON *non_array = cJSON_CreateObject();
    assert_null("Non-array JSON returns NULL", detect_duplicate_tool_names(non_array), "");
    cJSON_Delete(non_array);

    // Empty array
    cJSON *empty_array = cJSON_CreateArray();
    assert_null("Empty array returns NULL", detect_duplicate_tool_names(empty_array), "");
    cJSON_Delete(empty_array);
}

static void test_single_tool_no_duplicate(void) {
    printf("\n  [Single Tool Tests]\n");

    // Single tool in Messages API format
    cJSON *tool_array = cJSON_CreateArray();
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON *func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "SingleTool");
    cJSON_AddStringToObject(func, "description", "A single tool");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    assert_null("Single tool has no duplicate", detect_duplicate_tool_names(tool_array), "");
    cJSON_Delete(tool_array);
}

static void test_multiple_unique_tools(void) {
    printf("\n  [Multiple Unique Tools Tests]\n");

    // Multiple unique tools in Messages API format
    cJSON *tool_array = cJSON_CreateArray();

    for (int i = 0; i < 5; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "type", "function");
        cJSON *func = cJSON_CreateObject();
        char name[64];
        snprintf(name, sizeof(name), "UniqueTool%d", i);
        cJSON_AddStringToObject(func, "name", name);
        cJSON_AddStringToObject(func, "description", "A unique tool");
        cJSON_AddItemToObject(tool, "function", func);
        cJSON_AddItemToArray(tool_array, tool);
    }

    assert_null("5 unique tools have no duplicates", detect_duplicate_tool_names(tool_array), "");
    cJSON_Delete(tool_array);
}

static void test_duplicate_at_start(void) {
    printf("\n  [Duplicate at Start Tests]\n");

    // Duplicate at position 0
    cJSON *tool_array = cJSON_CreateArray();

    // Tool 0: ToolA
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON *func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "ToolA");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    // Tool 1: ToolB
    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "ToolB");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    // Tool 2: ToolA (duplicate of position 0)
    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "ToolA");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    const char *dup = detect_duplicate_tool_names(tool_array);
    assert_not_null("Duplicate at start detected", dup, "");
    assert_string_equal("Duplicate name is 'ToolA'", "ToolA", dup);
    cJSON_Delete(tool_array);
}

static void test_duplicate_at_end(void) {
    printf("\n  [Duplicate at End Tests]\n");

    // Duplicate at last position
    cJSON *tool_array = cJSON_CreateArray();

    for (int i = 0; i < 5; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "type", "function");
        cJSON *func = cJSON_CreateObject();
        char name[64];
        snprintf(name, sizeof(name), "Tool%d", i);
        cJSON_AddStringToObject(func, "name", name);
        cJSON_AddItemToObject(tool, "function", func);
        cJSON_AddItemToArray(tool_array, tool);
    }

    // Add duplicate of Tool2 at the end
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON *func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "Tool2");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    const char *dup = detect_duplicate_tool_names(tool_array);
    assert_not_null("Duplicate at end detected", dup, "");
    assert_string_equal("Duplicate name is 'Tool2'", "Tool2", dup);
    cJSON_Delete(tool_array);
}

static void test_duplicate_in_middle(void) {
    printf("\n  [Duplicate in Middle Tests]\n");

    cJSON *tool_array = cJSON_CreateArray();

    // Tool 0: FirstTool
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON *func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "FirstTool");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    // Tool 1: MiddleTool
    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "MiddleTool");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    // Tool 2: Duplicate of MiddleTool
    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "MiddleTool");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    // Tool 3: LastTool
    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "LastTool");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    const char *dup = detect_duplicate_tool_names(tool_array);
    assert_not_null("Duplicate in middle detected", dup, "");
    assert_string_equal("Duplicate name is 'MiddleTool'", "MiddleTool", dup);
    cJSON_Delete(tool_array);
}

static void test_multiple_duplicates(void) {
    printf("\n  [Multiple Duplicates Tests]\n");

    // When there are multiple duplicates, should detect the first one
    cJSON *tool_array = cJSON_CreateArray();

    // Add ToolA
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON *func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "ToolA");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    // Add ToolB
    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "ToolB");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    // Add duplicate ToolA
    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "ToolA");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    // Add duplicate ToolB
    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "ToolB");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    const char *dup = detect_duplicate_tool_names(tool_array);
    assert_not_null("Multiple duplicates detected", dup, "");
    // Should detect ToolA first (appears first as duplicate)
    assert_string_equal("First duplicate is 'ToolA'", "ToolA", dup);
    cJSON_Delete(tool_array);
}

static void test_responses_api_format(void) {
    printf("\n  [Responses API Format Tests]\n");

    // Responses API format: { type: "function", name: "ToolName", ... }
    cJSON *tool_array = cJSON_CreateArray();

    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddStringToObject(tool, "name", "ResponsesTool");
    cJSON_AddStringToObject(tool, "description", "Tool in responses format");
    cJSON_AddItemToArray(tool_array, tool);

    // Add duplicate
    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddStringToObject(tool, "name", "ResponsesTool");
    cJSON_AddStringToObject(tool, "description", "Duplicate tool");
    cJSON_AddItemToArray(tool_array, tool);

    const char *dup = detect_duplicate_tool_names(tool_array);
    assert_not_null("Responses API format duplicate detected", dup, "");
    assert_string_equal("Duplicate name is 'ResponsesTool'", "ResponsesTool", dup);
    cJSON_Delete(tool_array);
}

static void test_mixed_formats(void) {
    printf("\n  [Mixed API Format Tests]\n");

    // Mix of Messages and Responses API formats
    cJSON *tool_array = cJSON_CreateArray();

    // Messages format: { type: "function", function: { name: "..." } }
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON *func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "MixedTool");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    // Responses format: { type: "function", name: "..." }
    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddStringToObject(tool, "name", "MixedTool");
    cJSON_AddItemToArray(tool_array, tool);

    const char *dup = detect_duplicate_tool_names(tool_array);
    assert_not_null("Mixed format duplicate detected", dup, "");
    assert_string_equal("Duplicate name is 'MixedTool'", "MixedTool", dup);
    cJSON_Delete(tool_array);
}

static void test_web_browse_agent_specific(void) {
    printf("\n  [web_browse_agent Specific Tests]\n");

    // Test the specific case that was causing the bug
    cJSON *tool_array = cJSON_CreateArray();

    // Add web_browse_agent twice (simulating the original bug)
    for (int i = 0; i < 2; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "type", "function");
        cJSON *func = cJSON_CreateObject();
        cJSON_AddStringToObject(func, "name", "web_browse_agent");
        cJSON_AddStringToObject(func, "description", "Directly run the web_browse_agent binary");
        cJSON_AddItemToObject(tool, "function", func);
        cJSON_AddItemToArray(tool_array, tool);
    }

    const char *dup = detect_duplicate_tool_names(tool_array);
    assert_not_null("Duplicate web_browse_agent detected", dup, "");
    assert_string_equal("Duplicate is 'web_browse_agent'", "web_browse_agent", dup);
    cJSON_Delete(tool_array);
}

static void test_tool_with_no_name_field(void) {
    printf("\n  [Tool with Missing Name Field Tests]\n");

    // Tool array with a tool that has no name field
    cJSON *tool_array = cJSON_CreateArray();

    // Valid tool
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON *func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "ValidTool");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    // Tool without name field (should be skipped)
    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    // No function.name field
    cJSON_AddItemToArray(tool_array, tool);

    // Another valid tool
    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "AnotherValidTool");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    // No duplicates, should return NULL
    assert_null("Tool without name field is skipped", detect_duplicate_tool_names(tool_array), "");
    cJSON_Delete(tool_array);
}

static void test_case_sensitive_duplicates(void) {
    printf("\n  [Case-Sensitive Duplicate Tests]\n");

    // Tools with same name but different cases should be considered duplicates
    cJSON *tool_array = cJSON_CreateArray();

    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON *func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "MyTool");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "MyTool");  // Exact same case
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    const char *dup = detect_duplicate_tool_names(tool_array);
    assert_not_null("Case-sensitive duplicate detected", dup, "");
    assert_string_equal("Duplicate is 'MyTool'", "MyTool", dup);
    cJSON_Delete(tool_array);
}

static void test_simulated_core_tools(void) {
    printf("\n  [Simulated Core Tools Tests]\n");

    // Simulate core tools that Klawed typically uses
    cJSON *tool_array = cJSON_CreateArray();

    // Core tools
    const char *core_tools[] = {
        "Bash", "Read", "Write", "Edit", "MultiEdit",
        "Glob", "Grep", "TodoWrite", "Sleep", "Subagent",
        "CheckSubagentProgress", "InterruptSubagent",
        "MemoryStore", "MemoryRecall", "MemorySearch"
    };

    for (int i = 0; i < 15; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "type", "function");
        cJSON *func = cJSON_CreateObject();
        cJSON_AddStringToObject(func, "name", core_tools[i]);
        cJSON_AddStringToObject(func, "description", "Core tool");
        cJSON_AddItemToObject(tool, "function", func);
        cJSON_AddItemToArray(tool_array, tool);
    }

    // Should not have any duplicates
    assert_null("Core tools have no duplicates", detect_duplicate_tool_names(tool_array), "");
    cJSON_Delete(tool_array);
}

static void test_large_tool_array(void) {
    printf("\n  [Large Tool Array Tests]\n");

    // Test with a larger number of tools
    cJSON *tool_array = cJSON_CreateArray();

    for (int i = 0; i < 100; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "type", "function");
        cJSON *func = cJSON_CreateObject();
        char name[64];
        snprintf(name, sizeof(name), "LargeTool%d", i);
        cJSON_AddStringToObject(func, "name", name);
        cJSON_AddItemToObject(tool, "function", func);
        cJSON_AddItemToArray(tool_array, tool);
    }

    assert_null("100 unique tools have no duplicates", detect_duplicate_tool_names(tool_array), "");
    cJSON_Delete(tool_array);
}

static void test_many_duplicates(void) {
    printf("\n  [Many Duplicates Tests]\n");

    // Test with many duplicates in array
    cJSON *tool_array = cJSON_CreateArray();

    // Add 50 unique tools
    for (int i = 0; i < 50; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "type", "function");
        cJSON *func = cJSON_CreateObject();
        char name[64];
        snprintf(name, sizeof(name), "ManyTool%d", i);
        cJSON_AddStringToObject(func, "name", name);
        cJSON_AddItemToObject(tool, "function", func);
        cJSON_AddItemToArray(tool_array, tool);
    }

    // Add duplicate of tool 25
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON *func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", "ManyTool25");
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tool_array, tool);

    const char *dup = detect_duplicate_tool_names(tool_array);
    assert_not_null("Duplicate in large array detected", dup, "");
    assert_string_equal("Duplicate is 'ManyTool25'", "ManyTool25", dup);
    cJSON_Delete(tool_array);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("\n");
    printf(COLOR_CYAN "========================================\n");
    printf("  Duplicate Tool Detection Tests\n");
    printf("========================================" COLOR_RESET "\n");

    // Run all tests
    test_null_and_empty_inputs();
    test_single_tool_no_duplicate();
    test_multiple_unique_tools();
    test_duplicate_at_start();
    test_duplicate_at_end();
    test_duplicate_in_middle();
    test_multiple_duplicates();
    test_responses_api_format();
    test_mixed_formats();
    test_web_browse_agent_specific();
    test_tool_with_no_name_field();
    test_case_sensitive_duplicates();
    test_simulated_core_tools();
    test_large_tool_array();
    test_many_duplicates();

    // Print summary
    printf(COLOR_CYAN "\n========================================\n");
    printf("  Test Summary\n");
    printf("========================================\n" COLOR_RESET);
    printf("  Tests run:    %d\n", tests_run);
    printf(COLOR_GREEN "  Tests passed: %d\n" COLOR_RESET, tests_passed);
    if (tests_failed > 0) {
        printf(COLOR_RED "  Tests failed: %d\n" COLOR_RESET, tests_failed);
    }
    printf("========================================\n");

    if (tests_failed == 0) {
        printf(COLOR_GREEN "\n✓ All duplicate tool detection tests passed!\n\n" COLOR_RESET);
        return 0;
    } else {
        printf(COLOR_RED "\n✗ Some tests failed.\n\n" COLOR_RESET);
        return 1;
    }
}
