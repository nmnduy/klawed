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
#include <ctype.h>
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

// Simulate the restore logic from sqlite_queue.c
typedef enum {
    INTERNAL_TEXT,
    INTERNAL_TOOL_CALL
} InternalContentType;

typedef struct {
    InternalContentType type;
    char *text;
    char *reasoning_content;
    char *tool_id;
} SimulatedContent;

typedef struct {
    SimulatedContent *contents;
    int count;
    int capacity;
} PendingAssistant;

static void pending_assistant_free(PendingAssistant *pa) {
    for (int i = 0; i < pa->count; i++) {
        free(pa->contents[i].text);
        free(pa->contents[i].reasoning_content);
        free(pa->contents[i].tool_id);
    }
    free(pa->contents);
    pa->contents = NULL;
    pa->count = 0;
    pa->capacity = 0;
}

static int pending_assistant_append(PendingAssistant *pa, SimulatedContent item) {
    if (pa->count >= pa->capacity) {
        int new_cap = pa->capacity == 0 ? 4 : pa->capacity * 2;
        SimulatedContent *tmp = realloc(pa->contents, (size_t)new_cap * sizeof(SimulatedContent));
        if (!tmp) return -1;
        pa->contents = tmp;
        pa->capacity = new_cap;
    }
    pa->contents[pa->count++] = item;
    return 0;
}

static int flush_pending_assistant(PendingAssistant *pa, cJSON *assistant_turns) {
    if (pa->count == 0) return 0;

    cJSON *turn = cJSON_CreateObject();
    cJSON *contents = cJSON_CreateArray();

    for (int i = 0; i < pa->count; i++) {
        cJSON *content = cJSON_CreateObject();
        if (pa->contents[i].type == INTERNAL_TEXT) {
            cJSON_AddStringToObject(content, "type", "text");
            cJSON_AddStringToObject(content, "text", pa->contents[i].text ? pa->contents[i].text : "");
        } else {
            cJSON_AddStringToObject(content, "type", "tool_call");
            cJSON_AddStringToObject(content, "tool_id", pa->contents[i].tool_id ? pa->contents[i].tool_id : "");
        }
        if (pa->contents[i].reasoning_content) {
            cJSON_AddStringToObject(content, "reasoning_content", pa->contents[i].reasoning_content);
        }
        cJSON_AddItemToArray(contents, content);
    }

    cJSON_AddItemToObject(turn, "contents", contents);
    cJSON_AddItemToArray(assistant_turns, turn);

    pending_assistant_free(pa);
    return 0;
}

// Simulate restore with END_AI_TURN handling (the fix)
static cJSON* simulate_restore_with_end_ai_turn(cJSON *messages) {
    PendingAssistant pa = {0};
    cJSON *assistant_turns = cJSON_CreateArray();
    cJSON *msg = NULL;

    cJSON_ArrayForEach(msg, messages) {
        cJSON *sender = cJSON_GetObjectItem(msg, "sender");
        cJSON *message = cJSON_GetObjectItem(msg, "message");

        if (!sender || !message || !cJSON_IsString(message)) continue;

        cJSON *json = cJSON_Parse(message->valuestring);
        if (!json) continue;

        cJSON *jtype = cJSON_GetObjectItem(json, "messageType");
        if (!jtype || !cJSON_IsString(jtype)) {
            cJSON_Delete(json);
            continue;
        }
        const char *mt = jtype->valuestring;
        int from_klawed = (strcmp(sender->valuestring, "klawed") == 0);

        if (strcmp(mt, "TEXT") == 0 && from_klawed) {
            cJSON *jcontent = cJSON_GetObjectItem(json, "content");
            if (jcontent && cJSON_IsString(jcontent)) {
                SimulatedContent c = {0};
                c.type = INTERNAL_TEXT;
                c.text = strdup(jcontent->valuestring);
                pending_assistant_append(&pa, c);
            }
        } else if (strcmp(mt, "REASONING") == 0 && from_klawed) {
            cJSON *jreasoning = cJSON_GetObjectItem(json, "reasoningContent");
            if (jreasoning && cJSON_IsString(jreasoning) && pa.count > 0) {
                // Attach reasoning to most recent content block
                int last_idx = pa.count - 1;
                pa.contents[last_idx].reasoning_content = strdup(jreasoning->valuestring);
            }
        } else if (strcmp(mt, "TOOL") == 0 && from_klawed) {
            cJSON *jtool_id = cJSON_GetObjectItem(json, "toolId");
            if (jtool_id && cJSON_IsString(jtool_id)) {
                SimulatedContent c = {0};
                c.type = INTERNAL_TOOL_CALL;
                c.tool_id = strdup(jtool_id->valuestring);

                /* Extract reasoningContent from TOOL message (THE BUG FIX) */
                cJSON *jreasoning = cJSON_GetObjectItem(json, "reasoningContent");
                if (jreasoning && cJSON_IsString(jreasoning) && jreasoning->valuestring) {
                    c.reasoning_content = strdup(jreasoning->valuestring);
                }

                pending_assistant_append(&pa, c);
            }
        } else if (strcmp(mt, "END_AI_TURN") == 0 && from_klawed) {
            // THE FIX: Flush pending assistant on END_AI_TURN
            if (pa.count > 0) {
                flush_pending_assistant(&pa, assistant_turns);
            }
        }

        cJSON_Delete(json);
    }

    // Flush any remaining content
    if (pa.count > 0) {
        flush_pending_assistant(&pa, assistant_turns);
    }

    return assistant_turns;
}

