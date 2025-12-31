/*
 * test_sqlite_queue.c - Test SQLite queue functionality
 *
 * This test tests the basic SQLite queue functions that don't require klawed.c dependencies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <stdbool.h>

// Include the actual SQLite queue header
#include "../src/sqlite_queue.h"

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
        if (messages) {
            for (int i = 0; i < message_count; i++) {
                if (messages[i]) {
                    free(messages[i]);
                }
            }
            free(messages);
        }
        if (message_ids) {
            free(message_ids);
        }
        sqlite_queue_cleanup(sender);
        sqlite_queue_cleanup(receiver);
        return -1;
    }

    printf("  PASSED: Message received\n");

    // Verify message content
    if (strstr(messages[0], "Hello, World!") == NULL) {
        printf("  FAILED: Message content incorrect: %s\n", messages[0]);
        if (messages) {
            free(messages[0]);
            free(messages);
        }
        if (message_ids) {
            free(message_ids);
        }
        sqlite_queue_cleanup(sender);
        sqlite_queue_cleanup(receiver);
        return -1;
    }

    // Acknowledge the message
    result = sqlite_queue_acknowledge(receiver, message_ids[0]);
    if (result != SQLITE_QUEUE_ERROR_NONE) {
        printf("  FAILED: Acknowledge failed: %s\n", sqlite_queue_last_error(receiver));
        if (messages) {
            free(messages[0]);
            free(messages);
        }
        if (message_ids) {
            free(message_ids);
        }
        sqlite_queue_cleanup(sender);
        sqlite_queue_cleanup(receiver);
        return -1;
    }

    printf("  PASSED: Message acknowledged\n");

    // Verify message was marked as sent
    result = sqlite_queue_receive(receiver, NULL, 10, &messages, &message_count, &message_ids, 1000);
    if (result != SQLITE_QUEUE_ERROR_TIMEOUT && result != SQLITE_QUEUE_ERROR_NO_MESSAGES) {
        printf("  FAILED: Expected timeout after acknowledgment\n");
        if (messages) {
            free(messages[0]);
            free(messages);
        }
        if (message_ids) {
            free(message_ids);
        }
        sqlite_queue_cleanup(sender);
        sqlite_queue_cleanup(receiver);
        return -1;
    }

    printf("  PASSED: Message properly marked as sent\n");

    // Cleanup
    if (messages) {
        free(messages[0]);
        free(messages);
    }
    if (message_ids) {
        free(message_ids);
    }
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
    printf("  PASSED: Error handling works correctly\n");

    sqlite_queue_cleanup(ctx);
    return 0;
}

static int test_message_size_limit(void) {
    printf("Test: Message size limit\n");

    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB, "test_size");
    if (!ctx) {
        printf("  FAILED: Could not initialize SQLite queue\n");
        return -1;
    }

    // Create a message larger than default limit (1MB)
    size_t huge_size = 2 * 1024 * 1024; // 2MB
    char *huge_message = malloc(huge_size);
    if (!huge_message) {
        printf("  FAILED: Could not allocate huge message\n");
        sqlite_queue_cleanup(ctx);
        return -1;
    }

    memset(huge_message, 'A', huge_size - 1);
    huge_message[huge_size - 1] = '\0';

    int result = sqlite_queue_send(ctx, "receiver", huge_message, huge_size);
    free(huge_message);

    if (result != SQLITE_QUEUE_ERROR_MESSAGE_TOO_LONG) {
        printf("  FAILED: Should reject oversized message, got error code: %d\n", result);
        sqlite_queue_cleanup(ctx);
        return -1;
    }

    printf("  PASSED: Message size limit enforced\n");
    sqlite_queue_cleanup(ctx);
    return 0;
}

int main(void) {
    printf("========================================\n");
    printf("SQLite Queue Tests (Fixed Version)\n");
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

    if (test_message_size_limit() != 0) failed++;
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
