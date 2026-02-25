/*
 * tool_todo.h - TodoWrite tool implementation
 */

#ifndef TOOL_TODO_H
#define TOOL_TODO_H

#include <cjson/cJSON.h>

// Forward declaration of ConversationState
typedef struct ConversationState ConversationState;

/**
 * tool_todo_write - Creates and updates a task list to track progress
 *
 * @param params JSON object with: { "todos": array of todo items }
 * @param state Conversation state containing todo_list
 * @return JSON object with status, added count, total count, and rendered output
 */
cJSON* tool_todo_write(cJSON *params, ConversationState *state);

#endif // TOOL_TODO_H
