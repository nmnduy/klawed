/*
 * Test for provider_init - verify provider initialization from config/environment
 *
 * Tests that:
 * 1. Named provider's model takes precedence over passed model parameter
 * 2. KLAWED_LLM_PROVIDER env var correctly selects provider
 * 3. active_provider in config correctly selects provider
 * 4. Provider selection respects config file settings
 *
 * This test links against the full application to ensure all dependencies are satisfied.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <unistd.h>
#include <sys/stat.h>

// Include the source files we need
#include "../src/provider.h"
#include "../src/config.h"

// Simple test framework
static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests_run++; \
    if (cond) { tests_passed++; printf("[PASS] %s\n", msg); } \
    else { printf("[FAIL] %s\n", msg); } \
} while(0)

#define ASSERT_NULL(ptr, msg) ASSERT_TRUE((ptr) == NULL, msg)
#define ASSERT_NOT_NULL(ptr, msg) ASSERT_TRUE((ptr) != NULL, msg)
#define ASSERT_STR_EQ(actual, expected, msg) do { \
    tests_run++; \
    if ((actual) && (expected) && strcmp((actual), (expected)) == 0) { \
        tests_passed++; printf("[PASS] %s\n", msg); } \
    else { printf("[FAIL] %s (expected: '%s', got: '%s')\n", msg, expected, actual ? actual : "(null)"); } \
} while(0)

// Cleanup helper
static void cleanup_result(ProviderInitResult *result) {
    if (result->provider) {
        result->provider->cleanup(result->provider);
        result->provider = NULL;
    }
    if (result->api_url) {
        free(result->api_url);
        result->api_url = NULL;
    }
    if (result->model) {
        free(result->model);
        result->model = NULL;
    }
    if (result->error_message) {
        free(result->error_message);
        result->error_message = NULL;
    }
}

// Helper to set up test config
static void setup_test_config(const char *provider_name, const char *model) {
    KlawedConfig config;
    config_init_defaults(&config);

    // Create a test provider
    if (provider_name && model) {
        strlcpy(config.providers[0].key, provider_name, sizeof(config.providers[0].key));
        strlcpy(config.providers[0].config.model, model, sizeof(config.providers[0].config.model));
        strlcpy(config.providers[0].config.api_key, "sk-test-key-12345", sizeof(config.providers[0].config.api_key));
        strlcpy(config.providers[0].config.api_base, "https://api.openai.com/v1/chat/completions", sizeof(config.providers[0].config.api_base));
        config.providers[0].config.provider_type = PROVIDER_OPENAI;
        config.providers[0].config.use_bedrock = 0;
        config.provider_count = 1;
    }

    // Set as active provider
    if (provider_name) {
        strlcpy(config.active_provider, provider_name, sizeof(config.active_provider));
    }

    config_save(&config);
}

/*
 * Test: Named provider's model takes precedence over passed model parameter
 * This is the main bug fix - when a provider is explicitly selected via active_provider,
 * its configured model should override any passed model parameter.
 */
static void test_named_provider_model_precedence(void) {
    printf("\nTest: Named provider's model takes precedence over passed model parameter\n");

    // Set up a test provider with a specific model
    setup_test_config("test-provider", "claude-sonnet-4-20250514");

    // Clear OPENAI_API_KEY to avoid using it
    const char *old_key = getenv("OPENAI_API_KEY");
    unsetenv("OPENAI_API_KEY");
    // Set KLAWED_LLM_PROVIDER to select our test provider
    setenv("KLAWED_LLM_PROVIDER", "test-provider", 1);

    // Call provider_init with a different model (simulating DEFAULT_MODEL fallback)
    ProviderInitResult result = {0};
    provider_init("o4-mini", NULL, &result);

    // Provider should be created
    ASSERT_NOT_NULL(result.provider, "Provider should be created");
    ASSERT_NULL(result.error_message, "Error message should be NULL");

    // The key assertion: result.model should be from config, not the passed parameter
    ASSERT_STR_EQ(result.model, "claude-sonnet-4-20250514",
                  "Model should be from named provider config (not passed parameter)");

    // Verify the provider was selected
    ASSERT_NOT_NULL(result.api_url, "API URL should be set");

    cleanup_result(&result);

    // Restore environment
    unsetenv("KLAWED_LLM_PROVIDER");
    if (old_key) {
        setenv("OPENAI_API_KEY", old_key, 1);
    }

    // Clean up test config
    unlink(".klawed/config.json");
}

