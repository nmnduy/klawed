/*
 * File Utilities
 * Helper functions for file I/O operations
 */

#ifndef __APPLE__
    #define _GNU_SOURCE
#endif

#include "file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>

#ifndef TEST_BUILD
#include "../logger.h"
#else
// Test build stubs
#define LOG_ERROR(fmt, ...) ((void)0)
#define LOG_DEBUG(fmt, ...) ((void)0)
#endif

/**
 * Read entire file into memory
 */
char* read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *content = malloc((size_t)fsize + 1);
    if (content) {
        size_t bytes_read = fread(content, 1, (size_t)fsize, f);
        (void)bytes_read; // Suppress unused result warning
        content[fsize] = 0;
    }

    fclose(f);
    return content;
}

/**
 * Create directory recursively (like mkdir -p)
 */
int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    if (strlcpy(tmp, path, sizeof(tmp)) >= sizeof(tmp)) {
        return -1; // Path too long
    }
    len = strlen(tmp);

    // Remove trailing slash
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    // Create directories recursively
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    // Create final directory
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

/**
 * Write content to file
 */
int write_file(const char *path, const char *content) {
    // Create parent directories if they don't exist
    char *path_copy = strdup(path);
    if (!path_copy) return -1;

    // Extract directory path
    char *dir_path = dirname(path_copy);

    // Create directory recursively (ignore errors if directory already exists)
    (void)mkdir_p(dir_path); // Ignore errors - directory may already exist

    free(path_copy);

    // Now try to open/create the file
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    return (written == len) ? 0 : -1;
}

/**
 * Resolve path (handle relative paths and canonicalization)
 */
char* resolve_path(const char *path, const char *working_dir) {
    // Join with working_dir if relative; attempt to canonicalize if possible.
    char joined[PATH_MAX];
    if (path[0] == '/') {
        snprintf(joined, sizeof(joined), "%s", path);
    } else {
        snprintf(joined, sizeof(joined), "%s/%s", working_dir, path);
    }

    // Try to canonicalize. This succeeds only if the path (or its parents) exist.
    char *clean = realpath(joined, NULL);
    if (clean) {
        return clean; // Caller takes ownership
    }

    // Fall back to the joined path even if parent dirs don't exist.
    // This enables tools like Write to create missing directories (mkdir -p in write_file).
    return strdup(joined);
}

/**
 * Save binary data to file
 */
int save_binary_file(const char *filename, const void *data, size_t size) {
    if (!filename || !data || size == 0) {
        return -1;
    }

    // Create parent directories if they don't exist
    char *path_copy = strdup(filename);
    if (!path_copy) return -1;

    // Extract directory path
    char *dir_path = dirname(path_copy);

    // Create directory recursively (ignore errors if directory already exists)
    (void)mkdir_p(dir_path); // Ignore errors - directory may already exist

    free(path_copy);

    // Open file for writing
    FILE *f = fopen(filename, "wb");
    if (!f) {
        LOG_ERROR("Failed to open file for binary writing: %s", filename);
        return -1;
    }

    // Write binary data
    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        LOG_ERROR("Failed to write all binary data to file: %s (written: %zu, expected: %zu)",
                 filename, written, size);
        return -1;
    }

    LOG_DEBUG("Successfully saved binary data to '%s' (%zu bytes)", filename, size);
    return 0;
}
