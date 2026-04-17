/*
 * Unit Tests for Codex Tools
 *
 * Tests the Codex-compatible tool implementation including:
 * - apply_patch (add, delete, update, move to)
 * - shell / shell_command
 * - list_dir
 * - view_image
 * - send_message
 *
 * Compilation: make test-codex-tools
 * Usage: ./test_codex_tools
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cjson/cJSON.h>
#include <stdarg.h>

#include "../src/tools/codex_tools.h"
#include "../src/util/file_utils.h"

// Test framework colors
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN  "\033[36m"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void print_test_result(const char *test_name, int passed) {
    tests_run++;
    if (passed) {
        tests_passed++;
        printf(COLOR_GREEN "  PASS" COLOR_RESET " %s\n", test_name);
    } else {
        tests_failed++;
        printf(COLOR_RED "  FAIL" COLOR_RESET " %s\n", test_name);
    }
}

static void print_section(const char *name) {
    printf("\n%s[%s]%s\n", COLOR_CYAN, name, COLOR_RESET);
}

static void print_summary(void) {
    printf("\n%s════════════════════════════════════════════════%s\n", COLOR_CYAN, COLOR_RESET);
    printf("Tests run:    %d\n", tests_run);
    printf(COLOR_GREEN "Tests passed: %d\n" COLOR_RESET, tests_passed);
    if (tests_failed > 0) {
        printf(COLOR_RED "Tests failed: %d\n" COLOR_RESET, tests_failed);
    }
    printf("%s════════════════════════════════════════════════%s\n", COLOR_CYAN, COLOR_RESET);
}

// ============================================================================
// apply_patch Tests
// ============================================================================

#define TEST_DIR_APPLY "/tmp/test_codex_tools_apply"

static void cleanup_test_dir(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR_APPLY);
    int ret __attribute__((unused)) = system(cmd);
}

static void setup_test_dir(void) {
    cleanup_test_dir();
    mkdir_p(TEST_DIR_APPLY);
}

static char* build_path(const char *filename) {
    static char path[512];
    snprintf(path, sizeof(path), "%s/%s", TEST_DIR_APPLY, filename);
    return path;
}

/* Suppress format-nonliteral warning for our patch formatting helper */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
static char* format_patch(char *out, size_t out_size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(out, out_size, fmt, args);
    va_end(args);
    return out;
}
#pragma GCC diagnostic pop

static void test_apply_patch_add_file(void) {
    setup_test_dir();

    char patch_buf[1024];
    const char *patch = format_patch(patch_buf, sizeof(patch_buf),
        "*** Begin Patch\n"
        "*** Add File: %s/test_add.txt\n"
        "+Hello, world!\n"
        "*** End Patch\n", TEST_DIR_APPLY);

    cJSON *result = codex_tool_apply_patch(patch);
    int success = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "success") != NULL;

    char *content = read_file(build_path("test_add.txt"));
    int content_ok = (content && strcmp(content, "Hello, world!\n") == 0);

    print_test_result("apply_patch add file creates file with correct content",
                      success && content_ok);

    free(content);
    cJSON_Delete(result);
    cleanup_test_dir();
}

static void test_apply_patch_delete_file(void) {
    setup_test_dir();
    write_file(build_path("test_delete.txt"), "goodbye");

    char patch_buf[1024];
    const char *patch = format_patch(patch_buf, sizeof(patch_buf),
        "*** Begin Patch\n"
        "*** Delete File: %s/test_delete.txt\n"
        "*** End Patch\n", TEST_DIR_APPLY);

    cJSON *result = codex_tool_apply_patch(patch);
    int success = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "success") != NULL;

    struct stat st;
    int deleted = (stat(build_path("test_delete.txt"), &st) != 0);

    print_test_result("apply_patch delete file removes file",
                      success && deleted);

    cJSON_Delete(result);
    cleanup_test_dir();
}

