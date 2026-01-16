/*
 * Token Usage Tracking
 * Track and report token usage per session
 */

#ifndef SESSION_TOKEN_USAGE_H
#define SESSION_TOKEN_USAGE_H

#include "../klawed_internal.h"

/**
 * Print token usage summary for the current session
 * 
 * This function retrieves token usage statistics from the persistence database
 * and prints a formatted summary to stderr.
 * 
 * Parameters:
 *   state: Conversation state containing session_id and persistence_db
 * 
 * Returns:
 *   void
 */
void session_print_token_usage(ConversationState *state);

#endif // SESSION_TOKEN_USAGE_H
