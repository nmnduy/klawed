/*
 * background_init.c - Asynchronous background initialization
 */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "background_init.h"
#include "klawed_internal.h"
#include "logger.h"
#include "context/system_prompt.h"
#include "persistence.h"
#include "memvid.h"

/*
 * Background thread: Load system prompt
 */
static void* load_system_prompt_thread(void *arg) {
    ConversationState *state = (ConversationState *)arg;
    BackgroundLoaders *bg = state->bg_loaders;
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    LOG_DEBUG("[BG] System prompt loading started");

    // Build system prompt (this is the slow part: git, KLAWED.md)
    char *prompt = build_system_prompt(state);

    // Store result
    pthread_mutex_lock(&bg->system_prompt_mutex);
    bg->system_prompt_result = prompt;
    bg->system_prompt_ready = 1;
    pthread_mutex_unlock(&bg->system_prompt_mutex);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;
    LOG_DEBUG("[BG] System prompt loading completed in %ld ms", duration_ms);
    return NULL;
}

/*
 * Background thread: Initialize database
 */
static void* init_database_thread(void *arg) {
    ConversationState *state = (ConversationState *)arg;
    BackgroundLoaders *bg = state->bg_loaders;
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    LOG_DEBUG("[BG] Database initialization started");

    // Initialize persistence database
    PersistenceDB *db = persistence_init(NULL);

    // Store result
    pthread_mutex_lock(&bg->database_mutex);
    bg->database_result = db;
    bg->database_ready = 1;
    pthread_mutex_unlock(&bg->database_mutex);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    if (db) {
        LOG_DEBUG("[BG] Database initialization completed in %ld ms", duration_ms);
    } else {
        LOG_WARN("[BG] Database initialization failed after %ld ms", duration_ms);
    }

    return NULL;
}

/*
 * Background thread: Initialize memvid
 */
static void* init_memvid_thread(void *arg) {
    ConversationState *state = (ConversationState *)arg;
    BackgroundLoaders *bg = state->bg_loaders;
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    LOG_DEBUG("[BG] Memvid initialization started");

    // Initialize memvid
#ifdef HAVE_MEMVID
    int result = memvid_init_global(NULL);
#else
    int result = -1;  // Not available
#endif

    // Store result
    pthread_mutex_lock(&bg->memvid_mutex);
    bg->memvid_result = result;
    bg->memvid_ready = 1;
    pthread_mutex_unlock(&bg->memvid_mutex);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    if (result == 0) {
        LOG_DEBUG("[BG] Memvid initialization completed in %ld ms", duration_ms);
    } else {
        LOG_DEBUG("[BG] Memvid initialization failed or not available after %ld ms", duration_ms);
    }

    return NULL;
}

/*
 * Start all background loaders
 */
int start_background_loaders(ConversationState *state) {
    if (!state) {
        return -1;
    }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Allocate background loader state
    BackgroundLoaders *bg = calloc(1, sizeof(BackgroundLoaders));
    if (!bg) {
        LOG_ERROR("Failed to allocate background loaders");
        return -1;
    }

    // Initialize mutexes
    pthread_mutex_init(&bg->system_prompt_mutex, NULL);
    pthread_mutex_init(&bg->database_mutex, NULL);
    pthread_mutex_init(&bg->memvid_mutex, NULL);

    state->bg_loaders = bg;

    // Start system prompt loading thread
    if (pthread_create(&bg->system_prompt_thread, NULL, load_system_prompt_thread, state) == 0) {
        bg->system_prompt_started = 1;
        LOG_DEBUG("Background system prompt loading started");
    } else {
        LOG_ERROR("Failed to start system prompt background thread");
    }

    // Start database initialization thread
    if (pthread_create(&bg->database_thread, NULL, init_database_thread, state) == 0) {
        bg->database_started = 1;
        LOG_DEBUG("Background database initialization started");
    } else {
        LOG_ERROR("Failed to start database background thread");
    }

    // Start memvid initialization thread
    if (pthread_create(&bg->memvid_thread, NULL, init_memvid_thread, state) == 0) {
        bg->memvid_started = 1;
        LOG_DEBUG("Background memvid initialization started");
    } else {
        LOG_ERROR("Failed to start memvid background thread");
    }

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    LOG_INFO("Background initialization started in %ld ms (system_prompt=%d, database=%d, memvid=%d)",
             duration_ms, bg->system_prompt_started, bg->database_started, bg->memvid_started);

    return 0;
}

/*
 * Wait for system prompt to be ready and add it to conversation
 */
