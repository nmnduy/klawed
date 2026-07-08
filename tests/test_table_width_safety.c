/*
 * Table Width Safety Validator — one-off script
 *
 * Exercises table_distribute_widths() across a comprehensive range of
 * pad widths, column counts, and content widths.  Verifies that the
 * total rendered table row width never reaches pad_width — which is
 * the safety invariant that prevents ncurses right-edge auto-wrap from
 * creating phantom blank lines after every table row.
 *
 * Compile:
 *   cc -std=c11 -DTEST_BUILD -DTABLE_TOTAL_LT_PADWIDTH_ONLY \
 *      -o /tmp/test_table_width_safety \
 *      tests/test_table_width_safety.c src/markdown_render.c \
 *      -I./src -lbsd -lncurses
 *
 * Run:
 *   /tmp/test_table_width_safety
 *
 * This script is designed to be run BOTH before and after the fix to
 * confirm the issue and validate the fix:
 *   1. Checkout the commit before the fix, compile & run → FAIL
 *   2. Apply the fix, compile & run → PASS
 */

#define _POSIX_C_SOURCE 200809L
#define TABLE_TOTAL_LT_PADWIDTH_ONLY
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

/* Pull in the table functions from markdown_render.c (TEST_BUILD exposes them) */
#include "../src/markdown_render.h"

/* Internal helpers exposed by TEST_BUILD */
int table_should_wrap(size_t num_cols, int pad_width, int left_border_width,
                      const int *max_content_widths);
void table_distribute_widths(int *col_widths, size_t num_cols, int pad_width,
                             int left_border_width, const int *max_content_widths);

/* Constants matching markdown_render.c */
#define TABLE_WRAP_PER_COL_OVERHEAD 3
#define TABLE_WRAP_MIN_COL_WIDTH 8
#define TABLE_WRAP_MAX_COLS 6
#define TABLE_MAX_COLS 16

#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_CYAN  "\033[36m"
#define COLOR_RESET "\033[0m"

static int failures = 0;
static int cases_run = 0;

/* Compute total rendered width of a table row given distributed widths.
 * Each column renders as: space + content(col_width) + space + pipe
 * Plus the leading left border pipe. */
static int total_rendered_width(const int *col_widths, size_t num_cols,
                                int left_bw) {
    int total = left_bw;
    for (size_t j = 0; j < num_cols; j++) {
        total += 1 + col_widths[j] + 1 + 1;  /* space + content + space + pipe */
    }
    return total;
}

/* Check a single configuration.
 * left_border_width is the raw parameter (0 means no explicit border).
 * table_distribute_widths internally treats 0 as 1 for the '|' border,
 * so we must mirror that conversion when computing the total. */
static int check_one(size_t num_cols, int pad_width, int left_border_width,
                     const int *content_widths) {
    int col_widths[TABLE_MAX_COLS];
    int effective_left_bw = (left_border_width > 0) ? left_border_width : 1;
    table_distribute_widths(col_widths, num_cols, pad_width, left_border_width, content_widths);
    int total = total_rendered_width(col_widths, num_cols, effective_left_bw);
    cases_run++;
    if (total >= pad_width) {
        printf(COLOR_RED "  FAIL" COLOR_RESET
               " cols=%zu pad=%3d left_bw=%d  total=%3d >= pad_width  widths=[",
               num_cols, pad_width, effective_left_bw, total);
        for (size_t j = 0; j < num_cols; j++) {
            printf("%d%s", col_widths[j], j + 1 < num_cols ? "," : "");
        }
        printf("]\n");
        failures++;
        return 0;
    }
    return 1;
}

int main(void) {
    setlocale(LC_ALL, "en_US.UTF-8");
    if (!setlocale(LC_ALL, "")) {
        setlocale(LC_ALL, "C.UTF-8");
    }

    printf(COLOR_CYAN "Table Width Safety Validator\n" COLOR_RESET);
    printf("============================\n\n");
    printf("Verifying total rendered width < pad_width for all configurations.\n");
    printf("If any case shows 'total >= pad_width', ncurses right-edge auto-wrap\n");
    printf("will create phantom blank lines after table rows.\n\n");

    /* Generate a range of content widths to test */
    int content_profiles[][TABLE_MAX_COLS] = {
        /* balanced */
        {20, 20},
        {15, 15, 15},
        {10, 10, 10, 10},
        /* one wide column */
        {50, 10},
        {60, 20, 15},
        {70, 15, 15, 10},
        /* all narrow */
        {5, 5},
        {5, 5, 5},
        {8, 8, 8, 8, 8, 8},
        /* realistic mix */
        {40, 30, 25},
        {35, 50, 20},
        {10, 80, 15},
        {5, 5, 5, 5},
        {30, 25, 20, 15},
        {15, 20, 25, 30, 10, 5},
        /* extreme */
        {100, 100},
        {80, 80, 80},
    };
    size_t profile_cols[] = {2, 3, 4, 2, 3, 4, 2, 3, 6, 3, 3, 3, 4, 4, 6, 2, 3};
    int num_profiles = (int)(sizeof(profile_cols) / sizeof(profile_cols[0]));

    int pad_widths[] = {40, 50, 60, 70, 80, 90, 100, 120, 140, 160, 200};

    printf("--- Sweeping pad widths × content profiles ---\n");

    for (int pi = 0; pi < num_profiles; pi++) {
        size_t nc = profile_cols[pi];
        for (size_t pw_i = 0; pw_i < sizeof(pad_widths)/sizeof(pad_widths[0]); pw_i++) {
            int pw = pad_widths[pw_i];
            /* Skip if table naturally fits (wouldn't use wrapped mode) */
            int should = table_should_wrap(nc, pw, 0, content_profiles[pi]);
            if (!should) continue;
            check_one(nc, pw, 0, content_profiles[pi]);
        }
    }

    /* Also test with non-zero left borders */
    printf("--- Testing with border widths ---\n");
    int border_widths[] = {0, 2, 4, 6};  /* 0=no border(uses|), 2/4/6=multi-char borders */
    for (int bi = 0; bi < 4; bi++) {
        int bw = border_widths[bi];
        int test_cw[] = {40, 30, 25};
        for (size_t pw_i = 0; pw_i < sizeof(pad_widths)/sizeof(pad_widths[0]); pw_i++) {
            int pw = pad_widths[pw_i];
            int should = table_should_wrap(3, pw, bw, test_cw);
            if (!should) continue;
            int col_w[TABLE_MAX_COLS];
            table_distribute_widths(col_w, 3, pw, bw, test_cw);
            int total = total_rendered_width(col_w, 3, bw > 0 ? bw : 1);
            cases_run++;
            if (total >= pw) {
                printf(COLOR_RED "  FAIL" COLOR_RESET
                       " cols=3 pad=%3d border=%d total=%3d >= pad_width\n",
                       pw, bw, total);
                failures++;
            }
        }
    }

    printf("\n");
    if (failures == 0) {
        printf(COLOR_GREEN "✓ ALL %d CASES PASSED" COLOR_RESET
               " — total width < pad_width in every configuration.\n", cases_run);
        printf("  Ncurses right-edge auto-wrap cannot occur.  Table rendering is safe.\n");
        return 0;
    } else {
        printf(COLOR_RED "✗ %d FAILURES out of %d cases" COLOR_RESET "\n",
               failures, cases_run);
        printf("  Ncurses right-edge auto-wrap WILL create phantom blank lines.\n");
        return 1;
    }
}
