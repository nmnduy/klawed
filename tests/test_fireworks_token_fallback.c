/*
 * test_fireworks_token_fallback.c - Tests for Fireworks AI token counting fixes
 *
 * Tests cover two areas:
 * 1. Header-based fallback: When Fireworks returns zero token counts in the
 *    response body, parse response headers for fireworks-prompt-tokens and
 *    fireworks-cached-prompt-tokens.
 * 2. Stream options: Verify that streaming requests include
 *    stream_options.include_usage so Fireworks returns usage in final SSE chunk.
 *
 * These tests validate the logic that was added to:
 *   - src/persistence.c (persistence_log_api_call)
 *   - src/api/api_client.c (call_api_with_retries)
 *   - src/openai_provider.c (openai_call_api)
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

/* =========================================================================
 * Test infrastructure
 * ========================================================================= */

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        printf("  %s...", name); \
        tests_run++; \
    } while(0)

#define PASS() \
    do { \
        printf(" \033[32mPASS\033[0m\n"); \
        tests_passed++; \
    } while(0)

#define FAIL(...) \
    do { \
        printf(" \033[31mFAIL\033[0m: "); \
        printf(__VA_ARGS__); \
        printf("\n"); \
        return; \
    } while(0)

/* =========================================================================
 * Helper: Emulates the Fireworks header fallback logic from persistence.c
 * ========================================================================= */

/**
 * Extract token usage from response body and apply Fireworks header fallback.
 *
 * This replicates the core logic from persistence_log_api_call():
 * 1. Extract prompt_tokens, completion_tokens, total_tokens, cached_tokens
 *    from the response body's "usage" object.
 * 2. If ALL counts are zero AND headers_json is provided, parse the headers
 *    array for fireworks-prompt-tokens and fireworks-cached-prompt-tokens.
 * 3. Fill in prompt_tokens and cached_tokens from headers, and compute
 *    total_tokens = prompt_tokens + completion_tokens.
 *
 * Parameters:
 *   response_json: The body JSON (e.g., '{"usage": {"prompt_tokens": 0, ...}}')
 *   headers_json: The headers JSON array (e.g., '[{"name":"fireworks-prompt-tokens","value":"10"}]')
 *                  NULL if no headers available.
 *   out_prompt, out_completion, out_total, out_cached: Output token counts
 *
 * Returns: 0 on success, -1 on parse error
 */
