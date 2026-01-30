/*
 * print_helpers.c - Console output helpers implementation
 *
 * Formatted console output with colorscheme support.
 */

#define COLORSCHEME_EXTERN  // Use extern declarations for colorscheme globals
#include <stdio.h>
#include <string.h>
#include "print_helpers.h"
#include "../colorscheme.h"
#include "../fallback_colors.h"
#include "../logger.h"

void print_assistant(const char *text) {
    // Use accent color for role name, foreground for main text
    char role_color_code[32] = {0};
    char text_color_code[32] = {0};
    const char *role_color_start = NULL;
    const char *text_color_start = NULL;

    // Get accent color for role name
    if (get_colorscheme_color(COLORSCHEME_ASSISTANT, role_color_code, sizeof(role_color_code)) == 0) {
        role_color_start = role_color_code;
    } else {
        LOG_WARN("Using fallback ANSI color for ASSISTANT");
        role_color_start = ANSI_FALLBACK_ASSISTANT;
    }

    // Get foreground color for main text
    if (get_colorscheme_color(COLORSCHEME_FOREGROUND, text_color_code, sizeof(text_color_code)) == 0) {
        text_color_start = text_color_code;
    } else {
        LOG_WARN("Using fallback ANSI color for FOREGROUND");
        text_color_start = ANSI_FALLBACK_FOREGROUND;
    }

    printf("%s[Assistant]%s %s%s%s\n", role_color_start, ANSI_RESET, text_color_start, text, ANSI_RESET);
    fflush(stdout);
}

void print_tool(const char *tool_name, const char *details) {
    // Use status color for tool indicator (reduce rainbow), foreground for details
    char status_color_code[32] = {0};
    char text_color_code[32] = {0};
    const char *tool_color_start = NULL;
    const char *text_color_start = NULL;

    // Use STATUS color for the [Tool: ...] tag
    if (get_colorscheme_color(COLORSCHEME_STATUS, status_color_code, sizeof(status_color_code)) == 0) {
        tool_color_start = status_color_code;
    } else {
        LOG_WARN("Using fallback ANSI color for STATUS (tool tag)");
        tool_color_start = ANSI_FALLBACK_STATUS;
    }

    // Get foreground color for details
    if (get_colorscheme_color(COLORSCHEME_FOREGROUND, text_color_code, sizeof(text_color_code)) == 0) {
        text_color_start = text_color_code;
    } else {
        LOG_WARN("Using fallback ANSI color for FOREGROUND");
        text_color_start = ANSI_FALLBACK_FOREGROUND;
    }

    printf("%s● %s%s", tool_color_start, tool_name, ANSI_RESET);
    if (details && strlen(details) > 0) {
        printf(" %s%s%s", text_color_start, details, ANSI_RESET);
    }
    printf("\n");
    fflush(stdout);
}

void print_error(const char *text) {
    // Log to file only (no stderr output)
    LOG_ERROR("%s", text);
}

void print_status(const char *text) {
    // Use status color when not in TUI for consistency with tips
    char status_color_buf[32] = {0};
    const char *status_color = NULL;
    if (get_colorscheme_color(COLORSCHEME_STATUS, status_color_buf, sizeof(status_color_buf)) == 0) {
        status_color = status_color_buf;
    } else {
        LOG_WARN("Using fallback ANSI color for STATUS (print_status)");
        status_color = ANSI_FALLBACK_STATUS;
    }
    printf("%s[Status]%s %s\n", status_color, ANSI_RESET, text);
}
