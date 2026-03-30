/*
 * Test suite for model_capabilities module
 *
 * Tests model-specific capability lookup and safe max_tokens calculation.
 *
 * Compilation: make test-model-capabilities
 * Usage: ./build/test_model_capabilities
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/model_capabilities.h"

// Test helper macros
#define TEST(name) printf("\n=== Test: %s ===\n", name)
#define PASS() printf("✓ PASS\n")
#define FAIL(msg) do { printf("✗ FAIL: %s\n", msg); exit(1); } while(0)

// =============================================================================
// Test get_model_capabilities()
// =============================================================================

static void test_null_model_returns_defaults(void) {
    TEST("NULL model returns default values");

    ModelCapabilities caps = get_model_capabilities(NULL, 128000, 16384);

    assert(caps.context_limit == 128000);
    assert(caps.max_output_tokens == 16384);

    PASS();
}

static void test_empty_model_returns_defaults(void) {
    TEST("Empty string model returns default values");

    ModelCapabilities caps = get_model_capabilities("", 128000, 16384);

    assert(caps.context_limit == 128000);
    assert(caps.max_output_tokens == 16384);

    PASS();
}

static void test_exact_match_gpt4o(void) {
    TEST("Exact match for gpt-4o");

    ModelCapabilities caps = get_model_capabilities("gpt-4o", 128000, 16384);

    assert(caps.context_limit == 128000);
    assert(caps.max_output_tokens == 16384);

    PASS();
}

static void test_prefix_match_gpt4(void) {
    TEST("Prefix match for gpt-4 (matches 'gpt-4' entry)");

    ModelCapabilities caps = get_model_capabilities("gpt-4", 128000, 16384);

    // gpt-4 entry should match, not gpt-4o or others
    assert(caps.context_limit == 8191);
    assert(caps.max_output_tokens == 4096);

    PASS();
}

static void test_prefix_match_gpt4_turbo(void) {
    TEST("Prefix match for gpt-4-turbo");

    ModelCapabilities caps = get_model_capabilities("gpt-4-turbo", 128000, 16384);

    assert(caps.context_limit == 128000);
    assert(caps.max_output_tokens == 4096);

    PASS();
}

static void test_case_insensitive_match(void) {
    TEST("Case-insensitive prefix matching");

    ModelCapabilities caps_lower = get_model_capabilities("gpt-4o", 128000, 16384);
    ModelCapabilities caps_upper = get_model_capabilities("GPT-4O", 128000, 16384);
    ModelCapabilities caps_mixed = get_model_capabilities("Gpt-4O", 128000, 16384);

    assert(caps_lower.context_limit == caps_upper.context_limit);
    assert(caps_lower.max_output_tokens == caps_upper.max_output_tokens);
    assert(caps_lower.context_limit == caps_mixed.context_limit);
    assert(caps_lower.max_output_tokens == caps_mixed.max_output_tokens);

    PASS();
}

static void test_unknown_model_returns_defaults(void) {
    TEST("Unknown model returns default values");

    ModelCapabilities caps = get_model_capabilities("unknown-model-xyz", 128000, 16384);

    assert(caps.context_limit == 128000);
    assert(caps.max_output_tokens == 16384);

    PASS();
}

static void test_claude_opus_match(void) {
    TEST("Match for claude-opus-4");

    ModelCapabilities caps = get_model_capabilities("claude-opus-4", 200000, 32000);

    assert(caps.context_limit == 200000);
    assert(caps.max_output_tokens == 32000);

    PASS();
}

static void test_claude_sonnet_46_match(void) {
    TEST("Match for claude-sonnet-4.6");

    ModelCapabilities caps = get_model_capabilities("claude-sonnet-4.6", 200000, 32000);

    assert(caps.context_limit == 1000000);
    assert(caps.max_output_tokens == 128000);

    PASS();
}

static void test_gemini_match(void) {
    TEST("Match for gemini-2.5-pro");

    ModelCapabilities caps = get_model_capabilities("gemini-2.5-pro", 128000, 8192);

    assert(caps.context_limit == 1048576);
    assert(caps.max_output_tokens == 65536);

    PASS();
}

static void test_gpt54_pro_match(void) {
    TEST("Match for gpt-5.4-pro (high context model)");

    ModelCapabilities caps = get_model_capabilities("gpt-5.4-pro", 128000, 16384);

    assert(caps.context_limit == 1050000);
    assert(caps.max_output_tokens == 128000);

    PASS();
}

// =============================================================================
// Test get_model_max_tokens()
// =============================================================================

static void test_get_model_max_tokens_known(void) {
    TEST("get_model_max_tokens returns correct value for known model");

    int max_tokens = get_model_max_tokens("gpt-4o", 16384);

    assert(max_tokens == 16384);

    PASS();
}

static void test_get_model_max_tokens_unknown(void) {
    TEST("get_model_max_tokens returns default for unknown model");

    int max_tokens = get_model_max_tokens("unknown-model", 8192);

    assert(max_tokens == 8192);

    PASS();
}

static void test_get_model_max_tokens_null(void) {
    TEST("get_model_max_tokens returns default for NULL model");

    int max_tokens = get_model_max_tokens(NULL, 4096);

    assert(max_tokens == 4096);

    PASS();
}

static void test_get_model_max_tokens_o_series(void) {
    TEST("get_model_max_tokens for o-series reasoning models");

    int max_tokens = get_model_max_tokens("o3-mini", 16384);

    assert(max_tokens == 100000);

    PASS();
}

// =============================================================================
// Test get_model_context_limit()
// =============================================================================

static void test_get_model_context_limit_known(void) {
    TEST("get_model_context_limit returns correct value for known model");

    int context_limit = get_model_context_limit("gpt-4o", 128000);

    assert(context_limit == 128000);

    PASS();
}

static void test_get_model_context_limit_unknown(void) {
    TEST("get_model_context_limit returns default for unknown model");

    int context_limit = get_model_context_limit("unknown-model", 64000);

    assert(context_limit == 64000);

    PASS();
}

static void test_get_model_context_limit_claude(void) {
    TEST("get_model_context_limit for Claude models");

    int context_limit = get_model_context_limit("claude-opus-4", 128000);

    assert(context_limit == 200000);

    PASS();
}

// =============================================================================
// Test get_safe_max_tokens()
// =============================================================================

static void test_get_safe_max_tokens_no_capping(void) {
    TEST("get_safe_max_tokens returns original when no capping needed");

    // gpt-4o has 128k context limit
    // prompt=1000, buffer=1000, original=16000
    // remaining = 128000 - 1000 - 1000 = 126000
    // 126000 > 16000, so return original
    int safe_max = get_safe_max_tokens("gpt-4o", 1000, 16000, 1000);

    assert(safe_max == 16000);

    PASS();
}

static void test_get_safe_max_tokens_capping_needed(void) {
    TEST("get_safe_max_tokens caps when remaining < original");

    // gpt-4 has 8191 context limit
    // prompt=7000, buffer=500, original=4096
    // remaining = 8191 - 7000 - 500 = 691
    // 691 < 4096, so cap to 691
    int safe_max = get_safe_max_tokens("gpt-4", 7000, 4096, 500);

    assert(safe_max == 691);

    PASS();
}

static void test_get_safe_max_tokens_context_full(void) {
    TEST("get_safe_max_tokens returns 0 when context is full");

    // gpt-4 has 8191 context limit
    // prompt=7000, buffer=1500, original=4096
    // remaining = 8191 - 7000 - 1500 = -309
    // remaining <= 0, so return 0
    int safe_max = get_safe_max_tokens("gpt-4", 7000, 4096, 1500);

    assert(safe_max == 0);

    PASS();
}

static void test_get_safe_max_tokens_exact_fit(void) {
    TEST("get_safe_max_tokens returns exact remaining when it equals original");

    // gpt-4 has 8191 context limit
    // prompt=4000, buffer=191, original=4000
    // remaining = 8191 - 4000 - 191 = 4000
    // 4000 == 4000, so return 4000
    int safe_max = get_safe_max_tokens("gpt-4", 4000, 4000, 191);

    assert(safe_max == 4000);

    PASS();
}

static void test_get_safe_max_tokens_zero_buffer(void) {
    TEST("get_safe_max_tokens with zero buffer");

    // gpt-4 has 8191 context limit
    // prompt=7000, buffer=0, original=2000
    // remaining = 8191 - 7000 - 0 = 1191
    // 1191 < 2000, so cap to 1191
    int safe_max = get_safe_max_tokens("gpt-4", 7000, 2000, 0);

    assert(safe_max == 1191);

    PASS();
}

static void test_get_safe_max_tokens_unknown_model(void) {
    TEST("get_safe_max_tokens uses safe default for unknown model");

    // Unknown model uses 128000 default context limit
    // prompt=100000, buffer=10000, original=50000
    // remaining = 128000 - 100000 - 10000 = 18000
    // 18000 < 50000, so cap to 18000
    int safe_max = get_safe_max_tokens("unknown-model", 100000, 50000, 10000);

    assert(safe_max == 18000);

    PASS();
}

static void test_get_safe_max_tokens_large_context_model(void) {
    TEST("get_safe_max_tokens for large context model (gpt-5.4-pro)");

    // gpt-5.4-pro has 1050000 context limit
    // prompt=100000, buffer=50000, original=128000
    // remaining = 1050000 - 100000 - 50000 = 900000
    // 900000 > 128000, so return original
    int safe_max = get_safe_max_tokens("gpt-5.4-pro", 100000, 128000, 50000);

    assert(safe_max == 128000);

    PASS();
}

// =============================================================================
// Test prefix matching edge cases
// =============================================================================

static void test_prefix_matching_order_matters(void) {
    TEST("Prefix matching order - more specific prefix wins");

    // "gpt-4o-mini" should match "gpt-4o-mini" entry, not "gpt-4o"
    ModelCapabilities caps_mini = get_model_capabilities("gpt-4o-mini", 128000, 16384);

    // gpt-4o-mini should have 128000 context limit
    assert(caps_mini.context_limit == 128000);
    assert(caps_mini.max_output_tokens == 16384);

    // Regular gpt-4o also has 128000 context
    ModelCapabilities caps_regular = get_model_capabilities("gpt-4o", 128000, 16384);
    assert(caps_regular.context_limit == 128000);

    PASS();
}

static void test_prefix_matching_longer_names(void) {
    TEST("Prefix matching with longer model names");

    // Test with date suffix - should match base prefix
    ModelCapabilities caps = get_model_capabilities("gpt-4o-2024-08-06", 128000, 16384);

    assert(caps.context_limit == 128000);
    assert(caps.max_output_tokens == 16384);

    PASS();
}

// =============================================================================
// Main test runner
// =============================================================================

int main(void) {
    printf("========================================\n");
    printf("Model Capabilities Test Suite\n");
    printf("========================================\n");

    // Test get_model_capabilities()
    test_null_model_returns_defaults();
    test_empty_model_returns_defaults();
    test_exact_match_gpt4o();
    test_prefix_match_gpt4();
    test_prefix_match_gpt4_turbo();
    test_case_insensitive_match();
    test_unknown_model_returns_defaults();
    test_claude_opus_match();
    test_claude_sonnet_46_match();
    test_gemini_match();
    test_gpt54_pro_match();

    // Test get_model_max_tokens()
    test_get_model_max_tokens_known();
    test_get_model_max_tokens_unknown();
    test_get_model_max_tokens_null();
    test_get_model_max_tokens_o_series();

    // Test get_model_context_limit()
    test_get_model_context_limit_known();
    test_get_model_context_limit_unknown();
    test_get_model_context_limit_claude();

    // Test get_safe_max_tokens()
    test_get_safe_max_tokens_no_capping();
    test_get_safe_max_tokens_capping_needed();
    test_get_safe_max_tokens_context_full();
    test_get_safe_max_tokens_exact_fit();
    test_get_safe_max_tokens_zero_buffer();
    test_get_safe_max_tokens_unknown_model();
    test_get_safe_max_tokens_large_context_model();

    // Test prefix matching edge cases
    test_prefix_matching_order_matters();
    test_prefix_matching_longer_names();

    printf("\n========================================\n");
    printf("All tests passed! ✓\n");
    printf("========================================\n");

    return 0;
}
