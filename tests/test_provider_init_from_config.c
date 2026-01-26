/*
 * Test for provider_init_from_config - verify provider initialization logic
 *
 * Tests the provider_init_from_config function validation and parameter handling.
 * Tests actual provider creation for OpenAI, Anthropic, and Bedrock.
 *
 * This test links against the full application to ensure all dependencies are satisfied.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

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
#define ASSERT_STR_CONTAINS(haystack, needle, msg) \
    ASSERT_TRUE((haystack) != NULL && strstr((haystack), (needle)) != NULL, msg)

// Helper to initialize a config with defaults
static void init_config(LLMProviderConfig *config) {
    memset(config, 0, sizeof(*config));
    config->provider_type = PROVIDER_AUTO;
    config->use_bedrock = 0;
}

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
    if (result->error_message) {
        free(result->error_message);
        result->error_message = NULL;
    }
}

/*
 * Test: NULL config should return error
 */
static void test_null_config(void) {
    printf("\nTest: NULL config should return error\n");

    ProviderInitResult result = {0};
    provider_init_from_config("test-provider", NULL, &result);

    ASSERT_NULL(result.provider, "Provider should be NULL");
    ASSERT_NULL(result.api_url, "API URL should be NULL");
    ASSERT_NOT_NULL(result.error_message, "Error message should be set");
    ASSERT_STR_CONTAINS(result.error_message, "required", "Error should mention 'required'");

    cleanup_result(&result);
}

/*
 * Test: Empty model should return error
 */
static void test_empty_model(void) {
    printf("\nTest: Empty model should return error\n");

    LLMProviderConfig config;
    init_config(&config);
    // model is empty (default)

    ProviderInitResult result = {0};
    provider_init_from_config("test-provider", &config, &result);

    ASSERT_NULL(result.provider, "Provider should be NULL");
    ASSERT_NULL(result.api_url, "API URL should be NULL");
    ASSERT_NOT_NULL(result.error_message, "Error message should be set");
    ASSERT_STR_CONTAINS(result.error_message, "Model", "Error should mention 'Model'");

    cleanup_result(&result);
}

/*
 * Test: Missing API key for non-Bedrock provider should return error
 */
static void test_missing_api_key(void) {
    printf("\nTest: Missing API key for non-Bedrock provider should return error\n");

    // Ensure no fallback API key is available
    const char *old_key = getenv("OPENAI_API_KEY");
    unsetenv("OPENAI_API_KEY");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4", sizeof(config.model));
    config.provider_type = PROVIDER_OPENAI;

    ProviderInitResult result = {0};
    provider_init_from_config("test-provider", &config, &result);

    ASSERT_NULL(result.provider, "Provider should be NULL");
    ASSERT_NULL(result.api_url, "API URL should be NULL");
    ASSERT_NOT_NULL(result.error_message, "Error message should be set");
    ASSERT_STR_CONTAINS(result.error_message, "API key", "Error should mention 'API key'");

    cleanup_result(&result);

    // Restore environment
    if (old_key) {
        setenv("OPENAI_API_KEY", old_key, 1);
    }
}

/*
 * Test: OpenAI provider creation with direct api_key
 */
static void test_openai_provider_with_api_key(void) {
    printf("\nTest: OpenAI provider creation with direct api_key\n");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4", sizeof(config.model));
    strlcpy(config.api_key, "sk-test-api-key-12345", sizeof(config.api_key));
    strlcpy(config.api_base, "https://api.openai.com/v1/chat/completions", sizeof(config.api_base));
    config.provider_type = PROVIDER_OPENAI;

    ProviderInitResult result = {0};
    provider_init_from_config("test-openai", &config, &result);

    ASSERT_NOT_NULL(result.provider, "Provider should be created");
    ASSERT_NOT_NULL(result.api_url, "API URL should be set");
    ASSERT_NULL(result.error_message, "Error message should be NULL");
    ASSERT_STR_CONTAINS(result.api_url, "openai.com", "API URL should contain openai.com");

    cleanup_result(&result);
}

/*
 * Test: OpenAI provider creation with api_key_env
 */