static void test_apply_patch_update_file(void) {
    setup_test_dir();
    write_file(build_path("test_update.txt"), "foo\nbar\nbaz\n");

    char patch_buf[1024];
    const char *patch = format_patch(patch_buf, sizeof(patch_buf),
        "*** Begin Patch\n"
        "*** Update File: %s/test_update.txt\n"
        "@@context\n"
        " bar\n"
        "-baz\n"
        "+qux\n"
        "*** End Patch\n", TEST_DIR_APPLY);

    cJSON *result = codex_tool_apply_patch(patch);
    int success = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "success") != NULL;

    char *content = read_file(build_path("test_update.txt"));
    int content_ok = (content && strcmp(content, "foo\nbar\nqux\n") == 0);

    print_test_result("apply_patch update file replaces matching context",
                      success && content_ok);

    free(content);
    cJSON_Delete(result);
    cleanup_test_dir();
}

static void test_apply_patch_move_to(void) {
    setup_test_dir();
    write_file(build_path("old_name.txt"), "content");

    char patch_buf[1024];
    const char *patch = format_patch(patch_buf, sizeof(patch_buf),
        "*** Begin Patch\n"
        "*** Update File: %s/old_name.txt\n"
        "*** Move to: %s/new_name.txt\n"
        "*** End Patch\n", TEST_DIR_APPLY, TEST_DIR_APPLY);

    cJSON *result = codex_tool_apply_patch(patch);
    int success = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "success") != NULL;

    struct stat st_old, st_new;
    int old_gone = (stat(build_path("old_name.txt"), &st_old) != 0);
    int new_exists = (stat(build_path("new_name.txt"), &st_new) == 0);

    print_test_result("apply_patch move to renames file",
                      success && old_gone && new_exists);

    cJSON_Delete(result);
    cleanup_test_dir();
}

static void test_apply_patch_multiple_operations(void) {
    setup_test_dir();
    write_file(build_path("multi_a.txt"), "aaa\n");

    char patch_buf[1024];
    const char *patch = format_patch(patch_buf, sizeof(patch_buf),
        "*** Begin Patch\n"
        "*** Update File: %s/multi_a.txt\n"
        "@@context\n"
        "-aaa\n"
        "+bbb\n"
        "*** Add File: %s/multi_b.txt\n"
        "+ccc\n"
        "*** End Patch\n", TEST_DIR_APPLY, TEST_DIR_APPLY);

    cJSON *result = codex_tool_apply_patch(patch);
    int success = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "success") != NULL;

    char *content_a = read_file(build_path("multi_a.txt"));
    char *content_b = read_file(build_path("multi_b.txt"));

    int a_ok = (content_a && strcmp(content_a, "bbb\n") == 0);
    int b_ok = (content_b && strcmp(content_b, "ccc\n") == 0);

    print_test_result("apply_patch multiple operations (update + add)",
                      success && a_ok && b_ok);

    free(content_a);
    free(content_b);
    cJSON_Delete(result);
    cleanup_test_dir();
}

/*
 * Bug: apply_patch returns early when *** End Patch is encountered inside
 * an Update File block, skipping any remaining file operations.
 */
static void test_apply_patch_end_patch_inside_update(void) {
    setup_test_dir();
    write_file(build_path("update_then_add.txt"), "old_content\n");

    char patch_buf[1024];
    const char *patch = format_patch(patch_buf, sizeof(patch_buf),
        "*** Begin Patch\n"
        "*** Update File: %s/update_then_add.txt\n"
        "@@context\n"
        "-old_content\n"
        "+new_content\n"
        "*** Add File: %s/should_exist.txt\n"
        "+hello\n"
        "*** End Patch\n", TEST_DIR_APPLY, TEST_DIR_APPLY);

    cJSON *result = codex_tool_apply_patch(patch);
    int success = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "success") != NULL;

    char *updated = read_file(build_path("update_then_add.txt"));
    char *added = read_file(build_path("should_exist.txt"));

    int updated_ok = (updated && strcmp(updated, "new_content\n") == 0);
    int added_ok = (added && strcmp(added, "hello\n") == 0);

    print_test_result("apply_patch processes operations after End Patch inside update",
                      success && updated_ok && added_ok);

    free(updated);
    free(added);
    cJSON_Delete(result);
    cleanup_test_dir();
}

static void test_apply_patch_empty_input(void) {
    cJSON *result = codex_tool_apply_patch("");
    int has_error = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "error") != NULL;

    print_test_result("apply_patch empty input returns error",
                      has_error);

    cJSON_Delete(result);
}

