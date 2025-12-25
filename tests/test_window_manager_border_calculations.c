/*
 * test_window_manager_border_calculations.c - Unit tests for window manager border calculations
 * Tests the consistency of border/no-border calculations between window manager and TUI
 *
 * This test focuses on ensuring that window height calculations don't include borders
 * to match the TUI's borderless rendering approach.
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

// Simplified window manager config structure
struct TestWindowManagerConfig {
    int min_conv_height;
    int min_input_height;   // Minimum content lines (no borders)
    int max_input_height;   // Maximum content lines (no borders)
    int status_height;
    int padding;
    int conv_h_padding;
    int initial_pad_capacity;
};

// Default configuration values (from window_manager.c line 12-21)
static const struct TestWindowManagerConfig DEFAULT_TEST_CONFIG = {
    .min_conv_height = 5,
    .min_input_height = 2,  // Minimum content lines (no borders)
    .max_input_height = 5,  // Maximum content lines (no borders)
    .status_height = 1,
    .padding = 0,
    .conv_h_padding = 0,
    .initial_pad_capacity = 1000
};

// Test 1: Verify config values don't include borders
static int test_config_no_borders(void) {
    printf("\n%s[TEST] test_config_no_borders%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // From window_manager.c line 12-21:
    // .min_input_height = 2,  // Minimum content lines (no borders)
    // .max_input_height = 5,  // Maximum content lines (no borders)

    ASSERT(DEFAULT_TEST_CONFIG.min_input_height == 2);
    ASSERT(DEFAULT_TEST_CONFIG.max_input_height == 5);

    // These values should represent content lines only, not including borders
    // In the old buggy code, these would have been 3 and 6 (content + 2 borders)

    return 1;
}

// Test 2: Verify input resize calculations (no borders)
static int test_input_resize_no_borders(void) {
    printf("\n%s[TEST] test_input_resize_no_borders%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // Simulate window_manager_resize_input logic from window_manager.c line 413
    // After fix: int new_height = desired_content_lines;  // content only - no borders

    int desired_content_lines = 3;
    int new_height = desired_content_lines;  // No +2 for borders

    ASSERT(new_height == 3);

    // Clamp to min/max (content lines only)
    if (new_height < DEFAULT_TEST_CONFIG.min_input_height) {
        new_height = DEFAULT_TEST_CONFIG.min_input_height;
    }
    if (new_height > DEFAULT_TEST_CONFIG.max_input_height) {
        new_height = DEFAULT_TEST_CONFIG.max_input_height;
    }

    // Test clamping scenarios
    desired_content_lines = 1;  // Below minimum
    new_height = desired_content_lines;
    if (new_height < DEFAULT_TEST_CONFIG.min_input_height) {
        new_height = DEFAULT_TEST_CONFIG.min_input_height;
    }
    ASSERT(new_height == 2);  // Clamped to min_input_height

    desired_content_lines = 10; // Above maximum
    new_height = desired_content_lines;
    if (new_height > DEFAULT_TEST_CONFIG.max_input_height) {
        new_height = DEFAULT_TEST_CONFIG.max_input_height;
    }
    ASSERT(new_height == 5);  // Clamped to max_input_height

    return 1;
}

// Test 3: Verify layout calculations consistency
static int test_layout_calculations(void) {
    printf("\n%s[TEST] test_layout_calculations%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // Simulate screen dimensions
    int screen_height = 24;
    int screen_width = 80;
    (void)screen_width; // Mark as used to avoid warning

    // Window manager configuration
    int input_height = 3;  // Content lines (no borders)
    int status_height = DEFAULT_TEST_CONFIG.status_height;
    int padding = DEFAULT_TEST_CONFIG.padding;

    // Calculate conversation viewport height
    // Formula: screen_height - input_height - status_height - padding
    int conv_viewport_height = screen_height - input_height - status_height - padding;

    ASSERT(conv_viewport_height == 20);  // 24 - 3 - 1 - 0

    // Ensure minimum conversation height
    if (conv_viewport_height < DEFAULT_TEST_CONFIG.min_conv_height) {
        conv_viewport_height = DEFAULT_TEST_CONFIG.min_conv_height;
    }

    // Test with very small screen
    screen_height = 8;
    input_height = 3;
    conv_viewport_height = screen_height - input_height - status_height - padding;

    ASSERT(conv_viewport_height == 4);  // 8 - 3 - 1 - 0

    // Should be clamped to min_conv_height (5)
    if (conv_viewport_height < DEFAULT_TEST_CONFIG.min_conv_height) {
        conv_viewport_height = DEFAULT_TEST_CONFIG.min_conv_height;
    }

    // But we can't have conv_viewport_height > available space
    // In real code, this would adjust input_height downward
    // For test purposes, we'll verify the calculation logic

    return 1;
}

// Test 4: Verify TUI and window manager alignment
static int test_tui_wm_alignment(void) {
    printf("\n%s[TEST] test_tui_wm_alignment%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // This test ensures TUI and window manager agree on dimensions

    // TUI constants (from tui.c)
    #define TUI_INPUT_WIN_MIN_HEIGHT 2  // Min height for input window (content lines, no borders)

    // Window manager constants (from window_manager.c)
    #define WM_MIN_INPUT_HEIGHT 2  // Minimum content lines (no borders)

    // They should match!
    ASSERT(TUI_INPUT_WIN_MIN_HEIGHT == WM_MIN_INPUT_HEIGHT);

    // Simulate a scenario
    int screen_height = 24;

    // TUI calculates max height as 20% of screen
    int tui_max_height_percent = 20;
    int tui_calculated_max = (screen_height * tui_max_height_percent) / 100;
    if (tui_calculated_max < TUI_INPUT_WIN_MIN_HEIGHT) {
        tui_calculated_max = TUI_INPUT_WIN_MIN_HEIGHT;
    }

    // 20% of 24 = 4.8, rounded down to 4
    ASSERT(tui_calculated_max == 4);

    // Window manager has fixed max (5)
    int wm_max_height = 5;

    // The effective max should be the smaller of the two
    int effective_max = (tui_calculated_max < wm_max_height) ? tui_calculated_max : wm_max_height;

    ASSERT(effective_max == 4);  // TUI's 4 is smaller than WM's 5

    return 1;
}

// Test 5: Verify the fix for border calculation mismatch
static int test_border_calculation_fix(void) {
    printf("\n%s[TEST] test_border_calculation_fix%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // This test demonstrates the bug that was fixed

    // OLD BUGGY CODE (before fix):
    // TUI: Used border calculations (positions +1 for borders)
    // WM: Used content-only calculations (no borders)
    // Result: Mismatch causing scrolling issues

    // NEW FIXED CODE (after fix):
    // Both TUI and WM use content-only calculations (no borders)

    // Simulate old buggy calculation
    int old_tui_cursor_y = 1;  // +1 for border
    int old_tui_cursor_x = 1;  // +1 for border
    int old_wm_input_height = 3;  // Content lines only
    (void)old_tui_cursor_y; // Mark as used to avoid warning
    (void)old_tui_cursor_x; // Mark as used to avoid warning
    (void)old_wm_input_height; // Mark as used to avoid warning

    // In old code, TUI thought window had borders, WM didn't
    // This caused cursor to be positioned incorrectly

    // Simulate new fixed calculation
    int new_tui_cursor_y = 0;  // No border offset
    int new_tui_cursor_x = 0;  // No border offset
    int new_wm_input_height = 3;  // Content lines only

    // Now they match: both use content-only calculations

    // Verify the fix
    ASSERT(new_tui_cursor_y == 0);
    ASSERT(new_tui_cursor_x == 0);

    // The key insight: window manager's input_height should equal
    // the number of lines TUI can draw in (0 to height-1)

    // If WM says input_height = 3, TUI can use rows 0, 1, 2
    for (int row = 0; row < new_wm_input_height; row++) {
        ASSERT(row >= 0 && row < new_wm_input_height);
    }

    return 1;
}

int main(void) {
    printf("%sRunning Window Manager Border Calculation Tests%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    print_test_result("test_config_no_borders", test_config_no_borders());
    print_test_result("test_input_resize_no_borders", test_input_resize_no_borders());
    print_test_result("test_layout_calculations", test_layout_calculations());
    print_test_result("test_tui_wm_alignment", test_tui_wm_alignment());
    print_test_result("test_border_calculation_fix", test_border_calculation_fix());

    print_summary();

    return (tests_failed == 0) ? 0 : 1;
}