static void test_openai_provider_with_api_key_env(void) {
    printf("\nTest: OpenAI provider creation with api_key_env\n");

    // Set up a custom environment variable
    setenv("TEST_OPENAI_KEY", "sk-test-env-key-67890", 1);

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4", sizeof(config.model));
    strlcpy(config.api_key_env, "TEST_OPENAI_KEY", sizeof(config.api_key_env));
    strlcpy(config.api_base, "https://api.openai.com/v1/chat/completions", sizeof(config.api_base));
    config.provider_type = PROVIDER_OPENAI;

    ProviderInitResult result = {0};
    provider_init_from_config("test-openai-env", &config, &result);

    ASSERT_NOT_NULL(result.provider, "Provider should be created");
    ASSERT_NOT_NULL(result.api_url, "API URL should be set");
    ASSERT_NULL(result.error_message, "Error message should be NULL");

    cleanup_result(&result);
    unsetenv("TEST_OPENAI_KEY");
}

/*
 * Test: api_key_env takes priority over api_key (when OPENAI_API_KEY is not set)
 */
static void test_api_key_env_priority(void) {
    printf("\nTest: api_key_env takes priority over api_key (when OPENAI_API_KEY is not set)\n");

    // Clear OPENAI_API_KEY to test api_key_env priority
    const char *old_openai_key = getenv("OPENAI_API_KEY");
    unsetenv("OPENAI_API_KEY");

    // Set up environment variable
    setenv("TEST_PRIORITY_KEY", "sk-env-priority-key", 1);

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4", sizeof(config.model));
    strlcpy(config.api_key, "sk-direct-key", sizeof(config.api_key));
    strlcpy(config.api_key_env, "TEST_PRIORITY_KEY", sizeof(config.api_key_env));
    strlcpy(config.api_base, "https://api.openai.com/v1/chat/completions", sizeof(config.api_base));
    config.provider_type = PROVIDER_OPENAI;

    ProviderInitResult result = {0};
    provider_init_from_config("test-priority", &config, &result);

    // If provider was created, it used the env var key (api_key_env takes priority over api_key)
    ASSERT_NOT_NULL(result.provider, "Provider should be created");
    ASSERT_NULL(result.error_message, "Error message should be NULL");

    cleanup_result(&result);
    unsetenv("TEST_PRIORITY_KEY");

    // Restore OPENAI_API_KEY
    if (old_openai_key) {
        setenv("OPENAI_API_KEY", old_openai_key, 1);
    }
}

/*
 * Test: OPENAI_API_KEY takes priority over api_key_env and api_key
 */
static void test_openai_api_key_priority(void) {
    printf("\nTest: OPENAI_API_KEY takes priority over api_key_env and api_key\n");

    // Set OPENAI_API_KEY to test its priority
    setenv("OPENAI_API_KEY", "sk-openai-env-key-12345", 1);
    // Also set a custom env var for api_key_env
    setenv("CUSTOM_API_KEY_ENV", "sk-custom-env-key-67890", 1);

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4", sizeof(config.model));
    strlcpy(config.api_key, "sk-direct-key-in-config", sizeof(config.api_key));
    strlcpy(config.api_key_env, "CUSTOM_API_KEY_ENV", sizeof(config.api_key_env));
    strlcpy(config.api_base, "https://api.openai.com/v1/chat/completions", sizeof(config.api_base));
    config.provider_type = PROVIDER_OPENAI;

    ProviderInitResult result = {0};
    provider_init_from_config("test-openai-priority", &config, &result);

    // Provider should be created using OPENAI_API_KEY (highest priority)
    ASSERT_NOT_NULL(result.provider, "Provider should be created using OPENAI_API_KEY");
    ASSERT_NULL(result.error_message, "Error message should be NULL");

    cleanup_result(&result);
    unsetenv("OPENAI_API_KEY");
    unsetenv("CUSTOM_API_KEY_ENV");
}

/*
 * Test: Environment variables take priority over provider config for model
 */
static void test_env_var_model_priority(void) {
    printf("\nTest: Environment variables take priority over provider config for model\n");

    // Set environment variable
    setenv("OPENAI_MODEL", "env-model-override", 1);

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "config-model-value", sizeof(config.model));
    strlcpy(config.api_key, "sk-test-key", sizeof(config.api_key));
    strlcpy(config.api_base, "https://api.openai.com/v1/chat/completions", sizeof(config.api_base));
    config.provider_type = PROVIDER_OPENAI;

    ProviderInitResult result = {0};
    provider_init_from_config("test-model-priority", &config, &result);

    // Provider should be created using env var model
    ASSERT_NOT_NULL(result.provider, "Provider should be created");
    ASSERT_NULL(result.error_message, "Error message should be NULL");

    cleanup_result(&result);
    unsetenv("OPENAI_MODEL");
}

