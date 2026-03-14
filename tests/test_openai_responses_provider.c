/*
 * test_openai_responses_provider.c - Unit tests for the openai_responses provider
 *
 * Tests cover:
 *   - provider creation: happy path, missing key, defaults
 *   - config.c type string round-trip (to_string / from_string)
 *   - provider_init_from_config integration:
 *       * explicit api_key in config
 *       * OPENAI_API_KEY env var fallback
 *       * missing key → error
 *       * default URL used when api_base is empty
 *       * custom URL preserved
 *       * cleanup does not crash
 *
 * Compilation: make test-openai-responses-provider
 * Run:         ./build/test_openai_responses_provider
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

#include "../src/openai_responses_provider.h"
#include "../src/provider.h"
#include "../src/config.h"

/* ============================================================================
 * Minimal test framework (mirrors style used in existing test files)
 * ============================================================================ */

#define TC_RESET  "\033[0m"
#define TC_GREEN  "\033[32m"
#define TC_RED    "\033[31m"
#define TC_CYAN   "\033[36m"

static int g_tests_run    = 0;
static int g_tests_passed = 0;

#define ASSERT(cond, msg) \
    do { \
        g_tests_run++; \
        if (cond) { \
            g_tests_passed++; \
            printf(TC_GREEN "  [PASS]" TC_RESET " %s\n", (msg)); \
        } else { \
            printf(TC_RED   "  [FAIL]" TC_RESET " %s  (line %d)\n", (msg), __LINE__); \
        } \
    } while (0)

#define ASSERT_NULL(p, msg)     ASSERT((p) == NULL,  (msg))
#define ASSERT_NOT_NULL(p, msg) ASSERT((p) != NULL,  (msg))
#define ASSERT_STREQ(a, b, msg) ASSERT(strcmp((a),(b)) == 0, (msg))
#define ASSERT_CONTAINS(hay, needle, msg) \
    ASSERT((hay) != NULL && strstr((hay),(needle)) != NULL, (msg))

/* Helper: free a ProviderInitResult and its sub-resources */
static void cleanup_result(ProviderInitResult *r) {
    if (!r) return;
    if (r->provider) { r->provider->cleanup(r->provider); r->provider = NULL; }
    free(r->api_url);      r->api_url      = NULL;
    free(r->error_message); r->error_message = NULL;
}

/* Helper: build a blank LLMProviderConfig */
static void init_config(LLMProviderConfig *c) {
    memset(c, 0, sizeof(*c));
    c->provider_type = PROVIDER_OPENAI_RESPONSES;
    c->use_bedrock   = 0;
}

/* ============================================================================
 * 1. Direct factory function tests
 * ============================================================================ */

static void test_create_with_explicit_key(void) {
    printf("\nTest: openai_responses_provider_create — explicit api_key\n");

    Provider *p = openai_responses_provider_create(
        "sk-test-key-1234",
        "https://api.openai.com/v1/responses",
        "gpt-4o");

    ASSERT_NOT_NULL(p,          "provider should be created");
    ASSERT_NOT_NULL(p->config,  "config should be set");
    ASSERT_NOT_NULL(p->call_api,"call_api vtable should be set");
    ASSERT_NOT_NULL(p->cleanup, "cleanup vtable should be set");

    OpenAIResponsesProviderConfig *cfg = (OpenAIResponsesProviderConfig *)p->config;
    ASSERT_STREQ(cfg->api_key,  "sk-test-key-1234",                "api_key stored correctly");
    ASSERT_STREQ(cfg->api_base, "https://api.openai.com/v1/responses", "api_base stored correctly");
    ASSERT_STREQ(cfg->model,    "gpt-4o",                           "model stored correctly");

    p->cleanup(p);
}

static void test_create_defaults(void) {
    printf("\nTest: openai_responses_provider_create — defaults applied for NULL args\n");

    /* Ensure OPENAI_API_KEY is set so creation succeeds */
    const char *saved = getenv("OPENAI_API_KEY");
    setenv("OPENAI_API_KEY", "sk-env-default-key", 1);

    Provider *p = openai_responses_provider_create(NULL, NULL, NULL);

    ASSERT_NOT_NULL(p, "provider should be created from env key");
    if (p) {
        OpenAIResponsesProviderConfig *cfg = (OpenAIResponsesProviderConfig *)p->config;
        ASSERT_CONTAINS(cfg->api_base, "/responses", "default url contains /responses");
        ASSERT_NOT_NULL(cfg->model,                   "default model is set");
        p->cleanup(p);
    }

    if (saved) setenv("OPENAI_API_KEY", saved, 1);
    else       unsetenv("OPENAI_API_KEY");
}

