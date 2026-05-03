/*
 * Unit Tests for Markdown Rendering
 *
 * Tests the markdown detection and parsing functions in markdown_render.c:
 * - markdown_is_table_row()
 * - markdown_is_table_separator()
 * - markdown_code_fence()
 * - find_italic_underscore() (word boundary behavior)
 * - find_italic_star() (word boundary behavior)
 * - find_bold_underscores()
 * - find_bold_stars()
 *
 * Compilation: make test-markdown-render
 * Usage: ./test_markdown_render
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

/* Include markdown render header */
#include "../src/markdown_render.h"

/* Forward declarations of internal helpers (exposed via TEST_BUILD) */
const char *find_italic_underscore(const char *start, size_t len);
const char *find_italic_star(const char *start, size_t len);
const char *find_bold_underscores(const char *start, size_t len);
const char *find_bold_stars(const char *start, size_t len);

/* Test framework colors */
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_CYAN  "\033[36m"

/* Test counters */
static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* ------------------------------------------------------------------ */

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

/* ==================================================================
 * markdown_is_table_row  tests
 * ================================================================== */

static void test_table_row_simple(void) {
    const char *name = "table_row_simple";
    const char *line = "| Name | Age |";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_table_row_leading_whitespace(void) {
    const char *name = "table_row_leading_whitespace";
    const char *line = "   | Name | Age |";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_table_row_trailing_whitespace(void) {
    const char *name = "table_row_trailing_whitespace";
    const char *line = "| Name | Age |   ";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_table_row_single_column(void) {
    /* A row with only one column (no interior |) is NOT a table row */
    const char *name = "table_row_single_column";
    const char *line = "| Just one |";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_table_row_not_a_table(void) {
    const char *name = "table_row_not_a_table";
    const char *line = "This is just a regular line";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_table_row_pipe_mid_line(void) {
    /* A pipe in the middle but not at start/end is not a table row */
    const char *name = "table_row_pipe_mid_line";
    const char *line = "Some text | more text";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_table_row_empty(void) {
    const char *name = "table_row_empty";
    const char *line = "";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_table_row_null(void) {
    const char *name = "table_row_null";
    int r = markdown_is_table_row(NULL, 0);
    print_test_result(name, r == 0);
}

static void test_table_row_three_columns(void) {
    const char *name = "table_row_three_columns";
    const char *line = "| col1 | col2 | col3 |";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_table_row_empty_cells(void) {
    const char *name = "table_row_empty_cells";
    const char *line = "| | | |";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_table_row_whitespace_only_between_pipes(void) {
    const char *name = "table_row_whitespace_only";
    const char *line = "   |   |   |   ";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 1);
}

/* ==================================================================
 * markdown_is_table_separator  tests
 * ================================================================== */

static void test_separator_simple(void) {
    const char *name = "separator_simple";
    const char *line = "|------|------|";
    int r = markdown_is_table_separator(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_separator_with_colons(void) {
    const char *name = "separator_with_colons";
    const char *line = "|:-----|:----:|-----:|";
    int r = markdown_is_table_separator(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_separator_leading_whitespace(void) {
    const char *name = "separator_leading_whitespace";
    const char *line = "   |---|----|";
    int r = markdown_is_table_separator(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_separator_single_dash(void) {
    const char *name = "separator_single_dash";
    const char *line = "|-|-|";
    int r = markdown_is_table_separator(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_separator_no_dash(void) {
    /* Must have at least one dash */
    const char *name = "separator_no_dash";
    const char *line = "| | |";
    int r = markdown_is_table_separator(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_separator_invalid_chars(void) {
    const char *name = "separator_invalid_chars";
    const char *line = "| abc | def |";
    int r = markdown_is_table_separator(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_separator_not_a_table_row_first(void) {
    /* Not a valid table row at all */
    const char *name = "separator_not_table_row";
    const char *line = "just a line with --- dashes";
    int r = markdown_is_table_separator(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_separator_empty(void) {
    const char *name = "separator_empty";
    const char *line = "";
    int r = markdown_is_table_separator(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_separator_null(void) {
    const char *name = "separator_null";
    int r = markdown_is_table_separator(NULL, 0);
    print_test_result(name, r == 0);
}

static void test_separator_spaces_between(void) {
    const char *name = "separator_spaces_between";
    const char *line = "| --- | --- |";
    int r = markdown_is_table_separator(line, strlen(line));
    print_test_result(name, r == 1);
}

/* ==================================================================
 * markdown_code_fence tests
 * ================================================================== */

static void test_code_fence_simple(void) {
    const char *name = "code_fence_simple";
    const char *line = "```";
    int r = markdown_code_fence(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_code_fence_with_language(void) {
    const char *name = "code_fence_with_language";
    const char *line = "```sql";
    int r = markdown_code_fence(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_code_fence_with_content_no_space(void) {
    /* Fences can have content immediately after backticks */
    const char *name = "code_fence_with_content_no_space";
    const char *line = "```WITH lots AS (";
    int r = markdown_code_fence(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_code_fence_leading_whitespace(void) {
    const char *name = "code_fence_leading_whitespace";
    const char *line = "   ```python";
    int r = markdown_code_fence(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_code_fence_not_fence(void) {
    const char *name = "code_fence_not_fence";
    const char *line = "`` only two ticks";
    int r = markdown_code_fence(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_code_fence_inline_ticks(void) {
    const char *name = "code_fence_inline_ticks";
    const char *line = "some ```code``` here";
    int r = markdown_code_fence(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_code_fence_empty(void) {
    const char *name = "code_fence_empty";
    const char *line = "";
    int r = markdown_code_fence(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_code_fence_null(void) {
    const char *name = "code_fence_null";
    int r = markdown_code_fence(NULL, 0);
    print_test_result(name, r == 0);
}

/* ==================================================================
 * Italic underscore word boundary tests
 * ================================================================== */

static void test_italic_underscore_simple(void) {
    const char *name = "italic_underscore_simple";
    const char *text = "_italic_";
    const char *close = find_italic_underscore(text, strlen(text));
    print_test_result(name, close != NULL && close == text + 7);
}

static void test_italic_underscore_intra_word(void) {
    /* Intra-word underscore should NOT be treated as emphasis */
    const char *name = "italic_underscore_intra_word";
    const char *text = "quantity_bought";
    const char *close = find_italic_underscore(text, strlen(text));
    print_test_result(name, close == NULL);
}

static void test_italic_underscore_snake_case(void) {
    const char *name = "italic_underscore_snake_case";
    const char *text = "purchase_timestamp";
    const char *close = find_italic_underscore(text, strlen(text));
    print_test_result(name, close == NULL);
}

static void test_italic_underscore_after_space(void) {
    const char *name = "italic_underscore_after_space";
    const char *text = "word _italic_ word";
    const char *close = find_italic_underscore(text + 5, 8);
    print_test_result(name, close != NULL && close == text + 12);
}

static void test_italic_underscore_at_start(void) {
    const char *name = "italic_underscore_at_start";
    const char *text = "_start_";
    const char *close = find_italic_underscore(text, strlen(text));
    print_test_result(name, close != NULL && close == text + 6);
}

static void test_italic_underscore_no_close(void) {
    const char *name = "italic_underscore_no_close";
    const char *text = "_no_close";
    const char *close = find_italic_underscore(text, strlen(text));
    print_test_result(name, close == NULL);
}

static void test_italic_underscore_followed_by_alnum(void) {
    /* Closing _ followed by alphanumeric should not match */
    const char *name = "italic_underscore_followed_by_alnum";
    const char *text = "_test_x";
    const char *close = find_italic_underscore(text, strlen(text));
    print_test_result(name, close == NULL);
}

/* ==================================================================
 * Italic star word boundary tests
 * ================================================================== */

static void test_italic_star_simple(void) {
    const char *name = "italic_star_simple";
    const char *text = "*italic*";
    const char *close = find_italic_star(text, strlen(text));
    print_test_result(name, close != NULL && close == text + 7);
}

static void test_italic_star_intra_word(void) {
    /* Intra-word star should NOT be treated as emphasis */
    const char *name = "italic_star_intra_word";
    const char *text = "quantity*bought";
    const char *close = find_italic_star(text, strlen(text));
    print_test_result(name, close == NULL);
}

static void test_italic_star_after_space(void) {
    const char *name = "italic_star_after_space";
    const char *text = "word *italic* word";
    const char *close = find_italic_star(text + 5, 8);
    print_test_result(name, close != NULL && close == text + 12);
}

static void test_italic_star_at_start(void) {
    const char *name = "italic_star_at_start";
    const char *text = "*start*";
    const char *close = find_italic_star(text, strlen(text));
    print_test_result(name, close != NULL && close == text + 6);
}

static void test_italic_star_no_close(void) {
    const char *name = "italic_star_no_close";
    const char *text = "*no_close";
    const char *close = find_italic_star(text, strlen(text));
    print_test_result(name, close == NULL);
}

static void test_italic_star_followed_by_alnum(void) {
    /* Closing * followed by alphanumeric should not match */
    const char *name = "italic_star_followed_by_alnum";
    const char *text = "*test*x";
    const char *close = find_italic_star(text, strlen(text));
    print_test_result(name, close == NULL);
}

/* ==================================================================
 * Bold underscore tests
 * ================================================================== */

static void test_bold_underscore_simple(void) {
    const char *name = "bold_underscore_simple";
    const char *text = "__bold__";
    const char *close = find_bold_underscores(text, strlen(text));
    print_test_result(name, close != NULL && close == text + 6);
}

static void test_bold_underscore_no_close(void) {
    const char *name = "bold_underscore_no_close";
    const char *text = "__no_close";
    const char *close = find_bold_underscores(text, strlen(text));
    print_test_result(name, close == NULL);
}

/* ==================================================================
 * Bold star tests
 * ================================================================== */

static void test_bold_star_simple(void) {
    const char *name = "bold_star_simple";
    const char *text = "**bold**";
    const char *close = find_bold_stars(text, strlen(text));
    print_test_result(name, close != NULL && close == text + 6);
}

static void test_bold_star_no_close(void) {
    const char *name = "bold_star_no_close";
    const char *text = "**no_close";
    const char *close = find_bold_stars(text, strlen(text));
    print_test_result(name, close == NULL);
}

/* ==================================================================
 * Integration-style tests: detecting tables from multi-line text
 * ================================================================== */

static void test_detect_valid_table(void) {
    const char *name = "detect_valid_table";
    const char *lines[] = {
        "| Name  | Age |",
        "|-------|-----|",
        "| Alice | 30  |",
        "| Bob   | 25  |",
    };
    size_t n = sizeof(lines) / sizeof(lines[0]);

    /* All lines should be table rows */
    int ok = 1;
    for (size_t i = 0; i < n; i++) {
        if (!markdown_is_table_row(lines[i], strlen(lines[i]))) {
            ok = 0;
            break;
        }
    }
    /* Second line should be a separator */
    if (ok && !markdown_is_table_separator(lines[1], strlen(lines[1]))) {
        ok = 0;
    }
    /* First and third lines should NOT be separators */
    if (ok && markdown_is_table_separator(lines[0], strlen(lines[0]))) {
        ok = 0;
    }
    if (ok && markdown_is_table_separator(lines[2], strlen(lines[2]))) {
        ok = 0;
    }
    print_test_result(name, ok);
}

static void test_detect_single_pipe_line(void) {
    /* A line with | in it but not a table */
    const char *name = "detect_single_pipe_line";
    const char *line = "This has a | pipe character in it";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_detect_code_fence_not_table(void) {
    /* Code fences should not be detected as table rows */
    const char *name = "detect_code_fence_not_table";
    const char *line = "```";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_detect_header_not_table(void) {
    /* Headers should not be detected as table rows */
    const char *name = "detect_header_not_table";
    const char *line = "## Section Title";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_detect_hrule_not_table(void) {
    /* Horizontal rules should not be detected as table rows */
    const char *name = "detect_hrule_not_table";
    const char *line = "---";
    int r = markdown_is_table_row(line, strlen(line));
    print_test_result(name, r == 0);
}

static void test_detect_alignment_separator(void) {
    const char *name = "detect_alignment_separator";
    const char *line = "| :--- | :---: | ---: |";
    int r = markdown_is_table_separator(line, strlen(line));
    print_test_result(name, r == 1);
}

static void test_detect_multi_col_table(void) {
    const char *name = "detect_multi_col_table";
    const char *lines[] = {
        "| A | B | C | D | E |",
        "|---|---|---|---|---|",
        "| 1 | 2 | 3 | 4 | 5 |",
    };
    size_t n = sizeof(lines) / sizeof(lines[0]);

    int ok = 1;
    for (size_t i = 0; i < n; i++) {
        if (!markdown_is_table_row(lines[i], strlen(lines[i]))) {
            ok = 0;
        }
    }
    if (ok && !markdown_is_table_separator(lines[1], strlen(lines[1]))) {
        ok = 0;
    }
    print_test_result(name, ok);
}

/* ==================================================================
 * Main
 * ================================================================== */

int main(void) {
    printf(COLOR_CYAN "Running Markdown Render Tests\n" COLOR_RESET);
    printf("=============================\n\n");

    /* --- markdown_is_table_row --- */
    printf("--- markdown_is_table_row ---\n");
    test_table_row_simple();
    test_table_row_leading_whitespace();
    test_table_row_trailing_whitespace();
    test_table_row_single_column();
    test_table_row_not_a_table();
    test_table_row_pipe_mid_line();
    test_table_row_empty();
    test_table_row_null();
    test_table_row_three_columns();
    test_table_row_empty_cells();
    test_table_row_whitespace_only_between_pipes();

    /* --- markdown_is_table_separator --- */
    printf("\n--- markdown_is_table_separator ---\n");
    test_separator_simple();
    test_separator_with_colons();
    test_separator_leading_whitespace();
    test_separator_single_dash();
    test_separator_no_dash();
    test_separator_invalid_chars();
    test_separator_not_a_table_row_first();
    test_separator_empty();
    test_separator_null();
    test_separator_spaces_between();

    /* --- markdown_code_fence --- */
    printf("\n--- markdown_code_fence ---\n");
    test_code_fence_simple();
    test_code_fence_with_language();
    test_code_fence_with_content_no_space();
    test_code_fence_leading_whitespace();
    test_code_fence_not_fence();
    test_code_fence_inline_ticks();
    test_code_fence_empty();
    test_code_fence_null();

    /* --- find_italic_underscore --- */
    printf("\n--- find_italic_underscore ---\n");
    test_italic_underscore_simple();
    test_italic_underscore_intra_word();
    test_italic_underscore_snake_case();
    test_italic_underscore_after_space();
    test_italic_underscore_at_start();
    test_italic_underscore_no_close();
    test_italic_underscore_followed_by_alnum();

    /* --- find_italic_star --- */
    printf("\n--- find_italic_star ---\n");
    test_italic_star_simple();
    test_italic_star_intra_word();
    test_italic_star_after_space();
    test_italic_star_at_start();
    test_italic_star_no_close();
    test_italic_star_followed_by_alnum();

    /* --- find_bold_underscores --- */
    printf("\n--- find_bold_underscores ---\n");
    test_bold_underscore_simple();
    test_bold_underscore_no_close();

    /* --- find_bold_stars --- */
    printf("\n--- find_bold_stars ---\n");
    test_bold_star_simple();
    test_bold_star_no_close();

    /* --- Integration tests --- */
    printf("\n--- integration / cross-checks ---\n");
    test_detect_valid_table();
    test_detect_single_pipe_line();
    test_detect_code_fence_not_table();
    test_detect_header_not_table();
    test_detect_hrule_not_table();
    test_detect_alignment_separator();
    test_detect_multi_col_table();

    print_summary();
    return tests_failed > 0 ? 1 : 0;
}
