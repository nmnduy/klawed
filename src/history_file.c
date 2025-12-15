/*
 * history_file.c - Flat-file based input history (one entry per line)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>

#include "history_file.h"
#include "logger.h"

struct HistoryFile {
    char *path;
    FILE *fp; // append handle
};

static int mkdir_recursive(const char *path) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len == 0) return -1;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

char* history_file_default_path(void) {
    char *path = NULL;
    const char *env = getenv("CLAUDE_C_HISTORY_FILE_PATH");
    if (env && *env) return strdup(env);

    struct stat st;
    if (stat("./.claude-c", &st) == 0 && S_ISDIR(st.st_mode)) {
        return strdup("./.claude-c/input_history.txt");
    }
    if (mkdir("./.claude-c", 0755) == 0 || errno == EEXIST) {
        return strdup("./.claude-c/input_history.txt");
    }

    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        path = malloc(PATH_MAX);
        if (path) {
            snprintf(path, PATH_MAX, "%s/claude-c/input_history.txt", xdg);
            return path;
        }
    }

    const char *home = getenv("HOME");
    if (home && *home) {
        path = malloc(PATH_MAX);
        if (path) {
            snprintf(path, PATH_MAX, "%s/.local/share/claude-c/input_history.txt", home);
            return path;
        }
    }
    return strdup("./input_history.txt");
}

HistoryFile* history_file_open(const char *path) {
    HistoryFile *hf = calloc(1, sizeof(HistoryFile));
    if (!hf) return NULL;

    if (path && *path) hf->path = strdup(path);
    else hf->path = history_file_default_path();
    if (!hf->path) { free(hf); return NULL; }

    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", hf->path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdir_recursive(dir); }

    hf->fp = fopen(hf->path, "a");
    if (!hf->fp) {
        LOG_WARN("[HIST] Failed to open history file for append: %s", hf->path);
        free(hf->path);
        free(hf);
        return NULL;
    }
    // Use line buffering; no fsync to keep things fast
    setvbuf(hf->fp, NULL, _IOLBF, 0);
    return hf;
}

void history_file_close(HistoryFile *hf) {
    if (!hf) return;
    if (hf->fp) fclose(hf->fp);
    free(hf->path);
    free(hf);
}

// Escape newlines in text for storage
#ifdef TEST_BUILD
char* escape_newlines(const char *text) {
#else
static char* escape_newlines(const char *text) {
#endif
    if (!text) return NULL;

    // Count how many newlines we need to escape
    size_t newline_count = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') newline_count++;
    }

    // If no newlines, just return a copy
    if (newline_count == 0) {
        return strdup(text);
    }

    // Allocate buffer for escaped text (each \n becomes \\n)
    size_t escaped_len = strlen(text) + newline_count + 1;
    char *escaped = malloc(escaped_len);
    if (!escaped) return NULL;

    // Perform escaping
    char *dest = escaped;
    for (const char *src = text; *src; src++) {
        if (*src == '\n') {
            *dest++ = '\\';
            *dest++ = 'n';
        } else {
            *dest++ = *src;
        }
    }
    *dest = '\0';

    return escaped;
}

// Unescape newlines when loading from storage
#ifdef TEST_BUILD
char* unescape_newlines(const char *escaped_text) {
#else
static char* unescape_newlines(const char *escaped_text) {
#endif
    if (!escaped_text) return NULL;

    // Count how many escape sequences we have
    size_t escape_count = 0;
    for (const char *p = escaped_text; *p; p++) {
        if (*p == '\\' && *(p + 1) == 'n') {
            escape_count++;
            p++; // Skip the 'n'
        }
    }

    // If no escapes, just return a copy
    if (escape_count == 0) {
        return strdup(escaped_text);
    }

    // Allocate buffer for unescaped text (each \\n becomes \n)
    size_t unescaped_len = strlen(escaped_text) - escape_count + 1;
    char *unescaped = malloc(unescaped_len);
    if (!unescaped) return NULL;

    // Perform unescaping
    char *dest = unescaped;
    for (const char *src = escaped_text; *src; src++) {
        if (*src == '\\' && *(src + 1) == 'n') {
            *dest++ = '\n';
            src++; // Skip the 'n'
        } else {
            *dest++ = *src;
        }
    }
    *dest = '\0';

    return unescaped;
}

int history_file_append(HistoryFile *hf, const char *text) {
    if (!hf || !hf->fp || !text || text[0] == '\0') return 0;

    // Escape newlines before storing
    char *escaped_text = escape_newlines(text);
    if (!escaped_text) {
        LOG_WARN("[HIST] Failed to escape text for history");
        return -1;
    }

    if (fprintf(hf->fp, "%s\n", escaped_text) < 0) {
        LOG_WARN("[HIST] Failed to append to history file: %s", hf->path);
        free(escaped_text);
        return -1;
    }
    fflush(hf->fp); // allow OS buffering; no fsync
    free(escaped_text);
    return 0;
}

char** history_file_load_recent(HistoryFile *hf, int limit, int *out_count) {
    if (out_count) *out_count = 0;
    if (!hf || !hf->path || limit <= 0) return NULL;

    FILE *fr = fopen(hf->path, "r");
    if (!fr) return NULL;

    // Read entire file into memory (simple; history file typically small)
    if (fseek(fr, 0, SEEK_END) != 0) { fclose(fr); return NULL; }
    long size = ftell(fr);
    if (size < 0) { fclose(fr); return NULL; }
    if (fseek(fr, 0, SEEK_SET) != 0) { fclose(fr); return NULL; }

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(fr); return NULL; }
    size_t nread = fread(buf, 1, (size_t)size, fr);
    fclose(fr);
    buf[nread] = '\0';

    // Count lines
    int total_lines = 0;
    for (size_t i = 0; i < nread; i++) if (buf[i] == '\n') total_lines++;
    if (nread > 0 && buf[nread - 1] != '\n') total_lines++; // last line without newline

    if (total_lines == 0) { free(buf); return NULL; }

    // Determine start line to keep only last 'limit'
    int start_line = total_lines > limit ? total_lines - limit : 0;

    // Collect lines
    char **tmp = calloc((size_t)(total_lines - start_line), sizeof(char*));
    if (!tmp) { free(buf); return NULL; }

    int line_idx = 0, out_idx = 0;
    char *line_start = buf;
    for (size_t i = 0; i <= nread; i++) {
        if (i == nread || buf[i] == '\n') {
            size_t len = (size_t)(&buf[i] - line_start);
            // At end of buffer (i == nread): only process if there's content after the last \n
            // This prevents processing a phantom empty line when file ends with \n
            int should_process = (i < nread) || (len > 0);

            if (should_process && line_idx >= start_line) {
                // Trim carriage return
                if (len > 0 && line_start[len - 1] == '\r') len--;
                if (len > 0) {
                    // Create null-terminated copy of the line
                    char *line_copy = malloc(len + 1);
                    if (line_copy) {
                        memcpy(line_copy, line_start, len);
                        line_copy[len] = '\0';

                        // Unescape newlines
                        char *unescaped = unescape_newlines(line_copy);
                        free(line_copy);

                        if (unescaped) {
                            tmp[out_idx++] = unescaped;
                        }
                    }
                }
            }
            if (should_process) {
                line_idx++;
            }
            line_start = (i < nread) ? &buf[i + 1] : &buf[i];
        }
    }

    free(buf);
    if (out_count) *out_count = out_idx;
    return (out_idx > 0) ? tmp : (free(tmp), NULL);
}

