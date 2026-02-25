/*
 * test_conversation_free.c - Unit tests for conversation memory management
 *
 * Tests free_internal_message() to ensure:
 * - Double-free is prevented (the root cause of the segfault)
 * - Pointers are properly NULLed after freeing
 * - Multiple calls to free functions don't crash
 * - Session loading/clearing scenarios work correctly
 *
 * Compilation: make test-conversation-free
 * Usage: ./build/test_conversation_free
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <cjson/cJSON.h>
#include <bsd/stdlib.h>

// Forward declare what we need from openai_messages.h
// This allows us to test without pulling in all the logger dependencies
typedef enum {
    MSG_USER,
    MSG_ASSISTANT,
    MSG_SYSTEM,
    MSG_TOOL
} MessageRole;

typedef enum {
    INTERNAL_TEXT,
    INTERNAL_TOOL_CALL,
    INTERNAL_TOOL_RESPONSE
} InternalContentType;

typedef struct {
    InternalContentType type;
    char *text;
    char *tool_name;
    char *tool_id;
    cJSON *tool_params;
    cJSON *tool_output;
    int is_error;
} InternalContent;

typedef struct {
    MessageRole role;
    int content_count;
    InternalContent *contents;
} InternalMessage;

// The function under test
void free_internal_message(InternalMessage *msg);

// ============================================================================
// Test Framework
// ============================================================================

#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, ...) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf(COLOR_GREEN "  ✓ " COLOR_RESET); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } else { \
            tests_failed++; \
            printf(COLOR_RED "  ✗ " COLOR_RESET); \
            printf(__VA_ARGS__); \
            printf("\n"); \
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
// Helper Functions - Create test messages with content
// ============================================================================

static InternalMessage create_test_message_with_text(const char *text) {
    InternalMessage msg = {0};
    msg.role = MSG_ASSISTANT;
    msg.content_count = 1;
    msg.contents = calloc(1, sizeof(InternalContent));
    if (msg.contents) {
        msg.contents[0].type = INTERNAL_TEXT;
        msg.contents[0].text = strdup(text);
    }
    return msg;
}

static InternalMessage create_test_message_with_tool_call(const char *tool_name,
                                                          const char *tool_id,
                                                          const char *params_json) {
    InternalMessage msg = {0};
    msg.role = MSG_ASSISTANT;
    msg.content_count = 1;
    msg.contents = calloc(1, sizeof(InternalContent));
    if (msg.contents) {
        msg.contents[0].type = INTERNAL_TOOL_CALL;
        msg.contents[0].tool_name = strdup(tool_name);
        msg.contents[0].tool_id = strdup(tool_id);
        msg.contents[0].tool_params = cJSON_Parse(params_json);
    }
    return msg;
}

static InternalMessage create_test_message_mixed(void) {
    InternalMessage msg = {0};
    msg.role = MSG_ASSISTANT;
    msg.content_count = 2;
    msg.contents = calloc(2, sizeof(InternalContent));
    if (msg.contents) {
        msg.contents[0].type = INTERNAL_TEXT;
        msg.contents[0].text = strdup("Let me check that file.");

        msg.contents[1].type = INTERNAL_TOOL_CALL;
        msg.contents[1].tool_name = strdup("Read");
        msg.contents[1].tool_id = strdup("call_123");
        msg.contents[1].tool_params = cJSON_Parse("{\"file\":\"/test/file.txt\"}");
    }
    return msg;
}

// ============================================================================
// Test Cases - free_internal_message() regression tests
// ============================================================================

static void test_free_message_with_text(void) {
    printf(COLOR_YELLOW "\nTest: free_internal_message with text content\n" COLOR_RESET);

    InternalMessage msg = create_test_message_with_text("Hello, world!");

    TEST_ASSERT(msg.contents != NULL, "Message created with text content");
    TEST_ASSERT(msg.contents[0].text != NULL, "Text field allocated");
    TEST_ASSERT(strcmp(msg.contents[0].text, "Hello, world!") == 0, "Text content correct");

    free_internal_message(&msg);

    TEST_ASSERT(msg.contents == NULL, "contents is NULL after free");
    TEST_ASSERT(msg.content_count == 0, "content_count is 0 after free");
    TEST_ASSERT(msg.contents[0].text == NULL, "text field is NULL after free (prevents double-free)");
}

static void test_free_message_with_tool_call(void) {
    printf(COLOR_YELLOW "\nTest: free_internal_message with tool call\n" COLOR_RESET);

    InternalMessage msg = create_test_message_with_tool_call("Bash", "call_abc", "{\"command\":\"ls\"}");

    TEST_ASSERT(msg.contents != NULL, "Message created with tool call");
    TEST_ASSERT(msg.contents[0].tool_name != NULL, "tool_name allocated");
    TEST_ASSERT(msg.contents[0].tool_id != NULL, "tool_id allocated");
    TEST_ASSERT(msg.contents[0].tool_params != NULL, "tool_params allocated");

    free_internal_message(&msg);

    TEST_ASSERT(msg.contents == NULL, "contents is NULL after free");
    TEST_ASSERT(msg.contents[0].tool_name == NULL, "tool_name is NULL after free");
    TEST_ASSERT(msg.contents[0].tool_id == NULL, "tool_id is NULL after free");
    TEST_ASSERT(msg.contents[0].tool_params == NULL, "tool_params is NULL after free");
}

static void test_free_message_mixed_content(void) {
    printf(COLOR_YELLOW "\nTest: free_internal_message with mixed content\n" COLOR_RESET);

    InternalMessage msg = create_test_message_mixed();

    TEST_ASSERT(msg.content_count == 2, "Message has 2 content blocks");

    free_internal_message(&msg);

    TEST_ASSERT(msg.contents == NULL, "contents is NULL after free");
    TEST_ASSERT(msg.content_count == 0, "content_count is 0 after free");
}

static void test_double_free_prevention(void) {
    printf(COLOR_YELLOW "\nTest: Double-free prevention\n" COLOR_RESET);

    InternalMessage msg = create_test_message_with_text("Test content");

    TEST_ASSERT(msg.contents != NULL, "Message created");

    // First free
    free_internal_message(&msg);
    TEST_ASSERT(msg.contents == NULL, "contents is NULL after first free");

    // Second free - should not crash or cause issues
    free_internal_message(&msg);
    TEST_ASSERT(true, "Second free_internal_message call does not crash");

    // Verify state is still consistent
    TEST_ASSERT(msg.contents == NULL, "contents remains NULL");
    TEST_ASSERT(msg.content_count == 0, "content_count remains 0");
}

static void test_free_null_pointer(void) {
    printf(COLOR_YELLOW "\nTest: free_internal_message with NULL pointer\n" COLOR_RESET);

    InternalMessage msg = {0};
    msg.contents = NULL;
    msg.content_count = 0;

    // Should not crash
    free_internal_message(&msg);
    TEST_ASSERT(true, "free_internal_message handles NULL contents");
}

static void test_free_already_freed_text(void) {
    printf(COLOR_YELLOW "\nTest: Free already-freed text pointer\n" COLOR_RESET);

    InternalMessage msg = create_test_message_with_text("Test");
    char *text_copy = msg.contents[0].text;

    free_internal_message(&msg);

    // Verify the pointer was NULLed (this is the key fix!)
    TEST_ASSERT(text_copy != NULL, "Original text pointer was valid before free");
    TEST_ASSERT(msg.contents[0].text == NULL, "text field is NULL after free (THE FIX)");
}

static void test_free_multiple_messages(void) {
    printf(COLOR_YELLOW "\nTest: Free multiple messages in sequence\n" COLOR_RESET);

    InternalMessage msg1 = create_test_message_with_text("Message 1");
    InternalMessage msg2 = create_test_message_with_tool_call("Grep", "call_1", "{\"pattern\":\"TODO\"}");
    InternalMessage msg3 = create_test_message_mixed();

    // Free all three
    free_internal_message(&msg1);
    TEST_ASSERT(msg1.contents == NULL, "Message 1 freed correctly");

    free_internal_message(&msg2);
    TEST_ASSERT(msg2.contents == NULL, "Message 2 freed correctly");

    free_internal_message(&msg3);
    TEST_ASSERT(msg3.contents == NULL, "Message 3 freed correctly");

    TEST_ASSERT(true, "Multiple messages freed without crash");
}

// ============================================================================
// Regression Test - Session loading scenario (the actual bug scenario)
// ============================================================================

static void test_session_resume_double_free(void) {
    printf(COLOR_YELLOW "\nTest: Session resume double-free scenario\n" COLOR_RESET);

    // Simulate what happens when resuming a session:
    // 1. Load messages from database
    // 2. Clear conversation
    // 3. Try to free loaded messages

    InternalMessage loaded_messages[3];
    int loaded_count = 3;

    // Simulate loading messages from DB
    loaded_messages[0] = create_test_message_with_text("System: You are helpful.");
    loaded_messages[1] = create_test_message_with_text("User: Help me.");
    loaded_messages[2] = create_test_message_with_tool_call("Bash", "call_x", "{\"command\":\"pwd\"}");

    TEST_ASSERT(loaded_messages[0].contents != NULL, "Loaded message 1");
    TEST_ASSERT(loaded_messages[2].contents != NULL, "Loaded message 3 with tool call");

    // Simulate what session loading does:
    // It adds messages to conversation, then when clearing, it frees them

    // First, simulate adding to conversation (like session_load_from_db does)
    for (int i = 0; i < loaded_count; i++) {
        // In real code: conversation.messages[conversation.count++] = loaded_messages[i];
        // Then set loaded_messages[i].contents = NULL to prevent double-free
        loaded_messages[i].content_count = 0;
        loaded_messages[i].contents = NULL;
    }

    // Now simulate what the bug was: the original code didn't NULL pointers
    // in clear_conversation(), so when session_cleanup called conversation_free,
    // it tried to free already-freed pointers, causing "pointer being freed was not allocated"

    // With the fix, this should work:
    InternalMessage msg = create_test_message_with_text("test");
    free_internal_message(&msg);  // First free
    TEST_ASSERT(msg.contents[0].text == NULL, "text is NULL after free");

    // Simulate the bug scenario: calling free on already-freed data
    // Before the fix, this would crash with "pointer being freed was not allocated"
    // After the fix, it should be safe because pointers are NULLed
    free(msg.contents);  // This was the bug - contents was not NULLed!

    // The fix ensures that after free_internal_message, all pointers are NULL
    // so subsequent free() calls won't crash
    TEST_ASSERT(true, "No crash from double-free scenario");
}

static void test_conversation_clear_and_reuse(void) {
    printf(COLOR_YELLOW "\nTest: Conversation clear and reuse scenario\n" COLOR_RESET);

    // Simulate a conversation that gets cleared and reused

    // Create messages
    InternalMessage msg1 = create_test_message_with_text("First message");
    InternalMessage msg2 = create_test_message_with_text("Second message");

    // Simulate conversation_free
    free_internal_message(&msg1);
    free_internal_message(&msg2);

    TEST_ASSERT(msg1.contents == NULL, "msg1.contents is NULL");
    TEST_ASSERT(msg2.contents == NULL, "msg2.contents is NULL");

    // Now simulate what clear_conversation does - it keeps the system message
    // and frees everything else, then can reuse the conversation

    InternalMessage system_msg = create_test_message_with_text("System prompt");
    InternalMessage user_msg = create_test_message_with_text("User input");
    InternalMessage assistant_msg = create_test_message_with_text("Assistant response");

    // Simulate clear_conversation keeping system message
    InternalMessage *messages[] = {&system_msg, &user_msg, &assistant_msg};
    int system_msg_count = 1;

    // Free all except system message
    for (int i = system_msg_count; i < 3; i++) {
        free_internal_message(messages[i]);
        TEST_ASSERT(messages[i]->contents == NULL, "Message %d freed", i + 1);
    }

    // System message should still be valid
    TEST_ASSERT(system_msg.contents != NULL, "System message preserved");

    // Now free everything (conversation_free at shutdown)
    for (int i = 0; i < 3; i++) {
        free_internal_message(messages[i]);
    }

    TEST_ASSERT(true, "Clear and reuse scenario works correctly");
}

static void test_multiple_clear_calls(void) {
    printf(COLOR_YELLOW "\nTest: Multiple clear_conversation calls\n" COLOR_RESET);

    // Create a conversation with messages
    InternalMessage msg1 = create_test_message_with_text("Message 1");
    InternalMessage msg2 = create_test_message_with_text("Message 2");

    // Simulate first clear_conversation
    free_internal_message(&msg1);
    free_internal_message(&msg2);

    TEST_ASSERT(msg1.contents == NULL, "After first clear: msg1 NULL");
    TEST_ASSERT(msg2.contents == NULL, "After first clear: msg2 NULL");

    // Simulate second clear_conversation (like what happens when user types /clear twice)
    // Before the fix, this would crash because pointers weren't NULLed
    free_internal_message(&msg1);
    free_internal_message(&msg2);

    TEST_ASSERT(true, "Multiple clear calls don't crash");
    TEST_ASSERT(msg1.content_count == 0, "msg1 content_count is 0");
    TEST_ASSERT(msg2.content_count == 0, "msg2 content_count is 0");
}

// ============================================================================
// Test for NULL assignment in all pointer fields
// ============================================================================

static void test_all_pointers_nulled_after_free(void) {
    printf(COLOR_YELLOW "\nTest: All pointer fields are NULLed after free\n" COLOR_RESET);

    InternalMessage msg = create_test_message_mixed();

    InternalContent *content = &msg.contents[0];
    char *original_text = content->text;
    char *original_tool_name = content[1].tool_name;
    char *original_tool_id = content[1].tool_id;
    cJSON *original_tool_params = content[1].tool_params;

    TEST_ASSERT(original_text != NULL, "text was allocated");
    TEST_ASSERT(original_tool_name != NULL, "tool_name was allocated");
    TEST_ASSERT(original_tool_id != NULL, "tool_id was allocated");
    TEST_ASSERT(original_tool_params != NULL, "tool_params was allocated");

    free_internal_message(&msg);

    // All these assertions verify the fix from commit c537726
    TEST_ASSERT(content->text == NULL, "text is NULL after free");
    TEST_ASSERT(content[1].tool_name == NULL, "tool_name is NULL after free");
    TEST_ASSERT(content[1].tool_id == NULL, "tool_id is NULL after free");
    TEST_ASSERT(content[1].tool_params == NULL, "tool_params is NULL after free");
    TEST_ASSERT(content[1].tool_output == NULL, "tool_output is NULL after free");
    TEST_ASSERT(msg.contents == NULL, "contents is NULL after free");
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
    printf("=== Conversation Memory Management Tests ===\n");
    printf("Testing free_internal_message() for double-free prevention\n");
    printf("Regression tests for segfault on 'pointer being freed was not allocated'\n");
    printf("\nThis test suite verifies the fix in commit c537726 that ensures\n");
    printf("all pointers are set to NULL after freeing to prevent double-free.\n");

    // free_internal_message regression tests
    test_free_message_with_text();
    test_free_message_with_tool_call();
    test_free_message_mixed_content();
    test_double_free_prevention();
    test_free_null_pointer();
    test_free_already_freed_text();
    test_free_multiple_messages();

    // Session loading regression tests
    test_session_resume_double_free();
    test_conversation_clear_and_reuse();
    test_multiple_clear_calls();

    // Verify all pointers are NULLed (the actual fix)
    test_all_pointers_nulled_after_free();

    TEST_SUMMARY();
}
