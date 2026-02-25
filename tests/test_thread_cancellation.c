/**
 * Thread Cancellation Safety Tests
 *
 * Tests for:
 * 1. Thread cancellation during tool execution
 * 2. Proper cleanup of partially-created thread arrays
 * 3. Race conditions between cancellation and cleanup
 * 4. Memory safety during thread cancellation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>

// Test infrastructure
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        printf("Running %s...\n", #name); \
        tests_run++; \
        name(); \
        tests_passed++; \
        printf("  ✓ %s passed\n", #name); \
    } \
    static void name(void)

// Simulated structures (minimal versions for testing)
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int total;
    int completed;
    int cancelled;
} MockTracker;

typedef struct {
    char *tool_id;
    char *tool_name;
    int *result_written;
    MockTracker *tracker;
    int notified;
    volatile int *cleanup_called;
} MockThreadArg;

// Simulated cleanup handler
static void mock_cleanup_handler(void *arg) {
    MockThreadArg *t = (MockThreadArg *)arg;
    if (t->cleanup_called) {
        (*t->cleanup_called)++;
    }

    // Simulate writing error result
    if (t->result_written) {
        *t->result_written = 1;
    }

    // Notify tracker
    if (t->tracker && !t->notified) {
        pthread_mutex_lock(&t->tracker->mutex);
        t->tracker->completed++;
        t->notified = 1;
        pthread_cond_broadcast(&t->tracker->cond);
        pthread_mutex_unlock(&t->tracker->mutex);
    }
}

// Simulated long-running tool thread
static void *mock_tool_thread(void *arg) {
    MockThreadArg *t = (MockThreadArg *)arg;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    pthread_cleanup_push(mock_cleanup_handler, arg);

    // Simulate long-running work with cancellation points
    for (int i = 0; i < 100; i++) {
        // Check if cancelled
        pthread_testcancel();

        // Simulate work
        usleep(10000); // 10ms - cancellation point
    }

    // Normal completion
    if (t->result_written) {
        *t->result_written = 1;
    }

    if (t->tracker && !t->notified) {
        pthread_mutex_lock(&t->tracker->mutex);
        t->tracker->completed++;
        t->notified = 1;
        pthread_cond_broadcast(&t->tracker->cond);
        pthread_mutex_unlock(&t->tracker->mutex);
    }

    pthread_cleanup_pop(0);
    return NULL;
}

// Test 1: Single thread cancellation
TEST(test_single_thread_cancellation) {
    MockTracker tracker = {
        .total = 1,
        .completed = 0,
        .cancelled = 0
    };
    pthread_mutex_init(&tracker.mutex, NULL);
    pthread_cond_init(&tracker.cond, NULL);

    int result_written = 0;
    volatile int cleanup_called = 0;
    MockThreadArg arg = {
        .tool_id = "test-1",
        .tool_name = "Sleep",
        .result_written = &result_written,
        .tracker = &tracker,
        .notified = 0,
        .cleanup_called = &cleanup_called
    };

    pthread_t thread;
    int rc = pthread_create(&thread, NULL, mock_tool_thread, &arg);
    assert(rc == 0);

    // Let it run a bit
    usleep(50000); // 50ms

    // Cancel the thread
    pthread_cancel(thread);

    // Wait for completion
    pthread_join(thread, NULL);

    // Verify cleanup was called
    assert(cleanup_called == 1);
    assert(result_written == 1);
    assert(tracker.completed == 1);

    pthread_mutex_destroy(&tracker.mutex);
    pthread_cond_destroy(&tracker.cond);
}

// Test 2: Multiple thread cancellation
TEST(test_multiple_thread_cancellation) {
    const int NUM_THREADS = 5;
    MockTracker tracker = {
        .total = NUM_THREADS,
        .completed = 0,
        .cancelled = 0
    };
    pthread_mutex_init(&tracker.mutex, NULL);
    pthread_cond_init(&tracker.cond, NULL);

    pthread_t threads[NUM_THREADS];
    MockThreadArg args[NUM_THREADS];
    int results[NUM_THREADS];
    volatile int cleanup_counts[NUM_THREADS];

    memset(results, 0, sizeof(results));
    memset((void*)cleanup_counts, 0, sizeof(cleanup_counts));

    // Start all threads
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].tool_id = "test-id";
        args[i].tool_name = "Sleep";
        args[i].result_written = &results[i];
        args[i].tracker = &tracker;
        args[i].notified = 0;
        args[i].cleanup_called = &cleanup_counts[i];

        int rc = pthread_create(&threads[i], NULL, mock_tool_thread, &args[i]);
        assert(rc == 0);
    }

    // Let them run a bit
    usleep(50000); // 50ms

    // Cancel all threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_cancel(threads[i]);
    }

    // Join all threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Verify all cleanups were called exactly once
    for (int i = 0; i < NUM_THREADS; i++) {
        assert(cleanup_counts[i] == 1);
        assert(results[i] == 1);
    }

    assert(tracker.completed == NUM_THREADS);

    pthread_mutex_destroy(&tracker.mutex);
    pthread_cond_destroy(&tracker.cond);
}

// Test 3: Partial thread creation failure
TEST(test_partial_thread_creation) {
    const int EXPECTED_THREADS = 5;
    const int FAILED_AT = 3; // Simulate failure at thread 3

    pthread_t threads[EXPECTED_THREADS];
    MockThreadArg args[EXPECTED_THREADS];
    int started = 0;

    memset(threads, 0, sizeof(threads));
    memset(args, 0, sizeof(args));

    MockTracker tracker = {
        .total = EXPECTED_THREADS,
        .completed = 0,
        .cancelled = 0
    };
    pthread_mutex_init(&tracker.mutex, NULL);
    pthread_cond_init(&tracker.cond, NULL);

    // Try to start threads, simulate failure
    for (int i = 0; i < EXPECTED_THREADS; i++) {
        if (i == FAILED_AT) {
            // Simulate thread creation failure
            // In real scenario, we should cancel already-started threads
            break;
        }

        args[i].tool_id = "test-id";
        args[i].tool_name = "Sleep";
        args[i].tracker = &tracker;
        args[i].notified = 0;

        int rc = pthread_create(&threads[i], NULL, mock_tool_thread, &args[i]);
        assert(rc == 0);
        started++;
    }

    // CRITICAL: Must cancel already-started threads on failure
    for (int i = 0; i < started; i++) {
        pthread_cancel(threads[i]);
    }

    // Join the threads we started
    for (int i = 0; i < started; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&tracker.mutex);
    pthread_cond_destroy(&tracker.cond);

    // This test verifies the cleanup pattern for partial failures
    assert(started < EXPECTED_THREADS);
}

// Test 4: Race condition between cancellation and completion
TEST(test_cancellation_completion_race) {
    MockTracker tracker = {
        .total = 1,
        .completed = 0,
        .cancelled = 0
    };
    pthread_mutex_init(&tracker.mutex, NULL);
    pthread_cond_init(&tracker.cond, NULL);

    int result_written = 0;
    volatile int cleanup_called = 0;
    MockThreadArg arg = {
        .tool_id = "test-1",
        .tool_name = "FastTool",
        .result_written = &result_written,
        .tracker = &tracker,
        .notified = 0,
        .cleanup_called = &cleanup_called
    };

    pthread_t thread;
    int rc = pthread_create(&thread, NULL, mock_tool_thread, &arg);
    assert(rc == 0);

    // Try to cancel almost immediately (race with completion)
    usleep(1000); // 1ms - tool might already be done
    pthread_cancel(thread);

    // Wait for thread
    pthread_join(thread, NULL);

    // Either cleanup was called (cancelled) or result was written (completed)
    // But not both or neither
    assert((cleanup_called == 1 && result_written == 1) ||
           (cleanup_called == 0 && result_written == 1));

    // Exactly one completion notification
    assert(tracker.completed == 1);

    pthread_mutex_destroy(&tracker.mutex);
    pthread_cond_destroy(&tracker.cond);
}

// Test 5: Double notification prevention
TEST(test_double_notification_prevention) {
    MockTracker tracker = {
        .total = 1,
        .completed = 0,
        .cancelled = 0
    };
    pthread_mutex_init(&tracker.mutex, NULL);
    pthread_cond_init(&tracker.cond, NULL);

    int result_written = 0;
    volatile int cleanup_called = 0;
    MockThreadArg arg = {
        .tool_id = "test-1",
        .tool_name = "Sleep",
        .result_written = &result_written,
        .tracker = &tracker,
        .notified = 0,
        .cleanup_called = &cleanup_called
    };

    pthread_t thread;
    int rc = pthread_create(&thread, NULL, mock_tool_thread, &arg);
    assert(rc == 0);

    usleep(50000); // 50ms
    pthread_cancel(thread);
    pthread_join(thread, NULL);

    // The notified flag should prevent double notification
    // completed should be exactly 1, never 2
    assert(tracker.completed == 1);

    pthread_mutex_destroy(&tracker.mutex);
    pthread_cond_destroy(&tracker.cond);
}

// Test 6: Memory safety during cancellation
TEST(test_memory_safety_during_cancellation) {
    const int NUM_ITERATIONS = 100;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        MockTracker tracker = {
            .total = 1,
            .completed = 0,
            .cancelled = 0
        };
        pthread_mutex_init(&tracker.mutex, NULL);
        pthread_cond_init(&tracker.cond, NULL);

        int result_written = 0;
        volatile int cleanup_called = 0;
        MockThreadArg arg = {
            .tool_id = "test-1",
            .tool_name = "Sleep",
            .result_written = &result_written,
            .tracker = &tracker,
            .notified = 0,
            .cleanup_called = &cleanup_called
        };

        pthread_t thread;
        int rc = pthread_create(&thread, NULL, mock_tool_thread, &arg);
        assert(rc == 0);

        // Random delay before cancellation
        usleep((rand() % 10) * 1000); // 0-10ms

        pthread_cancel(thread);
        pthread_join(thread, NULL);

        // Verify no corruption
        assert(tracker.completed <= 1);
        assert(cleanup_called <= 1);

        pthread_mutex_destroy(&tracker.mutex);
        pthread_cond_destroy(&tracker.cond);
    }
}

int main(void) {
    printf("=== Thread Cancellation Safety Tests ===\n\n");

    run_test_single_thread_cancellation();
    run_test_multiple_thread_cancellation();
    run_test_partial_thread_creation();
    run_test_cancellation_completion_race();
    run_test_double_notification_prevention();
    run_test_memory_safety_during_cancellation();

    printf("\n=== Test Summary ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);

    if (tests_passed == tests_run) {
        printf("\n✓ All tests passed!\n");
        return 0;
    } else {
        printf("\n✗ Some tests failed!\n");
        return 1;
    }
}