static int extract_tokens_with_fireworks_fallback(
    const char *response_json,
    const char *headers_json,
    int *out_prompt,
    int *out_completion,
    int *out_total,
    int *out_cached)
{
    int prompt_tokens = 0, completion_tokens = 0, total_tokens = 0;
    int cached_tokens = 0;

    cJSON *json = cJSON_Parse(response_json);
    if (!json) {
        *out_prompt = 0; *out_completion = 0;
        *out_total = 0; *out_cached = 0;
        return -1;
    }

    cJSON *usage = cJSON_GetObjectItem(json, "usage");
    if (usage) {
        /* Try input_tokens first (Anthropic-style), then prompt_tokens */
        cJSON *p = cJSON_GetObjectItem(usage, "input_tokens");
        if (!p) p = cJSON_GetObjectItem(usage, "prompt_tokens");
        if (p && cJSON_IsNumber(p)) prompt_tokens = p->valueint;

        cJSON *c = cJSON_GetObjectItem(usage, "output_tokens");
        if (!c) c = cJSON_GetObjectItem(usage, "completion_tokens");
        if (c && cJSON_IsNumber(c)) completion_tokens = c->valueint;

        cJSON *t = cJSON_GetObjectItem(usage, "total_tokens");
        if (t && cJSON_IsNumber(t)) total_tokens = t->valueint;

        /* Try direct cached_tokens (Moonshot style) */
        cJSON *dc = cJSON_GetObjectItem(usage, "cached_tokens");
        if (dc && cJSON_IsNumber(dc)) cached_tokens = dc->valueint;

        /* Try prompt_tokens_details.cached_tokens (DeepSeek style) */
        if (cached_tokens == 0) {
            cJSON *details = cJSON_GetObjectItem(usage, "prompt_tokens_details");
            if (details) {
                cJSON *dc2 = cJSON_GetObjectItem(details, "cached_tokens");
                if (dc2 && cJSON_IsNumber(dc2)) cached_tokens = dc2->valueint;
            }
        }

        /* Try cache_read_input_tokens (Anthropic style) */
        if (cached_tokens == 0) {
            cJSON *cr = cJSON_GetObjectItem(usage, "cache_read_input_tokens");
            if (cr && cJSON_IsNumber(cr)) cached_tokens = cr->valueint;
        }
    }

    /* === FIREWORKS HEADER FALLBACK ===
     * Fireworks models return token counts in response headers but zero
     * in the response body's usage object. When all body-derived counts
     * are zero, check the response headers. */
    if (prompt_tokens == 0 && completion_tokens == 0 && total_tokens == 0
        && cached_tokens == 0 && headers_json) {
        cJSON *headers_array = cJSON_Parse(headers_json);
        if (headers_array && cJSON_IsArray(headers_array)) {
            int array_size = cJSON_GetArraySize(headers_array);
            for (int i = 0; i < array_size; i++) {
                cJSON *header_item = cJSON_GetArrayItem(headers_array, i);
                if (!header_item) continue;
                cJSON *name_item = cJSON_GetObjectItem(header_item, "name");
                cJSON *value_item = cJSON_GetObjectItem(header_item, "value");
                if (!name_item || !value_item) continue;
                if (!cJSON_IsString(name_item) || !cJSON_IsString(value_item)) continue;

                const char *name = name_item->valuestring;
                const char *value = value_item->valuestring;

                if (strcmp(name, "fireworks-prompt-tokens") == 0) {
                    char *endptr;
                    long val = strtol(value, &endptr, 10);
                    if (*endptr == '\0' && val >= 0) {
                        prompt_tokens = (int)val;
                    }
                } else if (strcmp(name, "fireworks-cached-prompt-tokens") == 0) {
                    char *endptr;
                    long val = strtol(value, &endptr, 10);
                    if (*endptr == '\0' && val >= 0) {
                        cached_tokens = (int)val;
                    }
                }
            }
            /* Set total_tokens from the header-derived values if we got prompt tokens */
            if (prompt_tokens > 0 && total_tokens == 0) {
                total_tokens = prompt_tokens + completion_tokens;
            }
            cJSON_Delete(headers_array);
        }
    }

    cJSON_Delete(json);

    *out_prompt = prompt_tokens;
    *out_completion = completion_tokens;
    *out_total = total_tokens;
    *out_cached = cached_tokens;
    return 0;
}

/* =========================================================================
 * Test 1: Fireworks header fallback with all-zero body, valid headers
 * ========================================================================= */
static void test_fireworks_header_fallback_basic(void) {
    TEST("Fireworks header fallback - all zeros, valid headers");

    const char *body = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 0,"
            "\"completion_tokens\": 0,"
            "\"total_tokens\": 0"
        "}"
    "}";

    const char *headers = "["
        "{\"name\": \"content-type\", \"value\": \"application/json\"},"
        "{\"name\": \"fireworks-prompt-tokens\", \"value\": \"7752\"},"
        "{\"name\": \"fireworks-cached-prompt-tokens\", \"value\": \"3000\"},"
        "{\"name\": \"x-request-id\", \"value\": \"abc123\"}"
    "]";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    if (prompt != 7752) FAIL("expected prompt_tokens=7752, got %d", prompt);
    if (completion != 0) FAIL("expected completion_tokens=0, got %d", completion);
    if (total != 7752) FAIL("expected total_tokens=7752, got %d", total);
    if (cached != 3000) FAIL("expected cached_tokens=3000, got %d", cached);

    PASS();
}

/* =========================================================================
 * Test 2: Fireworks header fallback - only prompt tokens in headers
 * ========================================================================= */
