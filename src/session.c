/*
 * Session Management - Load and resume conversations from persistence database
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>
#include <stdint.h>
#include <bsd/string.h>

#include "session.h"
#include "session_resolve.h"
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
        // Try partial match resolution first
        char errmsg[256] = {0};
        target_session_id = session_resolve_partial_id(db, session_id, errmsg);
        if (!target_session_id) {
            LOG_ERROR("Failed to resolve session ID '%s': %s", session_id, errmsg);
            fprintf(stderr, "Error: %s\n", errmsg);
            fprintf(stderr, "Use -l/--list-sessions to see available session IDs.\n");
            return -1;
        }
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
    LOG_DEBUG("session_load_from_db: statement bound");

    LOG_DEBUG("session_load_from_db: calling clear_conversation");
    // Clear existing conversation state (except system/compaction message at position 0)
    clear_conversation(state);

    // Ensure we have a system message at position 0
    // If clear_conversation didn't preserve one, add a default system message
    if (state->count == 0) {
        const char *default_system = "You are a helpful AI assistant.";
        add_system_message(state, default_system);
        LOG_DEBUG("session_load_from_db: added default system message");
    }

    // Set the session ID
    // Note: This takes ownership - we free any existing session_id and replace it with target_session_id.
    // Callers must not use or free any pointer that was aliased to state->session_id after this call.
    if (state->session_id) {
        free(state->session_id);
    }
    state->session_id = target_session_id;
    LOG_DEBUG("session_load_from_db: session_id set");

    LOG_DEBUG("session_load_from_db: about to execute query");
    // Process each API call in the session
    int call_num = 0;
    LOG_DEBUG("session_load_from_db: calling sqlite3_step");
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        call_num++;
        LOG_DEBUG("session_load_from_db: got row %d", call_num);
        LOG_DEBUG("session_load_from_db: getting column data");

        const char *request_json = (const char *)sqlite3_column_text(stmt, 0);
        const char *response_json = (const char *)sqlite3_column_text(stmt, 1);
        const char *model = (const char *)sqlite3_column_text(stmt, 2);
        const char *status = (const char *)sqlite3_column_text(stmt, 3);
        LOG_DEBUG("session_load_from_db: got column data");

        LOG_DEBUG("session_load_from_db: checking column completeness");
        if (!request_json || !response_json || !model || !status) {
            LOG_WARN("Skipping incomplete API call #%d in session", call_num);
            continue;
        }

        LOG_DEBUG("session_load_from_db: checking status");
        if (strcmp(status, "error") == 0) {
            LOG_WARN("Skipping failed API call #%d in session", call_num);
            continue;
        }
        LOG_DEBUG("session_load_from_db: status OK");

        LOG_DEBUG("session_load_from_db: parsing request JSON");
        // Parse request to extract user message
        cJSON *request = cJSON_Parse(request_json);
        LOG_DEBUG("session_load_from_db: cJSON_Parse returned %p", (void*)request);
        if (!request) {
            LOG_WARN("Failed to parse request JSON for call #%d", call_num);
            continue;
        }
        LOG_DEBUG("session_load_from_db: request parsed successfully");

        LOG_DEBUG("session_load_from_db: getting messages array");
        cJSON *messages = cJSON_GetObjectItem(request, "messages");
        LOG_DEBUG("session_load_from_db: messages = %p", (void*)messages);
        if (!messages || !cJSON_IsArray(messages)) {
            LOG_WARN("No messages array in request for call #%d", call_num);
            cJSON_Delete(request);
            continue;
        }
        LOG_DEBUG("session_load_from_db: messages is array, size = %d", cJSON_GetArraySize(messages));

        LOG_DEBUG("session_load_from_db: finding last user message");
        // Find the last user message in the request (this is what triggered the API call)
        cJSON *last_user_message = NULL;
        int msg_count = cJSON_GetArraySize(messages);
        LOG_DEBUG("session_load_from_db: msg_count = %d", msg_count);
        for (int i = msg_count - 1; i >= 0; i--) {
            LOG_DEBUG("session_load_from_db: checking message %d", i);
            cJSON *msg = cJSON_GetArrayItem(messages, i);
            if (!msg) {
                LOG_DEBUG("session_load_from_db: msg is NULL at index %d", i);
                continue;
            }
            cJSON *role = cJSON_GetObjectItem(msg, "role");
            if (role && cJSON_IsString(role) && strcmp(role->valuestring, "user") == 0) {
                last_user_message = msg;
                LOG_DEBUG("session_load_from_db: found user message at index %d", i);
                break;
            }
        }
        LOG_DEBUG("session_load_from_db: last_user_message = %p", (void*)last_user_message);

        if (!last_user_message) {
            LOG_WARN("No user message found in request for call #%d", call_num);
            cJSON_Delete(request);
            continue;
        }

        LOG_DEBUG("session_load_from_db: extracting content");
        // Extract user message content
        cJSON *content = cJSON_GetObjectItem(last_user_message, "content");
        LOG_DEBUG("session_load_from_db: content = %p", (void*)content);
        if (!content) {
            LOG_WARN("No content in user message for call #%d", call_num);
            cJSON_Delete(request);
            continue;
        }

        LOG_DEBUG("session_load_from_db: checking content type");
        // Handle different content formats
        if (cJSON_IsString(content)) {
            LOG_DEBUG("session_load_from_db: content is string, adding user message");
            // Simple text message
            add_user_message(state, content->valuestring);
            LOG_DEBUG("session_load_from_db: user message added");
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
        LOG_DEBUG("session_load_from_db: request deleted");

        LOG_DEBUG("session_load_from_db: parsing response JSON");
        // Parse response to extract assistant message and tool calls
        cJSON *response = cJSON_Parse(response_json);
        LOG_DEBUG("session_load_from_db: response = %p", (void*)response);
        if (!response) {
            LOG_WARN("Failed to parse response JSON for call #%d", call_num);
            continue;
        }

        LOG_DEBUG("session_load_from_db: calling parse_openai_response");
        // Parse OpenAI response into internal message format
        InternalMessage assistant_msg;
        parse_openai_response(response, &assistant_msg);
        LOG_DEBUG("session_load_from_db: parse_openai_response returned, content_count = %d", assistant_msg.content_count);

        LOG_DEBUG("session_load_from_db: adding assistant message to conversation");
        // Add assistant message to conversation
        if (assistant_msg.content_count > 0) {
            LOG_DEBUG("session_load_from_db: locking conversation state");
            if (conversation_state_lock(state) == 0) {
                LOG_DEBUG("session_load_from_db: locked, state->count = %d", state->count);
                if (state->count < MAX_MESSAGES) {
                    LOG_DEBUG("session_load_from_db: assigning message to array");
                    state->messages[state->count] = assistant_msg;
                    state->count++;
                    LOG_DEBUG("session_load_from_db: message assigned, new count = %d", state->count);
                    // Message successfully added to state, clear assistant_msg to avoid double-free
                    // Set content_count to 0 so it won't be freed again
                    assistant_msg.content_count = 0;
                    assistant_msg.contents = NULL;
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
    printf("================================================================================\n");
    printf("                           AVAILABLE SESSIONS\n");
    printf("================================================================================\n");
    printf("%-45s %-30s %-10s %-19s %-5s\n", "Summary", "Session ID", "Model", "Started", "Msgs");
    printf("--------------------------------------------------------------------------------\n");

    int count = 0;
    for (int i = 0; sessions[i] != NULL; i++) {
        char *timestamp = NULL;
        char *model = NULL;
        int message_count = 0;

        if (session_get_metadata(db, sessions[i], &timestamp, &model, &message_count) == 0) {

            // Get session title — display prominently in first column
            char *title = persistence_get_session_title(db, sessions[i]);
            if (!title) {
                // No stored title — extract first user message as fallback
                title = persistence_extract_first_user_message(db, sessions[i]);
                if (title) {
                    // Save it for future listings
                    persistence_set_session_title(db, sessions[i], title);
                }
            }
            char display_title[48];
            if (title) {
                // Truncate title for 45-char column
                if (strlen(title) > 45) {
                    snprintf(display_title, sizeof(display_title), "%.42s...", title);
                } else {
                    strlcpy(display_title, title, sizeof(display_title));
                }
                free(title);
            } else {
                // No title available — show placeholder
                strlcpy(display_title, "-", sizeof(display_title));
            }

            // Truncate model for display
            char display_model[12];
            if (model) {
                if (strlen(model) > 10) {
                    snprintf(display_model, sizeof(display_model), "%.7s...", model);
                } else {
                    strlcpy(display_model, model, sizeof(display_model));
                }
                free(model);
            } else {
                strlcpy(display_model, "unknown", sizeof(display_model));
            }

            // Truncate timestamp for display
            char display_time[20];
            if (timestamp) {
                // Show just date + time (first 19 chars of ISO format)
                if (strlen(timestamp) > 19) {
                    snprintf(display_time, sizeof(display_time), "%.19s", timestamp);
                } else {
                    strlcpy(display_time, timestamp, sizeof(display_time));
                }
                free(timestamp);
            } else {
                strlcpy(display_time, "unknown", sizeof(display_time));
            }

            printf("%-45s %-30s %-10s %-19s %-5d\n",
                   display_title,
                   sessions[i],
                   display_model,
                   display_time,
                   message_count);

            count++;
        } else {
            // Get session title — fall back to first user message if no stored title
            char *title = persistence_get_session_title(db, sessions[i]);
            if (!title) {
                title = persistence_extract_first_user_message(db, sessions[i]);
                if (title) {
                    persistence_set_session_title(db, sessions[i], title);
                }
            }
            char display_title[48];

            if (title) {
                if (strlen(title) > 45) {
                    snprintf(display_title, sizeof(display_title), "%.42s...", title);
                } else {
                    strlcpy(display_title, title, sizeof(display_title));
                }
                free(title);
            } else {
                strlcpy(display_title, "-", sizeof(display_title));
            }

            printf("%-45s %-30s %-10s %-19s %-5s\n",
                   display_title,
                   sessions[i],
                   "unknown", "unknown", "unknown");
            count++;
        }
    }

    printf("--------------------------------------------------------------------------------\n");
    printf("Total: %d session(s)\n", count);
    printf("\n");
    printf("To resume a session, use: klawed --resume <session_id>\n");
    printf("To dump a session, use: klawed --dump-conversation <session_id>\n");
    printf("================================================================================\n\n");

    session_free_list(sessions);
    return 0;
}

/**
 * Generate a session title from conversation content
 *
 * Extracts the first user message and creates a concise title
 * (~60 chars max, newlines removed, whitespace collapsed).
 */
