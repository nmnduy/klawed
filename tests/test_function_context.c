/*
 * test_function_context.c - Unit tests for function context @@ marker support
 *
 * Tests the enhanced @@ marker parsing that extracts line numbers and function context
 * for improved patch matching.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/patch_parser.h"
#include "../src/klawed_internal.h"

// Test file paths
#define TEST_DIR "/tmp/function_context_test"
#define TEST_FILE_1 TEST_DIR "/test_file1.c"

// Helper: Create test directory
static void setup_test_dir(void) {
    mkdir(TEST_DIR, 0755);
}

// Helper: Clean up test directory
static void cleanup_test_dir(void) {
    unlink(TEST_FILE_1);
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

// Test 1: Parse @@ marker with line numbers and function context
static void test_parse_at_marker_with_function_context(void) {
    printf("Test 1: Parse @@ marker with line numbers and function context... ");

    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: src/test.c\n"
        "@@ -15,3 +15,4 @@ int main() {\n"
        "-    printf(\"old\");\n"
        "+    printf(\"new\");\n"
        "+    printf(\"extra\");\n"
        "@@\n"
        "*** End Patch\n";

    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);
    assert(patch->operation_count == 1);

    PatchOperation *op = &patch->operations[0];
    assert(strcmp(op->file_path, "src/test.c") == 0);
    assert(op->old_start_line == 15);
    assert(op->old_line_count == 3);
    assert(op->new_start_line == 15);
    assert(op->new_line_count == 4);
    assert(op->function_context != NULL);
    assert(strcmp(op->function_context, "int main() {") == 0);

    free_parsed_patch(patch);

    printf("PASSED\n");
}

// Test 2: Parse @@ marker with function context only (no line numbers)
static void test_parse_at_marker_function_only(void) {
    printf("Test 2: Parse @@ marker with function context only... ");

    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: src/test.c\n"
        "@@ foo_function() @@\n"
        "-    old_code();\n"
        "+    new_code();\n"
        "@@\n"
        "*** End Patch\n";

    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);
    assert(patch->operation_count == 1);

    PatchOperation *op = &patch->operations[0];
    assert(op->old_start_line == -1);  // No line numbers
    assert(op->old_line_count == -1);
    assert(op->new_start_line == -1);
    assert(op->new_line_count == -1);
    assert(op->function_context != NULL);
    assert(strcmp(op->function_context, "foo_function()") == 0);

    free_parsed_patch(patch);

    printf("PASSED\n");
}

// Test 3: Parse @@ marker with line numbers but no function context
static void test_parse_at_marker_lines_only(void) {
    printf("Test 3: Parse @@ marker with line numbers only... ");

    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: src/test.c\n"
        "@@ -20,2 +20,3 @@\n"
        "-    old_line();\n"
        "+    new_line1();\n"
        "+    new_line2();\n"
        "@@\n"
        "*** End Patch\n";

    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);
    assert(patch->operation_count == 1);

    PatchOperation *op = &patch->operations[0];
    assert(op->old_start_line == 20);
    assert(op->old_line_count == 2);
    assert(op->new_start_line == 20);
    assert(op->new_line_count == 3);
    assert(op->function_context == NULL);  // No function context

    free_parsed_patch(patch);

    printf("PASSED\n");
}

// Test 4: Apply patch with function context for improved matching
static void test_apply_patch_with_function_context(void) {
    printf("Test 4: Apply patch with function context... ");

    setup_test_dir();

    // Create initial file with multiple functions containing similar code
    const char *initial_content =
        "int foo() {\n"
        "    printf(\"hello\");\n"
        "    return 1;\n"
        "}\n"
        "\n"
        "int main() {\n"
        "    printf(\"hello\");\n"
        "    return 0;\n"
        "}\n"
        "\n"
        "int bar() {\n"
        "    printf(\"hello\");\n"
        "    return 2;\n"
        "}\n";
    write_test_file(TEST_FILE_1, initial_content);

    // Create patch targeting the main() function specifically
    // This should match the printf in main(), not in foo() or bar()
    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: /tmp/function_context_test/test_file1.c\n"
        "@@ int main() { @@\n"
        "-    printf(\"hello\");\n"
        "+    printf(\"world\");\n"
        "@@\n"
        "*** End Patch\n";

    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);

    ConversationState state = {0};
    state.working_dir = strdup(TEST_DIR);

    cJSON *result = apply_patch(patch, &state);
    assert(result != NULL);
    assert(cJSON_GetObjectItem(result, "status") != NULL);

    // Verify file was modified correctly - only main() should be changed
    char *new_content = read_test_file(TEST_FILE_1);
    assert(new_content != NULL);

    // Count occurrences of "hello" and "world"
    char *pos = new_content;
    int hello_count = 0;
    int world_count = 0;

    while ((pos = strstr(pos, "hello")) != NULL) {
        hello_count++;
        pos += 5; // length of "hello"
    }

    pos = new_content;
    while ((pos = strstr(pos, "world")) != NULL) {
        world_count++;
        pos += 5; // length of "world"
    }

    // Should have 2 "hello" (in foo and bar) and 1 "world" (in main)
    assert(hello_count == 2);
    assert(world_count == 1);

    free(new_content);
    cJSON_Delete(result);
    free_parsed_patch(patch);
    free(state.working_dir);
    cleanup_test_dir();

    printf("PASSED\n");
}

// Test 5: Fallback when function context doesn't match
static void test_function_context_fallback(void) {
    printf("Test 5: Function context fallback to normal search... ");

    setup_test_dir();

    // Create file without the specified function
    const char *initial_content =
        "int foo() {\n"
        "    printf(\"target\");\n"
        "    return 1;\n"
        "}\n";
    write_test_file(TEST_FILE_1, initial_content);

    // Create patch looking for a different function - should fall back to normal search
    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: /tmp/function_context_test/test_file1.c\n"
        "@@ int main() { @@\n"
        "-    printf(\"target\");\n"
        "+    printf(\"modified\");\n"
        "@@\n"
        "*** End Patch\n";

    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);

    ConversationState state = {0};
    state.working_dir = strdup(TEST_DIR);

    // Should still succeed by falling back to normal content search
    cJSON *result = apply_patch(patch, &state);
    assert(result != NULL);
    assert(cJSON_GetObjectItem(result, "status") != NULL);

    // Verify file was still modified
    char *new_content = read_test_file(TEST_FILE_1);
    assert(new_content != NULL);
    assert(strstr(new_content, "modified") != NULL);
    assert(strstr(new_content, "target") == NULL);

    free(new_content);
    cJSON_Delete(result);
    free_parsed_patch(patch);
    free(state.working_dir);
    cleanup_test_dir();

    printf("PASSED\n");
}

// Test 6: Parse complex function context with class names
static void test_parse_class_context(void) {
    printf("Test 6: Parse @@ marker with class context... ");

    const char *patch_content =
        "*** Begin Patch\n"
        "*** Update File: src/MyClass.cpp\n"
        "@@ -100,1 +100,2 @@ MyClass::method_name() @@\n"
        "-    return false;\n"
        "+    return true;\n"
        "+    // Added comment\n"
        "@@\n"
        "*** End Patch\n";

    ParsedPatch *patch = parse_patch_format(patch_content);
    assert(patch != NULL);
    assert(patch->is_valid == 1);
    assert(patch->operation_count == 1);

    PatchOperation *op = &patch->operations[0];
    assert(op->old_start_line == 100);
    assert(op->old_line_count == 1);
    assert(op->new_start_line == 100);
    assert(op->new_line_count == 2);
    assert(op->function_context != NULL);
    assert(strcmp(op->function_context, "MyClass::method_name()") == 0);

    free_parsed_patch(patch);

    printf("PASSED\n");
}

int main(void) {
    printf("\n=== Function Context @@ Marker Tests ===\n\n");

    test_parse_at_marker_with_function_context();
    test_parse_at_marker_function_only();
    test_parse_at_marker_lines_only();
    test_apply_patch_with_function_context();
    test_function_context_fallback();
    test_parse_class_context();

    printf("\n=== All function context tests passed! ===\n\n");

    return 0;
}