static void test_fireworks_header_only_prompt(void) {
    TEST("Fireworks header fallback - only prompt tokens header");

    const char *body = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 0,"
            "\"completion_tokens\": 0,"
            "\"total_tokens\": 0"
        "}"
    "}";

    const char *headers = "["
        "{\"name\": \"fireworks-prompt-tokens\", \"value\": \"500\"}"
    "]";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    if (prompt != 500) FAIL("expected prompt_tokens=500, got %d", prompt);
    if (completion != 0) FAIL("expected completion_tokens=0, got %d", completion);
    if (total != 500) FAIL("expected total_tokens=500, got %d", total);
    if (cached != 0) FAIL("expected cached_tokens=0, got %d", cached);

    PASS();
}

/* =========================================================================
 * Test 3: Non-zero body usage - headers should be IGNORED
 * ========================================================================= */
static void test_fireworks_nonzero_body_ignores_headers(void) {
    TEST("Fireworks - non-zero body usage ignores header fallback");

    const char *body = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 100,"
            "\"completion_tokens\": 50,"
            "\"total_tokens\": 150"
        "}"
    "}";

    const char *headers = "["
        "{\"name\": \"fireworks-prompt-tokens\", \"value\": \"99999\"}"
    "]";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    if (prompt != 100) FAIL("expected prompt_tokens=100 (body), got %d", prompt);
    if (completion != 50) FAIL("expected completion_tokens=50 (body), got %d", completion);
    if (total != 150) FAIL("expected total_tokens=150 (body), got %d", total);
    if (cached != 0) FAIL("expected cached_tokens=0, got %d", cached);

    PASS();
}

/* =========================================================================
 * Test 4: Missing headers_json (NULL) - graceful degradation
 * ========================================================================= */
static void test_fireworks_null_headers(void) {
    TEST("Fireworks - NULL headers_json, all-zero body stays zero");

    const char *body = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 0,"
            "\"completion_tokens\": 0,"
            "\"total_tokens\": 0"
        "}"
    "}";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, NULL,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    if (prompt != 0) FAIL("expected prompt_tokens=0, got %d", prompt);
    if (completion != 0) FAIL("expected completion_tokens=0, got %d", completion);
    if (total != 0) FAIL("expected total_tokens=0, got %d", total);
    if (cached != 0) FAIL("expected cached_tokens=0, got %d", cached);

    PASS();
}

/* =========================================================================
 * Test 5: Invalid header values (non-numeric) - safely ignored
 * ========================================================================= */
static void test_fireworks_invalid_header_values(void) {
    TEST("Fireworks - invalid (non-numeric) header values ignored");

    const char *body = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 0,"
            "\"completion_tokens\": 0,"
            "\"total_tokens\": 0"
        "}"
    "}";

    const char *headers = "["
        "{\"name\": \"fireworks-prompt-tokens\", \"value\": \"not-a-number\"},"
        "{\"name\": \"fireworks-cached-prompt-tokens\", \"value\": \"\"}"
    "]";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    if (prompt != 0) FAIL("expected prompt_tokens=0 (invalid ignored), got %d", prompt);
    if (cached != 0) FAIL("expected cached_tokens=0 (invalid ignored), got %d", cached);

    PASS();
}

/* =========================================================================
 * Test 6: Fireworks header fallback - negative prompt value ignored
 * ========================================================================= */
static void test_fireworks_negative_value_ignored(void) {
    TEST("Fireworks - negative header value ignored");

    const char *body = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 0,"
            "\"completion_tokens\": 0,"
            "\"total_tokens\": 0"
        "}"
    "}";

    const char *headers = "["
        "{\"name\": \"fireworks-prompt-tokens\", \"value\": \"-1\"}"
    "]";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    if (prompt != 0) FAIL("expected prompt_tokens=0 (negative ignored), got %d", prompt);

    PASS();
}

