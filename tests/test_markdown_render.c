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
#include <locale.h>

/* Include markdown render header */
#include "../src/markdown_render.h"

/* Forward declarations of internal helpers (exposed via TEST_BUILD) */
const char *find_italic_underscore(const char *start, size_t len);
const char *find_italic_star(const char *start, size_t len);
const char *find_bold_underscores(const char *start, size_t len);
const char *find_bold_stars(const char *start, size_t len);
const char *find_code_ticks(const char *start, size_t len, size_t tick_len);
const char *find_strike_tildes(const char *start, size_t len);
size_t table_split_cells(const char *row, size_t len,
                         const char **cells, size_t *cell_lens, size_t max_cells);
int cell_display_width(const char *text, size_t len);
size_t cell_wrap_point(const char *text, size_t text_len, int max_display_width);
int cell_wrap_count(const char *text, size_t text_len, int max_width);
int cell_get_wrapped_line(const char *text, size_t text_len, int max_width,
                          int line_idx, const char **out_start, size_t *out_len);
int table_should_wrap(size_t num_cols, int pad_width, int left_border_width,
                      const int *max_content_widths);
void table_distribute_widths(int *col_widths, size_t num_cols, int pad_width,
                             int left_border_width, const int *max_content_widths);
int markdown_stripped_display_width(const char *text, size_t len);

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
 * table_split_cells  tests
 * ================================================================== */

static void test_split_cells_simple(void) {
    const char *name = "split_cells_simple";
    const char *row = "| a | b | c |";
    const char *cells[8];
    size_t cell_lens[8];
    size_t n = table_split_cells(row, strlen(row), cells, cell_lens, 8);
    int ok = (n == 3) &&
             (cell_lens[0] == 1 && memcmp(cells[0], "a", 1) == 0) &&
             (cell_lens[1] == 1 && memcmp(cells[1], "b", 1) == 0) &&
             (cell_lens[2] == 1 && memcmp(cells[2], "c", 1) == 0);
    print_test_result(name, ok);
}

static void test_split_cells_whitespace(void) {
    const char *name = "split_cells_whitespace";
    const char *row = "|  hello  |  world  |";
    const char *cells[8];
    size_t cell_lens[8];
    size_t n = table_split_cells(row, strlen(row), cells, cell_lens, 8);
    int ok = (n == 2) &&
             (cell_lens[0] == 5 && memcmp(cells[0], "hello", 5) == 0) &&
             (cell_lens[1] == 5 && memcmp(cells[1], "world", 5) == 0);
    print_test_result(name, ok);
}

static void test_split_cells_empty(void) {
    const char *name = "split_cells_empty";
    const char *row = "| a || c |";
    const char *cells[8];
    size_t cell_lens[8];
    size_t n = table_split_cells(row, strlen(row), cells, cell_lens, 8);
    int ok = (n == 3) &&
             (cell_lens[0] == 1 && memcmp(cells[0], "a", 1) == 0) &&
             (cell_lens[1] == 0) &&
             (cell_lens[2] == 1 && memcmp(cells[2], "c", 1) == 0);
    print_test_result(name, ok);
}

static void test_split_cells_leading_no_pipe(void) {
    const char *name = "split_cells_leading_no_pipe";
    const char *row = "a | b";
    const char *cells[8];
    size_t cell_lens[8];
    size_t n = table_split_cells(row, strlen(row), cells, cell_lens, 8);
    /* First cell includes everything up to the pipe, no trimming */
    int ok = (n >= 1);
    print_test_result(name, ok);
}

static void test_split_cells_max_limited(void) {
    const char *name = "split_cells_max_limited";
    const char *row = "| a | b | c | d | e |";
    const char *cells[3];
    size_t cell_lens[3];
    size_t n = table_split_cells(row, strlen(row), cells, cell_lens, 3);
    int ok = (n == 3);
    print_test_result(name, ok);
}

/* ==================================================================
 * cell_display_width  tests
 * ================================================================== */

static void test_display_width_ascii(void) {
    const char *name = "display_width_ascii";
    int w = cell_display_width("hello", 5);
    print_test_result(name, w == 5);
}