static void test_apply_patch_missing_begin_marker(void) {
    cJSON *result = codex_tool_apply_patch("random text\n");
    int has_error = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "error") != NULL;

    print_test_result("apply_patch missing Begin Patch returns error",
                      has_error);

    cJSON_Delete(result);
}

static void test_apply_patch_context_not_found(void) {
    setup_test_dir();
    write_file(build_path("no_match.txt"), "xyz\n");

    char patch_buf[1024];
    const char *patch = format_patch(patch_buf, sizeof(patch_buf),
        "*** Begin Patch\n"
        "*** Update File: %s/no_match.txt\n"
        "@@context\n"
        " does_not_exist\n"
        "-does_not_exist\n"
        "+replaced\n"
        "*** End Patch\n", TEST_DIR_APPLY);

    cJSON *result = codex_tool_apply_patch(patch);
    int has_error = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "error") != NULL;

    print_test_result("apply_patch context not found returns error",
                      has_error);

    cJSON_Delete(result);
    cleanup_test_dir();
}

// ============================================================================
// shell_command Tests
// ============================================================================

static void test_shell_command_basic(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "command", "echo hello");

    cJSON *result = codex_tool_shell_command(args);
    cJSON *stdout_json = cJSON_GetObjectItem(result, "stdout");
    int ok = (stdout_json && cJSON_IsString(stdout_json) &&
              strstr(stdout_json->valuestring, "hello") != NULL);

    print_test_result("shell_command basic execution works", ok);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

static void test_shell_command_workdir(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "command", "pwd");
    cJSON_AddStringToObject(args, "workdir", "/tmp");

    cJSON *result = codex_tool_shell_command(args);
    cJSON *stdout_json = cJSON_GetObjectItem(result, "stdout");
    int ok = (stdout_json && cJSON_IsString(stdout_json) &&
              strstr(stdout_json->valuestring, "/tmp") != NULL);

    print_test_result("shell_command respects workdir", ok);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

/*
 * Bug: shell_command wraps the command in single quotes using sh -c '%s'.
 * If the command itself contains a single quote, the shell syntax breaks.
 */
static void test_shell_command_with_single_quotes(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "command", "printf '%s\\n' 'it'\\''s working'");

    cJSON *result = codex_tool_shell_command(args);
    cJSON *stdout_json = cJSON_GetObjectItem(result, "stdout");
    int exit_code = 0;
    cJSON *exit_json = cJSON_GetObjectItem(result, "exit_code");
    if (exit_json && cJSON_IsNumber(exit_json)) {
        exit_code = exit_json->valueint;
    }

    if (exit_code != 0) {
        printf("    DEBUG single_quote exit_code=%d stdout='%s'\n", exit_code,
               stdout_json ? stdout_json->valuestring : "(null)");
    }

    int ok = (exit_code == 0 && stdout_json && cJSON_IsString(stdout_json) &&
              strcmp(stdout_json->valuestring, "it's working\n") == 0);

    print_test_result("shell_command handles commands with single quotes", ok);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

static void test_shell_command_preserves_regex_pipes(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "command",
                            "printf 'reasoning_content\\nSTREAM_APPEND\\n' | grep -En 'reasoning_content|STREAM_APPEND'");

    cJSON *result = codex_tool_shell_command(args);
    cJSON *stdout_json = cJSON_GetObjectItem(result, "stdout");
    cJSON *exit_json = cJSON_GetObjectItem(result, "exit_code");
    int ok = (exit_json && cJSON_IsNumber(exit_json) && exit_json->valueint == 0 &&
              stdout_json && cJSON_IsString(stdout_json) &&
              strstr(stdout_json->valuestring, "1:reasoning_content") != NULL &&
              strstr(stdout_json->valuestring, "2:STREAM_APPEND") != NULL &&
              strstr(stdout_json->valuestring, "command not found") == NULL);

    print_test_result("shell_command preserves quoted regex pipes", ok);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

