/*
 * TODO List Implementation
 */

#include "todo.h"
#include "fallback_colors.h"
#define COLORSCHEME_EXTERN
#include "colorscheme.h"
#include "logger.h"
#include <stdlib.h>
#include <bsd/stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define INITIAL_CAPACITY 10

// Helper: Duplicate a string
static char* str_dup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, str, len + 1);
    }
    return dup;
}

int todo_init(TodoList *list) {
    if (!list) return -1;

    // Initialize count and capacity first (even if allocation fails)
    list->count = 0;
    list->capacity = INITIAL_CAPACITY;

    list->items = malloc(INITIAL_CAPACITY * sizeof(TodoItem));
    if (!list->items) {
        // Allocation failed, but count and capacity are already set to safe values
        return -1;
    }

    return 0;
}

void todo_free(TodoList *list) {
    if (!list) return;

    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].content);
        free(list->items[i].active_form);
    }
    free(list->items);

    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

int todo_add(TodoList *list, const char *content, const char *active_form, TodoStatus status) {
    if (!list || !content || !active_form) return -1;

    // Allocate items array if it's NULL (e.g., after a failed todo_init)
    if (!list->items) {
        // Ensure capacity is at least INITIAL_CAPACITY
        if (list->capacity < INITIAL_CAPACITY) {
            list->capacity = INITIAL_CAPACITY;
        }
        list->items = reallocarray(NULL, list->capacity, sizeof(TodoItem));
        if (!list->items) return -1;
    }

    // Expand capacity if needed
    if (list->count >= list->capacity) {
        size_t new_capacity = list->capacity * 2;
        TodoItem *new_items = reallocarray(list->items, new_capacity, sizeof(TodoItem));
        if (!new_items) return -1;

        list->items = new_items;
        list->capacity = new_capacity;
    }

    // Add new item
    TodoItem *item = &list->items[list->count];
    item->content = str_dup(content);
    item->active_form = str_dup(active_form);
    item->status = status;

    if (!item->content || !item->active_form) {
        free(item->content);
        free(item->active_form);
        return -1;
    }

    list->count++;
    return 0;
}

int todo_update_status(TodoList *list, size_t index, TodoStatus status) {
    if (!list || !list->items || index >= list->count) return -1;

    list->items[index].status = status;
    return 0;
}

int todo_update_by_content(TodoList *list, const char *content, TodoStatus status) {
    if (!list || !content || !list->items) return -1;

    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].content, content) == 0) {
            list->items[i].status = status;
            return 0;
        }
    }

    return -1;  // Not found
}

int todo_remove(TodoList *list, size_t index) {
    if (!list || !list->items || index >= list->count) return -1;

    // Free the item being removed
    free(list->items[index].content);
    free(list->items[index].active_form);

    // Shift remaining items down
    for (size_t i = index; i < list->count - 1; i++) {
        list->items[i] = list->items[i + 1];
    }

    list->count--;
    return 0;
}

void todo_clear(TodoList *list) {
    if (!list || !list->items) return;

    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].content);
        free(list->items[i].active_form);
    }

    list->count = 0;
}

size_t todo_count_by_status(const TodoList *list, TodoStatus status) {
    if (!list || !list->items) return 0;

    size_t count = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].status == status) {
            count++;
        }
    }
    return count;
}