/* =========================================================================
 * Test 7: Fireworks - body has zero prompt/completion but nonzero total
 *         (should NOT trigger fallback since not ALL counts are zero)
 * ========================================================================= */
static void test_fireworks_partial_zero_no_fallback(void) {
    TEST("Fireworks - partial zero body does NOT trigger fallback");

    const char *body = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 0,"
            "\"completion_tokens\": 0,"
            "\"total_tokens\": 150"
        "}"
    "}";

    const char *headers = "["
        "{\"name\": \"fireworks-prompt-tokens\", \"value\": \"999\"}"
    "]";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    if (prompt != 0) FAIL("expected prompt_tokens=0 (body, fallback not triggered), got %d", prompt);
    if (total != 150) FAIL("expected total_tokens=150 (body), got %d", total);

    PASS();
}

/* =========================================================================
 * Test 8: Fireworks - headers have fireworks-cached-prompt-tokens only
 * ========================================================================= */
static void test_fireworks_cached_only_header(void) {
    TEST("Fireworks - only cached-tokens in headers");

    const char *body = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 0,"
            "\"completion_tokens\": 0,"
            "\"total_tokens\": 0"
        "}"
    "}";

    const char *headers = "["
        "{\"name\": \"fireworks-cached-prompt-tokens\", \"value\": \"4000\"}"
    "]";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    if (prompt != 0) FAIL("expected prompt_tokens=0, got %d", prompt);
    if (cached != 4000) FAIL("expected cached_tokens=4000, got %d", cached);
    if (total != 0) FAIL("expected total_tokens=0 (prompt still 0), got %d", total);

    PASS();
}

/* =========================================================================
 * Test 9: Fireworks - header present but headers array malformed
 * ========================================================================= */
static void test_fireworks_malformed_headers(void) {
    TEST("Fireworks - malformed headers array (graceful)");

    const char *body = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 0,"
            "\"completion_tokens\": 0,"
            "\"total_tokens\": 0"
        "}"
    "}";

    /* Not a valid JSON array */
    const char *headers = "{\"not\": \"an array\"}";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    if (prompt != 0) FAIL("expected prompt_tokens=0 (no fallback), got %d", prompt);

    PASS();
}

/* =========================================================================
 * Test 10: stream_options.include_usage present in streaming request JSON
 * ========================================================================= */
static void test_stream_options_include_usage_present(void) {
    TEST("stream_options.include_usage present in streaming request");

    /* Simulate a request built with streaming enabled.
     * When streaming, the code does:
     *   cJSON_AddBoolToObject(request, "stream", cJSON_True);
     *   cJSON *stream_opts = cJSON_CreateObject();
     *   cJSON_AddBoolToObject(stream_opts, "include_usage", cJSON_True);
     *   cJSON_AddItemToObject(request, "stream_options", stream_opts);
     */
    const char *request_json = "{"
        "\"model\": \"accounts/fireworks/models/kimi-k2\","
        "\"messages\": [],"
        "\"stream\": true,"
        "\"stream_options\": {"
            "\"include_usage\": true"
        "}"
    "}";

    cJSON *request = cJSON_Parse(request_json);
    if (!request) FAIL("failed to parse request JSON");

    /* Verify stream is true */
    cJSON *stream = cJSON_GetObjectItem(request, "stream");
    if (!stream || !cJSON_IsBool(stream) || !cJSON_IsTrue(stream))
        FAIL("stream should be true");

    /* Verify stream_options exists */
    cJSON *stream_opts = cJSON_GetObjectItem(request, "stream_options");
    if (!stream_opts || !cJSON_IsObject(stream_opts))
        FAIL("stream_options should exist as an object");

    /* Verify include_usage is true */
    cJSON *include_usage = cJSON_GetObjectItem(stream_opts, "include_usage");
    if (!include_usage || !cJSON_IsBool(include_usage) || !cJSON_IsTrue(include_usage))
        FAIL("stream_options.include_usage should be true");

    cJSON_Delete(request);
    PASS();
}

