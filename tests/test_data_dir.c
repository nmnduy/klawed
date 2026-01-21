/*
 * Unit Tests for Data Directory Utilities
 *
 * Tests the data_dir implementation including:
 * - data_dir_get_base() returns default when env not set
 * - data_dir_get_base() returns env value when set
 * - data_dir_build_path() with various inputs
 * - data_dir_build_path() null/empty handling
 * - data_dir_build_path() buffer overflow protection
 * - data_dir_ensure() directory creation
 *
 * Compilation: make test-data-dir
 * Usage: ./build/test_data_dir
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "../src/data_dir.h"

/* Test framework colors */
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Test utilities */
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

/* Helper to check if directory exists */
static int dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

/* Helper to remove directory recursively (for cleanup) */
static int rmdir_recursive(const char *path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' 2>/dev/null", path);
    return system(cmd);
}

/* ============================================================================
 * Tests for data_dir_get_base()
 * ============================================================================ */

static void test_get_base_default(void) {
    const char *test_name = "data_dir_get_base returns default when env not set";

    /* Clear the env var */
    unsetenv("KLAWED_DATA_DIR");

    const char *base = data_dir_get_base();

    int passed = (base != NULL && strcmp(base, ".klawed") == 0);

    print_test_result(test_name, passed);
}

static void test_get_base_env_set(void) {
    const char *test_name = "data_dir_get_base returns env value when set";

    setenv("KLAWED_DATA_DIR", "/custom/data/dir", 1);

    const char *base = data_dir_get_base();

    int passed = (base != NULL && strcmp(base, "/custom/data/dir") == 0);

    /* Cleanup */
    unsetenv("KLAWED_DATA_DIR");

    print_test_result(test_name, passed);
}

static void test_get_base_env_empty(void) {
    const char *test_name = "data_dir_get_base returns default when env is empty string";

    setenv("KLAWED_DATA_DIR", "", 1);

    const char *base = data_dir_get_base();

    int passed = (base != NULL && strcmp(base, ".klawed") == 0);

    /* Cleanup */
    unsetenv("KLAWED_DATA_DIR");

    print_test_result(test_name, passed);
}

/* ============================================================================
 * Tests for data_dir_build_path()
 * ============================================================================ */

static void test_build_path_null_buffer(void) {
    const char *test_name = "data_dir_build_path returns -1 with null buffer";

    int result = data_dir_build_path(NULL, 100, "subpath");

    int passed = (result == -1);

    print_test_result(test_name, passed);
}

static void test_build_path_zero_size(void) {
    const char *test_name = "data_dir_build_path returns -1 with zero buffer size";

    char buf[100];
    int result = data_dir_build_path(buf, 0, "subpath");

    int passed = (result == -1);

    print_test_result(test_name, passed);
}

static void test_build_path_no_subpath(void) {
    const char *test_name = "data_dir_build_path with NULL subpath returns base only";

    unsetenv("KLAWED_DATA_DIR");

    char buf[100];
    int result = data_dir_build_path(buf, sizeof(buf), NULL);

    int passed = (result == 0 && strcmp(buf, ".klawed") == 0);

    print_test_result(test_name, passed);
}

static void test_build_path_empty_subpath(void) {
    const char *test_name = "data_dir_build_path with empty subpath returns base only";

    unsetenv("KLAWED_DATA_DIR");

    char buf[100];
    int result = data_dir_build_path(buf, sizeof(buf), "");

    int passed = (result == 0 && strcmp(buf, ".klawed") == 0);

    print_test_result(test_name, passed);
}

static void test_build_path_simple_subpath(void) {
    const char *test_name = "data_dir_build_path with simple subpath";

    unsetenv("KLAWED_DATA_DIR");

    char buf[100];
    int result = data_dir_build_path(buf, sizeof(buf), "logs");

    int passed = (result == 0 && strcmp(buf, ".klawed/logs") == 0);

    print_test_result(test_name, passed);
}

static void test_build_path_nested_subpath(void) {
    const char *test_name = "data_dir_build_path with nested subpath";

    unsetenv("KLAWED_DATA_DIR");

    char buf[100];
    int result = data_dir_build_path(buf, sizeof(buf), "logs/klawed.log");

    int passed = (result == 0 && strcmp(buf, ".klawed/logs/klawed.log") == 0);

    print_test_result(test_name, passed);
}

