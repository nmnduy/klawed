/*
 * data_dir.c - Data directory path utilities implementation
 *
 * Provides centralized access to the .klawed data directory path.
 */

#include "data_dir.h"
#include <bsd/string.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DATA_DIR_DEFAULT ".klawed"

const char *data_dir_get_base(void) {
    const char *env = getenv("KLAWED_DATA_DIR");
    if (env != NULL && env[0] != '\0') {
        return env;
    }
    return DATA_DIR_DEFAULT;
}

int data_dir_build_path(char *buf, size_t buf_size, const char *subpath) {
    if (buf == NULL || buf_size == 0) {
        return -1;
    }

    const char *base = data_dir_get_base();
    size_t needed = strlcpy(buf, base, buf_size);
    if (needed >= buf_size) {
        return -1;
    }

    /* If subpath is provided, append it with a separator */
    if (subpath != NULL && subpath[0] != '\0') {
        needed = strlcat(buf, "/", buf_size);
        if (needed >= buf_size) {
            return -1;
        }
        needed = strlcat(buf, subpath, buf_size);
        if (needed >= buf_size) {
            return -1;
        }
    }

    return 0;
}

/*
 * Create directory recursively (like mkdir -p)
 */
static int mkdir_recursive(const char *path) {
    char tmp[1024];
    char *p = NULL;
    size_t len;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }

    len = strlcpy(tmp, path, sizeof(tmp));
    if (len >= sizeof(tmp)) {
        return -1;
    }

    /* Remove trailing slash */
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    /* Create directories recursively */
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

int data_dir_ensure(const char *subpath) {
    char path[1024];

    if (data_dir_build_path(path, sizeof(path), subpath) != 0) {
        return -1;
    }

    return mkdir_recursive(path);
}