static void test_shell_command_sed_with_single_quotes(void) {
    setup_test_dir();
    write_file(build_path("sed_case.txt"), "alpha\nbeta\ngamma\n");

    cJSON *args = cJSON_CreateObject();
    char command[1024];
    snprintf(command, sizeof(command),
             "sed -n '1,2p' %s/sed_case.txt",
             TEST_DIR_APPLY);
    cJSON_AddStringToObject(args, "command", command);

    cJSON *result = codex_tool_shell_command(args);
    cJSON *stdout_json = cJSON_GetObjectItem(result, "stdout");
    cJSON *exit_json = cJSON_GetObjectItem(result, "exit_code");
    int ok = (exit_json && cJSON_IsNumber(exit_json) && exit_json->valueint == 0 &&
              stdout_json && cJSON_IsString(stdout_json) &&
              strcmp(stdout_json->valuestring, "alpha\nbeta\n") == 0);

    print_test_result("shell_command handles sed ranges with single quotes", ok);

    cJSON_Delete(args);
    cJSON_Delete(result);
    cleanup_test_dir();
}

static void test_shell_command_perl_dollar_dot_not_redirected(void) {
    setup_test_dir();
    write_file(build_path("perl_case.txt"), "one\ntwo\nthree\n");

    cJSON *args = cJSON_CreateObject();
    char command[1024];
    snprintf(command, sizeof(command),
             "perl -ne 'print if $.>=1 && $.<=2' %s/perl_case.txt",
             TEST_DIR_APPLY);
    cJSON_AddStringToObject(args, "command", command);
    cJSON_AddStringToObject(args, "workdir", TEST_DIR_APPLY);

    cJSON *result = codex_tool_shell_command(args);
    cJSON *stdout_json = cJSON_GetObjectItem(result, "stdout");
    cJSON *exit_json = cJSON_GetObjectItem(result, "exit_code");

    struct stat st_one = {0};
    struct stat st_two = {0};
    int created_eq1 = (stat(build_path("=1"), &st_one) == 0);
    int created_eq2 = (stat(build_path("=2"), &st_two) == 0);

    int ok = (exit_json && cJSON_IsNumber(exit_json) && exit_json->valueint == 0 &&
              stdout_json && cJSON_IsString(stdout_json) &&
              strcmp(stdout_json->valuestring, "one\ntwo\n") == 0 &&
              !created_eq1 && !created_eq2);

    print_test_result("shell_command keeps perl $. expressions intact", ok);

    cJSON_Delete(args);
    cJSON_Delete(result);
    cleanup_test_dir();
}

// ============================================================================
// shell Tests
// ============================================================================

static void test_shell_basic(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON *cmd = cJSON_CreateArray();
    cJSON_AddItemToArray(cmd, cJSON_CreateString("echo"));
    cJSON_AddItemToArray(cmd, cJSON_CreateString("shell_test"));
    cJSON_AddItemToObject(args, "command", cmd);

    cJSON *result = codex_tool_shell(args);
    cJSON *stdout_json = cJSON_GetObjectItem(result, "stdout");
    int ok = (stdout_json && cJSON_IsString(stdout_json) &&
              strstr(stdout_json->valuestring, "shell_test") != NULL);

    print_test_result("shell basic execution works", ok);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

static void test_shell_missing_command(void) {
    cJSON *args = cJSON_CreateObject();
    /* No "command" field */

    cJSON *result = codex_tool_shell(args);
    int has_error = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "error") != NULL;

    print_test_result("shell missing command returns error", has_error);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

// ============================================================================
// list_dir Tests
// ============================================================================

#define TEST_DIR_LIST "/tmp/test_codex_tools_list"

static void setup_list_dir(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "rm -rf %s && mkdir -p %s/subdir && "
             "touch %s/file1.txt %s/file2.txt %s/subdir/nested.txt",
             TEST_DIR_LIST, TEST_DIR_LIST, TEST_DIR_LIST, TEST_DIR_LIST, TEST_DIR_LIST);
    int ret1 __attribute__((unused)) = system(cmd);
}

static void cleanup_list_dir(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR_LIST);
    int ret2 __attribute__((unused)) = system(cmd);
}

static void test_list_dir_basic(void) {
    setup_list_dir();

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "dir_path", TEST_DIR_LIST);

    cJSON *result = codex_tool_list_dir(args);
    cJSON *entries = cJSON_GetObjectItem(result, "entries");
    int has_entries = (entries && cJSON_IsArray(entries) && cJSON_GetArraySize(entries) >= 2);

    print_test_result("list_dir returns entries for existing directory", has_entries);

    cJSON_Delete(args);
    cJSON_Delete(result);
    cleanup_list_dir();
}

