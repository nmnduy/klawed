/*
 * TODO List - Task tracking for Claude Code
 *
 * Provides task management similar to the official Claude Code CLI:
 * - Tracks tasks with three states: pending, in_progress, completed
 * - Displays visual task list in TUI
 * - Parses task updates from assistant responses
 */

#ifndef TODO_H
#define TODO_H

#include <stddef.h>

// Task status
typedef enum {
    TODO_PENDING,
    TODO_IN_PROGRESS,
    TODO_COMPLETED
} TodoStatus;

// Individual task item
typedef struct {
    char *content;       // "Run tests"
    char *active_form;   // "Running tests"
    TodoStatus status;
} TodoItem;

// TODO list container
typedef struct TodoList {
    TodoItem *items;
    size_t count;
    size_t capacity;
} TodoList;

// Initialize TODO list
// Returns: 0 on success, -1 on failure
int todo_init(TodoList *list);

// Free TODO list and all items
void todo_free(TodoList *list);

// Add a new TODO item
// Returns: 0 on success, -1 on failure
int todo_add(TodoList *list, const char *content, const char *active_form, TodoStatus status);

// Update status of a TODO item by index
// Returns: 0 on success, -1 on failure
int todo_update_status(TodoList *list, size_t index, TodoStatus status);

// Update status of a TODO item by content match
// Returns: 0 on success, -1 on failure (not found)
int todo_update_by_content(TodoList *list, const char *content, TodoStatus status);

// Remove a TODO item by index
// Returns: 0 on success, -1 on failure
int todo_remove(TodoList *list, size_t index);

// Clear all TODO items
void todo_clear(TodoList *list);

// Get count of items by status
size_t todo_count_by_status(const TodoList *list, TodoStatus status);

// Render TODO list to string (with ANSI colors for terminal output)
// Returns: Allocated string that must be freed by caller, NULL if no todos
char* todo_render_to_string(const TodoList *list);

// Render TODO list to string (plain text, no ANSI codes, for TUI)
// Returns: Allocated string that must be freed by caller, NULL if no todos
char* todo_render_to_string_plain(const TodoList *list);

// Render TODO list to terminal as plain conversation output
void todo_render(const TodoList *list);

// Parse TODO updates from assistant text
// Looks for patterns like "marking X as completed" or "adding todo: Y"
// Returns: Number of updates applied
int todo_parse_from_text(TodoList *list, const char *text);

#endif // TODO_H
