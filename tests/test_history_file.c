/*
 * test_history_file.c - Unit tests for history file functionality
 * Tests newline escaping/unescaping and dynamic buffer resizing
 */

#define TEST_BUILD 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include "../src/history_file.h"

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

// Test utilities
static void print_test_result(const char *test_name, int passed) {
    tests_run++;
    if (passed) {
        tests_passed++;
        printf(COLOR_GREEN "✓ PASS" COLOR_RESET " %s\n", test_name);
    } else {
        tests_failed++;
        printf(COLOR_RED "✗ FAIL" COLOR_RESET " %s\n", test_name);
    }
}

static void print_summary(void) {
    printf("\n" COLOR_CYAN "Test Summary:" COLOR_RESET "\n");
    printf("Tests run: %d\n", tests_run);
    printf(COLOR_GREEN "Tests passed: %d\n" COLOR_RESET, tests_passed);
    if (tests_failed > 0) {
        printf(COLOR_RED "Tests failed: %d\n" COLOR_RESET, tests_failed);
    } else {
        printf(COLOR_GREEN "All tests passed!\n" COLOR_RESET);
    }
}

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("%s[FAIL] %s:%d: Assertion failed: %s%s\n", COLOR_RED, __FILE__, __LINE__, #cond, COLOR_RESET); \
        return 0; \
    } \
} while(0)

static int test_escape_newlines_basic(void) {
    printf("\n%s[TEST] test_escape_newlines_basic%s\n", COLOR_CYAN, COLOR_RESET);

    // Test basic escaping
    char *escaped = escape_newlines("hello\nworld");
    ASSERT(escaped != NULL);
    ASSERT(strcmp(escaped, "hello\\nworld") == 0);
    free(escaped);

    // Test multiple newlines
    escaped = escape_newlines("line1\nline2\nline3");
    ASSERT(escaped != NULL);
    ASSERT(strcmp(escaped, "line1\\nline2\\nline3") == 0);
    free(escaped);

    // Test no newlines
    escaped = escape_newlines("no newlines here");
    ASSERT(escaped != NULL);
    ASSERT(strcmp(escaped, "no newlines here") == 0);
    free(escaped);

    // Test empty string
    escaped = escape_newlines("");
    ASSERT(escaped != NULL);
    ASSERT(strcmp(escaped, "") == 0);
    free(escaped);

    // Test NULL input
    escaped = escape_newlines(NULL);
    ASSERT(escaped == NULL);

    return 1;
}

static int test_unescape_newlines_basic(void) {
    printf("\n%s[TEST] test_unescape_newlines_basic%s\n", COLOR_CYAN, COLOR_RESET);

    // Test basic unescaping
    char *unescaped = unescape_newlines("hello\\nworld");
    ASSERT(unescaped != NULL);
    ASSERT(strcmp(unescaped, "hello\nworld") == 0);
    free(unescaped);

    // Test multiple escaped sequences
    unescaped = unescape_newlines("line1\\nline2\\nline3");
    ASSERT(unescaped != NULL);
    ASSERT(strcmp(unescaped, "line1\nline2\nline3") == 0);
    free(unescaped);

    // Test no escape sequences
    unescaped = unescape_newlines("no escapes here");
    ASSERT(unescaped != NULL);
    ASSERT(strcmp(unescaped, "no escapes here") == 0);
    free(unescaped);

    // Test empty string
    unescaped = unescape_newlines("");
    ASSERT(unescaped != NULL);
    ASSERT(strcmp(unescaped, "") == 0);
    free(unescaped);

    // Test NULL input
    unescaped = unescape_newlines(NULL);
    ASSERT(unescaped == NULL);

    return 1;
}

static int test_escape_unescape_roundtrip(void) {
    printf("\n%s[TEST] test_escape_unescape_roundtrip%s\n", COLOR_CYAN, COLOR_RESET);

    const char *test_cases[] = {
        "single line",
        "line1\nline2",
        "line1\nline2\nline3",
        "text with\nmultiple\nnewlines\nin it",
        "",
        "\n",
        "\n\n\n",
        "text\n",
        "\ntext",
        "text\n\ntext",
        NULL
    };

    for (int i = 0; test_cases[i] != NULL; i++) {
        char *escaped = escape_newlines(test_cases[i]);
        ASSERT(escaped != NULL);

        char *unescaped = unescape_newlines(escaped);
        ASSERT(unescaped != NULL);

        ASSERT(strcmp(unescaped, test_cases[i]) == 0);

        free(escaped);
        free(unescaped);
    }

    return 1;
}

