/*
 * test_sqlite_queue_threading.c - Test SQLite queue async message processing
 *
 * Tests the new threading infrastructure for user message injection during tool execution.
 * This is a minimal test that doesn't require the full klawed build.
 */

#define _GNU_SOURCE  // For pthread_timedjoin_np

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include <sqlite3.h>

// Minimal definitions matching sqlite_queue.h

typedef enum {
    SQLITE_QUEUE_ERROR_NONE = 0,
    SQLITE_QUEUE_ERROR_INVALID_PARAM,
    SQLITE_QUEUE_ERROR_DB_OPEN,
    SQLITE_QUEUE_ERROR_DB_QUERY,
    SQLITE_QUEUE_ERROR_DB_SCHEMA,
    SQLITE_QUEUE_ERROR_MEMORY,
    SQLITE_QUEUE_ERROR_NO_MESSAGES,
    SQLITE_QUEUE_ERROR_TIMEOUT,
    SQLITE_QUEUE_ERROR_GENERAL
} SQLiteQueueErrorCode;

// Pending message entry for queued user input during tool execution
typedef struct PendingMessage {
    long long msg_id;        // Message ID from database
    char *content;           // Message content (TEXT type only)
    struct PendingMessage *next;
} PendingMessage;

// Minimal SQLite queue context for testing
typedef struct {
    char *db_path;
    char *sender_name;
    int enabled;

    // Threading support
    pthread_t worker_thread;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    PendingMessage *pending_messages;
    PendingMessage *pending_tail;
    int pending_count;
    volatile int processing;
    volatile int shutdown;
    void *state;

    // Database handle
    sqlite3 *db;
} TestQueueContext;

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(expr, msg) do { \
    tests_run++; \
    if (!(expr)) { \
        printf("  FAILED: %s\n", msg); \
        return -1; \
    } else { \
        printf("  ✓ %s\n", msg); \
        tests_passed++; \
    } \
} while(0)

// Simple context init for testing
static TestQueueContext* test_ctx_init(void) {
    TestQueueContext *ctx = calloc(1, sizeof(TestQueueContext));
    if (!ctx) return NULL;

    if (pthread_mutex_init(&ctx->queue_mutex, NULL) != 0) {
        free(ctx);
        return NULL;
    }

    if (pthread_cond_init(&ctx->queue_cond, NULL) != 0) {
        pthread_mutex_destroy(&ctx->queue_mutex);
        free(ctx);
        return NULL;
    }

    ctx->enabled = 1;
    return ctx;
}

// Simple context cleanup
static void test_ctx_cleanup(TestQueueContext *ctx) {
    if (!ctx) return;

    ctx->shutdown = 1;
    pthread_cond_broadcast(&ctx->queue_cond);

    // Free pending messages
    pthread_mutex_lock(&ctx->queue_mutex);
    PendingMessage *pm = ctx->pending_messages;
    while (pm) {
        PendingMessage *next = pm->next;
        free(pm->content);
        free(pm);
        pm = next;
    }
    pthread_mutex_unlock(&ctx->queue_mutex);

    pthread_mutex_destroy(&ctx->queue_mutex);
    pthread_cond_destroy(&ctx->queue_cond);

    free(ctx->db_path);
    free(ctx->sender_name);
    free(ctx);
}

// Enqueue a pending message
static int test_enqueue(TestQueueContext *ctx, long long msg_id, const char *content) {
    if (!ctx || !content) return -1;

    PendingMessage *pm = calloc(1, sizeof(PendingMessage));
    if (!pm) return -1;

    pm->msg_id = msg_id;
    pm->content = strdup(content);
    if (!pm->content) {
        free(pm);
        return -1;
    }
    pm->next = NULL;

    pthread_mutex_lock(&ctx->queue_mutex);

    if (ctx->pending_tail) {
        ctx->pending_tail->next = pm;
    } else {
        ctx->pending_messages = pm;
    }
    ctx->pending_tail = pm;
    ctx->pending_count++;

    pthread_cond_signal(&ctx->queue_cond);
    pthread_mutex_unlock(&ctx->queue_mutex);

    return 0;
}