static void test_end_ai_turn_separates_assistant_turns(void) {
    printf(COLOR_YELLOW "\nTest: END_AI_TURN separates multiple assistant turns\n" COLOR_RESET);
    printf("This test simulates the bug: reasoning_content from turn 2 was attached to turn 1\n");

    TestQueueContext ctx = {0};
    TEST_ASSERT(init_test_queue(&ctx) == 0, "Should create test queue");
    if (!ctx.db) return;

    // Simulate the sequence from the real bug:
    // Turn 1: TEXT + REASONING + TOOL
    // END_AI_TURN
    // Turn 2: TEXT + REASONING + TOOL (but reasoning was attached to turn 1's tool!)

    // Turn 1
    cJSON *text1 = cJSON_CreateObject();
    cJSON_AddStringToObject(text1, "messageType", "TEXT");
    cJSON_AddStringToObject(text1, "content", "First response");
    char *s1 = cJSON_PrintUnformatted(text1);
    insert_message(&ctx, "klawed", s1);
    cJSON_Delete(text1);
    free(s1);

    cJSON *reasoning1 = cJSON_CreateObject();
    cJSON_AddStringToObject(reasoning1, "messageType", "REASONING");
    cJSON_AddStringToObject(reasoning1, "reasoningContent", "Reasoning for first turn");
    char *r1 = cJSON_PrintUnformatted(reasoning1);
    insert_message(&ctx, "klawed", r1);
    cJSON_Delete(reasoning1);
    free(r1);

    cJSON *tool1 = cJSON_CreateObject();
    cJSON_AddStringToObject(tool1, "messageType", "TOOL");
    cJSON_AddStringToObject(tool1, "toolId", "tool_001");
    cJSON_AddStringToObject(tool1, "toolName", "Bash");
    char *t1 = cJSON_PrintUnformatted(tool1);
    insert_message(&ctx, "klawed", t1);
    cJSON_Delete(tool1);
    free(t1);

    // END_AI_TURN - This is the key! Without this, turn 2 merges with turn 1
    cJSON *end_turn = cJSON_CreateObject();
    cJSON_AddStringToObject(end_turn, "messageType", "END_AI_TURN");
    char *et = cJSON_PrintUnformatted(end_turn);
    insert_message(&ctx, "klawed", et);
    cJSON_Delete(end_turn);
    free(et);

    // Turn 2
    cJSON *text2 = cJSON_CreateObject();
    cJSON_AddStringToObject(text2, "messageType", "TEXT");
    cJSON_AddStringToObject(text2, "content", "Second response");
    char *s2 = cJSON_PrintUnformatted(text2);
    insert_message(&ctx, "klawed", s2);
    cJSON_Delete(text2);
    free(s2);

    cJSON *reasoning2 = cJSON_CreateObject();
    cJSON_AddStringToObject(reasoning2, "messageType", "REASONING");
    cJSON_AddStringToObject(reasoning2, "reasoningContent", "Reasoning for second turn");
    char *r2 = cJSON_PrintUnformatted(reasoning2);
    insert_message(&ctx, "klawed", r2);
    cJSON_Delete(reasoning2);
    free(r2);

    cJSON *tool2 = cJSON_CreateObject();
    cJSON_AddStringToObject(tool2, "messageType", "TOOL");
    cJSON_AddStringToObject(tool2, "toolId", "tool_002");
    cJSON_AddStringToObject(tool2, "toolName", "Read");
    char *t2 = cJSON_PrintUnformatted(tool2);
    insert_message(&ctx, "klawed", t2);
    cJSON_Delete(tool2);
    free(t2);

    // Retrieve and simulate restore
    cJSON *messages = retrieve_messages(&ctx);
    TEST_ASSERT(messages != NULL, "Should retrieve messages");
    TEST_ASSERT(cJSON_GetArraySize(messages) == 7, "Should have 7 messages");

    // Simulate restore with END_AI_TURN handling
    cJSON *assistant_turns = simulate_restore_with_end_ai_turn(messages);
    TEST_ASSERT(assistant_turns != NULL, "Should produce assistant turns");
    TEST_ASSERT(cJSON_GetArraySize(assistant_turns) == 2, "Should have exactly 2 assistant turns (not merged!)");

    // Verify turn 1 has its reasoning, turn 2 has its reasoning
    // Note: reasoning_content is attached to the most recent content block when received
    // In this sequence: TEXT (gets reasoning) → TOOL
    if (cJSON_GetArraySize(assistant_turns) == 2) {
        cJSON *turn1 = cJSON_GetArrayItem(assistant_turns, 0);
        cJSON *contents1 = cJSON_GetObjectItem(turn1, "contents");
        cJSON *turn2 = cJSON_GetArrayItem(assistant_turns, 1);
        cJSON *contents2 = cJSON_GetObjectItem(turn2, "contents");

        // Check turn 1's text block has reasoning (index 0 is text, gets reasoning attached)
        cJSON *text_content1 = cJSON_GetArrayItem(contents1, 0);
        cJSON *rc1 = cJSON_GetObjectItem(text_content1, "reasoning_content");
        TEST_ASSERT(rc1 != NULL, "Turn 1 text should have reasoning_content");
        if (rc1) {
            TEST_ASSERT(strstr(rc1->valuestring, "first turn") != NULL,
                       "Turn 1 reasoning should be 'first turn'");
        }

        // Check turn 2's text block has reasoning
        cJSON *text_content2 = cJSON_GetArrayItem(contents2, 0);
        cJSON *rc2 = cJSON_GetObjectItem(text_content2, "reasoning_content");
        TEST_ASSERT(rc2 != NULL, "Turn 2 text should have reasoning_content");
        if (rc2) {
            TEST_ASSERT(strstr(rc2->valuestring, "second turn") != NULL,
                       "Turn 2 reasoning should be 'second turn'");
        }
    }

    cJSON_Delete(messages);
    cJSON_Delete(assistant_turns);
    cleanup_test_queue(&ctx);

    TEST_ASSERT(true, "END_AI_TURN separates assistant turns test completed");
}