char* todo_render_to_string(const TodoList *list) {
    if (!list || list->count == 0) {
        return NULL;  // No todos to display
    }

    // Calculate approximate buffer size needed
    // Account for ANSI codes (up to 20 chars per color code) + indentation
    size_t buffer_size = 256;  // Base size for intro text
    for (size_t i = 0; i < list->count; i++) {
        buffer_size += strlen(list->items[i].content) + strlen(list->items[i].active_form) + 150;
    }

    char *result = malloc(buffer_size);
    if (!result) {
        LOG_ERROR("Failed to allocate memory for todo render string");
        return NULL;
    }

    size_t offset = 0;

    // Get color codes for bullet points
    char color_completed[32] = {0};
    char color_in_progress[32] = {0};
    char color_pending[32] = {0};
    char color_foreground[32] = {0};

    // Try to get colors from theme, fall back to ANSI codes
    if (get_colorscheme_color(COLORSCHEME_USER, color_completed, sizeof(color_completed)) != 0) {
        snprintf(color_completed, sizeof(color_completed), "%s", ANSI_FALLBACK_GREEN);
    }
    if (get_colorscheme_color(COLORSCHEME_STATUS, color_in_progress, sizeof(color_in_progress)) != 0) {
        snprintf(color_in_progress, sizeof(color_in_progress), "%s", ANSI_FALLBACK_YELLOW);
    }
    if (get_colorscheme_color(COLORSCHEME_ASSISTANT, color_pending, sizeof(color_pending)) != 0) {
        snprintf(color_pending, sizeof(color_pending), "%s", ANSI_FALLBACK_CYAN);
    }
    if (get_colorscheme_color(COLORSCHEME_FOREGROUND, color_foreground, sizeof(color_foreground)) != 0) {
        snprintf(color_foreground, sizeof(color_foreground), "%s", ANSI_FALLBACK_FOREGROUND);
    }

    // Render each item with colored bullets and indentation
    for (size_t i = 0; i < list->count; i++) {
        const TodoItem *item = &list->items[i];

        switch (item->status) {
            default:
                LOG_WARN("Unknown TODO status: %d", (int)item->status);
                offset += (size_t)snprintf(result + offset, buffer_size - offset,
                                 "    %s• %s%s\n",
                                 color_foreground, ANSI_RESET, item->content);
                break;
            case TODO_COMPLETED:
                offset += (size_t)snprintf(result + offset, buffer_size - offset,
                                 "    %s✓%s %s\n",
                                 color_completed, color_foreground, item->content);
                break;

            case TODO_IN_PROGRESS:
                offset += (size_t)snprintf(result + offset, buffer_size - offset,
                                 "    %s⋯%s %s\n",
                                 color_in_progress, color_foreground, item->active_form);
                break;

            case TODO_PENDING:
                offset += (size_t)snprintf(result + offset, buffer_size - offset,
                                 "    %s○%s %s\n",
                                 color_pending, color_foreground, item->content);
                break;
        }
    }

    // Remove trailing newline if present
    if (offset > 0 && offset <= buffer_size && result[offset - 1] == '\n') {
        result[offset - 1] = '\0';
    }

    return result;
}

char* todo_render_to_string_plain(const TodoList *list) {
    if (!list || list->count == 0) {
        return NULL;  // No todos to display
    }

    // Calculate approximate buffer size needed
    size_t buffer_size = 256;  // Base size for intro text
    for (size_t i = 0; i < list->count; i++) {
        buffer_size += strlen(list->items[i].content) + strlen(list->items[i].active_form) + 50;
    }

    char *result = malloc(buffer_size);
    if (!result) {
        LOG_ERROR("Failed to allocate memory for todo render string (plain)");
        return NULL;
    }

    size_t offset = 0;

    // Render each item with plain text bullets (no ANSI codes)
    for (size_t i = 0; i < list->count; i++) {
        const TodoItem *item = &list->items[i];

        switch (item->status) {
            default:
                LOG_WARN("Unknown TODO status: %d", (int)item->status);
                offset += (size_t)snprintf(result + offset, buffer_size - offset,
                                 "    • %s\n", item->content);
                break;
            case TODO_COMPLETED:
                offset += (size_t)snprintf(result + offset, buffer_size - offset,
                                 "    ✓ %s\n", item->content);
                break;

            case TODO_IN_PROGRESS:
                offset += (size_t)snprintf(result + offset, buffer_size - offset,
                                 "    ⋯ %s\n", item->active_form);
                break;

            case TODO_PENDING:
                offset += (size_t)snprintf(result + offset, buffer_size - offset,
                                 "    ○ %s\n", item->content);
                break;
        }
    }

    // Remove trailing newline if present
    if (offset > 0 && offset <= buffer_size && result[offset - 1] == '\n') {
        result[offset - 1] = '\0';
    }

    return result;
}

void todo_render(const TodoList *list) {
    if (!list || list->count == 0) {
        return;  // No todos to display
    }

    char *text = todo_render_to_string(list);
    if (!text) {
        return;
    }

    printf("%s\n", text);
    fflush(stdout);
    free(text);
}

// Simple parser for TODO updates from text
// Looks for common patterns like:
// - "marking X as completed"
// - "marking X as in_progress"
// - "adding todo: X"
int todo_parse_from_text(TodoList *list, const char *text) {
    if (!list || !text) return 0;

    int updates = 0;
    // TODO: Implement parsing logic
    // For now, this is a stub - we'll manually call todo functions
    // In the future, this could parse natural language from assistant responses

    return updates;
}
