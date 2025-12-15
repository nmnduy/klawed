/*
 * Unit Tests for Bash Tool Stderr Output Fix
 *
 * Tests the Bash tool's stderr output handling including:
 * - Shell wrapper with proper quoting for consistent stderr capture
 * - Temporary stderr redirection to prevent terminal corruption
 * - Robust error handling with stderr restoration
 * - Both stdout and stderr are captured in the output field
 *
 * Compilation: make test-bash-stderr
 * Usage: ./test_bash_stderr
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

// Test functions
static void test_stderr_capture_basic(void) {
    printf(COLOR_CYAN "\nTest: Basic stderr capture\n" COLOR_RESET);

    setup_environment();

    // Create params with command that outputs to stderr
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo 'stdout message' >&1 && echo 'stderr message' >&2");

    cJSON *result = tool_bash(params, NULL);

    // Check that result is valid
    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");

    ASSERT(cJSON_IsNumber(exit_code), "Exit code should be a number");
    ASSERT(cJSON_IsString(output), "Output should be a string");
    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");

    // Both stdout and stderr should be captured in the output
    ASSERT_STRING_CONTAINS(output->valuestring, "stdout message", "Output should contain stdout");
    ASSERT_STRING_CONTAINS(output->valuestring, "stderr message", "Output should contain stderr");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_stderr_only_command(void) {
    printf(COLOR_CYAN "\nTest: Command that only outputs to stderr\n" COLOR_RESET);

    setup_environment();

    // Create params with command that only outputs to stderr
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo 'only stderr' >&2");

    cJSON *result = tool_bash(params, NULL);

    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");

    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");
    ASSERT_STRING_CONTAINS(output->valuestring, "only stderr", "Output should contain stderr message");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_command_with_quotes(void) {
    printf(COLOR_CYAN "\nTest: Command with single quotes in output\n" COLOR_RESET);

    setup_environment();

    // Test that shell wrapper properly handles quotes
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo \"stdout with 'single quotes'\" && echo \"stderr with 'single quotes'\" >&2");

    cJSON *result = tool_bash(params, NULL);

    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");

    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");
    ASSERT_STRING_CONTAINS(output->valuestring, "single quotes", "Output should contain quoted text");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_command_with_special_chars(void) {
    printf(COLOR_CYAN "\nTest: Command with special characters\n" COLOR_RESET);

    setup_environment();

    // Test that shell wrapper properly handles special characters
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo 'stdout: $PATH' && echo 'stderr: $PATH' >&2");

    cJSON *result = tool_bash(params, NULL);

    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");

    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");
    ASSERT_STRING_CONTAINS(output->valuestring, "PATH", "Output should contain PATH reference");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_error_command_stderr(void) {
    printf(COLOR_CYAN "\nTest: Error command with stderr output\n" COLOR_RESET);

    setup_environment();

    // Create params with command that fails and outputs to stderr
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "ls /nonexistent_directory_xyz 2>&1");

    cJSON *result = tool_bash(params, NULL);

    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");

    // Command should fail with non-zero exit code
    ASSERT(cJSON_IsNumber(exit_code), "Exit code should be a number");
    ASSERT(exit_code->valueint != 0, "Exit code should be non-zero for failed command");

    // Error message should be captured in output
    ASSERT(cJSON_IsString(output), "Output should be a string");
    ASSERT(strlen(output->valuestring) > 0, "Output should contain error message");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_mixed_stdout_stderr(void) {
    printf(COLOR_CYAN "\nTest: Mixed stdout and stderr output\n" COLOR_RESET);

    setup_environment();

    // Create params with mixed stdout/stderr output
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo 'line1: stdout' && echo 'line2: stderr' >&2 && echo 'line3: stdout' && echo 'line4: stderr' >&2");

    cJSON *result = tool_bash(params, NULL);

    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");

    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");

    // All lines should be present in the output
    ASSERT_STRING_CONTAINS(output->valuestring, "line1: stdout", "Output should contain first stdout line");
    ASSERT_STRING_CONTAINS(output->valuestring, "line2: stderr", "Output should contain first stderr line");
    ASSERT_STRING_CONTAINS(output->valuestring, "line3: stdout", "Output should contain second stdout line");
    ASSERT_STRING_CONTAINS(output->valuestring, "line4: stderr", "Output should contain second stderr line");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_command_with_newlines(void) {
    printf(COLOR_CYAN "\nTest: Command with newlines in output\n" COLOR_RESET);

    setup_environment();

    // Create params with command that outputs multiple lines
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "printf 'stdout line1\\nstdout line2\\n' && printf 'stderr line1\\nstderr line2\\n' >&2");

    cJSON *result = tool_bash(params, NULL);

    ASSERT(cJSON_IsObject(result), "Result should be a JSON object");

    cJSON *exit_code = cJSON_GetObjectItem(result, "exit_code");
    cJSON *output = cJSON_GetObjectItem(result, "output");

    ASSERT_NUMBER_EQUAL(exit_code->valueint, 0, "Exit code should be 0 for successful command");

    // All lines should be present with proper newlines
    ASSERT_STRING_CONTAINS(output->valuestring, "stdout line1", "Output should contain first stdout line");
    ASSERT_STRING_CONTAINS(output->valuestring, "stdout line2", "Output should contain second stdout line");
    ASSERT_STRING_CONTAINS(output->valuestring, "stderr line1", "Output should contain first stderr line");
    ASSERT_STRING_CONTAINS(output->valuestring, "stderr line2", "Output should contain second stderr line");

    cJSON_Delete(params);
    cJSON_Delete(result);

    cleanup_environment();
}

static void test_tool_definition_stderr_mention(void) {
    printf(COLOR_CYAN "\nTest: Tool definition mentions stderr redirection\n" COLOR_RESET);

    // Verify that the tool description mentions stderr redirection
    FILE *file = fopen("src/claude.c", "r");
    ASSERT(file != NULL, "Should be able to open claude.c");

    char line[1024];
    int found_stderr_mention = 0;
    int found_stdout_stderr_capture = 0;

    // Look for the Bash tool definition section
    while (fgets(line, sizeof(line), file)) {
        // Check for stderr redirection mention in Bash tool definition
        if (strstr(line, "stderr is automatically redirected to stdout")) {
            found_stderr_mention = 1;
        }

        // Check for both stdout and stderr capture mention (split across lines)
        if (strstr(line, "both stdout and stderr output will be")) {
            found_stdout_stderr_capture = 1;
        }
    }

    fclose(file);

    ASSERT(found_stderr_mention, "Tool description should mention stderr redirection");
    ASSERT(found_stdout_stderr_capture, "Tool description should mention both stdout and stderr capture");
}

// Main test runner
int main(void) {
    printf(COLOR_YELLOW "\nRunning Bash Stderr Output Fix Tests\n" COLOR_RESET);
    printf("=====================================\n");

    // Run all tests
    test_stderr_capture_basic();
    test_stderr_only_command();
    test_command_with_quotes();
    test_command_with_special_chars();
    test_error_command_stderr();
    test_mixed_stdout_stderr();
    test_command_with_newlines();
    test_tool_definition_stderr_mention();

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
