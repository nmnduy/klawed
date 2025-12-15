/*
 * Session Management - Load and resume conversations from persistence database
 *
 * This module provides functionality to resume previous conversations by
 * loading them from the SQLite database and reconstructing the conversation state.
 */

#ifndef SESSION_H
#define SESSION_H

#include "claude_internal.h"
#include "persistence.h"

/**
 * Load a session from the database and reconstruct the conversation state
 *
 * Parameters:
 *   db: Persistence database handle
 *   session_id: Session identifier to load (NULL = load most recent session)
 *   state: ConversationState to populate with loaded messages
 *
 * Returns:
 *   0 on success, -1 on error
 */
int session_load_from_db(PersistenceDB *db, const char *session_id, ConversationState *state);

/**
 * Get list of available sessions from the database
 *
 * Parameters:
 *   db: Persistence database handle
 *   limit: Maximum number of sessions to return (0 = no limit)
 *
 * Returns:
 *   Array of session IDs (caller must free with session_free_list), NULL on error
 *   Each session ID is a newly allocated string
 */
char** session_get_list(PersistenceDB *db, int limit);

/**
 * Free a session list returned by session_get_list
 *
 * Parameters:
 *   sessions: Array of session IDs
 */
void session_free_list(char **sessions);

/**
 * Get session metadata (timestamp, model, message count)
 *
 * Parameters:
 *   db: Persistence database handle
 *   session_id: Session identifier
 *   timestamp: Output parameter for session creation timestamp (ISO format)
 *   model: Output parameter for model used in session
 *   message_count: Output parameter for number of messages in session
 *
 * Returns:
 *   0 on success, -1 on error
 */
int session_get_metadata(PersistenceDB *db, const char *session_id,
                         char **timestamp, char **model, int *message_count);

/**
 * List available sessions with metadata
 *
 * Parameters:
 *   db: Persistence database handle
 *   limit: Maximum number of sessions to list (0 = no limit)
 *
 * Returns:
 *   0 on success, -1 on error
 */
int session_list_sessions(PersistenceDB *db, int limit);

#endif // SESSION_H
