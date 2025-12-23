/*
 * test_sqlite_queue.c - Test SQLite queue functionality
 */

// Disable unused function warnings for test builds since not all functions are used by tests
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

#include "../src/sqlite_queue.h"

// Stub functions for functions called by sqlite_queue.c that we don't need for testing
// These are normally defined in klawed.c and would be linked in when building klawed
typedef struct ConversationState {
    char *session_id;
} ConversationState;

typedef struct TUIStateStruct TUIState;

// Stubs for functions used by sqlite_queue_process_message
static void add_user_message(ConversationState *state, const char *text) {
    (void)state;
    (void)text;
}

static void add_assistant_message_openai(ConversationState *state, void *message) {
    (void)state;
    (void)message;
}

static void add_tool_results(ConversationState *state, void *results, int count) {
    (void)state;
    (void)results;
    (void)count;
}

static int is_tool_allowed(const char *tool_name, ConversationState *state) {
    (void)tool_name;
    (void)state;
    return 0;
}

static void* execute_tool(const char *tool_name, void *input, ConversationState *state) {
    (void)tool_name;
    (void)input;
    (void)state;
    return NULL;
}

static void* call_api_with_retries(ConversationState *state) {
    (void)state;
    return NULL;
}

static void api_response_free(void *response) {
    (void)response;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_DB "/tmp/test_sqlite_queue.db"

static int test_init_cleanup(void) {
    printf("Test: Initialize and cleanup SQLite queue context\n");

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB, "test_sender");
    if (!ctx) {
        printf("  FAILED: Could not initialize SQLite queue\n");
        return -1;
    }

    printf("  PASSED: SQLite queue initialized\n");

    sqlite_queue_cleanup(ctx);
    printf("  PASSED: SQLite queue cleaned up\n");

    return 0;
}

static int test_schema_init(void) {
    printf("Test: Initialize database schema\n");

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB, "test_sender");
    if (!ctx) {
        printf("  FAILED: Could not initialize SQLite queue\n");
        return -1;
    }

    int result = sqlite_queue_init_schema(ctx);
    if (result != SQLITE_QUEUE_ERROR_NONE) {
        printf("  FAILED: Schema initialization failed: %s\n", sqlite_queue_last_error(ctx));
        sqlite_queue_cleanup(ctx);
        return -1;
    }

    printf("  PASSED: Database schema initialized\n");

    sqlite_queue_cleanup(ctx);
    return 0;
}

static int test_send_receive(void) {
    printf("Test: Send and receive messages\n");

    // Remove existing test database
    unlink(TEST_DB);

    SQLiteQueueContext *sender = sqlite_queue_init(TEST_DB, "sender");
    SQLiteQueueContext *receiver = sqlite_queue_init(TEST_DB, "receiver");

    if (!sender || !receiver) {
        printf("  FAILED: Could not initialize contexts\n");
        if (sender) sqlite_queue_cleanup(sender);
        if (receiver) sqlite_queue_cleanup(receiver);
        return -1;
    }

    // Send a message
    const char *test_message = "{\"messageType\": \"TEXT\", \"content\": \"Hello, World!\"}";
    int result = sqlite_queue_send(sender, "receiver", test_message, strlen(test_message));
    if (result != SQLITE_QUEUE_ERROR_NONE) {
        printf("  FAILED: Send failed: %s\n", sqlite_queue_last_error(sender));
        sqlite_queue_cleanup(sender);
        sqlite_queue_cleanup(receiver);
        return -1;
    }

    printf("  PASSED: Message sent\n");

    // Receive the message (with short timeout)
    char **messages = NULL;
    long long *message_ids = NULL;
    int message_count = 0;

    result = sqlite_queue_receive(receiver, NULL, 10, &messages, &message_count, &message_ids, 5000);
    if (result != SQLITE_QUEUE_ERROR_NONE) {
        printf("  FAILED: Receive failed: %s\n", sqlite_queue_last_error(receiver));
        sqlite_queue_cleanup(sender);
        sqlite_queue_cleanup(receiver);
        return -1;
    }

    if (message_count != 1) {
        printf("  FAILED: Expected 1 message, got %d\n", message_count);
        for (int i = 0; i < message_count; i++) {
            free(messages[i]);
        }
        free(messages);
        free(message_ids);
        sqlite_queue_cleanup(sender);
        sqlite_queue_cleanup(receiver);
        return -1;
    }

    printf("  PASSED: Message received\n");

    // Acknowledge the message
    result = sqlite_queue_acknowledge(receiver, message_ids[0]);
    if (result != SQLITE_QUEUE_ERROR_NONE) {
        printf("  FAILED: Acknowledge failed: %s\n", sqlite_queue_last_error(receiver));
        free(messages[0]);
        free(messages);
        free(message_ids);
        sqlite_queue_cleanup(sender);
        sqlite_queue_cleanup(receiver);
        return -1;
    }

    printf("  PASSED: Message acknowledged\n");

    // Verify message was marked as sent
    result = sqlite_queue_receive(receiver, NULL, 10, &messages, &message_count, &message_ids, 1000);
    if (result != SQLITE_QUEUE_ERROR_TIMEOUT && result != SQLITE_QUEUE_ERROR_NO_MESSAGES) {
        printf("  FAILED: Expected timeout after acknowledgment\n");
        free(messages[0]);
        free(messages);
        free(message_ids);
        sqlite_queue_cleanup(sender);
        sqlite_queue_cleanup(receiver);
        return -1;
    }

    printf("  PASSED: Message properly marked as sent\n");

    // Cleanup
    free(messages[0]);
    free(messages);
    free(message_ids);
    sqlite_queue_cleanup(sender);
    sqlite_queue_cleanup(receiver);

    // Remove test database
    unlink(TEST_DB);

    return 0;
}

