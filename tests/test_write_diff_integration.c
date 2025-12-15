/**
 * test_write_diff_integration.c - Integration test for Write tool with diff colorization
 *
 * Tests the complete Write tool workflow including colorized diffs when overwriting existing files
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cjson/cJSON.h>

// Include internal header to get ConversationState definition
#include "../src/claude_internal.h"

// Define TEST_BUILD to expose internal functions
#define TEST_BUILD 1

// External functions from claude.c (exposed via TEST_BUILD)
extern int write_file(const char *path, const char *content);
extern char* read_file(const char *path);
extern int show_diff(const char *file_path, const char *original_content);
extern cJSON* tool_write(cJSON *params, ConversationState *state);



// Test helpers
static void test_write_with_diff(const char *test_name,
                                 const char *file_path,
                                 const char *original_content,
                                 const char *new_content) {
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║ Test: %-32s ║\n", test_name);
    printf("╚════════════════════════════════════════╝\n\n");

    // Write original content if provided
    if (original_content) {
        if (write_file(file_path, original_content) != 0) {
            printf("❌ Failed to write original file\n");
            return;
        }

        printf("Original content:\n");
        printf("─────────────────────────────────────────\n");
        printf("%s", original_content);
        printf("─────────────────────────────────────────\n\n");
    } else {
        printf("Creating new file (no original content)\n\n");
    }

    // Perform write
    printf("Writing new content:\n");
    printf("─────────────────────────────────────────\n");
    printf("%s", new_content);
    printf("─────────────────────────────────────────\n\n");

    // Create JSON parameters for the Write tool
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", file_path);
    cJSON_AddStringToObject(params, "content", new_content);

    // Create conversation state
    ConversationState state = {0};
    if (conversation_state_init(&state) != 0) {
        fprintf(stderr, "Failed to initialize conversation state\n");
        exit(1);
    }
    state.working_dir = strdup("/tmp");

    // Call the Write tool
    cJSON *result = tool_write(params, &state);

    if (result) {
        if (cJSON_HasObjectItem(result, "error")) {
            printf("❌ Write failed: %s\n", cJSON_GetObjectItem(result, "error")->valuestring);
        } else {
            printf("✓ Write completed successfully\n");
        }
        cJSON_Delete(result);
    } else {
        printf("❌ Write failed with NULL result\n");
    }

    cJSON_Delete(params);
    free(state.working_dir);
    conversation_state_destroy(&state);
    printf("\n");

    // Clean up
    unlink(file_path);
}

int main(void) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║   Write Tool + Diff Colorization Integration Tests          ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");

    // Test 1: Overwrite existing file with simple change
    test_write_with_diff(
        "Overwrite existing file",
        "/tmp/test_write_1.txt",
        "Hello World\nThis is a test\nGoodbye World\n",
        "Hello Universe\nThis is a modified test\nGoodbye World\n"
    );

    // Test 2: Overwrite with multi-line changes
    test_write_with_diff(
        "Multi-line overwrite",
        "/tmp/test_write_2.txt",
        "Line 1: original\nLine 2: original\nLine 3: original\n",
        "Line 1: MODIFIED\nLine 2: original\nLine 3: MODIFIED\nNew Line 4: added\n"
    );

    // Test 3: Create new file (no diff should be shown)
    test_write_with_diff(
        "Create new file",
        "/tmp/test_write_3.txt",
        NULL,  // No original content
        "This is a brand new file\nWith some content\n"
    );

    // Test 4: Complete content replacement
    test_write_with_diff(
        "Complete replacement",
        "/tmp/test_write_4.txt",
        "Old content line 1\nOld content line 2\nOld content line 3\n",
        "Completely new content\nDifferent structure\nNew format\n"
    );

    // Test 5: Complex multi-line replacement
    test_write_with_diff(
        "Complex multi-line replacement",
        "/tmp/test_write_5.txt",
        "function oldFunction() {\n    console.log('old code');\n    return false;\n}",
        "function newFunction() {\n    console.log('new improved code');\n    return true;\n}\n\n// Additional helper function\nfunction helper() {\n    return 'helper result';\n}"
    );

    printf("\n╔════════════════════════════════════════╗\n");
    printf("║   All Integration Tests Completed     ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    return 0;
}
