/*
 * Unit tests for thread cancellation on ESC press
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

// Simulated tool thread that runs for a long time
typedef struct {
    volatile int cancelled;
    volatile int completed;
    int sleep_seconds;
} TestThreadArg;

static void test_thread_cleanup(void *arg) {
    TestThreadArg *t = (TestThreadArg *)arg;
    t->cancelled = 1;
    printf("Cleanup handler called - thread cancelled\n");
}

static void *test_thread_func(void *arg) {
    TestThreadArg *t = (TestThreadArg *)arg;

    // Enable thread cancellation
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    // Register cleanup handler
    pthread_cleanup_push(test_thread_cleanup, arg);

    printf("Thread started, will run for %d seconds\n", t->sleep_seconds);

    // Simulate long-running work with cancellation points
    for (int i = 0; i < t->sleep_seconds; i++) {
        sleep(1);
        pthread_testcancel();  // Cancellation point
        printf("Thread tick %d/%d\n", i + 1, t->sleep_seconds);
    }

    t->completed = 1;
    printf("Thread completed normally\n");

    // Pop cleanup handler (don't execute on normal exit)
    pthread_cleanup_pop(0);

    return NULL;
}

static void test_thread_cancel_basic(void) {
    printf("\n=== Test: Basic thread cancellation ===\n");

    TestThreadArg arg = {0, 0, 10};  // Would run for 10 seconds
    pthread_t thread;

    pthread_create(&thread, NULL, test_thread_func, &arg);

    // Let thread run for 2 seconds
    sleep(2);

    // Cancel the thread
    printf("Cancelling thread...\n");
    pthread_cancel(thread);

    // Join to ensure cleanup
    pthread_join(thread, NULL);

    // Verify cleanup handler was called
    assert(arg.cancelled == 1 && "Cleanup handler should have been called");
    assert(arg.completed == 0 && "Thread should not have completed normally");

    printf("✓ Thread was cancelled and cleanup handler executed\n");
}

static void test_thread_cancel_multiple(void) {
    printf("\n=== Test: Multiple thread cancellation ===\n");

    const int thread_count = 5;
    TestThreadArg args[5];
    pthread_t threads[5];

    // Create multiple threads
    for (int i = 0; i < thread_count; i++) {
        args[i].cancelled = 0;
        args[i].completed = 0;
        args[i].sleep_seconds = 10;
        pthread_create(&threads[i], NULL, test_thread_func, &args[i]);
    }

    // Let threads run for 2 seconds
    sleep(2);

    // Cancel all threads
    printf("Cancelling all %d threads...\n", thread_count);
    for (int i = 0; i < thread_count; i++) {
        pthread_cancel(threads[i]);
    }

    // Join all threads
    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    // Verify all cleanup handlers were called
    for (int i = 0; i < thread_count; i++) {
        assert(args[i].cancelled == 1 && "Cleanup handler should have been called");
        assert(args[i].completed == 0 && "Thread should not have completed normally");
    }

    printf("✓ All %d threads were cancelled with cleanup\n", thread_count);
}

static void test_thread_normal_completion(void) {
    printf("\n=== Test: Normal thread completion (no cancel) ===\n");

    TestThreadArg arg = {0, 0, 1};  // Run for only 1 second
    pthread_t thread;

    pthread_create(&thread, NULL, test_thread_func, &arg);

    // Let thread complete normally
    pthread_join(thread, NULL);

    // Verify thread completed without cancellation
    assert(arg.cancelled == 0 && "Cleanup handler should NOT have been called");
    assert(arg.completed == 1 && "Thread should have completed normally");

    printf("✓ Thread completed normally without cancellation\n");
}

static void test_immediate_cancel(void) {
    printf("\n=== Test: Immediate cancellation ===\n");

    TestThreadArg arg = {0, 0, 10};
    pthread_t thread;

    pthread_create(&thread, NULL, test_thread_func, &arg);

    // Cancel immediately (no sleep)
    printf("Cancelling thread immediately...\n");
    pthread_cancel(thread);

    pthread_join(thread, NULL);

    // Thread might not have even started, but cleanup should be safe
    printf("✓ Immediate cancellation handled safely\n");
}

int main(void) {
    printf("Thread Cancellation Tests\n");
    printf("==========================\n");

    test_thread_cancel_basic();
    test_thread_normal_completion();
    test_thread_cancel_multiple();
    test_immediate_cancel();

    printf("\n=== All tests passed! ===\n");
    return 0;
}