// Dequeue a pending message
static PendingMessage* test_dequeue(TestQueueContext *ctx) {
    if (!ctx) return NULL;

    pthread_mutex_lock(&ctx->queue_mutex);

    PendingMessage *pm = ctx->pending_messages;
    if (pm) {
        ctx->pending_messages = pm->next;
        if (ctx->pending_messages == NULL) {
            ctx->pending_tail = NULL;
        }
        ctx->pending_count--;
    }

    pthread_mutex_unlock(&ctx->queue_mutex);

    return pm;
}

// Get pending count
static int test_pending_count(TestQueueContext *ctx) {
    if (!ctx) return 0;

    pthread_mutex_lock(&ctx->queue_mutex);
    int count = ctx->pending_count;
    pthread_mutex_unlock(&ctx->queue_mutex);

    return count;
}

/*
 * Test: Threading primitives are initialized correctly
 */
static int test_init_threading_primitives(void) {
    printf("\nTest: Threading primitives are initialized\n");

    TestQueueContext *ctx = test_ctx_init();
    ASSERT(ctx != NULL, "Context initialized");

    // Check that we can lock and unlock the mutex
    int lock_result = pthread_mutex_lock(&ctx->queue_mutex);
    ASSERT(lock_result == 0, "Mutex can be locked");

    int unlock_result = pthread_mutex_unlock(&ctx->queue_mutex);
    ASSERT(unlock_result == 0, "Mutex can be unlocked");

    // Verify initial state
    ASSERT(ctx->pending_messages == NULL, "Pending messages list is NULL initially");
    ASSERT(ctx->pending_tail == NULL, "Pending tail is NULL initially");
    ASSERT(ctx->pending_count == 0, "Pending count is 0 initially");
    ASSERT(ctx->processing == 0, "Processing flag is 0 initially");
    ASSERT(ctx->shutdown == 0, "Shutdown flag is 0 initially");
    ASSERT(ctx->state == NULL, "State pointer is NULL initially");

    test_ctx_cleanup(ctx);
    printf("  ✓ Cleanup completed without crash\n");
    tests_passed++;
    tests_run++;

    return 0;
}

/*
 * Test: Enqueue and dequeue messages
 */
static int test_enqueue_dequeue(void) {
    printf("\nTest: Enqueue and dequeue messages\n");

    TestQueueContext *ctx = test_ctx_init();
    ASSERT(ctx != NULL, "Context initialized");

    // Initial count should be 0
    ASSERT(test_pending_count(ctx) == 0, "Initial count is 0");

    // Enqueue first message
    int result = test_enqueue(ctx, 100, "Message 1");
    ASSERT(result == 0, "First enqueue succeeded");
    ASSERT(test_pending_count(ctx) == 1, "Count is 1 after first enqueue");

    // Enqueue second message
    result = test_enqueue(ctx, 200, "Message 2");
    ASSERT(result == 0, "Second enqueue succeeded");
    ASSERT(test_pending_count(ctx) == 2, "Count is 2 after second enqueue");

    // Enqueue third message
    result = test_enqueue(ctx, 300, "Message 3");
    ASSERT(result == 0, "Third enqueue succeeded");
    ASSERT(test_pending_count(ctx) == 3, "Count is 3 after third enqueue");

    // Dequeue in FIFO order
    PendingMessage *pm = test_dequeue(ctx);
    ASSERT(pm != NULL, "First dequeue returned a message");
    ASSERT(pm->msg_id == 100, "First message has correct ID");
    ASSERT(strcmp(pm->content, "Message 1") == 0, "First message has correct content");
    ASSERT(test_pending_count(ctx) == 2, "Count is 2 after first dequeue");
    free(pm->content);
    free(pm);

    pm = test_dequeue(ctx);
    ASSERT(pm != NULL, "Second dequeue returned a message");
    ASSERT(pm->msg_id == 200, "Second message has correct ID");
    ASSERT(strcmp(pm->content, "Message 2") == 0, "Second message has correct content");
    ASSERT(test_pending_count(ctx) == 1, "Count is 1 after second dequeue");
    free(pm->content);
    free(pm);

    pm = test_dequeue(ctx);
    ASSERT(pm != NULL, "Third dequeue returned a message");
    ASSERT(pm->msg_id == 300, "Third message has correct ID");
    ASSERT(strcmp(pm->content, "Message 3") == 0, "Third message has correct content");
    ASSERT(test_pending_count(ctx) == 0, "Count is 0 after third dequeue");
    free(pm->content);
    free(pm);

    // Dequeue from empty queue
    pm = test_dequeue(ctx);
    ASSERT(pm == NULL, "Dequeue from empty queue returns NULL");

    test_ctx_cleanup(ctx);
    return 0;
}

