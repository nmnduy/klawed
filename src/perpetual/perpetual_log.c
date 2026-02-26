/*
 * perpetual_log.c - Append-only markdown log for perpetual mode sessions
 */

#include "perpetual_log.h"

#include <bsd/string.h>  /* strlcpy, strlcat */
#include <bsd/stdlib.h>  /* reallocarray */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* Max length for the request field in the log (chars, excluding NUL). */
#define REQUEST_MAX_LEN  200
/* Suffix appended when request is truncated. */
#define TRUNCATE_SUFFIX  "..."
/* Timestamp format: "2026-02-26 14:03" */
#define TIMESTAMP_FMT    "%Y-%m-%d %H:%M"
#define TIMESTAMP_BUF    32

/* ---------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------*/

/* Create a single directory component. Ignores EEXIST. Returns 0/-1. */
static int make_dir(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

/* Ensure every directory component of file_path exists.
 * Works on a temporary copy of file_path; only creates up to the last '/'.
 * Returns 0 on success, -1 on error. */
static int ensure_parent_dirs(const char *file_path)
{
    if (!file_path) return -1;

    size_t len = strlen(file_path);
    char *buf = NULL;

    buf = reallocarray(NULL, len + 1, sizeof(char));
    if (!buf) return -1;

    strlcpy(buf, file_path, len + 1);

    /* Walk forward and mkdir each component up to (but not including) the
     * final path element (the filename itself). */
    for (size_t i = 1; i < len; i++) {
        if (buf[i] == '/') {
            char saved = buf[i];
            buf[i] = '\0';
            if (make_dir(buf) != 0) {
                free(buf);
                return -1;
            }
            buf[i] = saved;
        }
    }

    free(buf);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

char *perpetual_log_get_path(const char *data_dir)
{
    const char *env = getenv("KLAWED_PERPETUAL_FILE");
    if (env && env[0] != '\0') {
        char *result = NULL;
        size_t len = strlen(env);
        result = reallocarray(NULL, len + 1, sizeof(char));
        if (!result) return NULL;
        strlcpy(result, env, len + 1);
        return result;
    }

    /* Fall back: data_dir/perpetual.md or .klawed/perpetual.md */
    const char *base = (data_dir && data_dir[0] != '\0') ? data_dir : ".klawed";
    const char *filename = "/perpetual.md";

    size_t base_len = strlen(base);
    size_t file_len = strlen(filename);
    size_t total    = base_len + file_len + 1; /* +1 for NUL */

    char *result = reallocarray(NULL, total, sizeof(char));
    if (!result) return NULL;

    strlcpy(result, base, total);
    strlcat(result, filename, total);
    return result;
}

int perpetual_log_append(const char *log_path,
                         const char *session_id,
                         const char *request,
                         const char *summary,
                         const char *files_touched,
                         const char *commit_hash)
{
    if (!log_path || !session_id || !request || !summary) return -1;

    /* Build timestamp. */
    char timestamp[TIMESTAMP_BUF] = {0};
    time_t now = time(NULL);
    struct tm tm_buf = {0};
    if (!localtime_r(&now, &tm_buf)) return -1;
    if (strftime(timestamp, sizeof(timestamp), TIMESTAMP_FMT, &tm_buf) == 0) return -1;

    /* Truncate request to REQUEST_MAX_LEN chars if needed. */
    char req_buf[REQUEST_MAX_LEN + sizeof(TRUNCATE_SUFFIX)] = {0};
    if (strlen(request) > REQUEST_MAX_LEN) {
        strlcpy(req_buf, request, REQUEST_MAX_LEN + 1);   /* copies at most REQUEST_MAX_LEN chars + NUL */
        strlcat(req_buf, TRUNCATE_SUFFIX, sizeof(req_buf));
    } else {
        strlcpy(req_buf, request, sizeof(req_buf));
    }

    /* Ensure parent directories exist. */
    if (ensure_parent_dirs(log_path) != 0) return -1;

    /* Open in append mode. */
    FILE *f = fopen(log_path, "a");
    if (!f) return -1;

    /* Write the markdown block. */
    int rc = 0;

    if (fprintf(f, "## [%s] Session %s\n", timestamp, session_id) < 0) { rc = -1; goto done; }
    if (fprintf(f, "**Request:** %s\n", req_buf)                  < 0) { rc = -1; goto done; }
    if (fprintf(f, "**Summary:** %s\n", summary)                  < 0) { rc = -1; goto done; }

    if (files_touched && files_touched[0] != '\0') {
        if (fprintf(f, "**Files:** %s\n", files_touched) < 0) { rc = -1; goto done; }
    }
    if (commit_hash && commit_hash[0] != '\0') {
        if (fprintf(f, "**Commit:** %s\n", commit_hash) < 0) { rc = -1; goto done; }
    }

    if (fprintf(f, "\n---\n\n") < 0) { rc = -1; goto done; }

done:
    if (fclose(f) != 0) rc = -1;
    return rc;
}

long perpetual_log_size(const char *log_path)
{
    if (!log_path) return -1;

    struct stat st = {0};
    if (stat(log_path, &st) != 0) {
        if (errno == ENOENT) return 0;
        return -1;
    }
    return (long)st.st_size;
}
