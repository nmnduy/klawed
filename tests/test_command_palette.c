/*
 * test_command_palette.c - Unit tests for command palette filtering logic
 *
 * Tests the case-insensitive substring matching used by the command palette
 * to filter slash commands by name and description.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

/* Test framework */
#define TEST_COLOR_RESET   "\033[0m"
#define TEST_COLOR_GREEN   "\033[32m"
#define TEST_COLOR_RED     "\033[31m"
#define TEST_COLOR_CYAN    "\033[36m"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

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

/* Simplified Command struct matching what commands.h defines */
typedef struct {
    const char *name;
    const char *description;
} TestCommand;

/*
 * filter_match: replicates the filtering logic from tui_render.c
 * Returns 1 if the command's name or description contains the filter string
 * (case-insensitive substring match).
 */
static int filter_match(const TestCommand *cmd, const char *filter, int filter_len) {
    if (!cmd || !cmd->name) return 0;
    if (filter_len == 0) return 1;

    const char *name = cmd->name;
    const char *desc = cmd->description ? cmd->description : "";
    int found = 0;

    /* Case-insensitive substring match in name */
    for (int ni = 0; name[ni] && !found; ni++) {
        int match = 1;
        for (int fi = 0; fi < filter_len && match; fi++) {
            char nc = name[ni + fi];
            if (!nc) { match = 0; break; }
            if (tolower((unsigned char)nc) != tolower((unsigned char)filter[fi])) {
                match = 0;
            }
        }
        if (match) found = 1;
    }

    /* Case-insensitive substring match in description */
    if (!found) {
        for (int di = 0; desc[di] && !found; di++) {
            int match = 1;
            for (int fi = 0; fi < filter_len && match; fi++) {
                char dc = desc[di + fi];
                if (!dc) { match = 0; break; }
                if (tolower((unsigned char)dc) != tolower((unsigned char)filter[fi])) {
                    match = 0;
                }
            }
            if (match) found = 1;
        }
    }

    return found;
}

/* Test commands fixture */
static TestCommand test_commands[] = {
    {"help",    "Show help information"},
    {"goal",    "Set or view a goal (Ralph mode)"},
    {"exit",    "Exit the application"},
    {"clear",   "Clear the conversation"},
    {"memory",  "Search or manage persistent memory"},
    {"add-dir", "Add a directory to the workspace"},
    {"model",   "Switch the AI model"},
    {"voice",   "Start voice input mode"},
    {NULL, NULL}
};

static int count_commands(void) {
    int count = 0;
    while (test_commands[count].name) count++;
    return count;
}

/* ========================================================================== */

static void test_empty_filter_all_match(void) {
    int total_count = count_commands();
    int matched = 0;
    for (int i = 0; i < total_count; i++) {
        if (filter_match(&test_commands[i], "", 0)) matched++;
    }
    print_test_result("Empty filter matches all commands",
                      matched == total_count && matched == 8);
}

static void test_filter_by_name_exact(void) {
    int passed = filter_match(&test_commands[1], "goal", 4)  /* "goal" matches */
              && !filter_match(&test_commands[2], "goal", 4); /* "exit" doesn't */
    print_test_result("Filter by exact name", passed);
}

static void test_filter_by_name_prefix(void) {
    int passed = filter_match(&test_commands[0], "hel", 3)  /* "help" starts with "hel" */
              && !filter_match(&test_commands[2], "hel", 3); /* "exit" doesn't */
    print_test_result("Filter by name prefix", passed);
}

static void test_filter_by_name_substring(void) {
    /* "add-dir" contains "dd" */
    int passed = filter_match(&test_commands[5], "dd", 2)
              && !filter_match(&test_commands[1], "dd", 2); /* "goal" doesn't */
    print_test_result("Filter by name substring", passed);
}

static void test_filter_case_insensitive(void) {
    /* "HELP" should match "help" */
    int passed = filter_match(&test_commands[0], "HELP", 4)
              && filter_match(&test_commands[0], "Help", 4)
              && filter_match(&test_commands[0], "help", 4)
              && filter_match(&test_commands[0], "HeLp", 4);
    print_test_result("Case-insensitive name matching", passed);
}

static void test_filter_by_description(void) {
    /* "memory" description contains "persistent" */
    int passed = filter_match(&test_commands[4], "persistent", 10)
              && !filter_match(&test_commands[0], "persistent", 10);
    print_test_result("Filter by description text", passed);
}

static void test_filter_by_description_case_insensitive(void) {
    /* "exit" description is "Exit the application" */
    int passed = filter_match(&test_commands[2], "APPLICATION", 11)
              && filter_match(&test_commands[2], "application", 11);
    print_test_result("Case-insensitive description matching", passed);
}

static void test_filter_zero_length(void) {
    /* Empty filter matches everything */
    int passed = 1;
    int total = count_commands();
    for (int i = 0; i < total && passed; i++) {
        if (!filter_match(&test_commands[i], "", 0)) passed = 0;
    }
    print_test_result("Zero-length filter matches all", passed);
}

static void test_filter_narrows_to_zero(void) {
    /* "xyzzy" shouldn't match anything */
    int total_count = count_commands();
    int matched = 0;
    for (int i = 0; i < total_count; i++) {
        if (filter_match(&test_commands[i], "xyzzy", 5)) matched++;
    }
    print_test_result("Non-matching filter returns zero results", matched == 0);
}

static void test_filter_null_command(void) {
    /* NULL command should not crash or match */
    int result = filter_match(NULL, "test", 4);
    print_test_result("NULL command returns 0", result == 0);
}

static void test_filter_null_name(void) {
    TestCommand bad = {NULL, "desc"};
    int result = filter_match(&bad, "test", 4);
    print_test_result("NULL name returns 0", result == 0);
}

/* ========================================================================== */

int main(void) {
    printf("\n" TEST_COLOR_CYAN "=== Command Palette Filtering Tests ===" TEST_COLOR_RESET "\n\n");

    test_empty_filter_all_match();
    test_filter_by_name_exact();
    test_filter_by_name_prefix();
    test_filter_by_name_substring();
    test_filter_case_insensitive();
    test_filter_by_description();
    test_filter_by_description_case_insensitive();
    test_filter_zero_length();
    test_filter_narrows_to_zero();
    test_filter_null_command();
    test_filter_null_name();

    print_summary();
    return tests_failed > 0 ? 1 : 0;
}
