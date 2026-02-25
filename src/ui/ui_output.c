/*
 * ui_output.c - UI abstraction layer implementation
 *
 * Routes UI output to the appropriate destination based on active mode.
 */

#ifndef __APPLE__
    #define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

#include "ui_output.h"
#include "print_helpers.h"
#include "../tui.h"
#include "../message_queue.h"
#include "../logger.h"

void ui_append_line(TUIState *tui,
                    TUIMessageQueue *queue,
                    const char *prefix,
                    const char *text,
                    TUIColorPair color) {
    const char *safe_text = text ? text : "";
    const char *safe_prefix = prefix ? prefix : "";

    if (queue) {
        size_t prefix_len = safe_prefix[0] ? strlen(safe_prefix) : 0;
        size_t text_len = strlen(safe_text);
        size_t extra_space = (prefix_len > 0 && text_len > 0) ? 1 : 0;
        size_t total = prefix_len + extra_space + text_len + 1;

        char *formatted = malloc(total);
        if (!formatted) {
            LOG_ERROR("Failed to allocate memory for TUI message");
            // Fall through to direct UI/console output
        } else {
            if (prefix_len > 0 && text_len > 0) {
                snprintf(formatted, total, "%s %s", safe_prefix, safe_text);
            } else if (prefix_len > 0) {
                snprintf(formatted, total, "%s", safe_prefix);
            } else {
                snprintf(formatted, total, "%s", safe_text);
            }

            if (post_tui_message(queue, TUI_MSG_ADD_LINE, formatted) == 0) {
                free(formatted);
                return;
            }

            LOG_WARN("Failed to enqueue TUI message, falling back to direct render");
            free(formatted);
        }
    }

    if (tui) {
        tui_add_conversation_line(tui, safe_prefix, safe_text, color);
        return;
    }

    // Console mode fallback
    if (strcmp(safe_prefix, "[Assistant]") == 0) {
        print_assistant(safe_text);
        return;
    }

    if (strncmp(safe_prefix, "[Tool", 5) == 0) {
        const char *colon = strchr(safe_prefix, ':');
        const char *close = strrchr(safe_prefix, ']');
        const char *name_start = NULL;
        size_t name_len = 0;
        if (colon) {
            name_start = colon + 1;
            if (*name_start == ' ') {
                name_start++;
            }
            if (close && close > name_start) {
                name_len = (size_t)(close - name_start);
            }
        }

        char tool_name[128] = {0};
        if (name_len == 0 || name_len >= sizeof(tool_name)) {
            snprintf(tool_name, sizeof(tool_name), "tool");
        } else {
            memcpy(tool_name, name_start, name_len);
            tool_name[name_len] = '\0';
        }
        print_tool(tool_name, safe_text);
        return;
    }

    if (strcmp(safe_prefix, "[Error]") == 0) {
        print_error(safe_text);
        return;
    }

    // Generic fallback
    if (safe_prefix[0]) {
        printf("%s %s\n", safe_prefix, safe_text);
    } else {
        printf("%s\n", safe_text);
    }
    fflush(stdout);
    return;
}

void ui_set_status(TUIState *tui,
                   TUIMessageQueue *queue,
                   const char *status_text) {
    const char *safe = status_text ? status_text : "";
    if (queue) {
        if (post_tui_message(queue, TUI_MSG_STATUS, safe) == 0) {
            return;
        }
        LOG_WARN("Failed to enqueue status update, falling back to direct render");
    }

    if (tui) {
        tui_update_status(tui, safe);
        return;
    }

    // Console mode fallback
    if (safe[0] != '\0') {
        print_status(safe);
    }
}

void ui_show_error(TUIState *tui,
                   TUIMessageQueue *queue,
                   const char *error_text) {
    const char *safe = error_text ? error_text : "";
    if (queue) {
        if (post_tui_message(queue, TUI_MSG_ERROR, safe) == 0) {
            return;
        }
        LOG_WARN("Failed to enqueue error message, falling back to direct render");
    }
    if (tui) {
        tui_add_conversation_line(tui, "[Error]", safe, COLOR_PAIR_ERROR);
        return;
    }
    print_error(safe);
}

void ui_set_status_varied(TUIState *tui,
                          TUIMessageQueue *queue,
                          SpinnerMessageContext context) {
    const char *msg = spinner_random_msg_for_context(context);
    ui_set_status(tui, queue, msg);
}

void ui_set_status_for_tool(TUIState *tui,
                            TUIMessageQueue *queue,
                            const char *tool_name) {
    const char *msg = spinner_random_msg_for_tool(tool_name);
    ui_set_status(tui, queue, msg);
}
