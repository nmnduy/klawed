/*
 * Unit Tests for SQLite Queue Conversation Seeding
 *
 * Tests the sqlite_queue_restore_conversation() function which loads conversation
 * history from the database, handling TEXT, TOOL, and TOOL_RESULT messages with
 * proper pairing and error injection.
 *
 * Compilation: make test-sqlite-queue-seeding
 * Usage: ./build/test_sqlite_queue_seeding
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

// Include internal header to get ConversationState definition
#include "../src/klawed_internal.h"
#include "../src/sqlite_queue.h"

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

// Test database path
#define TEST_DB_PATH "/tmp/test_sqlite_queue_seeding.db"

// Forward declarations
// (conversation_state_init and conversation_state_destroy are already declared in klawed_internal.h)

// Test utilities
static void setup_test_db(void) {
    // Remove any existing test database
    unlink(TEST_DB_PATH);
}

static void cleanup_test_db(void) {
    unlink(TEST_DB_PATH);
}

static void assert_true(const char *test_name, int condition, const char *message) {
    tests_run++;
    if (condition) {
        tests_passed++;
        printf("%s✓%s %s\n", COLOR_GREEN, COLOR_RESET, test_name);
    } else {
        tests_failed++;
        printf("%s✗%s %s: %s\n", COLOR_RED, COLOR_RESET, test_name, message);
    }
}

static void assert_equal_int(const char *test_name, int expected, int actual) {
    tests_run++;
    if (expected == actual) {
        tests_passed++;
        printf("%s✓%s %s\n", COLOR_GREEN, COLOR_RESET, test_name);
    } else {
        tests_failed++;
        printf("%s✗%s %s: expected %d, got %d\n", COLOR_RED, COLOR_RESET, test_name, expected, actual);
    }
}

// Helper to insert a message into the database
static int insert_message(sqlite3 *db, const char *sender, const char *receiver, const char *message_json) {
    const char *sql = "INSERT INTO messages (sender, receiver, message, sent) VALUES (?, ?, ?, 1);";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare insert: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, receiver, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, message_json, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to insert message: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    return 0;
}

// Helper to create TEXT message JSON
static char* create_text_message(const char *content) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "messageType", "TEXT");
    cJSON_AddStringToObject(json, "content", content);
    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return str;
}

// Helper to create TOOL message JSON
static char* create_tool_message(const char *tool_name, const char *tool_id, cJSON *params) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "messageType", "TOOL");
    cJSON_AddStringToObject(json, "toolName", tool_name);
    cJSON_AddStringToObject(json, "toolId", tool_id);
    if (params) {
        cJSON_AddItemToObject(json, "toolParameters", cJSON_Duplicate(params, 1));
    } else {
        cJSON_AddItemToObject(json, "toolParameters", cJSON_CreateObject());
    }
    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return str;
}

// Helper to create TOOL_RESULT message JSON
static char* create_tool_result_message(const char *tool_name, const char *tool_id, const char *output, int is_error) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "messageType", "TOOL_RESULT");
    cJSON_AddStringToObject(json, "toolName", tool_name);
    cJSON_AddStringToObject(json, "toolId", tool_id);

    cJSON *output_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(output_obj, "result", output);
    cJSON_AddItemToObject(json, "toolOutput", output_obj);

    cJSON_AddBoolToObject(json, "isError", is_error);
    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return str;
}

// Test 1: Basic TEXT message seeding
static void test_basic_text_messages(void) {
    setup_test_db();

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "klawed");
    assert(ctx != NULL);

    // Initialize schema
    int rc = sqlite_queue_init_schema(ctx);
    assert(rc == 0);

    // Get direct database handle for inserting test data
    sqlite3 *db = (sqlite3 *)ctx->db_handle;
    assert(db != NULL);

    // Insert user message and assistant response
    char *user_msg = create_text_message("Hello, can you help me?");
    char *asst_msg = create_text_message("Of course! I'd be happy to help.");

    insert_message(db, "client", "klawed", user_msg);
    insert_message(db, "klawed", "client", asst_msg);

    free(user_msg);
    free(asst_msg);

    // Create conversation state and seed
    ConversationState state = {0};
    conversation_state_init(&state);

    int seeded = sqlite_queue_restore_conversation(ctx, &state);

    // Verify results
    assert_true("test_basic_text_messages: seed returned positive count", seeded == 2, "expected 2 messages seeded");
    assert_equal_int("test_basic_text_messages: message count", 2, state.count);

    if (state.count >= 2) {
        assert_true("test_basic_text_messages: first message is user",
                   state.messages[0].role == MSG_USER, "first message should be user");
        assert_true("test_basic_text_messages: second message is assistant",
                   state.messages[1].role == MSG_ASSISTANT, "second message should be assistant");
        assert_true("test_basic_text_messages: first message has text content",
                   state.messages[0].content_count == 1 &&
                   state.messages[0].contents[0].type == INTERNAL_TEXT,
                   "first message should have text content");
    }

    conversation_free(&state);
    conversation_state_destroy(&state);
    sqlite_queue_cleanup(ctx);
    cleanup_test_db();
}

// Test 2: TOOL + TOOL_RESULT pairing
static void test_tool_result_pairing(void) {
    setup_test_db();

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "klawed");
    assert(ctx != NULL);

    sqlite_queue_init_schema(ctx);
    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    // Insert user message
    char *user_msg = create_text_message("Read the file test.txt");
    insert_message(db, "client", "klawed", user_msg);
    free(user_msg);

    // Insert TOOL message (assistant requesting tool execution)
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", "test.txt");
    char *tool_msg = create_tool_message("Read", "call_123", params);
    insert_message(db, "klawed", "client", tool_msg);
    free(tool_msg);
    cJSON_Delete(params);

    // Insert TOOL_RESULT message (klawed sends the result after executing the tool)
    char *result_msg = create_tool_result_message("Read", "call_123", "File contents here", 0);
    insert_message(db, "klawed", "client", result_msg);
    free(result_msg);

    // Restore conversation
    ConversationState state = {0};
    conversation_state_init(&state);
    int seeded = sqlite_queue_restore_conversation(ctx, &state);
    (void)seeded; // Used in checks below
    (void)seeded; // Used in assertions below

    // Verify: should have 3 messages - user TEXT, assistant TOOL, and user TOOL_RESULT
    assert_equal_int("test_tool_result_pairing: message count", 3, state.count);

    if (state.count >= 3) {
        assert_true("test_tool_result_pairing: first is user text",
                   state.messages[0].role == MSG_USER &&
                   state.messages[0].contents[0].type == INTERNAL_TEXT,
                   "first message should be user text");

        assert_true("test_tool_result_pairing: second is assistant tool request",
                   state.messages[1].role == MSG_ASSISTANT &&
                   state.messages[1].contents[0].type == INTERNAL_TOOL_CALL,
                   "second message should be assistant tool request");

        assert_true("test_tool_result_pairing: third is tool result",
                   state.messages[2].role == MSG_USER &&
                   state.messages[2].contents[0].type == INTERNAL_TOOL_RESPONSE,
                   "third message should be tool result");

        assert_true("test_tool_result_pairing: tool result has correct id",
                   strcmp(state.messages[2].contents[0].tool_id, "call_123") == 0,
                   "tool result should have matching id");

        assert_true("test_tool_result_pairing: tool result is not error",
                   state.messages[2].contents[0].is_error == 0,
                   "tool result should not be an error");

        assert_true("test_tool_result_pairing: tool result has output",
                   state.messages[2].contents[0].tool_output != NULL,
                   "tool result should have output");
    }

    conversation_free(&state);
    conversation_state_destroy(&state);
    sqlite_queue_cleanup(ctx);
    cleanup_test_db();
}

// Test 3: Orphaned TOOL_RESULT ignored
static void test_orphaned_tool_result(void) {
    setup_test_db();

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "klawed");
    assert(ctx != NULL);

    sqlite_queue_init_schema(ctx);
    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    // Insert user message
    char *user_msg = create_text_message("Hello");
    insert_message(db, "client", "klawed", user_msg);
    free(user_msg);

    // Insert TOOL_RESULT without matching TOOL (orphaned)
    char *result_msg = create_tool_result_message("Read", "call_orphan", "Some output", 0);
    insert_message(db, "client", "klawed", result_msg);
    free(result_msg);

    // Insert another user message
    char *user_msg2 = create_text_message("World");
    insert_message(db, "client", "klawed", user_msg2);
    free(user_msg2);

    // Restore conversation
    ConversationState state = {0};
    conversation_state_init(&state);
    int seeded = sqlite_queue_restore_conversation(ctx, &state);
    (void)seeded; // Used in checks below

    // Should only have 2 messages (the orphaned TOOL_RESULT should be ignored)
    assert_equal_int("test_orphaned_tool_result: message count", 2, state.count);

    if (state.count >= 2) {
        assert_true("test_orphaned_tool_result: both are text messages",
                   state.messages[0].contents[0].type == INTERNAL_TEXT &&
                   state.messages[1].contents[0].type == INTERNAL_TEXT,
                   "both messages should be text");
    }

    conversation_free(&state);
    conversation_state_destroy(&state);
    sqlite_queue_cleanup(ctx);
    cleanup_test_db();
}

// Test 4: Synthetic error injection for interrupted tool
static void test_synthetic_error_injection(void) {
    setup_test_db();

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "klawed");
    assert(ctx != NULL);

    sqlite_queue_init_schema(ctx);
    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    // Insert user message
    char *user_msg = create_text_message("Read the file");
    insert_message(db, "client", "klawed", user_msg);
    free(user_msg);

    // Insert TOOL message (assistant requesting tool execution)
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", "test.txt");
    char *tool_msg = create_tool_message("Read", "call_interrupted", params);
    insert_message(db, "klawed", "client", tool_msg);
    free(tool_msg);
    cJSON_Delete(params);

    // NO TOOL_RESULT - conversation ends here (interrupted)

    // Restore conversation
    ConversationState state = {0};
    conversation_state_init(&state);
    int seeded = sqlite_queue_restore_conversation(ctx, &state);
    (void)seeded; // Used in checks below

    // Should have 3 messages: user text, assistant TOOL, synthetic error result
    assert_equal_int("test_synthetic_error_injection: message count", 3, state.count);

    if (state.count >= 3) {
        assert_true("test_synthetic_error_injection: first is user text",
                   state.messages[0].role == MSG_USER &&
                   state.messages[0].contents[0].type == INTERNAL_TEXT,
                   "first message should be user text");

        assert_true("test_synthetic_error_injection: second is assistant tool request",
                   state.messages[1].role == MSG_ASSISTANT &&
                   state.messages[1].contents[0].type == INTERNAL_TOOL_CALL,
                   "second message should be assistant tool request");

        assert_true("test_synthetic_error_injection: third is error result",
                   state.messages[2].role == MSG_USER &&
                   state.messages[2].contents[0].type == INTERNAL_TOOL_RESPONSE,
                   "third message should be tool result");

        assert_true("test_synthetic_error_injection: result is error",
                   state.messages[2].contents[0].is_error == 1,
                   "tool result should be error");

        assert_true("test_synthetic_error_injection: has error message",
                   state.messages[2].contents[0].tool_output != NULL &&
                   cJSON_GetObjectItem(state.messages[2].contents[0].tool_output, "error") != NULL,
                   "tool result should contain error message");
    }

    conversation_free(&state);
    conversation_state_destroy(&state);
    sqlite_queue_cleanup(ctx);
    cleanup_test_db();
}

// Test 5: User message typed while tool was executing (arrives between TOOL and TOOL_RESULT).
// The restore path must NOT flush the assistant turn early — that would produce
// assistant:[tool_use] → user:[plain text] which Anthropic rejects.  Instead the
// user text is buffered and injected AFTER the tool round-trip completes.
//
// In this sub-case there is NO TOOL_RESULT at all (session died mid-execution),
// so the interrupted-tool path fires: it synthesises an error result and then
// injects the deferred user text afterward.
// Expected sequence:
//   [0] user:  "Read the file"          (original request)
//   [1] asst:  tool_use Read            (tool call)
//   [2] user:  tool_result(error)       (synthetic error — klawed was killed)
//   [3] user:  "Never mind, cancel"     (deferred, injected after tool round-trip)
static void test_user_message_interrupts_tool(void) {
    setup_test_db();

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "klawed");
    assert(ctx != NULL);

    sqlite_queue_init_schema(ctx);
    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    // Insert user message
    char *user_msg = create_text_message("Read the file");
    insert_message(db, "client", "klawed", user_msg);
    free(user_msg);

    // Insert TOOL message
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", "test.txt");
    char *tool_msg = create_tool_message("Read", "call_interrupted", params);
    insert_message(db, "klawed", "client", tool_msg);
    free(tool_msg);
    cJSON_Delete(params);

    // User sends another message before TOOL_RESULT arrives (interleaved during execution)
    char *user_msg2 = create_text_message("Never mind, cancel that");
    insert_message(db, "client", "klawed", user_msg2);
    free(user_msg2);

    // Restore conversation
    ConversationState state = {0};
    conversation_state_init(&state);
    int seeded = sqlite_queue_restore_conversation(ctx, &state);
    (void)seeded;

    // 4 messages: original user text, assistant tool_use, synthetic error result,
    // deferred user text (injected after tool round-trip)
    assert_equal_int("test_user_message_interrupts_tool: message count", 4, state.count);

    if (state.count >= 4) {
        assert_true("test_user_message_interrupts_tool: [0] is user text",
                   state.messages[0].role == MSG_USER &&
                   state.messages[0].contents[0].type == INTERNAL_TEXT,
                   "first message should be user text");

        assert_true("test_user_message_interrupts_tool: [1] is assistant tool_use",
                   state.messages[1].role == MSG_ASSISTANT &&
                   state.messages[1].contents[0].type == INTERNAL_TOOL_CALL,
                   "second message should be assistant tool_use");

        assert_true("test_user_message_interrupts_tool: [2] is synthetic error tool_result",
                   state.messages[2].role == MSG_USER &&
                   state.messages[2].contents[0].type == INTERNAL_TOOL_RESPONSE &&
                   state.messages[2].contents[0].is_error == 1,
                   "third message should be synthetic error tool_result");

        assert_true("test_user_message_interrupts_tool: [3] is deferred user text",
                   state.messages[3].role == MSG_USER &&
                   state.messages[3].contents[0].type == INTERNAL_TEXT,
                   "fourth message should be deferred user text");
    }

    conversation_free(&state);
    conversation_state_destroy(&state);
    sqlite_queue_cleanup(ctx);
    cleanup_test_db();
}

// Test 6: Multiple tool calls in sequence
static void test_multiple_tool_calls(void) {
    setup_test_db();

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "klawed");
    assert(ctx != NULL);

    sqlite_queue_init_schema(ctx);
    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    // Insert user message
    char *user_msg = create_text_message("Read both files");
    insert_message(db, "client", "klawed", user_msg);
    free(user_msg);

    // Insert first TOOL message
    cJSON *params1 = cJSON_CreateObject();
    cJSON_AddStringToObject(params1, "file_path", "file1.txt");
    char *tool_msg1 = create_tool_message("Read", "call_1", params1);
    insert_message(db, "klawed", "client", tool_msg1);
    free(tool_msg1);
    cJSON_Delete(params1);

    // Insert first TOOL_RESULT (klawed sends result after executing the tool)
    char *result_msg1 = create_tool_result_message("Read", "call_1", "Contents of file1", 0);
    insert_message(db, "klawed", "client", result_msg1);
    free(result_msg1);

    // Insert second TOOL message
    cJSON *params2 = cJSON_CreateObject();
    cJSON_AddStringToObject(params2, "file_path", "file2.txt");
    char *tool_msg2 = create_tool_message("Read", "call_2", params2);
    insert_message(db, "klawed", "client", tool_msg2);
    free(tool_msg2);
    cJSON_Delete(params2);

    // Insert second TOOL_RESULT (klawed sends result after executing the tool)
    char *result_msg2 = create_tool_result_message("Read", "call_2", "Contents of file2", 0);
    insert_message(db, "klawed", "client", result_msg2);
    free(result_msg2);

    // Restore conversation
    ConversationState state = {0};
    conversation_state_init(&state);
    int seeded = sqlite_queue_restore_conversation(ctx, &state);
    (void)seeded; // Used in checks below

    // Should have 5 messages: user text, asst tool_use(call_1), user tool_result(call_1),
    // asst tool_use(call_2), user tool_result(call_2)
    assert_equal_int("test_multiple_tool_calls: message count", 5, state.count);

    if (state.count >= 5) {
        assert_true("test_multiple_tool_calls: first is user text",
                   state.messages[0].role == MSG_USER &&
                   state.messages[0].contents[0].type == INTERNAL_TEXT,
                   "first message should be user text");

        assert_true("test_multiple_tool_calls: second is assistant with first tool",
                   state.messages[1].role == MSG_ASSISTANT &&
                   state.messages[1].contents[0].type == INTERNAL_TOOL_CALL,
                   "second message should be assistant with first tool");

        assert_true("test_multiple_tool_calls: third is tool_result for call_1",
                   state.messages[2].role == MSG_USER &&
                   state.messages[2].contents[0].type == INTERNAL_TOOL_RESPONSE &&
                   state.messages[2].contents[0].is_error == 0,
                   "third message should be successful tool_result for call_1");

        assert_true("test_multiple_tool_calls: fourth is assistant with second tool",
                   state.messages[3].role == MSG_ASSISTANT &&
                   state.messages[3].contents[0].type == INTERNAL_TOOL_CALL,
                   "fourth message should be assistant with second tool");

        assert_true("test_multiple_tool_calls: fifth is tool_result for call_2",
                   state.messages[4].role == MSG_USER &&
                   state.messages[4].contents[0].type == INTERNAL_TOOL_RESPONSE &&
                   state.messages[4].contents[0].is_error == 0,
                   "fifth message should be successful tool_result for call_2");
    }

    conversation_free(&state);
    conversation_state_destroy(&state);
    sqlite_queue_cleanup(ctx);
    cleanup_test_db();
}

// Test 7: Mixed scenarios - first tool completes, second is interrupted with a
// user message arriving between the second TOOL and its (missing) TOOL_RESULT.
// Expected sequence after restore:
//   [0] user:  "Do multiple things"
//   [1] asst:  tool_use Read(call_success)
//   [2] user:  tool_result(call_success, ok)
//   [3] asst:  tool_use Bash(call_interrupted)
//   [4] user:  tool_result(call_interrupted, error)  ← synthetic
//   [5] user:  "Stop that"                           ← deferred, safe to inject here
static void test_mixed_tool_completion(void) {
    setup_test_db();

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "klawed");
    assert(ctx != NULL);

    sqlite_queue_init_schema(ctx);
    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    // Insert user message
    char *user_msg = create_text_message("Do multiple things");
    insert_message(db, "client", "klawed", user_msg);
    free(user_msg);

    // First tool - completes successfully
    cJSON *params1 = cJSON_CreateObject();
    cJSON_AddStringToObject(params1, "file_path", "file1.txt");
    char *tool_msg1 = create_tool_message("Read", "call_success", params1);
    insert_message(db, "klawed", "client", tool_msg1);
    free(tool_msg1);
    cJSON_Delete(params1);

    char *result_msg1 = create_tool_result_message("Read", "call_success", "Success", 0);
    insert_message(db, "klawed", "client", result_msg1);
    free(result_msg1);

    // Second tool - gets interrupted (no result)
    cJSON *params2 = cJSON_CreateObject();
    cJSON_AddStringToObject(params2, "command", "ls -la");
    char *tool_msg2 = create_tool_message("Bash", "call_interrupted", params2);
    insert_message(db, "klawed", "client", tool_msg2);
    free(tool_msg2);
    cJSON_Delete(params2);

    // User sends a message while second tool was executing (interleaved)
    char *user_msg2 = create_text_message("Stop that");
    insert_message(db, "client", "klawed", user_msg2);
    free(user_msg2);

    // Restore conversation
    ConversationState state = {0};
    conversation_state_init(&state);
    int seeded = sqlite_queue_restore_conversation(ctx, &state);
    (void)seeded;

    // 6 messages (see comment above)
    assert_equal_int("test_mixed_tool_completion: message count", 6, state.count);

    if (state.count >= 6) {
        assert_true("test_mixed_tool_completion: [0] user text",
                   state.messages[0].role == MSG_USER &&
                   state.messages[0].contents[0].type == INTERNAL_TEXT,
                   "[0] should be user text");

        assert_true("test_mixed_tool_completion: [1] asst tool_use (first)",
                   state.messages[1].role == MSG_ASSISTANT &&
                   state.messages[1].contents[0].type == INTERNAL_TOOL_CALL,
                   "[1] should be assistant tool_use");

        assert_true("test_mixed_tool_completion: [2] user tool_result (first, ok)",
                   state.messages[2].role == MSG_USER &&
                   state.messages[2].contents[0].type == INTERNAL_TOOL_RESPONSE &&
                   state.messages[2].contents[0].is_error == 0,
                   "[2] should be successful tool_result");

        assert_true("test_mixed_tool_completion: [3] asst tool_use (second)",
                   state.messages[3].role == MSG_ASSISTANT &&
                   state.messages[3].contents[0].type == INTERNAL_TOOL_CALL,
                   "[3] should be assistant tool_use");

        assert_true("test_mixed_tool_completion: [4] synthetic error tool_result",
                   state.messages[4].role == MSG_USER &&
                   state.messages[4].contents[0].type == INTERNAL_TOOL_RESPONSE &&
                   state.messages[4].contents[0].is_error == 1,
                   "[4] should be synthetic error tool_result");

        assert_true("test_mixed_tool_completion: [5] deferred user text",
                   state.messages[5].role == MSG_USER &&
                   state.messages[5].contents[0].type == INTERNAL_TEXT,
                   "[5] should be deferred user text");
    }

    conversation_free(&state);
    conversation_state_destroy(&state);
    sqlite_queue_cleanup(ctx);
    cleanup_test_db();
}

// Test 8: Empty database
static void test_empty_database(void) {
    setup_test_db();

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "klawed");
    assert(ctx != NULL);

    sqlite_queue_init_schema(ctx);

    // Restore conversation from empty database
    ConversationState state = {0};
    conversation_state_init(&state);
    int seeded = sqlite_queue_restore_conversation(ctx, &state);

    assert_equal_int("test_empty_database: seed count", 0, seeded);
    assert_equal_int("test_empty_database: message count", 0, state.count);

    conversation_free(&state);
    conversation_state_destroy(&state);
    sqlite_queue_cleanup(ctx);
    cleanup_test_db();
}

// Test 9: Only assistant messages (no user messages)
static void test_only_assistant_messages(void) {
    setup_test_db();

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "klawed");
    assert(ctx != NULL);

    sqlite_queue_init_schema(ctx);
    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    // Insert only assistant text messages
    char *asst_msg1 = create_text_message("I'm here to help");
    insert_message(db, "klawed", "client", asst_msg1);
    free(asst_msg1);

    char *asst_msg2 = create_text_message("What can I do for you?");
    insert_message(db, "klawed", "client", asst_msg2);
    free(asst_msg2);

    // Restore conversation
    ConversationState state = {0};
    conversation_state_init(&state);
    int seeded = sqlite_queue_restore_conversation(ctx, &state);
    (void)seeded; // Used in checks below

    // Note: restore_conversation filters out pure assistant text messages without user context
    // These assistant messages have no preceding user message, so they may be skipped
    assert_equal_int("test_only_assistant_messages: message count", 1, state.count);

    if (state.count >= 1) {
        assert_true("test_only_assistant_messages: message is assistant",
                   state.messages[0].role == MSG_ASSISTANT,
                   "message should be from assistant");
    }

    conversation_free(&state);
    conversation_state_destroy(&state);
    sqlite_queue_cleanup(ctx);
    cleanup_test_db();
}

// Test 10: The exact scenario from the bug report:
//   TOOL (Sleep) → client TEXT (user typed during sleep) → TOOL_RESULT (Sleep done)
// The restore path used to produce the illegal sequence:
//   assistant:[tool_use] → user:[plain text] → user:[tool_result]
// (Anthropic rejects this because tool_use must be immediately followed by tool_result)
// With the fix the user text is deferred and injected only after the tool round-trip:
//   [0] user:  "Sleep for 70s"
//   [1] asst:  tool_use Sleep
//   [2] user:  tool_result Sleep
//   [3] user:  "also see if there is a bug..." (deferred, now safe)
static void test_user_text_between_tool_and_result(void) {
    setup_test_db();

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "klawed");
    assert(ctx != NULL);

    sqlite_queue_init_schema(ctx);
    sqlite3 *db = (sqlite3 *)ctx->db_handle;

    // msg 1: user asks klawed to sleep
    char *user_msg = create_text_message("Sleep for 70 seconds");
    insert_message(db, "client", "klawed", user_msg);
    free(user_msg);

    // msg 2 (id 397 equivalent): klawed issues a Sleep tool call
    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "duration", 70);
    char *tool_msg = create_tool_message("Sleep", "tooluse_E4sagE1", params);
    insert_message(db, "klawed", "client", tool_msg);
    free(tool_msg);
    cJSON_Delete(params);

    // msg 3 (id 398): user typed while klawed was sleeping — INTERLEAVED
    char *user_during = create_text_message("also see if there is a bug");
    insert_message(db, "client", "klawed", user_during);
    free(user_during);

    // msg 4 (id 399): Sleep result arrives (klawed sends result after tool completes)
    char *result_msg = create_tool_result_message("Sleep", "tooluse_E4sagE1", "slept 70s", 0);
    insert_message(db, "klawed", "client", result_msg);
    free(result_msg);

    // Restore conversation
    ConversationState state = {0};
    conversation_state_init(&state);
    int seeded = sqlite_queue_restore_conversation(ctx, &state);
    (void)seeded;

    // 4 messages: original user, assistant tool_use, tool_result, deferred user text
    assert_equal_int("test_user_text_between_tool_and_result: message count", 4, state.count);

    if (state.count >= 4) {
        assert_true("test_user_text_between_tool_and_result: [0] user text",
                   state.messages[0].role == MSG_USER &&
                   state.messages[0].contents[0].type == INTERNAL_TEXT,
                   "[0] should be user text");

        assert_true("test_user_text_between_tool_and_result: [1] asst tool_use",
                   state.messages[1].role == MSG_ASSISTANT &&
                   state.messages[1].contents[0].type == INTERNAL_TOOL_CALL,
                   "[1] should be assistant tool_use");

        // Critical: tool_result must be at [2], not after the plain user text
        assert_true("test_user_text_between_tool_and_result: [2] tool_result (not plain text)",
                   state.messages[2].role == MSG_USER &&
                   state.messages[2].contents[0].type == INTERNAL_TOOL_RESPONSE,
                   "[2] must be tool_result, not plain user text");

        assert_true("test_user_text_between_tool_and_result: [2] tool_result not error",
                   state.messages[2].contents[0].is_error == 0,
                   "[2] tool_result should not be an error");

        assert_true("test_user_text_between_tool_and_result: [3] deferred user text",
                   state.messages[3].role == MSG_USER &&
                   state.messages[3].contents[0].type == INTERNAL_TEXT,
                   "[3] should be the deferred user text (injected after tool round-trip)");
    }

    conversation_free(&state);
    conversation_state_destroy(&state);
    sqlite_queue_cleanup(ctx);
    cleanup_test_db();
}

// Main test runner
int main(void) {
    printf("\n%s=== SQLite Queue Seeding Test Suite ===%s\n\n", COLOR_CYAN, COLOR_RESET);

    test_basic_text_messages();
    test_tool_result_pairing();
    test_orphaned_tool_result();
    test_synthetic_error_injection();
    test_user_message_interrupts_tool();
    test_multiple_tool_calls();
    test_mixed_tool_completion();
    test_empty_database();
    test_only_assistant_messages();
    test_user_text_between_tool_and_result();

    printf("\n%s=== Test Summary ===%s\n", COLOR_CYAN, COLOR_RESET);
    printf("Total tests: %d\n", tests_run);
    printf("%sPassed: %d%s\n", COLOR_GREEN, tests_passed, COLOR_RESET);

    if (tests_failed > 0) {
        printf("%sFailed: %d%s\n", COLOR_RED, tests_failed, COLOR_RESET);
        return 1;
    }

    printf("\n%s✓ All tests passed!%s\n\n", COLOR_GREEN, COLOR_RESET);
    return 0;
}
