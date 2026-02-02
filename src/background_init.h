/*
 * background_init.h
 *
 * Async background initialization for system prompt, database, and memory database.
 * Allows these components to load in parallel during startup to improve
 * time-to-first-response.
 */

#ifndef BACKGROUND_INIT_H
#define BACKGROUND_INIT_H

#include "klawed_internal.h"

// Forward declaration - actual definition is in persistence.h
struct PersistenceDB;

/*
 * Start all background loaders (system prompt, database, memory_db)
 * Returns 0 on success, -1 on error
 */
int start_background_loaders(ConversationState *state);

/*
 * Wait for system prompt to be ready and add it to conversation
 */
void await_system_prompt_ready(ConversationState *state);

/*
 * Get database handle (wait if not ready)
 */
struct PersistenceDB* await_database_ready(ConversationState *state);

/*
 * Check if memory database is ready (wait if not ready)
 * Returns 0 on success, -1 on failure
 */
int await_memory_db_ready(ConversationState *state);

/*
 * Cleanup background loaders and join threads
 */
void cleanup_background_loaders(ConversationState *state);

#endif /* BACKGROUND_INIT_H */
