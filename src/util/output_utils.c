/*
 * Output Utilities
 * Helper functions for formatting and emitting tool output
 */

#include "output_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

#ifndef TEST_BUILD
#include "../logger.h"
#include "../message_queue.h"
#else
// Test build stubs
#define LOG_ERROR(fmt, ...) ((void)0)
#define LOG_WARN(fmt, ...) ((void)0)
#endif

#include "../fallback_colors.h"

// Thread-local state for tool output
static _Thread_local TUIMessageQueue *g_active_tool_queue = NULL;
static _Thread_local int g_oneshot_mode = 0;

/**
 * Set the active tool queue for output
 */
void output_set_tool_queue(TUIMessageQueue *queue) {
    g_active_tool_queue = queue;
}

/**
 * Get the current tool queue
 */
TUIMessageQueue* output_get_tool_queue(void) {
    return g_active_tool_queue;
}

/**
 * Set oneshot mode flag
 */
void output_set_oneshot_mode(int enabled) {
    g_oneshot_mode = enabled;
}

/**
 * Get oneshot mode flag
 */
int output_get_oneshot_mode(void) {
    return g_oneshot_mode;
}

/**
 * Emit a line of tool output
 */
void tool_emit_line(const char *prefix, const char *text) {
    const char *safe_prefix = prefix ? prefix : "";
    const char *safe_text = text ? text : "";

    if (g_active_tool_queue) {
        size_t prefix_len = safe_prefix[0] ? strlen(safe_prefix) : 0;
        size_t text_len = strlen(safe_text);
        size_t extra = (prefix_len > 0 && text_len > 0) ? 1 : 0;
        size_t total = prefix_len + extra + text_len + 1;

        char *formatted = malloc(total);
        if (!formatted) {
            LOG_ERROR("Failed to allocate tool output buffer");
            return;
        }

        if (prefix_len > 0 && text_len > 0) {
            snprintf(formatted, total, "%s %s", safe_prefix, safe_text);
        } else if (prefix_len > 0) {
            snprintf(formatted, total, "%s", safe_prefix);
        } else {
            snprintf(formatted, total, "%s", safe_text);
        }

        if (post_tui_message(g_active_tool_queue, TUI_MSG_ADD_LINE, formatted) != 0) {
            LOG_WARN("Failed to post tool output to TUI queue");
        }
        free(formatted);
        return;
    }

    // In oneshot/subagent mode, suppress individual line output
    // Tool output will be captured and wrapped in HTML-style tags by the caller
    if (g_oneshot_mode) {
        return;
    }

    if (safe_prefix[0] && safe_text[0]) {
        printf("%s %s\n", safe_prefix, safe_text);
    } else if (safe_prefix[0]) {
        printf("%s\n", safe_prefix);
    } else {
        printf("%s\n", safe_text);
    }
    fflush(stdout);
}

/**
 * Emit a diff line with appropriate coloring
 */
void emit_diff_line(const char *line,
                    const char *add_color,
                    const char *remove_color) {
    if (!line) {
        return;
    }

    // Trim trailing newlines
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        len--;
    }

    if (len == 0) {
        return;
    }

    char *trimmed = strndup(line, len);
    if (!trimmed) {
        LOG_ERROR("Failed to allocate trimmed diff line");
        return;
    }

    // Print with indentation and color if applicable
    if (g_active_tool_queue) {
        // For TUI mode: just pass the line as-is
        // The TUI will detect diff prefixes and color appropriately
        tool_emit_line("", trimmed);
    } else if (!g_oneshot_mode) {
        // For non-TUI mode (direct stdout): use ANSI color codes
        // Skip output in oneshot/subagent mode as it will be captured in JSON
        const char *color = NULL;
        if (trimmed[0] == '+' && trimmed[1] != '+') {
            color = add_color;
        } else if (trimmed[0] == '-' && trimmed[1] != '-') {
            color = remove_color;
        }

        if (color) {
            printf("  %s%s%s\n", color, trimmed, ANSI_RESET);
        } else {
            printf("  %s\n", trimmed);
        }
    }

    free(trimmed);
}
