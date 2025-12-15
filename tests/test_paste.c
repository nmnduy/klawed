/**
 * test_paste.c
 *
 * Unit tests for paste handling functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/paste_handler.h"

// Test colors
#define GREEN "\033[32m"
#define RED "\033[31m"
#define YELLOW "\033[33m"
#define RESET "\033[0m"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, msg) do { \
    if (condition) { \
        printf(GREEN "✓" RESET " %s\n", msg); \
        tests_passed++; \
    } else { \
        printf(RED "✗" RESET " %s\n", msg); \
        tests_failed++; \
    } \
} while(0)

static void test_paste_state_init(void) {
    printf("\n" YELLOW "Testing paste state initialization..." RESET "\n");

    PasteState *state = paste_state_init();
    TEST_ASSERT(state != NULL, "paste_state_init() returns non-NULL");
    TEST_ASSERT(state->buffer != NULL, "Buffer is allocated");
    TEST_ASSERT(state->buffer_capacity == PASTE_BUFFER_SIZE, "Buffer capacity is correct");
    TEST_ASSERT(state->buffer_size == 0, "Buffer size starts at 0");
    TEST_ASSERT(state->in_paste == 0, "Not in paste mode initially");

    paste_state_free(state);
}

static void test_paste_buffer_add_char(void) {
    printf("\n" YELLOW "Testing character buffering..." RESET "\n");

    PasteState *state = paste_state_init();

    // Add single character
    int result = paste_buffer_add_char(state, 'A');
    TEST_ASSERT(result == 0, "Add character succeeds");
    TEST_ASSERT(state->buffer_size == 1, "Buffer size incremented");
    TEST_ASSERT(state->buffer[0] == 'A', "Character stored correctly");

    // Add multiple characters
    const char *text = "Hello World";
    for (size_t i = 0; i < strlen(text); i++) {
        paste_buffer_add_char(state, text[i]);
    }
    TEST_ASSERT(state->buffer_size == strlen(text) + 1, "Multiple characters buffered");
    TEST_ASSERT(strncmp(state->buffer, "AHello World", 12) == 0, "Buffer content correct");

    paste_state_free(state);
}

static void test_paste_buffer_overflow(void) {
    printf("\n" YELLOW "Testing buffer overflow protection..." RESET "\n");

    PasteState *state = paste_state_init();

    // Fill buffer to capacity
    for (size_t i = 0; i < PASTE_BUFFER_SIZE - 1; i++) {
        paste_buffer_add_char(state, 'X');
    }
    TEST_ASSERT(state->buffer_size == PASTE_BUFFER_SIZE - 1, "Buffer filled to capacity");

    // Try to exceed capacity
    int result = paste_buffer_add_char(state, 'Y');
    TEST_ASSERT(result == -1, "Buffer overflow detected");
    TEST_ASSERT(state->buffer_size == PASTE_BUFFER_SIZE - 1, "Buffer size unchanged");

    paste_state_free(state);
}

static void test_paste_sanitize_control_chars(void) {
    printf("\n" YELLOW "Testing control character removal..." RESET "\n");

    char buffer[256];
    strcpy(buffer, "Hello\x01\x02\x03World");

    PasteSanitizeOptions opts = {
        .remove_control_chars = 1,
        .normalize_newlines = 0,
        .trim_whitespace = 0,
        .collapse_multiple_newlines = 0
    };

    size_t new_len = paste_sanitize(buffer, strlen(buffer), &opts);
    TEST_ASSERT(new_len == 10, "Control characters removed");
    TEST_ASSERT(strcmp(buffer, "HelloWorld") == 0, "Result is correct");
}

static void test_paste_sanitize_newlines(void) {
    printf("\n" YELLOW "Testing newline normalization..." RESET "\n");

    char buffer[256];
    strcpy(buffer, "Line1\r\nLine2\rLine3\nLine4");

    PasteSanitizeOptions opts = {
        .remove_control_chars = 0,
        .normalize_newlines = 1,
        .trim_whitespace = 0,
        .collapse_multiple_newlines = 0
    };

    paste_sanitize(buffer, strlen(buffer), &opts);
    TEST_ASSERT(strcmp(buffer, "Line1\nLine2\nLine3\nLine4") == 0, "Newlines normalized");
}

static void test_paste_sanitize_whitespace(void) {
    printf("\n" YELLOW "Testing whitespace trimming..." RESET "\n");

    char buffer[256];
    strcpy(buffer, "   Hello World   ");

    PasteSanitizeOptions opts = {
        .remove_control_chars = 0,
        .normalize_newlines = 0,
        .trim_whitespace = 1,
        .collapse_multiple_newlines = 0
    };

    paste_sanitize(buffer, strlen(buffer), &opts);
    TEST_ASSERT(strcmp(buffer, "Hello World") == 0, "Whitespace trimmed");
}

static void test_paste_sanitize_multiple_newlines(void) {
    printf("\n" YELLOW "Testing multiple newline collapse..." RESET "\n");

    char buffer[256];
    strcpy(buffer, "Line1\n\n\n\n\nLine2");

    PasteSanitizeOptions opts = {
        .remove_control_chars = 0,
        .normalize_newlines = 0,
        .trim_whitespace = 0,
        .collapse_multiple_newlines = 1
    };

    paste_sanitize(buffer, strlen(buffer), &opts);
    TEST_ASSERT(strcmp(buffer, "Line1\n\nLine2") == 0, "Multiple newlines collapsed to 2");
}

static void test_paste_get_preview(void) {
    printf("\n" YELLOW "Testing preview generation..." RESET "\n");

    const char *content = "This is a very long string that should be truncated in the preview";
    char *preview = paste_get_preview(content, strlen(content), 20);

    TEST_ASSERT(preview != NULL, "Preview generated");
    TEST_ASSERT(strlen(preview) == 23, "Preview length correct (20 + '...')");
    TEST_ASSERT(strstr(preview, "...") != NULL, "Preview contains ellipsis");

    free(preview);

    // Test short content (no truncation)
    const char *short_content = "Short";
    preview = paste_get_preview(short_content, strlen(short_content), 20);
    TEST_ASSERT(strcmp(preview, "Short") == 0, "Short content not truncated");

    free(preview);
}

static void test_paste_state_reset(void) {
    printf("\n" YELLOW "Testing state reset..." RESET "\n");

    PasteState *state = paste_state_init();

    // Add some data
    state->in_paste = 1;
    paste_buffer_add_char(state, 'A');
    paste_buffer_add_char(state, 'B');
    paste_buffer_add_char(state, 'C');

    TEST_ASSERT(state->buffer_size == 3, "Buffer has content before reset");
    TEST_ASSERT(state->in_paste == 1, "In paste mode before reset");

    // Reset
    paste_state_reset(state);

    TEST_ASSERT(state->buffer_size == 0, "Buffer cleared after reset");
    TEST_ASSERT(state->in_paste == 0, "Not in paste mode after reset");

    paste_state_free(state);
}

static void test_bracketed_paste_detection(void) {
    printf("\n" YELLOW "Testing bracketed paste sequence detection..." RESET "\n");

    // Test start sequence
    const char *start_seq = "\033[200~";
    int result = check_paste_start_sequence(start_seq, 6);
    TEST_ASSERT(result == 6, "Paste start sequence detected");

    // Test end sequence
    const char *end_seq = "\033[201~";
    result = check_paste_end_sequence(end_seq, 6);
    TEST_ASSERT(result == 6, "Paste end sequence detected");

    // Test invalid sequence
    const char *invalid = "\033[999~";
    result = check_paste_start_sequence(invalid, 6);
    TEST_ASSERT(result == 0, "Invalid sequence rejected");
}

static void test_paste_get_content(void) {
    printf("\n" YELLOW "Testing content retrieval..." RESET "\n");

    PasteState *state = paste_state_init();

    const char *text = "Test content";
    for (size_t i = 0; i < strlen(text); i++) {
        paste_buffer_add_char(state, text[i]);
    }

    size_t len = 0;
    const char *content = paste_get_content(state, &len);

    TEST_ASSERT(content != NULL, "Content retrieved");
    TEST_ASSERT(len == strlen(text), "Length correct");
    TEST_ASSERT(strcmp(content, text) == 0, "Content matches");
    TEST_ASSERT(content[len] == '\0', "Content is null-terminated");

    paste_state_free(state);
}

static void test_full_sanitization(void) {
    printf("\n" YELLOW "Testing full sanitization pipeline..." RESET "\n");

    char buffer[256];
    strcpy(buffer, "  \x01Line1\r\n\n\n\nLine2\x02  \n\n");

    PasteSanitizeOptions opts = {
        .remove_control_chars = 1,
        .normalize_newlines = 1,
        .trim_whitespace = 1,
        .collapse_multiple_newlines = 1
    };

    size_t new_len = paste_sanitize(buffer, strlen(buffer), &opts);
    TEST_ASSERT(new_len > 0, "Sanitization produced output");
    TEST_ASSERT(strcmp(buffer, "Line1\n\nLine2") == 0, "Full sanitization correct");
}

int main(void) {
    printf(YELLOW "\n========================================\n" RESET);
    printf(YELLOW "  Paste Handler Unit Tests\n" RESET);
    printf(YELLOW "========================================\n" RESET);

    test_paste_state_init();
    test_paste_buffer_add_char();
    test_paste_buffer_overflow();
    test_paste_sanitize_control_chars();
    test_paste_sanitize_newlines();
    test_paste_sanitize_whitespace();
    test_paste_sanitize_multiple_newlines();
    test_paste_get_preview();
    test_paste_state_reset();
    test_bracketed_paste_detection();
    test_paste_get_content();
    test_full_sanitization();

    printf(YELLOW "\n========================================\n" RESET);
    printf("Tests passed: " GREEN "%d" RESET "\n", tests_passed);
    printf("Tests failed: " RED "%d" RESET "\n", tests_failed);
    printf(YELLOW "========================================\n\n" RESET);

    return (tests_failed == 0) ? 0 : 1;
}