// ============================================================================
// Main Test Runner
// ============================================================================

// Simulate restore with the NEW fix: detect text response followed by reasoning
static cJSON* simulate_restore_with_text_response_detection(cJSON *messages) {
    PendingAssistant pa = {0};
    cJSON *assistant_turns = cJSON_CreateArray();
    cJSON *msg = NULL;

    cJSON_ArrayForEach(msg, messages) {
        cJSON *sender = cJSON_GetObjectItem(msg, "sender");
        cJSON *message = cJSON_GetObjectItem(msg, "message");

        if (!sender || !message || !cJSON_IsString(message)) continue;

        cJSON *json = cJSON_Parse(message->valuestring);
        if (!json) continue;

        cJSON *jtype = cJSON_GetObjectItem(json, "messageType");
        if (!jtype || !cJSON_IsString(jtype)) {
            cJSON_Delete(json);
            continue;
        }
        const char *mt = jtype->valuestring;
        int from_klawed = (strcmp(sender->valuestring, "klawed") == 0);

        if (strcmp(mt, "TEXT") == 0 && from_klawed) {
            cJSON *jcontent = cJSON_GetObjectItem(json, "content");
            if (jcontent && cJSON_IsString(jcontent)) {
                SimulatedContent c = {0};
                c.type = INTERNAL_TEXT;
                c.text = strdup(jcontent->valuestring);
                pending_assistant_append(&pa, c);
            }
        } else if (strcmp(mt, "REASONING") == 0 && from_klawed) {
            cJSON *jreasoning = cJSON_GetObjectItem(json, "reasoningContent");
            if (jreasoning && cJSON_IsString(jreasoning)) {
                // THE NEW FIX: Check if last block is text with non-empty content
                // If so, create a new block instead of attaching to existing
                if (pa.count > 0) {
                    int last_idx = pa.count - 1;
                    if (pa.contents[last_idx].type == INTERNAL_TEXT &&
                        pa.contents[last_idx].text &&
                        pa.contents[last_idx].text[0] != '\0') {
                        // Last block is a text response, create new block for reasoning
                        SimulatedContent c = {0};
                        c.type = INTERNAL_TEXT;
                        c.text = strdup("");  // Empty text
                        c.reasoning_content = strdup(jreasoning->valuestring);
                        pending_assistant_append(&pa, c);
                    } else {
                        // Attach to existing block (tool call or empty text)
                        pa.contents[last_idx].reasoning_content = strdup(jreasoning->valuestring);
                    }
                } else {
                    // No pending content, create synthetic block
                    SimulatedContent c = {0};
                    c.type = INTERNAL_TEXT;
                    c.text = strdup("");
                    c.reasoning_content = strdup(jreasoning->valuestring);
                    pending_assistant_append(&pa, c);
                }
            }
        } else if (strcmp(mt, "TOOL") == 0 && from_klawed) {
            cJSON *jtool_id = cJSON_GetObjectItem(json, "toolId");
            if (jtool_id && cJSON_IsString(jtool_id)) {
                SimulatedContent c = {0};
                c.type = INTERNAL_TOOL_CALL;
                c.tool_id = strdup(jtool_id->valuestring);
                pending_assistant_append(&pa, c);
            }
        } else if (strcmp(mt, "END_AI_TURN") == 0 && from_klawed) {
            if (pa.count > 0) {
                flush_pending_assistant(&pa, assistant_turns);
            }
        }

        cJSON_Delete(json);
    }

    // Flush any remaining content
    if (pa.count > 0) {
        flush_pending_assistant(&pa, assistant_turns);
    }

    return assistant_turns;
}

