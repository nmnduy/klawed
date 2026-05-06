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
                           const char *file_pattern, cJSON *files,
                           int *total_found, int *returned_count, int max_results);
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

// Check if a directory should be excluded from recursive traversal
static int is_excluded_dir(const char *name) {
    static const char *excluded[] = {
        ".git", ".svn", ".hg",
        "node_modules", "bower_components", "vendor",
        "build", "dist", "target",
        ".cache", ".venv", "venv", "__pycache__",
        ".klawed",
        NULL
    };
    for (int i = 0; excluded[i] != NULL; i++) {
        if (strcmp(name, excluded[i]) == 0) {
            return 1;
        }
    }
    return 0;
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
                           const char *file_pattern, cJSON *files,
                           int *total_found, int *returned_count, int max_results) {
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

        if (is_excluded_dir(entry->d_name)) {
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
            glob_recursive(base_path, new_rel_path, file_pattern, files,
                           total_found, returned_count, max_results);
        } else if (S_ISREG(st.st_mode)) {
            if (match_simple_glob(entry->d_name, file_pattern)) {
                (*total_found)++;
                if (*returned_count < max_results) {
                    cJSON_AddItemToArray(files, cJSON_CreateString(full_path));
                    (*returned_count)++;
                }
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
    int total_found = 0;
    int returned_count = 0;
    int truncated = 0;
    int max_results = 200;  // Default limit for tests
    const char *max_env = getenv("KLAWED_GLOB_MAX_RESULTS");
    if (max_env) {
        int max_val = atoi(max_env);
        if (max_val > 0) {
            max_results = max_val;
        }
    }

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

        glob_recursive(state->working_dir, "", file_pattern, files,
                       &total_found, &returned_count, max_results);

        for (int dir_idx = 0; dir_idx < state->additional_dirs_count; dir_idx++) {
            glob_recursive(state->additional_dirs[dir_idx], "", file_pattern, files,
                           &total_found, &returned_count, max_results);
        }
    } else {
        char full_pattern[PATH_MAX];
        snprintf(full_pattern, sizeof(full_pattern), "%s/%s", state->working_dir, pattern);

        glob_t glob_result;
        int ret = glob(full_pattern, GLOB_TILDE, NULL, &glob_result);

        if (ret == 0) {
            for (size_t i = 0; i < glob_result.gl_pathc && returned_count < max_results; i++) {
                cJSON_AddItemToArray(files, cJSON_CreateString(glob_result.gl_pathv[i]));
                returned_count++;
            }
            total_found += (int)glob_result.gl_pathc;
            globfree(&glob_result);
        }

        for (int dir_idx = 0; dir_idx < state->additional_dirs_count; dir_idx++) {
            snprintf(full_pattern, sizeof(full_pattern), "%s/%s",
                     state->additional_dirs[dir_idx], pattern);

            ret = glob(full_pattern, GLOB_TILDE, NULL, &glob_result);

            if (ret == 0) {
                for (size_t i = 0; i < glob_result.gl_pathc && returned_count < max_results; i++) {
                    cJSON_AddItemToArray(files, cJSON_CreateString(glob_result.gl_pathv[i]));
                    returned_count++;
                }
                total_found += (int)glob_result.gl_pathc;
                globfree(&glob_result);
            }
        }
    }

    if (total_found > returned_count) {
        truncated = 1;
    }

    cJSON_AddItemToObject(result, "files", files);
    cJSON_AddNumberToObject(result, "count", returned_count);
    cJSON_AddNumberToObject(result, "total_matches", total_found);
    cJSON_AddBoolToObject(result, "truncated", truncated);

    if (truncated) {
        char warning[256];
        snprintf(warning, sizeof(warning),
                 "Results truncated: showing %d/%d matches. Use KLAWED_GLOB_MAX_RESULTS to adjust limit, or refine your pattern.",
                 returned_count, total_found);
        cJSON_AddStringToObject(result, "warning", warning);
    }

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

// Test is_excluded_dir directly (core string list processor)
static void test_is_excluded_dir_directly(void) {
    print_test_header("is_excluded_dir direct string matching");

    assert_test(is_excluded_dir(".git") == 1,
                ".git should be excluded");
    assert_test(is_excluded_dir("node_modules") == 1,
                "node_modules should be excluded");
    assert_test(is_excluded_dir("build") == 1,
                "build should be excluded");
    assert_test(is_excluded_dir("dist") == 1,
                "dist should be excluded");
    assert_test(is_excluded_dir("target") == 1,
                "target should be excluded");
    assert_test(is_excluded_dir("__pycache__") == 1,
                "__pycache__ should be excluded");
    assert_test(is_excluded_dir(".klawed") == 1,
                ".klawed should be excluded");
    assert_test(is_excluded_dir("src") == 0,
                "src should NOT be excluded");
    assert_test(is_excluded_dir("docs") == 0,
                "docs should NOT be excluded");
    assert_test(is_excluded_dir("lib") == 0,
                "lib should NOT be excluded");
    assert_test(is_excluded_dir(".github") == 0,
                ".github should NOT be excluded");
}

// Test excluded directories are skipped by glob_recursive
static void test_excluded_directories_skipped(void) {
    print_test_header("Excluded directories are skipped");

    system("rm -rf /tmp/glob_test_exclude");
    mkdir("/tmp/glob_test_exclude", 0755);
    mkdir("/tmp/glob_test_exclude/.git", 0755);
    mkdir("/tmp/glob_test_exclude/node_modules", 0755);
    mkdir("/tmp/glob_test_exclude/build", 0755);
    mkdir("/tmp/glob_test_exclude/src", 0755);

    FILE *f;
    f = fopen("/tmp/glob_test_exclude/.git/config", "w"); if (f) fclose(f);
    f = fopen("/tmp/glob_test_exclude/node_modules/foo.js", "w"); if (f) fclose(f);
    f = fopen("/tmp/glob_test_exclude/build/output.js", "w"); if (f) fclose(f);
    f = fopen("/tmp/glob_test_exclude/src/main.js", "w"); if (f) fclose(f);

    TestConversationState state = {0};
    strlcpy(state.working_dir, "/tmp/glob_test_exclude", sizeof(state.working_dir));
    state.additional_dirs_count = 0;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "pattern", "**/*.js");

    cJSON *result = tool_glob(params, &state);
    cJSON *files = cJSON_GetObjectItem(result, "files");
    cJSON *count = cJSON_GetObjectItem(result, "count");

    assert_test(cJSON_IsArray(files) && cJSON_GetArraySize(files) == 1,
                "Should find only 1 .js file (excluded dirs skipped)");
    assert_test(count && count->valueint == 1,
                "Count should be 1");

    cJSON_Delete(params);
    cJSON_Delete(result);
    system("rm -rf /tmp/glob_test_exclude");
}

// Test truncation with recursive glob
static void test_truncation_recursive(void) {
    print_test_header("Truncation with recursive glob");

    system("rm -rf /tmp/glob_test_trunc");
    mkdir("/tmp/glob_test_trunc", 0755);

    for (int i = 0; i < 10; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/tmp/glob_test_trunc/file%d.txt", i);
        FILE *f = fopen(path, "w");
        if (f) fclose(f);
    }

    setenv("KLAWED_GLOB_MAX_RESULTS", "3", 1);

    TestConversationState state = {0};
    strlcpy(state.working_dir, "/tmp/glob_test_trunc", sizeof(state.working_dir));
    state.additional_dirs_count = 0;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "pattern", "**/*.txt");

    cJSON *result = tool_glob(params, &state);
    cJSON *files = cJSON_GetObjectItem(result, "files");
    cJSON *count = cJSON_GetObjectItem(result, "count");
    cJSON *total_matches = cJSON_GetObjectItem(result, "total_matches");
    cJSON *truncated = cJSON_GetObjectItem(result, "truncated");

    assert_test(cJSON_IsArray(files) && cJSON_GetArraySize(files) == 3,
                "Should return only 3 files when max_results=3");
    assert_test(count && count->valueint == 3,
                "count should be 3");
    assert_test(total_matches && total_matches->valueint == 10,
                "total_matches should be 10");
    assert_test(truncated && cJSON_IsBool(truncated) && cJSON_IsTrue(truncated),
                "truncated should be true");

    cJSON_Delete(params);
    cJSON_Delete(result);
    unsetenv("KLAWED_GLOB_MAX_RESULTS");
    system("rm -rf /tmp/glob_test_trunc");
}