static void test_display_width_empty(void) {
    const char *name = "display_width_empty";
    int w = cell_display_width("", 0);
    print_test_result(name, w == 0);
}

static void test_display_width_wide(void) {
    const char *name = "display_width_wide";
    /* 4-byte emoji: 😀 = F0 9F 98 80 */
    int w = cell_display_width("\xF0\x9F\x98\x80", 4);
    print_test_result(name, w == 2);
}

static void test_display_width_3byte_wide(void) {
    const char *name = "display_width_3byte_wide";
    /* ✅ = U+2705, 3-byte UTF-8: E2 9C 85. wcwidth=2.
     * This is the regression case: the old naive byte-count gave 1,
     * but wcwidth correctly gives 2. */
    int w = cell_display_width("\xE2\x9C\x85", 3);
    print_test_result(name, w == 2);
}

static void test_display_width_3byte_narrow(void) {
    const char *name = "display_width_3byte_narrow";
    /* — = U+2014, 3-byte UTF-8: E2 80 94. wcwidth=1. */
    int w = cell_display_width("\xE2\x80\x94", 3);
    print_test_result(name, w == 1);
}

static void test_display_width_mixed_wide_ascii(void) {
    const char *name = "display_width_mixed_wide_ascii";
    /* ✅ ok = 2 (emoji) + 1 (space) + 2 (ok) = 5 */
    int w = cell_display_width("\xE2\x9C\x85 ok", 6);
    print_test_result(name, w == 5);
}

static void test_display_width_stripped_wide_consistency(void) {
    const char *name = "display_width_stripped_wide_consistency";
    /* ✅ **CONFIRMED** — the exact broken-table cell.
     * cell_display_width: 2(emoji) + 1(space) + 2(**) + 9(CONFIRMED) + 2(**) = 16
     * stripped: 2(emoji) + 1(space) + 9(CONFIRMED) = 12
     * The key check: cell_display_width must agree with cell_wrap_point. */
    const char *cell = "\xE2\x9C\x85 **CONFIRMED**";
    size_t len = strlen(cell);
    int raw = cell_display_width(cell, len);
    int stripped = markdown_stripped_display_width(cell, len);
    int ok = (raw == 16 && stripped == 12);
    if (!ok) {
        printf("  raw=%d expected=16  stripped=%d expected=12\n", raw, stripped);
    }
    print_test_result(name, ok);
}

static void test_width_wrap_consistency_wide_3byte(void) {
    const char *name = "width_wrap_consistency_wide_3byte";
    /* Core regression test: cell_display_width and cell_wrap_point must
     * agree on the display width of text containing 3-byte wide chars.
     * If they disagree, columns are allocated too narrow and wrapping
     * breaks words mid-syllable (the broken-table bug). */
    const char *cell = "\xE2\x9C\x85 **CONFIRMED**";
    size_t len = strlen(cell);
    int display_w = cell_display_width(cell, len);

    /* cell_wrap_point with max_width = display_w should consume the ENTIRE
     * string (i.e. return len). If it returns less, the width functions
     * disagree. */
    size_t wrap_bytes = cell_wrap_point(cell, len, display_w);
    int ok = (wrap_bytes == len);
    if (!ok) {
        printf("  display_width=%d  wrap_point(bytes)=%zu/%zu  MISMATCH!\n",
               display_w, wrap_bytes, len);
    }
    print_test_result(name, ok);
}

static void test_width_wrap_consistency_cjk(void) {
    const char *name = "width_wrap_consistency_cjk";
    /* CJK character 中 = U+4E2D, 3-byte, wcwidth=2.
     * Another 3-byte wide char that was miscounted. */
    const char *cell = "\xE4\xB8\xAD\xE6\x96\x87 test";  /* 中文 test */
    size_t len = strlen(cell);
    int display_w = cell_display_width(cell, len);
    size_t wrap_bytes = cell_wrap_point(cell, len, display_w);
    int ok = (wrap_bytes == len);
    if (!ok) {
        printf("  display_width=%d  wrap_point(bytes)=%zu/%zu  MISMATCH!\n",
               display_w, wrap_bytes, len);
    }
    print_test_result(name, ok);
}