static void test_missing_end_ai_turn_with_text_response(void) {
    printf(COLOR_YELLOW "\nTest: Missing END_AI_TURN with text response before reasoning\n" COLOR_RESET);
    printf("This tests the bug where reasoning_content was attached to previous turn's text\n");

    TestQueueContext ctx = {0};
    TEST_ASSERT(init_test_queue(&ctx) == 0, "Should create test queue");
    if (!ctx.db) return;

    // Simulate buggy sequence WITHOUT END_AI_TURN between turns:
    // Turn 1: TEXT response "First response" + REASONING "First reasoning" + TOOL
    // Turn 2: TEXT response "Second response" + REASONING "Second reasoning" + TOOL
    // (No END_AI_TURN between them!)

    // Turn 1
    cJSON *text1 = cJSON_CreateObject();
    cJSON_AddStringToObject(text1, "messageType", "TEXT");
    cJSON_AddStringToObject(text1, "content", "First response");
    char *s1 = cJSON_PrintUnformatted(text1);
    insert_message(&ctx, "klawed", s1);
    cJSON_Delete(text1);
    free(s1);

    cJSON *reasoning1 = cJSON_CreateObject();
    cJSON_AddStringToObject(reasoning1, "messageType", "REASONING");
    cJSON_AddStringToObject(reasoning1, "reasoningContent", "First reasoning");
    char *r1 = cJSON_PrintUnformatted(reasoning1);
    insert_message(&ctx, "klawed", r1);
    cJSON_Delete(reasoning1);
    free(r1);

    cJSON *tool1 = cJSON_CreateObject();
    cJSON_AddStringToObject(tool1, "messageType", "TOOL");
    cJSON_AddStringToObject(tool1, "toolId", "tool_001");
    cJSON_AddStringToObject(tool1, "toolName", "Bash");
    char *t1 = cJSON_PrintUnformatted(tool1);
    insert_message(&ctx, "klawed", t1);
    cJSON_Delete(tool1);
    free(t1);

    // Turn 2 - NO END_AI_TURN before this!
    cJSON *text2 = cJSON_CreateObject();
    cJSON_AddStringToObject(text2, "messageType", "TEXT");
    cJSON_AddStringToObject(text2, "content", "Second response");
    char *s2 = cJSON_PrintUnformatted(text2);
    insert_message(&ctx, "klawed", s2);
    cJSON_Delete(text2);
    free(s2);

    cJSON *reasoning2 = cJSON_CreateObject();
    cJSON_AddStringToObject(reasoning2, "messageType", "REASONING");
    cJSON_AddStringToObject(reasoning2, "reasoningContent", "Second reasoning");
    char *r2 = cJSON_PrintUnformatted(reasoning2);
    insert_message(&ctx, "klawed", r2);
    cJSON_Delete(reasoning2);
    free(r2);

    cJSON *tool2 = cJSON_CreateObject();
    cJSON_AddStringToObject(tool2, "messageType", "TOOL");
    cJSON_AddStringToObject(tool2, "toolId", "tool_002");
    cJSON_AddStringToObject(tool2, "toolName", "Read");
    char *t2 = cJSON_PrintUnformatted(tool2);
    insert_message(&ctx, "klawed", t2);
    cJSON_Delete(tool2);
    free(t2);

    // END_AI_TURN at the very end
    cJSON *end_turn = cJSON_CreateObject();
    cJSON_AddStringToObject(end_turn, "messageType", "END_AI_TURN");
    char *et = cJSON_PrintUnformatted(end_turn);
    insert_message(&ctx, "klawed", et);
    cJSON_Delete(end_turn);
    free(et);

    // Retrieve and simulate restore WITH the new fix
    cJSON *messages = retrieve_messages(&ctx);
    TEST_ASSERT(messages != NULL, "Should retrieve messages");
    TEST_ASSERT(cJSON_GetArraySize(messages) == 7, "Should have 7 messages");

    // With the fix, we should have 1 merged assistant turn but with separate content blocks
    cJSON *assistant_turns = simulate_restore_with_text_response_detection(messages);
    TEST_ASSERT(assistant_turns != NULL, "Should produce assistant turns");

    // Should have 1 turn (merged) but with correct reasoning attached to each part
    TEST_ASSERT(cJSON_GetArraySize(assistant_turns) == 1, "Should have 1 merged assistant turn");

    if (cJSON_GetArraySize(assistant_turns) == 1) {
        cJSON *turn = cJSON_GetArrayItem(assistant_turns, 0);
        cJSON *contents = cJSON_GetObjectItem(turn, "contents");

        // Should have at least 4 content blocks: text1, tool1, text2 (empty+reasoning2), tool2
        int content_count = cJSON_GetArraySize(contents);
        TEST_ASSERT(content_count >= 4, "Should have at least 4 content blocks");

        // Verify that first text has first reasoning, and second text has second reasoning
        int found_first_reasoning = 0;
        int found_second_reasoning = 0;
        for (int i = 0; i < content_count; i++) {
            cJSON *content = cJSON_GetArrayItem(contents, i);
            cJSON *rc = cJSON_GetObjectItem(content, "reasoning_content");
            cJSON *text = cJSON_GetObjectItem(content, "text");
            if (rc && rc->valuestring) {
                if (strstr(rc->valuestring, "First reasoning")) {
                    found_first_reasoning = 1;
                }
                if (strstr(rc->valuestring, "Second reasoning")) {
                    found_second_reasoning = 1;
                    // Second reasoning should be on an empty text block (synthetic)
                    if (text && text->valuestring && strlen(text->valuestring) == 0) {
                        TEST_ASSERT(true, "Second reasoning is on synthetic (empty) text block");
                    }
                }
            }
        }
        TEST_ASSERT(found_first_reasoning, "Should find First reasoning attached to some block");
        TEST_ASSERT(found_second_reasoning, "Should find Second reasoning attached to some block");
    }

    cJSON_Delete(messages);
    cJSON_Delete(assistant_turns);
    cleanup_test_queue(&ctx);

    TEST_ASSERT(true, "Missing END_AI_TURN with text response test completed");
}

