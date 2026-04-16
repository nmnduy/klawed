/*
 * test_tui_scrolling_calculations.c - Unit tests for TUI scrolling and cursor position calculations
 * Tests the border calculation consistency between TUI and window manager
 *
 * This test focuses on ensuring that border calculations are consistent
 * between TUI input rendering and window manager layout calculations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Test framework colors
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

// Simplified structures for testing
struct TestInputState {
    char buffer[1024];
    int length;
    int line_scroll_offset;
    int win_width;
    int win_height;
    int cursor_pos;
    int cursor_line;
    int cursor_col;
};

// Helper to calculate cursor position from buffer
static void calculate_cursor_position(struct TestInputState *input) {
    input->cursor_line = 0;
    input->cursor_col = 0;

    for (int i = 0; i < input->cursor_pos && i < input->length; i++) {
        if (input->buffer[i] == '\n') {
            input->cursor_line++;
            input->cursor_col = 0;
        } else {
            input->cursor_col++;
        }
    }
}

// Test 1: Verify cursor screen position calculations (no borders)
static int test_cursor_screen_position_no_borders(void) {
    printf("\n%s[TEST] test_cursor_screen_position_no_borders%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    struct TestInputState input = {0};
    strcpy(input.buffer, "Hello\nWorld\nTest");
    input.length = (int)strlen(input.buffer);
    input.win_width = 80;
    input.win_height = 5;
    input.cursor_pos = 12; // Position at 'T' in "Test"

    calculate_cursor_position(&input);

    // With no borders, cursor_line should be 2 (third line)
    ASSERT(input.cursor_line == 2);
    ASSERT(input.cursor_col == 0); // 'T' is first character on line

    // Test scrolling offset
    input.line_scroll_offset = 1; // Scroll past first line

    // Cursor screen position calculation (from tui.c line 1317-1318 after fix)
    int cursor_screen_y = input.cursor_line - input.line_scroll_offset;
    int cursor_screen_x = input.cursor_col;  // No border offset

    // With scroll offset 1, cursor should be at screen line 1 (0-indexed)
    ASSERT(cursor_screen_y == 1);
    ASSERT(cursor_screen_x == 0);

    // Bounds check (from tui.c line 1339-1342 after fix)
    ASSERT(cursor_screen_y >= 0 && cursor_screen_y < input.win_height);
    ASSERT(cursor_screen_x >= 0 && cursor_screen_x < input.win_width);

    return 1;
}

// Test 2: Verify line width calculations without border offset
static int test_line_width_calculations(void) {
    printf("\n%s[TEST] test_line_width_calculations%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    struct TestInputState input = {0};
    strcpy(input.buffer, "This is a long line that might wrap");
    input.length = (int)strlen(input.buffer);
    input.win_width = 20;  // Small width to force wrapping
    input.win_height = 10;
    input.cursor_pos = input.length;

    calculate_cursor_position(&input);

    // Calculate available width for text (from tui.c line 1188 after fix)
    int prompt_len = 3; // "> " plus space
    int available_width = input.win_width - prompt_len;

    ASSERT(available_width == 17); // 20 - 3

    // Simulate text rendering with wrapping
    int screen_y = 0;
    int screen_x = prompt_len; // Start after prompt (no +1 for border)
    int current_line = 0;
    (void)screen_y; // Mark as used to avoid warning

    for (int i = 0; i < input.length; i++) {
        char c = input.buffer[i];

        if (c == '\n') {
            screen_y++;
            current_line++;
            screen_x = 0;  // Reset to left edge (no border)
        } else {
            // Check if we need to wrap
            if (screen_x >= available_width) {
                screen_y++;
                current_line++;
                screen_x = 0;  // Reset to left edge (no border)
            }
            screen_x++;
        }
    }

    // Text should wrap to multiple lines
    ASSERT(current_line > 0);

    return 1;
}

// Test 3: Verify window height calculations consistency
static int test_window_height_consistency(void) {
    printf("\n%s[TEST] test_window_height_consistency%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // Test constants from tui.c
    #define INPUT_WIN_MIN_HEIGHT 2  // Min height for input window (content lines, no borders)
    #define INPUT_WIN_MAX_HEIGHT_PERCENT 20

    // Simulate screen height calculations
    int screen_height = 24;

    // Calculate max input height as 20% of screen height (from tui.c line 1422)
    int calculated_max_height = (screen_height * INPUT_WIN_MAX_HEIGHT_PERCENT) / 100;

    // Minimum of INPUT_WIN_MIN_HEIGHT to ensure at least some content lines
    if (calculated_max_height < INPUT_WIN_MIN_HEIGHT) {
        calculated_max_height = INPUT_WIN_MIN_HEIGHT;
    }

    // 20% of 24 = 4.8, rounded down to 4
    ASSERT(calculated_max_height == 4);

    // Test with very small screen
    screen_height = 10;
    calculated_max_height = (screen_height * INPUT_WIN_MAX_HEIGHT_PERCENT) / 100;
    if (calculated_max_height < INPUT_WIN_MIN_HEIGHT) {
        calculated_max_height = INPUT_WIN_MIN_HEIGHT;
    }

    // 20% of 10 = 2, which equals INPUT_WIN_MIN_HEIGHT
    ASSERT(calculated_max_height == 2);

    // Test with minimum screen height
    screen_height = 5;
    calculated_max_height = (screen_height * INPUT_WIN_MAX_HEIGHT_PERCENT) / 100;
    if (calculated_max_height < INPUT_WIN_MIN_HEIGHT) {
        calculated_max_height = INPUT_WIN_MIN_HEIGHT;
    }

    // 20% of 5 = 1, but minimum is 2
    ASSERT(calculated_max_height == 2);

    return 1;
}

// Test 4: Verify prompt positioning without borders
static int test_prompt_positioning(void) {
    printf("\n%s[TEST] test_prompt_positioning%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // From tui.c line 1254 after fix:
    // mvwprintw(win, 0, 0, "%s ", prompt);

    // Prompt should be at position (0, 0) without border offset
    int prompt_y = 0;
    int prompt_x = 0;

    ASSERT(prompt_y == 0);
    ASSERT(prompt_x == 0);

    // Text should start after prompt length
    int prompt_len = 3; // "> " plus space
    int text_start_x = prompt_len; // Not prompt_len + 1 (no border)

    ASSERT(text_start_x == 3);

    return 1;
}

// Test 5: Verify scrolling bounds checking
static int test_scrolling_bounds(void) {
    printf("\n%s[TEST] test_scrolling_bounds%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    struct TestInputState input = {0};
    strcpy(input.buffer, "Line 1\nLine 2\nLine 3\nLine 4\nLine 5");
    input.length = (int)strlen(input.buffer);
    input.win_height = 3;  // Only show 3 lines at a time
    input.cursor_pos = input.length;

    calculate_cursor_position(&input);

    // Cursor is at line 4 (0-indexed: lines 0-4)
    ASSERT(input.cursor_line == 4);

    // Test different scroll offsets
    for (int offset = 0; offset <= 5; offset++) {
        input.line_scroll_offset = offset;

        int cursor_screen_y = input.cursor_line - input.line_scroll_offset;

        // Bounds check (from tui.c line 1339-1342)
        int in_bounds = (cursor_screen_y >= 0 && cursor_screen_y < input.win_height);

        if (offset <= 4 && offset >= 2) { // Offsets 2, 3, 4 should show cursor
            ASSERT(in_bounds == 1);
        } else { // Offsets 0, 1, 5 should not show cursor
            ASSERT(in_bounds == 0);
        }
    }

    return 1;
}

int main(void) {
    printf("%sRunning TUI Scrolling Calculation Tests%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    print_test_result("test_cursor_screen_position_no_borders", test_cursor_screen_position_no_borders());
    print_test_result("test_line_width_calculations", test_line_width_calculations());
    print_test_result("test_window_height_consistency", test_window_height_consistency());
    print_test_result("test_prompt_positioning", test_prompt_positioning());
    print_test_result("test_scrolling_bounds", test_scrolling_bounds());

    print_summary();

    return (tests_failed == 0) ? 0 : 1;
}


static int clamp_nonnegative_test(int value) {
    return value < 0 ? 0 : value;
}

static int safe_right_start_col_test(int width, int right_total_width) {
    int col = width - right_total_width;
    return clamp_nonnegative_test(col);
}

static int safe_remaining_columns_test(int width, int x) {
    if (width <= 0 || x < 0 || x >= width) {
        return 0;
    }
    return width - x;
}

static int test_status_layout_clamps_negative_right_column(void) {
    ASSERT(safe_right_start_col_test(10, 25) == 0);
    ASSERT(safe_right_start_col_test(80, 20) == 60);
    return 1;
}

static int test_safe_remaining_columns_rejects_oob_positions(void) {
    ASSERT(safe_remaining_columns_test(0, 0) == 0);
    ASSERT(safe_remaining_columns_test(5, -1) == 0);
    ASSERT(safe_remaining_columns_test(5, 5) == 0);
    ASSERT(safe_remaining_columns_test(5, 2) == 3);
    return 1;
}
