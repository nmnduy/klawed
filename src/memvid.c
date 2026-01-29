/*
 * memvid.c - Memvid FFI integration implementation
 *
 * Provides wrapper functions for memvid library integration.
 * When HAVE_MEMVID is not defined, all functions return error values.
 */

#include "memvid.h"
#include "logger.h"
#include "data_dir.h"

#include <bsd/string.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef HAVE_MEMVID

/* Default memory file subpath within data directory */
#define MEMVID_DEFAULT_SUBPATH "memory.mv2"
#define MEMVID_MAX_PATH 4096

/* Global instance management */
static MemvidHandle *g_memvid_handle = NULL;
static pthread_once_t g_memvid_init_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_memvid_mutex = PTHREAD_MUTEX_INITIALIZER;
static char *g_memvid_path = NULL;
static int g_memvid_init_result = -1;

/*
 * Create directory recursively (like mkdir -p)
 * Returns: 0 on success, -1 on error
 */
static int mkdir_p(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    if (path == NULL) {
        return -1;
    }

    len = strlcpy(tmp, path, sizeof(tmp));
    if (len >= sizeof(tmp)) {
        return -1;  /* Path too long */
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

/*
 * Ensure parent directory for a file path exists.
 * Returns: 0 on success, -1 on error.
 */
static int ensure_parent_dir_exists(const char *file_path) {
    const char *slash = NULL;
    char *dir = NULL;
    size_t dir_len;
    int rc = 0;

    if (file_path == NULL) {
        return -1;
    }

    slash = strrchr(file_path, '/');
    if (slash == NULL) {
        /* No directory component (current working directory) */
        return 0;
    }

    dir_len = (size_t)(slash - file_path);
    if (dir_len == 0) {
        /* Path like "/file"; root directory is assumed to exist */
        return 0;
    }

    if (dir_len >= MEMVID_MAX_PATH) {
        LOG_ERROR("Memvid: Directory portion of path is too long");
        return -1;
    }

    dir = calloc(dir_len + 1, sizeof(char));
    if (dir == NULL) {
        LOG_ERROR("Memvid: Failed to allocate directory buffer");
        return -1;
    }

    /* Copy just the directory component; strlcpy would report truncation because
     * it measures the full source length (file_path), not the slice we want. */
    memcpy(dir, file_path, dir_len);
    dir[dir_len] = '\0';

    rc = mkdir_p(dir);
    if (rc != 0) {
        LOG_ERROR("Memvid: Failed to create directory %s: %s", dir, strerror(errno));
    }

    free(dir);
    return rc;
}

/*
 * Internal initialization function (called via pthread_once)
 */
static void memvid_do_init(void) {
    const char *path = g_memvid_path;
    static char default_path[MEMVID_MAX_PATH];
    int doctor_result;

    /* Use default path if none provided */
    if (path == NULL) {
        if (data_dir_build_path(default_path, sizeof(default_path), MEMVID_DEFAULT_SUBPATH) == 0) {
            path = default_path;
        } else {
            LOG_ERROR("Memvid: Failed to build default path");
            g_memvid_init_result = -1;
            return;
        }
    }

    if (ensure_parent_dir_exists(path) != 0) {
        g_memvid_init_result = -1;
        return;
    }

    LOG_INFO("Memvid: Opening database at %s", path);

    g_memvid_handle = memvid_open(path);
    if (g_memvid_handle == NULL) {
        const char *err = memvid_last_error();
        LOG_WARN("Memvid: Failed to open database: %s", err ? err : "unknown error");

        /* Attempt self-healing via doctor */
        LOG_INFO("Memvid: Attempting automatic repair...");
        doctor_result = memvid_doctor(path, 1, 1, 1); /* Rebuild all indices */

        if (doctor_result == MEMVID_DOCTOR_STATUS_CLEAN ||
            doctor_result == MEMVID_DOCTOR_STATUS_HEALED) {
            LOG_INFO("Memvid: Repair successful (status=%d), retrying open", doctor_result);

            /* Retry opening after repair */
            g_memvid_handle = memvid_open(path);
            if (g_memvid_handle == NULL) {
                err = memvid_last_error();
                LOG_ERROR("Memvid: Failed to open database after repair: %s",
                          err ? err : "unknown error");
                g_memvid_init_result = -1;
                return;
            }

            LOG_INFO("Memvid: Database opened successfully after repair");
            g_memvid_init_result = 0;
            return;
        } else if (doctor_result == MEMVID_DOCTOR_STATUS_PARTIAL) {
            LOG_WARN("Memvid: Partial repair (status=%d), attempting to open anyway", doctor_result);

            /* Try opening anyway - might work for some operations */
            g_memvid_handle = memvid_open(path);
            if (g_memvid_handle != NULL) {
                LOG_INFO("Memvid: Database opened after partial repair");
                g_memvid_init_result = 0;
                return;
            }
            /* Fall through to error */
        } else {
            LOG_ERROR("Memvid: Automatic repair failed (status=%d)", doctor_result);
        }

        g_memvid_init_result = -1;
        return;
    }

    LOG_INFO("Memvid: Database opened successfully");
    g_memvid_init_result = 0;
}

int memvid_init_global(const char *path) {
    const char *effective_path = NULL;
    size_t path_len = 0;

    pthread_mutex_lock(&g_memvid_mutex);

    /* If already initialized, return previous result */
    if (g_memvid_handle != NULL) {
        pthread_mutex_unlock(&g_memvid_mutex);
        return 0;
    }

    /* Precedence: explicit argument > env var > default */
    effective_path = path;
    if (effective_path == NULL || effective_path[0] == '\0') {
        const char *env_path = getenv("KLAWED_MEMORY_PATH");
        if (env_path != NULL && env_path[0] != '\0') {
            effective_path = env_path;
        }
    }

    if (effective_path != NULL && effective_path[0] != '\0') {
        path_len = strnlen(effective_path, MEMVID_MAX_PATH + 1);
        if (path_len == 0 || path_len > MEMVID_MAX_PATH) {
            LOG_ERROR("Memvid: Memory path too long (max %d)", MEMVID_MAX_PATH);
            pthread_mutex_unlock(&g_memvid_mutex);
            return -1;
        }

        g_memvid_path = strdup(effective_path);
        if (g_memvid_path == NULL) {
            LOG_ERROR("Memvid: Failed to allocate memory for path");
            pthread_mutex_unlock(&g_memvid_mutex);
            return -1;
        }
    }

    /* Perform one-time initialization */
    pthread_once(&g_memvid_init_once, memvid_do_init);

    pthread_mutex_unlock(&g_memvid_mutex);
    return g_memvid_init_result;
}

void memvid_cleanup_global(void) {
    pthread_mutex_lock(&g_memvid_mutex);

    if (g_memvid_handle != NULL) {
        LOG_INFO("Memvid: Closing database");
        memvid_close(g_memvid_handle);
        g_memvid_handle = NULL;
    }

    if (g_memvid_path != NULL) {
        free(g_memvid_path);
        g_memvid_path = NULL;
    }

    /* Reset pthread_once so init can be called again if needed */
    g_memvid_init_once = (pthread_once_t)PTHREAD_ONCE_INIT;
    g_memvid_init_result = -1;

    pthread_mutex_unlock(&g_memvid_mutex);
}

MemvidHandle* memvid_get_global(void) {
    MemvidHandle *handle = NULL;

    pthread_mutex_lock(&g_memvid_mutex);
    handle = g_memvid_handle;
    pthread_mutex_unlock(&g_memvid_mutex);

    return handle;
}

int memvid_is_available(void) {
    return 1;
}

MemvidHandle* memvid_open_for_path(const char *path) {
    if (path == NULL || path[0] == '\0') {
        /* Return global handle if no specific path requested */
        return memvid_get_global();
    }

    /* Ensure parent directory exists */
    if (ensure_parent_dir_exists(path) != 0) {
        LOG_ERROR("Memvid: Failed to create parent directory for %s", path);
        return NULL;
    }

    LOG_INFO("Memvid: Opening database at %s (custom path)", path);

    MemvidHandle *handle = memvid_open(path);
    if (handle == NULL) {
        const char *err = memvid_last_error();
        LOG_ERROR("Memvid: Failed to open database at %s: %s",
                  path, err ? err : "unknown error");
        return NULL;
    }

    return handle;
}

#else /* !HAVE_MEMVID */

/*
 * Stub implementations when memvid is not available
 */

MemvidHandle* memvid_open(const char *path) {
    (void)path;
    LOG_WARN("Memvid: Not available (built without HAVE_MEMVID)");
    return NULL;
}

void memvid_close(MemvidHandle *handle) {
    (void)handle;
}

int64_t memvid_put_memory(MemvidHandle *handle, const char *entity,
                          const char *slot, const char *value,
                          uint8_t kind, uint8_t relation) {
    (void)handle;
    (void)entity;
    (void)slot;
    (void)value;
    (void)kind;
    (void)relation;
    return -1;
}

char* memvid_get_current(MemvidHandle *handle, const char *entity, const char *slot) {
    (void)handle;
    (void)entity;
    (void)slot;
    return NULL;
}

char* memvid_search(MemvidHandle *handle, const char *query, uint32_t top_k) {
    (void)handle;
    (void)query;
    (void)top_k;
    return NULL;
}

char* memvid_get_entity_memories(MemvidHandle *handle, const char *entity) {
    (void)handle;
    (void)entity;
    return NULL;
}

int memvid_commit(MemvidHandle *handle) {
    (void)handle;
    return -1;
}

void memvid_free_string(char *s) {
    (void)s;
}

const char* memvid_last_error(void) {
    return "Memvid not available (built without HAVE_MEMVID)";
}

int memvid_init_global(const char *path) {
    (void)path;
    LOG_WARN("Memvid: Not available (built without HAVE_MEMVID)");
    return -1;
}

void memvid_cleanup_global(void) {
    /* No-op */
}

MemvidHandle* memvid_get_global(void) {
    return NULL;
}

int memvid_is_available(void) {
    return 0;
}

MemvidHandle* memvid_open_for_path(const char *path) {
    (void)path;
    LOG_WARN("Memvid: Not available (built without HAVE_MEMVID)");
    return NULL;
}

int memvid_doctor(const char *path, int rebuild_time_index,
                  int rebuild_lex_index, int rebuild_vec_index) {
    (void)path;
    (void)rebuild_time_index;
    (void)rebuild_lex_index;
    (void)rebuild_vec_index;
    return MEMVID_DOCTOR_STATUS_ERROR;
}

#endif /* HAVE_MEMVID */
