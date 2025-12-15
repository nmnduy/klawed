/*
 * Unit Tests for Bash Tool Timeout Functionality
 *
 * Tests the Bash tool's timeout functionality including:
 * - Timeout parameter handling
 * - Environment variable timeout configuration
 * - Command timeout behavior
 * - Process cleanup on timeout
 * - Exit code handling for timeout
 *
 * Compilation: make test-bash-timeout
 * Usage: ./test_bash_timeout
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
    // Clear any existing timeout environment variable
    unsetenv("CLAUDE_C_BASH_TIMEOUT");
}

static void cleanup_environment(void) {
    // Clean up environment after tests
    unsetenv("CLAUDE_C_BASH_TIMEOUT");
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

// Test functions
static void test_default_timeout(void) {
    printf(COLOR_CYAN "\nTest: Default timeout (30 seconds)\n" COLOR_RESET);

    setup_environment();

    // Create params with just a command
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo 'hello world'");

    cJSON *result = tool_bash(params, NULL);

    // Check that result is valid
    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");

    ASSERT(cJSON_IsNumber(exit_code), "Exit code should be a number");
    ASSERT(cJSON_IsString(output), "Output should be a string");
    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");
    ASSERT_STRING_EQUAL(output->valuestring, "hello world\n", "Output should match command output");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_timeout_parameter_zero(void) {
    printf(COLOR_CYAN "\nTest: Timeout parameter set to 0 (no timeout)\n" COLOR_RESET);

    setup_environment();

    // Create params with timeout set to 0
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "sleep 1 && echo 'no timeout'");
    cJSON_AddNumberToObject(params, "timeout", 0);

    cJSON *result = tool_bash(params, NULL);

    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");

    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");
    ASSERT_STRING_EQUAL(output->valuestring, "no timeout\n", "Output should match command output");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_timeout_parameter_custom(void) {
    printf(COLOR_CYAN "\nTest: Custom timeout parameter (2 seconds)\n" COLOR_RESET);

    setup_environment();

    // Create params with a short timeout
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "sleep 5 && echo 'this should timeout'");
    cJSON_AddNumberToObject(params, "timeout", 2);

    cJSON *result = tool_bash(params, NULL);

    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *timeout_error = cJSON_GetObjectItem(result, "timeout_error");

    // Command should timeout with exit code -2
    ASSERT_NUMBER_EQUAL(exit_code->valueint, -2, "Exit code should be -2 for timeout");
    ASSERT(cJSON_IsString(timeout_error), "Timeout error message should be present");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_environment_timeout(void) {
    printf(COLOR_CYAN "\nTest: Environment variable timeout\n" COLOR_RESET);

    setup_environment();

    // Set environment variable
    setenv("CLAUDE_C_BASH_TIMEOUT", "1", 1);

    // Create params without timeout parameter
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "sleep 3 && echo 'should timeout from env'");

    cJSON *result = tool_bash(params, NULL);

    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *timeout_error = cJSON_GetObjectItem(result, "timeout_error");

    // Command should timeout with exit code -2
    ASSERT_NUMBER_EQUAL(exit_code->valueint, -2, "Exit code should be -2 for timeout");
    ASSERT(cJSON_IsString(timeout_error), "Timeout error message should be present");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_parameter_overrides_environment(void) {
    printf(COLOR_CYAN "\nTest: Parameter timeout overrides environment\n" COLOR_RESET);

    setup_environment();

    // Set environment variable to a short timeout
    setenv("CLAUDE_C_BASH_TIMEOUT", "1", 1);

    // Create params with longer timeout parameter
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "sleep 2 && echo 'parameter timeout wins'");
    cJSON_AddNumberToObject(params, "timeout", 3);

    cJSON *result = tool_bash(params, NULL);

    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");

    // Command should complete successfully because parameter timeout is longer
    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");
    ASSERT_STRING_EQUAL(output->valuestring, "parameter timeout wins\n", "Output should match command output");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_negative_timeout_parameter(void) {
    printf(COLOR_CYAN "\nTest: Negative timeout parameter treated as 0\n" COLOR_RESET);

    setup_environment();

    // Create params with negative timeout
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "sleep 1 && echo 'negative timeout'");
    cJSON_AddNumberToObject(params, "timeout", -5);

    cJSON *result = tool_bash(params, NULL);

    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");

    // Command should complete successfully (negative timeout treated as 0 = no timeout)
    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");
    ASSERT_STRING_EQUAL(output->valuestring, "negative timeout\n", "Output should match command output");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_successful_command_with_timeout(void) {
    printf(COLOR_CYAN "\nTest: Successful command within timeout\n" COLOR_RESET);

    setup_environment();

    // Create params with timeout that should be sufficient
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo 'quick command'");
    cJSON_AddNumberToObject(params, "timeout", 5);

    cJSON *result = tool_bash(params, NULL);

    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");
    cJSON *timeout_error = cJSON_GetObjectItem(result, "timeout_error");

    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");
    ASSERT_STRING_EQUAL(output->valuestring, "quick command\n", "Output should match command output");
    ASSERT(timeout_error == NULL, "No timeout error should be present for successful command");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_tool_definition_includes_timeout(void) {
    printf(COLOR_CYAN "\nTest: Tool definition includes timeout parameter\n" COLOR_RESET);

    // This test would require access to get_tool_definitions function
    // For now, we'll verify the timeout parameter is documented in the description
    // by checking the actual implementation

    // Read the tool definition section from claude.c
    FILE *file = fopen("src/claude.c", "r");
    ASSERT(file != NULL, "Should be able to open claude.c");

    char line[1024];
    int found_timeout_description = 0;
    int found_timeout_parameter = 0;

    // Look for the Bash tool definition section
    while (fgets(line, sizeof(line), file)) {
        // Check for timeout description in Bash tool definition
        if (strstr(line, "Commands have a configurable timeout")) {
            found_timeout_description = 1;
        }

        // Check for timeout parameter in Bash tool schema
        if (strstr(line, "cJSON_AddItemToObject(bash_props, \"timeout\"")) {
            found_timeout_parameter = 1;
        }
    }

    fclose(file);

    ASSERT(found_timeout_description, "Tool description should mention configurable timeout");
    ASSERT(found_timeout_parameter, "Tool definition should include timeout parameter");
}

// Main test runner
int main(void) {
    printf(COLOR_YELLOW "\nRunning Bash Timeout Tests\n" COLOR_RESET);
    printf("===========================\n");

    // Run all tests
    test_default_timeout();
    test_timeout_parameter_zero();
    test_timeout_parameter_custom();
    test_environment_timeout();
    test_parameter_overrides_environment();
    test_negative_timeout_parameter();
    test_successful_command_with_timeout();
    test_tool_definition_includes_timeout();

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