static void test_list_dir_limit(void) {
    setup_list_dir();

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "dir_path", TEST_DIR_LIST);
    cJSON_AddNumberToObject(args, "limit", 1);

    cJSON *result = codex_tool_list_dir(args);
    cJSON *entries = cJSON_GetObjectItem(result, "entries");
    int limited = (entries && cJSON_IsArray(entries) && cJSON_GetArraySize(entries) == 1);

    print_test_result("list_dir respects limit parameter", limited);

    cJSON_Delete(args);
    cJSON_Delete(result);
    cleanup_list_dir();
}

/*
 * Bug: list_dir parses the depth parameter but never uses it. The function
 * always lists only the top-level directory regardless of depth.
 */
static void test_list_dir_depth(void) {
    setup_list_dir();

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "dir_path", TEST_DIR_LIST);
    cJSON_AddNumberToObject(args, "depth", 2);

    cJSON *result = codex_tool_list_dir(args);
    cJSON *entries = cJSON_GetObjectItem(result, "entries");

    int found_nested = 0;
    if (entries && cJSON_IsArray(entries)) {
        int count = cJSON_GetArraySize(entries);
        for (int i = 0; i < count; i++) {
            cJSON *entry = cJSON_GetArrayItem(entries, i);
            cJSON *name = cJSON_GetObjectItem(entry, "name");
            if (name && cJSON_IsString(name)) {
                if (strcmp(name->valuestring, "subdir/nested.txt") == 0) {
                    found_nested = 1;
                }
            }
        }
    }

    print_test_result("list_dir respects depth parameter (recurses into subdirs)", found_nested);

    cJSON_Delete(args);
    cJSON_Delete(result);
    cleanup_list_dir();
}

static void test_list_dir_nonexistent(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "dir_path", "/tmp/does_not_exist_12345");

    cJSON *result = codex_tool_list_dir(args);
    int has_error = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "error") != NULL;

    print_test_result("list_dir nonexistent directory returns error", has_error);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

// ============================================================================
// view_image Tests
// ============================================================================

static void test_view_image_nonexistent(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "path", "/tmp/does_not_exist_image.png");

    cJSON *result = codex_tool_view_image(args);
    int has_error = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "error") != NULL;

    print_test_result("view_image nonexistent file returns error", has_error);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

static void test_view_image_basic(void) {
    /* Create a minimal 1x1 GIF */
    unsigned char gif_data[] = {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x3b
    };

    FILE *fp = fopen("/tmp/test_codex_tools_image.gif", "wb");
    if (!fp) {
        print_test_result("view_image basic read", 0);
        return;
    }
    fwrite(gif_data, 1, sizeof(gif_data), fp);
    fclose(fp);

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "path", "/tmp/test_codex_tools_image.gif");

    cJSON *result = codex_tool_view_image(args);
    cJSON *url = cJSON_GetObjectItem(result, "image_url");
    int ok = (url && cJSON_IsString(url) &&
              strstr(url->valuestring, "data:image/gif;base64,") != NULL);

    print_test_result("view_image returns correct data URL for GIF", ok);

    cJSON_Delete(args);
    cJSON_Delete(result);
    unlink("/tmp/test_codex_tools_image.gif");
}

// ============================================================================
// send_message Tests
// ============================================================================

static void test_send_message_basic(void) {
    cJSON *args = cJSON_CreateObject();
    /* Use a high PID that's unlikely to exist (but not 1 which requires root on macOS) */
    cJSON_AddStringToObject(args, "target", "99999");
    cJSON_AddStringToObject(args, "message", "hello");

    cJSON *result = codex_tool_send_message(args);
    /* Note: send_message doesn't actually deliver messages, just validates the target exists */
    /* Since PID 99999 doesn't exist, this should return an error */
    int has_error = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "error") != NULL;

    print_test_result("send_message validates target exists", has_error);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

static void test_send_message_missing_target(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "message", "hello");

    cJSON *result = codex_tool_send_message(args);
    int has_error = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "error") != NULL;

    print_test_result("send_message missing target returns error", has_error);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