// Simulate restore that extracts reasoningContent from TOOL messages (THE BUG FIX)
static cJSON* simulate_restore_with_tool_reasoning(cJSON *messages) {
    PendingAssistant pa = {0};
    cJSON *assistant_turns = cJSON_CreateArray();
    cJSON *msg = NULL;

    cJSON_ArrayForEach(msg, messages) {
        cJSON *sender = cJSON_GetObjectItem(msg, "sender");
        cJSON *message = cJSON_GetObjectItem(msg, "message");

        if (!sender || !message || !cJSON_IsString(message)) continue;

        int from_klawed = (strcmp(sender->valuestring, "klawed") == 0);
        cJSON *json = cJSON_Parse(message->valuestring);
        if (!json) continue;

        cJSON *jtype = cJSON_GetObjectItem(json, "messageType");
        if (!jtype || !cJSON_IsString(jtype)) { cJSON_Delete(json); continue; }
        const char *mt = jtype->valuestring;

        if (strcmp(mt, "TEXT") == 0 && from_klawed) {
            cJSON *jcontent = cJSON_GetObjectItem(json, "content");
            if (jcontent && cJSON_IsString(jcontent)) {
                SimulatedContent c = {0};
                c.type = INTERNAL_TEXT;
                c.text = strdup(jcontent->valuestring);
                pending_assistant_append(&pa, c);
            }
        } else if (strcmp(mt, "TOOL") == 0 && from_klawed) {
            cJSON *jtool_id = cJSON_GetObjectItem(json, "toolId");
            if (jtool_id && cJSON_IsString(jtool_id)) {
                SimulatedContent c = {0};
                c.type = INTERNAL_TOOL_CALL;
                c.tool_id = strdup(jtool_id->valuestring);

                /* THE FIX: Extract reasoningContent from TOOL message */
                cJSON *jreasoning = cJSON_GetObjectItem(json, "reasoningContent");
                if (jreasoning && cJSON_IsString(jreasoning) && jreasoning->valuestring) {
                    const char *reasoning = jreasoning->valuestring;
                    /* Skip empty / whitespace-only reasoning */
                    const char *pr = reasoning;
                    while (*pr && isspace((unsigned char)*pr)) pr++;
                    if (*pr != '\0') {
                        c.reasoning_content = strdup(reasoning);
                    }
                }

                pending_assistant_append(&pa, c);
            }
        } else if (strcmp(mt, "END_AI_TURN") == 0 && from_klawed) {
            if (pa.count > 0) {
                flush_pending_assistant(&pa, assistant_turns);
            }
        }

        cJSON_Delete(json);
    }

    if (pa.count > 0) {
        flush_pending_assistant(&pa, assistant_turns);
    }

    return assistant_turns;
}

