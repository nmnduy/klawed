/*
 * Test suite for TODO list functionality
 *
 * Compilation: make test-todo
 * Usage: ./build/test_todo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/todo.h"

// Test helper macros
#define TEST(name) printf("\n=== Test: %s ===\n", name)
#define PASS() printf("✓ PASS\n")
#define FAIL(msg) do { printf("✗ FAIL: %s\n", msg); exit(1); } while(0)

static void test_init_and_free(void) {
    TEST("Initialize and free TODO list");

    TodoList list;
    int result = todo_init(&list);

    assert(result == 0);
    assert(list.items != NULL);
    assert(list.count == 0);
    assert(list.capacity >= 10);

    todo_free(&list);
    assert(list.items == NULL);
    assert(list.count == 0);

    PASS();
}

static void test_add_todos(void) {
    TEST("Add TODO items");

    TodoList list;
    todo_init(&list);

    // Add first item
    int result = todo_add(&list, "Run tests", "Running tests", TODO_PENDING);
    assert(result == 0);
    assert(list.count == 1);
    assert(strcmp(list.items[0].content, "Run tests") == 0);
    assert(strcmp(list.items[0].active_form, "Running tests") == 0);
    assert(list.items[0].status == TODO_PENDING);

    // Add second item
    result = todo_add(&list, "Build project", "Building project", TODO_IN_PROGRESS);
    assert(result == 0);
    assert(list.count == 2);
    assert(list.items[1].status == TODO_IN_PROGRESS);

    // Add third item
    result = todo_add(&list, "Fix bugs", "Fixing bugs", TODO_COMPLETED);
    assert(result == 0);
    assert(list.count == 3);
    assert(list.items[2].status == TODO_COMPLETED);

    todo_free(&list);
    PASS();
}

static void test_update_status(void) {
    TEST("Update TODO status by index");

    TodoList list;
    todo_init(&list);

    todo_add(&list, "Run tests", "Running tests", TODO_PENDING);
    todo_add(&list, "Build project", "Building project", TODO_PENDING);

    // Update first item
    int result = todo_update_status(&list, 0, TODO_IN_PROGRESS);
    assert(result == 0);
    assert(list.items[0].status == TODO_IN_PROGRESS);

    // Update second item
    result = todo_update_status(&list, 1, TODO_COMPLETED);
    assert(result == 0);
    assert(list.items[1].status == TODO_COMPLETED);

    // Try invalid index
    result = todo_update_status(&list, 5, TODO_COMPLETED);
    assert(result == -1);

    todo_free(&list);
    PASS();
}

static void test_update_by_content(void) {
    TEST("Update TODO status by content");

    TodoList list;
    todo_init(&list);

    todo_add(&list, "Run tests", "Running tests", TODO_PENDING);
    todo_add(&list, "Build project", "Building project", TODO_PENDING);

    // Update by content
    int result = todo_update_by_content(&list, "Run tests", TODO_IN_PROGRESS);
    assert(result == 0);
    assert(list.items[0].status == TODO_IN_PROGRESS);

    // Update non-existent item
    result = todo_update_by_content(&list, "Non-existent", TODO_COMPLETED);
    assert(result == -1);

    todo_free(&list);
    PASS();
}

static void test_count_by_status(void) {
    TEST("Count TODOs by status");

    TodoList list;
    todo_init(&list);

    todo_add(&list, "Task 1", "Doing task 1", TODO_PENDING);
    todo_add(&list, "Task 2", "Doing task 2", TODO_PENDING);
    todo_add(&list, "Task 3", "Doing task 3", TODO_IN_PROGRESS);
    todo_add(&list, "Task 4", "Doing task 4", TODO_COMPLETED);
    todo_add(&list, "Task 5", "Doing task 5", TODO_COMPLETED);
    todo_add(&list, "Task 6", "Doing task 6", TODO_COMPLETED);

    assert(todo_count_by_status(&list, TODO_PENDING) == 2);
    assert(todo_count_by_status(&list, TODO_IN_PROGRESS) == 1);
    assert(todo_count_by_status(&list, TODO_COMPLETED) == 3);

    todo_free(&list);
    PASS();
}

static void test_remove_todo(void) {
    TEST("Remove TODO item");

    TodoList list;
    todo_init(&list);

    todo_add(&list, "Task 1", "Doing task 1", TODO_PENDING);
    todo_add(&list, "Task 2", "Doing task 2", TODO_PENDING);
    todo_add(&list, "Task 3", "Doing task 3", TODO_PENDING);

    // Remove middle item
    int result = todo_remove(&list, 1);
    assert(result == 0);
    assert(list.count == 2);
    assert(strcmp(list.items[0].content, "Task 1") == 0);
    assert(strcmp(list.items[1].content, "Task 3") == 0);

    // Try invalid index
    result = todo_remove(&list, 5);
    assert(result == -1);

    todo_free(&list);
    PASS();
}

static void test_clear_todos(void) {
    TEST("Clear all TODOs");

    TodoList list;
    todo_init(&list);

    todo_add(&list, "Task 1", "Doing task 1", TODO_PENDING);
    todo_add(&list, "Task 2", "Doing task 2", TODO_PENDING);
    todo_add(&list, "Task 3", "Doing task 3", TODO_PENDING);

    todo_clear(&list);
    assert(list.count == 0);

    todo_free(&list);
    PASS();
}

static void test_render_visual(void) {
    TEST("Visual rendering test (manual inspection)");

    TodoList list;
    todo_init(&list);

    printf("\nEmpty list (should show nothing):\n");
    todo_render(&list);

    printf("\nList with mixed statuses:\n");
    todo_add(&list, "Initialize project structure", "Initializing project structure", TODO_COMPLETED);
    todo_add(&list, "Implement core functionality", "Implementing core functionality", TODO_IN_PROGRESS);
    todo_add(&list, "Write unit tests", "Writing unit tests", TODO_PENDING);
    todo_add(&list, "Update documentation", "Updating documentation", TODO_PENDING);
    todo_add(&list, "Run CI pipeline", "Running CI pipeline", TODO_PENDING);

    todo_render(&list);

    printf("\nProgressing tasks:\n");
    todo_update_status(&list, 1, TODO_COMPLETED);
    todo_update_status(&list, 2, TODO_IN_PROGRESS);
    todo_render(&list);

    todo_free(&list);
    PASS();
}

int main(void) {
    printf("\n========================================\n");
    printf("TODO List Test Suite\n");
    printf("========================================\n");

    test_init_and_free();
    test_add_todos();
    test_update_status();
    test_update_by_content();
    test_count_by_status();
    test_remove_todo();
    test_clear_todos();
    test_render_visual();

    printf("\n========================================\n");
    printf("✓ All tests passed!\n");
    printf("========================================\n\n");

    return 0;
}
