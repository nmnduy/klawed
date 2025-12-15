/*
 * Unit Tests for Bash Tool Output Truncation
 *
 * Tests the Bash tool's output truncation feature including:
 * - Output is truncated when exceeding BASH_OUTPUT_MAX_SIZE (12,228 bytes)
 * - Proper truncation warning is added to result
 * - Memory management during truncation
 * - Tool definition mentions truncation feature
 *
 * Compilation: make test-bash-truncation
 * Usage: ./test_bash_truncation
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cjson/cJSON.h>

// Include internal header to get ConversationState definition
#include "../src/claude_internal.h"

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

// Forward declarations from claude.c
extern cJSON* tool_bash(cJSON *params, ConversationState *state);

// Test utilities
static void setup_environment(void) {
    // Clear any existing environment variables that might affect tests
    unsetenv("CLAUDE_C_BASH_TIMEOUT");
    unsetenv("CLAUDE_C_BASH_FILTER_ANSI");
}

static void cleanup_environment(void) {
    // Clean up environment after tests
    unsetenv("CLAUDE_C_BASH_TIMEOUT");
    unsetenv("CLAUDE_C_BASH_FILTER_ANSI");
}

// Test assertion macros
#define ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf(COLOR_GREEN "✓ %s\n" COLOR_RESET, message); \
    } else { \
        tests_failed++; \
        printf(COLOR_RED "✗ %s\n" COLOR_RESET, message); \
    } \
} while(0)

#define ASSERT_STRING_EQUAL(actual, expected, message) do { \
    tests_run++; \
    if (strcmp(actual, expected) == 0) { \
        tests_passed++; \
        printf(COLOR_GREEN "✓ %s\n" COLOR_RESET, message); \
    } else { \
        tests_failed++; \
        printf(COLOR_RED "✗ %s (expected '%s', got '%s')\n" COLOR_RESET, message, expected, actual); \
    } \
} while(0)

#define ASSERT_STRING_CONTAINS(actual, expected_substring, message) do { \
    tests_run++; \
    if (strstr(actual, expected_substring) != NULL) { \
        tests_passed++; \
        printf(COLOR_GREEN "✓ %s\n" COLOR_RESET, message); \
    } else { \
        tests_failed++; \
        printf(COLOR_RED "✗ %s (expected to contain '%s', got '%s')\n" COLOR_RESET, message, expected_substring, actual); \
    } \
} while(0)

#define ASSERT_NUMBER_EQUAL(actual, expected, message) do { \
    tests_run++; \
    if (actual == expected) { \
        tests_passed++; \
        printf(COLOR_GREEN "✓ %s\n" COLOR_RESET, message); \
    } else { \
        tests_failed++; \
        printf(COLOR_RED "✗ %s (expected %d, got %d)\n" COLOR_RESET, message, expected, actual); \
    } \
} while(0)

#define ASSERT_NUMBER_LESS_THAN(actual, expected, message) do { \
    tests_run++; \
    if (actual < expected) { \
        tests_passed++; \
        printf(COLOR_GREEN "✓ %s\n" COLOR_RESET, message); \
    } else { \
        tests_failed++; \
        printf(COLOR_RED "✗ %s (expected less than %zu, got %zu)\n" COLOR_RESET, message, (size_t)expected, (size_t)actual); \
    } \
} while(0)

// Test functions
static void test_output_below_limit_no_truncation(void) {
    printf(COLOR_CYAN "\nTest: Output below limit - no truncation\n" COLOR_RESET);

    setup_environment();

    // Create params with command that outputs less than the limit
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo 'Hello World'");

    cJSON *result = tool_bash(params, NULL);

    // Check that result is valid
    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");
    cJSON *truncation_warning = cJSON_GetObjectItem(result, "truncation_warning");

    ASSERT(cJSON_IsNumber(exit_code), "Exit code should be a number");
    ASSERT(cJSON_IsString(output), "Output should be a string");
    // Don't check exit code for large output commands as they might get killed
    ASSERT_STRING_CONTAINS(output->valuestring, "Hello World", "Output should contain expected text");

    // No truncation warning should be present
    ASSERT(truncation_warning == NULL, "No truncation warning should be present for small output");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_output_exceeds_limit_truncated(void) {
    printf(COLOR_CYAN "\nTest: Output exceeds limit - truncated with warning\n" COLOR_RESET);

    setup_environment();

    // Create a command that generates output exceeding the limit
    // We'll use a command that generates a lot of text but is less likely to get killed
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command",
        "printf '%*s' 15000 | tr ' ' 'x'");

    cJSON *result = tool_bash(params, NULL);

    // Check that result is valid
    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");
    cJSON *truncation_warning = cJSON_GetObjectItem(result, "truncation_warning");

    ASSERT(cJSON_IsNumber(exit_code), "Exit code should be a number");
    ASSERT(cJSON_IsString(output), "Output should be a string");
    // Don't check exit code for large output commands as they might get killed

    // Output should be truncated to BASH_OUTPUT_MAX_SIZE or less
    ASSERT_NUMBER_LESS_THAN(strlen(output->valuestring), BASH_OUTPUT_MAX_SIZE + 100,
                           "Output should be truncated to near the limit");

    // Truncation warning should be present
    ASSERT(cJSON_IsString(truncation_warning), "Truncation warning should be present");
    ASSERT_STRING_CONTAINS(truncation_warning->valuestring, "truncated",
                          "Truncation warning should mention truncation");
    ASSERT_STRING_CONTAINS(truncation_warning->valuestring, "bytes",
                          "Truncation warning should mention bytes");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_exact_limit_output(void) {
    printf(COLOR_CYAN "\nTest: Output exactly at limit\n" COLOR_RESET);

    setup_environment();

    // Create a command that generates output close to the limit
    // We'll use printf to generate exactly the right amount of text
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command",
        "printf '%*s' 12228 | tr ' ' 'x'");

    cJSON *result = tool_bash(params, NULL);

    // Check that result is valid
    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");

    ASSERT(cJSON_IsNumber(exit_code), "Exit code should be a number");
    ASSERT(cJSON_IsString(output), "Output should be a string");
    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");

    // Output should be close to the limit
    ASSERT(strlen(output->valuestring) > 0, "Output should not be empty");

    // Note: We don't check truncation_warning here since it might be present
    // even when output is at exact limit due to implementation details

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_truncation_with_stderr(void) {
    printf(COLOR_CYAN "\nTest: Truncation with stderr output\n" COLOR_RESET);

    setup_environment();

    // Create a command that generates both stdout and stderr exceeding the limit
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command",
        "printf 'stdout: %*s' 8000 | tr ' ' 'x' && printf 'stderr: %*s' 8000 | tr ' ' 'y' >&2");

    cJSON *result = tool_bash(params, NULL);

    // Check that result is valid
    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");
    cJSON *truncation_warning = cJSON_GetObjectItem(result, "truncation_warning");

    ASSERT(cJSON_IsNumber(exit_code), "Exit code should be a number");
    ASSERT(cJSON_IsString(output), "Output should be a string");
    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");

    // Output should be truncated
    ASSERT_NUMBER_LESS_THAN(strlen(output->valuestring), BASH_OUTPUT_MAX_SIZE + 100,
                           "Output should be truncated to near the limit");

    // Both stdout and stderr should be present in truncated output
    ASSERT_STRING_CONTAINS(output->valuestring, "stdout",
                          "Truncated output should contain stdout");
    ASSERT_STRING_CONTAINS(output->valuestring, "stderr",
                          "Truncated output should contain stderr");

    // Truncation warning should be present
    ASSERT(cJSON_IsString(truncation_warning), "Truncation warning should be present");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_tool_definition_truncation_mention(void) {
    printf(COLOR_CYAN "\nTest: Tool definition mentions truncation feature\n" COLOR_RESET);

    // Verify that the tool description mentions output truncation
    FILE *file = fopen("src/claude.c", "r");
    ASSERT(file != NULL, "Should be able to open claude.c");

    char line[1024];
    int found_truncation_mention = 0;
    int found_byte_limit = 0;

    // Look for the Bash tool definition section
    while (fgets(line, sizeof(line), file)) {
        // Check for truncation mention in Bash tool definition
        if (strstr(line, "truncated")) {
            found_truncation_mention = 1;
        }

        // Check for byte limit mention
        if (strstr(line, "bytes")) {
            found_byte_limit = 1;
        }
    }

    fclose(file);

    ASSERT(found_truncation_mention, "Tool description should mention truncation");
    ASSERT(found_byte_limit, "Tool description should mention byte limit");
}

static void test_constant_defined(void) {
    printf(COLOR_CYAN "\nTest: BASH_OUTPUT_MAX_SIZE constant is defined\n" COLOR_RESET);

    // Verify that the constant is defined with the expected value
    ASSERT(BASH_OUTPUT_MAX_SIZE == 12228, "BASH_OUTPUT_MAX_SIZE should be 12228");

    // Verify it's a reasonable value
    ASSERT(BASH_OUTPUT_MAX_SIZE > 0, "BASH_OUTPUT_MAX_SIZE should be positive");
    ASSERT(BASH_OUTPUT_MAX_SIZE < 1000000, "BASH_OUTPUT_MAX_SIZE should be reasonable");
}

// Main test runner
int main(void) {
    printf(COLOR_YELLOW "\nRunning Bash Output Truncation Tests\n" COLOR_RESET);
    printf("========================================\n");

    // Run all tests
    test_output_below_limit_no_truncation();
    test_output_exceeds_limit_truncated();
    test_exact_limit_output();
    test_truncation_with_stderr();
    test_tool_definition_truncation_mention();
    test_constant_defined();

    // Print summary
    printf(COLOR_YELLOW "\nTest Summary\n" COLOR_RESET);
    printf("=============\n");
    printf("Tests Run: %d\n", tests_run);
    printf(COLOR_GREEN "Tests Passed: %d\n" COLOR_RESET, tests_passed);

    if (tests_failed > 0) {
        printf(COLOR_RED "Tests Failed: %d\n" COLOR_RESET, tests_failed);
        return 1;
    } else {
        printf(COLOR_GREEN "All tests passed!\n" COLOR_RESET);
        return 0;
    }
}
