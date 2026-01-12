/*
 * test_tool_disable.c - Tests for KLAWED_DISABLE_TOOLS functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../src/tool_utils.h"

static void test_no_env_set(void) {
    printf("  test_no_env_set... ");
    unsetenv("KLAWED_DISABLE_TOOLS");

    assert(is_tool_disabled("UploadImage") == 0);
    assert(is_tool_disabled("Subagent") == 0);
    assert(is_tool_disabled("Read") == 0);

    printf("PASSED\n");
}

static void test_single_tool_disabled(void) {
    printf("  test_single_tool_disabled... ");
    setenv("KLAWED_DISABLE_TOOLS", "UploadImage", 1);

    assert(is_tool_disabled("UploadImage") == 1);
    assert(is_tool_disabled("Subagent") == 0);
    assert(is_tool_disabled("Read") == 0);

    unsetenv("KLAWED_DISABLE_TOOLS");
    printf("PASSED\n");
}

static void test_multiple_tools_disabled(void) {
    printf("  test_multiple_tools_disabled... ");
    setenv("KLAWED_DISABLE_TOOLS", "UploadImage,Subagent,Write", 1);

    assert(is_tool_disabled("UploadImage") == 1);
    assert(is_tool_disabled("Subagent") == 1);
    assert(is_tool_disabled("Write") == 1);
    assert(is_tool_disabled("Read") == 0);
    assert(is_tool_disabled("Grep") == 0);

    unsetenv("KLAWED_DISABLE_TOOLS");
    printf("PASSED\n");
}

static void test_whitespace_handling(void) {
    printf("  test_whitespace_handling... ");
    setenv("KLAWED_DISABLE_TOOLS", " UploadImage , Subagent , Write ", 1);

    assert(is_tool_disabled("UploadImage") == 1);
    assert(is_tool_disabled("Subagent") == 1);
    assert(is_tool_disabled("Write") == 1);
    assert(is_tool_disabled("Read") == 0);

    unsetenv("KLAWED_DISABLE_TOOLS");
    printf("PASSED\n");
}

static void test_case_insensitive(void) {
    printf("  test_case_insensitive... ");
    setenv("KLAWED_DISABLE_TOOLS", "uploadimage,SUBAGENT", 1);

    assert(is_tool_disabled("UploadImage") == 1);
    assert(is_tool_disabled("uploadimage") == 1);
    assert(is_tool_disabled("UPLOADIMAGE") == 1);
    assert(is_tool_disabled("Subagent") == 1);
    assert(is_tool_disabled("SUBAGENT") == 1);
    assert(is_tool_disabled("subagent") == 1);

    unsetenv("KLAWED_DISABLE_TOOLS");
    printf("PASSED\n");
}

static void test_empty_env(void) {
    printf("  test_empty_env... ");
    setenv("KLAWED_DISABLE_TOOLS", "", 1);

    assert(is_tool_disabled("UploadImage") == 0);
    assert(is_tool_disabled("Subagent") == 0);

    unsetenv("KLAWED_DISABLE_TOOLS");
    printf("PASSED\n");
}

static void test_null_input(void) {
    printf("  test_null_input... ");
    setenv("KLAWED_DISABLE_TOOLS", "UploadImage", 1);

    assert(is_tool_disabled(NULL) == 0);

    unsetenv("KLAWED_DISABLE_TOOLS");
    printf("PASSED\n");
}

int main(void) {
    printf("Running tool disable tests...\n");

    test_no_env_set();
    test_single_tool_disabled();
    test_multiple_tools_disabled();
    test_whitespace_handling();
    test_case_insensitive();
    test_empty_env();
    test_null_input();

    printf("All tool disable tests PASSED!\n");
    return 0;
}
