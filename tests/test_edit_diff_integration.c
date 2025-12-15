/**
 * test_edit_diff_integration.c - Integration test for Edit tool with diff colorization
 *
 * Tests the complete Edit tool workflow including colorized diffs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Define TEST_BUILD to expose internal functions
#define TEST_BUILD 1
#define main unused_main
#include "../src/claude.c"
#undef main

// Test helpers
static void test_edit_with_diff(const char *test_name,
                                 const char *file_path,
                                 const char *original_content,
                                 const char *old_string,
                                 const char *new_string,
                                 int replace_all,
                                 int use_regex) {
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║ Test: %-32s ║\n", test_name);
    printf("╚════════════════════════════════════════╝\n\n");

    // Write original content
    if (write_file(file_path, original_content) != 0) {
        printf("❌ Failed to write original file\n");
        return;
    }

    printf("Original content:\n");
    printf("─────────────────────────────────────────\n");
    printf("%s", original_content);
    printf("─────────────────────────────────────────\n\n");

    // Perform edit
    printf("Performing edit:\n");
    printf("  old_string: \"%s\"\n", old_string);
    printf("  new_string: \"%s\"\n", new_string);
    printf("  replace_all: %d\n", replace_all);
    printf("  use_regex: %d\n\n", use_regex);

    char *result = tool_edit(file_path, old_string, new_string, replace_all, use_regex);

    if (result) {
        printf("✓ Edit completed:\n%s\n", result);
        free(result);
    } else {
        printf("❌ Edit failed\n");
    }

    printf("\n");

    // Clean up
    unlink(file_path);
}

int main(void) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║   Edit Tool + Diff Colorization Integration Tests           ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");

    // Test 1: Simple string replacement
    test_edit_with_diff(
        "Simple replacement",
        "/tmp/test_edit_1.txt",
        "Hello World\nThis is a test\nGoodbye World\n",
        "World",
        "Universe",
        0,  // replace first only
        0   // not regex
    );

    // Test 2: Replace all occurrences
    test_edit_with_diff(
        "Replace all",
        "/tmp/test_edit_2.txt",
        "foo bar foo baz foo\n",
        "foo",
        "qux",
        1,  // replace all
        0   // not regex
    );

    // Test 3: Multi-line change
    test_edit_with_diff(
        "Multi-line edit",
        "/tmp/test_edit_3.txt",
        "Line 1: original\nLine 2: original\nLine 3: original\nLine 4: original\n",
        "Line 2: original",
        "Line 2: MODIFIED",
        0,
        0
    );

    // Test 4: With theme loaded (Dracula)
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║   Testing with Dracula Theme           ║\n");
    printf("╚════════════════════════════════════════╝\n");

    Theme theme = {0};
    const char *builtin = get_builtin_theme_content("dracula");
    if (builtin && load_kitty_theme_buf(builtin, &theme)) {
        printf("✓ Dracula theme loaded\n");

        test_edit_with_diff(
            "With Dracula theme",
            "/tmp/test_edit_4.txt",
            "Original line 1\nOriginal line 2\nOriginal line 3\n",
            "Original",
            "Modified",
            1,  // replace all
            0
        );
    }

    // Test 5: Regex replacement
    test_edit_with_diff(
        "Regex replacement",
        "/tmp/test_edit_5.txt",
        "Error: something went wrong\nWarning: another issue\nInfo: all good\n",
        "^(Error|Warning):",
        "LOG:",
        1,  // replace all
        1   // regex
    );

    printf("\n╔════════════════════════════════════════╗\n");
    printf("║   All Integration Tests Completed     ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    return 0;
}
