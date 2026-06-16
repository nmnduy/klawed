/*
 * Session Management - Load and resume conversations from persistence database
 *
 * This module provides functionality to resume previous conversations by
 * loading them from the SQLite database and reconstructing the conversation state.
 */

#ifndef SESSION_H
#define SESSION_H

#include "klawed_internal.h"
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
 * Resolve a partial session ID prefix to the full session ID.
 *
 * Queries the database for sessions whose ID starts with the given prefix.
 * Returns the full session ID if exactly one match is found.
 * Returns NULL if no match or multiple ambiguous matches are found.
 *
 * Parameters:
 *   db: Persistence database handle
 *   prefix: Partial session ID to resolve
 *   errmsg: Optional output buffer for error message (must be at least 256 bytes
 *           if non-NULL). Receives a human-readable error on ambiguity/no-match.
 *
 * Returns:
 *   Newly allocated full session ID string (caller must free),
 *   or NULL if resolution failed (check errmsg for details).
 */
char* session_resolve_partial_id(PersistenceDB *db, const char *prefix, char *errmsg);

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

/**
 * Generate a session title from conversation content
 *
 * Extracts the first user message from the conversation state and
 * creates a concise title (truncated to ~60 characters, newlines removed).
 * The title is intended to provide a meaningful summary of the session's topic.
 *
 * Parameters:
 *   state: Conversation state with messages
 *
 * Returns:
 *   Newly allocated string with session title (caller must free),
 *   or NULL if no suitable content found for a title
 */
char* session_generate_title(ConversationState *state);

/**
 * Try to generate and save a session title if one doesn't exist yet
 *
 * Checks the total token usage for the session, and if it exceeds the
 * threshold (configurable via KLAWED_SESSION_TITLE_THRESHOLD env var,
 * default: 1000 tokens), generates a title from conversation context
 * and saves it to the database.
 *
 * This is intended to be called after each API call completes.
 *
 * Parameters:
 *   state: Conversation state with messages and persistence database
 *
 * Returns:
 *   0 on success or if title already exists/below threshold,
 *   -1 on error
 */
int session_maybe_generate_title(ConversationState *state);

#endif // SESSION_H