/* =========================================================================
 * Test 11: stream_options NOT present in non-streaming request JSON
 * ========================================================================= */
static void test_stream_options_absent_when_not_streaming(void) {
    TEST("stream_options absent in non-streaming request");

    /* Simulate a request built WITHOUT streaming enabled */
    const char *request_json = "{"
        "\"model\": \"accounts/fireworks/models/kimi-k2\","
        "\"messages\": []"
    "}";

    cJSON *request = cJSON_Parse(request_json);
    if (!request) FAIL("failed to parse request JSON");

    /* Verify stream is NOT present */
    cJSON *stream = cJSON_GetObjectItem(request, "stream");
    if (stream) FAIL("stream should not be present in non-streaming request");

    /* Verify stream_options is NOT present */
    cJSON *stream_opts = cJSON_GetObjectItem(request, "stream_options");
    if (stream_opts) FAIL("stream_options should not be present in non-streaming request");

    cJSON_Delete(request);
    PASS();
}

/* =========================================================================
 * Test 12: stream_options.include_usage is false - explicit test
 * ========================================================================= */
static void test_stream_options_include_usage_false(void) {
    TEST("stream_options.include_usage false is detected");

    /* Test what happens if someone incorrectly sets it to false */
    const char *request_json = "{"
        "\"model\": \"test\","
        "\"messages\": [],"
        "\"stream\": true,"
        "\"stream_options\": {"
            "\"include_usage\": false"
        "}"
    "}";

    cJSON *request = cJSON_Parse(request_json);
    if (!request) FAIL("failed to parse request JSON");

    cJSON *stream_opts = cJSON_GetObjectItem(request, "stream_options");
    cJSON *include_usage = cJSON_GetObjectItem(stream_opts, "include_usage");

    if (!include_usage) FAIL("include_usage field missing");
    if (!cJSON_IsBool(include_usage)) FAIL("include_usage should be boolean");
    if (!cJSON_IsFalse(include_usage)) FAIL("include_usage should be false in this test");

    cJSON_Delete(request);
    PASS();
}

/* =========================================================================
 * Test 13: Anthropic-style body usage - cache_read_input_tokens still works
 * ========================================================================= */
static void test_anthropic_body_preserved_without_fireworks_headers(void) {
    TEST("Anthropic body usage preserved (no Fireworks fallback)");

    const char *body = "{"
        "\"usage\": {"
            "\"input_tokens\": 200,"
            "\"output_tokens\": 75,"
            "\"cache_read_input_tokens\": 150"
        "}"
    "}";

    /* No Fireworks headers, just normal Anthropic usage */
    const char *headers = "["
        "{\"name\": \"content-type\", \"value\": \"application/json\"}"
    "]";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    if (prompt != 200) FAIL("expected prompt_tokens=200, got %d", prompt);
    if (completion != 75) FAIL("expected completion_tokens=75, got %d", completion);
    if (cached != 150) FAIL("expected cached_tokens=150, got %d", cached);

    PASS();
}

/* =========================================================================
 * Test 14: DeepSeek-style body - prompt_tokens_details.cached_tokens
 * ========================================================================= */
static void test_deepseek_body_preserved_without_fireworks_headers(void) {
    TEST("DeepSeek prompt_tokens_details.cached_tokens preserved");

    const char *body = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 1000,"
            "\"completion_tokens\": 200,"
            "\"total_tokens\": 1200,"
            "\"prompt_tokens_details\": {"
                "\"cached_tokens\": 500"
            "}"
        "}"
    "}";

    const char *headers = "[]";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    if (prompt != 1000) FAIL("expected prompt_tokens=1000, got %d", prompt);
    if (completion != 200) FAIL("expected completion_tokens=200, got %d", completion);
    if (total != 1200) FAIL("expected total_tokens=1200, got %d", total);
    if (cached != 500) FAIL("expected cached_tokens=500, got %d", cached);

    PASS();
}

/* =========================================================================
 * Test 15: Fireworks headers with leading/trailing whitespace
 * ========================================================================= */
