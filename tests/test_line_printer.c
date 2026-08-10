/*
 * Unit Tests for LinePrinter
 *
 * Tests for the unified conversation pad line rendering abstraction.
 * Uses real ncurses pads to verify cursor positioning behavior,
 * which is the only reliable way to test that lp_newline
 * correctly emits '\n' regardless of cur_x position.
 *
 * Compilation: make test-line-printer
 * Usage: ./build/test_line_printer
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <locale.h>

#include <ncurses.h>

#include "../src/line_printer.h"
#include "../src/tui.h"

/* Test framework colors (avoid ncurses COLOR_* macro conflicts) */
#define CLR_RESET "\033[0m"
#define CLR_GREEN "\033[32m"
#define CLR_RED   "\033[31m"
#define CLR_CYAN  "\033[36m"

/* Test counters */
static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* ------------------------------------------------------------------ */

static void print_test_result(const char *test_name, int passed) {
    tests_run++;
    if (passed) {
        tests_passed++;
        printf(CLR_GREEN "✓ PASS" CLR_RESET " %s\n", test_name);
    } else {
        tests_failed++;
        printf(CLR_RED "✗ FAIL" CLR_RESET " %s\n", test_name);
    }
}

static void print_summary(void) {
    printf("\n" CLR_CYAN "Test Summary:" CLR_RESET "\n");
    printf("Tests run: %d\n", tests_run);
    printf(CLR_GREEN "Tests passed: %d\n" CLR_RESET, tests_passed);
    if (tests_failed > 0) {
        printf(CLR_RED "Tests failed: %d\n" CLR_RESET, tests_failed);
    } else {
        printf(CLR_GREEN "All tests passed!\n" CLR_RESET);
    }
}

/* ==================================================================
 * lp_init tests
 * ================================================================== */

