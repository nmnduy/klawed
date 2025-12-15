/*
 * Session Management - Load and resume conversations from persistence database
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

#include "session.h"
#include "logger.h"
#include "openai_messages.h"

/**
 * Load a session from the database and reconstruct the conversation state
 */
int session_load_from_db(PersistenceDB *db, const char *session_id, ConversationState *state) {
    if (!db || !db->db || !state) {
        LOG_ERROR("Invalid parameters to session_load_from_db");
        return -1;
    }

    // If no session_id provided, get the most recent one
    char *target_session_id = NULL;
    if (!session_id) {
        const char *latest_query =
            "SELECT session_id FROM api_calls "
            "WHERE session_id IS NOT NULL "
            "ORDER BY created_at DESC LIMIT 1";

        sqlite3_stmt *latest_stmt = NULL;
        int rc = sqlite3_prepare_v2(db->db, latest_query, -1, &latest_stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("Failed to get latest session: %s", sqlite3_errmsg(db->db));
            return -1;
        }

        rc = sqlite3_step(latest_stmt);
        if (rc == SQLITE_ROW) {
            const unsigned char *sid = sqlite3_column_text(latest_stmt, 0);
            if (sid) {
                target_session_id = strdup((const char *)sid);
            }
        }
        sqlite3_finalize(latest_stmt);

        if (!target_session_id) {
            LOG_ERROR("No sessions found in database");
            return -1;
        }
    } else {
        target_session_id = strdup(session_id);
    }

    LOG_INFO("Loading session: %s", target_session_id);

    // Query for all API calls in this session
    const char *query =
        "SELECT request_json, response_json, model, status "
        "FROM api_calls "
        "WHERE session_id = ? "
        "ORDER BY created_at ASC";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare query: %s", sqlite3_errmsg(db->db));
        free(target_session_id);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, target_session_id, -1, SQLITE_TRANSIENT);

    // Clear existing conversation state (except system message)
    clear_conversation(state);

    // Set the session ID
    if (state->session_id) {
        free(state->session_id);
    }
    state->session_id = target_session_id;

    // Process each API call in the session
    int call_num = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        call_num++;

        const char *request_json = (const char *)sqlite3_column_text(stmt, 0);
        const char *response_json = (const char *)sqlite3_column_text(stmt, 1);
        const char *model = (const char *)sqlite3_column_text(stmt, 2);
        const char *status = (const char *)sqlite3_column_text(stmt, 3);

        if (!request_json || !response_json || !model || !status) {
            LOG_WARN("Skipping incomplete API call #%d in session", call_num);
            continue;
        }

        if (strcmp(status, "error") == 0) {
            LOG_WARN("Skipping failed API call #%d in session", call_num);
            continue;
        }

        // Parse request to extract user message
        cJSON *request = cJSON_Parse(request_json);
        if (!request) {
            LOG_WARN("Failed to parse request JSON for call #%d", call_num);
            continue;
        }

        cJSON *messages = cJSON_GetObjectItem(request, "messages");
        if (!messages || !cJSON_IsArray(messages)) {
            LOG_WARN("No messages array in request for call #%d", call_num);
            cJSON_Delete(request);
            continue;
        }

        // Find the last user message in the request (this is what triggered the API call)
        cJSON *last_user_message = NULL;
        int msg_count = cJSON_GetArraySize(messages);
        for (int i = msg_count - 1; i >= 0; i--) {
            cJSON *msg = cJSON_GetArrayItem(messages, i);
            cJSON *role = cJSON_GetObjectItem(msg, "role");
            if (role && cJSON_IsString(role) && strcmp(role->valuestring, "user") == 0) {
                last_user_message = msg;
                break;
            }
        }

        if (!last_user_message) {
            LOG_WARN("No user message found in request for call #%d", call_num);
            cJSON_Delete(request);
            continue;
        }

        // Extract user message content
        cJSON *content = cJSON_GetObjectItem(last_user_message, "content");
        if (!content) {
            LOG_WARN("No content in user message for call #%d", call_num);
            cJSON_Delete(request);
            continue;
        }

        // Handle different content formats
        if (cJSON_IsString(content)) {
            // Simple text message
            add_user_message(state, content->valuestring);
        } else if (cJSON_IsArray(content)) {
            // Complex content array (text + tool results)
            // For now, extract text content
            int content_count = cJSON_GetArraySize(content);
            for (int i = 0; i < content_count; i++) {
                cJSON *block = cJSON_GetArrayItem(content, i);
                cJSON *type = cJSON_GetObjectItem(block, "type");
                if (type && cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0) {
                    cJSON *text = cJSON_GetObjectItem(block, "text");
                    if (text && cJSON_IsString(text)) {
                        add_user_message(state, text->valuestring);
                        break; // Just take the first text block for now
                    }
                }
            }
        }

        cJSON_Delete(request);

        // Parse response to extract assistant message and tool calls
        cJSON *response = cJSON_Parse(response_json);
        if (!response) {
            LOG_WARN("Failed to parse response JSON for call #%d", call_num);
            continue;
        }

        // Parse OpenAI response into internal message format
        InternalMessage assistant_msg = parse_openai_response(response);

        // Add assistant message to conversation
        if (assistant_msg.content_count > 0) {
            if (conversation_state_lock(state) == 0) {
                if (state->count < MAX_MESSAGES) {
                    state->messages[state->count] = assistant_msg;
                    state->count++;
                } else {
                    LOG_WARN("Conversation buffer full, cannot add more messages");
                    // Free the message contents
                    free_internal_message(&assistant_msg);
                }
                conversation_state_unlock(state);
            } else {
                // Failed to lock, free the message
                free_internal_message(&assistant_msg);
            }
        } else {
            // Empty message, free it
            free_internal_message(&assistant_msg);
        }

        cJSON_Delete(response);
    }

    sqlite3_finalize(stmt);

    if (call_num == 0) {
        LOG_ERROR("No valid API calls found in session");
        return -1;
    }

    LOG_INFO("Successfully loaded session with %d API calls", call_num);
    return 0;
}

