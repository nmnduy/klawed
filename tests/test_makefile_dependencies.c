/*
 * test_makefile_dependencies.c - Unit tests for Makefile dependency validation
 * Tests that Makefile dependencies match actual source files
 *
 * This test helps catch issues where Makefile references non-existent
 * source files (like the zmq_reliable_queue issue that was fixed).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
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

// Helper to check if file exists
static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Helper to extract source file references from Makefile
static int extract_source_files(const char *makefile_path, char ***files, int *count) {
    FILE *fp = fopen(makefile_path, "r");
    if (!fp) {
        printf("Failed to open Makefile: %s\n", makefile_path);
        return 0;
    }

    char line[1024];
    *files = NULL;
    *count = 0;

    while (fgets(line, sizeof(line), fp)) {
        // Look for common patterns in Makefile that reference .c files
        const char *patterns[] = {
            ".c)",
            ".c ",
            ".c:",
            ".c\\",
        };

        for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
            char *pos = strstr(line, patterns[i]);
            if (pos) {
                // Try to extract the filename
                // Walk backwards to find start of filename
                char *start = pos;
                while (start > line && *(start-1) != ' ' && *(start-1) != '(' &&
                       *(start-1) != '$' && *(start-1) != '\t' && *(start-1) != '\n' &&
                       *(start-1) != '/' && *(start-1) != '.') {
                    start--;
                }

                // Walk forward to end of filename
                char *end = pos + strlen(patterns[i]);
                while (*end && *end != ' ' && *end != ')' && *end != ':' &&
                       *end != '\\' && *end != '\n' && *end != '\t' && *end != '\"' &&
                       *end != '\'') {
                    end++;
                }

                // Extract filename
                int len = (int)(end - start);
                if (len > 3 && len < 256) { // At least "x.c"
                    char filename[256];
                    strncpy(filename, start, len);
                    filename[len] = '\0';

                    // Check if it's a .c file (ends with .c)
                    if (len >= 3 && strcmp(filename + len - 2, ".c") == 0) {
                        // Check if it looks like a real filename (not part of a sentence)
                        int looks_like_file = 1;
                        for (int j = 0; j < len; j++) {
                            if (filename[j] == ' ' || filename[j] == '\t' ||
                                (filename[j] == '.' && j != len - 2)) {
                                looks_like_file = 0;
                                break;
                            }
                        }

                        if (looks_like_file) {
                            // Add to array
                            *files = realloc(*files, (size_t)(*count + 1) * sizeof(char*));
                            (*files)[*count] = strdup(filename);
                            (*count)++;
                        }
                    }
                }
            }
        }
    }

    fclose(fp);
    return 1;
}

// Test 1: Check that all .c files referenced in Makefile exist
static int test_makefile_references_exist(void) {
    printf("\n%s[TEST] test_makefile_references_exist%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    char **files = NULL;
    int count = 0;

    if (!extract_source_files("Makefile", &files, &count)) {
        return 0;
    }

    printf("Found %d .c file references in Makefile\n", count);

    int all_exist = 1;
    for (int i = 0; i < count; i++) {
        if (!file_exists(files[i])) {
            printf("%s  Missing file: %s%s\n", TEST_COLOR_RED, files[i], TEST_COLOR_RESET);
            all_exist = 0;
        } else {
            printf("  ✓ %s\n", files[i]);
        }
        free(files[i]);
    }
    free(files);

    ASSERT(all_exist == 1);
    return 1;
}

// Test 2: Check for common problematic patterns
static int test_problematic_patterns(void) {
    printf("\n%s[TEST] test_problematic_patterns%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    FILE *fp = fopen("Makefile", "r");
    ASSERT(fp != NULL);

    char line[1024];
    int line_num = 0;
    int found_issues = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;

        // Check for zmq_reliable_queue which was removed
        if (strstr(line, "zmq_reliable_queue")) {
            printf("%s  Line %d: Reference to removed file 'zmq_reliable_queue'%s\n",
                   TEST_COLOR_RED, line_num, TEST_COLOR_RESET);
            found_issues++;
        }

        // Check for common build issues
        if (strstr(line, "$(ZMQ_RELIABLE_QUEUE_SRC)") ||
            strstr(line, "$(ZMQ_RELIABLE_QUEUE_OBJ)")) {
            printf("%s  Line %d: Reference to removed variable%s\n",
                   TEST_COLOR_RED, line_num, TEST_COLOR_RESET);
            found_issues++;
        }
    }

    fclose(fp);

    if (found_issues == 0) {
        printf("  No problematic patterns found\n");
    }

    ASSERT(found_issues == 0);
    return 1;
}

// Test 3: Verify source directory structure
static int test_source_directory_structure(void) {
    printf("\n%s[TEST] test_source_directory_structure%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // Check that src/ directory exists
    ASSERT(file_exists("src"));

    // Check for common source directories
    const char *dirs[] = {"src", "tests"};
    for (size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); i++) {
        ASSERT(file_exists(dirs[i]));
        printf("  ✓ Directory exists: %s/\n", dirs[i]);
    }

    return 1;
}

// Test 4: Simple build test
static int test_simple_build(void) {
    printf("\n%s[TEST] test_simple_build%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    // Try to run make check-deps (should work if Makefile is valid)
    printf("  Running 'make check-deps'...\n");
    int result = system("make check-deps > /dev/null 2>&1");

    if (result != 0) {
        printf("%s  'make check-deps' failed with code %d%s\n",
               TEST_COLOR_RED, result, TEST_COLOR_RESET);
        return 0;
    }

    printf("  ✓ 'make check-deps' succeeded\n");
    return 1;
}

// Test 5: Check for orphaned object file references
static int test_orphaned_object_files(void) {
    printf("\n%s[TEST] test_orphaned_object_files%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    FILE *fp = fopen("Makefile", "r");
    ASSERT(fp != NULL);

    char line[1024];
    int found_zmq_reliable_queue_obj = 0;

    // Look for zmq_reliable_queue_all.o which was in the sanitize-all target
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "zmq_reliable_queue_all.o")) {
            found_zmq_reliable_queue_obj = 1;
            break;
        }
    }

    fclose(fp);

    if (found_zmq_reliable_queue_obj) {
        printf("%s  Found orphaned reference to zmq_reliable_queue_all.o%s\n",
               TEST_COLOR_RED, TEST_COLOR_RESET);
        return 0;
    }

    printf("  ✓ No orphaned object file references found\n");
    return 1;
}

int main(void) {
    printf("%sRunning Makefile Dependency Tests%s\n", TEST_COLOR_CYAN, TEST_COLOR_RESET);

    print_test_result("test_makefile_references_exist", test_makefile_references_exist());
    print_test_result("test_problematic_patterns", test_problematic_patterns());
    print_test_result("test_source_directory_structure", test_source_directory_structure());
    print_test_result("test_simple_build", test_simple_build());
    print_test_result("test_orphaned_object_files", test_orphaned_object_files());

    print_summary();

    return (tests_failed == 0) ? 0 : 1;
}
