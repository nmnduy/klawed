/*
 * tool_utils.c - Helper utilities for tool argument summarization
 */

#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

#include "tool_utils.h"

static void safe_copy_with_ellipsis(const char *src, char *out, size_t outsz) {
    if (!out || outsz == 0) return;
    if (!src) { out[0] = '\0'; return; }

    size_t max_no_nul = outsz > 0 ? outsz - 1 : 0;
    size_t len = strlen(src);

    if (len <= max_no_nul) {
        memcpy(out, src, len);
        out[len] = '\0';
        return;
    }

    if (outsz > 4) {
        size_t head = outsz - 4; // room for '...' and NUL
        memcpy(out, src, head);
        out[head] = '.';
        out[head+1] = '.';
        out[head+2] = '.';
        out[head+3] = '\0';
    } else {
        // Not enough room for ellipsis; hard truncate
        memcpy(out, src, max_no_nul);
        out[max_no_nul] = '\0';
    }
}

int summarize_bash_command(const char *cmd, char *out, size_t outsz) {
    if (!out || outsz == 0) return -1;
    out[0] = '\0';
    if (!cmd) return 0;

    // Skip leading whitespace
    const char *p = cmd;
    while (*p == ' ' || *p == '\t') p++;

    const char *chosen = p;

    // Detect leading 'cd <dir>'
    if (strncmp(p, "cd ", 3) == 0) {
        p += 3;
        while (*p == ' ' || *p == '\t') p++;

        const char *dir_start = p;
        const char *dir_end = NULL;
        char quote = 0;
        if (*p == '\'' || *p == '"') {
            quote = *p++;
            dir_start = p;
            while (*p && *p != quote) p++;
            dir_end = p;
            if (*p == quote) p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && strncmp(p, "&&", 2) != 0 && *p != ';' && *p != '\n') {
                p++;
            }
            dir_end = p;
        }

        // Skip whitespace after path
        while (*p == ' ' || *p == '\t') p++;

        // Accept separators '&&' or ';' before the rest of the command
        const char *rest = NULL;
        if (strncmp(p, "&&", 2) == 0) {
            rest = p + 2;
        } else if (*p == ';') {
            rest = p + 1;
        }

        if (dir_start && dir_end && dir_end >= dir_start) {
            size_t dir_len = (size_t)(dir_end - dir_start);
            if (dir_len > 0) {
                char *dir_buf = (char*)malloc(dir_len + 1);
                if (dir_buf) {
                    memcpy(dir_buf, dir_start, dir_len);
                    dir_buf[dir_len] = '\0';

                    char *cwd = getcwd(NULL, 0);
                    char *cwd_real = cwd ? realpath(cwd, NULL) : NULL;
                    char *dir_real = realpath(dir_buf, NULL);

                    int same = 0;
                    if (dir_real && cwd_real) {
                        same = (strcmp(dir_real, cwd_real) == 0);
                    } else if (cwd && dir_buf) {
                        same = (strcmp(dir_buf, cwd) == 0);
                    }

                    if (same && rest) {
                        while (*rest == ' ' || *rest == '\t') rest++;
                        chosen = rest; // may be empty string
                    }

                    free(dir_real);
                    free(cwd_real);
                    free(cwd);
                    free(dir_buf);
                }
            }
        }
    }

    safe_copy_with_ellipsis(chosen, out, outsz);
    return 0;
}

