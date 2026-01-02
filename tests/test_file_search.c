/*
 * Unit Tests for File Search Fuzzy Matching
 *
 * Tests the fuzzy scoring algorithm added in commit 1be3658.
 * Focuses on:
 * - Basic fuzzy matching functionality
 * - Scoring algorithm correctness
 * - Edge cases and boundary conditions
 * - Sorting by score
 *
 * Compilation: make test
 * Usage: ./test_file_search
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <assert.h>

// Include the file_search header to access internal functions
// We'll need to compile with -DTEST_BUILD to expose static functions
#include "../src/file_search.h"

// Test framework colors (avoid ncurses conflicts)
#define TEST_COLOR_RESET "\033[0m"
#define TEST_COLOR_GREEN "\033[32m"
#define TEST_COLOR_RED "\033[31m"
#define TEST_COLOR_YELLOW "\033[33m"
#define TEST_COLOR_CYAN "\033[36m"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Forward declarations of internal functions (exposed when compiled with -DTEST_BUILD)
// These functions are defined in file_search.c and made non-static when TEST_BUILD is defined
int fuzzy_score(const char *haystack, const char *needle);
int compare_results(const void *a, const void *b);

// Test utilities
static void print_test_header(const char *test_name) {
    printf("%s[TEST] %s%s\n", TEST_COLOR_CYAN, test_name, TEST_COLOR_RESET);
}

static void print_test_result(const char *test_name, int passed) {
    tests_run++;
    if (passed) {
        tests_passed++;
        printf("%s  ✓ %s%s\n", TEST_COLOR_GREEN, test_name, TEST_COLOR_RESET);
    } else {
        tests_failed++;
        printf("%s  ✗ %s%s\n", TEST_COLOR_RED, test_name, TEST_COLOR_RESET);
    }
}

static void assert_int_equal(int actual, int expected, const char *test_name) {
    if (actual == expected) {
        print_test_result(test_name, 1);
    } else {
        printf("%s    Expected: %d, Got: %d%s\n", TEST_COLOR_YELLOW, expected, actual, TEST_COLOR_RESET);
        print_test_result(test_name, 0);
    }
}

static void assert_int_greater(int actual, int min, const char *test_name) {
    if (actual > min) {
        print_test_result(test_name, 1);
    } else {
        printf("%s    Expected > %d, Got: %d%s\n", TEST_COLOR_YELLOW, min, actual, TEST_COLOR_RESET);
        print_test_result(test_name, 0);
    }
}

static void assert_int_less(int actual, int max, const char *test_name) {
    if (actual < max) {
        print_test_result(test_name, 1);
    } else {
        printf("%s    Expected < %d, Got: %d%s\n", TEST_COLOR_YELLOW, max, actual, TEST_COLOR_RESET);
        print_test_result(test_name, 0);
    }
}



// Test cases
static void test_empty_pattern(void) {
    print_test_header("Empty pattern matches everything");

    // Empty pattern should return minimal score (1)
    assert_int_equal(fuzzy_score("any_file.txt", ""), 1, "Empty pattern returns 1");
    assert_int_equal(fuzzy_score("", ""), 1, "Empty haystack and pattern returns 1");
    assert_int_equal(fuzzy_score("src/main.c", NULL), 1, "NULL pattern returns 1");
}

static void test_exact_match(void) {
    print_test_header("Exact matches get highest scores");

    int score1 = fuzzy_score("main.c", "main.c");
    int score2 = fuzzy_score("src/main.c", "main.c");

    // Exact match should have high score
    assert_int_greater(score1, 50, "Exact match has high score");
    assert_int_greater(score2, 30, "Partial path match has good score");

    // Case-insensitive exact match
    int score3 = fuzzy_score("MAIN.C", "main.c");
    assert_int_greater(score3, 40, "Case-insensitive exact match has good score");
}

static void test_fuzzy_matching(void) {
    print_test_header("Fuzzy matching basics");

    // "main" should match "main.c"
    int score1 = fuzzy_score("main.c", "main");
    assert_int_greater(score1, 0, "'main' matches 'main.c'");

    // "mc" should match "main.c" (subsequence)
    int score2 = fuzzy_score("main.c", "mc");
    assert_int_greater(score2, 0, "'mc' matches 'main.c' as subsequence");

    // "xyz" should NOT match "main.c"
    int score3 = fuzzy_score("main.c", "xyz");
    assert_int_equal(score3, 0, "'xyz' does not match 'main.c'");
}

static void test_scoring_priorities(void) {
    print_test_header("Scoring priorities");

    // Test that matches at word boundaries get bonus
    int score1 = fuzzy_score("src/main.c", "main");
    int score2 = fuzzy_score("amain.c", "main");  // 'main' not at word boundary

    // Score1 should be higher due to separator bonus
    assert_int_greater(score1, score2, "Word boundary match scores higher");

    // Test consecutive match bonus
    int score3 = fuzzy_score("main.c", "mai");  // consecutive
    int score4 = fuzzy_score("mxaixn.c", "mai"); // non-consecutive

    assert_int_greater(score3, score4, "Consecutive matches score higher");
}

static void test_case_sensitivity(void) {
    print_test_header("Case sensitivity handling");

    // Exact case match should score slightly higher
    int score1 = fuzzy_score("Main.c", "Main.c");
    int score2 = fuzzy_score("Main.c", "main.c");

    // Case mismatch gets penalty, so exact case should be higher
    assert_int_greater(score1, score2, "Exact case match scores higher");

    // But both should match
    assert_int_greater(score1, 0, "Exact case matches");
    assert_int_greater(score2, 0, "Case-insensitive matches");
}

static void test_path_scoring(void) {
    print_test_header("Path scoring preferences");

    // Test path length penalty
    // The algorithm subtracts hlen/100, so very long paths get a penalty
    int score1 = fuzzy_score("short.c", "c");  // length 7, penalty 0
    int score2 = fuzzy_score("very/long/path/to/file.c", "c");  // length 27, penalty 0

    // Both should have the same score since penalty is 0 for paths < 100 chars
    assert_int_equal(score1, 10, "Short path score is 10");
    assert_int_equal(score2, 10, "Long path score is also 10 (penalty is 0 for <100 chars)");

    // Test with a very long path (>100 chars) to see the penalty
    char long_path[150];
    memset(long_path, 'a', 149);
    long_path[149] = '\0';
    strcpy(long_path + 140, ".c");  // Make it end with .c so 'c' matches

    int score3 = fuzzy_score(long_path, "c");
    // Path length is ~150, penalty is 150/100 = 1
    // Base score is 10, minus penalty of 1 = 9
    assert_int_equal(score3, 9, "Very long path gets penalty");
}

static void test_compare_results(void) {
    print_test_header("Result comparison for sorting");

    // Create test results with allocated strings
    FileSearchResult r1 = {.path = strdup("test1.c"), .score = 100};
    FileSearchResult r2 = {.path = strdup("test2.c"), .score = 200};
    FileSearchResult r3 = {.path = strdup("abc.c"), .score = 100};
    FileSearchResult r4 = {.path = strdup("def.c"), .score = 100};

    // Higher score should come first (descending order)
    int cmp1 = compare_results(&r2, &r1);
    assert_int_less(cmp1, 0, "Higher score comes first (r2.score=200 > r1.score=100)");

    // Lower score should come after
    int cmp2 = compare_results(&r1, &r2);
    assert_int_greater(cmp2, 0, "Lower score comes after");

    // Same score: alphabetical order (case-insensitive)
    int cmp3 = compare_results(&r3, &r4);
    assert_int_less(cmp3, 0, "Same score: 'abc.c' comes before 'def.c'");

    int cmp4 = compare_results(&r4, &r3);
    assert_int_greater(cmp4, 0, "Same score: 'def.c' comes after 'abc.c'");

    // Clean up
    free(r1.path);
    free(r2.path);
    free(r3.path);
    free(r4.path);
}

static void test_edge_cases(void) {
    print_test_header("Edge cases");

    // Very long pattern (should be limited by FUZZY_MAX_PATTERN)
    char long_pattern[300];
    memset(long_pattern, 'a', 299);
    long_pattern[299] = '\0';

    int score = fuzzy_score("test", long_pattern);
    assert_int_equal(score, 0, "Very long pattern doesn't match short string");

    // Empty haystack with non-empty pattern
    int score2 = fuzzy_score("", "test");
    assert_int_equal(score2, 0, "Empty haystack doesn't match non-empty pattern");

    // Pattern longer than haystack
    int score3 = fuzzy_score("ab", "abcd");
    assert_int_equal(score3, 0, "Pattern longer than haystack doesn't match");
}

static void test_real_world_examples(void) {
    print_test_header("Real-world examples");

    // Common file patterns
    assert_int_greater(fuzzy_score("src/main.c", "main"), 0, "Finds main in src/main.c");
    assert_int_greater(fuzzy_score("include/header.h", "head"), 0, "Finds head in header.h");
    assert_int_greater(fuzzy_score("Makefile", "make"), 0, "Finds make in Makefile");
    assert_int_greater(fuzzy_score("README.md", "read"), 0, "Finds read in README.md");

    // With separators
    assert_int_greater(fuzzy_score("src_utils.c", "utils"), 0, "Finds utils after underscore");
    assert_int_greater(fuzzy_score("test-unit.c", "unit"), 0, "Finds unit after hyphen");
}

// Run all tests
int main(void) {
    printf("\n%s========================================%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);
    printf("%s  File Search Fuzzy Matching Tests%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);
    printf("%s========================================%s\n\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    test_empty_pattern();
    test_exact_match();
    test_fuzzy_matching();
    test_scoring_priorities();
    test_case_sensitivity();
    test_path_scoring();
    test_compare_results();
    test_edge_cases();
    test_real_world_examples();

    printf("\n%s========================================%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);
    printf("Summary: %d tests run\n", tests_run);
    if (tests_failed == 0) {
        printf("%sAll %d tests passed!%s\n", TEST_COLOR_GREEN, tests_passed, TEST_COLOR_RESET);
    } else {
        printf("%s%d passed, %d failed%s\n",
               tests_failed > 0 ? TEST_COLOR_RED : TEST_COLOR_YELLOW,
               tests_passed, tests_failed, TEST_COLOR_RESET);
    }
    printf("%s========================================%s\n\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    return tests_failed == 0 ? 0 : 1;
}