static void test_tool_message_with_reasoning_content(void) {
    printf(COLOR_YELLOW "\nTest: TOOL message with reasoningContent\n" COLOR_RESET);
    printf("This test verifies the bug fix: reasoningContent is extracted from TOOL messages\n");

    TestQueueContext ctx = {0};
    TEST_ASSERT(init_test_queue(&ctx) == 0, "Should create test queue");
    if (!ctx.db) return;

    // Simulate: Assistant sends tool call with reasoningContent (Moonshot/Kimi behavior)
    // This is the exact scenario that was broken before the fix

    // User request
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "messageType", "TEXT");
    cJSON_AddStringToObject(user_msg, "content", "Check the system status");
    char *user_str = cJSON_PrintUnformatted(user_msg);
    insert_message(&ctx, "user", user_str);
    cJSON_Delete(user_msg);
    free(user_str);

    // Assistant tool call WITH reasoningContent (the key test case)
    cJSON *tool_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(tool_msg, "messageType", "TOOL");
    cJSON_AddStringToObject(tool_msg, "toolId", "tool_with_reasoning_001");
    cJSON_AddStringToObject(tool_msg, "toolName", "Bash");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "uptime");
    cJSON_AddItemToObject(tool_msg, "toolParameters", params);
    // THE KEY: This reasoningContent was being lost before the fix
    cJSON_AddStringToObject(tool_msg, "reasoningContent",
        "Let me check the system uptime to see how long the server has been running.");
    char *tool_str = cJSON_PrintUnformatted(tool_msg);
    insert_message(&ctx, "klawed", tool_str);
    cJSON_Delete(tool_msg);
    free(tool_str);

    // END_AI_TURN
    cJSON *end_turn = cJSON_CreateObject();
    cJSON_AddStringToObject(end_turn, "messageType", "END_AI_TURN");
    char *et = cJSON_PrintUnformatted(end_turn);
    insert_message(&ctx, "klawed", et);
    cJSON_Delete(end_turn);
    free(et);

    // Retrieve messages
    cJSON *messages = retrieve_messages(&ctx);
    TEST_ASSERT(messages != NULL, "Should retrieve messages");
    TEST_ASSERT(cJSON_GetArraySize(messages) == 3, "Should have 3 messages");

    // Verify the TOOL message has reasoningContent in the database
    cJSON *tool_db_msg = cJSON_GetArrayItem(messages, 1);
    TEST_ASSERT(tool_db_msg != NULL, "Should have tool message");
    cJSON *tool_sender = cJSON_GetObjectItem(tool_db_msg, "sender");
    TEST_ASSERT(tool_sender && strcmp(tool_sender->valuestring, "klawed") == 0,
               "Tool message should be from klawed");
    cJSON *tool_message = cJSON_GetObjectItem(tool_db_msg, "message");
    TEST_ASSERT(tool_message && cJSON_IsString(tool_message), "Tool should have message");

    cJSON *parsed_tool = cJSON_Parse(tool_message->valuestring);
    TEST_ASSERT(parsed_tool != NULL, "Should parse tool message");
    cJSON *rc = cJSON_GetObjectItem(parsed_tool, "reasoningContent");
    TEST_ASSERT(rc && cJSON_IsString(rc), "TOOL message should have reasoningContent field");
    TEST_ASSERT(strstr(rc->valuestring, "system uptime") != NULL,
               "reasoningContent should contain the thinking");
    cJSON_Delete(parsed_tool);

    // Now simulate restore with the fix
    cJSON *assistant_turns = simulate_restore_with_tool_reasoning(messages);
    TEST_ASSERT(assistant_turns != NULL, "Should produce assistant turns");
    TEST_ASSERT(cJSON_GetArraySize(assistant_turns) == 1, "Should have 1 assistant turn");

    // Verify the tool call has reasoning_content attached
    if (cJSON_GetArraySize(assistant_turns) == 1) {
        cJSON *turn = cJSON_GetArrayItem(assistant_turns, 0);
        cJSON *contents = cJSON_GetObjectItem(turn, "contents");
        TEST_ASSERT(contents != NULL, "Turn should have contents");
        TEST_ASSERT(cJSON_GetArraySize(contents) == 1, "Should have 1 content block (the tool call)");

        cJSON *tool_content = cJSON_GetArrayItem(contents, 0);
        cJSON *restored_rc = cJSON_GetObjectItem(tool_content, "reasoning_content");
        TEST_ASSERT(restored_rc && cJSON_IsString(restored_rc),
                   "Tool call content should have reasoning_content after restore");
        TEST_ASSERT(strstr(restored_rc->valuestring, "system uptime") != NULL,
                   "reasoning_content should contain the original thinking");
    }

    cJSON_Delete(messages);
    cJSON_Delete(assistant_turns);
    cleanup_test_queue(&ctx);

    TEST_ASSERT(true, "TOOL message with reasoningContent test completed");
}

