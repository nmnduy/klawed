/*
 * Session Persistence
 * Session dump and conversation export functionality
 */

#include "session_persistence.h"
#include "../persistence.h"
#include "../dump_utils.h"
#include "../logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

int session_dump_conversation(const char *session_id, const char *format) {
    PersistenceDB *db = persistence_init(NULL);
    if (!db) {
        fprintf(stderr, "Error: Failed to open persistence database\n");
        return 1;
    }

    // Query for all API calls in this session
    const char *query =
        "SELECT timestamp, request_json, response_json, model, status, error_message "
        "FROM api_calls "
        "WHERE session_id = ? "
        "ORDER BY created_at ASC";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare query: %s\n", sqlite3_errmsg(db->db));
        persistence_close(db);
        return 1;
    }

    // If no session_id provided, get the most recent one
    if (!session_id) {
        const char *latest_query =
            "SELECT session_id FROM api_calls "
            "WHERE session_id IS NOT NULL "
            "ORDER BY created_at DESC LIMIT 1";

        sqlite3_stmt *latest_stmt = NULL;
        rc = sqlite3_prepare_v2(db->db, latest_query, -1, &latest_stmt, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Error: Failed to get latest session: %s\n", sqlite3_errmsg(db->db));
            sqlite3_finalize(stmt);
            persistence_close(db);
            return 1;
        }

        rc = sqlite3_step(latest_stmt);
        if (rc == SQLITE_ROW) {
            const unsigned char *sid = sqlite3_column_text(latest_stmt, 0);
            if (sid) {
                session_id = strdup((const char *)sid);
            }
        }
        sqlite3_finalize(latest_stmt);

        if (!session_id) {
            fprintf(stderr, "Error: No sessions found in database\n");
            sqlite3_finalize(stmt);
            persistence_close(db);
            return 1;
        }
    }

    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_TRANSIENT);

    // Default format if not specified
    if (!format) {
        format = "default";
    }

    int call_num = 0;
    int step_rc;

    // For JSON format, we need to output a complete JSON array
    if (strcmp(format, "json") == 0) {
        fprintf(stdout, "{\n");
        fprintf(stdout, "  \"session_id\": \"%s\",\n", session_id);
        fprintf(stdout, "  \"api_calls\": [\n");

        int first_call = 1;
        while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            call_num++;

            const char *timestamp = (const char *)sqlite3_column_text(stmt, 0);
            const char *request_json = (const char *)sqlite3_column_text(stmt, 1);
            const char *response_json = (const char *)sqlite3_column_text(stmt, 2);
            const char *model = (const char *)sqlite3_column_text(stmt, 3);
            const char *status = (const char *)sqlite3_column_text(stmt, 4);
            const char *error_msg = (const char *)sqlite3_column_text(stmt, 5);

            if (!first_call) {
                fprintf(stdout, ",\n");
            }
            first_call = 0;

            // Use the JSON dump function
            dump_api_call_json(timestamp, request_json, response_json, model, status, error_msg, call_num, stdout);
        }

        fprintf(stdout, "\n  ]");

        // Check for SQLite errors
        if (step_rc != SQLITE_DONE) {
            fprintf(stderr, "Error: SQLite error while reading rows: %s\n", sqlite3_errmsg(db->db));
        }

        // Output empty array message as a JSON field if no calls
        if (call_num == 0) {
            fprintf(stdout, ",\n  \"message\": \"No API calls found for this session.\"");
        }

        fprintf(stdout, "\n}\n");
    }
    // For Markdown format
    else if (strcmp(format, "markdown") == 0 || strcmp(format, "md") == 0) {
        fprintf(stdout, "# Conversation: %s\n\n", session_id);

        while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            call_num++;

            const char *timestamp = (const char *)sqlite3_column_text(stmt, 0);
            const char *request_json = (const char *)sqlite3_column_text(stmt, 1);
            const char *response_json = (const char *)sqlite3_column_text(stmt, 2);
            const char *model = (const char *)sqlite3_column_text(stmt, 3);
            const char *status = (const char *)sqlite3_column_text(stmt, 4);
            const char *error_msg = (const char *)sqlite3_column_text(stmt, 5);

            // Use the Markdown dump function
            dump_api_call_markdown(timestamp, request_json, response_json, model, status, error_msg, call_num, stdout);
        }

        // Check for SQLite errors
        if (step_rc != SQLITE_DONE) {
            fprintf(stderr, "Error: SQLite error while reading rows: %s\n", sqlite3_errmsg(db->db));
        }

        if (call_num == 0) {
            fprintf(stdout, "*No API calls found for this session.*\n\n");
        }
    }
    // Default format (original format)
    else {
        fprintf(stdout, "\n");
        fprintf(stdout, "=================================================================\n");
        fprintf(stdout, "                    CONVERSATION DUMP\n");
        fprintf(stdout, "=================================================================\n");
        fprintf(stdout, "Session ID: %s\n", session_id);
        fprintf(stdout, "=================================================================\n\n");

        while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            call_num++;

            const char *timestamp = (const char *)sqlite3_column_text(stmt, 0);
            const char *request_json = (const char *)sqlite3_column_text(stmt, 1);
            const char *response_json = (const char *)sqlite3_column_text(stmt, 2);
            const char *model = (const char *)sqlite3_column_text(stmt, 3);
            const char *status = (const char *)sqlite3_column_text(stmt, 4);
            const char *error_msg = (const char *)sqlite3_column_text(stmt, 5);

            fprintf(stdout, "-----------------------------------------------------------------\n");
            fprintf(stdout, "API Call #%d - %s\n", call_num, timestamp ? timestamp : "unknown");
            fprintf(stdout, "Model: %s\n", model ? model : "unknown");
            fprintf(stdout, "Status: %s\n", status ? status : "unknown");
            fprintf(stdout, "-----------------------------------------------------------------\n\n");

            // Parse and display request
            if (request_json) {
                cJSON *request = cJSON_Parse(request_json);
                if (request) {
                    cJSON *messages = cJSON_GetObjectItem(request, "messages");
                    if (messages && cJSON_IsArray(messages)) {
                        fprintf(stdout, "REQUEST MESSAGES:\n");
                        int msg_count = cJSON_GetArraySize(messages);
                        for (int i = 0; i < msg_count; i++) {
                            cJSON *msg = cJSON_GetArrayItem(messages, i);
                            cJSON *role = cJSON_GetObjectItem(msg, "role");
                            cJSON *content = cJSON_GetObjectItem(msg, "content");

                            if (role && cJSON_IsString(role)) {
                                fprintf(stdout, "\n  [%s]\n", role->valuestring);
                            }

                            if (content) {
                                if (cJSON_IsString(content)) {
                                    fprintf(stdout, "  %s\n", content->valuestring);
                                } else if (cJSON_IsArray(content)) {
                                    int content_count = cJSON_GetArraySize(content);
                                    for (int j = 0; j < content_count; j++) {
                                        cJSON *block = cJSON_GetArrayItem(content, j);
                                        cJSON *type = cJSON_GetObjectItem(block, "type");

                                        if (type && cJSON_IsString(type)) {
                                            if (strcmp(type->valuestring, "text") == 0) {
                                                cJSON *text = cJSON_GetObjectItem(block, "text");
                                                if (text && cJSON_IsString(text)) {
                                                    fprintf(stdout, "  %s\n", text->valuestring);
                                                }
                                            } else if (strcmp(type->valuestring, "tool_use") == 0) {
                                                cJSON *name = cJSON_GetObjectItem(block, "name");
                                                cJSON *id = cJSON_GetObjectItem(block, "id");
                                                fprintf(stdout, "  [TOOL_USE: %s", name && cJSON_IsString(name) ? name->valuestring : "unknown");
                                                if (id && cJSON_IsString(id)) {
                                                    fprintf(stdout, " (id: %s)", id->valuestring);
                                                }
                                                fprintf(stdout, "]\n");
                                            } else if (strcmp(type->valuestring, "tool_result") == 0) {
                                                cJSON *tool_use_id = cJSON_GetObjectItem(block, "tool_use_id");
                                                fprintf(stdout, "  [TOOL_RESULT");
                                                if (tool_use_id && cJSON_IsString(tool_use_id)) {
                                                    fprintf(stdout, " for %s", tool_use_id->valuestring);
                                                }
                                                fprintf(stdout, "]\n");
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    cJSON_Delete(request);
                }
            }

            // Parse and display response
            fprintf(stdout, "\nRESPONSE:\n");
            if (status && strcmp(status, "error") == 0 && error_msg) {
                fprintf(stdout, "  [ERROR] %s\n", error_msg);
            } else if (response_json) {
                (void)dump_response_content(response_json, stdout);
            }

            fprintf(stdout, "\n");
        }

        // Check for SQLite errors
        if (step_rc != SQLITE_DONE) {
            fprintf(stderr, "Error: SQLite error while reading rows: %s\n", sqlite3_errmsg(db->db));
        }

        if (call_num == 0) {
            fprintf(stdout, "No API calls found for this session.\n");
        }

        fprintf(stdout, "=================================================================\n");
        fprintf(stdout, "                    END OF CONVERSATION\n");
        fprintf(stdout, "=================================================================\n\n");
    }

    sqlite3_finalize(stmt);
    persistence_close(db);
    return 0;
}