// Test truncation with non-recursive glob
static void test_truncation_non_recursive(void) {
    print_test_header("Truncation with non-recursive glob");

    system("rm -rf /tmp/glob_test_trunc2");
    mkdir("/tmp/glob_test_trunc2", 0755);

    for (int i = 0; i < 10; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/tmp/glob_test_trunc2/file%d.txt", i);
        FILE *f = fopen(path, "w");
        if (f) fclose(f);
    }

    setenv("KLAWED_GLOB_MAX_RESULTS", "5", 1);

    TestConversationState state = {0};
    strlcpy(state.working_dir, "/tmp/glob_test_trunc2", sizeof(state.working_dir));
    state.additional_dirs_count = 0;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "pattern", "*.txt");

    cJSON *result = tool_glob(params, &state);
    cJSON *files = cJSON_GetObjectItem(result, "files");
    cJSON *count = cJSON_GetObjectItem(result, "count");
    cJSON *total_matches = cJSON_GetObjectItem(result, "total_matches");

    assert_test(cJSON_IsArray(files) && cJSON_GetArraySize(files) == 5,
                "Should return only 5 files when max_results=5");
    assert_test(count && count->valueint == 5,
                "count should be 5");
    assert_test(total_matches && total_matches->valueint == 10,
                "total_matches should be 10");

    cJSON_Delete(params);
    cJSON_Delete(result);
    unsetenv("KLAWED_GLOB_MAX_RESULTS");
    system("rm -rf /tmp/glob_test_trunc2");
}