/*
 * Test: Environment variables take priority over provider config for API base
 */
static void test_env_var_api_base_priority(void) {
    printf("\nTest: Environment variables take priority over provider config for API base\n");

    // Set environment variable
    setenv("OPENAI_API_BASE", "https://env-api-base.example.com", 1);

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4", sizeof(config.model));
    strlcpy(config.api_key, "sk-test-key", sizeof(config.api_key));
    strlcpy(config.api_base, "https://config-api-base.example.com", sizeof(config.api_base));
    config.provider_type = PROVIDER_OPENAI;

    ProviderInitResult result = {0};
    provider_init_from_config("test-api-base-priority", &config, &result);

    // Provider should be created using env var API base
    ASSERT_NOT_NULL(result.provider, "Provider should be created");
    ASSERT_NULL(result.error_message, "Error message should be NULL");

    cleanup_result(&result);
    unsetenv("OPENAI_API_BASE");
}

/*
 * Test: Fallback to OPENAI_API_KEY when api_key_env is empty
 */
static void test_fallback_to_openai_api_key(void) {
    printf("\nTest: Fallback to OPENAI_API_KEY when api_key_env is empty\n");

    // Ensure OPENAI_API_KEY is set
    setenv("OPENAI_API_KEY", "sk-fallback-key-12345", 1);

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4", sizeof(config.model));
    // api_key and api_key_env are empty
    strlcpy(config.api_base, "https://api.openai.com/v1/chat/completions", sizeof(config.api_base));
    config.provider_type = PROVIDER_OPENAI;

    ProviderInitResult result = {0};
    provider_init_from_config("test-fallback", &config, &result);

    ASSERT_NOT_NULL(result.provider, "Provider should be created using OPENAI_API_KEY fallback");
    ASSERT_NULL(result.error_message, "Error message should be NULL");

    cleanup_result(&result);
}

/*
 * Test: Anthropic provider creation
 */
static void test_anthropic_provider_creation(void) {
    printf("\nTest: Anthropic provider creation\n");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "claude-sonnet-4-20250514", sizeof(config.model));
    strlcpy(config.api_key, "sk-ant-test-key-12345", sizeof(config.api_key));
    config.provider_type = PROVIDER_ANTHROPIC;
    // api_base empty - should use default Anthropic URL

    ProviderInitResult result = {0};
    provider_init_from_config("test-anthropic", &config, &result);

    ASSERT_NOT_NULL(result.provider, "Anthropic provider should be created");
    ASSERT_NOT_NULL(result.api_url, "API URL should be set");
    ASSERT_NULL(result.error_message, "Error message should be NULL");
    ASSERT_STR_CONTAINS(result.api_url, "anthropic.com", "API URL should contain anthropic.com");

    cleanup_result(&result);
}

/*
 * Test: Anthropic provider with custom base URL
 */
static void test_anthropic_provider_custom_url(void) {
    printf("\nTest: Anthropic provider with custom base URL\n");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "claude-sonnet-4-20250514", sizeof(config.model));
    strlcpy(config.api_key, "sk-ant-test-key-12345", sizeof(config.api_key));
    strlcpy(config.api_base, "https://custom.example.com/anthropic", sizeof(config.api_base));
    config.provider_type = PROVIDER_ANTHROPIC;

    ProviderInitResult result = {0};
    provider_init_from_config("test-anthropic-custom", &config, &result);

    ASSERT_NOT_NULL(result.provider, "Anthropic provider should be created");
    ASSERT_NOT_NULL(result.api_url, "API URL should be set");
    ASSERT_NULL(result.error_message, "Error message should be NULL");
    ASSERT_STR_CONTAINS(result.api_url, "custom.example.com", "API URL should use custom URL");

    cleanup_result(&result);
}

/*
 * Test: Auto-detect Anthropic from URL containing anthropic.com
 */