static void test_create_no_key_returns_null(void) {
    printf("\nTest: openai_responses_provider_create — no key available → NULL\n");

    const char *saved = getenv("OPENAI_API_KEY");
    unsetenv("OPENAI_API_KEY");

    Provider *p = openai_responses_provider_create(NULL, NULL, NULL);
    ASSERT_NULL(p, "provider should be NULL when no API key is available");

    if (saved) setenv("OPENAI_API_KEY", saved, 1);
}

static void test_create_empty_key_falls_back_to_env(void) {
    printf("\nTest: openai_responses_provider_create — empty string key falls back to env\n");

    const char *saved = getenv("OPENAI_API_KEY");
    setenv("OPENAI_API_KEY", "sk-env-fallback", 1);

    Provider *p = openai_responses_provider_create("", NULL, NULL);
    ASSERT_NOT_NULL(p, "empty key string falls back to env var");
    if (p) p->cleanup(p);

    if (saved) setenv("OPENAI_API_KEY", saved, 1);
    else       unsetenv("OPENAI_API_KEY");
}

static void test_cleanup_is_safe(void) {
    printf("\nTest: provider cleanup does not crash or double-free\n");

    Provider *p = openai_responses_provider_create(
        "sk-cleanup-test", NULL, NULL);
    ASSERT_NOT_NULL(p, "provider created for cleanup test");
    if (p) {
        p->cleanup(p);  /* should not crash */
        ASSERT(1, "cleanup completed without crash");
    }
}

/* ============================================================================
 * 2. config.c type-string round-trip
 * ============================================================================ */

static void test_type_string_round_trip(void) {
    printf("\nTest: config type string round-trip for openai_responses\n");

    const char *str = config_provider_type_to_string(PROVIDER_OPENAI_RESPONSES);
    ASSERT_NOT_NULL(str,                              "to_string returns non-NULL");
    ASSERT_STREQ(str, "openai_responses",             "to_string is \"openai_responses\"");

    LLMProviderType t = config_provider_type_from_string("openai_responses");
    ASSERT(t == PROVIDER_OPENAI_RESPONSES,            "from_string maps back to enum");
}

static void test_type_string_unknown_does_not_match(void) {
    printf("\nTest: \"openai\" type string does not map to PROVIDER_OPENAI_RESPONSES\n");

    LLMProviderType t = config_provider_type_from_string("openai");
    ASSERT(t != PROVIDER_OPENAI_RESPONSES, "\"openai\" ≠ PROVIDER_OPENAI_RESPONSES");
}

/* ============================================================================
 * 3. provider_init_from_config integration tests
 * ============================================================================ */

static void test_init_from_config_with_api_key(void) {
    printf("\nTest: provider_init_from_config — PROVIDER_OPENAI_RESPONSES with api_key\n");

    /* Hide OPENAI_API_KEY to force use of config api_key */
    const char *saved = getenv("OPENAI_API_KEY");
    unsetenv("OPENAI_API_KEY");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model,    "gpt-4o",                              sizeof(config.model));
    strlcpy(config.api_key,  "sk-cfg-key-5678",                     sizeof(config.api_key));
    strlcpy(config.api_base, "https://api.openai.com/v1/responses", sizeof(config.api_base));

    ProviderInitResult result = {0};
    provider_init_from_config("test-responses", &config, &result);

    ASSERT_NOT_NULL(result.provider,      "provider should be created");
    ASSERT_NOT_NULL(result.api_url,       "api_url should be set");
    ASSERT_NULL(result.error_message,     "no error message");
    ASSERT_CONTAINS(result.api_url, "responses", "api_url contains 'responses'");

    cleanup_result(&result);
    if (saved) setenv("OPENAI_API_KEY", saved, 1);
}

static void test_init_from_config_env_key_fallback(void) {
    printf("\nTest: provider_init_from_config — OPENAI_API_KEY env var fallback\n");

    const char *saved = getenv("OPENAI_API_KEY");
    setenv("OPENAI_API_KEY", "sk-env-integration-key", 1);

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4o-mini", sizeof(config.model));
    /* api_key intentionally empty — should fall back to env */

    ProviderInitResult result = {0};
    provider_init_from_config("test-responses-env", &config, &result);

    ASSERT_NOT_NULL(result.provider,  "provider should be created from env key");
    ASSERT_NULL(result.error_message, "no error");

    cleanup_result(&result);
    if (saved) setenv("OPENAI_API_KEY", saved, 1);
    else       unsetenv("OPENAI_API_KEY");
}

