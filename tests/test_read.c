/*
 * Unit Tests for Enhanced Read Tool
 *
 * Tests the Read tool's functionality including:
 * - Reading entire file
 * - Reading specific line ranges
 * - Reading from start_line onwards
 * - Reading up to end_line
 * - Error handling for invalid ranges
 *
 * Compilation: make test_read
 * Usage: ./test_read
 */

#define _POSIX_C_SOURCE 200809L
#define TEST_BUILD 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cjson/cJSON.h>

// Include the internal header for proper ConversationState definition
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
extern cJSON* tool_read(cJSON *params, ConversationState *state);
extern char* read_file(const char *path);
extern int write_file(const char *path, const char *content);

// Test utilities
#define TEST_FILE "/tmp/test_read_temp.txt"

static void setup_test_file(const char *content) {
    write_file(TEST_FILE, content);
}

static void cleanup_test_file(void) {
    unlink(TEST_FILE);
}

static void print_test_header(const char *test_name) {
    printf("\n%s[TEST]%s %s\n", COLOR_CYAN, COLOR_RESET, test_name);
}

static void assert_test(int condition, const char *message) {
    tests_run++;
    if (condition) {
        tests_passed++;
        printf("  %s✓%s %s\n", COLOR_GREEN, COLOR_RESET, message);
    } else {
        tests_failed++;
        printf("  %s✗%s %s\n", COLOR_RED, COLOR_RESET, message);
    }
}

static void print_test_summary(void) {
    printf("\n%s========================================%s\n", COLOR_CYAN, COLOR_RESET);
    printf("Tests run: %d\n", tests_run);
    printf("%sPassed: %d%s\n", COLOR_GREEN, tests_passed, COLOR_RESET);
    if (tests_failed > 0) {
        printf("%sFailed: %d%s\n", COLOR_RED, tests_failed, COLOR_RESET);
    } else {
        printf("Failed: %d\n", tests_failed);
    }
    printf("%s========================================%s\n", COLOR_CYAN, COLOR_RESET);
}

// Test functions

static void test_read_entire_file(ConversationState *state) {
    print_test_header("Read Entire File");

    const char *content = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5\n";
    setup_test_file(content);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);

    cJSON *result = tool_read(params, state);

    assert_test(result != NULL, "Result is not NULL");
    assert_test(!cJSON_HasObjectItem(result, "error"), "No error in result");

    cJSON *content_json = cJSON_GetObjectItem(result, "content");
    assert_test(content_json != NULL, "Content field exists");
    assert_test(strcmp(content_json->valuestring, content) == 0, "Content matches");

    cJSON *total_lines = cJSON_GetObjectItem(result, "total_lines");
    assert_test(total_lines != NULL, "total_lines field exists");
    assert_test(total_lines->valueint == 5, "Total lines is 5");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
}

static void test_read_line_range(ConversationState *state) {
    print_test_header("Read Specific Line Range (lines 2-4)");

    const char *content = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5\n";
    setup_test_file(content);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddNumberToObject(params, "start_line", 2);
    cJSON_AddNumberToObject(params, "end_line", 4);

    cJSON *result = tool_read(params, state);

    assert_test(result != NULL, "Result is not NULL");
    assert_test(!cJSON_HasObjectItem(result, "error"), "No error in result");

    cJSON *content_json = cJSON_GetObjectItem(result, "content");
    const char *expected = "Line 2\nLine 3\nLine 4\n";
    assert_test(strcmp(content_json->valuestring, expected) == 0, "Content matches lines 2-4");

    cJSON *start_line = cJSON_GetObjectItem(result, "start_line");
    cJSON *end_line = cJSON_GetObjectItem(result, "end_line");
    assert_test(start_line->valueint == 2, "start_line is 2");
    assert_test(end_line->valueint == 4, "end_line is 4");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
}

static void test_read_invalid_range(ConversationState *state) {
    print_test_header("Invalid Range (start > end)");

    const char *content = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5\n";
    setup_test_file(content);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddNumberToObject(params, "start_line", 4);
    cJSON_AddNumberToObject(params, "end_line", 2);

    cJSON *result = tool_read(params, state);

    assert_test(result != NULL, "Result is not NULL");
    assert_test(cJSON_HasObjectItem(result, "error"), "Error field exists");

    cJSON *error = cJSON_GetObjectItem(result, "error");
    assert_test(strstr(error->valuestring, "start_line must be <= end_line") != NULL,
                "Error message mentions invalid range");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
}

int main(void) {
    printf("\n%s========================================%s\n", COLOR_CYAN, COLOR_RESET);
    printf("   Enhanced Read Tool Test Suite\n");
    printf("%s========================================%s\n", COLOR_CYAN, COLOR_RESET);

    // Initialize state - zero out all fields first
    ConversationState state = {0};
    if (conversation_state_init(&state) != 0) {
        fprintf(stderr, "Failed to initialize conversation state\n");
        return 1;
    }

    // Use strdup to create a mutable copy for working_dir
    char *working_dir_copy = strdup("/tmp");
    if (!working_dir_copy) {
        fprintf(stderr, "Failed to allocate memory for working_dir\n");
        return 1;
    }

    state.api_key = NULL;
    state.working_dir = working_dir_copy;
    state.api_url = NULL;
    state.model = NULL;
    state.additional_dirs = NULL;
    state.additional_dirs_count = 0;
    state.additional_dirs_capacity = 0;
    state.session_id = NULL;
    state.persistence_db = NULL;
    state.todo_list = NULL;
    state.count = 0;

    // Run tests
    test_read_entire_file(&state);
    test_read_line_range(&state);
    test_read_invalid_range(&state);

    // Print summary
    print_test_summary();

    // Cleanup
    free(working_dir_copy);
    conversation_state_destroy(&state);

    return tests_failed > 0 ? 1 : 0;
}