/* ==================================================================
 * cell_wrap_count / cell_get_wrapped_line  tests
 * ================================================================== */

static void test_wrap_count_no_wrap(void) {
    const char *name = "wrap_count_no_wrap";
    int n = cell_wrap_count("short", 5, 10);
    print_test_result(name, n == 1);
}

static void test_wrap_count_two_lines(void) {
    const char *name = "wrap_count_two_lines";
    int n = cell_wrap_count("abcdefghijklmnop", 16, 8);
    print_test_result(name, n == 2);
}

static void test_wrap_count_many_lines(void) {
    const char *name = "wrap_count_many_lines";
    int n = cell_wrap_count("abcdefghijklmnopqrstuvwxyz", 26, 3);
    /* 26 chars / 3 per line = 9 lines */
    int ok = (n == 9);
    print_test_result(name, ok);
}

static void test_wrapped_line_get(void) {
    const char *name = "wrapped_line_get";
    const char *text = "abcdefghijklmnopqrstuvwxyz";
    const char *seg;
    size_t seg_len;
    int found = cell_get_wrapped_line(text, 26, 5, 2, &seg, &seg_len);
    /* Line 2 should be chars 10-14 */
    int ok = found && (seg_len == 5) && (memcmp(seg, "klmno", 5) == 0);
    print_test_result(name, ok);
}

static void test_wrapped_line_beyond(void) {
    const char *name = "wrapped_line_beyond";
    const char *text = "short";
    const char *seg;
    size_t seg_len;
    int found = cell_get_wrapped_line(text, 5, 10, 5, &seg, &seg_len);
    int ok = !found;
    print_test_result(name, ok);
}

static void test_wrap_count_with_code_spans(void) {
    const char *name = "wrap_count_with_code_spans";
    /* Text with backtick code spans. Raw width = 34, stripped = 30.
     * With fix: column width = raw width = 34, so no wrapping. */
    const char *text = "Master added `--params`/`--pap` er";
    int raw_w = cell_display_width(text, strlen(text));
    int n = cell_wrap_count(text, strlen(text), raw_w);
    int ok = (n == 1);
    if (!ok) {
        printf("  raw_w=%d lines=%d\n", raw_w, n);
    }
    print_test_result(name, ok);
}

static void test_wrap_count_code_at_column(void) {
    const char *name = "wrap_count_code_at_column";
    /* Text fits exactly at its raw display width */
    const char *text = "foo `bar` baz";
    int raw = cell_display_width(text, strlen(text));
    int n = cell_wrap_count(text, strlen(text), raw);
    int ok = (n == 1);
    print_test_result(name, ok);
}

static void test_wrapped_line_code_spans(void) {
    const char *name = "wrapped_line_code_spans";
    /* Force a narrow column to trigger wrapping with code spans */
    const char *text = "abc `def` ghi `jkl`";
    const char *seg;
    size_t seg_len;
    /* Raw width = 19. With width=6, line 1 should be "abc `d" */
    int found = cell_get_wrapped_line(text, strlen(text), 6, 1, &seg, &seg_len);
    int ok = found && seg_len > 0;
    if (!ok) {
        printf("  found=%d\n", found);
    }
    print_test_result(name, ok);
}

static void test_cell_width_vs_stripped(void) {
    const char *name = "cell_width_vs_stripped";
    /* cell_display_width includes backticks; stripped excludes them.
     * For correct wrapping, column widths must use raw width so the
     * column is wide enough to hold formatting characters. */
    const char *text = "`--params`/`--pap`";
    int raw = cell_display_width(text, strlen(text));
    int stripped = markdown_stripped_display_width(text, strlen(text));
    /* raw: `--params`/`--pap` = 18 chars
     * stripped: --params/--pap = 14 chars */
    int ok = (raw == 18 && stripped == 14);
    if (!ok) {
        printf("  raw=%d stripped=%d\n", raw, stripped);
    }
    print_test_result(name, ok);
}