static void test_init_from_config_no_key_fails(void) {
    printf("\nTest: provider_init_from_config — no key available → error\n");

    const char *saved = getenv("OPENAI_API_KEY");
    unsetenv("OPENAI_API_KEY");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4o", sizeof(config.model));
    /* api_key empty, env unset */

    ProviderInitResult result = {0};
    provider_init_from_config("test-responses-nokey", &config, &result);

    ASSERT_NULL(result.provider,       "provider should be NULL");
    ASSERT_NOT_NULL(result.error_message, "error message should describe missing key");

    cleanup_result(&result);
    if (saved) setenv("OPENAI_API_KEY", saved, 1);
}

static void test_init_from_config_default_url(void) {
    printf("\nTest: provider_init_from_config — empty api_base gets default URL\n");

    const char *saved = getenv("OPENAI_API_KEY");
    setenv("OPENAI_API_KEY", "sk-default-url-test", 1);

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4o", sizeof(config.model));
    /* api_base intentionally empty */

    ProviderInitResult result = {0};
    provider_init_from_config("test-responses-default-url", &config, &result);

    ASSERT_NOT_NULL(result.provider,   "provider should be created");
    ASSERT_NOT_NULL(result.api_url,    "api_url should be set");
    ASSERT_CONTAINS(result.api_url, "responses", "default url contains 'responses'");

    cleanup_result(&result);
    if (saved) setenv("OPENAI_API_KEY", saved, 1);
    else       unsetenv("OPENAI_API_KEY");
}

static void test_init_from_config_custom_url(void) {
    printf("\nTest: provider_init_from_config — custom api_base is preserved\n");

    const char *saved = getenv("OPENAI_API_KEY");
    setenv("OPENAI_API_KEY", "sk-custom-url-test", 1);

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model,    "gpt-4o",                                    sizeof(config.model));
    strlcpy(config.api_base, "https://my-proxy.example.com/v1/responses", sizeof(config.api_base));

    ProviderInitResult result = {0};
    provider_init_from_config("test-responses-custom-url", &config, &result);

    ASSERT_NOT_NULL(result.provider, "provider should be created");
    ASSERT_NOT_NULL(result.api_url,  "api_url should be set");
    ASSERT_CONTAINS(result.api_url, "my-proxy.example.com",
                    "custom api_base is preserved in api_url");

    cleanup_result(&result);
    if (saved) setenv("OPENAI_API_KEY", saved, 1);
    else       unsetenv("OPENAI_API_KEY");
}

static void test_init_from_config_empty_model_fails(void) {
    printf("\nTest: provider_init_from_config — empty model → error\n");

    const char *saved = getenv("OPENAI_API_KEY");
    setenv("OPENAI_API_KEY", "sk-model-test", 1);

    LLMProviderConfig config;
    init_config(&config);
    /* model intentionally left empty */
    strlcpy(config.api_key, "sk-model-test", sizeof(config.api_key));

    ProviderInitResult result = {0};
    provider_init_from_config("test-responses-no-model", &config, &result);

    /* An empty model is caught by the generic validation in provider_init_from_config */
    ASSERT_NULL(result.provider,       "provider should be NULL with empty model");
    ASSERT_NOT_NULL(result.error_message, "error message should be set");

    cleanup_result(&result);
    if (saved) setenv("OPENAI_API_KEY", saved, 1);
    else       unsetenv("OPENAI_API_KEY");
}

/* ============================================================================
 * main
 * ============================================================================ */

int main(void) {
    printf(TC_CYAN
           "=== openai_responses_provider unit tests ===\n"
           TC_RESET);

    /* Direct factory tests */
    test_create_with_explicit_key();
    test_create_defaults();
    test_create_no_key_returns_null();
    test_create_empty_key_falls_back_to_env();
    test_cleanup_is_safe();

    /* Type string round-trip */
    test_type_string_round_trip();
    test_type_string_unknown_does_not_match();

    /* provider_init_from_config integration */
    test_init_from_config_with_api_key();
    test_init_from_config_env_key_fallback();
    test_init_from_config_no_key_fails();
    test_init_from_config_default_url();
    test_init_from_config_custom_url();
    test_init_from_config_empty_model_fails();

    /* Summary */
    printf("\n" TC_CYAN "Test Summary:" TC_RESET "\n");
    printf("  Total:  %d\n", g_tests_run);
    printf("  Passed: " TC_GREEN "%d" TC_RESET "\n", g_tests_passed);
    printf("  Failed: " TC_RED "%d" TC_RESET "\n", g_tests_run - g_tests_passed);

    if (g_tests_run == g_tests_passed) {
        printf(TC_GREEN "\n\xe2\x9c\x93 All tests passed!\n" TC_RESET);
        return 0;
    }
    printf(TC_RED "\n\xe2\x9c\x97 Some tests failed!\n" TC_RESET);
    return 1;
}