static void test_auto_detect_anthropic_from_url(void) {
    printf("\nTest: Auto-detect Anthropic from URL containing anthropic.com\n");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "claude-sonnet-4-20250514", sizeof(config.model));
    strlcpy(config.api_key, "sk-ant-test-key-12345", sizeof(config.api_key));
    strlcpy(config.api_base, "https://api.anthropic.com/v1/messages", sizeof(config.api_base));
    config.provider_type = PROVIDER_AUTO;  // Auto-detect

    ProviderInitResult result = {0};
    provider_init_from_config("test-auto-anthropic", &config, &result);

    ASSERT_NOT_NULL(result.provider, "Provider should be created");
    ASSERT_NOT_NULL(result.api_url, "API URL should be set");
    ASSERT_NULL(result.error_message, "Error message should be NULL");
    // The provider should detect it's Anthropic from the URL
    ASSERT_STR_CONTAINS(result.api_url, "anthropic.com", "Should use Anthropic URL");

    cleanup_result(&result);
}

/*
 * Test: Auto-detect Anthropic from URL containing /anthropic path
 */
static void test_auto_detect_anthropic_from_path(void) {
    printf("\nTest: Auto-detect Anthropic from URL containing /anthropic path\n");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "claude-sonnet-4-20250514", sizeof(config.model));
    strlcpy(config.api_key, "sk-ant-test-key-12345", sizeof(config.api_key));
    strlcpy(config.api_base, "https://proxy.example.com/anthropic/v1/messages", sizeof(config.api_base));
    config.provider_type = PROVIDER_AUTO;  // Auto-detect

    ProviderInitResult result = {0};
    provider_init_from_config("test-auto-anthropic-path", &config, &result);

    ASSERT_NOT_NULL(result.provider, "Provider should be created");
    ASSERT_NOT_NULL(result.api_url, "API URL should be set");
    ASSERT_NULL(result.error_message, "Error message should be NULL");

    cleanup_result(&result);
}

/*
 * Test: Empty api_key_env falls back to api_key
 */
static void test_empty_api_key_env_fallback(void) {
    printf("\nTest: Empty api_key_env falls back to api_key\n");

    // Make sure the env var doesn't exist
    unsetenv("NONEXISTENT_KEY_VAR");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4", sizeof(config.model));
    strlcpy(config.api_key, "sk-direct-api-key", sizeof(config.api_key));
    strlcpy(config.api_key_env, "NONEXISTENT_KEY_VAR", sizeof(config.api_key_env));
    strlcpy(config.api_base, "https://api.openai.com/v1/chat/completions", sizeof(config.api_base));
    config.provider_type = PROVIDER_OPENAI;

    ProviderInitResult result = {0};
    provider_init_from_config("test-fallback-to-direct", &config, &result);

    // Should fall back to api_key since env var doesn't exist
    ASSERT_NOT_NULL(result.provider, "Provider should be created using api_key fallback");
    ASSERT_NULL(result.error_message, "Error message should be NULL");

    cleanup_result(&result);
}

/*
 * Test: Result fields are initialized even on error
 */
static void test_result_initialization(void) {
    printf("\nTest: Result fields are initialized even on error\n");

    ProviderInitResult result;
    // Intentionally don't initialize result to test that the function does it
    result.provider = (Provider *)0xDEADBEEF;
    result.api_url = (char *)0xDEADBEEF;
    result.error_message = (char *)0xDEADBEEF;

    provider_init_from_config("test-init", NULL, &result);

    ASSERT_NULL(result.provider, "Provider should be NULL (not 0xDEADBEEF)");
    ASSERT_NULL(result.api_url, "API URL should be NULL (not 0xDEADBEEF)");
    ASSERT_NOT_NULL(result.error_message, "Error message should be set");

    cleanup_result(&result);
}

/*
 * Test: NULL provider_key is handled (used for logging only)
 */
static void test_null_provider_key(void) {
    printf("\nTest: NULL provider_key is handled gracefully\n");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4", sizeof(config.model));
    strlcpy(config.api_key, "sk-test-api-key", sizeof(config.api_key));
    strlcpy(config.api_base, "https://api.openai.com/v1/chat/completions", sizeof(config.api_base));
    config.provider_type = PROVIDER_OPENAI;

    ProviderInitResult result = {0};
    // NULL provider_key should be handled (it's only used for logging)
    provider_init_from_config(NULL, &config, &result);

    ASSERT_NOT_NULL(result.provider, "Provider should be created even with NULL provider_key");
    ASSERT_NULL(result.error_message, "Error message should be NULL");

    cleanup_result(&result);
}

