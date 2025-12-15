/*
 * Unit Tests for Enhanced Edit Tool
 *
 * Tests the Edit tool's functionality including:
 * - Simple string replacement
 * - Multi-replace (replace_all)
 * - Regex replacement
 * - Regex with multi-replace
 * - Error handling
 *
 * Compilation: make test
 * Usage: ./test_edit
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
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
extern cJSON* tool_edit(cJSON *params, ConversationState *state);
extern char* read_file(const char *path);
extern int write_file(const char *path, const char *content);

// Test utilities
#define TEST_FILE "/tmp/test_edit_temp.txt"

static void setup_test_file(const char *content) {
    write_file(TEST_FILE, content);
}

static void cleanup_test_file(void) {
    unlink(TEST_FILE);
}

static char* read_test_file(void) {
    return read_file(TEST_FILE);
}

// Helper to initialize ConversationState with working_dir
static void init_test_state(ConversationState *state) {
    memset(state, 0, sizeof(ConversationState));
    int rc = conversation_state_init(state);
    if (rc != 0) {
        fprintf(stderr, "Failed to initialize conversation state for test\n");
        exit(1);
    }
    state->working_dir = strdup("/tmp");
}

static void cleanup_test_state(ConversationState *state) {
    if (!state) {
        return;
    }
    free(state->working_dir);
    state->working_dir = NULL;
    conversation_state_destroy(state);
}

static void assert_true(const char *test_name, int condition, const char *message) {
    tests_run++;
    if (condition) {
        tests_passed++;
        printf("%s✓%s %s\n", COLOR_GREEN, COLOR_RESET, test_name);
    } else {
        tests_failed++;
        printf("%s✗%s %s: %s\n", COLOR_RED, COLOR_RESET, test_name, message);
    }
}

static void assert_file_equals(const char *test_name, const char *expected) {
    char *actual = read_test_file();
    int matches = (actual && strcmp(actual, expected) == 0);

    if (!matches) {
        printf("%s  Expected: %s%s\n", COLOR_YELLOW, expected, COLOR_RESET);
        printf("%s  Actual:   %s%s\n", COLOR_YELLOW, actual ? actual : "(null)", COLOR_RESET);
    }

    free(actual);
    assert_true(test_name, matches, "file content mismatch");
}

static void assert_json_has_field(const char *test_name, cJSON *json, const char *field) {
    int has_field = cJSON_HasObjectItem(json, field);
    char msg[256];
    snprintf(msg, sizeof(msg), "missing field '%s'", field);
    assert_true(test_name, has_field, msg);
}

static void assert_json_string_equals(const char *test_name, cJSON *json,
                                      const char *field, const char *expected) {
    cJSON *item = cJSON_GetObjectItem(json, field);
    int matches = (item && cJSON_IsString(item) && strcmp(item->valuestring, expected) == 0);

    if (!matches && item && cJSON_IsString(item)) {
        printf("%s  Expected %s: %s%s\n", COLOR_YELLOW, field, expected, COLOR_RESET);
        printf("%s  Actual %s:   %s%s\n", COLOR_YELLOW, field, item->valuestring, COLOR_RESET);
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "field '%s' value mismatch", field);
    assert_true(test_name, matches, msg);
}

static void assert_json_number_equals(const char *test_name, cJSON *json,
                                      const char *field, int expected) {
    cJSON *item = cJSON_GetObjectItem(json, field);
    int matches = (item && cJSON_IsNumber(item) && item->valueint == expected);

    if (!matches && item && cJSON_IsNumber(item)) {
        printf("%s  Expected %s: %d%s\n", COLOR_YELLOW, field, expected, COLOR_RESET);
        printf("%s  Actual %s:   %d%s\n", COLOR_YELLOW, field, item->valueint, COLOR_RESET);
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "field '%s' value mismatch", field);
    assert_true(test_name, matches, msg);
}

// ============================================================================
// Test Cases
// ============================================================================

static void test_simple_single_replace(void) {
    printf("\n%s[Test: Simple Single Replace]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("This is a test file.\nThe word test appears multiple times.\nWe use test to test the edit tool.\nTest test test!");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "test");
    cJSON_AddStringToObject(params, "new_string", "demo");

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");
    assert_json_number_equals("Replaces 1 occurrence", result, "replacements", 1);

    // Verify only first occurrence was replaced
    char *content = read_test_file();
    int count = 0;
    const char *pos = content;
    while ((pos = strstr(pos, "demo")) != NULL) {
        count++;
        pos++;
    }
    free(content);
    assert_true("File contains exactly 1 'demo'", count == 1, "replacement count mismatch");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_multi_replace(void) {
    printf("\n%s[Test: Multi-Replace (replace_all=true)]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("This is a test file.\nThe word test appears multiple times.\nWe use test to test the edit tool.\nTest test test!");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "test");
    cJSON_AddStringToObject(params, "new_string", "demo");
    cJSON_AddBoolToObject(params, "replace_all", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");
    assert_json_number_equals("Replaces 6 occurrences", result, "replacements", 6);

    // Verify all occurrences were replaced (6 lowercase "test")
    char *content = read_test_file();
    int has_test = (strstr(content, "test") != NULL);
    int count = 0;
    const char *pos = content;
    while ((pos = strstr(pos, "demo")) != NULL) {
        count++;
        pos++;
    }
    free(content);

    assert_true("File has no 'test' remaining", !has_test, "old string still present");
    assert_true("File contains 6 'demo'", count == 6, "replacement count mismatch");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_regex_single_replace(void) {
    printf("\n%s[Test: Regex Single Replace]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("int oldVar = 5;\nint oldVar2 = 10;\nprintf(\"Value: %d\", oldVar);");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "int oldVar[0-9]*");
    cJSON_AddStringToObject(params, "new_string", "int newVar");
    cJSON_AddBoolToObject(params, "use_regex", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");
    assert_json_number_equals("Replaces 1 occurrence", result, "replacements", 1);

    // Verify only first match was replaced
    char *content = read_test_file();
    int has_first_match = (strstr(content, "int newVar = 5") != NULL);
    int has_second_match = (strstr(content, "oldVar2") != NULL);
    free(content);

    assert_true("First match replaced", has_first_match, "first match not replaced");
    assert_true("Second match preserved", has_second_match, "second match incorrectly replaced");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_regex_multi_replace(void) {
    printf("\n%s[Test: Regex Multi-Replace]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("// TODO: Fix this bug\n// TODO: Add error handling\n// TODO: Optimize performance\nint x = 5;");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "// TODO:[^\n]*");
    cJSON_AddStringToObject(params, "new_string", "// DONE");
    cJSON_AddBoolToObject(params, "use_regex", 1);
    cJSON_AddBoolToObject(params, "replace_all", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");
    assert_json_number_equals("Replaces 3 occurrences", result, "replacements", 3);

    // Verify all TODOs were replaced
    char *content = read_test_file();
    int has_todo = (strstr(content, "TODO") != NULL);
    int count = 0;
    const char *pos = content;
    while ((pos = strstr(pos, "// DONE")) != NULL) {
        count++;
        pos++;
    }
    free(content);

    assert_true("No TODOs remaining", !has_todo, "TODO still present");
    assert_true("File contains 3 DONE", count == 3, "replacement count mismatch");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_regex_word_boundary(void) {
    printf("\n%s[Test: Regex with Space Boundaries]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("The oldVar variable and oldVar2 and myoldVar are different.");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", " oldVar ");
    cJSON_AddStringToObject(params, "new_string", " newVar ");
    cJSON_AddBoolToObject(params, "use_regex", 1);
    cJSON_AddBoolToObject(params, "replace_all", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");
    assert_json_number_equals("Replaces 1 occurrence", result, "replacements", 1);

    // Verify only the space-bounded "oldVar" was replaced
    char *content = read_test_file();
    int has_newvar = (strstr(content, " newVar ") != NULL);
    int has_oldvar2 = (strstr(content, "oldVar2") != NULL);
    int has_myoldvar = (strstr(content, "myoldVar") != NULL);
    free(content);

    assert_true("Space-bounded word replaced", has_newvar, "word not replaced");
    assert_true("oldVar2 preserved", has_oldvar2, "oldVar2 incorrectly replaced");
    assert_true("myoldVar preserved", has_myoldvar, "myoldVar incorrectly replaced");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_replace_numbers(void) {
    printf("\n%s[Test: Replace Numbers with Regex]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("Replace 123 with NUMBER\nReplace 456 with NUMBER\nReplace 789 with NUMBER");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "[0-9]+");
    cJSON_AddStringToObject(params, "new_string", "XXX");
    cJSON_AddBoolToObject(params, "use_regex", 1);
    cJSON_AddBoolToObject(params, "replace_all", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");
    assert_json_number_equals("Replaces 3 numbers", result, "replacements", 3);

    char *content = read_test_file();
    int has_numbers = 0;
    for (const char *p = content; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            has_numbers = 1;
            break;
        }
    }
    free(content);

    assert_true("No numbers remaining", !has_numbers, "numbers still present");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_string_not_found(void) {
    printf("\n%s[Test: String Not Found Error]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("This file has no match");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "nonexistent");
    cJSON_AddStringToObject(params, "new_string", "replacement");

    cJSON *result = tool_edit(params, &state);

    assert_json_has_field("Returns error", result, "error");

    cJSON *error = cJSON_GetObjectItem(result, "error");
    if (error && cJSON_IsString(error)) {
        int is_not_found = (strstr(error->valuestring, "not found") != NULL);
        assert_true("Error message mentions 'not found'", is_not_found,
                   "error message doesn't indicate string not found");
    }

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_invalid_regex(void) {
    printf("\n%s[Test: Invalid Regex Error]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("Some content");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "[invalid(regex");  // Unmatched bracket
    cJSON_AddStringToObject(params, "new_string", "replacement");
    cJSON_AddBoolToObject(params, "use_regex", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_has_field("Returns error", result, "error");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_missing_parameters(void) {
    printf("\n%s[Test: Missing Parameters Error]%s\n", COLOR_CYAN, COLOR_RESET);

    ConversationState state;
    init_test_state(&state);

    // Missing new_string
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "test");

    cJSON *result = tool_edit(params, &state);

    assert_json_has_field("Returns error for missing parameter", result, "error");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_state(&state);
}

static void test_empty_string_replacement(void) {
    printf("\n%s[Test: Replace with Empty String]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("Remove XXX from XXX this XXX text");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "XXX ");
    cJSON_AddStringToObject(params, "new_string", "");
    cJSON_AddBoolToObject(params, "replace_all", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");
    assert_json_number_equals("Replaces 3 occurrences", result, "replacements", 3);

    assert_file_equals("Empty string replacement works", "Remove from this text");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_multiline_content(void) {
    printf("\n%s[Test: Multi-line Content]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("Line 1: test\nLine 2: test\nLine 3: test\n");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "test");
    cJSON_AddStringToObject(params, "new_string", "result");
    cJSON_AddBoolToObject(params, "replace_all", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");
    assert_json_number_equals("Replaces all occurrences", result, "replacements", 3);

    assert_file_equals("Multi-line replacement works",
                       "Line 1: result\nLine 2: result\nLine 3: result\n");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    printf("\n%s╔════════════════════════════════════════════╗%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("%s║   Edit Tool Unit Test Suite               ║%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("%s╚════════════════════════════════════════════╝%s\n",
           COLOR_CYAN, COLOR_RESET);

    // Run all tests
    test_simple_single_replace();
    test_multi_replace();
    test_regex_single_replace();
    test_regex_multi_replace();
    test_regex_word_boundary();
    test_replace_numbers();
    test_string_not_found();
    test_invalid_regex();
    test_missing_parameters();
    test_empty_string_replacement();
    test_multiline_content();

    // Print summary
    printf("\n%s╔════════════════════════════════════════════╗%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("%s║   Test Summary                             ║%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("%s╚════════════════════════════════════════════╝%s\n",
           COLOR_CYAN, COLOR_RESET);

    printf("\nTotal tests:  %d\n", tests_run);
    printf("%sPassed:       %d%s\n", COLOR_GREEN, tests_passed, COLOR_RESET);

    if (tests_failed > 0) {
        printf("%sFailed:       %d%s\n", COLOR_RED, tests_failed, COLOR_RESET);
        return 1;
    } else {
        printf("\n%s✓ All tests passed!%s\n\n", COLOR_GREEN, COLOR_RESET);
        return 0;
    }
}
