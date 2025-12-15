/*
 * test_todo_write.c - Test the TodoWrite tool implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/todo.h"
#include "../src/claude_internal.h"
#include <cjson/cJSON.h>

// External tool function
extern cJSON* tool_todo_write(cJSON *params, ConversationState *state);

void test_basic_todo_write(void) {
    printf("Test: Basic TodoWrite functionality\n");

    // Initialize conversation state with todo list
    ConversationState state = {0};
    assert(conversation_state_init(&state) == 0);
    assert(conversation_state_init(&state) == 0);
    assert(conversation_state_init(&state) == 0);
    assert(conversation_state_init(&state) == 0);
    TodoList list;
    todo_init(&list);
    state.todo_list = &list;

    // Create JSON params
    const char *json_str =
        "{"
        "  \"todos\": ["
        "    {"
        "      \"content\": \"First task\","
        "      \"activeForm\": \"Doing first task\","
        "      \"status\": \"pending\""
        "    },"
        "    {"
        "      \"content\": \"Second task\","
        "      \"activeForm\": \"Doing second task\","
        "      \"status\": \"in_progress\""
        "    },"
        "    {"
        "      \"content\": \"Third task\","
        "      \"activeForm\": \"Doing third task\","
        "      \"status\": \"completed\""
        "    }"
        "  ]"
        "}";

    cJSON *params = cJSON_Parse(json_str);
    assert(params != NULL);

    // Call tool
    cJSON *result = tool_todo_write(params, &state);
    assert(result != NULL);

    // Check result
    cJSON *status = cJSON_GetObjectItem(result, "status");
    assert(status != NULL);
    assert(strcmp(status->valuestring, "success") == 0);

    cJSON *added = cJSON_GetObjectItem(result, "added");
    assert(added != NULL);
    assert(added->valueint == 3);

    // Verify todo list contents
    assert(list.count == 3);
    assert(strcmp(list.items[0].content, "First task") == 0);
    assert(list.items[0].status == TODO_PENDING);
    assert(strcmp(list.items[1].content, "Second task") == 0);
    assert(list.items[1].status == TODO_IN_PROGRESS);
    assert(strcmp(list.items[2].content, "Third task") == 0);
    assert(list.items[2].status == TODO_COMPLETED);

    // Cleanup
    cJSON_Delete(params);
    cJSON_Delete(result);
    todo_free(&list);
    conversation_state_destroy(&state);
    conversation_state_destroy(&state);
    conversation_state_destroy(&state);
    conversation_state_destroy(&state);

    printf("  ✓ Basic TodoWrite test passed\n");
}

void test_replace_todos(void) {
    printf("Test: Replace existing todos\n");

    // Initialize with existing todos
    ConversationState state = {0};
    TodoList list;
    todo_init(&list);
    todo_add(&list, "Old task 1", "Doing old task 1", TODO_PENDING);
    todo_add(&list, "Old task 2", "Doing old task 2", TODO_COMPLETED);
    state.todo_list = &list;

    assert(list.count == 2);

    // Replace with new todos
    const char *json_str =
        "{"
        "  \"todos\": ["
        "    {"
        "      \"content\": \"New task\","
        "      \"activeForm\": \"Doing new task\","
        "      \"status\": \"pending\""
        "    }"
        "  ]"
        "}";

    cJSON *params = cJSON_Parse(json_str);
    cJSON *result = tool_todo_write(params, &state);

    // Verify old todos are replaced
    assert(list.count == 1);
    assert(strcmp(list.items[0].content, "New task") == 0);

    // Cleanup
    cJSON_Delete(params);
    cJSON_Delete(result);
    todo_free(&list);

    printf("  ✓ Replace todos test passed\n");
}

void test_invalid_status(void) {
    printf("Test: Invalid status handling\n");

    ConversationState state = {0};
    TodoList list;
    todo_init(&list);
    state.todo_list = &list;

    // Try to add todo with invalid status
    const char *json_str =
        "{"
        "  \"todos\": ["
        "    {"
        "      \"content\": \"Valid task\","
        "      \"activeForm\": \"Doing valid task\","
        "      \"status\": \"pending\""
        "    },"
        "    {"
        "      \"content\": \"Invalid task\","
        "      \"activeForm\": \"Doing invalid task\","
        "      \"status\": \"invalid_status\""
        "    }"
        "  ]"
        "}";

    cJSON *params = cJSON_Parse(json_str);
    cJSON *result = tool_todo_write(params, &state);

    // Should only add the valid one
    cJSON *added = cJSON_GetObjectItem(result, "added");
    assert(added->valueint == 1);
    assert(list.count == 1);
    assert(strcmp(list.items[0].content, "Valid task") == 0);

    // Cleanup
    cJSON_Delete(params);
    cJSON_Delete(result);
    todo_free(&list);

    printf("  ✓ Invalid status test passed\n");
}

void test_empty_todos(void) {
    printf("Test: Empty todos array\n");

    ConversationState state = {0};
    TodoList list;
    todo_init(&list);
    todo_add(&list, "Existing task", "Doing existing task", TODO_PENDING);
    state.todo_list = &list;

    const char *json_str = "{\"todos\": []}";

    cJSON *params = cJSON_Parse(json_str);
    cJSON *result = tool_todo_write(params, &state);

    // Should clear all todos
    assert(list.count == 0);

    // Cleanup
    cJSON_Delete(params);
    cJSON_Delete(result);
    todo_free(&list);

    printf("  ✓ Empty todos test passed\n");
}

int main(void) {
    printf("Running TodoWrite tool tests...\n\n");

    test_basic_todo_write();
    test_replace_todos();
    test_invalid_status();
    test_empty_todos();

    printf("\n✅ All TodoWrite tests passed!\n");
    return 0;
}
