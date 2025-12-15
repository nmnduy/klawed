/*
 * test_token_usage_comprehensive.c - Comprehensive token usage extraction tests
 * Tests token usage extraction from various API providers
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_CYAN "\033[36m"
#define COLOR_MAGENTA "\033[35m"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static int test_extract_tokens(
    const char *usage_json_str,
    int *prompt_tokens, int *completion_tokens, int *total_tokens,
    int *cached_tokens, int *prompt_cache_hit_tokens, int *prompt_cache_miss_tokens
) {
    if (!usage_json_str) return -1;
    cJSON *usage = cJSON_Parse(usage_json_str);
    if (!usage) return -1;

    *prompt_tokens = 0;
    *completion_tokens = 0;
    *total_tokens = 0;
    *cached_tokens = 0;
    *prompt_cache_hit_tokens = 0;
    *prompt_cache_miss_tokens = 0;

    // Try input_tokens first (Anthropic style)
    cJSON *p = cJSON_GetObjectItem(usage, "input_tokens");
    if (!p) p = cJSON_GetObjectItem(usage, "prompt_tokens");

    cJSON *c = cJSON_GetObjectItem(usage, "output_tokens");
    if (!c) c = cJSON_GetObjectItem(usage, "completion_tokens");

    cJSON *t = cJSON_GetObjectItem(usage, "total_tokens");

    if (p && cJSON_IsNumber(p)) *prompt_tokens = p->valueint;
    if (c && cJSON_IsNumber(c)) *completion_tokens = c->valueint;
    if (t && cJSON_IsNumber(t)) *total_tokens = t->valueint;

    // Try Moonshot-style direct cached_tokens
    cJSON *dc = cJSON_GetObjectItem(usage, "cached_tokens");
    if (dc && cJSON_IsNumber(dc)) {
        *cached_tokens = dc->valueint;
    } else if (*cached_tokens == 0) {
        // Try DeepSeek-style: inside prompt_tokens_details
        cJSON *details = cJSON_GetObjectItem(usage, "prompt_tokens_details");
        if (details) {
            cJSON *dc2 = cJSON_GetObjectItem(details, "cached_tokens");
            if (dc2 && cJSON_IsNumber(dc2)) *cached_tokens = dc2->valueint;
        }
    }

    // Try Anthropic-style: cache_read_input_tokens
    if (*cached_tokens == 0) {
        cJSON *cr = cJSON_GetObjectItem(usage, "cache_read_input_tokens");
        if (cr && cJSON_IsNumber(cr)) *cached_tokens = cr->valueint;
    }

    // Extract detailed cache metrics
    cJSON *hit = cJSON_GetObjectItem(usage, "prompt_cache_hit_tokens");
    cJSON *miss = cJSON_GetObjectItem(usage, "prompt_cache_miss_tokens");
    if (hit && cJSON_IsNumber(hit)) *prompt_cache_hit_tokens = hit->valueint;
    if (miss && cJSON_IsNumber(miss)) *prompt_cache_miss_tokens = miss->valueint;

    cJSON_Delete(usage);
    return 0;
}

static void assert_equal(int actual, int expected, const char *field_name, int *test_passed) {
    if (actual != expected) {
        printf("    %s✗ %s: expected %d, got %d%s\n", COLOR_RED, field_name, expected, actual, COLOR_RESET);
        *test_passed = 0;
    } else {
        printf("    %s✓ %s: %d%s\n", COLOR_GREEN, field_name, actual, COLOR_RESET);
    }
}

static void test_response(const char *name, const char *json, int ep, int ec, int et, int ecached, int eh, int em) {
    tests_run++;
    int passed = 1;
    printf("\n%s[TEST %d] %s%s\n", COLOR_CYAN, tests_run, name, COLOR_RESET);

    int p, c, t, cached, hit, miss;
    if (test_extract_tokens(json, &p, &c, &t, &cached, &hit, &miss) != 0) {
        printf("    %s✗ Failed to parse%s\n", COLOR_RED, COLOR_RESET);
        tests_failed++;
        return;
    }

    assert_equal(p, ep, "prompt_tokens", &passed);
    assert_equal(c, ec, "completion_tokens", &passed);
    assert_equal(t, et, "total_tokens", &passed);
    assert_equal(cached, ecached, "cached_tokens", &passed);
    assert_equal(hit, eh, "cache_hit_tokens", &passed);
    assert_equal(miss, em, "cache_miss_tokens", &passed);

    if (passed) {
        tests_passed++;
        printf("    %s✓ PASSED%s\n", COLOR_GREEN, COLOR_RESET);
    } else {
        tests_failed++;
    }
}

int main(void) {
    printf("\n%s=== Token Usage Tests (Multi-Provider) ===%s\n", COLOR_MAGENTA, COLOR_RESET);
    printf("%sThis test verifies token parsing for all supported API providers.%s\n\n", COLOR_GREEN, COLOR_RESET);

    // Anthropic
    test_response("Anthropic - No cache",
        "{\"input_tokens\": 34122, \"output_tokens\": 106}",
        34122, 106, 0, 0, 0, 0);

    test_response("Anthropic - With cache",
        "{\"input_tokens\": 5454, \"cache_read_input_tokens\": 3000, \"output_tokens\": 69}",
        5454, 69, 0, 3000, 0, 0);

    // AWS Bedrock
    test_response("AWS Bedrock",
        "{\"input_tokens\": 15382, \"output_tokens\": 145}",
        15382, 145, 0, 0, 0, 0);

    // OpenAI
    test_response("OpenAI",
        "{\"prompt_tokens\": 100, \"completion_tokens\": 50, \"total_tokens\": 150}",
        100, 50, 150, 0, 0, 0);

    // DeepSeek
    test_response("DeepSeek - With cache",
        "{\"prompt_tokens\": 37667, \"completion_tokens\": 25, \"total_tokens\": 37692, \"prompt_tokens_details\": {\"cached_tokens\": 37632}, \"prompt_cache_hit_tokens\": 37632, \"prompt_cache_miss_tokens\": 35}",
        37667, 25, 37692, 37632, 37632, 35);

    test_response("DeepSeek - No cache",
        "{\"prompt_tokens\": 2000, \"completion_tokens\": 300, \"total_tokens\": 2300}",
        2000, 300, 2300, 0, 0, 0);

    // Moonshot
    test_response("Moonshot - With cache",
        "{\"prompt_tokens\": 1551, \"completion_tokens\": 232, \"total_tokens\": 1783, \"cached_tokens\": 768}",
        1551, 232, 1783, 768, 0, 0);

    test_response("Moonshot - No cache",
        "{\"prompt_tokens\": 500, \"completion_tokens\": 100, \"total_tokens\": 600}",
        500, 100, 600, 0, 0, 0);

    // Edge cases
    test_response("Minimal response",
        "{\"prompt_tokens\": 10, \"completion_tokens\": 5}",
        10, 5, 0, 0, 0, 0);

    test_response("Large numbers",
        "{\"prompt_tokens\": 1000000, \"completion_tokens\": 50000, \"total_tokens\": 1050000}",
        1000000, 50000, 1050000, 0, 0, 0);

    printf("\n%s=== Summary ===%s\n", COLOR_MAGENTA, COLOR_RESET);
    printf("Tests run: %d\n", tests_run);
    printf("%sPassed: %d%s\n", COLOR_GREEN, tests_passed, COLOR_RESET);
    printf("%sFailed: %d%s\n", tests_failed > 0 ? COLOR_RED : COLOR_GREEN, tests_failed, COLOR_RESET);

    if (tests_failed == 0) {
        printf("\n%s✓ All tests passed!%s\n\n", COLOR_GREEN, COLOR_RESET);
        return 0;
    } else {
        printf("\n%s✗ Some tests failed!%s\n\n", COLOR_RED, COLOR_RESET);
        return 1;
    }
}