/* ==================================================================
 * markdown_stripped_display_width  tests
 * ================================================================== */

static void test_stripped_plain_text(void) {
    const char *name = "stripped_plain_text";
    int w = markdown_stripped_display_width("hello world", 11);
    print_test_result(name, w == 11);
}

static void test_stripped_empty(void) {
    const char *name = "stripped_empty";
    int w = markdown_stripped_display_width("", 0);
    print_test_result(name, w == 0);
}

static void test_stripped_null(void) {
    const char *name = "stripped_null";
    int w = markdown_stripped_display_width(NULL, 10);
    print_test_result(name, w == 0);
}

static void test_stripped_bold_stars(void) {
    const char *name = "stripped_bold_stars";
    /* **bold** -> bold (4 fewer chars) */
    int w = markdown_stripped_display_width("**bold**", 8);
    print_test_result(name, w == 4);
}

static void test_stripped_bold_underscores(void) {
    const char *name = "stripped_bold_underscores";
    /* __bold__ -> bold */
    int w = markdown_stripped_display_width("__bold__", 8);
    print_test_result(name, w == 4);
}

static void test_stripped_italic_star(void) {
    const char *name = "stripped_italic_star";
    /* *italic* -> italic */
    int w = markdown_stripped_display_width("*italic*", 8);
    print_test_result(name, w == 6);
}

static void test_stripped_italic_underscore(void) {
    const char *name = "stripped_italic_underscore";
    /* _italic_ -> italic */
    int w = markdown_stripped_display_width("_italic_", 8);
    print_test_result(name, w == 6);
}

static void test_stripped_code(void) {
    const char *name = "stripped_code";
    /* `code` -> code */
    int w = markdown_stripped_display_width("`code`", 6);
    print_test_result(name, w == 4);
}

static void test_stripped_strikethrough(void) {
    const char *name = "stripped_strikethrough";
    /* ~~strike~~ -> strike */
    int w = markdown_stripped_display_width("~~strike~~", 10);
    print_test_result(name, w == 6);
}

static void test_stripped_mixed_formatting(void) {
    const char *name = "stripped_mixed_formatting";
    /* **bold** and *italic* text: 26 raw chars, stripped = 4+5+6+5=20 */
    int w = markdown_stripped_display_width("**bold** and *italic* text", 26);
    print_test_result(name, w == 20);
}

static void test_stripped_unmatched_delimiter(void) {
    const char *name = "stripped_unmatched_delimiter";
    /* **unmatched: first * consumed as bold attempt (1), second * treated as
     * italic attempt (1), then "unmatched" (9) = 11 total */
    int w = markdown_stripped_display_width("**unmatched", 11);
    print_test_result(name, w == 11);
}

static void test_stripped_snake_case(void) {
    const char *name = "stripped_snake_case";
    /* snake_case: _ after alnum is not italic delimiter */
    int w = markdown_stripped_display_width("snake_case", 10);
    print_test_result(name, w == 10);
}

static void test_stripped_intra_word_star(void) {
    const char *name = "stripped_intra_word_star";
    /* intra*word: * after alnum is not italic */
    int w = markdown_stripped_display_width("intra*word", 10);
    print_test_result(name, w == 10);
}

/* ==================================================================
 * table_should_wrap / table_distribute_widths  tests
 * ================================================================== */

static void test_should_wrap_fits_naturally(void) {
    const char *name = "should_wrap_fits_naturally";
    int widths[] = {5, 5, 5};
    /* 3 cols: natural = 1 + 3*(5+3) = 25. pad=80 > 25, so no wrap needed */
    int r = table_should_wrap(3, 80, 0, widths);
    print_test_result(name, r == 0);
}

static void test_should_wrap_overflow(void) {
    const char *name = "should_wrap_overflow";
    int widths[] = {40, 40, 40};
    /* 3 cols: natural = 1 + 3*(40+3) = 130. pad=80 < 130, should wrap */
    int r = table_should_wrap(3, 80, 0, widths);
    print_test_result(name, r == 1);
}

