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
int insert_system_message(InternalMessage *messages, int *count, char *system_text);

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
