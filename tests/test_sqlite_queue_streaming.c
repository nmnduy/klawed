/*
 * test_sqlite_queue_streaming.c - Unit tests for SQLite queue streaming functions
 *
 * Tests the streaming chunk functionality:
 * - sqlite_queue_send_streaming_chunk()
 * - sqlite_queue_streaming_callback()
 *
 * These tests verify that streaming text chunks are properly formatted
 * and stored in the SQLite queue database.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <cjson/cJSON.h>

// Include headers for stub declarations
#include "../src/klawed_internal.h"

// Forward declarations for stub functions
struct SubagentManager;
struct TODO;
struct SubagentManager* subagent_manager_init(void);
void subagent_manager_free(struct SubagentManager *mgr);
void register_subagent_manager_for_cleanup(struct SubagentManager *mgr);
struct TODO* todo_init(void);
void todo_free(struct TODO *todo);
int get_colorscheme_color(int color_id);

// Stubs for functions not used in streaming tests but referenced by sqlite_queue.c
// These are needed because sqlite_queue.c contains restore_conversation which
// depends on conversation functionality, but we don't use those code paths here.
void add_user_message(struct ConversationState *state, const char *text) { (void)state; (void)text; }
struct SubagentManager* subagent_manager_init(void) { return NULL; }
void subagent_manager_free(struct SubagentManager *mgr) { (void)mgr; }
void register_subagent_manager_for_cleanup(struct SubagentManager *mgr) { (void)mgr; }
struct TODO* todo_init(void) { return NULL; }
void todo_free(struct TODO *todo) { (void)todo; }
int get_colorscheme_color(int color_id) { (void)color_id; return 0; }

// Include the actual SQLite queue header
#include "../src/sqlite_queue.h"

#define TEST_DB "/tmp/test_sqlite_queue_streaming.db"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf("  ✓ %s\n", message); \
        } else { \
            tests_failed++; \
            printf("  ✗ %s\n", message); \
        } \
    } while (0)

static int test_send_streaming_chunk(void) {
    printf("\nTest: Send streaming text chunks\n");

    unlink(TEST_DB);

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB, "klawed");
    SQLiteQueueContext *client = sqlite_queue_init(TEST_DB, "client");

    if (!ctx || !client) {
        printf("  FAILED: Could not initialize contexts\n");
        if (ctx) sqlite_queue_cleanup(ctx);
        if (client) sqlite_queue_cleanup(client);
        return -1;
    }

    // Send multiple streaming chunks
    const char *chunks[] = {"Hello, ", "world", "!"};
    for (size_t i = 0; i < sizeof(chunks)/sizeof(chunks[0]); i++) {
        int result = sqlite_queue_send_streaming_chunk(ctx, "client", chunks[i]);
        TEST_ASSERT(result == SQLITE_QUEUE_ERROR_NONE, "Send streaming chunk should succeed");
    }

    // Receive and verify
    char **messages = NULL;
    long long *message_ids = NULL;
    int message_count = 0;

    // Poll with retries
    int retries = 10;
    int result = SQLITE_QUEUE_ERROR_NO_MESSAGES;
    while (retries-- > 0 && result == SQLITE_QUEUE_ERROR_NO_MESSAGES) {
        result = sqlite_queue_receive(client, NULL, 10, &messages, &message_count, &message_ids);
        if (result == SQLITE_QUEUE_ERROR_NO_MESSAGES) {
            usleep(100000); // 100ms
        }
    }

    TEST_ASSERT(result == SQLITE_QUEUE_ERROR_NONE, "Receive should succeed");
    TEST_ASSERT(message_count == 3, "Should receive 3 chunks");

    if (message_count == 3) {
        // Verify each message format
        for (int i = 0; i < 3; i++) {
            cJSON *json = cJSON_Parse(messages[i]);
            TEST_ASSERT(json != NULL, "Message should be valid JSON");

            if (json) {
                cJSON *msg_type = cJSON_GetObjectItem(json, "messageType");
                cJSON *content = cJSON_GetObjectItem(json, "content");
                cJSON *is_complete = cJSON_GetObjectItem(json, "isComplete");

                TEST_ASSERT(msg_type && cJSON_IsString(msg_type),
                           "Should have messageType field");
                TEST_ASSERT(strcmp(msg_type->valuestring, "TEXT_STREAM_CHUNK") == 0,
                           "messageType should be TEXT_STREAM_CHUNK");
                TEST_ASSERT(content && cJSON_IsString(content),
                           "Should have content field");
                TEST_ASSERT(strcmp(content->valuestring, chunks[i]) == 0,
                           "Content should match sent chunk");
                TEST_ASSERT(is_complete && cJSON_IsBool(is_complete),
                           "Should have isComplete field");
                // Debug: print the actual boolean value
                int is_true = cJSON_IsTrue(is_complete);
                int is_false = cJSON_IsFalse(is_complete);
                int bool_value = is_complete->valueint;
                if (is_true) {
                    printf("    [DEBUG] isComplete is TRUE (valueint=%d)\n", bool_value);
                } else if (is_false) {
                    printf("    [DEBUG] isComplete is FALSE (valueint=%d)\n", bool_value);
                } else {
                    printf("    [DEBUG] isComplete is neither TRUE nor FALSE (type=%d, valueint=%d)\n",
                           is_complete->type, bool_value);
                }
                TEST_ASSERT(!is_true, "isComplete should not be true");

                cJSON_Delete(json);
            }
        }
    }

    // Cleanup
    for (int i = 0; i < message_count; i++) free(messages[i]);
    free(messages);
    free(message_ids);
    sqlite_queue_cleanup(ctx);
    sqlite_queue_cleanup(client);
    unlink(TEST_DB);

    return (tests_failed > 0) ? -1 : 0;
}

static int test_streaming_callback(void) {
    printf("\nTest: Streaming callback function\n");

    unlink(TEST_DB);

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB, "klawed");
    SQLiteQueueContext *client = sqlite_queue_init(TEST_DB, "client");

    if (!ctx || !client) {
        printf("  FAILED: Could not initialize contexts\n");
        if (ctx) sqlite_queue_cleanup(ctx);
        if (client) sqlite_queue_cleanup(client);
        return -1;
    }

    // Test callback with valid parameters
    const char *test_chunk = "Callback test chunk";
    sqlite_queue_streaming_callback(test_chunk, ctx);

    // Verify message was sent
    char **messages = NULL;
    long long *message_ids = NULL;
    int message_count = 0;

    int retries = 10;
    int result = SQLITE_QUEUE_ERROR_NO_MESSAGES;
    while (retries-- > 0 && result == SQLITE_QUEUE_ERROR_NO_MESSAGES) {
        result = sqlite_queue_receive(client, NULL, 10, &messages, &message_count, &message_ids);
        if (result == SQLITE_QUEUE_ERROR_NO_MESSAGES) {
            usleep(100000);
        }
    }

    TEST_ASSERT(result == SQLITE_QUEUE_ERROR_NONE, "Receive should succeed after callback");
    TEST_ASSERT(message_count == 1, "Should receive 1 message");

    if (message_count == 1) {
        cJSON *json = cJSON_Parse(messages[0]);
        TEST_ASSERT(json != NULL, "Message should be valid JSON");

        if (json) {
            cJSON *msg_type = cJSON_GetObjectItem(json, "messageType");
            cJSON *content = cJSON_GetObjectItem(json, "content");

            TEST_ASSERT(msg_type && strcmp(msg_type->valuestring, "TEXT_STREAM_CHUNK") == 0,
                       "Message type should be TEXT_STREAM_CHUNK");
            TEST_ASSERT(content && strcmp(content->valuestring, test_chunk) == 0,
                       "Content should match callback argument");

            cJSON_Delete(json);
        }
    }

    // Cleanup first batch
    for (int i = 0; i < message_count; i++) free(messages[i]);
    free(messages);
    free(message_ids);

    // Test NULL parameter handling (should not crash)
    printf("  Testing NULL parameter handling...\n");
    sqlite_queue_streaming_callback(NULL, ctx);       // NULL chunk
    sqlite_queue_streaming_callback("test", NULL);    // NULL userdata
    sqlite_queue_streaming_callback(NULL, NULL);      // Both NULL
    TEST_ASSERT(true, "NULL parameter handling (no crash)");

    sqlite_queue_cleanup(ctx);
    sqlite_queue_cleanup(client);
    unlink(TEST_DB);

    return (tests_failed > 0) ? -1 : 0;
}

static int test_empty_chunk(void) {
    printf("\nTest: Empty and NULL chunks (heartbeats)\n");

    unlink(TEST_DB);

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB, "klawed");
    SQLiteQueueContext *client = sqlite_queue_init(TEST_DB, "client");

    if (!ctx || !client) {
        printf("  FAILED: Could not initialize contexts\n");
        if (ctx) sqlite_queue_cleanup(ctx);
        if (client) sqlite_queue_cleanup(client);
        return -1;
    }

    // Test empty string
    int result = sqlite_queue_send_streaming_chunk(ctx, "client", "");
    TEST_ASSERT(result == SQLITE_QUEUE_ERROR_NONE, "Empty string chunk should succeed");

    // Test NULL chunk
    result = sqlite_queue_send_streaming_chunk(ctx, "client", NULL);
    TEST_ASSERT(result == SQLITE_QUEUE_ERROR_NONE, "NULL chunk should succeed");

    // Receive and verify
    char **messages = NULL;
    long long *message_ids = NULL;
    int message_count = 0;

    int retries = 10;
    result = SQLITE_QUEUE_ERROR_NO_MESSAGES;
    while (retries-- > 0 && result == SQLITE_QUEUE_ERROR_NO_MESSAGES) {
        result = sqlite_queue_receive(client, NULL, 10, &messages, &message_count, &message_ids);
        if (result == SQLITE_QUEUE_ERROR_NO_MESSAGES) {
            usleep(100000);
        }
    }

    TEST_ASSERT(result == SQLITE_QUEUE_ERROR_NONE, "Receive should succeed");
    TEST_ASSERT(message_count == 2, "Should receive 2 messages");

    if (message_count == 2) {
        // Both should have empty content
        for (int i = 0; i < 2; i++) {
            cJSON *json = cJSON_Parse(messages[i]);
            if (json) {
                cJSON *content = cJSON_GetObjectItem(json, "content");
                TEST_ASSERT(content && cJSON_IsString(content) && strlen(content->valuestring) == 0,
                           "Empty chunk should have empty content");
                cJSON_Delete(json);
            }
        }
    }

    // Cleanup
    for (int i = 0; i < message_count; i++) free(messages[i]);
    free(messages);
    free(message_ids);
    sqlite_queue_cleanup(ctx);
    sqlite_queue_cleanup(client);
    unlink(TEST_DB);

    return (tests_failed > 0) ? -1 : 0;
}

static int test_invalid_params(void) {
    printf("\nTest: Invalid parameters\n");

    unlink(TEST_DB);

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB, "klawed");

    if (!ctx) {
        printf("  FAILED: Could not initialize context\n");
        return -1;
    }

    // Test NULL context
    int result = sqlite_queue_send_streaming_chunk(NULL, "client", "test");
    TEST_ASSERT(result != SQLITE_QUEUE_ERROR_NONE, "NULL context should fail");

    // Test NULL receiver
    result = sqlite_queue_send_streaming_chunk(ctx, NULL, "test");
    TEST_ASSERT(result != SQLITE_QUEUE_ERROR_NONE, "NULL receiver should fail");

    sqlite_queue_cleanup(ctx);
    unlink(TEST_DB);

    return (tests_failed > 0) ? -1 : 0;
}

int main(void) {
    printf("========================================\n");
    printf("SQLite Queue Streaming Tests\n");
    printf("========================================\n");

    if (test_send_streaming_chunk() != 0) {}
    if (test_streaming_callback() != 0) {}
    if (test_empty_chunk() != 0) {}
    if (test_invalid_params() != 0) {}

    printf("\n========================================\n");
    printf("Test Summary:\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    if (tests_failed == 0) {
        printf("\n✓ All tests PASSED!\n");
    } else {
        printf("\n✗ Some tests FAILED!\n");
    }
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