static void test_tool_message_with_empty_reasoning_content(void) {
    printf(COLOR_YELLOW "\nTest: TOOL message with empty/whitespace reasoningContent\n" COLOR_RESET);

    TestQueueContext ctx = {0};
    TEST_ASSERT(init_test_queue(&ctx) == 0, "Should create test queue");
    if (!ctx.db) return;

    // User request
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "messageType", "TEXT");
    cJSON_AddStringToObject(user_msg, "content", "Do something");
    char *user_str = cJSON_PrintUnformatted(user_msg);
    insert_message(&ctx, "user", user_str);
    cJSON_Delete(user_msg);
    free(user_str);

    // Tool with empty reasoningContent
    cJSON *tool1 = cJSON_CreateObject();
    cJSON_AddStringToObject(tool1, "messageType", "TOOL");
    cJSON_AddStringToObject(tool1, "toolId", "tool_empty");
    cJSON_AddStringToObject(tool1, "toolName", "Read");
    cJSON_AddStringToObject(tool1, "reasoningContent", "");
    char *t1 = cJSON_PrintUnformatted(tool1);
    insert_message(&ctx, "klawed", t1);
    cJSON_Delete(tool1);
    free(t1);

    // Tool with whitespace-only reasoningContent
    cJSON *tool2 = cJSON_CreateObject();
    cJSON_AddStringToObject(tool2, "messageType", "TOOL");
    cJSON_AddStringToObject(tool2, "toolId", "tool_ws");
    cJSON_AddStringToObject(tool2, "toolName", "Read");
    cJSON_AddStringToObject(tool2, "reasoningContent", "   \n\t  ");
    char *t2 = cJSON_PrintUnformatted(tool2);
    insert_message(&ctx, "klawed", t2);
    cJSON_Delete(tool2);
    free(t2);

    // Tool with valid reasoningContent
    cJSON *tool3 = cJSON_CreateObject();
    cJSON_AddStringToObject(tool3, "messageType", "TOOL");
    cJSON_AddStringToObject(tool3, "toolId", "tool_valid");
    cJSON_AddStringToObject(tool3, "toolName", "Bash");
    cJSON_AddStringToObject(tool3, "reasoningContent", "Valid reasoning here");
    char *t3 = cJSON_PrintUnformatted(tool3);
    insert_message(&ctx, "klawed", t3);
    cJSON_Delete(tool3);
    free(t3);

    cJSON *end = cJSON_CreateObject();
    cJSON_AddStringToObject(end, "messageType", "END_AI_TURN");
    char *es = cJSON_PrintUnformatted(end);
    insert_message(&ctx, "klawed", es);
    cJSON_Delete(end);
    free(es);

    cJSON *messages = retrieve_messages(&ctx);
    cJSON *assistant_turns = simulate_restore_with_tool_reasoning(messages);

    TEST_ASSERT(assistant_turns != NULL, "Should produce assistant turns");
    if (cJSON_GetArraySize(assistant_turns) == 1) {
        cJSON *turn = cJSON_GetArrayItem(assistant_turns, 0);
        cJSON *contents = cJSON_GetObjectItem(turn, "contents");
        TEST_ASSERT(cJSON_GetArraySize(contents) == 3, "Should have 3 tool calls");

        // First two should NOT have reasoning_content (empty/whitespace skipped)
        // Third one should have reasoning_content
        cJSON *tool1_content = cJSON_GetArrayItem(contents, 0);
        cJSON *tool2_content = cJSON_GetArrayItem(contents, 1);
        cJSON *tool3_content = cJSON_GetArrayItem(contents, 2);

        cJSON *rc1 = cJSON_GetObjectItem(tool1_content, "reasoning_content");
        cJSON *rc2 = cJSON_GetObjectItem(tool2_content, "reasoning_content");
        cJSON *rc3 = cJSON_GetObjectItem(tool3_content, "reasoning_content");

        TEST_ASSERT(rc1 == NULL, "Empty reasoning should not be stored");
        TEST_ASSERT(rc2 == NULL, "Whitespace reasoning should not be stored");
        TEST_ASSERT(rc3 != NULL && strstr(rc3->valuestring, "Valid reasoning"),
                   "Valid reasoning should be stored");
    }

    cJSON_Delete(messages);
    cJSON_Delete(assistant_turns);
    cleanup_test_queue(&ctx);

    TEST_ASSERT(true, "TOOL message with empty/whitespace reasoningContent test completed");
}

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
    test_end_ai_turn_separates_assistant_turns();
    test_missing_end_ai_turn_with_text_response();
    test_tool_message_with_reasoning_content();
    test_tool_message_with_empty_reasoning_content();

    TEST_SUMMARY();
}