/*
 * Test: Cleanup with pending messages frees them
 */
static int test_cleanup_with_pending(void) {
    printf("\nTest: Cleanup with pending messages\n");

    TestQueueContext *ctx = test_ctx_init();
    ASSERT(ctx != NULL, "Context initialized");

    // Enqueue some messages
    test_enqueue(ctx, 1, "Pending 1");
    test_enqueue(ctx, 2, "Pending 2");
    test_enqueue(ctx, 3, "Pending 3");
    ASSERT(test_pending_count(ctx) == 3, "3 messages enqueued");

    // Cleanup should free all pending messages without crash
    test_ctx_cleanup(ctx);
    printf("  ✓ Cleanup with pending messages completed\n");
    tests_passed++;
    tests_run++;

    return 0;
}

/*
 * Test: Thread-safe concurrent enqueue
 */
typedef struct {
    TestQueueContext *ctx;
    int start_id;
    int count;
} EnqueueWorkerArgs;

static void* enqueue_worker(void *arg) {
    EnqueueWorkerArgs *args = (EnqueueWorkerArgs *)arg;

    for (int i = 0; i < args->count; i++) {
        char content[64];
        snprintf(content, sizeof(content), "Message from thread %d", args->start_id + i);
        test_enqueue(args->ctx, args->start_id + i, content);
        usleep(1000);  // Small delay to interleave
    }

    return NULL;
}

static int test_concurrent_enqueue(void) {
    printf("\nTest: Concurrent enqueue from multiple threads\n");

    TestQueueContext *ctx = test_ctx_init();
    ASSERT(ctx != NULL, "Context initialized");

    const int num_threads = 4;
    const int msgs_per_thread = 25;
    pthread_t threads[4];
    EnqueueWorkerArgs args[4];

    // Start threads
    for (int i = 0; i < num_threads; i++) {
        args[i].ctx = ctx;
        args[i].start_id = i * msgs_per_thread;
        args[i].count = msgs_per_thread;
        pthread_create(&threads[i], NULL, enqueue_worker, &args[i]);
    }

    // Wait for all threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    int final_count = test_pending_count(ctx);
    ASSERT(final_count == num_threads * msgs_per_thread,
           "All messages enqueued (100 total)");

    // Drain the queue
    int drained = 0;
    PendingMessage *pm;
    while ((pm = test_dequeue(ctx)) != NULL) {
        free(pm->content);
        free(pm);
        drained++;
    }
    ASSERT(drained == num_threads * msgs_per_thread, "All messages dequeued");
    ASSERT(test_pending_count(ctx) == 0, "Queue empty after drain");

    test_ctx_cleanup(ctx);
    return 0;
}

/*
 * Test: Worker thread pattern (simulated)
 */
typedef struct {
    TestQueueContext *ctx;
    int messages_processed;
} WorkerState;

