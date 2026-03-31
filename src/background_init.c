/*
 * background_init.c - Asynchronous background initialization
 */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "background_init.h"
#include "klawed_internal.h"
#include "logger.h"
#include "context/system_prompt.h"
#include "persistence.h"
#include "memory_db.h"
#include "data_dir.h"

/*
 * Default timeout for waiting on background database initialization.
 * On macOS, we use a shorter timeout to prevent TUI hangs.
 */
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_MAC
#define DATABASE_INIT_TIMEOUT_MS 5000  /* 5 seconds on macOS */
#else
#define DATABASE_INIT_TIMEOUT_MS 30000 /* 30 seconds on other Apple platforms */
#endif
#else
#define DATABASE_INIT_TIMEOUT_MS 30000 /* 30 seconds on Linux/other */
#endif

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

    PersistenceDB *db = NULL;

    // Skip database initialization in no-storage mode
    if (data_dir_is_no_storage_mode()) {
        LOG_INFO("[BG] Database initialization skipped (KLAWED_NO_STORAGE=1)");
    } else {
        LOG_DEBUG("[BG] Database initialization started");
        // Initialize persistence database
        db = persistence_init(NULL);
    }

    // Store result
    pthread_mutex_lock(&bg->database_mutex);
    bg->database_result = db;
    bg->database_ready = 1;
    pthread_mutex_unlock(&bg->database_mutex);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    if (!data_dir_is_no_storage_mode()) {
        if (db) {
            LOG_DEBUG("[BG] Database initialization completed in %ld ms", duration_ms);
        } else {
            LOG_WARN("[BG] Database initialization failed after %ld ms", duration_ms);
        }
    }

    return NULL;
}

/*
 * Background thread: Initialize memory database
 */
