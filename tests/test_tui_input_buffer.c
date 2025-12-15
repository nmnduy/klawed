/*
 * test_tui_input_buffer.c - Unit tests for TUI input buffer dynamic resizing
 * Tests the new dynamic buffer expansion when loading large history entries
 *
 * This test focuses on the buffer expansion logic without full TUI dependencies
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Test framework colors (using different names to avoid ncurses conflicts)
#define TEST_COLOR_RESET "\033[0m"
#define TEST_COLOR_GREEN "\033[32m"
#define TEST_COLOR_RED "\033[31m"
#define TEST_COLOR_YELLOW "\033[33m"
#define TEST_COLOR_CYAN "\033[36m"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Test utilities
static void print_test_result(const char *test_name, int passed) {
    tests_run++;
    if (passed) {
        tests_passed++;
        printf(TEST_COLOR_GREEN "✓ PASS" TEST_COLOR_RESET " %s\n", test_name);
    } else {
        tests_failed++;
        printf(TEST_COLOR_RED "✗ FAIL" TEST_COLOR_RESET " %s\n", test_name);
    }
}

static void print_summary(void) {
    printf("\n" TEST_COLOR_CYAN "Test Summary:" TEST_COLOR_RESET "\n");
    printf("Tests run: %d\n", tests_run);
    printf(TEST_COLOR_GREEN "Tests passed: %d\n" TEST_COLOR_RESET, tests_passed);
    if (tests_failed > 0) {
        printf(TEST_COLOR_RED "Tests failed: %d\n" TEST_COLOR_RESET, tests_failed);
    } else {
        printf(TEST_COLOR_GREEN "All tests passed!\n" TEST_COLOR_RESET);
    }
}

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("%s[FAIL] %s:%d: Assertion failed: %s%s\n", TEST_COLOR_RED, __FILE__, __LINE__, #cond, TEST_COLOR_RESET); \
        return 0; \
    } \
} while(0)

// Simplified input buffer structure for testing
struct TestInputBuffer {
    char *buffer;
    int length;
    int capacity;
};

// Mock implementations for testing
static int mock_realloc_success = 1; // Control realloc behavior
static size_t mock_realloc_calls = 0;
static size_t last_realloc_size = 0;

// Mock realloc for testing
static void* mock_realloc(void *ptr, size_t size) {
    mock_realloc_calls++;
    last_realloc_size = size;

    if (!mock_realloc_success) {
        return NULL;
    }

    return realloc(ptr, size);
}

// Helper to create a test input buffer
static struct TestInputBuffer* create_test_input_buffer(int initial_capacity) {
    struct TestInputBuffer *input = calloc(1, sizeof(struct TestInputBuffer));
    if (!input) return NULL;

    input->buffer = malloc((size_t)initial_capacity);
    if (!input->buffer) {
        free(input);
        return NULL;
    }

    input->capacity = initial_capacity;
    input->length = 0;
    input->buffer[0] = '\0';

    return input;
}

static int test_input_buffer_expansion_success(void) {
    printf("\n%s[TEST] test_input_buffer_expansion_success%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // Create a small input buffer
    struct TestInputBuffer *input = create_test_input_buffer(10);
    ASSERT(input != NULL);

    // Simulate loading a large history entry
    const char *large_history = "This is a very long history entry that exceeds the initial buffer capacity";
    size_t history_len = strlen(large_history);

    // Verify buffer is too small initially
    ASSERT(history_len >= (size_t)input->capacity);

    // Save the original buffer pointer
    char *original_buffer = input->buffer;

    // Simulate the expansion logic from tui.c
    if (history_len >= (size_t)input->capacity) {
        size_t new_capacity = history_len + 1024;  // Add some extra space
        char *new_buffer = mock_realloc(input->buffer, new_capacity);
        if (new_buffer) {
            input->buffer = new_buffer;
            input->capacity = (int)new_capacity;
            // LOG_DEBUG("[TUI] Expanded input buffer to %zu bytes for history entry", new_capacity);
        } else {
            // If realloc fails, truncate to current capacity
            // LOG_WARN("[TUI] Failed to expand input buffer, truncating history entry");
            history_len = (size_t)input->capacity - 1;
        }
    }

    // Verify buffer was expanded
    ASSERT((size_t)input->capacity >= history_len + 1024);
    // Note: Don't check if buffer pointer changed - realloc() may return the same
    // address if it can expand in place, especially on Ubuntu/GCC builds
    (void)original_buffer; // Mark as used to avoid unused variable warning

    // Simulate copying the history
    memcpy(input->buffer, large_history, history_len);
    input->buffer[history_len] = '\0';
    input->length = (int)history_len;

    // Verify the content
    ASSERT(strcmp(input->buffer, large_history) == 0);
    ASSERT(input->length == (int)history_len);

    free(input->buffer);
    free(input);

    return 1;
}

static int test_input_buffer_expansion_failure(void) {
    printf("\n%s[TEST] test_input_buffer_expansion_failure%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // Create a small input buffer
    struct TestInputBuffer *input = create_test_input_buffer(10);
    ASSERT(input != NULL);

    // Simulate realloc failure
    mock_realloc_success = 0;

    const char *large_history = "This is a very long history entry that exceeds the initial buffer capacity";
    size_t history_len = strlen(large_history);

    // Save original state
    char *original_buffer = input->buffer;
    int original_capacity = input->capacity;

    // Simulate the expansion logic with failure
    if (history_len >= (size_t)input->capacity) {
        size_t new_capacity = history_len + 1024;
        char *new_buffer = mock_realloc(input->buffer, new_capacity);
        if (new_buffer) {
            input->buffer = new_buffer;
            input->capacity = (int)new_capacity;
        } else {
            // If realloc fails, truncate to current capacity
            history_len = (size_t)input->capacity - 1;
        }
    }

    // Verify buffer was NOT expanded (due to mock failure)
    ASSERT(input->buffer == original_buffer);
    ASSERT(input->capacity == original_capacity);
    ASSERT(history_len == (size_t)original_capacity - 1);

    // Simulate copying truncated content
    memcpy(input->buffer, large_history, history_len);
    input->buffer[history_len] = '\0';
    input->length = (int)history_len;

    // Verify truncated content
    ASSERT(strncmp(input->buffer, large_history, history_len) == 0);
    ASSERT(input->length == original_capacity - 1);

    mock_realloc_success = 1; // Reset for other tests
    free(input->buffer);
    free(input);

    return 1;
}

static int test_input_buffer_no_expansion_needed(void) {
    printf("\n%s[TEST] test_input_buffer_no_expansion_needed%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // Create a sufficiently large input buffer
    struct TestInputBuffer *input = create_test_input_buffer(100);
    ASSERT(input != NULL);

    const char *small_history = "Short history";
    size_t history_len = strlen(small_history);

    // Save original state
    char *original_buffer = input->buffer;
    int original_capacity = input->capacity;

    // Simulate the expansion logic
    if (history_len >= (size_t)input->capacity) {
        size_t new_capacity = history_len + 1024;
        char *new_buffer = mock_realloc(input->buffer, new_capacity);
        if (new_buffer) {
            input->buffer = new_buffer;
            input->capacity = (int)new_capacity;
        } else {
            history_len = (size_t)input->capacity - 1;
        }
    }

    // Verify buffer was NOT expanded (not needed)
    ASSERT(input->buffer == original_buffer);
    ASSERT(input->capacity == original_capacity);

    // Simulate copying the content
    memcpy(input->buffer, small_history, history_len);
    input->buffer[history_len] = '\0';
    input->length = (int)history_len;

    // Verify the content
    ASSERT(strcmp(input->buffer, small_history) == 0);
    ASSERT(input->length == (int)history_len);

    free(input->buffer);
    free(input);

    return 1;
}

static int test_input_buffer_edge_cases(void) {
    printf("\n%s[TEST] test_input_buffer_edge_cases%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // Test with exact capacity
    struct TestInputBuffer *input = create_test_input_buffer(20);
    ASSERT(input != NULL);

    const char *exact_fit = "Exactly 19 chars!"; // 19 chars + null terminator = 20
    size_t history_len = strlen(exact_fit);

    // Should not need expansion
    ASSERT(history_len < (size_t)input->capacity);

    // Simulate copying
    memcpy(input->buffer, exact_fit, history_len);
    input->buffer[history_len] = '\0';
    input->length = (int)history_len;

    ASSERT(strcmp(input->buffer, exact_fit) == 0);

    free(input->buffer);
    free(input);

    // Test with empty string
    input = create_test_input_buffer(10);
    ASSERT(input != NULL);

    const char *empty = "";
    history_len = strlen(empty);

    // Should not need expansion
    ASSERT(history_len < (size_t)input->capacity);

    memcpy(input->buffer, empty, history_len);
    input->buffer[history_len] = '\0';
    input->length = (int)history_len;

    ASSERT(strcmp(input->buffer, empty) == 0);
    ASSERT(input->length == 0);

    free(input->buffer);
    free(input);

    return 1;
}

static int test_input_buffer_multiple_expansions(void) {
    printf("\n%s[TEST] test_input_buffer_multiple_expansions%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    struct TestInputBuffer *input = create_test_input_buffer(10);
    ASSERT(input != NULL);

    // First expansion with minimal padding
    const char *first_history = "First long history entry";
    size_t first_len = strlen(first_history);

    if (first_len >= (size_t)input->capacity) {
        size_t new_capacity = first_len + 10;  // Small padding to force second expansion
        char *new_buffer = mock_realloc(input->buffer, new_capacity);
        if (new_buffer) {
            input->buffer = new_buffer;
            input->capacity = (int)new_capacity;
        } else {
            first_len = (size_t)input->capacity - 1;
        }
    }

    memcpy(input->buffer, first_history, first_len);
    input->buffer[first_len] = '\0';
    input->length = (int)first_len;

    size_t first_capacity = (size_t)input->capacity;

    // Second expansion with larger content
    const char *second_history = "This is a much longer history entry that should definitely trigger another expansion because the first expansion only added minimal padding.";
    size_t second_len = strlen(second_history);

    if (second_len >= (size_t)input->capacity) {
        size_t new_capacity = second_len + 1024;
        char *new_buffer = mock_realloc(input->buffer, new_capacity);
        if (new_buffer) {
            input->buffer = new_buffer;
            input->capacity = (int)new_capacity;
        } else {
            second_len = (size_t)input->capacity - 1;
        }
    }

    memcpy(input->buffer, second_history, second_len);
    input->buffer[second_len] = '\0';
    input->length = (int)second_len;

    // Verify second expansion was larger than first
    ASSERT((size_t)input->capacity > first_capacity);
    ASSERT(strcmp(input->buffer, second_history) == 0);

    free(input->buffer);
    free(input);

    return 1;
}

int main(void) {
    printf("%sRunning TUI Input Buffer Tests%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    print_test_result("test_input_buffer_expansion_success", test_input_buffer_expansion_success());
    print_test_result("test_input_buffer_expansion_failure", test_input_buffer_expansion_failure());
    print_test_result("test_input_buffer_no_expansion_needed", test_input_buffer_no_expansion_needed());
    print_test_result("test_input_buffer_edge_cases", test_input_buffer_edge_cases());
    print_test_result("test_input_buffer_multiple_expansions", test_input_buffer_multiple_expansions());

    print_summary();

    return (tests_failed == 0) ? 0 : 1;
}
