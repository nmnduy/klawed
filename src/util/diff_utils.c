/*
 * Diff Utilities
 * Helper functions for generating and displaying diffs
 */

#include "diff_utils.h"
#include "output_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#ifndef TEST_BUILD
#include "../logger.h"
// Use extern declarations for global theme variables
#define COLORSCHEME_EXTERN 1
#include "../colorscheme.h"
#include "../fallback_colors.h"
#else
// Test build stubs
#define LOG_ERROR(fmt, ...) ((void)0)
#define LOG_WARN(fmt, ...) ((void)0)
#define ANSI_FALLBACK_DIFF_ADD "\033[32m"
#define ANSI_FALLBACK_DIFF_REMOVE "\033[31m"
typedef enum {
    COLORSCHEME_DIFF_ADD = 0,
    COLORSCHEME_DIFF_REMOVE = 1
} ColorSchemeColor;
static inline int get_colorscheme_color(ColorSchemeColor color_type, char *buffer, size_t buffer_size) {
    (void)color_type; (void)buffer; (void)buffer_size; return -1;
}
#endif

/**
 * Show unified diff between original content and current file
 */
int show_diff(const char *file_path, const char *original_content) {
    // Create temporary file for original content
    char temp_path[PATH_MAX];
    snprintf(temp_path, sizeof(temp_path), "%s.klawed_diff.XXXXXX", file_path);

    int fd = mkstemp(temp_path);
    if (fd == -1) {
        LOG_ERROR("Failed to create temporary file for diff");
        return -1;
    }

    // Write original content to temp file
    size_t content_len = strlen(original_content);
    ssize_t written = write(fd, original_content, content_len);
    close(fd);

    if (written < 0 || (size_t)written != content_len) {
        LOG_ERROR("Failed to write original content to temp file");
        unlink(temp_path);
        return -1;
    }

    // Run diff command to show changes
    char diff_cmd[PATH_MAX * 2];
    snprintf(diff_cmd, sizeof(diff_cmd), "diff -u \"%s\" \"%s\"", temp_path, file_path);

    FILE *pipe = popen(diff_cmd, "r");
    if (!pipe) {
        LOG_ERROR("Failed to run diff command");
        unlink(temp_path);
        return -1;
    }

    // Get color codes for added and removed lines
    char add_color[32], remove_color[32];
    const char *add_color_str, *remove_color_str;

#ifndef TEST_BUILD
    // Try to get colors from colorscheme, fall back to ANSI colors
    if (get_colorscheme_color(COLORSCHEME_DIFF_ADD, add_color, sizeof(add_color)) == 0) {
        add_color_str = add_color;
    } else {
        LOG_WARN("Using fallback ANSI color for DIFF_ADD");
        add_color_str = ANSI_FALLBACK_DIFF_ADD;
    }

    if (get_colorscheme_color(COLORSCHEME_DIFF_REMOVE, remove_color, sizeof(remove_color)) == 0) {
        remove_color_str = remove_color;
    } else {
        LOG_WARN("Using fallback ANSI color for DIFF_REMOVE");
        remove_color_str = ANSI_FALLBACK_DIFF_REMOVE;
    }
#else
    add_color_str = ANSI_FALLBACK_DIFF_ADD;
    remove_color_str = ANSI_FALLBACK_DIFF_REMOVE;
#endif

    // Read and display diff output with simple indentation
    char line[1024];
    int has_diff = 0;

    while (fgets(line, sizeof(line), pipe)) {
        has_diff = 1;
        emit_diff_line(line, add_color_str, remove_color_str);
    }

    int result = pclose(pipe);
    unlink(temp_path);

    if (!has_diff) {
        tool_emit_line(" ", "(No changes - files are identical)");
    } else if (result == 0) {
        // diff returns 0 when files are identical, 1 when different, >1 for errors
        // We already checked has_diff, so this shouldn't happen
    }

    return 0;
}
