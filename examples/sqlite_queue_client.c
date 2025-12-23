/*
 * sqlite_queue_client.c - Simple client for testing SQLite queue communication with klawed
 *
 * This example demonstrates:
 * 1. How to send messages to klawed via SQLite queue
 * 2. How to receive responses from klawed via SQLite queue
 *
 * Usage:
 *   1. Start klawed in daemon mode:
 *      ./build/klawed --sqlite-queue /path/to/messages.db
 *      Or via env: KLAWED_SQLITE_DB_PATH=/path/to/messages.db ./build/klawed
 *
 *   2. Run this client:
 *      gcc -o sqlite_queue_client examples/sqlite_queue_client.c -lsqlite3 -lcjson
 *      ./sqlite_queue_client /path/to/messages.db "your prompt here"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

#define DB_PATH_MAX 256
#define SENDER_NAME "client"
#define RECEIVER_NAME "klawed"
#define POLL_INTERVAL_MS 100
#define MAX_RETRIES 100

static int send_message(sqlite3 *db, const char *receiver, const char *message_type, const char *content) {
    char *errmsg = NULL;
    int rc;

    // Create JSON message
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        fprintf(stderr, "Failed to create JSON object\n");
        return -1;
    }
    cJSON_AddStringToObject(json, "messageType", message_type);
    if (content) {
        cJSON_AddStringToObject(json, "content", content);
    }

    char *json_str = cJSON_PrintUnformatted(json);
    if (!json_str) {
        fprintf(stderr, "Failed to serialize JSON\n");
        cJSON_Delete(json);
        return -1;
    }

    // Insert message
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "INSERT INTO messages (sender, receiver, message, sent) VALUES ('%s', '%s', '%s', 0);",
             SENDER_NAME, receiver, json_str);

    // Use sqlite3_exec with proper escaping
    rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to insert message: %s\n", errmsg ? errmsg : sqlite3_errmsg(db));
        free(json_str);
        cJSON_Delete(json);
        if (errmsg) sqlite3_free(errmsg);
        return -1;
    }

    free(json_str);
    cJSON_Delete(json);
    return 0;
}

static int receive_messages(sqlite3 *db, char ***messages, long long **ids, int *count) {
    char *errmsg = NULL;
    int rc;

    *messages = NULL;
    *ids = NULL;
    *count = 0;

    // Query for messages from klawed
    const char *sql = "SELECT id, message FROM messages WHERE sender = 'klawed' AND sent = 0 ORDER BY created_at ASC LIMIT 10;";

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    // Allocate arrays for results
    char **msg_array = calloc(100, sizeof(char *));
    long long *id_array = calloc(100, sizeof(long long));
    if (!msg_array || !id_array) {
        free(msg_array);
        free(id_array);
        sqlite3_finalize(stmt);
        return -1;
    }

    int msg_count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && msg_count < 100) {
        id_array[msg_count] = sqlite3_column_int64(stmt, 0);
        const char *msg_text = (const char *)sqlite3_column_text(stmt, 1);
        if (msg_text) {
            msg_array[msg_count] = strdup(msg_text);
            if (!msg_array[msg_count]) {
                fprintf(stderr, "Failed to allocate memory for message\n");
                for (int i = 0; i < msg_count; i++) {
                    free(msg_array[i]);
                }
                free(msg_array);
                free(id_array);
                sqlite3_finalize(stmt);
                return -1;
            }
            msg_count++;
        }
    }

    sqlite3_finalize(stmt);

    if (msg_count > 0) {
        *messages = msg_array;
        *ids = id_array;
        *count = msg_count;
    } else {
        free(msg_array);
        free(id_array);
    }

    return msg_count;
}

static int acknowledge_message(sqlite3 *db, long long msg_id) {
    char sql[256];
    snprintf(sql, sizeof(sql), "UPDATE messages SET sent = 1 WHERE id = %lld;", msg_id);

    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to acknowledge message: %s\n", errmsg ? errmsg : sqlite3_errmsg(db));
        if (errmsg) sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <database_path> <prompt>\n", argv[0]);
        fprintf(stderr, "\nExample:\n");
        fprintf(stderr, "  %s /tmp/messages.db \"list files in current directory\"\n", argv[0]);
        return 1;
    }

    const char *db_path = argv[1];
    const char *prompt = argv[2];

    printf("SQLite Queue Client\n");
    printf("===================\n");
    printf("Database: %s\n", db_path);
    printf("Prompt: %s\n\n", prompt);

    // Open database
    sqlite3 *db = NULL;
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // Send prompt to klawed
    printf("Sending message to klawed...\n");
    if (send_message(db, RECEIVER_NAME, "TEXT", prompt) != 0) {
        fprintf(stderr, "Failed to send message\n");
        sqlite3_close(db);
        return 1;
    }
    printf("Message sent successfully\n\n");

    // Poll for responses
    printf("Waiting for responses from klawed...\n");
    printf("(Polling every %d ms)\n\n", POLL_INTERVAL_MS);

    int total_messages = 0;
    int poll_count = 0;
    int still_processing = 1;

    while (still_processing && poll_count < MAX_RETRIES) {
        poll_count++;

        char **messages = NULL;
        long long *ids = NULL;
        int count = 0;

        int received = receive_messages(db, &messages, &ids, &count);
        if (received > 0) {
            printf("Received %d message(s):\n", count);
            for (int i = 0; i < count; i++) {
                // Parse and display message
                cJSON *json = cJSON_Parse(messages[i]);
                if (json) {
                    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
                    cJSON *content = cJSON_GetObjectItem(json, "content");

                    const char *type_str = message_type && cJSON_IsString(message_type)
                        ? message_type->valuestring : "UNKNOWN";

                    printf("\n[%s]", type_str);

                    if (content && cJSON_IsString(content)) {
                        printf(" %s\n", content->valuestring);
                    } else if (content && cJSON_IsObject(content)) {
                        // For tool results, print the JSON
                        char *content_str = cJSON_PrintUnformatted(content);
                        printf(" %s\n", content_str);
                        free(content_str);
                    } else {
                        printf("\n");
                    }

                    // Check if processing is complete
                    if (strcmp(type_str, "COMPLETED") == 0) {
                        still_processing = 0;
                    }

                    cJSON_Delete(json);
                } else {
                    printf("RAW: %s\n", messages[i]);
                }

                // Acknowledge message
                acknowledge_message(db, ids[i]);
                free(messages[i]);
            }

            free(messages);
            free(ids);
            total_messages += count;

        } else {
            // No messages, sleep briefly
            usleep(POLL_INTERVAL_MS * 1000);
        }
    }

    if (poll_count >= MAX_RETRIES) {
        fprintf(stderr, "\nTimeout: No response after %d polls (%.1f seconds)\n",
                MAX_RETRIES, MAX_RETRIES * POLL_INTERVAL_MS / 1000.0);
    }

    printf("\n");
    printf("===================\n");
    printf("Total messages received: %d\n", total_messages);
    printf("Polls made: %d\n", poll_count);

    sqlite3_close(db);
    return 0;
}