/**
 * Get list of available sessions from the database
 */
char** session_get_list(PersistenceDB *db, int limit) {
    if (!db || !db->db) {
        LOG_ERROR("Invalid parameters to session_get_list");
        return NULL;
    }

    // Build query with optional limit
    char query[512];
    if (limit > 0) {
        snprintf(query, sizeof(query),
                 "SELECT DISTINCT session_id, MAX(created_at) as last_activity "
                 "FROM api_calls "
                 "WHERE session_id IS NOT NULL "
                 "GROUP BY session_id "
                 "ORDER BY last_activity DESC "
                 "LIMIT %d", limit);
    } else {
        snprintf(query, sizeof(query),
                 "SELECT DISTINCT session_id, MAX(created_at) as last_activity "
                 "FROM api_calls "
                 "WHERE session_id IS NOT NULL "
                 "GROUP BY session_id "
                 "ORDER BY last_activity DESC");
    }

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare query: %s", sqlite3_errmsg(db->db));
        return NULL;
    }

    // Count results
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        count++;
    }
    sqlite3_reset(stmt);

    if (count == 0) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    // Allocate array (plus one for NULL terminator)
    char **sessions = calloc((size_t)count + 1, sizeof(char *));
    if (!sessions) {
        LOG_ERROR("Failed to allocate session list");
        sqlite3_finalize(stmt);
        return NULL;
    }

    // Fetch sessions
    int idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < count) {
        const unsigned char *session_id = sqlite3_column_text(stmt, 0);
        if (session_id) {
            sessions[idx] = strdup((const char *)session_id);
            if (!sessions[idx]) {
                LOG_ERROR("Failed to duplicate session ID");
                // Clean up and return NULL
                for (int i = 0; i < idx; i++) {
                    free(sessions[i]);
                }
                free(sessions);
                sqlite3_finalize(stmt);
                return NULL;
            }
            idx++;
        }
    }

    sessions[idx] = NULL; // NULL terminator
    sqlite3_finalize(stmt);

    LOG_DEBUG("Retrieved %d sessions from database", idx);
    return sessions;
}

/**
 * Free a session list returned by session_get_list
 */
void session_free_list(char **sessions) {
    if (!sessions) {
        return;
    }

    for (int i = 0; sessions[i] != NULL; i++) {
        free(sessions[i]);
    }
    free(sessions);
}

/**
 * Get session metadata
 */
int session_get_metadata(PersistenceDB *db, const char *session_id,
                         char **timestamp, char **model, int *message_count) {
    if (!db || !db->db || !session_id || !timestamp || !model || !message_count) {
        LOG_ERROR("Invalid parameters to session_get_metadata");
        return -1;
    }

    // Initialize output parameters
    *timestamp = NULL;
    *model = NULL;
    *message_count = 0;

    const char *query =
        "SELECT MIN(timestamp) as start_time, "
        "       model, "
        "       COUNT(*) as call_count "
        "FROM api_calls "
        "WHERE session_id = ? "
        "GROUP BY session_id, model";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare query: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *ts = sqlite3_column_text(stmt, 0);
        const unsigned char *mod = sqlite3_column_text(stmt, 1);
        int calls = sqlite3_column_int(stmt, 2);

        if (ts) {
            *timestamp = strdup((const char *)ts);
        }
        if (mod) {
            *model = strdup((const char *)mod);
        }
        *message_count = calls * 2; // Rough estimate: each API call has user + assistant messages
    } else {
        LOG_WARN("No metadata found for session: %s", session_id);
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

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
int session_list_sessions(PersistenceDB *db, int limit) {
    if (!db || !db->db) {
        LOG_ERROR("Invalid parameters to session_list_sessions");
        return -1;
    }

    char **sessions = session_get_list(db, limit);
    if (!sessions) {
        printf("No sessions found in database.\n");
        return 0;
    }

    printf("\n");
    printf("=================================================================\n");
    printf("                    AVAILABLE SESSIONS\n");
    printf("=================================================================\n");
    printf("%-40s %-20s %-15s %s\n", "Session ID", "Started", "Model", "Messages");
    printf("-----------------------------------------------------------------\n");

    int count = 0;
    for (int i = 0; sessions[i] != NULL; i++) {
        char *timestamp = NULL;
        char *model = NULL;
        int message_count = 0;

        if (session_get_metadata(db, sessions[i], &timestamp, &model, &message_count) == 0) {
            // Truncate session ID for display
            char display_id[41];
            if (strlen(sessions[i]) > 40) {
                snprintf(display_id, sizeof(display_id), "%.37s...", sessions[i]);
            } else {
                snprintf(display_id, sizeof(display_id), "%s", sessions[i]);
            }

            printf("%-40s %-20s %-15s %d\n",
                   display_id,
                   timestamp ? timestamp : "unknown",
                   model ? model : "unknown",
                   message_count);

            free(timestamp);
            free(model);
            count++;
        } else {
            printf("%-40s %-20s %-15s %s\n",
                   sessions[i],
                   "unknown", "unknown", "unknown");
            count++;
        }
    }

    printf("-----------------------------------------------------------------\n");
    printf("Total: %d session(s)\n", count);
    printf("\n");
    printf("To resume a session, use: claude-c --resume <session_id>\n");
    printf("To dump a session, use: claude-c --dump-conversation <session_id>\n");
    printf("=================================================================\n\n");

    session_free_list(sessions);
    return 0;
}