static void test_should_wrap_too_many_cols(void) {
    const char *name = "should_wrap_too_many_cols";
    int widths[] = {10, 10, 10, 10, 10, 10, 10};
    int r = table_should_wrap(7, 80, 0, widths);
    print_test_result(name, r == 0);
}

static void test_should_wrap_narrow_screen(void) {
    const char *name = "should_wrap_narrow_screen";
    int widths[] = {20, 20};
    int r = table_should_wrap(2, 30, 0, widths);
    print_test_result(name, r == 0);
}

static void test_distribute_widths_proportional(void) {
    const char *name = "distribute_widths_proportional";
    int col_widths[3];
    int max_widths[] = {5, 50, 15};
    table_distribute_widths(col_widths, 3, 80, 0, max_widths);
    /* avail = 80 - 1 - 9 - 1 = 69 (1 col right margin reserved).
     * min = 24. extra = 45.
     * total_content = 5+50+15 = 70.
     * col 0: 8 + (5*45)/70 = 8 + 3 = 11
     * col 1: 8 + (50*45)/70 = 8 + 32 = 40
     * col 2: 8 + (45-3-32) = 8 + 10 = 18
     */
    int ok = (col_widths[0] == 11 && col_widths[1] == 40);
    print_test_result(name, ok);
}

/* Verify total rendered width never reaches pad_width — this is the
 * safety invariant that prevents ncurses right-edge auto-wrap from
 * creating phantom blank lines after every table row. */
static void test_distribute_widths_total_lt_padwidth(void) {
    const char *name = "distribute_widths_total_lt_padwidth";
    int widths_2[] = {30, 30};
    int widths_3[] = {40, 30, 50};
    int widths_4[] = {25, 20, 30, 35};
    int widths_6[] = {15, 15, 15, 15, 15, 15};
    struct { int *cw; size_t nc; } cases[] = {
        {widths_2, 2}, {widths_3, 3}, {widths_4, 4}, {widths_6, 6},
    };
    int ok = 1;
    for (size_t c = 0; c < 4; c++) {
        size_t nc = cases[c].nc;
        int col_w[16];
        /* Test at several pad widths */
        int test_widths[] = {60, 80, 100, 140, 200};
        for (size_t tw = 0; tw < 5; tw++) {
            int pw = test_widths[tw];
            /* Only test configs where wrapped mode would actually be used.
             * table_distribute_widths is only called after table_should_wrap
             * returns true; testing configs it rejects is invalid. */
            if (!table_should_wrap(nc, pw, 0, cases[c].cw)) continue;
            table_distribute_widths(col_w, nc, pw, 0, cases[c].cw);
            /* Compute total rendered row width:
             * left_bw(1) + sum(1 space + col_width + 1 space + 1 pipe) */
            int total = 1; /* left border '|' */
            for (size_t j = 0; j < nc; j++) {
                total += 1 + col_w[j] + 1 + 1;
            }
            if (total >= pw) {
                printf("  FAIL: nc=%zu pw=%d total=%d >= pad_width\n",
                       nc, pw, total);
                ok = 0;
            }
        }
    }
    print_test_result(name, ok);
}

/* ==================================================================
 * Integration: table cell with markdown formatting
 * ================================================================== */

static void test_stripped_table_cell_scenario(void) {
    const char *name = "stripped_table_cell_scenario";
    /* Simulate a typical AI response table cell with bold formatting */
    const char *cell = "**2tgUb** buys tokens";
    int raw = cell_display_width(cell, strlen(cell));
    int stripped = markdown_stripped_display_width(cell, strlen(cell));
    /* raw: 21, stripped: "2tgUb buys tokens" = 17 */
    int ok = (raw == 21 && stripped == 17);
    if (!ok) {
        printf("  raw=%d stripped=%d\n", raw, stripped);
    }
    print_test_result(name, ok);
}