char* session_generate_title(ConversationState *state) {
    if (!state) {
        return NULL;
    }

    // Find the first user message in the conversation
    const char *user_text = NULL;
    for (int i = 0; i < state->count; i++) {
        if (state->messages[i].role == MSG_USER) {
            // Extract text from first text content block
            for (int j = 0; j < state->messages[i].content_count; j++) {
                if (state->messages[i].contents[j].type == INTERNAL_TEXT &&
                    state->messages[i].contents[j].text) {
                    user_text = state->messages[i].contents[j].text;
                    break;
                }
            }
            if (user_text) break;
        }
    }

    if (!user_text || user_text[0] == '\0') {
        return NULL;
    }

    // Clean up the text: strip leading/trailing whitespace, collapse newlines
    size_t len = strlen(user_text);
    char *clean = malloc(len + 1);
    if (!clean) return NULL;

    int in_space = 1;  // Start in "space" mode to skip leading whitespace
    size_t out_idx = 0;
    int max_chars = 125;

    for (size_t i = 0; i < len && (int)out_idx < max_chars; i++) {
        char c = user_text[i];
        if (c == '\n' || c == '\r' || c == '\t') {
            if (!in_space) {
                clean[out_idx++] = ' ';
                in_space = 1;
            }
        } else if (c == ' ') {
            if (!in_space) {
                clean[out_idx++] = ' ';
                in_space = 1;
            }
        } else {
            clean[out_idx++] = c;
            in_space = 0;
        }
    }

    // Remove trailing space
    if (out_idx > 0 && clean[out_idx - 1] == ' ') {
        out_idx--;
    }

    clean[out_idx] = '\0';

    if (out_idx == 0) {
        free(clean);
        return NULL;
    }

    return clean;
}