/*
 * Test: KLAWED_LLM_PROVIDER selects correct provider
 */
static void test_klawed_llm_provider_env(void) {
    printf("\nTest: KLAWED_LLM_PROVIDER environment variable selects provider\n");

    // Set up two providers
    KlawedConfig config;
    config_init_defaults(&config);

    // Provider 1: gpt-4
    strlcpy(config.providers[0].key, "openai-gpt4", sizeof(config.providers[0].key));
    strlcpy(config.providers[0].config.model, "gpt-4", sizeof(config.providers[0].config.model));
    strlcpy(config.providers[0].config.api_key, "sk-gpt4-key", sizeof(config.providers[0].config.api_key));
    config.providers[0].config.provider_type = PROVIDER_OPENAI;

    // Provider 2: claude
    strlcpy(config.providers[1].key, "anthropic-sonnet", sizeof(config.providers[1].key));
    strlcpy(config.providers[1].config.model, "claude-sonnet-4-20250514", sizeof(config.providers[1].config.model));
    strlcpy(config.providers[1].config.api_key, "sk-claude-key", sizeof(config.providers[1].config.api_key));
    config.providers[1].config.provider_type = PROVIDER_OPENAI;

    config.provider_count = 2;

    config_save(&config);

    // Clear OPENAI_API_KEY
    const char *old_key = getenv("OPENAI_API_KEY");
    unsetenv("OPENAI_API_KEY");
    // Select provider 2 via env var
    setenv("KLAWED_LLM_PROVIDER", "anthropic-sonnet", 1);

    ProviderInitResult result = {0};
    provider_init("some-default-model", NULL, &result);

    ASSERT_NOT_NULL(result.provider, "Provider should be created");
    ASSERT_NULL(result.error_message, "Error message should be NULL");
    ASSERT_STR_EQ(result.model, "claude-sonnet-4-20250514",
                  "Model should be from anthropic-sonnet provider");

    cleanup_result(&result);

    // Restore and cleanup
    unsetenv("KLAWED_LLM_PROVIDER");
    if (old_key) {
        setenv("OPENAI_API_KEY", old_key, 1);
    }
    unlink(".klawed/config.json");
}

/*
 * Test: active_provider in config is used when KLAWED_LLM_PROVIDER is not set
 */
static void test_active_provider_from_config(void) {
    printf("\nTest: active_provider in config is used when KLAWED_LLM_PROVIDER is not set\n");

    // Set up config with active_provider
    KlawedConfig config;
    config_init_defaults(&config);

    strlcpy(config.providers[0].key, "my-bedrock", sizeof(config.providers[0].key));
    strlcpy(config.providers[0].config.model, "anthropic.claude-sonnet-4-20250514-v1:0",
            sizeof(config.providers[0].config.model));
    strlcpy(config.providers[0].config.api_key, "sk-bedrock-key", sizeof(config.providers[0].config.api_key));
    config.providers[0].config.provider_type = PROVIDER_OPENAI;
    config.provider_count = 1;

    // Set as active provider
    strlcpy(config.active_provider, "my-bedrock", sizeof(config.active_provider));

    config_save(&config);

    // Clear OPENAI_API_KEY
    const char *old_key = getenv("OPENAI_API_KEY");
    unsetenv("OPENAI_API_KEY");
    // Make sure KLAWED_LLM_PROVIDER is not set
    unsetenv("KLAWED_LLM_PROVIDER");

    ProviderInitResult result = {0};
    provider_init("wrong-model", NULL, &result);

    ASSERT_NOT_NULL(result.provider, "Provider should be created");
    ASSERT_NULL(result.error_message, "Error message should be NULL");
    ASSERT_STR_EQ(result.model, "anthropic.claude-sonnet-4-20250514-v1:0",
                  "Model should be from active_provider in config");

    cleanup_result(&result);

    // Restore and cleanup
    if (old_key) {
        setenv("OPENAI_API_KEY", old_key, 1);
    }
    unlink(".klawed/config.json");
}

/*
 * Test: Legacy mode (no named provider) falls back to passed model parameter
 */
