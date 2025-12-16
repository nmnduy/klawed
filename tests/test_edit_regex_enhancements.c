/*
 * Test suite for Edit tool regex enhancements
 * Tests capture groups, backreferences, and regex flags
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cjson/cJSON.h>
#include <assert.h>

// Include internal header to get ConversationState definition
#include "../src/klawed_internal.h"

// Color definitions for output
#define COLOR_GREEN "\033[0;32m"
#define COLOR_RED "\033[0;31m"
#define COLOR_CYAN "\033[0;36m"
#define COLOR_YELLOW "\033[0;33m"
#define COLOR_RESET "\033[0m"

// Test file path
#define TEST_FILE "/tmp/test_edit_regex_enhancements.txt"

// External functions from klawed.c
extern cJSON* tool_edit(cJSON *params, ConversationState *state);

// Test helper functions
static void setup_test_file(const char *content) {
    FILE *f = fopen(TEST_FILE, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

static char* read_test_file(void) {
    FILE *f = fopen(TEST_FILE, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc((size_t)size + 1);
    if (content) {
        size_t read = fread(content, 1, (size_t)size, f);
        content[read] = '\0';
    }
    fclose(f);
    return content;
}

static void cleanup_test_file(void) {
    unlink(TEST_FILE);
}

static void init_test_state(ConversationState *state) {
    memset(state, 0, sizeof(ConversationState));
    state->working_dir = strdup("/tmp");
}

static void cleanup_test_state(ConversationState *state) {
    if (state->working_dir) {
        free(state->working_dir);
        state->working_dir = NULL;
    }
}

// Assertion helpers
static void assert_true(const char *test_name, int condition, const char *msg) {
    if (condition) {
        printf("%s  ✓ %s%s\n", COLOR_GREEN, test_name, COLOR_RESET);
    } else {
        printf("%s  ✗ %s: %s%s\n", COLOR_RED, test_name, msg, COLOR_RESET);
        exit(1);
    }
}

static void assert_json_string_equals(const char *test_name, cJSON *json, const char *key, const char *expected) {
    cJSON *item = cJSON_GetObjectItem(json, key);
    if (!item || !cJSON_IsString(item)) {
        printf("%s  ✗ %s: key '%s' not found or not a string%s\n", COLOR_RED, test_name, key, COLOR_RESET);
        exit(1);
    }
    if (strcmp(item->valuestring, expected) != 0) {
        printf("%s  ✗ %s: expected '%s', got '%s'%s\n", COLOR_RED, test_name, expected, item->valuestring, COLOR_RESET);
        exit(1);
    }
    printf("%s  ✓ %s%s\n", COLOR_GREEN, test_name, COLOR_RESET);
}

static void assert_json_number_equals(const char *test_name, cJSON *json, const char *key, double expected) {
    cJSON *item = cJSON_GetObjectItem(json, key);
    if (!item || !cJSON_IsNumber(item)) {
        printf("%s  ✗ %s: key '%s' not found or not a number%s\n", COLOR_RED, test_name, key, COLOR_RESET);
        exit(1);
    }
    if (item->valuedouble != expected) {
        printf("%s  ✗ %s: expected %.0f, got %.0f%s\n", COLOR_RED, test_name, expected, item->valuedouble, COLOR_RESET);
        exit(1);
    }
    printf("%s  ✓ %s%s\n", COLOR_GREEN, test_name, COLOR_RESET);
}

// ===== Test Cases =====

static void test_capture_group_swap(void) {
    printf("\n%s[Test: Capture Group Swap (firstname lastname -> lastname, firstname)]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("John Doe\nJane Smith\nBob Johnson");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "([A-Z][a-z]+) ([A-Z][a-z]+)");
    cJSON_AddStringToObject(params, "new_string", "\\2, \\1");
    cJSON_AddBoolToObject(params, "use_regex", 1);
    cJSON_AddBoolToObject(params, "replace_all", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");
    assert_json_number_equals("Replaces 3 occurrences", result, "replacements", 3);

    // Verify content
    char *content = read_test_file();
    int has_doe = (strstr(content, "Doe, John") != NULL);
    int has_smith = (strstr(content, "Smith, Jane") != NULL);
    int has_johnson = (strstr(content, "Johnson, Bob") != NULL);
    free(content);

    assert_true("Contains 'Doe, John'", has_doe, "name swap failed");
    assert_true("Contains 'Smith, Jane'", has_smith, "name swap failed");
    assert_true("Contains 'Johnson, Bob'", has_johnson, "name swap failed");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_capture_group_reformat_date(void) {
    printf("\n%s[Test: Capture Group Reformat Date (MM/DD/YYYY -> YYYY-MM-DD)]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("Date: 12/25/2023\nExpiry: 01/01/2024");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "([0-9]{2})/([0-9]{2})/([0-9]{4})");
    cJSON_AddStringToObject(params, "new_string", "\\3-\\1-\\2");
    cJSON_AddBoolToObject(params, "use_regex", 1);
    cJSON_AddBoolToObject(params, "replace_all", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");
    assert_json_number_equals("Replaces 2 occurrences", result, "replacements", 2);

    // Verify content
    char *content = read_test_file();
    int has_first_date = (strstr(content, "2023-12-25") != NULL);
    int has_second_date = (strstr(content, "2024-01-01") != NULL);
    free(content);

    assert_true("Contains reformatted first date", has_first_date, "date reformat failed");
    assert_true("Contains reformatted second date", has_second_date, "date reformat failed");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_capture_group_extract_version(void) {
    printf("\n%s[Test: Capture Group Extract Version Parts]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("Version 3.14.159 released");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "Version ([0-9]+)\\.([0-9]+)\\.([0-9]+)");
    cJSON_AddStringToObject(params, "new_string", "Major: \\1, Minor: \\2, Patch: \\3");
    cJSON_AddBoolToObject(params, "use_regex", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");

    // Verify content
    char *content = read_test_file();
    int correct = (strstr(content, "Major: 3, Minor: 14, Patch: 159") != NULL);
    free(content);

    assert_true("Version parts extracted correctly", correct, "version extraction failed");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_regex_flag_case_insensitive(void) {
    printf("\n%s[Test: Regex Flag - Case Insensitive (REG_ICASE)]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("TODO: Fix bug\ntodo: Add test\nToDo: Update docs");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "todo:");
    cJSON_AddStringToObject(params, "new_string", "DONE:");
    cJSON_AddBoolToObject(params, "use_regex", 1);
    cJSON_AddBoolToObject(params, "replace_all", 1);
    cJSON_AddStringToObject(params, "regex_flags", "i");  // Case-insensitive

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");
    assert_json_number_equals("Replaces 3 occurrences", result, "replacements", 3);

    // Verify all variations were replaced
    char *content = read_test_file();
    int no_todo = (strstr(content, "TODO") == NULL && strstr(content, "todo") == NULL && strstr(content, "ToDo") == NULL);
    int has_done = (strstr(content, "DONE:") != NULL);
    free(content);

    assert_true("No TODO variations remain", no_todo, "case-insensitive replacement failed");
    assert_true("Has DONE markers", has_done, "replacement missing");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_regex_flag_multiline(void) {
    printf("\n%s[Test: Regex Flag - Multiline (REG_NEWLINE)]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("Start\nLine 1\nLine 2\nEnd");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "^Line");  // ^ should match start of each line
    cJSON_AddStringToObject(params, "new_string", ">>> Line");
    cJSON_AddBoolToObject(params, "use_regex", 1);
    cJSON_AddBoolToObject(params, "replace_all", 1);
    cJSON_AddStringToObject(params, "regex_flags", "m");  // Multiline mode

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");

    char *content = read_test_file();
    int has_marker1 = (strstr(content, ">>> Line 1") != NULL);
    int has_marker2 = (strstr(content, ">>> Line 2") != NULL);
    free(content);

    assert_true("Line 1 has marker", has_marker1, "multiline flag not working");
    assert_true("Line 2 has marker", has_marker2, "multiline flag not working");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_regex_flags_combined(void) {
    printf("\n%s[Test: Regex Flags - Combined (i + m)]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("START: Alpha\nstart: Beta\nStart: Gamma");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "^start:");
    cJSON_AddStringToObject(params, "new_string", "BEGIN:");
    cJSON_AddBoolToObject(params, "use_regex", 1);
    cJSON_AddBoolToObject(params, "replace_all", 1);
    cJSON_AddStringToObject(params, "regex_flags", "im");  // Both flags

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");
    assert_json_number_equals("Replaces 3 occurrences", result, "replacements", 3);

    char *content = read_test_file();
    int no_start = (strstr(content, "START:") == NULL && strstr(content, "start:") == NULL && strstr(content, "Start:") == NULL);
    free(content);

    assert_true("All start variations replaced", no_start, "combined flags not working");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_backreference_full_match(void) {
    printf("\n%s[Test: Backreference - Full Match (\\0)]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("Error: Something went wrong\nWarning: Check this");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "(Error|Warning): ([^\n]+)");
    cJSON_AddStringToObject(params, "new_string", "[\\1] \\2 (full: \\0)");
    cJSON_AddBoolToObject(params, "use_regex", 1);
    cJSON_AddBoolToObject(params, "replace_all", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");

    char *content = read_test_file();
    int has_error = (strstr(content, "[Error] Something went wrong (full: Error: Something went wrong)") != NULL);
    int has_warning = (strstr(content, "[Warning] Check this (full: Warning: Check this)") != NULL);
    free(content);

    assert_true("Error line formatted correctly", has_error, "full match backreference failed");
    assert_true("Warning line formatted correctly", has_warning, "full match backreference failed");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_escaped_backslash_in_replacement(void) {
    printf("\n%s[Test: Escaped Backslash in Replacement]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("path/to/file");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "/");
    cJSON_AddStringToObject(params, "new_string", "\\\\");  // Should produce single backslash
    cJSON_AddBoolToObject(params, "use_regex", 1);
    cJSON_AddBoolToObject(params, "replace_all", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");

    char *content = read_test_file();
    int has_backslashes = (strstr(content, "path\\to\\file") != NULL);
    free(content);

    assert_true("Forward slashes replaced with backslashes", has_backslashes, "escaped backslash failed");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

static void test_multiple_same_capture_group(void) {
    printf("\n%s[Test: Multiple References to Same Capture Group]%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_file("foo bar baz");

    ConversationState state;
    init_test_state(&state);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", TEST_FILE);
    cJSON_AddStringToObject(params, "old_string", "(foo)");
    cJSON_AddStringToObject(params, "new_string", "\\1-\\1-\\1");  // Repeat captured group
    cJSON_AddBoolToObject(params, "use_regex", 1);

    cJSON *result = tool_edit(params, &state);

    assert_json_string_equals("Returns success status", result, "status", "success");

    char *content = read_test_file();
    int correct = (strstr(content, "foo-foo-foo") != NULL);
    free(content);

    assert_true("Capture group repeated correctly", correct, "multiple references failed");

    cJSON_Delete(params);
    cJSON_Delete(result);
    cleanup_test_file();
    cleanup_test_state(&state);
}

// ===== Main Test Runner =====

int main(void) {
    printf("\n%s========================================%s\n", COLOR_YELLOW, COLOR_RESET);
    printf("%sEdit Tool Regex Enhancement Tests%s\n", COLOR_YELLOW, COLOR_RESET);
    printf("%s========================================%s\n", COLOR_YELLOW, COLOR_RESET);

    test_capture_group_swap();
    test_capture_group_reformat_date();
    test_capture_group_extract_version();
    test_regex_flag_case_insensitive();
    test_regex_flag_multiline();
    test_regex_flags_combined();
    test_backreference_full_match();
    test_escaped_backslash_in_replacement();
    test_multiple_same_capture_group();

    printf("\n%s========================================%s\n", COLOR_GREEN, COLOR_RESET);
    printf("%sAll tests passed! ✓%s\n", COLOR_GREEN, COLOR_RESET);
    printf("%s========================================%s\n\n", COLOR_GREEN, COLOR_RESET);

    return 0;
}
