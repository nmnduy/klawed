/*
 * Unit Tests for Glob Tool with recursive glob support
 *
 * Tests the Glob tool's functionality including:
 * - Standard glob patterns (star.tex, src/star.js)
 * - Recursive glob patterns (starstar/star.tex, starstar/test_star.c)
 * - Pattern matching for star and ? wildcards
 *
 * Compilation: make test_glob_recursive
 * Usage: ./test_glob_recursive
 */

#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/types.h>
#include <limits.h>
#include <glob.h>
#include <cjson/cJSON.h>

// Minimal ConversationState for testing
typedef struct {
    char working_dir[PATH_MAX];
    char additional_dirs[10][PATH_MAX];
    int additional_dirs_count;
} TestConversationState;

// Test framework colors
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Forward declarations for helper functions
static int match_simple_glob(const char *filename, const char *pattern);
static void glob_recursive(const char *base_path, const char *rel_path,
                           const char *file_pattern, cJSON *files, int *total_count);
static int has_recursive_glob(const char *pattern);
static cJSON* tool_glob(cJSON *params, TestConversationState *state);

// Test utilities
static void print_test_header(const char *test_name) {
    printf("\n%s[TEST]%s %s\n", COLOR_CYAN, COLOR_RESET, test_name);
}

static void assert_test(int condition, const char *message) {
    tests_run++;
    if (condition) {
        tests_passed++;
        printf("  %s✓%s %s\n", COLOR_GREEN, COLOR_RESET, message);
    } else {
        tests_failed++;
        printf("  %s✗%s %s\n", COLOR_RED, COLOR_RESET, message);
    }
}

static void print_test_summary(void) {
    printf("\n%s========================================%s\n", COLOR_CYAN, COLOR_RESET);
    printf("Tests run: %d\n", tests_run);
    printf("%sPassed: %d%s\n", COLOR_GREEN, tests_passed, COLOR_RESET);
    if (tests_failed > 0) {
        printf("%sFailed: %d%s\n", COLOR_RED, tests_failed, COLOR_RESET);
    } else {
        printf("Failed: %d\n", tests_failed);
    }
    printf("%s========================================%s\n", COLOR_CYAN, COLOR_RESET);
}

// Test helper: Create test directory structure
static void setup_test_dirs(void) {
    system("rm -rf /tmp/glob_test");
    mkdir("/tmp/glob_test", 0755);
    mkdir("/tmp/glob_test/subdir", 0755);
    mkdir("/tmp/glob_test/subdir/nested", 0755);

    // Create test files
    FILE *f;
    f = fopen("/tmp/glob_test/file1.tex", "w"); if (f) fclose(f);
    f = fopen("/tmp/glob_test/subdir/file2.tex", "w"); if (f) fclose(f);
    f = fopen("/tmp/glob_test/subdir/nested/file3.tex", "w"); if (f) fclose(f);
    f = fopen("/tmp/glob_test/README.md", "w"); if (f) fclose(f);
    f = fopen("/tmp/glob_test/subdir/code.py", "w"); if (f) fclose(f);
}

// Test helper: Cleanup test directory
static void cleanup_test_dirs(void) {
    system("rm -rf /tmp/glob_test");
}

// Check if a character matches a glob pattern component
static int match_simple_glob(const char *filename, const char *pattern) {
    while (*pattern && *filename) {
        if (*pattern == '*') {
            while (*pattern == '*') pattern++;
            if (!*pattern) return 1;
            while (*filename) {
                if (match_simple_glob(filename, pattern)) return 1;
                filename++;
            }
            return 0;
        } else if (*pattern == '?') {
            if (!*filename) return 0;
            pattern++;
            filename++;
        } else {
            if (*pattern != *filename) return 0;
            pattern++;
            filename++;
        }
    }
    while (*pattern == '*') pattern++;
    return (*pattern == '\0' && *filename == '\0');
}

