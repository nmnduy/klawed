/*
 * Session Resolve - Partial session ID prefix resolution
 *
 * Provides session_resolve_partial_id() which resolves a short prefix
 * to a full session ID by querying the database, similar to how git
 * resolves partial commit hashes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <sqlite3.h>

#include "session_resolve.h"
#include "persistence.h"
#include "logger.h"

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
char* session_resolve_partial_id(PersistenceDB *db, const char *prefix, char *errmsg) {
    if (!db || !db->db || !prefix || prefix[0] == '\0') {
        if (errmsg) {
            strlcpy(errmsg, "Invalid session ID prefix", 256);
        }
        return NULL;
    }

    // Query for sessions matching the prefix.
    // Use ESCAPE '\\' so we can safely escape LIKE wildcards in the prefix.
    const char *query =
        "SELECT DISTINCT session_id FROM api_calls "
        "WHERE session_id LIKE (? || '%') ESCAPE '\\' "
        "ORDER BY session_id";

    // Escape LIKE wildcards (% and _) in the prefix by prefixing with backslash
    char escaped_prefix[512];
    size_t esc_len = 0;
    size_t max_esc = sizeof(escaped_prefix) - 1;
    const char *p = prefix;
    while (*p && esc_len < max_esc) {
        if (*p == '%' || *p == '_') {
            if (esc_len + 1 < max_esc) {
                escaped_prefix[esc_len++] = '\\';
            } else {
                break;
            }
        }
        escaped_prefix[esc_len++] = *p;
        p++;
    }
    escaped_prefix[esc_len] = '\0';

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare partial match query: %s", sqlite3_errmsg(db->db));
        if (errmsg) {
            strlcpy(errmsg, "Database error during session lookup", 256);
        }
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, escaped_prefix, -1, SQLITE_TRANSIENT);

    // Collect all matching session IDs
    char *matches[32];  // Reasonable upper bound for ambiguous matches
    int match_count = 0;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && match_count < 32) {
        const unsigned char *sid = sqlite3_column_text(stmt, 0);
        if (sid) {
            matches[match_count] = strdup((const char *)sid);
            if (!matches[match_count]) {
                // OOM during collection — clean up and report error
                LOG_ERROR("Out of memory collecting session matches for prefix '%s'", prefix);
                if (errmsg) {
                    strlcpy(errmsg, "Out of memory during session lookup", 256);
                }
                for (int i = 0; i < match_count; i++) {
                    free(matches[i]);
                }
                sqlite3_finalize(stmt);
                return NULL;
            }
            match_count++;
        }
    }

    // Check if there are more matches beyond our limit
    if (rc == SQLITE_ROW && match_count >= 32) {
        // There are more - we can't know the exact count, but this is enough to report ambiguity
        LOG_WARN("Too many sessions match prefix '%s' (at least %d)", prefix, match_count);
    }

    sqlite3_finalize(stmt);

    char *result = NULL;

    if (match_count == 0) {
        LOG_ERROR("No sessions found matching prefix '%s'", prefix);
        if (errmsg) {
            snprintf(errmsg, 256, "No session found matching prefix '%s'", prefix);
        }
    } else if (match_count == 1) {
        result = matches[0];  // Return the single match
        LOG_INFO("Resolved partial session ID '%s' -> '%s'", prefix, result);
    } else {
        // Multiple ambiguous matches
        LOG_ERROR("Ambiguous session prefix '%s' matches %d sessions", prefix, match_count);
        if (errmsg) {
            int offset = snprintf(errmsg, 256, "Ambiguous prefix '%s' matches %d sessions: ", prefix, match_count);
            for (int i = 0; i < match_count && offset < 255; i++) {
                size_t remaining = (size_t)(255 - offset);
                offset += snprintf(errmsg + offset, remaining, "%s%s",
                                   i > 0 ? ", " : "", matches[i]);
            }
            if (offset >= 255) {
                strlcpy(errmsg + 252, "...", 4);
            }
        }
        // Free all matches
        for (int i = 0; i < match_count; i++) {
            free(matches[i]);
        }
    }

    return result;
}