static void test_lp_init_no_border(void) {
    const char *name = "lp_init_no_border";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    int ok = (lp.pad == pad) &&
             (lp.border_str == NULL) &&
             (lp.pad_width == 80) &&
             (lp.content_width == 80) &&
             (lp.fill_bg_pair == 0);
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_init_with_border(void) {
    const char *name = "lp_init_with_border";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    /* "│ " is 2 columns wide */
    lp_init(&lp, NULL, pad, "│ ", 1, 2, 80);

    int ok = (lp.pad == pad) &&
             (lp.border_str != NULL) &&
             (lp.pad_width == 80) &&
             (lp.content_width == 78) &&  /* 80 - 2 */
             (lp.fill_bg_pair == 0);
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_init_bg_style(void) {
    const char *name = "lp_init_bg_style";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    /* NCURSES_PAIR_ASSISTANT_BG triggers fill_bg_pair */
    lp_init(&lp, NULL, pad, NULL, 0, NCURSES_PAIR_ASSISTANT_BG, 80);

    int ok = (lp.fill_bg_pair == NCURSES_PAIR_ASSISTANT_BG);
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_init_narrow_content(void) {
    const char *name = "lp_init_narrow_content";
    /* Pad is 2 cols wide, border "│ " is 2 cols, content_width = 2-2 = 0,
     * clamped to 1. */
    WINDOW *pad = newpad(10, 2);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, "│ ", 1, 2, 2);

    int ok = (lp.content_width == 1);
    if (!ok) {
        printf("  content_width=%d, pad_width=%d\n", lp.content_width, lp.pad_width);
    }
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_init_null_lp(void) {
    const char *name = "lp_init_null_lp";
    /* Should not crash */
    lp_init(NULL, NULL, NULL, NULL, 0, 0, 0);
    print_test_result(name, 1);
}

/* ==================================================================
 * lp_newline tests (regression: empty-line paragraph break swallowing)
 * ================================================================== */

static void test_lp_newline_advances_y(void) {
    const char *name = "lp_newline_advances_y";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    /* Position cursor at (0, 5) - simulating mid-line content */
    wmove(pad, 0, 5);
    lp_newline(&lp);

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    int ok = (cur_y == 1) && (cur_x == 0);
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_newline_at_col_zero(void) {
    /* Regression test: before the fix, lp_newline had a guard
     * "if (cur_x > 0)" that silently dropped the newline when
     * the cursor was at column 0. This caused empty-line paragraph
     * breaks to be swallowed in non-bordered response styles
     * (caret, robot, cat). Bordered and BG styles were unaffected
     * because their border drawing / background fill advanced cur_x
     * past 0 before lp_newline was called. */
    const char *name = "lp_newline_at_col_zero";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    /* Position cursor at (0, 0) - simulating empty line */
    wmove(pad, 0, 0);
    lp_newline(&lp);

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* After the fix, a newline should ALWAYS advance to next line */
    int ok = (cur_y == 1) && (cur_x == 0);
    if (!ok) {
        printf("  expected (1,0) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_newline_multiple_empty_lines(void) {
    /* Simulate rendering multiple empty lines (paragraph breaks).
     * Three consecutive lp_newline calls at col 0 should advance
     * to line 3. */
    const char *name = "lp_newline_multiple_empty_lines";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    wmove(pad, 0, 0);
    lp_newline(&lp);  /* line 0 -> 1 */
    lp_newline(&lp);  /* line 1 -> 2 */
    lp_newline(&lp);  /* line 2 -> 3 */

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    int ok = (cur_y == 3) && (cur_x == 0);
    if (!ok) {
        printf("  expected (3,0) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_newline_mixed_content_and_empty(void) {
    /* Render: content line, then empty line, then content line.
     * This is the exact scenario that was broken: paragraph breaks
     * between non-empty lines were being swallowed. */
    const char *name = "lp_newline_mixed_content_and_empty";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    /* First paragraph */
    wmove(pad, 0, 0);
    waddnstr(pad, "Hello world", 11);
    int cur_y_before, cur_x_before;
    getyx(pad, cur_y_before, cur_x_before);
    (void)cur_y_before;
    (void)cur_x_before;
    lp_newline(&lp);  /* end of first paragraph line */

    /* Empty line (paragraph break) — this is the regression case */
    lp_newline(&lp);

    /* Second paragraph */
    waddnstr(pad, "Second paragraph", 16);

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* Should be on line 2 (0: "Hello world", 1: empty, 2: "Second paragraph") */
    int ok = (cur_y == 2) && (cur_x == 16);
    if (!ok) {
        printf("  expected (2,16) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_newline_with_border_at_col_zero(void) {
    /* Bordered mode: border drawing advances cur_x past 0,
     * so the old code worked here. Verify the fix doesn't
     * break this case. */
    const char *name = "lp_newline_with_border_at_col_zero";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, "│ ", 1, 2, 80);

    wmove(pad, 0, 0);
    lp_border(&lp);  /* advances cur_x to 2 */
    lp_newline(&lp);

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    int ok = (cur_y == 1) && (cur_x == 0);
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_newline_with_fill_bg(void) {
    /* BG response style: fill_bg_pair is active.
     * lp_newline should fill the line to pad_width then emit \n. */
    const char *name = "lp_newline_with_fill_bg";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);
    lp.fill_bg_pair = 7;  /* simulate BG fill active */

    wmove(pad, 0, 0);
    lp_newline(&lp);

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* After filling 80 spaces and writing '\n' at the right margin,
     * ncurses may auto-wrap first then advance for newline.
     * We just verify the cursor advanced past line 0. */
    int ok = (cur_y > 0) && (cur_x == 0);
    if (!ok) {
        printf("  expected (y>0,0) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_newline_null_pad(void) {
    const char *name = "lp_newline_null_pad";
    LinePrinter lp = {0};
    lp.pad = NULL;
    lp_newline(&lp);  /* should not crash */
    print_test_result(name, 1);
}

static void test_lp_newline_null_lp(void) {
    const char *name = "lp_newline_null_lp";
    lp_newline(NULL);  /* should not crash */
    print_test_result(name, 1);
}

/* ==================================================================
 * lp_border tests
 * ================================================================== */

static void test_lp_border_null_border(void) {
    const char *name = "lp_border_null_border";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    wmove(pad, 0, 0);
    lp_border(&lp);

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* No border drawn, cursor stays at (0,0) */
    int ok = (cur_y == 0) && (cur_x == 0);
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_border_with_border(void) {
    const char *name = "lp_border_with_border";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, "│ ", 1, 2, 80);

    wmove(pad, 0, 0);
    lp_border(&lp);

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* Border "│ " is 2 display columns (│ = 1 col, space = 1 col) */
    int ok = (cur_y == 0) && (cur_x == 2);
    if (!ok) {
        printf("  expected (0,2) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_border_null_pad(void) {
    const char *name = "lp_border_null_pad";
    LinePrinter lp = {0};
    lp.pad = NULL;
    lp_border(&lp);  /* should not crash */
    print_test_result(name, 1);
}

/* ==================================================================
 * find_wrap_point tests (pure function, testable without ncurses)
 * ================================================================== */

static void test_find_wrap_point_no_wrap(void) {
    const char *name = "find_wrap_point_no_wrap";
    /* "hello" fits in 10 cols */
    size_t pos = find_wrap_point("hello", 5, 10);
    print_test_result(name, pos == 5);
}

static void test_find_wrap_point_mid_word(void) {
    const char *name = "find_wrap_point_mid_word";
    /* "hello" doesn't fit in 3 cols */
    size_t pos = find_wrap_point("hello", 5, 3);
    print_test_result(name, pos == 3);
}

static void test_find_wrap_point_zero_width(void) {
    const char *name = "find_wrap_point_zero_width";
    size_t pos = find_wrap_point("hello", 5, 0);
    print_test_result(name, pos == 1);
}

static void test_find_wrap_point_negative_width(void) {
    const char *name = "find_wrap_point_negative_width";
    size_t pos = find_wrap_point("hello", 5, -5);
    print_test_result(name, pos == 1);
}

static void test_find_wrap_point_empty(void) {
    const char *name = "find_wrap_point_empty";
    /* find_wrap_point returns at least 1 as a fallback
     * (to prevent callers from getting stuck in infinite loops). */
    size_t pos = find_wrap_point("", 0, 10);
    print_test_result(name, pos == 1);
}

static void test_find_wrap_point_exact_fit(void) {
    const char *name = "find_wrap_point_exact_fit";
    /* "abcde" exactly fits in 5 cols */
    size_t pos = find_wrap_point("abcde", 5, 5);
    print_test_result(name, pos == 5);
}

/* ==================================================================
 * lp_print_raw tests
 * ================================================================== */

static void test_lp_print_raw_basic(void) {
    const char *name = "lp_print_raw_basic";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    wmove(pad, 0, 0);
    lp_print_raw(&lp, "hello", 5, 0);

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    int ok = (cur_y == 0) && (cur_x == 5);
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_print_raw_null_text(void) {
    const char *name = "lp_print_raw_null_text";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    wmove(pad, 0, 0);
    lp_print_raw(&lp, NULL, 5, 0);

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* Should not advance cursor */
    int ok = (cur_y == 0) && (cur_x == 0);
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_print_raw_zero_len(void) {
    const char *name = "lp_print_raw_zero_len";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    wmove(pad, 0, 0);
    lp_print_raw(&lp, "hello", 0, 0);

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    int ok = (cur_y == 0) && (cur_x == 0);
    print_test_result(name, ok);
    delwin(pad);
}

/* ==================================================================
 * lp_print_text_wrapped tests
 * ================================================================== */

/* Regression: text that exactly fills a line, followed by explicit \n,
 * should NOT produce a double newline (blank line).
 * This tests the ncurses auto-wrap + explicit \n collision bug. */
static void test_lp_wrap_exact_fill_then_newline(void) {
    const char *name = "lp_wrap_exact_fill_then_newline";
    /* pad width 10, "0123456789"=10chars exactly fills the line.
     * After writing, ncurses may auto-wrap. The \n should NOT add a
     * second newline, leaving cursor at (1,0) not (2,0). */
    WINDOW *pad = newpad(10, 10);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 10);

    wmove(pad, 0, 0);
    lp_print_text_wrapped(&lp, "0123456789\nabc");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* Expected: y=1 (not 2 — no blank line), x=3 (len of "abc") */
    int ok = (cur_y == 1) && (cur_x == 3);
    if (!ok) {
        printf("  expected (1,3) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

/* Regression: text that exceeds line width should wrap, but the
 * overflow should NOT leave a blank line between the wrapped content. */
static void test_lp_wrap_overflow_no_blank_line(void) {
    const char *name = "lp_wrap_overflow_no_blank_line";
    /* pad width 10, "0123456789abc" is 13 chars, first 10 fit, next 3 wrap.
     * After wrapping, cursor should be at (1,3), not (2,3). */
    WINDOW *pad = newpad(10, 10);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 10);

    wmove(pad, 0, 0);
    lp_print_text_wrapped(&lp, "0123456789abc");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* Expected: y=1 (wrapped once), x=3 (remaining "abc") */
    int ok = (cur_y == 1) && (cur_x == 3);
    if (!ok) {
        printf("  expected (1,3) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

/* Regression: consecutive newlines (\n\n) for paragraph breaks
 * should produce one blank line between paragraphs. */
static void test_lp_wrap_paragraph_break_preserved(void) {
    const char *name = "lp_wrap_paragraph_break_preserved";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    wmove(pad, 0, 0);
    /* "First" on line 0, blank line 1, "Second" on line 2 */
    lp_print_text_wrapped(&lp, "First\n\nSecond");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* "Second" is 6 chars, should be on line 2 (0: First, 1: blank, 2: Second) */
    int ok = (cur_y == 2) && (cur_x == 6);
    if (!ok) {
        printf("  expected (2,6) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

/* Regression: exact-fill in bordered mode (border "│ " = 2 cols).
 * pad_width=12, content_width=10. "0123456789" exactly fills
 * the content area. Followed by \nabc. No double newline. */
static void test_lp_wrap_bordered_exact_fill_then_newline(void) {
    const char *name = "lp_wrap_bordered_exact_fill_then_newline";
    WINDOW *pad = newpad(10, 12);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, "│ ", 1, 2, 12);

    wmove(pad, 0, 0);
    lp_print_text_wrapped(&lp, "0123456789\nabc");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* Border (2) + content fits. After \n: line 1 has border+abc.
     * Expected y=1, x=5 (2 border + 3 "abc") */
    int ok = (cur_y == 1) && (cur_x == 5);
    if (!ok) {
        printf("  expected (1,5) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

/* Regression: bordered overflow should wrap without blank line */
static void test_lp_wrap_bordered_overflow_no_blank(void) {
    const char *name = "lp_wrap_bordered_overflow_no_blank";
    WINDOW *pad = newpad(10, 12);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, "│ ", 1, 2, 12);

    wmove(pad, 0, 0);
    lp_print_text_wrapped(&lp, "0123456789abc");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* 10 chars fill content, 3 wrap: y=1 (not 2), x=5 (2 border + 3) */
    int ok = (cur_y == 1) && (cur_x == 5);
    if (!ok) {
        printf("  expected (1,5) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

/* Regression: explicit \n at pad_width (after content fills line)
 * should not produce a double newline.  This is the real streaming
 * scenario: a previous chunk filled to the right margin, leaving
 * the cursor at pad_width.  The next chunk starts with \n.
 * The \n should be consumed without creating a blank line. */
static void test_lp_wrap_newline_at_col_zero_after_wrap(void) {
    const char *name = "lp_wrap_newline_after_full_line";
    WINDOW *pad = newpad(10, 10);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 10);

    /* Simulate: previous content filled the 10-wide line,
     * leaving cursor at pad_width (10).  Next streaming chunk
     * starts with \n. */
    wmove(pad, 0, 10);  /* past right margin */
    lp_print_text_wrapped(&lp, "\nabc");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* \n at pad_width: handled by boundary code, no blank line.
     * "abc" lands on line 1, x=3. */
    int ok = (cur_y == 1) && (cur_x == 3);
    if (!ok) {
        printf("  expected (1,3) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

/* Regression: multiple newlines at col 0 after wrap should produce
 * paragraph break. E.g., "0123456789\n\nabc" with pad_width=10.
 * Line 0: "0123456789" (ncurses wraps), Line 1: blank, Line 2: "abc". */
static void test_lp_wrap_double_newline_after_wrap(void) {
    const char *name = "lp_wrap_double_newline_after_wrap";
    WINDOW *pad = newpad(10, 10);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 10);

    wmove(pad, 0, 0);
    lp_print_text_wrapped(&lp, "0123456789\n\nabc");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* 10 chars fill line 0 → auto-wrap to (1,0).
     * Double \n at (1,0): first \n is skipped (auto-wrap),
     * second \n is paragraph break → line 2.
     * "abc" on line 2.
     * Expected: y=2, x=3 */
    int ok = (cur_y == 2) && (cur_x == 3);
    if (!ok) {
        printf("  expected (2,3) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

/* Regression: text after explicit \n should render at correct position
 * when previous content did NOT fill the line (no auto-wrap). */
static void test_lp_wrap_newline_mid_line(void) {
    const char *name = "lp_wrap_newline_mid_line";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    wmove(pad, 0, 0);
    /* "hello" is 5 chars, \n goes to line 1, "world" is 5 chars */
    lp_print_text_wrapped(&lp, "hello\nworld");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    int ok = (cur_y == 1) && (cur_x == 5);
    if (!ok) {
        printf("  expected (1,5) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

/* Regression: content fills line exactly multiple times,
 * each with explicit \n, should not produce extra blank lines.
 * On platforms where ncurses auto-wraps at the right margin,
 * the cursor may end up on the next empty line after the
 * last fill — this is the correct streaming behavior. */
static void test_lp_wrap_multiple_exact_fills(void) {
    const char *name = "lp_wrap_multiple_exact_fills";
    WINDOW *pad = newpad(10, 10);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 10);

    wmove(pad, 0, 0);
    /* Three lines of exactly 10 chars, each with explicit \n */
    lp_print_text_wrapped(&lp, "AAAAAAAAAA\nBBBBBBBBBB\nCCCCCCCCCC");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* Each line exactly fills → ncurses auto-wraps. Each \n is consumed.
     * The cursor ends at the next line after the third fill.
     * On platforms that auto-wrap during waddnstr: y=3, x=0.
     * On platforms that don't: y=2, x=10. */
    int ok = (cur_y == 3 && cur_x == 0) ||
             (cur_y == 2 && cur_x == 10);
    if (!ok) {
        printf("  expected (3,0) or (2,10) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

/* Regression: in BG fill mode, exact fill + newline should not double */
static void test_lp_wrap_bg_exact_fill_then_newline(void) {
    const char *name = "lp_wrap_bg_exact_fill_then_newline";
    WINDOW *pad = newpad(10, 10);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 10);
    lp.fill_bg_pair = 7;  /* simulate BG fill active */

    wmove(pad, 0, 0);
    /* "0123456789" fills 10-col pad. BG fill won't change width. */
    lp_print_text_wrapped(&lp, "0123456789\nabc");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* With BG fill, lp_fill_line fills to pad_width, then restores cursor.
     * The \n after exact fill: ncurses behavior may differ with BG fill.
     * We just check that y didn't jump too far. */
    int ok = (cur_y == 1) && (cur_x == 3);
    if (!ok) {
        printf("  expected (1,3) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_print_text_wrapped_simple(void) {
    const char *name = "lp_print_text_wrapped_simple";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    wmove(pad, 0, 0);
    lp_print_text_wrapped(&lp, "hello world");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* "hello world" = 11 chars, all on one line */
    int ok = (cur_y == 0) && (cur_x == 11);
    if (!ok) {
        printf("  expected (0,11) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_print_text_wrapped_newlines(void) {
    const char *name = "lp_print_text_wrapped_newlines";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    wmove(pad, 0, 0);
    lp_print_text_wrapped(&lp, "line one\nline two\nline three");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* "line one" (8) \n "line two" (8) \n "line three" (10) = line 2, col 10 */
    int ok = (cur_y == 2) && (cur_x == 10);
    if (!ok) {
        printf("  expected (2,10) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

static void test_lp_print_text_wrapped_empty_lines(void) {
    /* Regression test: empty lines (paragraph breaks) in the text
     * should be preserved by lp_print_text_wrapped. Before the
     * lp_newline fix, these were silently dropped. */
    const char *name = "lp_print_text_wrapped_empty_lines";
    WINDOW *pad = newpad(10, 80);
    assert(pad != NULL);

    LinePrinter lp;
    lp_init(&lp, NULL, pad, NULL, 0, 0, 80);

    wmove(pad, 0, 0);
    /* First paragraph, blank line, second paragraph */
    lp_print_text_wrapped(&lp, "First\n\nSecond");

    int cur_y, cur_x;
    getyx(pad, cur_y, cur_x);
    /* "First" (5) on line 0, \n to line 1, \n to line 2, "Second" (6) on line 2 */
    int ok = (cur_y == 2) && (cur_x == 6);
    if (!ok) {
        printf("  expected (2,6) got (%d,%d)\n", cur_y, cur_x);
    }
    print_test_result(name, ok);
    delwin(pad);
}

/* ==================================================================
 * Main
 * ================================================================== */

int main(void) {
    /* Initialize ncurses before any tests that use pads */
    setlocale(LC_ALL, "");
    initscr();
    noecho();
    cbreak();

    printf(CLR_CYAN "Running LinePrinter Tests\n" CLR_RESET);
    printf("=========================\n\n");

    /* --- lp_init --- */
    printf("--- lp_init ---\n");
    test_lp_init_no_border();
    test_lp_init_with_border();
    test_lp_init_bg_style();
    test_lp_init_narrow_content();
    test_lp_init_null_lp();

    /* --- lp_newline (regression tests) --- */
    printf("\n--- lp_newline ---\n");
    test_lp_newline_advances_y();
    test_lp_newline_at_col_zero();
    test_lp_newline_multiple_empty_lines();
    test_lp_newline_mixed_content_and_empty();
    test_lp_newline_with_border_at_col_zero();
    test_lp_newline_with_fill_bg();
    test_lp_newline_null_pad();
    test_lp_newline_null_lp();

    /* --- lp_border --- */
    printf("\n--- lp_border ---\n");
    test_lp_border_null_border();
    test_lp_border_with_border();
    test_lp_border_null_pad();

    /* --- find_wrap_point --- */
    printf("\n--- find_wrap_point ---\n");
    test_find_wrap_point_no_wrap();
    test_find_wrap_point_mid_word();
    test_find_wrap_point_zero_width();
    test_find_wrap_point_negative_width();
    test_find_wrap_point_empty();
    test_find_wrap_point_exact_fit();

    /* --- lp_print_raw --- */
    printf("\n--- lp_print_raw ---\n");
    test_lp_print_raw_basic();
    test_lp_print_raw_null_text();
    test_lp_print_raw_zero_len();

    /* --- lp_print_text_wrapped --- */
    printf("\n--- lp_print_text_wrapped ---\n");
    test_lp_print_text_wrapped_simple();
    test_lp_print_text_wrapped_newlines();
    test_lp_print_text_wrapped_empty_lines();

    /* --- lp_print_text_wrapped regression: double-newline after auto-wrap --- */
    printf("\n--- lp_print_text_wrapped regression: auto-wrap double-newline ---\n");
    test_lp_wrap_exact_fill_then_newline();
    test_lp_wrap_overflow_no_blank_line();
    test_lp_wrap_paragraph_break_preserved();
    test_lp_wrap_bordered_exact_fill_then_newline();
    test_lp_wrap_bordered_overflow_no_blank();
    test_lp_wrap_newline_at_col_zero_after_wrap();
    test_lp_wrap_double_newline_after_wrap();
    test_lp_wrap_newline_mid_line();
    test_lp_wrap_multiple_exact_fills();
    test_lp_wrap_bg_exact_fill_then_newline();

    print_summary();

    endwin();
    return tests_failed > 0 ? 1 : 0;
}