static int test_history_file_append_with_newlines(void) {
    printf("\n%s[TEST] test_history_file_append_with_newlines%s\n", COLOR_CYAN, COLOR_RESET);

    // Create temporary file for testing
    char temp_path[] = "/tmp/test_history_XXXXXX";
    int fd = mkstemp(temp_path);
    ASSERT(fd != -1);
    close(fd);

    HistoryFile *hf = history_file_open(temp_path);
    ASSERT(hf != NULL);

    // Test appending text with newlines
    const char *text_with_newlines = "line1\nline2\nline3";
    int result = history_file_append(hf, text_with_newlines);
    ASSERT(result == 0);

    // Close and reopen to verify persistence
    history_file_close(hf);
    hf = history_file_open(temp_path);
    ASSERT(hf != NULL);

    // Load and verify the escaped content
    int count = 0;
    char **lines = history_file_load_recent(hf, 10, &count);
    ASSERT(lines != NULL);
    ASSERT(count == 1);
    ASSERT(strcmp(lines[0], text_with_newlines) == 0);

    // Cleanup
    for (int i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
    history_file_close(hf);
    unlink(temp_path);

    return 1;
}

static int test_history_file_load_recent_with_escaped_newlines(void) {
    printf("\n%s[TEST] test_history_file_load_recent_with_escaped_newlines%s\n", COLOR_CYAN, COLOR_RESET);

    // Create temporary file with escaped newlines
    char temp_path[] = "/tmp/test_history_escaped_XXXXXX";
    int fd = mkstemp(temp_path);
    ASSERT(fd != -1);

    // Write escaped content directly
    FILE *fp = fdopen(fd, "w");
    ASSERT(fp != NULL);
    fprintf(fp, "line1\\nline2\\nline3\n");
    fprintf(fp, "single line\n");
    fclose(fp);

    HistoryFile *hf = history_file_open(temp_path);
    ASSERT(hf != NULL);

    // Load and verify unescaped content
    int count = 0;
    char **lines = history_file_load_recent(hf, 10, &count);
    ASSERT(lines != NULL);
    ASSERT(count == 2);
    ASSERT(strcmp(lines[0], "line1\nline2\nline3") == 0);
    ASSERT(strcmp(lines[1], "single line") == 0);

    // Cleanup
    for (int i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
    history_file_close(hf);
    unlink(temp_path);

    return 1;
}

static int test_history_file_edge_cases(void) {
    printf("\n%s[TEST] test_history_file_edge_cases%s\n", COLOR_CYAN, COLOR_RESET);

    char temp_path[] = "/tmp/test_history_edge_XXXXXX";
    int fd = mkstemp(temp_path);
    ASSERT(fd != -1);
    close(fd);

    HistoryFile *hf = history_file_open(temp_path);
    ASSERT(hf != NULL);

    // Test empty string (should be skipped)
    int result = history_file_append(hf, "");
    ASSERT(result == 0);  // Should succeed but not write anything

    // Test NULL text
    result = history_file_append(hf, NULL);
    ASSERT(result == 0);  // Should succeed but not write anything

    // Test valid text
    result = history_file_append(hf, "valid text");
    ASSERT(result == 0);

    // Verify only valid text was written
    int count = 0;
    char **lines = history_file_load_recent(hf, 10, &count);
    ASSERT(lines != NULL);
    ASSERT(count == 1);
    ASSERT(strcmp(lines[0], "valid text") == 0);

    // Cleanup
    for (int i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
    history_file_close(hf);
    unlink(temp_path);

    return 1;
}

int main(void) {
    printf("%sRunning History File Tests%s\n", COLOR_CYAN, COLOR_RESET);

    print_test_result("test_escape_newlines_basic", test_escape_newlines_basic());
    print_test_result("test_unescape_newlines_basic", test_unescape_newlines_basic());
    print_test_result("test_escape_unescape_roundtrip", test_escape_unescape_roundtrip());
    print_test_result("test_history_file_append_with_newlines", test_history_file_append_with_newlines());
    print_test_result("test_history_file_load_recent_with_escaped_newlines", test_history_file_load_recent_with_escaped_newlines());
    print_test_result("test_history_file_edge_cases", test_history_file_edge_cases());

    print_summary();

    return (tests_failed == 0) ? 0 : 1;
}