// Recursively traverse directory and match files against pattern
static void glob_recursive(const char *base_path, const char *rel_path,
                           const char *file_pattern, cJSON *files, int *total_count) {
    char current_dir[PATH_MAX];
    if (rel_path[0] == '\0') {
        strlcpy(current_dir, base_path, sizeof(current_dir));
    } else {
        snprintf(current_dir, sizeof(current_dir), "%s/%s", base_path, rel_path);
    }

    DIR *dir = opendir(current_dir);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }

        char full_path[PATH_MAX];
        char new_rel_path[PATH_MAX];
        if (rel_path[0] == '\0') {
            strlcpy(new_rel_path, entry->d_name, sizeof(new_rel_path));
        } else {
            snprintf(new_rel_path, sizeof(new_rel_path), "%s/%s", rel_path, entry->d_name);
        }
        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, new_rel_path);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            glob_recursive(base_path, new_rel_path, file_pattern, files, total_count);
        } else if (S_ISREG(st.st_mode)) {
            if (match_simple_glob(entry->d_name, file_pattern)) {
                cJSON_AddItemToArray(files, cJSON_CreateString(full_path));
                (*total_count)++;
            }
        }
    }

    closedir(dir);
}

// Check if pattern contains ** (recursive glob)
static int has_recursive_glob(const char *pattern) {
    const char *p = pattern;
    while ((p = strstr(p, "**")) != NULL) {
        if ((p == pattern || p[-1] == '/') &&
            (p[2] == '\0' || p[2] == '/')) {
            return 1;
        }
        p += 2;
    }
    return 0;
}

// Implementation of tool_glob
static cJSON* tool_glob(cJSON *params, TestConversationState *state) {
    const cJSON *pattern_json = cJSON_GetObjectItem(params, "pattern");
    if (!pattern_json || !cJSON_IsString(pattern_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'pattern' parameter");
        return error;
    }

    const char *pattern = pattern_json->valuestring;
    cJSON *result = cJSON_CreateObject();
    cJSON *files = cJSON_CreateArray();
    int total_count = 0;

    if (has_recursive_glob(pattern)) {
        const char *file_pattern = pattern;
        const char *starstar = strstr(pattern, "**/");
        if (starstar) {
            file_pattern = starstar + 3;
        } else {
            starstar = strstr(pattern, "**");
            if (starstar) {
                file_pattern = starstar + 2;
                if (*file_pattern == '/') file_pattern++;
            }
        }
        if (file_pattern[0] == '\0') {
            file_pattern = "*";
        }

        glob_recursive(state->working_dir, "", file_pattern, files, &total_count);

        for (int dir_idx = 0; dir_idx < state->additional_dirs_count; dir_idx++) {
            glob_recursive(state->additional_dirs[dir_idx], "", file_pattern, files, &total_count);
        }
    } else {
        char full_pattern[PATH_MAX];
        snprintf(full_pattern, sizeof(full_pattern), "%s/%s", state->working_dir, pattern);

        glob_t glob_result;
        int ret = glob(full_pattern, GLOB_TILDE, NULL, &glob_result);

        if (ret == 0) {
            for (size_t i = 0; i < glob_result.gl_pathc; i++) {
                cJSON_AddItemToArray(files, cJSON_CreateString(glob_result.gl_pathv[i]));
                total_count++;
            }
            globfree(&glob_result);
        }

        for (int dir_idx = 0; dir_idx < state->additional_dirs_count; dir_idx++) {
            snprintf(full_pattern, sizeof(full_pattern), "%s/%s",
                     state->additional_dirs[dir_idx], pattern);

            ret = glob(full_pattern, GLOB_TILDE, NULL, &glob_result);

            if (ret == 0) {
                for (size_t i = 0; i < glob_result.gl_pathc; i++) {
                    cJSON_AddItemToArray(files, cJSON_CreateString(glob_result.gl_pathv[i]));
                    total_count++;
                }
                globfree(&glob_result);
            }
        }
    }

    cJSON_AddItemToObject(result, "files", files);
    cJSON_AddNumberToObject(result, "count", total_count);

    return result;
}

// Test recursive glob starstar/.tex
static void test_recursive_tex_files(void) {
    print_test_header("Recursive glob starstar/.tex");

    TestConversationState state = {0};
    strlcpy(state.working_dir, "/tmp/glob_test", sizeof(state.working_dir));
    state.additional_dirs_count = 0;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "pattern", "**/*.tex");

    cJSON *result = tool_glob(params, &state);
    cJSON *files = cJSON_GetObjectItem(result, "files");
    cJSON *count = cJSON_GetObjectItem(result, "count");

    assert_test(cJSON_IsArray(files) && cJSON_GetArraySize(files) == 3,
                "Should find 3 .tex files recursively");
    assert_test(count && count->valueint == 3,
                "Count should be 3");

    cJSON_Delete(params);
    cJSON_Delete(result);
}