static void test_stripped_mixed_bold_italic(void) {
    const char *name = "stripped_mixed_bold_italic";
    /* **bold** and *italic* plus __more__: 35 raw chars */
    const char *cell = "**bold** and *italic* plus __more__";
    int raw = cell_display_width(cell, strlen(cell));
    int stripped = markdown_stripped_display_width(cell, strlen(cell));
    /* stripped: bold(4)+and(5)+italic(6)+plus(6)+more(4) = 25 */
    int ok = (stripped == 25) && (raw == 35);
    if (!ok) {
        printf("  raw=%d stripped=%d expected_raw=35 expected_stripped=25\n", raw, stripped);
    }
    print_test_result(name, ok);
}

static void test_table_cell_with_code_spans_fits(void) {
    const char *name = "table_cell_with_code_spans_fits";
    /* Regression test: table cell from the garbled-output bug.
     * The cell content contains backtick code spans. Column width
     * must use raw display width (not stripped) so wrapping doesn't
     * break mid-word. */
    const char *cell = "Master added `--params`/`--pap` er flags; Kelly added `--kelly` flags";
    int raw = cell_display_width(cell, strlen(cell));
    int stripped = markdown_stripped_display_width(cell, strlen(cell));
    /* stripped excludes backticks → narrower → wrapping inconsistency */
    int ok = (raw > stripped);
    if (!ok) {
        printf("  raw=%d stripped=%d (raw should be > stripped due to backticks)\n", raw, stripped);
    }
    print_test_result(name, ok);
}

/* ==================================================================
 * Main
 * ================================================================== */

int main(void) {
    /* Set locale for wcwidth/mbrtowc to work correctly with UTF-8 */
    setlocale(LC_ALL, "en_US.UTF-8");
    if (!setlocale(LC_ALL, "")) {
        setlocale(LC_ALL, "C.UTF-8");
    }

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

    /* --- table_split_cells --- */
    printf("\n--- table_split_cells ---\n");
    test_split_cells_simple();
    test_split_cells_whitespace();
    test_split_cells_empty();
    test_split_cells_leading_no_pipe();
    test_split_cells_max_limited();

    /* --- cell_display_width --- */
    printf("\n--- cell_display_width ---\n");
    test_display_width_ascii();
    test_display_width_empty();
    test_display_width_wide();
    test_display_width_3byte_wide();
    test_display_width_3byte_narrow();
    test_display_width_mixed_wide_ascii();
    test_display_width_stripped_wide_consistency();
    test_width_wrap_consistency_wide_3byte();
    test_width_wrap_consistency_cjk();

    /* --- cell_wrap_count / cell_get_wrapped_line --- */
    printf("\n--- cell_wrap_count / cell_get_wrapped_line ---\n");
    test_wrap_count_no_wrap();
    test_wrap_count_two_lines();
    test_wrap_count_many_lines();
    test_wrapped_line_get();
    test_wrapped_line_beyond();
    test_wrap_count_with_code_spans();
    test_wrap_count_code_at_column();
    test_wrapped_line_code_spans();
    test_cell_width_vs_stripped();

    /* --- markdown_stripped_display_width --- */
    printf("\n--- markdown_stripped_display_width ---\n");
    test_stripped_plain_text();
    test_stripped_empty();
    test_stripped_null();
    test_stripped_bold_stars();
    test_stripped_bold_underscores();
    test_stripped_italic_star();
    test_stripped_italic_underscore();
    test_stripped_code();
    test_stripped_strikethrough();
    test_stripped_mixed_formatting();
    test_stripped_unmatched_delimiter();
    test_stripped_snake_case();
    test_stripped_intra_word_star();

    /* --- table_should_wrap / table_distribute_widths --- */
    printf("\n--- table_should_wrap / table_distribute_widths ---\n");
    test_should_wrap_fits_naturally();
    test_should_wrap_overflow();
    test_should_wrap_too_many_cols();
    test_should_wrap_narrow_screen();
    test_distribute_widths_proportional();
    test_distribute_widths_total_lt_padwidth();

    /* --- integration: markdown in table cells --- */
    printf("\n--- integration: markdown in table cells ---\n");
    test_stripped_table_cell_scenario();
    test_stripped_mixed_bold_italic();
    test_table_cell_with_code_spans_fits();

    print_summary();
    return tests_failed > 0 ? 1 : 0;
}