static void* simulated_worker(void *arg) {
    WorkerState *ws = (WorkerState *)arg;
    TestQueueContext *ctx = ws->ctx;

    while (!ctx->shutdown) {
        pthread_mutex_lock(&ctx->queue_mutex);

        // Wait for a message
        while (!ctx->shutdown && ctx->pending_messages == NULL) {
            // Use timed wait to allow checking shutdown flag
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 100000000; // 100ms
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&ctx->queue_cond, &ctx->queue_mutex, &ts);
        }

        if (ctx->shutdown) {
            pthread_mutex_unlock(&ctx->queue_mutex);
            break;
        }

        // Dequeue
        PendingMessage *pm = ctx->pending_messages;
        if (pm) {
            ctx->pending_messages = pm->next;
            if (ctx->pending_messages == NULL) {
                ctx->pending_tail = NULL;
            }
            ctx->pending_count--;
        }
        ctx->processing = 1;
        pthread_mutex_unlock(&ctx->queue_mutex);

        if (pm) {
            // Simulate processing
            usleep(5000);  // 5ms
            ws->messages_processed++;
            free(pm->content);
            free(pm);
        }

        pthread_mutex_lock(&ctx->queue_mutex);
        ctx->processing = 0;
        pthread_mutex_unlock(&ctx->queue_mutex);
    }

    return NULL;
}

static int test_worker_pattern(void) {
    printf("\nTest: Worker thread pattern\n");

    TestQueueContext *ctx = test_ctx_init();
    ASSERT(ctx != NULL, "Context initialized");

    WorkerState ws = {.ctx = ctx, .messages_processed = 0};

    // Start worker thread
    pthread_t worker;
    pthread_create(&worker, NULL, simulated_worker, &ws);

    // Enqueue messages while worker is running
    for (int i = 0; i < 10; i++) {
        test_enqueue(ctx, i, "Worker test message");
        usleep(10000);  // 10ms between enqueues
    }

    // Wait a bit for processing
    usleep(200000);  // 200ms

    // Signal shutdown
    ctx->shutdown = 1;
    pthread_cond_broadcast(&ctx->queue_cond);

    // Wait for worker with timeout
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 2;
    pthread_timedjoin_np(worker, NULL, &timeout);

    ASSERT(ws.messages_processed == 10, "Worker processed all 10 messages");
    ASSERT(test_pending_count(ctx) == 0, "Queue is empty");

    test_ctx_cleanup(ctx);
    return 0;
}

/*
 * Test: Multiple init/cleanup cycles
 */
static int test_multiple_init_cleanup(void) {
    printf("\nTest: Multiple init/cleanup cycles\n");

    for (int i = 0; i < 10; i++) {
        TestQueueContext *ctx = test_ctx_init();
        if (!ctx) {
            printf("  FAILED: Init failed on iteration %d\n", i);
            return -1;
        }

        // Enqueue some messages
        test_enqueue(ctx, i * 10, "Cycle test");
        test_enqueue(ctx, i * 10 + 1, "Cycle test 2");

        test_ctx_cleanup(ctx);
    }

    printf("  ✓ 10 init/cleanup cycles completed\n");
    tests_passed++;
    tests_run++;

    return 0;
}

int main(void) {
    printf("\n═══════════════════════════════════════════════\n");
    printf("  SQLite Queue Threading Test Suite\n");
    printf("  Testing async message processing infrastructure\n");
    printf("═══════════════════════════════════════════════\n");

    int failures = 0;

    if (test_init_threading_primitives() != 0) failures++;
    if (test_enqueue_dequeue() != 0) failures++;
    if (test_cleanup_with_pending() != 0) failures++;
    if (test_concurrent_enqueue() != 0) failures++;
    if (test_worker_pattern() != 0) failures++;
    if (test_multiple_init_cleanup() != 0) failures++;

    printf("\n═══════════════════════════════════════════════\n");
    printf("  Test Summary\n");
    printf("═══════════════════════════════════════════════\n");
    printf("Total tests: %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_run - tests_passed);

    if (failures == 0 && tests_run == tests_passed) {
        printf("\n✓ All tests passed!\n\n");
        return 0;
    } else {
        printf("\n✗ Some tests failed!\n\n");
        return 1;
    }
}
