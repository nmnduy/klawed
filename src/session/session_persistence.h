/*
 * Session Persistence
 * Session dump and conversation export functionality
 */

#ifndef SESSION_PERSISTENCE_H
#define SESSION_PERSISTENCE_H

/**
 * Dump a conversation from the database
 * 
 * This function retrieves and displays all API calls from a session
 * in the specified format.
 * 
 * Parameters:
 *   session_id: Session identifier to dump (NULL = most recent session)
 *   format: Output format ("default", "json", "markdown"/"md")
 * 
 * Returns:
 *   0 on success, 1 on error
 */
int session_dump_conversation(const char *session_id, const char *format);

#endif // SESSION_PERSISTENCE_H