static void* init_memory_db_thread(void *arg) {
    ConversationState *state = (ConversationState *)arg;
    BackgroundLoaders *bg = state->bg_loaders;
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    LOG_DEBUG("[BG] Memory database initialization started");

    // Initialize memory database
    int result = memory_db_init_global(NULL);

    // Store result
    pthread_mutex_lock(&bg->memvid_mutex);
    bg->memvid_result = result;
    bg->memvid_ready = 1;
    pthread_mutex_unlock(&bg->memvid_mutex);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    if (result == 0) {
        LOG_DEBUG("[BG] Memory database initialization completed in %ld ms", duration_ms);
    } else {
        LOG_DEBUG("[BG] Memory database initialization failed after %ld ms", duration_ms);
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

    // Start memory database initialization thread
    if (pthread_create(&bg->memvid_thread, NULL, init_memory_db_thread, state) == 0) {
        bg->memvid_started = 1;
        LOG_DEBUG("Background memory database initialization started");
    } else {
        LOG_ERROR("Failed to start memory database background thread");
    }

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    LOG_INFO("Background initialization started in %ld ms (system_prompt=%d, database=%d, memory_db=%d)",
             duration_ms, bg->system_prompt_started, bg->database_started, bg->memvid_started);

    return 0;
}

/*
 * Insert or replace system message at position 0 of the message array.
 *
 * This function handles the logic of inserting a system message at the
 * correct position, handling edge cases like:
 * - Empty message array (just append)
 * - Existing system message at position 0 (replace)
 * - Existing non-system messages (shift down and insert at 0)
 * - Full message array (replace first message)
 *
 * Parameters:
 *   messages     - Array of InternalMessage (must have room for MAX_MESSAGES)
 *   count        - Pointer to current message count (will be updated)
 *   system_text  - The system message text (will be owned by the message array)
 *
 * Returns:
 *   0 on success, -1 on error
 *
 * Note: This function does NOT lock the conversation state - the caller
 * is responsible for thread safety.
 */
int insert_system_message(InternalMessage *messages, int *count, char *system_text) {
    if (!messages || !count || !system_text) {
        return -1;
    }

    int current_count = *count;

    // If messages already exist, insert system message at position 0
    if (current_count > 0) {
        // Check if position 0 is already a system message
        if (messages[0].role == MSG_SYSTEM) {
            // Replace existing system message
            free(messages[0].contents[0].text);
            messages[0].contents[0].text = system_text;
            return 0;
        }

        // Insert at position 0 by shifting existing messages
        if (current_count < MAX_MESSAGES) {
            // Shift all messages down by 1
            memmove(&messages[1], &messages[0],
                    (size_t)current_count * sizeof(InternalMessage));
            (*count)++;

            // Insert system message at position 0
            messages[0].role = MSG_SYSTEM;
            messages[0].contents = calloc(1, sizeof(InternalContent));
            messages[0].content_count = 1;
            messages[0].contents[0].type = INTERNAL_TEXT;
            messages[0].contents[0].text = system_text;
            return 0;
        }

        // Conversation full, replace first message
        LOG_WARN("Conversation full, replacing first message with system prompt");
        free(messages[0].contents[0].text);
        free(messages[0].contents);
        messages[0].role = MSG_SYSTEM;
        messages[0].contents = calloc(1, sizeof(InternalContent));
        messages[0].content_count = 1;
        messages[0].contents[0].type = INTERNAL_TEXT;
        messages[0].contents[0].text = system_text;
        return 0;
    }

    // No existing messages, just append
    messages[0].role = MSG_SYSTEM;
    messages[0].contents = calloc(1, sizeof(InternalContent));
    messages[0].content_count = 1;
    messages[0].contents[0].type = INTERNAL_TEXT;
    messages[0].contents[0].text = system_text;
    *count = 1;
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
    char *system_prompt = bg->system_prompt_result;
    bg->system_prompt_result = NULL;
    pthread_mutex_unlock(&bg->system_prompt_mutex);

    if (system_prompt) {
        // Use conversation lock for thread-safe message array modification
        if (conversation_state_lock(state) != 0) {
            LOG_ERROR("Failed to acquire conversation lock for system prompt insertion");
            free(system_prompt);
        } else {
            insert_system_message(state->messages, &state->count, system_prompt);
            conversation_state_unlock(state);
        }
    }

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;
    LOG_DEBUG("System prompt added to conversation (total wait: %ld ms)", duration_ms);

    // Debug: print if requested
    if (getenv("DEBUG_PROMPT")) {
        if (conversation_state_lock(state) == 0) {
            if (state->count > 0 && state->messages[0].role == MSG_SYSTEM &&
                state->messages[0].content_count > 0 && state->messages[0].contents[0].text) {
                printf("\n=== SYSTEM PROMPT (DEBUG) ===\n%s\n=== END SYSTEM PROMPT ===\n\n",
                       state->messages[0].contents[0].text);
            }
            conversation_state_unlock(state);
        }
    }
}

/*
 * Get database handle (wait if not ready, with timeout)
 *
 * This function waits for the database initialization thread to complete,
 * but with a timeout to prevent indefinite hangs (especially on macOS).
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

    // Wait for thread to complete with timeout
    LOG_DEBUG("Waiting for database initialization to complete (timeout: %d ms)...", DATABASE_INIT_TIMEOUT_MS);
    if (bg->database_started) {
        /* Use pthread_timedjoin_np if available (Linux) or implement polling timeout */
        int timeout_ms = DATABASE_INIT_TIMEOUT_MS;
        int waited_ms = 0;
        int poll_interval_ms = 100;  /* Check every 100ms */

        while (waited_ms < timeout_ms) {
            /* Check if thread has completed by checking the ready flag */
            pthread_mutex_lock(&bg->database_mutex);
            ready = bg->database_ready;
            db = bg->database_result;
            pthread_mutex_unlock(&bg->database_mutex);

            if (ready) {
                LOG_DEBUG("Database initialization completed (waited %d ms)", waited_ms);
                break;
            }

            /* Small sleep to avoid busy-waiting */
            struct timespec sleep_ts = {0, poll_interval_ms * 1000000};
            nanosleep(&sleep_ts, NULL);
            waited_ms += poll_interval_ms;
        }

        if (!ready) {
            LOG_WARN("Database initialization timed out after %d ms - continuing without database", timeout_ms);
            /*
             * Note: We don't cancel the thread here as it might still be working.
             * We just return NULL to indicate the database is not available.
             * The thread will eventually complete and the result will be cleaned up
             * in cleanup_background_loaders().
             */
            return NULL;
        }

        /* Thread has signaled completion, now join it to clean up */
        pthread_join(bg->database_thread, NULL);
        bg->database_started = 0;
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
 * Check if memory database is ready (wait if not ready)
 */
int await_memory_db_ready(ConversationState *state) {
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
        LOG_DEBUG("Memory database already ready (fast path)");
        return result;
    }

    // Wait for thread to complete
    LOG_DEBUG("Waiting for memory database initialization to complete...");
    if (bg->memvid_started) {
        pthread_join(bg->memvid_thread, NULL);
        bg->memvid_started = 0;
        LOG_DEBUG("Memory database initialization completed (waited)");
    }

    pthread_mutex_lock(&bg->memvid_mutex);
    result = bg->memvid_result;
    pthread_mutex_unlock(&bg->memvid_mutex);

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;
    LOG_DEBUG("Memory database ready after %ld ms wait", duration_ms);

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