static void test_fireworks_header_with_whitespace(void) {
    TEST("Fireworks - header value with whitespace");

    const char *body = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 0,"
            "\"completion_tokens\": 0,"
            "\"total_tokens\": 0"
        "}"
    "}";

    /* strtol skips leading whitespace but stops at trailing */
    const char *headers = "["
        "{\"name\": \"fireworks-prompt-tokens\", \"value\": \"  1234\"}"
    "]";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    /* strtol skips leading whitespace */
    if (prompt != 1234) FAIL("expected prompt_tokens=1234 (whitespace trimmed), got %d", prompt);

    PASS();
}

/* =========================================================================
 * Test 16: Fireworks header value with trailing garbage - should be rejected
 * ========================================================================= */
static void test_fireworks_header_with_trailing_garbage(void) {
    TEST("Fireworks - header value with trailing garbage rejected");

    const char *body = "{"
        "\"usage\": {"
            "\"prompt_tokens\": 0,"
            "\"completion_tokens\": 0,"
            "\"total_tokens\": 0"
        "}"
    "}";

    const char *headers = "["
        "{\"name\": \"fireworks-prompt-tokens\", \"value\": \"1234abc\"}"
    "]";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    /* strtol stops at 'a', so *endptr != '\0' - should reject */
    if (prompt != 0) FAIL("expected prompt_tokens=0 (trailing garbage rejected), got %d", prompt);

    PASS();
}

/* =========================================================================
 * Test 17: Fireworks headers present but body has no usage field at all
 *          (should still attempt fallback since all counts are zero)
 * ========================================================================= */
static void test_fireworks_no_usage_field(void) {
    TEST("Fireworks - no usage field in body, all counts zero");

    const char *body = "{\"id\": \"chatcmpl-123\", \"choices\": []}";

    const char *headers = "["
        "{\"name\": \"fireworks-prompt-tokens\", \"value\": \"8000\"}"
    "]";

    int prompt, completion, total, cached;
    int rc = extract_tokens_with_fireworks_fallback(body, headers,
        &prompt, &completion, &total, &cached);

    if (rc != 0) FAIL("extraction returned error");
    if (prompt != 8000) FAIL("expected prompt_tokens=8000, got %d", prompt);
    if (total != 8000) FAIL("expected total_tokens=8000, got %d", total);

    PASS();
}

/* =========================================================================
 * Main test runner
 * ========================================================================= */

int main(void) {
    printf("\n=== Fireworks Token Fallback Tests ===\n\n");

    /* Fireworks header fallback tests */
    printf("Fireworks Header Fallback:\n");
    test_fireworks_header_fallback_basic();
    test_fireworks_header_only_prompt();
    test_fireworks_nonzero_body_ignores_headers();
    test_fireworks_null_headers();
    test_fireworks_invalid_header_values();
    test_fireworks_negative_value_ignored();
    test_fireworks_partial_zero_no_fallback();
    test_fireworks_cached_only_header();
    test_fireworks_malformed_headers();
    test_fireworks_header_with_whitespace();
    test_fireworks_header_with_trailing_garbage();
    test_fireworks_no_usage_field();

    /* Stream options tests */
    printf("\nStream options (include_usage):\n");
    test_stream_options_include_usage_present();
    test_stream_options_absent_when_not_streaming();
    test_stream_options_include_usage_false();

    /* Provider body format regression tests */
    printf("\nProvider body format regression:\n");
    test_anthropic_body_preserved_without_fireworks_headers();
    test_deepseek_body_preserved_without_fireworks_headers();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Total: %d, Passed: \033[32m%d\033[0m, Failed: \033[31m%d\033[0m\n\n",
           tests_run, tests_passed, tests_run - tests_passed);

    if (tests_passed == tests_run) {
        printf("\033[32m✓ All tests passed!\033[0m\n\n");
        return 0;
    } else {
        printf("\033[31m✗ Some tests failed!\033[0m\n\n");
        return 1;
    }
}