static void test_send_message_valid_target(void) {
    cJSON *args = cJSON_CreateObject();
    /* Use our own PID as a target that definitely exists */
    char target[32];
    snprintf(target, sizeof(target), "%d", getpid());
    cJSON_AddStringToObject(args, "target", target);
    cJSON_AddStringToObject(args, "message", "hello");

    cJSON *result = codex_tool_send_message(args);
    int success = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "success") != NULL &&
                  cJSON_IsTrue(cJSON_GetObjectItem(result, "success"));

    print_test_result("send_message accepts valid target (own PID)", success);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

// ============================================================================
// spawn_agent Tests
// ============================================================================

static void test_spawn_agent_missing_task_name(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "message", "do something");

    cJSON *result = codex_tool_spawn_agent(args);
    int has_error = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "error") != NULL;

    print_test_result("spawn_agent missing task_name returns error", has_error);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

static void test_spawn_agent_missing_message(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "task_name", "test_task");

    cJSON *result = codex_tool_spawn_agent(args);
    int has_error = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "error") != NULL;

    print_test_result("spawn_agent missing message returns error", has_error);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

static void test_spawn_agent_invalid_task_name(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "task_name", "Invalid-Name!");
    cJSON_AddStringToObject(args, "message", "do something");

    cJSON *result = codex_tool_spawn_agent(args);
    int has_error = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "error") != NULL;

    print_test_result("spawn_agent invalid task_name returns error", has_error);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

// ============================================================================
// Additional shell Tests
// ============================================================================

static void test_shell_workdir(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON *cmd = cJSON_CreateArray();
    cJSON_AddItemToArray(cmd, cJSON_CreateString("pwd"));
    cJSON_AddItemToObject(args, "command", cmd);
    cJSON_AddStringToObject(args, "workdir", "/tmp");

    cJSON *result = codex_tool_shell(args);
    cJSON *stdout_json = cJSON_GetObjectItem(result, "stdout");
    int ok = (stdout_json && cJSON_IsString(stdout_json) &&
              strstr(stdout_json->valuestring, "/tmp") != NULL);

    print_test_result("shell respects workdir parameter", ok);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

static void test_shell_timeout(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON *cmd = cJSON_CreateArray();
    cJSON_AddItemToArray(cmd, cJSON_CreateString("sleep"));
    cJSON_AddItemToArray(cmd, cJSON_CreateString("10"));
    cJSON_AddItemToObject(args, "command", cmd);
    cJSON_AddNumberToObject(args, "timeout_ms", 100); /* 100ms = should round up to 1s */

    cJSON *result = codex_tool_shell(args);
    cJSON *error = cJSON_GetObjectItem(result, "error");
    int timed_out = (error && cJSON_IsString(error) &&
                     strstr(error->valuestring, "timed out") != NULL);

    print_test_result("shell respects timeout parameter", timed_out);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

// ============================================================================
// Additional shell_command Tests
// ============================================================================

static void test_shell_command_timeout(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "command", "sleep 10");
    cJSON_AddNumberToObject(args, "timeout_ms", 100); /* 100ms */

    cJSON *result = codex_tool_shell_command(args);
    cJSON *error = cJSON_GetObjectItem(result, "error");
    int timed_out = (error && cJSON_IsString(error) &&
                     strstr(error->valuestring, "timed out") != NULL);

    print_test_result("shell_command respects timeout parameter", timed_out);

    cJSON_Delete(args);
    cJSON_Delete(result);
}

// ============================================================================
// Additional apply_patch Tests
// ============================================================================

static void test_apply_patch_multi_hunk(void) {
    setup_test_dir();
    write_file(build_path("multi_hunk.txt"), "line1\nline2\nline3\nline4\nline5\n");

    char patch_buf[1024];
    const char *patch = format_patch(patch_buf, sizeof(patch_buf),
        "*** Begin Patch\n"
        "*** Update File: %s/multi_hunk.txt\n"
        "@@ line1\n"
        " line1\n"
        "-line2\n"
        "+line2a\n"
        "@@ line4\n"
        " line4\n"
        "-line5\n"
        "+line5a\n"
        "*** End Patch\n", TEST_DIR_APPLY);

    cJSON *result = codex_tool_apply_patch(patch);
    int success = cJSON_IsObject(result) && cJSON_GetObjectItem(result, "success") != NULL;

    char *content = read_file(build_path("multi_hunk.txt"));
    int content_ok = (content && strcmp(content, "line1\nline2a\nline3\nline4\nline5a\n") == 0);

    print_test_result("apply_patch multi-hunk updates both locations",
                      success && content_ok);

    free(content);
    cJSON_Delete(result);
    cleanup_test_dir();
}

// ============================================================================
// Additional list_dir Tests
// ============================================================================

static void test_list_dir_offset(void) {
    setup_list_dir();

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "dir_path", TEST_DIR_LIST);
    cJSON_AddNumberToObject(args, "offset", 2);
    cJSON_AddNumberToObject(args, "limit", 10);

    cJSON *result = codex_tool_list_dir(args);
    cJSON *entries = cJSON_GetObjectItem(result, "entries");

    int first_number = 0;
    if (entries && cJSON_IsArray(entries) && cJSON_GetArraySize(entries) > 0) {
        cJSON *first = cJSON_GetArrayItem(entries, 0);
        cJSON *num = cJSON_GetObjectItem(first, "number");
        if (num && cJSON_IsNumber(num)) {
            first_number = num->valueint;
        }
    }

    print_test_result("list_dir respects offset parameter", first_number >= 2);

    cJSON_Delete(args);
    cJSON_Delete(result);
    cleanup_list_dir();
}