void await_system_prompt_ready(ConversationState *state) {
    if (!state || !state->bg_loaders) {
        LOG_WARN("No background loaders initialized");
        return;
    }

    BackgroundLoaders *bg = state->bg_loaders;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Check if already ready (fast path)
    pthread_mutex_lock(&bg->system_prompt_mutex);
    int ready = bg->system_prompt_ready;
    pthread_mutex_unlock(&bg->system_prompt_mutex);

    if (ready) {
        LOG_DEBUG("System prompt already ready (fast path)");
    } else {
        // Wait for thread to complete
        LOG_DEBUG("Waiting for system prompt to finish loading...");
        if (bg->system_prompt_started) {
            pthread_join(bg->system_prompt_thread, NULL);
            bg->system_prompt_started = 0;  // Mark as joined
            LOG_DEBUG("System prompt loading completed (waited)");
        }
    }

    // Add system prompt to conversation
    pthread_mutex_lock(&bg->system_prompt_mutex);
    if (bg->system_prompt_result) {
        add_system_message(state, bg->system_prompt_result);
        free(bg->system_prompt_result);
        bg->system_prompt_result = NULL;
        
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &end);
        long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                          (end.tv_nsec - start.tv_nsec) / 1000000;
        LOG_DEBUG("System prompt added to conversation (total wait: %ld ms)", duration_ms);

        // Debug: print if requested
        if (getenv("DEBUG_PROMPT")) {
            if (state->count > 0 && state->messages[0].role == MSG_SYSTEM &&
                state->messages[0].content_count > 0 && state->messages[0].contents[0].text) {
                printf("\n=== SYSTEM PROMPT (DEBUG) ===\n%s\n=== END SYSTEM PROMPT ===\n\n",
                       state->messages[0].contents[0].text);
            }
        }
    }
    pthread_mutex_unlock(&bg->system_prompt_mutex);
}

/*
 * Get database handle (wait if not ready)
 */
PersistenceDB* await_database_ready(ConversationState *state) {
    if (!state || !state->bg_loaders) {
        LOG_WARN("No background loaders initialized");
        return NULL;
    }

    BackgroundLoaders *bg = state->bg_loaders;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Check if already ready (fast path)
    pthread_mutex_lock(&bg->database_mutex);
    int ready = bg->database_ready;
    PersistenceDB *db = bg->database_result;
    pthread_mutex_unlock(&bg->database_mutex);

    if (ready) {
        LOG_DEBUG("Database already ready (fast path)");
        return db;
    }

    // Wait for thread to complete
    LOG_DEBUG("Waiting for database initialization to complete...");
    if (bg->database_started) {
        pthread_join(bg->database_thread, NULL);
        bg->database_started = 0;
        LOG_DEBUG("Database initialization completed (waited)");
    }

    pthread_mutex_lock(&bg->database_mutex);
    db = bg->database_result;
    pthread_mutex_unlock(&bg->database_mutex);

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;
    LOG_DEBUG("Database ready after %ld ms wait", duration_ms);

    return db;
}

/*
 * Check if memvid is ready (wait if not ready)
 */
int await_memvid_ready(ConversationState *state) {
    if (!state || !state->bg_loaders) {
        LOG_WARN("No background loaders initialized");
        return -1;
    }

    BackgroundLoaders *bg = state->bg_loaders;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Check if already ready (fast path)
    pthread_mutex_lock(&bg->memvid_mutex);
    int ready = bg->memvid_ready;
    int result = bg->memvid_result;
    pthread_mutex_unlock(&bg->memvid_mutex);

    if (ready) {
        LOG_DEBUG("Memvid already ready (fast path)");
        return result;
    }

    // Wait for thread to complete
    LOG_DEBUG("Waiting for memvid initialization to complete...");
    if (bg->memvid_started) {
        pthread_join(bg->memvid_thread, NULL);
        bg->memvid_started = 0;
        LOG_DEBUG("Memvid initialization completed (waited)");
    }

    pthread_mutex_lock(&bg->memvid_mutex);
    result = bg->memvid_result;
    pthread_mutex_unlock(&bg->memvid_mutex);

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;
    LOG_DEBUG("Memvid ready after %ld ms wait", duration_ms);

    return result;
}

/*
 * Cleanup background loaders
 */
void cleanup_background_loaders(ConversationState *state) {
    if (!state || !state->bg_loaders) {
        return;
    }

    BackgroundLoaders *bg = state->bg_loaders;

    // Join any still-running threads
    if (bg->system_prompt_started) {
        pthread_join(bg->system_prompt_thread, NULL);
    }
    if (bg->database_started) {
        pthread_join(bg->database_thread, NULL);
    }
    if (bg->memvid_started) {
        pthread_join(bg->memvid_thread, NULL);
    }

    // Destroy mutexes
    pthread_mutex_destroy(&bg->system_prompt_mutex);
    pthread_mutex_destroy(&bg->database_mutex);
    pthread_mutex_destroy(&bg->memvid_mutex);

    // Free any remaining data
    if (bg->system_prompt_result) {
        free(bg->system_prompt_result);
    }

    free(bg);
    state->bg_loaders = NULL;

    LOG_DEBUG("Background loaders cleaned up");
}