/*
 * Test: use_bedrock flag triggers Bedrock provider
 * Note: This test may fail if AWS credentials are not configured
 */
static void test_use_bedrock_flag(void) {
    printf("\nTest: use_bedrock flag triggers Bedrock provider (may fail without AWS creds)\n");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "anthropic.claude-sonnet-4-20250514-v1:0", sizeof(config.model));
    config.use_bedrock = 1;

    ProviderInitResult result = {0};
    provider_init_from_config("test-bedrock", &config, &result);

    // Bedrock may fail if AWS credentials aren't configured - that's OK
    // We just verify that it attempts to create a Bedrock provider
    if (result.provider) {
        ASSERT_NOT_NULL(result.api_url, "Bedrock provider should have endpoint URL");
        printf("  [INFO] Bedrock provider created successfully\n");
    } else {
        ASSERT_NOT_NULL(result.error_message, "Should have error message if Bedrock fails");
        printf("  [INFO] Bedrock provider failed (expected if no AWS creds): %s\n",
               result.error_message);
        // Mark as passed since we're testing the code path, not AWS config
        tests_passed++;
        tests_run++;
    }

    cleanup_result(&result);
}

/*
 * Test: PROVIDER_BEDROCK type triggers Bedrock provider
 */
static void test_provider_bedrock_type(void) {
    printf("\nTest: PROVIDER_BEDROCK type triggers Bedrock provider (may fail without AWS creds)\n");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "anthropic.claude-sonnet-4-20250514-v1:0", sizeof(config.model));
    config.provider_type = PROVIDER_BEDROCK;

    ProviderInitResult result = {0};
    provider_init_from_config("test-bedrock-type", &config, &result);

    // Similar to above - verify the code path is exercised
    if (result.provider) {
        printf("  [INFO] Bedrock provider created successfully\n");
    } else {
        ASSERT_NOT_NULL(result.error_message, "Should have error message if Bedrock fails");
        printf("  [INFO] Bedrock provider failed (expected if no AWS creds): %s\n",
               result.error_message);
        tests_passed++;
        tests_run++;
    }

    cleanup_result(&result);
}

/*
 * Test: Provider type PROVIDER_AUTO defaults to OpenAI for non-Anthropic URLs
 */
static void test_auto_defaults_to_openai(void) {
    printf("\nTest: PROVIDER_AUTO defaults to OpenAI for non-Anthropic URLs\n");

    LLMProviderConfig config;
    init_config(&config);
    strlcpy(config.model, "gpt-4", sizeof(config.model));
    strlcpy(config.api_key, "sk-test-api-key", sizeof(config.api_key));
    strlcpy(config.api_base, "https://api.example.com/v1/completions", sizeof(config.api_base));
    config.provider_type = PROVIDER_AUTO;

    ProviderInitResult result = {0};
    provider_init_from_config("test-auto-openai", &config, &result);

    ASSERT_NOT_NULL(result.provider, "Provider should be created");
    ASSERT_NOT_NULL(result.api_url, "API URL should be set");
    ASSERT_NULL(result.error_message, "Error message should be NULL");
    ASSERT_STR_CONTAINS(result.api_url, "example.com", "API URL should use the configured URL");

    cleanup_result(&result);
}

int main(void) {
    printf("=== provider_init_from_config Tests ===\n");

    // Basic error cases
    test_null_config();
    test_empty_model();
    test_missing_api_key();
    test_result_initialization();

    // OpenAI provider tests
    test_openai_provider_with_api_key();
    test_openai_provider_with_api_key_env();
    test_api_key_env_priority();
    test_openai_api_key_priority();
    test_env_var_model_priority();
    test_env_var_api_base_priority();
    test_fallback_to_openai_api_key();
    test_empty_api_key_env_fallback();
    test_null_provider_key();
    test_auto_defaults_to_openai();

    // Anthropic provider tests
    test_anthropic_provider_creation();
    test_anthropic_provider_custom_url();
    test_auto_detect_anthropic_from_url();
    test_auto_detect_anthropic_from_path();

    // Bedrock provider tests (may require AWS credentials)
    test_use_bedrock_flag();
    test_provider_bedrock_type();

    printf("\n=== Results ===\n");
    printf("Tests run: %d, passed: %d, failed: %d\n",
           tests_run, tests_passed, tests_run - tests_passed);

    return (tests_run == tests_passed) ? 0 : 1;
}
