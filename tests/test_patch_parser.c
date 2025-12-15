/*
 * test_patch_parser.c - Unit tests for patch parser functionality
 *
 * Tests the detection and parsing of "Begin Patch/End Patch" format
 * from o4-mini model outputs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/patch_parser.h"
#include "../src/claude_internal.h"

// Test file paths
#define TEST_DIR "/tmp/patch_parser_test"
#define TEST_FILE_1 TEST_DIR "/test_file1.c"
#define TEST_FILE_2 TEST_DIR "/test_file2.h"

// Helper: Create test directory
static void setup_test_dir(void) {
    mkdir(TEST_DIR, 0755);
}

// Helper: Clean up test directory
static void cleanup_test_dir(void) {
    unlink(TEST_FILE_1);
    unlink(TEST_FILE_2);
    rmdir(TEST_DIR);
}

// Helper: Write content to a file
static void write_test_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fprintf(f, "%s", content);
    fclose(f);
}

// Helper: Read content from a file
static char* read_test_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc((size_t)size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(content, 1, (size_t)size, f);
    content[read] = '\0';
    fclose(f);

    return content;
}

// Test 1: Detect valid patch format
static void test_detect_valid_patch(void) {
    printf("Test 1: Detect valid patch format... ");

    const char *valid_patch =
        "*** Begin Patch\n"
        "*** Update File: test.c\n"
        "@@\n"
        "-old line\n"
        "+new line\n"
        "@@\n"
        "*** End Patch\n";

    assert(is_patch_format(valid_patch) == 1);

    printf("PASSED\n");
}

// Test 2: Reject invalid format (missing markers)
static void test_reject_invalid_format(void) {
    printf("Test 2: Reject invalid format... ");

    const char *invalid1 = "This is just regular text";
    const char *invalid2 = "*** Begin Patch\nsome content\n";  // Missing End Patch
    const char *invalid3 = "*** Begin Patch\n*** End Patch\n";  // Missing Update File

    assert(is_patch_format(invalid1) == 0);
    assert(is_patch_format(invalid2) == 0);
    assert(is_patch_format(invalid3) == 0);

    printf("PASSED\n");
}

// Test 3: Parse single file operation
static void test_parse_single_operation(void) {
    printf("Test 3: Parse single file operation... ");

    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: src/test.c\n"
        "@@\n"
        "-    int old_var = 0;\n"
        "+    int new_var = 1;\n"
        "@@\n"
        "*** End Patch\n";

    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);
    assert(patch->operation_count == 1);
    assert(strcmp(patch->operations[0].file_path, "src/test.c") == 0);
    assert(strstr(patch->operations[0].old_content, "old_var") != NULL);
    assert(strstr(patch->operations[0].new_content, "new_var") != NULL);

    free_parsed_patch(patch);

    printf("PASSED\n");
}

// Test 4: Parse multiple file operations
static void test_parse_multiple_operations(void) {
    printf("Test 4: Parse multiple file operations... ");

    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: src/file1.c\n"
        "@@\n"
        "-old content 1\n"
        "+new content 1\n"
        "@@\n"
        "*** Update File: src/file2.h\n"
        "@@\n"
        "-old content 2\n"
        "+new content 2\n"
        "@@\n"
        "*** End Patch\n";

    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);
    assert(patch->operation_count == 2);
    assert(strcmp(patch->operations[0].file_path, "src/file1.c") == 0);
    assert(strcmp(patch->operations[1].file_path, "src/file2.h") == 0);

    free_parsed_patch(patch);

    printf("PASSED\n");
}

// Test 5: Apply patch to real file (single operation)
static void test_apply_single_patch(void) {
    printf("Test 5: Apply patch to real file... ");

    setup_test_dir();

    // Create initial file
    const char *initial_content =
        "int main() {\n"
        "    int old_var = 0;\n"
        "    return 0;\n"
        "}\n";
    write_test_file(TEST_FILE_1, initial_content);

    // Create patch
    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: /tmp/patch_parser_test/test_file1.c\n"
        "@@\n"
        "-    int old_var = 0;\n"
        "+    int new_var = 1;\n"
        "@@\n"
        "*** End Patch\n";

    // Parse and apply
    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);

    // Create a minimal conversation state
    ConversationState state = {0};
    if (conversation_state_init(&state) != 0) {
        fprintf(stderr, "Failed to initialize conversation state\n");
        exit(1);
    }
    if (conversation_state_init(&state) != 0) {
        fprintf(stderr, "Failed to initialize conversation state\n");
        exit(1);
    }
    if (conversation_state_init(&state) != 0) {
        fprintf(stderr, "Failed to initialize conversation state\n");
        exit(1);
    }
    if (conversation_state_init(&state) != 0) {
        fprintf(stderr, "Failed to initialize conversation state\n");
        exit(1);
    }
    state.working_dir = strdup(TEST_DIR);

    // Apply patch
    cJSON *result = apply_patch(patch, &state);
    assert(result != NULL);
    assert(cJSON_GetObjectItem(result, "status") != NULL);

    // Verify file was modified
    char *new_content = read_test_file(TEST_FILE_1);
    assert(new_content != NULL);
    assert(strstr(new_content, "new_var") != NULL);
    assert(strstr(new_content, "old_var") == NULL);

    free(new_content);
    cJSON_Delete(result);
    free_parsed_patch(patch);
    free(state.working_dir);
    conversation_state_destroy(&state);
    conversation_state_destroy(&state);
    conversation_state_destroy(&state);
    conversation_state_destroy(&state);
    cleanup_test_dir();

    printf("PASSED\n");
}

// Test 6: Apply patch to multiple files
static void test_apply_multiple_patches(void) {
    printf("Test 6: Apply patch to multiple files... ");

    setup_test_dir();

    // Create initial files
    write_test_file(TEST_FILE_1, "int foo = 1;\n");
    write_test_file(TEST_FILE_2, "#define BAR 2\n");

    // Create patch affecting both files
    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: /tmp/patch_parser_test/test_file1.c\n"
        "@@\n"
        "-int foo = 1;\n"
        "+int foo = 42;\n"
        "@@\n"
        "*** Update File: /tmp/patch_parser_test/test_file2.h\n"
        "@@\n"
        "-#define BAR 2\n"
        "+#define BAR 99\n"
        "@@\n"
        "*** End Patch\n";

    // Parse and apply
    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);
    assert(patch->operation_count == 2);

    ConversationState state = {0};
    state.working_dir = strdup(TEST_DIR);

    cJSON *result = apply_patch(patch, &state);
    assert(result != NULL);

    // Verify both files were modified
    char *content1 = read_test_file(TEST_FILE_1);
    char *content2 = read_test_file(TEST_FILE_2);
    assert(content1 != NULL);
    assert(content2 != NULL);
    assert(strstr(content1, "foo = 42") != NULL);
    assert(strstr(content2, "BAR 99") != NULL);

    free(content1);
    free(content2);
    cJSON_Delete(result);
    free_parsed_patch(patch);
    free(state.working_dir);
    cleanup_test_dir();

    printf("PASSED\n");
}

// Test 7: Handle error when old content not found
static void test_error_content_not_found(void) {
    printf("Test 7: Handle error when old content not found... ");

    setup_test_dir();

    // Create file with different content
    write_test_file(TEST_FILE_1, "int bar = 2;\n");

    // Create patch looking for different content
    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: /tmp/patch_parser_test/test_file1.c\n"
        "@@\n"
        "-int foo = 1;\n"
        "+int foo = 42;\n"
        "@@\n"
        "*** End Patch\n";

    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);

    ConversationState state = {0};
    state.working_dir = strdup(TEST_DIR);

    // Should fail to apply
    cJSON *result = apply_patch(patch, &state);
    assert(result != NULL);
    assert(cJSON_GetObjectItem(result, "error") != NULL);

    cJSON_Delete(result);
    free_parsed_patch(patch);
    free(state.working_dir);
    cleanup_test_dir();

    printf("PASSED\n");
}

// Test 8: Parse patch with multiple lines in old/new content
static void test_parse_multiline_content(void) {
    printf("Test 8: Parse multiline content... ");

    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: src/test.c\n"
        "@@\n"
        "-int foo() {\n"
        "-    return 1;\n"
        "-}\n"
        "+int bar() {\n"
        "+    return 2;\n"
        "+}\n"
        "@@\n"
        "*** End Patch\n";

    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);
    assert(patch->operation_count == 1);
    assert(strstr(patch->operations[0].old_content, "foo()") != NULL);
    assert(strstr(patch->operations[0].old_content, "return 1") != NULL);
    assert(strstr(patch->operations[0].new_content, "bar()") != NULL);
    assert(strstr(patch->operations[0].new_content, "return 2") != NULL);

    free_parsed_patch(patch);

    printf("PASSED\n");
}

// Test 9: Real-world example from problem description
static void test_realworld_example(void) {
    printf("Test 9: Real-world example from problem description... ");

    setup_test_dir();

    // Create initial file matching the example
    const char *initial_content =
        "typedef struct ConversationState {\n"
        "    char **additional_dirs;         // Array of additional working directory paths\n"
        "    int additional_dirs_capacity;   // Capacity of additional_dirs array\n"
        "} ConversationState;\n";
    write_test_file(TEST_FILE_1, initial_content);

    // Real-world patch format from o4-mini (simplified for our parser)
    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: /tmp/patch_parser_test/test_file1.c\n"
        "@@\n"
        "-    int additional_dirs_capacity;   // Capacity of additional_dirs array\n"
        "+    int additional_dirs_capacity;   // Capacity of additional_dirs array\n"
        "+    // Toggle for enabling extra reasoning effort in LLM requests\n"
        "+    int thinking_mode;              // 0=off, 1=on; when enabled, include reasoning_effort in requests\n"
        "@@\n"
        "*** End Patch\n";

    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);

    ConversationState state = {0};
    state.working_dir = strdup(TEST_DIR);

    cJSON *result = apply_patch(patch, &state);
    assert(result != NULL);

    // Verify the patch was applied
    char *new_content = read_test_file(TEST_FILE_1);
    assert(new_content != NULL);
    assert(strstr(new_content, "thinking_mode") != NULL);

    free(new_content);
    cJSON_Delete(result);
    free_parsed_patch(patch);
    free(state.working_dir);
    cleanup_test_dir();

    printf("PASSED\n");
}

int main(void) {
    printf("\n=== Patch Parser Unit Tests ===\n\n");

    test_detect_valid_patch();
    test_reject_invalid_format();
    test_parse_single_operation();
    test_parse_multiple_operations();
    test_apply_single_patch();
    test_apply_multiple_patches();
    test_error_content_not_found();
    test_parse_multiline_content();
    test_realworld_example();

    printf("\n=== All tests passed! ===\n\n");

    return 0;
}