static void test_build_path_custom_base(void) {
    const char *test_name = "data_dir_build_path with custom base dir";

    setenv("KLAWED_DATA_DIR", "/tmp/custom_klawed", 1);

    char buf[100];
    int result = data_dir_build_path(buf, sizeof(buf), "config.json");

    int passed = (result == 0 && strcmp(buf, "/tmp/custom_klawed/config.json") == 0);

    unsetenv("KLAWED_DATA_DIR");

    print_test_result(test_name, passed);
}

static void test_build_path_buffer_too_small_for_base(void) {
    const char *test_name = "data_dir_build_path returns -1 when buffer too small for base";

    unsetenv("KLAWED_DATA_DIR");

    /* .klawed is 7 chars, so buffer of 5 should fail */
    char buf[5];
    int result = data_dir_build_path(buf, sizeof(buf), NULL);

    int passed = (result == -1);

    print_test_result(test_name, passed);
}

static void test_build_path_buffer_too_small_for_subpath(void) {
    const char *test_name = "data_dir_build_path returns -1 when buffer too small for subpath";

    unsetenv("KLAWED_DATA_DIR");

    /* .klawed/logs is 12 chars, so buffer of 10 should fail */
    char buf[10];
    int result = data_dir_build_path(buf, sizeof(buf), "logs");

    int passed = (result == -1);

    print_test_result(test_name, passed);
}

static void test_build_path_exact_size(void) {
    const char *test_name = "data_dir_build_path with exact buffer size (needs +1 for NUL)";

    unsetenv("KLAWED_DATA_DIR");

    /* .klawed/logs is 12 chars, need 13 for NUL terminator */
    char buf[13];
    int result = data_dir_build_path(buf, sizeof(buf), "logs");

    int passed = (result == 0 && strcmp(buf, ".klawed/logs") == 0);

    print_test_result(test_name, passed);
}

/* ============================================================================
 * Tests for data_dir_ensure()
 * ============================================================================ */

static void test_ensure_base_only(void) {
    const char *test_name = "data_dir_ensure creates base directory";

    /* Use temp directory to avoid polluting working directory */
    char test_dir[] = "/tmp/test_data_dir_XXXXXX";
    if (mkdtemp(test_dir) == NULL) {
        print_test_result(test_name, 0);
        return;
    }

    setenv("KLAWED_DATA_DIR", test_dir, 1);

    /* Remove the temp dir so ensure can create it */
    rmdir(test_dir);

    int result = data_dir_ensure(NULL);

    int passed = (result == 0 && dir_exists(test_dir));

    /* Cleanup */
    rmdir_recursive(test_dir);
    unsetenv("KLAWED_DATA_DIR");

    print_test_result(test_name, passed);
}

static void test_ensure_subdirectory(void) {
    const char *test_name = "data_dir_ensure creates subdirectory";

    char test_dir[] = "/tmp/test_data_dir_XXXXXX";
    if (mkdtemp(test_dir) == NULL) {
        print_test_result(test_name, 0);
        return;
    }

    setenv("KLAWED_DATA_DIR", test_dir, 1);

    int result = data_dir_ensure("logs");

    char expected_path[256];
    snprintf(expected_path, sizeof(expected_path), "%s/logs", test_dir);

    int passed = (result == 0 && dir_exists(expected_path));

    /* Cleanup */
    rmdir_recursive(test_dir);
    unsetenv("KLAWED_DATA_DIR");

    print_test_result(test_name, passed);
}

static void test_ensure_nested_subdirectory(void) {
    const char *test_name = "data_dir_ensure creates nested subdirectories";

    char test_dir[] = "/tmp/test_data_dir_XXXXXX";
    if (mkdtemp(test_dir) == NULL) {
        print_test_result(test_name, 0);
        return;
    }

    setenv("KLAWED_DATA_DIR", test_dir, 1);

    int result = data_dir_ensure("mcp/logs/nested");

    char expected_path[256];
    snprintf(expected_path, sizeof(expected_path), "%s/mcp/logs/nested", test_dir);

    int passed = (result == 0 && dir_exists(expected_path));

    /* Cleanup */
    rmdir_recursive(test_dir);
    unsetenv("KLAWED_DATA_DIR");

    print_test_result(test_name, passed);
}