// Test truncation warning field is present and correct
static void test_truncation_fields_present(void) {
    print_test_header("Truncation warning field present");

    system("rm -rf /tmp/glob_test_warn");
    mkdir("/tmp/glob_test_warn", 0755);

    for (int i = 0; i < 5; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/tmp/glob_test_warn/file%d.txt", i);
        FILE *f = fopen(path, "w");
        if (f) fclose(f);
    }

    setenv("KLAWED_GLOB_MAX_RESULTS", "2", 1);

    TestConversationState state = {0};
    strlcpy(state.working_dir, "/tmp/glob_test_warn", sizeof(state.working_dir));
    state.additional_dirs_count = 0;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "pattern", "**/*.txt");

    cJSON *result = tool_glob(params, &state);
    cJSON *truncated = cJSON_GetObjectItem(result, "truncated");
    cJSON *warning = cJSON_GetObjectItem(result, "warning");

    assert_test(truncated && cJSON_IsBool(truncated) && cJSON_IsTrue(truncated),
                "truncated should be true when results are limited");
    assert_test(warning != NULL && cJSON_IsString(warning),
                "warning should be present when truncated");
    assert_test(strstr(warning->valuestring, "2/5") != NULL,
                "warning should contain correct counts (2/5)");

    cJSON_Delete(params);
    cJSON_Delete(result);
    unsetenv("KLAWED_GLOB_MAX_RESULTS");
    system("rm -rf /tmp/glob_test_warn");
}

// Test no truncation fields when under limit
static void test_no_truncation_fields_absent(void) {
    print_test_header("No truncation fields when under limit");

    TestConversationState state = {0};
    strlcpy(state.working_dir, "/tmp/glob_test", sizeof(state.working_dir));
    state.additional_dirs_count = 0;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "pattern", "**/*.tex");

    cJSON *result = tool_glob(params, &state);
    cJSON *truncated = cJSON_GetObjectItem(result, "truncated");
    cJSON *warning = cJSON_GetObjectItem(result, "warning");

    assert_test(truncated && cJSON_IsBool(truncated) && !cJSON_IsTrue(truncated),
                "truncated should be false when under limit");
    assert_test(warning == NULL,
                "warning should be absent when not truncated");

    cJSON_Delete(params);
    cJSON_Delete(result);
}

int main(void) {
    printf("%s========================================%s\n", COLOR_CYAN, COLOR_RESET);
    printf("Glob Tool Unit Tests (with starstar support)\n");
    printf("%s========================================%s\n", COLOR_CYAN, COLOR_RESET);

    setup_test_dirs();

    test_is_excluded_dir_directly();
    test_excluded_directories_skipped();
    test_recursive_tex_files();
    test_recursive_md_files();
    test_recursive_pattern_with_prefix();
    test_standard_glob_tex();
    test_standard_glob_subdir();
    test_recursive_all_files();
    test_truncation_recursive();
    test_truncation_non_recursive();
    test_truncation_fields_present();
    test_no_truncation_fields_absent();

    cleanup_test_dirs();

    print_test_summary();

    return tests_failed > 0 ? 1 : 0;
}
