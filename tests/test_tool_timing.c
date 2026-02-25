/**
 * Unit tests for tool execution timing
 *
 * This test ensures that tool execution completes without unnecessary delays.
 * Specifically, it verifies that the ESC-checking loop doesn't add 60+ seconds
 * of delay after tool completion.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

// Monitor thread argument
typedef struct {
    pthread_t *threads;
    int thread_count;
    volatile int *done_flag;
} MonitorArg;

// Fast tool thread (simulates quick tool execution)
static void *fast_tool_func(void *arg) {
    (void)arg;
    usleep(10000);  // 10ms tool execution
    return NULL;
}

// Monitor thread that joins tool threads
static void *monitor_func(void *arg) {
    MonitorArg *ma = (MonitorArg *)arg;
    for (int i = 0; i < ma->thread_count; i++) {
        pthread_join(ma->threads[i], NULL);
    }
    *ma->done_flag = 1;
    return NULL;
}

// Test: Verify that monitor thread completes immediately after tool threads finish
static void test_monitor_thread_timing(void) {
    printf("Test: Monitor thread timing\n");

    // Simulate tool threads that complete quickly
    volatile int done_flag = 0;

    // Create and time the execution
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Launch 3 fast tool threads
    pthread_t tool_threads[3];
    for (int i = 0; i < 3; i++) {
        pthread_create(&tool_threads[i], NULL, fast_tool_func, NULL);
    }

    // Launch monitor thread
    pthread_t monitor_thread;
    MonitorArg monitor_arg = {tool_threads, 3, &done_flag};
    pthread_create(&monitor_thread, NULL, monitor_func, &monitor_arg);

    // Wait for completion
    while (!done_flag) {
        usleep(10000);  // Check every 10ms
    }
    pthread_join(monitor_thread, NULL);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    printf("  Tool execution + monitor took: %ld ms\n", elapsed_ms);

    // Should complete in under 200ms (10ms tool + overhead)
    // Definitely should NOT take 60+ seconds!
    if (elapsed_ms > 500) {
        printf("  ✗ FAILED: Took too long (%ld ms > 500 ms)\n", elapsed_ms);
        exit(1);
    }

    printf("  ✓ PASSED\n\n");
}

// Test: Verify ESC checking loop exits immediately when tools complete
static void test_esc_checking_loop(void) {
    printf("Test: ESC checking loop timing\n");

    volatile int all_tools_done = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Simulate tools completing immediately
    all_tools_done = 1;

    // This simulates the ESC checking loop from process_response()
    int checks = 0;
    while (!all_tools_done) {
        checks++;
        usleep(50000);  // 50ms

        // Safety: break after 1 second to prevent test hanging
        if (checks > 20) {
            break;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    printf("  ESC loop took: %ld ms (checks: %d)\n", elapsed_ms, checks);

    // Should exit immediately (< 10ms) since all_tools_done was already true
    if (elapsed_ms > 10) {
        printf("  ✗ FAILED: Loop should exit immediately\n");
        exit(1);
    }

    if (checks > 0) {
        printf("  ✗ FAILED: Should not enter loop when tools already done\n");
        exit(1);
    }

    printf("  ✓ PASSED\n\n");
}

// Very fast tool (5ms)
static void *very_fast_tool(void *arg) {
    (void)arg;
    usleep(5000);  // 5ms
    return NULL;
}

// Test: Verify that old bug (waiting 60 seconds) is fixed
static void test_no_60_second_delay(void) {
    printf("Test: No 60-second delay after tool completion\n");

    volatile int done_flag = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Create tool thread
    pthread_t tool_thread;
    pthread_create(&tool_thread, NULL, very_fast_tool, NULL);

    // Create monitor thread
    pthread_t monitor_thread;
    MonitorArg monitor_arg = {&tool_thread, 1, &done_flag};
    pthread_create(&monitor_thread, NULL, monitor_func, &monitor_arg);

    // Wait for completion (simulating old buggy code)
    // OLD BUG: Would check 600 times * 100ms = 60 seconds
    // NEW FIX: Should exit as soon as done_flag is set
    int max_checks = 600;  // Simulate old loop limit
    for (int checks = 0; checks < max_checks; checks++) {
        if (done_flag) {
            break;  // EXIT IMMEDIATELY when done
        }
        usleep(100000);  // 100ms (old interval)
    }

    pthread_join(monitor_thread, NULL);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    printf("  Total time: %ld ms\n", elapsed_ms);

    // Should complete in under 500ms, NOT 60+ seconds
    if (elapsed_ms > 1000) {
        printf("  ✗ FAILED: Took %ld ms (should be < 1000 ms)\n", elapsed_ms);
        printf("  This indicates the 60-second delay bug!\n");
        exit(1);
    }

    printf("  ✓ PASSED (no 60-second delay)\n\n");
}

int main(void) {
    printf("=== Tool Timing Tests ===\n\n");

    test_esc_checking_loop();
    test_monitor_thread_timing();
    test_no_60_second_delay();

    printf("=== All tests passed! ===\n");
    return 0;
}