/**
 * Try to generate and save a session title if one doesn't exist yet
 *
 * Checks the total token usage for the session, and if it exceeds the
 * threshold (configurable via KLAWED_SESSION_TITLE_THRESHOLD env var,
 * default: 1000 tokens), generates a title from conversation context
 * and saves it to the database.
 */
int session_maybe_generate_title(ConversationState *state) {
    if (!state || !state->persistence_db || !state->session_id) {
        return -1;
    }

    // Check if a title already exists
    char *existing_title = persistence_get_session_title(
        state->persistence_db, state->session_id);
    if (existing_title) {
        free(existing_title);
        return 0;  // Title already exists, nothing to do
    }

    // Check total token count against threshold
    int64_t prompt_tokens = 0;
    int64_t completion_tokens = 0;
    int64_t cached_tokens = 0;

    int result = persistence_get_session_token_totals(
        state->persistence_db,
        state->session_id,
        &prompt_tokens,
        &completion_tokens,
        &cached_tokens
    );

    if (result != 0) {
        // Can't determine token count, still try to generate a title
        // if we have at least one user message
        LOG_DEBUG("Could not get token totals, will generate title anyway");
    } else {
        // Check threshold
        int threshold = 1000;  // Default threshold
        const char *env_threshold = getenv("KLAWED_SESSION_TITLE_THRESHOLD");
        if (env_threshold) {
            int parsed = atoi(env_threshold);
            if (parsed > 0) {
                threshold = parsed;
            }
        }

        int64_t total_tokens = prompt_tokens + completion_tokens;
        if (total_tokens < threshold) {
            LOG_DEBUG("Session tokens (%lld) below title threshold (%d), skipping",
                     (long long)total_tokens, threshold);
            return 0;
        }
    }

    // Generate title from conversation content
    char *title = session_generate_title(state);
    if (!title) {
        LOG_DEBUG("Could not generate session title from conversation");
        return -1;
    }

    // Save to database
    result = persistence_set_session_title(
        state->persistence_db, state->session_id, title);
    if (result != 0) {
        LOG_WARN("Failed to save session title to database");
        free(title);
        return -1;
    }

    LOG_INFO("Generated session title: %s", title);
    free(title);
    return 0;
}