static int test_statistics(void) {
    printf("Test: Get queue statistics\n");

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB, "test_stats");
    if (!ctx) {
        printf("  FAILED: Could not initialize SQLite queue\n");
        return -1;
    }

    int pending = 0, total = 0, unread = 0;
    int result = sqlite_queue_get_stats(ctx, &pending, &total, &unread);

    if (result != 0) {
        printf("  FAILED: Could not get statistics\n");
        sqlite_queue_cleanup(ctx);
        return -1;
    }

    printf("  PASSED: Statistics retrieved (pending: %d, total: %d, unread: %d)\n",
           pending, total, unread);

    sqlite_queue_cleanup(ctx);
    return 0;
}

static int test_status(void) {
    printf("Test: Get queue status\n");

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB, "test_status");
    if (!ctx) {
        printf("  FAILED: Could not initialize SQLite queue\n");
        return -1;
    }

    char status[512];
    int result = sqlite_queue_get_status(ctx, status, sizeof(status));

    if (result != 0) {
        printf("  FAILED: Could not get status\n");
        sqlite_queue_cleanup(ctx);
        return -1;
    }

    printf("  PASSED: Status retrieved\n");
    printf("  Status: %s\n", status);

    sqlite_queue_cleanup(ctx);
    return 0;
}

static int test_available(void) {
    printf("Test: Check if SQLite queue is available\n");

    bool available = sqlite_queue_available();
    if (!available) {
        printf("  FAILED: SQLite queue should always be available\n");
        return -1;
    }

    printf("  PASSED: SQLite queue is available\n");
    return 0;
}

static int test_error_handling(void) {
    printf("Test: Error handling\n");

    // Test with NULL context
    const char *error = sqlite_queue_last_error(NULL);
    if (error == NULL) {
        printf("  FAILED: Should return error string for NULL context\n");
        return -1;
    }

    // Test error clearing
    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB, "test_error");
    if (!ctx) {
        printf("  FAILED: Could not initialize context\n");
        return -1;
    }

    sqlite_queue_clear_error(ctx);
    if (ctx->last_error != SQLITE_QUEUE_ERROR_NONE) {
        printf("  FAILED: Error not cleared\n");
        sqlite_queue_cleanup(ctx);
        return -1;
    }

    printf("  PASSED: Error handling works correctly\n");

    sqlite_queue_cleanup(ctx);
    return 0;
}

int main(void) {
    printf("========================================\n");
    printf("SQLite Queue Tests\n");
    printf("========================================\n\n");

    int failed = 0;

    // Remove existing test database
    unlink(TEST_DB);

    if (test_init_cleanup() != 0) failed++;
    printf("\n");

    if (test_schema_init() != 0) failed++;
    printf("\n");

    if (test_send_receive() != 0) failed++;
    printf("\n");

    if (test_statistics() != 0) failed++;
    printf("\n");

    if (test_status() != 0) failed++;
    printf("\n");

    if (test_available() != 0) failed++;
    printf("\n");

    if (test_error_handling() != 0) failed++;
    printf("\n");

    // Cleanup test database
    unlink(TEST_DB);

    printf("========================================\n");
    if (failed == 0) {
        printf("All tests PASSED!\n");
    } else {
        printf("%d test(s) FAILED\n", failed);
    }
    printf("========================================\n");

    return failed > 0 ? 1 : 0;
}