static void test_legacy_mode_uses_passed_model(void) {
    printf("\nTest: Legacy mode (no named provider) uses passed model parameter\n");

    // Create minimal config without any providers
    KlawedConfig config;
    config_init_defaults(&config);
    config.provider_count = 0;
    config.active_provider[0] = '\0';
    config_save(&config);

    // Set OPENAI_API_KEY for the provider
    setenv("OPENAI_API_KEY", "sk-legacy-key", 1);
    unsetenv("KLAWED_LLM_PROVIDER");

    ProviderInitResult result = {0};
    provider_init("gpt-4-legacy", NULL, &result);

    ASSERT_NOT_NULL(result.provider, "Provider should be created");
    ASSERT_NULL(result.error_message, "Error message should be NULL");
    ASSERT_STR_EQ(result.model, "gpt-4-legacy",
                  "Model should be the passed parameter in legacy mode");

    cleanup_result(&result);

    unsetenv("OPENAI_API_KEY");
    unlink(".klawed/config.json");
}

/*
 * Test: Invalid KLAWED_LLM_PROVIDER still creates provider with fallback
 */
static void test_invalid_provider_env_fallback(void) {
    printf("\nTest: Invalid KLAWED_LLM_PROVIDER creates provider with fallback\n");

    // Set up a valid config with API key
    KlawedConfig config;
    config_init_defaults(&config);
    strlcpy(config.providers[0].key, "valid-provider", sizeof(config.providers[0].key));
    strlcpy(config.providers[0].config.model, "valid-model", sizeof(config.providers[0].config.model));
    strlcpy(config.providers[0].config.api_key, "sk-valid-key", sizeof(config.providers[0].config.api_key));
    config.providers[0].config.provider_type = PROVIDER_OPENAI;
    config.provider_count = 1;
    config_save(&config);

    // Set OPENAI_API_KEY so provider can be created
    setenv("OPENAI_API_KEY", "sk-test-fallback-key", 1);
    // Set invalid provider name
    setenv("KLAWED_LLM_PROVIDER", "nonexistent-provider", 1);

    ProviderInitResult result = {0};
    provider_init("fallback-model", NULL, &result);

    // Should still create a provider with the passed model (falls back gracefully)
    ASSERT_NOT_NULL(result.provider, "Provider should be created even with invalid KLAWED_LLM_PROVIDER");
    ASSERT_NULL(result.error_message, "Error message should be NULL (falls back gracefully)");
    ASSERT_STR_EQ(result.model, "fallback-model",
                  "Model should be passed parameter when provider not found");

    cleanup_result(&result);

    // Restore and cleanup
    unsetenv("KLAWED_LLM_PROVIDER");
    unsetenv("OPENAI_API_KEY");
    unlink(".klawed/config.json");
}

int main(void) {
    printf("=== provider_init Tests ===\n");
    printf("Testing provider model selection and config file integration\n\n");

    // Create .klawed directory for test configs
    mkdir(".klawed", 0755);

    // Use a temporary data directory to avoid loading user/global config
    // This ensures our test config is the only one loaded
    const char *old_data_dir = getenv("KLAWED_DATA_DIR");
    setenv("KLAWED_DATA_DIR", ".klawed", 1);

    // Clear HOME to prevent loading ~/.klawed/config.json
    const char *old_home = getenv("HOME");
    unsetenv("HOME");

    // Test named provider model precedence (the main bug fix)
    test_named_provider_model_precedence();

    // Test KLAWED_LLM_PROVIDER environment variable
    test_klawed_llm_provider_env();

    // Test active_provider from config
    test_active_provider_from_config();

    // Test legacy mode
    test_legacy_mode_uses_passed_model();

    // Test invalid provider name fallback
    test_invalid_provider_env_fallback();

    // Restore environment
    if (old_data_dir) {
        setenv("KLAWED_DATA_DIR", old_data_dir, 1);
    } else {
        unsetenv("KLAWED_DATA_DIR");
    }
    if (old_home) {
        setenv("HOME", old_home, 1);
    }

    // Cleanup
    rmdir(".klawed");

    printf("\n=== Results ===\n");
    printf("Tests run: %d, passed: %d, failed: %d\n",
           tests_run, tests_passed, tests_run - tests_passed);

    return (tests_run == tests_passed) ? 0 : 1;
}