static void test_ensure_existing_directory(void) {
    const char *test_name = "data_dir_ensure succeeds for existing directory";

    char test_dir[] = "/tmp/test_data_dir_XXXXXX";
    if (mkdtemp(test_dir) == NULL) {
        print_test_result(test_name, 0);
        return;
    }

    setenv("KLAWED_DATA_DIR", test_dir, 1);

    /* Create the subdir first */
    char subdir[256];
    snprintf(subdir, sizeof(subdir), "%s/logs", test_dir);
    mkdir(subdir, 0755);

    /* Ensure should succeed even though it already exists */
    int result = data_dir_ensure("logs");

    int passed = (result == 0 && dir_exists(subdir));

    /* Cleanup */
    rmdir_recursive(test_dir);
    unsetenv("KLAWED_DATA_DIR");

    print_test_result(test_name, passed);
}

static void test_ensure_with_empty_subpath(void) {
    const char *test_name = "data_dir_ensure with empty subpath creates base";

    char test_dir[] = "/tmp/test_data_dir_XXXXXX";
    if (mkdtemp(test_dir) == NULL) {
        print_test_result(test_name, 0);
        return;
    }

    /* Use a subdirectory that doesn't exist */
    char new_dir[300];
    snprintf(new_dir, sizeof(new_dir), "%s/subdir", test_dir);

    setenv("KLAWED_DATA_DIR", new_dir, 1);

    int result = data_dir_ensure("");

    int passed = (result == 0 && dir_exists(new_dir));

    /* Cleanup */
    rmdir_recursive(test_dir);
    unsetenv("KLAWED_DATA_DIR");

    print_test_result(test_name, passed);
}

/* ============================================================================
 * Integration tests
 * ============================================================================ */

static void test_integration_full_workflow(void) {
    const char *test_name = "Integration: full workflow with custom base";

    char test_dir[] = "/tmp/test_data_dir_int_XXXXXX";
    if (mkdtemp(test_dir) == NULL) {
        print_test_result(test_name, 0);
        return;
    }

    setenv("KLAWED_DATA_DIR", test_dir, 1);

    int passed = 1;

    /* Check base is correct */
    const char *base = data_dir_get_base();
    if (strcmp(base, test_dir) != 0) {
        passed = 0;
    }

    /* Build path for config */
    char config_path[256];
    if (data_dir_build_path(config_path, sizeof(config_path), "config.json") != 0) {
        passed = 0;
    }

    char expected_config[256];
    snprintf(expected_config, sizeof(expected_config), "%s/config.json", test_dir);
    if (strcmp(config_path, expected_config) != 0) {
        passed = 0;
    }

    /* Ensure logs directory exists */
    if (data_dir_ensure("logs") != 0) {
        passed = 0;
    }

    char logs_path[256];
    snprintf(logs_path, sizeof(logs_path), "%s/logs", test_dir);
    if (!dir_exists(logs_path)) {
        passed = 0;
    }

    /* Build path for log file */
    char log_file_path[256];
    if (data_dir_build_path(log_file_path, sizeof(log_file_path), "logs/klawed.log") != 0) {
        passed = 0;
    }

    char expected_log[256];
    snprintf(expected_log, sizeof(expected_log), "%s/logs/klawed.log", test_dir);
    if (strcmp(log_file_path, expected_log) != 0) {
        passed = 0;
    }

    /* Cleanup */
    rmdir_recursive(test_dir);
    unsetenv("KLAWED_DATA_DIR");

    print_test_result(test_name, passed);
}

/* ============================================================================
 * Main test runner
 * ============================================================================ */

int main(void) {
    printf(COLOR_CYAN "Running Data Directory Unit Tests\n" COLOR_RESET);
    printf("==================================\n\n");

    printf(COLOR_YELLOW "data_dir_get_base() tests:\n" COLOR_RESET);
    test_get_base_default();
    test_get_base_env_set();
    test_get_base_env_empty();

    printf("\n" COLOR_YELLOW "data_dir_build_path() tests:\n" COLOR_RESET);
    test_build_path_null_buffer();
    test_build_path_zero_size();
    test_build_path_no_subpath();
    test_build_path_empty_subpath();
    test_build_path_simple_subpath();
    test_build_path_nested_subpath();
    test_build_path_custom_base();
    test_build_path_buffer_too_small_for_base();
    test_build_path_buffer_too_small_for_subpath();
    test_build_path_exact_size();

    printf("\n" COLOR_YELLOW "data_dir_ensure() tests:\n" COLOR_RESET);
    test_ensure_base_only();
    test_ensure_subdirectory();
    test_ensure_nested_subdirectory();
    test_ensure_existing_directory();
    test_ensure_with_empty_subpath();

    printf("\n" COLOR_YELLOW "Integration tests:\n" COLOR_RESET);
    test_integration_full_workflow();

    print_summary();

    return tests_failed > 0 ? 1 : 0;
}
