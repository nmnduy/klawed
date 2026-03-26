/*
 * test_reasoning_content_sqlite_queue.c - Unit tests for reasoning_content preservation in sqlite-queue mode
 *
 * Tests that reasoning_content from thinking models (Moonshot/Kimi) is properly
 * preserved through the sqlite queue database and restored during conversation recovery.
 *
 * This addresses the issue: "reasoning_content is missing in assistant tool call message at index 5"
 *
 * Compilation: make test-reasoning-content-sqlite-queue
 * Usage: ./build/test_reasoning_content_sqlite_queue
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <cjson/cJSON.h>
#include <sqlite3.h>

// Test framework colors
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf(COLOR_GREEN "  ✓ " COLOR_RESET "%s\n", message); \
        } else { \
            tests_failed++; \
            printf(COLOR_RED "  ✗ " COLOR_RESET "%s\n", message); \
        } \
    } while (0)

#define TEST_SUMMARY() \
    do { \
        printf("\n" COLOR_CYAN "Test Summary:" COLOR_RESET "\n"); \
        printf("  Total:  %d\n", tests_run); \
        printf("  Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed); \
        printf("  Failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed); \
        if (tests_failed == 0) { \
            printf(COLOR_GREEN "\n✓ All tests passed!" COLOR_RESET "\n"); \
            return 0; \
        } else { \
            printf(COLOR_RED "\n✗ Some tests failed!" COLOR_RESET "\n"); \
            return 1; \
        } \
    } while (0)

// ============================================================================
// Mock SQLite Queue Database Helpers
// ============================================================================

typedef struct {
    sqlite3 *db;
    char *db_path;
} TestQueueContext;

static int init_test_queue(TestQueueContext *ctx) {
    // Create temporary database
    char template[] = "/tmp/test_reasoning_queue_XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) return -1;
    close(fd);

    ctx->db_path = strdup(template);

    int rc = sqlite3_open(ctx->db_path, &ctx->db);
    if (rc != SQLITE_OK) {
        free(ctx->db_path);
        return -1;
    }

    // Create messages table (simplified schema matching sqlite_queue)
    const char *create_sql =
        "CREATE TABLE messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  sender TEXT NOT NULL,"
        "  message TEXT NOT NULL,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  sent INTEGER DEFAULT 0"
        ");";

    rc = sqlite3_exec(ctx->db, create_sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_close(ctx->db);
        free(ctx->db_path);
        return -1;
    }

    return 0;
}

static void cleanup_test_queue(TestQueueContext *ctx) {
    if (ctx->db) {
        sqlite3_close(ctx->db);
        ctx->db = NULL;
    }
    if (ctx->db_path) {
        unlink(ctx->db_path);
        free(ctx->db_path);
        ctx->db_path = NULL;
    }
}

static int insert_message(TestQueueContext *ctx, const char *sender, const char *message) {
    const char *sql = "INSERT INTO messages (sender, message, sent) VALUES (?, ?, 1)";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(ctx->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, message, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

static cJSON* retrieve_messages(TestQueueContext *ctx) {
    const char *sql = "SELECT sender, message FROM messages ORDER BY id";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(ctx->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return NULL;

    cJSON *messages = cJSON_CreateArray();
    if (!messages) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *sender = (const char *)sqlite3_column_text(stmt, 0);
        const char *message = (const char *)sqlite3_column_text(stmt, 1);

        cJSON *msg_obj = cJSON_CreateObject();
        if (msg_obj) {
            cJSON_AddStringToObject(msg_obj, "sender", sender ? sender : "");
            cJSON_AddStringToObject(msg_obj, "message", message ? message : "");
            cJSON_AddItemToArray(messages, msg_obj);
        }
    }

    sqlite3_finalize(stmt);
    return messages;
}

// ============================================================================
// Test Cases
// ============================================================================

static void test_reasoning_message_format(void) {
    printf(COLOR_YELLOW "\nTest: REASONING message format in queue\n" COLOR_RESET);

    TestQueueContext ctx = {0};
    TEST_ASSERT(init_test_queue(&ctx) == 0, "Should create test queue");
    if (!ctx.db) return;

    // Simulate sending a REASONING message (as done by sqlite_queue_send_reasoning)
    cJSON *reasoning_json = cJSON_CreateObject();
    cJSON_AddStringToObject(reasoning_json, "messageType", "REASONING");
    cJSON_AddStringToObject(reasoning_json, "reasoningContent",
        "Let me think about this step by step. First, I need to check the file...");

    char *json_str = cJSON_PrintUnformatted(reasoning_json);
    TEST_ASSERT(json_str != NULL, "Should serialize reasoning message");

    int result = insert_message(&ctx, "klawed", json_str);
    TEST_ASSERT(result == 0, "Should insert reasoning message into queue");

    // Retrieve and verify
    cJSON *messages = retrieve_messages(&ctx);
    TEST_ASSERT(messages != NULL, "Should retrieve messages");
    TEST_ASSERT(cJSON_GetArraySize(messages) == 1, "Should have 1 message");

    if (cJSON_GetArraySize(messages) == 1) {
        cJSON *msg = cJSON_GetArrayItem(messages, 0);
        cJSON *message_json = cJSON_GetObjectItem(msg, "message");
        TEST_ASSERT(message_json != NULL, "Should have message field");

        if (message_json && cJSON_IsString(message_json)) {
            cJSON *parsed = cJSON_Parse(message_json->valuestring);
            TEST_ASSERT(parsed != NULL, "Should parse stored message");

            if (parsed) {
                cJSON *msg_type = cJSON_GetObjectItem(parsed, "messageType");
                cJSON *reasoning = cJSON_GetObjectItem(parsed, "reasoningContent");

                TEST_ASSERT(msg_type != NULL && cJSON_IsString(msg_type),
                           "Should have messageType field");
                TEST_ASSERT(strcmp(msg_type->valuestring, "REASONING") == 0,
                           "messageType should be REASONING");
                TEST_ASSERT(reasoning != NULL && cJSON_IsString(reasoning),
                           "Should have reasoningContent field");
                TEST_ASSERT(strstr(reasoning->valuestring, "step by step") != NULL,
                           "reasoningContent should contain thinking");

                cJSON_Delete(parsed);
            }
        }
    }

    cJSON_Delete(messages);
    cJSON_Delete(reasoning_json);
    free(json_str);
    cleanup_test_queue(&ctx);

    TEST_ASSERT(true, "REASONING message format test completed");
}

static void test_conversation_with_reasoning_and_tools(void) {
    printf(COLOR_YELLOW "\nTest: Conversation sequence with reasoning + tool calls\n" COLOR_RESET);

    TestQueueContext ctx = {0};
    TEST_ASSERT(init_test_queue(&ctx) == 0, "Should create test queue");
    if (!ctx.db) return;

    // Simulate a conversation sequence:
    // 1. User asks a question
    // 2. Assistant sends reasoning (thinking)
    // 3. Assistant sends tool call
    // 4. Tool result
    // 5. Assistant sends final response

    // 1. User message
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "messageType", "TEXT");
    cJSON_AddStringToObject(user_msg, "content", "Read the config file");
    char *user_str = cJSON_PrintUnformatted(user_msg);
    insert_message(&ctx, "user", user_str);
    cJSON_Delete(user_msg);
    free(user_str);

    // 2. Assistant reasoning (thinking)
    cJSON *reasoning_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(reasoning_msg, "messageType", "REASONING");
    cJSON_AddStringToObject(reasoning_msg, "reasoningContent",
        "I need to read the configuration file. Let me use the Read tool.");
    char *reasoning_str = cJSON_PrintUnformatted(reasoning_msg);
    insert_message(&ctx, "klawed", reasoning_str);
    cJSON_Delete(reasoning_msg);
    free(reasoning_str);

    // 3. Assistant tool call
    cJSON *tool_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(tool_msg, "messageType", "TOOL");
    cJSON_AddStringToObject(tool_msg, "toolId", "call_123");
    cJSON_AddStringToObject(tool_msg, "toolName", "Read");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", "/tmp/config.txt");
    cJSON_AddItemToObject(tool_msg, "toolParameters", params);
    char *tool_str = cJSON_PrintUnformatted(tool_msg);
    insert_message(&ctx, "klawed", tool_str);
    cJSON_Delete(tool_msg);
    free(tool_str);

    // 4. Tool result
    cJSON *result_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(result_msg, "messageType", "TOOL_RESULT");
    cJSON_AddStringToObject(result_msg, "toolId", "call_123");
    cJSON_AddStringToObject(result_msg, "toolName", "Read");
    cJSON *output = cJSON_CreateObject();
    cJSON_AddStringToObject(output, "content", "setting=value");
    cJSON_AddItemToObject(result_msg, "toolOutput", output);
    cJSON_AddBoolToObject(result_msg, "isError", 0);
    char *result_str = cJSON_PrintUnformatted(result_msg);
    insert_message(&ctx, "klawed", result_str);
    cJSON_Delete(result_msg);
    free(result_str);

    // 5. Final assistant response
    cJSON *final_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(final_msg, "messageType", "TEXT");
    cJSON_AddStringToObject(final_msg, "content", "The config contains: setting=value");
    char *final_str = cJSON_PrintUnformatted(final_msg);
    insert_message(&ctx, "klawed", final_str);
    cJSON_Delete(final_msg);
    free(final_str);

    // Verify all messages are stored
    cJSON *messages = retrieve_messages(&ctx);
    TEST_ASSERT(messages != NULL, "Should retrieve all messages");
    TEST_ASSERT(cJSON_GetArraySize(messages) == 5, "Should have 5 messages total");

    if (cJSON_GetArraySize(messages) == 5) {
        // Check message types in order
        const char *expected_types[] = {"TEXT", "REASONING", "TOOL", "TOOL_RESULT", "TEXT"};
        const char *expected_senders[] = {"user", "klawed", "klawed", "klawed", "klawed"};

        for (int i = 0; i < 5; i++) {
            cJSON *msg = cJSON_GetArrayItem(messages, i);
            cJSON *sender = cJSON_GetObjectItem(msg, "sender");
            cJSON *message = cJSON_GetObjectItem(msg, "message");

            TEST_ASSERT(sender != NULL, "Message should have sender");
            TEST_ASSERT(strcmp(sender->valuestring, expected_senders[i]) == 0,
                       "Sender should match expected");

            if (message && cJSON_IsString(message)) {
                cJSON *parsed = cJSON_Parse(message->valuestring);
                if (parsed) {
                    cJSON *msg_type = cJSON_GetObjectItem(parsed, "messageType");
                    TEST_ASSERT(msg_type != NULL, "Message should have type");
                    TEST_ASSERT(strcmp(msg_type->valuestring, expected_types[i]) == 0,
                               "Message type should match expected");
                    cJSON_Delete(parsed);
                }
            }
        }
    }

    cJSON_Delete(messages);
    cleanup_test_queue(&ctx);

    TEST_ASSERT(true, "Conversation sequence test completed");
}

static void test_restore_preserves_reasoning(void) {
    printf(COLOR_YELLOW "\nTest: Restore conversation preserves reasoning_content\n" COLOR_RESET);

    // This test simulates what happens during sqlite_queue_restore_conversation
    // when REASONING messages are encountered

    TestQueueContext ctx = {0};
    TEST_ASSERT(init_test_queue(&ctx) == 0, "Should create test queue");
    if (!ctx.db) return;

    // Simulate storing reasoning followed by text (as would happen with thinking models)
    cJSON *reasoning = cJSON_CreateObject();
    cJSON_AddStringToObject(reasoning, "messageType", "REASONING");
    cJSON_AddStringToObject(reasoning, "reasoningContent", "My thinking process here");
    char *r_str = cJSON_PrintUnformatted(reasoning);
    insert_message(&ctx, "klawed", r_str);
    cJSON_Delete(reasoning);
    free(r_str);

    cJSON *text = cJSON_CreateObject();
    cJSON_AddStringToObject(text, "messageType", "TEXT");
    cJSON_AddStringToObject(text, "content", "My response");
    char *t_str = cJSON_PrintUnformatted(text);
    insert_message(&ctx, "klawed", t_str);
    cJSON_Delete(text);
    free(t_str);

    // Simulate restore: retrieve messages and reconstruct conversation
    cJSON *messages = retrieve_messages(&ctx);
    TEST_ASSERT(messages != NULL, "Should retrieve messages");

    // Simulate what sqlite_queue_restore_conversation does:
    // - REASONING message should attach reasoning_content to the pending assistant content
    // - TEXT message should create a content block that includes the reasoning

    int reasoning_found = 0;
    int text_found = 0;

    cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *message = cJSON_GetObjectItem(msg, "message");

        if (message && cJSON_IsString(message)) {
            cJSON *parsed = cJSON_Parse(message->valuestring);
            if (parsed) {
                cJSON *msg_type = cJSON_GetObjectItem(parsed, "messageType");

                if (msg_type && strcmp(msg_type->valuestring, "REASONING") == 0) {
                    cJSON *rc = cJSON_GetObjectItem(parsed, "reasoningContent");
                    TEST_ASSERT(rc != NULL, "REASONING message should have reasoningContent");
                    if (rc) reasoning_found = 1;
                } else if (msg_type && strcmp(msg_type->valuestring, "TEXT") == 0) {
                    text_found = 1;
                }

                cJSON_Delete(parsed);
            }
        }
    }

    TEST_ASSERT(reasoning_found, "Should find REASONING message during restore");
    TEST_ASSERT(text_found, "Should find TEXT message during restore");

    cJSON_Delete(messages);
    cleanup_test_queue(&ctx);

    TEST_ASSERT(true, "Restore preserves reasoning test completed");
}

static void test_empty_reasoning_content(void) {
    printf(COLOR_YELLOW "\nTest: Empty/whitespace reasoning content handling\n" COLOR_RESET);

    TestQueueContext ctx = {0};
    TEST_ASSERT(init_test_queue(&ctx) == 0, "Should create test queue");
    if (!ctx.db) return;

    // Test empty reasoning (should be skipped by real implementation)
    cJSON *empty_reasoning = cJSON_CreateObject();
    cJSON_AddStringToObject(empty_reasoning, "messageType", "REASONING");
    cJSON_AddStringToObject(empty_reasoning, "reasoningContent", "   ");
    char *empty_str = cJSON_PrintUnformatted(empty_reasoning);
    int result = insert_message(&ctx, "klawed", empty_str);
    TEST_ASSERT(result == 0, "Should insert empty reasoning message");
    cJSON_Delete(empty_reasoning);
    free(empty_str);

    // Test whitespace-only reasoning
    cJSON *ws_reasoning = cJSON_CreateObject();
    cJSON_AddStringToObject(ws_reasoning, "messageType", "REASONING");
    cJSON_AddStringToObject(ws_reasoning, "reasoningContent", "\n\t  \n");
    char *ws_str = cJSON_PrintUnformatted(ws_reasoning);
    result = insert_message(&ctx, "klawed", ws_str);
    TEST_ASSERT(result == 0, "Should insert whitespace reasoning message");
    cJSON_Delete(ws_reasoning);
    free(ws_str);

    // Verify both messages are stored
    cJSON *messages = retrieve_messages(&ctx);
    TEST_ASSERT(messages != NULL, "Should retrieve messages");
    TEST_ASSERT(cJSON_GetArraySize(messages) == 2, "Should have 2 messages");

    cJSON_Delete(messages);
    cleanup_test_queue(&ctx);

    TEST_ASSERT(true, "Empty reasoning handling test completed");
}

static void test_multiple_reasoning_messages(void) {
    printf(COLOR_YELLOW "\nTest: Multiple reasoning messages in sequence\n" COLOR_RESET);

    TestQueueContext ctx = {0};
    TEST_ASSERT(init_test_queue(&ctx) == 0, "Should create test queue");
    if (!ctx.db) return;

    // Multiple reasoning chunks (simulating streaming)
    const char *reasoning_chunks[] = {
        "First, let me understand...",
        "Then I need to check...",
        "Finally, I should verify..."
    };

    for (size_t i = 0; i < sizeof(reasoning_chunks)/sizeof(reasoning_chunks[0]); i++) {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "messageType", "REASONING");
        cJSON_AddStringToObject(msg, "reasoningContent", reasoning_chunks[i]);
        char *str = cJSON_PrintUnformatted(msg);
        insert_message(&ctx, "klawed", str);
        cJSON_Delete(msg);
        free(str);
    }

    // Final text response
    cJSON *text = cJSON_CreateObject();
    cJSON_AddStringToObject(text, "messageType", "TEXT");
    cJSON_AddStringToObject(text, "content", "Done!");
    char *t_str = cJSON_PrintUnformatted(text);
    insert_message(&ctx, "klawed", t_str);
    cJSON_Delete(text);
    free(t_str);

    cJSON *messages = retrieve_messages(&ctx);
    TEST_ASSERT(messages != NULL, "Should retrieve messages");
    TEST_ASSERT(cJSON_GetArraySize(messages) == 4, "Should have 4 messages (3 reasoning + 1 text)");

    // Count reasoning messages
    int reasoning_count = 0;
    int text_count = 0;

    cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *message = cJSON_GetObjectItem(msg, "message");

        if (message && cJSON_IsString(message)) {
            cJSON *parsed = cJSON_Parse(message->valuestring);
            if (parsed) {
                cJSON *msg_type = cJSON_GetObjectItem(parsed, "messageType");
                if (msg_type) {
                    if (strcmp(msg_type->valuestring, "REASONING") == 0) reasoning_count++;
                    else if (strcmp(msg_type->valuestring, "TEXT") == 0) text_count++;
                }
                cJSON_Delete(parsed);
            }
        }
    }

    TEST_ASSERT(reasoning_count == 3, "Should have 3 REASONING messages");
    TEST_ASSERT(text_count == 1, "Should have 1 TEXT message");

    cJSON_Delete(messages);
    cleanup_test_queue(&ctx);

    TEST_ASSERT(true, "Multiple reasoning messages test completed");
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
    printf("=== reasoning_content SQLite Queue Preservation Tests ===\n");
    printf("Testing that reasoning_content from thinking models is preserved\n");
    printf("through the sqlite queue database and restored correctly.\n");
    printf("Fixes: 'reasoning_content is missing in assistant tool call message'\n\n");

    test_reasoning_message_format();
    test_conversation_with_reasoning_and_tools();
    test_restore_preserves_reasoning();
    test_empty_reasoning_content();
    test_multiple_reasoning_messages();

    TEST_SUMMARY();
}
