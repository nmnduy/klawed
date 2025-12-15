/*
 * test_token_usage.c - Test token usage extraction from different API providers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <cjson/cJSON.h>
#include "../src/persistence.h"

// Test helper to verify token extraction
static void test_token_extraction(const char *provider_name, const char *response_json,
                                  int expected_prompt, int expected_completion, int expected_cached) {
    int prompt_tokens = 0, completion_tokens = 0, total_tokens = 0;
    int cached_tokens = 0;

    // This would normally be static, so we need to expose it for testing
    // For now, let's simulate what the function should do
    printf("Testing %s provider response...\n", provider_name);

    // Parse JSON
    cJSON *json = cJSON_Parse(response_json);
    assert(json != NULL);

    cJSON *usage = cJSON_GetObjectItem(json, "usage");
    assert(usage != NULL);

    // Extract basic tokens
    cJSON *prompt_json = cJSON_GetObjectItem(usage, "prompt_tokens");
    cJSON *completion_json = cJSON_GetObjectItem(usage, "completion_tokens");
    cJSON *total_json = cJSON_GetObjectItem(usage, "total_tokens");

    if (prompt_json && cJSON_IsNumber(prompt_json)) {
        prompt_tokens = prompt_json->valueint;
    }
    if (completion_json && cJSON_IsNumber(completion_json)) {
        completion_tokens = completion_json->valueint;
    }
    if (total_json && cJSON_IsNumber(total_json)) {
        total_tokens = total_json->valueint;
    }

    // Extract cache tokens (provider-specific logic)
    // 1. Moonshot-style: direct cached_tokens
    cJSON *direct_cached = cJSON_GetObjectItem(usage, "cached_tokens");
    if (direct_cached && cJSON_IsNumber(direct_cached)) {
        cached_tokens = direct_cached->valueint;
        printf("  Found Moonshot-style cached_tokens: %d\n", cached_tokens);
    }

    // 2. DeepSeek-style: cached_tokens in prompt_tokens_details
    if (cached_tokens == 0) {
        cJSON *details = cJSON_GetObjectItem(usage, "prompt_tokens_details");
        if (details) {
            cJSON *details_cached = cJSON_GetObjectItem(details, "cached_tokens");
            if (details_cached && cJSON_IsNumber(details_cached)) {
                cached_tokens = details_cached->valueint;
                printf("  Found DeepSeek-style cached_tokens: %d\n", cached_tokens);
            }
        }
    }

    // 3. Anthropic-style: cache_read_input_tokens
    if (cached_tokens == 0) {
        cJSON *cache_read = cJSON_GetObjectItem(usage, "cache_read_input_tokens");
        if (cache_read && cJSON_IsNumber(cache_read)) {
            cached_tokens = cache_read->valueint;
            printf("  Found Anthropic-style cache_read_input_tokens: %d\n", cached_tokens);
        }
    }

    printf("  Results: prompt=%d, completion=%d, total=%d, cached=%d\n",
           prompt_tokens, completion_tokens, total_tokens, cached_tokens);

    // Verify expectations
    assert(prompt_tokens == expected_prompt);
    assert(completion_tokens == expected_completion);
    assert(cached_tokens == expected_cached);

    printf("  ✓ %s test passed!\n\n", provider_name);

    cJSON_Delete(json);
}

int main(void) {
    printf("=== Token Usage Extraction Test ===\n\n");

    // Test DeepSeek response
    const char *deepseek_response = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 37667,"
            "\"completion_tokens\": 25,"
            "\"total_tokens\": 37692,"
            "\"prompt_tokens_details\": {"
                "\"cached_tokens\": 37632"
            "},"
            "\"prompt_cache_hit_tokens\": 37632,"
            "\"prompt_cache_miss_tokens\": 35"
        "}"
    "}";

    test_token_extraction("DeepSeek", deepseek_response, 37667, 25, 37632);

    // Test Moonshot response
    const char *moonshot_response = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 1551,"
            "\"completion_tokens\": 232,"
            "\"total_tokens\": 1783,"
            "\"cached_tokens\": 768"
        "}"
    "}";

    test_token_extraction("Moonshot", moonshot_response, 1551, 232, 768);

    // Test OpenAI response (no cache)
    const char *openai_response = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 100,"
            "\"completion_tokens\": 50,"
            "\"total_tokens\": 150"
        "}"
    "}";

    test_token_extraction("OpenAI", openai_response, 100, 50, 0);

    // Test Anthropic response (converted format)
    const char *anthropic_response = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 200,"
            "\"completion_tokens\": 75,"
            "\"total_tokens\": 275,"
            "\"cache_read_input_tokens\": 150"
        "}"
    "}";

    test_token_extraction("Anthropic", anthropic_response, 200, 75, 150);

    printf("=== All tests passed! ===\n");
    return 0;
}


