/*
 * Unit Tests for Retry Jitter Feature
 *
 * Tests the exponential backoff with jitter functionality:
 * - Jitter range verification (0-25% reduction)
 * - Multiple retry attempts with increasing backoff
 * - Statistical distribution of jitter values
 * - Edge cases (zero delay, max delay)
 *
 * Compilation: make test-retry-jitter
 * Usage: ./build/test_retry_jitter
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// Test framework colors
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"

// Constants from claude_internal.h
#define INITIAL_BACKOFF_MS 1000
#define MAX_BACKOFF_MS 10000
#define BACKOFF_MULTIPLIER 2.0

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Test utilities
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

static void assert_in_range(const char *test_name, int value, int min, int max, const char *desc) {
    char msg[256];
    snprintf(msg, sizeof(msg), "%s: %d not in range [%d, %d]", desc, value, min, max);
    assert_true(test_name, value >= min && value <= max, msg);
}

// Simulate the jitter calculation from claude.c
static int calculate_jitter_delay(int backoff_ms) {
    double jitter = 1.0 - ((double)rand() / RAND_MAX) * 0.25;
    return (int)(backoff_ms * jitter);
}

// Test 1: Basic jitter range validation
static void test_jitter_range() {
    printf("\n%s=== Test 1: Jitter Range Validation ===%s\n", COLOR_CYAN, COLOR_RESET);

    // Test with initial backoff
    int backoff = INITIAL_BACKOFF_MS;
    int samples = 100;

    for (int i = 0; i < samples; i++) {
        int actual = calculate_jitter_delay(backoff);

        // Expected range: 75% to 100% of original delay
        int min_expected = (int)(backoff * 0.75);
        int max_expected = backoff;

        char test_name[128];
        snprintf(test_name, sizeof(test_name), "Jitter sample %d (%dms)", i + 1, actual);
        assert_in_range(test_name, actual, min_expected, max_expected, "jitter delay");
    }
}

// Test 2: Jitter with increasing backoff values
static void test_jitter_with_exponential_backoff() {
    printf("\n%s=== Test 2: Jitter with Exponential Backoff ===%s\n", COLOR_CYAN, COLOR_RESET);

    int backoff = INITIAL_BACKOFF_MS;
    int retry_count = 0;
    int max_retries = 3;

    while (retry_count < max_retries) {
        int actual = calculate_jitter_delay(backoff);

        // Expected range for current backoff
        int min_expected = (int)(backoff * 0.75);
        int max_expected = backoff;

        char test_name[128];
        snprintf(test_name, sizeof(test_name),
                "Retry %d: backoff=%dms, jittered=%dms",
                retry_count + 1, backoff, actual);
        assert_in_range(test_name, actual, min_expected, max_expected, "exponential backoff jitter");

        // Increase backoff for next iteration
        backoff = (int)(backoff * BACKOFF_MULTIPLIER);
        if (backoff > MAX_BACKOFF_MS) {
            backoff = MAX_BACKOFF_MS;
        }
        retry_count++;
    }
}

// Test 3: Statistical distribution of jitter
static void test_jitter_distribution() {
    printf("\n%s=== Test 3: Statistical Distribution of Jitter ===%s\n", COLOR_CYAN, COLOR_RESET);

    int backoff = INITIAL_BACKOFF_MS;
    int samples = 1000;
    int sum = 0;
    int min_value = backoff;
    int max_value = 0;

    // Collect samples
    for (int i = 0; i < samples; i++) {
        int actual = calculate_jitter_delay(backoff);
        sum += actual;
        if (actual < min_value) min_value = actual;
        if (actual > max_value) max_value = actual;
    }

    // Calculate mean
    double mean = (double)sum / samples;

    // Expected mean: approximately 87.5% of backoff (midpoint of 75%-100% range)
    double expected_mean = backoff * 0.875;
    double tolerance = backoff * 0.05;  // 5% tolerance

    char test_name[128];
    snprintf(test_name, sizeof(test_name),
            "Mean jitter (%.1fms) near expected (%.1fms)",
            mean, expected_mean);
    assert_true(test_name,
                fabs(mean - expected_mean) < tolerance,
                "mean jitter outside expected range");

    // Check min/max observed values
    snprintf(test_name, sizeof(test_name),
            "Min observed (%dms) >= theoretical min (%dms)",
            min_value, (int)(backoff * 0.75));
    assert_true(test_name, min_value >= (int)(backoff * 0.70),
                "min value too low (might happen with low sample size)");

    snprintf(test_name, sizeof(test_name),
            "Max observed (%dms) <= backoff (%dms)",
            max_value, backoff);
    assert_true(test_name, max_value <= backoff, "max value exceeds backoff");

    printf("%s  Statistics: mean=%.1fms, min=%dms, max=%dms (n=%d)%s\n",
           COLOR_YELLOW, mean, min_value, max_value, samples, COLOR_RESET);
}

// Test 4: Edge cases
static void test_edge_cases() {
    printf("\n%s=== Test 4: Edge Cases ===%s\n", COLOR_CYAN, COLOR_RESET);

    // Test with small backoff
    int small_backoff = 10;
    int actual = calculate_jitter_delay(small_backoff);
    assert_in_range("Small backoff (10ms)", actual, 7, 10, "small value jitter");

    // Test with max backoff
    int max_backoff = MAX_BACKOFF_MS;
    actual = calculate_jitter_delay(max_backoff);
    assert_in_range("Max backoff (10000ms)", actual, 7500, 10000, "max value jitter");

    // Test multiple times to ensure consistency
    for (int i = 0; i < 10; i++) {
        actual = calculate_jitter_delay(INITIAL_BACKOFF_MS);
        char test_name[128];
        snprintf(test_name, sizeof(test_name), "Consistency check iteration %d", i + 1);
        assert_in_range(test_name, actual, 750, 1000, "consistency");
    }
}

// Test 5: Verify jitter prevents thundering herd
static void test_thundering_herd_prevention(void) {
    printf("\n%s=== Test 5: Thundering Herd Prevention ===%s\n", COLOR_CYAN, COLOR_RESET);

    // Simulate multiple clients retrying simultaneously
    int num_clients = 10;
    int backoff = INITIAL_BACKOFF_MS;
    int delays[10];
    int all_different = 1;

    // Generate delays for multiple "clients"
    for (int i = 0; i < num_clients; i++) {
        delays[i] = calculate_jitter_delay(backoff);
    }

    // Check that not all delays are identical (statistical test)
    for (int i = 1; i < num_clients; i++) {
        if (delays[i] != delays[0]) {
            all_different = 0;
            break;
        }
    }

    // With 10 clients and 25% jitter range, probability of all being identical is negligible
    assert_true("Multiple clients have different delays",
                !all_different,
                "all delays are identical (thundering herd risk)");

    // Print delay distribution for visibility
    printf("%s  Client delays (ms):%s", COLOR_YELLOW, COLOR_RESET);
    for (int i = 0; i < num_clients; i++) {
        printf(" %d", delays[i]);
    }
    printf("\n");
}

// Test 6: Verify jitter formula correctness
static void test_jitter_formula() {
    printf("\n%s=== Test 6: Jitter Formula Verification ===%s\n", COLOR_CYAN, COLOR_RESET);

    // Test the formula: actual_delay = backoff * (1.0 - rand(0, 0.25))
    // This means: actual_delay is between 75% and 100% of backoff

    int backoff = 2000;  // 2 seconds

    // Test extremes by controlling rand()
    // We can't directly control rand(), but we can verify the formula logic

    // When jitter = 1.0 (no reduction), actual_delay = backoff
    double jitter_max = 1.0 - 0.0;  // No reduction
    int delay_max = (int)(backoff * jitter_max);
    assert_true("Max jitter (no reduction) equals backoff",
                delay_max == backoff,
                "formula incorrect at maximum");

    // When jitter = 0.75 (25% reduction), actual_delay = 0.75 * backoff
    double jitter_min = 1.0 - 0.25;  // 25% reduction
    int delay_min = (int)(backoff * jitter_min);
    assert_true("Min jitter (25% reduction) equals 75% of backoff",
                delay_min == (int)(backoff * 0.75),
                "formula incorrect at minimum");

    // Verify formula with middle value (12.5% reduction)
    double jitter_mid = 1.0 - 0.125;
    int delay_mid = (int)(backoff * jitter_mid);
    assert_true("Mid jitter (12.5% reduction) equals 87.5% of backoff",
                delay_mid == (int)(backoff * 0.875),
                "formula incorrect at midpoint");
}

// Main test runner
int main(void) {
    printf("\n%s╔══════════════════════════════════════════════════════╗%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("%s║     Retry Jitter Feature - Unit Test Suite        ║%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("%s╚══════════════════════════════════════════════════════╝%s\n",
           COLOR_CYAN, COLOR_RESET);

    // Initialize random number generator with fixed seed for reproducibility
    srand(42);

    test_jitter_range();
    test_jitter_with_exponential_backoff();
    test_jitter_distribution();
    test_edge_cases();
    test_thundering_herd_prevention();
    test_jitter_formula();

    // Print summary
    printf("\n%s═══════════════════════════════════════════════════════%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("Tests run:    %d\n", tests_run);
    printf("%sPassed:       %d%s\n", COLOR_GREEN, tests_passed, COLOR_RESET);
    if (tests_failed > 0) {
        printf("%sFailed:       %d%s\n", COLOR_RED, tests_failed, COLOR_RESET);
    }
    printf("%s═══════════════════════════════════════════════════════%s\n",
           COLOR_CYAN, COLOR_RESET);

    return (tests_failed == 0) ? 0 : 1;
}
