/*
 * Test suite for insert_system_message function
 *
 * Tests the logic for inserting system messages into a message array,
 * covering all edge cases:
 * - Empty message array
 * - Existing system message at position 0 (replacement)
 * - Existing non-system messages (shift and insert)
 * - Full message array (replace first message)
 * - NULL parameter handling
 *
 * Compilation: make test-insert-system-message
 * Usage: ./build/test_insert_system_message
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/klawed_internal.h"
#include "../src/background_init.h"

// Test helper macros
#define TEST(name) printf("\n=== Test: %s ===\n", name)
#define PASS() printf("✓ PASS\n")
#define FAIL(msg) do { printf("✗ FAIL: %s\n", msg); exit(1); } while(0)

// Helper to create a text content
static InternalContent* create_text_content(const char *text) {
    InternalContent *content = calloc(1, sizeof(InternalContent));
    if (content) {
        content->type = INTERNAL_TEXT;
        content->text = strdup(text);
    }
    return content;
}

// Helper to create a test message
static void init_test_message(InternalMessage *msg, MessageRole role, const char *text) {
    memset(msg, 0, sizeof(InternalMessage));
    msg->role = role;
    msg->contents = create_text_content(text);
    msg->content_count = 1;
}

// Helper to free message contents
static void free_message_contents(InternalMessage *msg, int count) {
    for (int i = 0; i < count; i++) {
        if (msg[i].contents) {
            for (int j = 0; j < msg[i].content_count; j++) {
                if (msg[i].contents[j].text) {
                    free(msg[i].contents[j].text);
                }
            }
            free(msg[i].contents);
        }
    }
}

// Test 1: Insert into empty array
static void test_insert_into_empty_array(void) {
    TEST("Insert system message into empty array");

    InternalMessage messages[MAX_MESSAGES] = {0};
    int count = 0;
    char *system_text = strdup("System prompt");

    int result = insert_system_message(messages, &count, system_text);

    if (result != 0) {
        FAIL("Expected return value 0");
    }
    if (count != 1) {
        FAIL("Expected count to be 1");
    }
    if (messages[0].role != MSG_SYSTEM) {
        FAIL("Expected role to be MSG_SYSTEM");
    }
    if (strcmp(messages[0].contents[0].text, "System prompt") != 0) {
        FAIL("Expected text to be 'System prompt'");
    }

    free_message_contents(messages, count);
    PASS();
}

// Test 2: Replace existing system message at position 0
static void test_replace_existing_system_message(void) {
    TEST("Replace existing system message at position 0");

    InternalMessage messages[MAX_MESSAGES] = {0};
    int count = 1;

    // Set up existing system message
    init_test_message(&messages[0], MSG_SYSTEM, "Old system prompt");

    char *new_system_text = strdup("New system prompt");

    int result = insert_system_message(messages, &count, new_system_text);

    if (result != 0) {
        FAIL("Expected return value 0");
    }
    if (count != 1) {
        FAIL("Expected count to remain 1");
    }
    if (messages[0].role != MSG_SYSTEM) {
        FAIL("Expected role to be MSG_SYSTEM");
    }
    if (strcmp(messages[0].contents[0].text, "New system prompt") != 0) {
        FAIL("Expected text to be updated to 'New system prompt'");
    }

    free_message_contents(messages, count);
    PASS();
}

// Test 3: Insert at position 0, shifting existing messages
static void test_insert_shifting_messages(void) {
    TEST("Insert system message, shifting existing messages");

    InternalMessage messages[MAX_MESSAGES] = {0};
    int count = 2;

    // Set up existing messages
    init_test_message(&messages[0], MSG_USER, "User message 1");
    init_test_message(&messages[1], MSG_ASSISTANT, "Assistant message 1");

    char *system_text = strdup("System prompt");

    int result = insert_system_message(messages, &count, system_text);

    if (result != 0) {
        FAIL("Expected return value 0");
    }
    if (count != 3) {
        FAIL("Expected count to be 3");
    }

    // Check position 0 is system message
    if (messages[0].role != MSG_SYSTEM) {
        FAIL("Expected position 0 role to be MSG_SYSTEM");
    }
    if (strcmp(messages[0].contents[0].text, "System prompt") != 0) {
        FAIL("Expected position 0 text to be 'System prompt'");
    }

    // Check original messages were shifted
    if (messages[1].role != MSG_USER) {
        FAIL("Expected position 1 role to be MSG_USER");
    }
    if (strcmp(messages[1].contents[0].text, "User message 1") != 0) {
        FAIL("Expected position 1 to have original user message");
    }

    if (messages[2].role != MSG_ASSISTANT) {
        FAIL("Expected position 2 role to be MSG_ASSISTANT");
    }
    if (strcmp(messages[2].contents[0].text, "Assistant message 1") != 0) {
        FAIL("Expected position 2 to have original assistant message");
    }

    free_message_contents(messages, count);
    PASS();
}

// Test 4: Insert when array is full (MAX_MESSAGES)
static void test_insert_full_array(void) {
    TEST("Insert system message when array is full");

    // Create a smaller test array to avoid huge allocations
    InternalMessage messages[MAX_MESSAGES] = {0};
    int count = MAX_MESSAGES;

    // Fill the array
    for (int i = 0; i < MAX_MESSAGES; i++) {
        char text[32];
        snprintf(text, sizeof(text), "Message %d", i);
        init_test_message(&messages[i], MSG_USER, text);
    }

    char *system_text = strdup("System prompt");

    int result = insert_system_message(messages, &count, system_text);

    if (result != 0) {
        FAIL("Expected return value 0");
    }
    if (count != MAX_MESSAGES) {
        FAIL("Expected count to remain MAX_MESSAGES");
    }

    // Check position 0 is now system message (replaced first message)
    if (messages[0].role != MSG_SYSTEM) {
        FAIL("Expected position 0 role to be MSG_SYSTEM after replacement");
    }
    if (strcmp(messages[0].contents[0].text, "System prompt") != 0) {
        FAIL("Expected position 0 text to be 'System prompt'");
    }

    // Free all messages
    free_message_contents(messages, count);
    PASS();
}

// Test 5: NULL messages parameter
static void test_null_messages(void) {
    TEST("NULL messages parameter");

    int count = 0;
    char *system_text = strdup("System prompt");

    int result = insert_system_message(NULL, &count, system_text);

    if (result != -1) {
        free(system_text);
        FAIL("Expected return value -1 for NULL messages");
    }

    free(system_text);
    PASS();
}

// Test 6: NULL count parameter
static void test_null_count(void) {
    TEST("NULL count parameter");

    InternalMessage messages[MAX_MESSAGES] = {0};
    char *system_text = strdup("System prompt");

    int result = insert_system_message(messages, NULL, system_text);

    if (result != -1) {
        free(system_text);
        FAIL("Expected return value -1 for NULL count");
    }

    free(system_text);
    PASS();
}

// Test 7: NULL system_text parameter
static void test_null_system_text(void) {
    TEST("NULL system_text parameter");

    InternalMessage messages[MAX_MESSAGES] = {0};
    int count = 0;

    int result = insert_system_message(messages, &count, NULL);

    if (result != -1) {
        FAIL("Expected return value -1 for NULL system_text");
    }

    PASS();
}

// Test 8: Multiple insert calls (idempotent behavior check)
static void test_multiple_inserts(void) {
    TEST("Multiple insert calls");

    InternalMessage messages[MAX_MESSAGES] = {0};
    int count = 0;

    // First insert
    char *system_text1 = strdup("First system prompt");
    insert_system_message(messages, &count, system_text1);

    // Second insert should replace, not add
    char *system_text2 = strdup("Second system prompt");
    int result = insert_system_message(messages, &count, system_text2);

    if (result != 0) {
        FAIL("Expected return value 0");
    }
    if (count != 1) {
        FAIL("Expected count to remain 1 after replacement");
    }
    if (strcmp(messages[0].contents[0].text, "Second system prompt") != 0) {
        FAIL("Expected text to be updated to 'Second system prompt'");
    }

    free_message_contents(messages, count);
    PASS();
}

// Test 9: Insert with user message at position 0
static void test_insert_with_user_at_position_0(void) {
    TEST("Insert with user message at position 0");

    InternalMessage messages[MAX_MESSAGES] = {0};
    int count = 1;

    // User message at position 0 (no system message yet)
    init_test_message(&messages[0], MSG_USER, "User question");

    char *system_text = strdup("System prompt");

    int result = insert_system_message(messages, &count, system_text);

    if (result != 0) {
        FAIL("Expected return value 0");
    }
    if (count != 2) {
        FAIL("Expected count to be 2");
    }
    if (messages[0].role != MSG_SYSTEM) {
        FAIL("Expected position 0 to be system message");
    }
    if (messages[1].role != MSG_USER) {
        FAIL("Expected position 1 to be user message");
    }

    free_message_contents(messages, count);
    PASS();
}

// Test 10: Insert with assistant message at position 0
static void test_insert_with_assistant_at_position_0(void) {
    TEST("Insert with assistant message at position 0");

    InternalMessage messages[MAX_MESSAGES] = {0};
    int count = 1;

    // Assistant message at position 0
    init_test_message(&messages[0], MSG_ASSISTANT, "Assistant response");

    char *system_text = strdup("System prompt");

    int result = insert_system_message(messages, &count, system_text);

    if (result != 0) {
        FAIL("Expected return value 0");
    }
    if (count != 2) {
        FAIL("Expected count to be 2");
    }
    if (messages[0].role != MSG_SYSTEM) {
        FAIL("Expected position 0 to be system message");
    }
    if (messages[1].role != MSG_ASSISTANT) {
        FAIL("Expected position 1 to be assistant message");
    }

    free_message_contents(messages, count);
    PASS();
}

int main(void) {
    printf("=== insert_system_message Test Suite ===\n");
    printf("Testing system message insertion logic\n");

    test_insert_into_empty_array();
    test_replace_existing_system_message();
    test_insert_shifting_messages();
    test_insert_full_array();
    test_null_messages();
    test_null_count();
    test_null_system_text();
    test_multiple_inserts();
    test_insert_with_user_at_position_0();
    test_insert_with_assistant_at_position_0();

    printf("\n=== All tests passed! ===\n");
    return 0;
}
