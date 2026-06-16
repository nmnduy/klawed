/*
 * Session Resolve - Partial session ID prefix resolution
 */

#ifndef SESSION_RESOLVE_H
#define SESSION_RESOLVE_H

#include "persistence.h"

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

#endif /* SESSION_RESOLVE_H */