// Test recursive glob starstar/.md
static void test_recursive_md_files(void) {
    print_test_header("Recursive glob starstar/.md");

    TestConversationState state = {0};
    strlcpy(state.working_dir, "/tmp/glob_test", sizeof(state.working_dir));
    state.additional_dirs_count = 0;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "pattern", "**/*.md");

    cJSON *result = tool_glob(params, &state);
    cJSON *files = cJSON_GetObjectItem(result, "files");
    cJSON *count = cJSON_GetObjectItem(result, "count");

    assert_test(cJSON_IsArray(files) && cJSON_GetArraySize(files) == 1,
                "Should find 1 .md file recursively");
    assert_test(count && count->valueint == 1,
                "Count should be 1");

    cJSON_Delete(params);
    cJSON_Delete(result);
}

// Test recursive glob starstar/filestar.tex
static void test_recursive_pattern_with_prefix(void) {
    print_test_header("Recursive glob starstar/filestar.tex");

    TestConversationState state = {0};
    strlcpy(state.working_dir, "/tmp/glob_test", sizeof(state.working_dir));
    state.additional_dirs_count = 0;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "pattern", "**/file*.tex");

    cJSON *result = tool_glob(params, &state);
    cJSON *files = cJSON_GetObjectItem(result, "files");
    cJSON *count = cJSON_GetObjectItem(result, "count");

    assert_test(cJSON_IsArray(files) && cJSON_GetArraySize(files) == 3,
                "Should find 3 file*.tex files recursively");
    assert_test(count && count->valueint == 3,
                "Count should be 3");

    cJSON_Delete(params);
    cJSON_Delete(result);
}

// Test standard (non-recursive) glob star.tex
static void test_standard_glob_tex(void) {
    print_test_header("Standard glob star.tex (non-recursive)");

    TestConversationState state = {0};
    strlcpy(state.working_dir, "/tmp/glob_test", sizeof(state.working_dir));
    state.additional_dirs_count = 0;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "pattern", "*.tex");

    cJSON *result = tool_glob(params, &state);
    cJSON *files = cJSON_GetObjectItem(result, "files");
    cJSON *count = cJSON_GetObjectItem(result, "count");

    assert_test(cJSON_IsArray(files) && cJSON_GetArraySize(files) == 1,
                "Should find 1 .tex file in root only");
    assert_test(count && count->valueint == 1,
                "Count should be 1");

    cJSON_Delete(params);
    cJSON_Delete(result);
}

// Test standard glob subdir/star.tex
static void test_standard_glob_subdir(void) {
    print_test_header("Standard glob subdir/star.tex");

    TestConversationState state = {0};
    strlcpy(state.working_dir, "/tmp/glob_test", sizeof(state.working_dir));
    state.additional_dirs_count = 0;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "pattern", "subdir/*.tex");

    cJSON *result = tool_glob(params, &state);
    cJSON *files = cJSON_GetObjectItem(result, "files");
    cJSON *count = cJSON_GetObjectItem(result, "count");

    assert_test(cJSON_IsArray(files) && cJSON_GetArraySize(files) == 1,
                "Should find 1 .tex file in subdir");
    assert_test(count && count->valueint == 1,
                "Count should be 1");

    cJSON_Delete(params);
    cJSON_Delete(result);
}

// Test recursive glob starstar (match all files)
static void test_recursive_all_files(void) {
    print_test_header("Recursive glob starstar (all files)");

    TestConversationState state = {0};
    strlcpy(state.working_dir, "/tmp/glob_test", sizeof(state.working_dir));
    state.additional_dirs_count = 0;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "pattern", "**");

    cJSON *result = tool_glob(params, &state);
    cJSON *files = cJSON_GetObjectItem(result, "files");
    cJSON *count = cJSON_GetObjectItem(result, "count");

    assert_test(cJSON_IsArray(files) && cJSON_GetArraySize(files) == 5,
                "Should find all 5 files recursively");
    assert_test(count && count->valueint == 5,
                "Count should be 5");

    cJSON_Delete(params);
    cJSON_Delete(result);
}

int main(void) {
    printf("%s========================================%s\n", COLOR_CYAN, COLOR_RESET);
    printf("Glob Tool Unit Tests (with starstar support)\n");
    printf("%s========================================%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_dirs();

    test_recursive_tex_files();
    test_recursive_md_files();
    test_recursive_pattern_with_prefix();
    test_standard_glob_tex();
    test_standard_glob_subdir();
    test_recursive_all_files();

    cleanup_test_dirs();

    print_test_summary();

    return tests_failed > 0 ? 1 : 0;
}
