/*
 * memvid.c - Memvid FFI integration implementation
 *
 * Provides wrapper functions for memvid library integration.
 * When HAVE_MEMVID is not defined, all functions return error values.
 */

#include "memvid.h"
#include "logger.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef HAVE_MEMVID

/* Default memory file path (project-local) */
#define MEMVID_DEFAULT_DIR ".klawed"
#define MEMVID_DEFAULT_FILE ".klawed/memory.mv2"

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
 * External FFI declarations for libmemvid
 * These are the actual symbols from the Rust library
 */
extern MemvidHandle* memvid_open(const char *path);
extern void memvid_close(MemvidHandle *handle);
extern int64_t memvid_put_memory(MemvidHandle *handle, const char *entity,
                                  const char *slot, const char *value,
                                  uint8_t kind, uint8_t relation);
extern char* memvid_get_current(MemvidHandle *handle, const char *entity, const char *slot);
extern char* memvid_search(MemvidHandle *handle, const char *query, uint32_t top_k);
extern char* memvid_get_entity_memories(MemvidHandle *handle, const char *entity);
extern int memvid_commit(MemvidHandle *handle);
extern void memvid_free_string(char *s);
extern const char* memvid_last_error(void);

/*
 * Internal initialization function (called via pthread_once)
 */
static void memvid_do_init(void) {
    const char *path = g_memvid_path;

    /* Use default path if none provided */
    if (path == NULL) {
        path = MEMVID_DEFAULT_FILE;

        /* Ensure .klawed directory exists */
        if (mkdir_p(MEMVID_DEFAULT_DIR) != 0) {
            LOG_ERROR("Memvid: Failed to create directory %s: %s",
                      MEMVID_DEFAULT_DIR, strerror(errno));
            g_memvid_init_result = -1;
            return;
        }
    }

    LOG_INFO("Memvid: Opening database at %s", path);

    g_memvid_handle = memvid_open(path);
    if (g_memvid_handle == NULL) {
        const char *err = memvid_last_error();
        LOG_ERROR("Memvid: Failed to open database: %s", err ? err : "unknown error");
        g_memvid_init_result = -1;
        return;
    }

    LOG_INFO("Memvid: Database opened successfully");
    g_memvid_init_result = 0;
}

int memvid_init_global(const char *path) {
    pthread_mutex_lock(&g_memvid_mutex);

    /* If already initialized, return previous result */
    if (g_memvid_handle != NULL) {
        pthread_mutex_unlock(&g_memvid_mutex);
        return 0;
    }

    /* Store path for pthread_once callback */
    if (path != NULL) {
        g_memvid_path = strdup(path);
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

#endif /* HAVE_MEMVID */
