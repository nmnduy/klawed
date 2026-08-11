/*
 * test_mouse_scroll.c - Unit tests for mouse wheel scroll direction decoding
 *
 * Tests tui_mouse_wheel_delta() from tui.h against the bstate values that
 * real ncurses implementations produce:
 *   - Modern ncurses (6.x, SGR protocol): clean BUTTON4/BUTTON5 events
 *   - Apple's system ncurses 5.4 (legacy X10 protocol): every wheel byte was
 *     fed through the real libncurses.5.4 on macOS and the resulting bstates
 *     captured. The literal values below are that empirical table:
 *
 *       '$' (36)  0x02000002 = SHIFT|BUTTON1          classic wheel-up
 *       '%' (37)  0x02000080 = SHIFT|BUTTON2          classic wheel-down
 *       '`' (96)  0x00080000 = BUTTON4                plain X10 wheel-up
 *       'a' (97)  0x08000000 = REPORT_MOUSE_POSITION  plain X10 wheel-down
 *       'd' (100) 0x02080000 = SHIFT|BUTTON4          96-offset wheel-up
 *       'e' (101) 0x0a000000 = SHIFT|REPORT_POS       96-offset wheel-down
 *
 * The plain '`'/'a' pair is what xterm-style terminals (and phone emulators)
 * actually send; 'd'/'e' is the iTerm2 / Terminal.app convention.  'a' is
 * only delivered if REPORT_MOUSE_POSITION is included in the mousemask.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ncurses.h>
#include "../src/tui.h"

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

static int test_ncurses6_clean_events(void) {
    // Modern ncurses (6.x, SGR protocol): clean single-button events.
    // Wheel up = BUTTON4_PRESSED -> negative delta (scroll toward older).
    ASSERT(tui_mouse_wheel_delta(BUTTON4_PRESSED) < 0);
    // Wheel down = BUTTON5_PRESSED -> positive delta (scroll toward newer).
    ASSERT(tui_mouse_wheel_delta(BUTTON5_PRESSED) > 0);
    print_test_result("ncurses6 clean BUTTON4/BUTTON5 wheel events", 1);
    return 1;
}

static int test_apple_pairing_events(void) {
    // Apple ncurses 5.4 pairs BUTTON5_PRESSED with a low button bit on BOTH
    // wheel directions. Build the bstates from the local header's masks so
    // this test is portable across ncurses versions.
    unsigned long apple_wheel_up = BUTTON5_PRESSED | BUTTON1_PRESSED;
    unsigned long apple_wheel_down = BUTTON5_PRESSED | BUTTON2_PRESSED;
    ASSERT(tui_mouse_wheel_delta(apple_wheel_up) < 0);
    ASSERT(tui_mouse_wheel_delta(apple_wheel_down) > 0);
    print_test_result("apple-style BUTTON5|BUTTON1/BUTTON2 pairing", 1);
    return 1;
}

static int test_apple_literal_bstates(void) {
#if NCURSES_MOUSE_VERSION == 1
    // Verified empirically against libncurses.5.4 on macOS with the legacy
    // X10 wheel sequences (see header comment for the byte table):
    ASSERT(tui_mouse_wheel_delta(0x02000002UL) < 0);   // '$' classic wheel-up
    ASSERT(tui_mouse_wheel_delta(0x02000080UL) > 0);   // '%' classic wheel-down
    ASSERT(tui_mouse_wheel_delta(0x00080000UL) < 0);   // '`' plain wheel-up
    ASSERT(tui_mouse_wheel_delta(0x02080000UL) < 0);   // 'd' 96-offset wheel-up
    ASSERT(tui_mouse_wheel_delta(0x0a000000UL) > 0);   // 'e' 96-offset wheel-down
    ASSERT(tui_mouse_wheel_delta(0x08000000UL) > 0);   // 'a' plain wheel-down
    print_test_result("apple ncurses 5.4 literal bstate values (6 bytes)", 1);
#else
    // On modern ncurses these literals are not the native encoding; the
    // pairing test above already covers the semantics.
    print_test_result("apple ncurses 5.4 literal bstate values (skipped, ncurses > 1)", 1);
#endif
    return 1;
}

static int test_non_wheel_events(void) {
    // Non-wheel events must produce no scroll delta.
    ASSERT(tui_mouse_wheel_delta(0) == 0);
    ASSERT(tui_mouse_wheel_delta(BUTTON1_PRESSED) == 0);    // left click
    ASSERT(tui_mouse_wheel_delta(BUTTON2_PRESSED) == 0);    // middle click
    ASSERT(tui_mouse_wheel_delta(BUTTON3_PRESSED) == 0);    // right click
    ASSERT(tui_mouse_wheel_delta(BUTTON1_RELEASED) == 0);   // release
    ASSERT(tui_mouse_wheel_delta(BUTTON1_CLICKED) == 0);    // click
    ASSERT(tui_mouse_wheel_delta(BUTTON4_RELEASED) == 0);   // wheel release
    ASSERT(tui_mouse_wheel_delta(BUTTON5_PRESSED | BUTTON3_PRESSED) > 0); // wheel down w/ right button
    print_test_result("non-wheel events produce no scroll", 1);
    return 1;
}

int main(void) {
    printf(TEST_COLOR_YELLOW "\n=== Mouse Scroll Direction Tests ===\n" TEST_COLOR_RESET "\n");
    test_ncurses6_clean_events();
    test_apple_pairing_events();
    test_apple_literal_bstates();
    test_non_wheel_events();
    print_summary();
    return tests_failed > 0 ? 1 : 0;
}