// ============================================================================
// Additional view_image Tests
// ============================================================================

static void test_view_image_oversized(void) {
    /* Create a sparse file just over 20MB */
    int fd = open("/tmp/test_codex_tools_oversized.img", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        print_test_result("view_image rejects oversized files", 0);
        return;
    }
    if (ftruncate(fd, 20 * 1024 * 1024 + 1) != 0) {
        close(fd);
        unlink("/tmp/test_codex_tools_oversized.img");
        print_test_result("view_image rejects oversized files", 0);
        return;
    }
    close(fd);

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "path", "/tmp/test_codex_tools_oversized.img");

    cJSON *result = codex_tool_view_image(args);
    cJSON *error = cJSON_GetObjectItem(result, "error");
    int has_error = (error && cJSON_IsString(error) &&
                     strstr(error->valuestring, "too large") != NULL);

    print_test_result("view_image rejects oversized files (>20MB)", has_error);

    cJSON_Delete(args);
    cJSON_Delete(result);
    unlink("/tmp/test_codex_tools_oversized.img");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("\n%s╔════════════════════════════════════════════╗%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s║   Codex Tools Unit Test Suite             ║%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s╚════════════════════════════════════════════╝%s\n", COLOR_CYAN, COLOR_RESET);

    print_section("apply_patch");
    test_apply_patch_add_file();
    test_apply_patch_delete_file();
    test_apply_patch_update_file();
    test_apply_patch_move_to();
    test_apply_patch_multiple_operations();
    test_apply_patch_end_patch_inside_update();
    test_apply_patch_empty_input();
    test_apply_patch_missing_begin_marker();
    test_apply_patch_context_not_found();
    test_apply_patch_multi_hunk();

    print_section("shell_command");
    test_shell_command_basic();
    test_shell_command_workdir();
    test_shell_command_with_single_quotes();
    test_shell_command_preserves_regex_pipes();
    test_shell_command_sed_with_single_quotes();
    test_shell_command_perl_dollar_dot_not_redirected();
    test_shell_command_timeout();

    print_section("shell");
    test_shell_basic();
    test_shell_missing_command();
    test_shell_workdir();
    test_shell_timeout();

    print_section("list_dir");
    test_list_dir_basic();
    test_list_dir_limit();
    test_list_dir_depth();
    test_list_dir_nonexistent();
    test_list_dir_offset();

    print_section("view_image");
    test_view_image_nonexistent();
    test_view_image_basic();
    test_view_image_oversized();

    print_section("send_message");
    test_send_message_basic();
    test_send_message_missing_target();
    test_send_message_valid_target();

    print_section("spawn_agent");
    test_spawn_agent_missing_task_name();
    test_spawn_agent_missing_message();
    test_spawn_agent_invalid_task_name();

    print_summary();

    return tests_failed > 0 ? 1 : 0;
}
