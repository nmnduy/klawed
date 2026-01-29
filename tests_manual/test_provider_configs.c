/*
 * test_provider_configs.c - Manual provider configuration tests
 *
 * Tests configuration loading and validation for real LLM providers.
 * These tests require actual API keys but do NOT make API calls.
 *
 * This is NOT part of the normal test suite. Run manually:
 *
 *   make -C tests_manual
 *   ./tests_manual/test_provider_configs [options]
 *
 * Environment variables required:
 *   - MINIMAX_API_KEY         (for MiniMax API)
 *   - MOONSHOT_AI_API_KEY     (for Moonshot/Kimi API)
 *   - AWS_ACCESS_KEY_ID       (for Bedrock)
 *   - AWS_SECRET_ACCESS_KEY   (for Bedrock)
 *   - AWS_REGION              (for Bedrock, optional, defaults to us-west-2)
 *   - ANTHROPIC_MODEL         (for Bedrock model, optional)
 *
 * Options:
 *   --list        List all providers and their configuration status
 *   --test-all    Test all configured providers
 *   --sonnet      Test sonnet-4.5 (LM Studio local)
 *   --minimax     Test minimax-2.1
 *   --kimi        Test kimi-k2.5
 *   --bedrock     Test AWS Bedrock
 *   --help        Show help
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <bsd/string.h>

#include "../src/config.h"
#include "test_provider_configs.h"

// Test framework
static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    tests_run++; \
    if (cond) { \
        tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf("  [FAIL] %s\n", msg); \
    } \
} while(0)

#define TEST_ASSERT_NOT_NULL(ptr, msg) TEST_ASSERT((ptr) != NULL, msg)
#define TEST_ASSERT_STR_EQ(a, b, msg) TEST_ASSERT(strcmp((a), (b)) == 0, msg)
#define TEST_ASSERT_STR_CONTAINS(str, substr, msg) \
    TEST_ASSERT((str) != NULL && strstr((str), (substr)) != NULL, msg)

// Provider test result
typedef struct {
    const char *name;
    int is_configured;
    int config_valid;
    char *errors;
} ProviderTestResult;

static void print_header(const char *title) {
    printf("\n%s\n", title);
    size_t len = strlen(title);
    for (size_t i = 0; i < len; i++) {
        putchar('=');
    }
    printf("\n");
}

static void print_config(const LLMProviderConfig *config) {
    printf("  Configuration:\n");
    printf("    Provider Type: %s\n", config_provider_type_to_string(config->provider_type));
    printf("    Provider Name: %s\n", config->provider_name[0] ? config->provider_name : "(not set)");
    printf("    Model:         %s\n", config->model[0] ? config->model : "(not set)");
    printf("    API Base:      %s\n", config->api_base[0] ? config->api_base : "(not set)");
    printf("    API Key:       %s\n", config->api_key[0] ? "(set, length=" : "(not set)");
    if (config->api_key[0]) {
        printf("%zu)\n", strlen(config->api_key));
    }
    printf("    API Key Env:   %s\n", config->api_key_env[0] ? config->api_key_env : "(not set)");
    printf("    Use Bedrock:   %s\n", config->use_bedrock ? "yes" : "no");
}

// Validate a provider configuration
static int validate_config(const LLMProviderConfig *config, const char *provider_name, char **errors) {
    (void)provider_name;  // Unused but kept for API consistency
    int valid = 1;
    size_t error_cap = 1024;
    *errors = malloc(error_cap);
    (*errors)[0] = '\0';

    // Check model is set
    if (!config->model[0]) {
        valid = 0;
        strlcat(*errors, "Model not set; ", error_cap);
    }

    // Check provider type
    if (config->provider_type == PROVIDER_AUTO) {
        valid = 0;
        strlcat(*errors, "Provider type is AUTO; ", error_cap);
    }

    // For non-bedrock providers, check API key
    if (config->provider_type != PROVIDER_BEDROCK && !config->use_bedrock) {
        int has_key = config->api_key[0] || config->api_key_env[0];
        if (!has_key) {
            // Check if it's the LM Studio local instance (may not need auth)
            if (strstr(config->api_base, "192.168.1.45") == NULL) {
                valid = 0;
                strlcat(*errors, "No API key configured; ", error_cap);
            }
        }
    }

    // For bedrock, no api_key needed - uses AWS credentials
    if (config->provider_type == PROVIDER_BEDROCK || config->use_bedrock) {
        // Bedrock is valid if model is set
        if (config->model[0]) {
            // Strip any API key errors since Bedrock doesn't need them
            if (strstr(*errors, "No API key") != NULL) {
                // This is OK for Bedrock
                char *p = strstr(*errors, "No API key");
                *p = '\0';
            }
        }
    }

    return valid;
}

// Test sonnet-4.5 (Local LM Studio)
static ProviderTestResult test_sonnet_4_5(void) {
    print_header("Testing: sonnet-4.5 (Local LM Studio)");

    ProviderTestResult result = {0};
    result.name = "sonnet-4.5";
    result.is_configured = sonnet_4_5_is_configured();

    printf("  Description: %s\n", sonnet_4_5_description());
    printf("  Configured:  %s\n", result.is_configured ? "YES" : "NO");

    LLMProviderConfig config;
    get_sonnet_4_5_config(&config);
    print_config(&config);

    printf("\n  Validating configuration...\n");
    result.config_valid = validate_config(&config, "sonnet-4.5", &result.errors);

    TEST_ASSERT_STR_EQ(config_provider_type_to_string(config.provider_type), "openai",
                       "Provider type is 'openai'");
    TEST_ASSERT_STR_CONTAINS(config.model, "sonnet", "Model contains 'sonnet'");
    TEST_ASSERT_STR_CONTAINS(config.api_base, "192.168.1.45", "API base contains local IP");

    if (result.config_valid) {
        printf("  [INFO] Configuration is valid\n");
    } else {
        printf("  [INFO] Configuration issues: %s\n", result.errors);
    }

    return result;
}

// Test minimax-2.1
static ProviderTestResult test_minimax_2_1(void) {
    print_header("Testing: minimax-2.1 (MiniMax)");

    ProviderTestResult result = {0};
    result.name = "minimax-2.1";
    result.is_configured = minimax_2_1_is_configured();

    printf("  Description: %s\n", minimax_2_1_description());
    printf("  Configured:  %s\n", result.is_configured ? "YES" : "NO");

    if (!result.is_configured) {
        printf("  [SKIP] MINIMAX_API_KEY not set\n");
        // Still validate the config structure
    }

    LLMProviderConfig config;
    get_minimax_2_1_config(&config);
    print_config(&config);

    printf("\n  Validating configuration...\n");
    result.config_valid = validate_config(&config, "minimax-2.1", &result.errors);

    TEST_ASSERT_STR_EQ(config_provider_type_to_string(config.provider_type), "anthropic",
                       "Provider type is 'anthropic'");
    TEST_ASSERT_STR_CONTAINS(config.model, "MiniMax", "Model contains 'MiniMax'");
    TEST_ASSERT_STR_CONTAINS(config.api_base, "minimax.io", "API base contains 'minimax.io'");

    if (result.is_configured) {
        TEST_ASSERT(config.api_key[0] != '\0', "API key loaded from environment");
    }

    if (result.config_valid) {
        printf("  [INFO] Configuration is valid\n");
    } else {
        printf("  [INFO] Configuration issues: %s\n", result.errors);
    }

    return result;
}

// Test kimi-k2.5
static ProviderTestResult test_kimi_k2_5(void) {
    print_header("Testing: kimi-k2.5 (Moonshot AI)");

    ProviderTestResult result = {0};
    result.name = "kimi-k2.5";
    result.is_configured = kimi_k2_5_is_configured();

    printf("  Description: %s\n", kimi_k2_5_description());
    printf("  Configured:  %s\n", result.is_configured ? "YES" : "NO");

    if (!result.is_configured) {
        printf("  [SKIP] MOONSHOT_AI_API_KEY not set\n");
    }

    LLMProviderConfig config;
    get_kimi_k2_5_config(&config);
    print_config(&config);

    printf("\n  Validating configuration...\n");
    result.config_valid = validate_config(&config, "kimi-k2.5", &result.errors);

    TEST_ASSERT_STR_EQ(config_provider_type_to_string(config.provider_type), "moonshot",
                       "Provider type is 'moonshot'");
    TEST_ASSERT_STR_CONTAINS(config.model, "kimi", "Model contains 'kimi'");
    TEST_ASSERT_STR_CONTAINS(config.api_base, "moonshot", "API base contains 'moonshot'");

    if (result.is_configured) {
        TEST_ASSERT(config.api_key[0] != '\0', "API key loaded from environment");
    }

    if (result.config_valid) {
        printf("  [INFO] Configuration is valid\n");
    } else {
        printf("  [INFO] Configuration issues: %s\n", result.errors);
    }

    return result;
}

// Test AWS Bedrock
static ProviderTestResult test_bedrock(void) {
    print_header("Testing: AWS Bedrock");

    ProviderTestResult result = {0};
    result.name = "bedrock";
    result.is_configured = bedrock_is_configured();

    printf("  Description: %s\n", bedrock_description());
    printf("  Configured:  %s\n", result.is_configured ? "YES" : "NO");

    if (!result.is_configured) {
        printf("  [SKIP] AWS credentials not set\n");
        printf("         Set AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY\n");
    }

    LLMProviderConfig config;
    get_bedrock_config(&config);
    print_config(&config);

    const char *region = getenv("AWS_REGION");
    printf("  AWS Region:    %s\n", region ? region : "(not set, uses default)");

    printf("\n  Validating configuration...\n");
    result.config_valid = validate_config(&config, "bedrock", &result.errors);

    TEST_ASSERT_STR_EQ(config_provider_type_to_string(config.provider_type), "bedrock",
                       "Provider type is 'bedrock'");
    TEST_ASSERT(config.use_bedrock == 1, "use_bedrock flag is set");
    TEST_ASSERT_STR_CONTAINS(config.model, "anthropic", "Model contains 'anthropic'");

    if (result.config_valid) {
        printf("  [INFO] Configuration is valid\n");
    } else {
        printf("  [INFO] Configuration issues: %s\n", result.errors);
    }

    return result;
}

// List all providers and their status
static void list_providers(void) {
    print_header("Provider Configuration Status");

    printf("\n1. sonnet-4.5 (Local LM Studio)\n");
    printf("   Description: %s\n", sonnet_4_5_description());
    printf("   Configured:  %s\n", sonnet_4_5_is_configured() ? "YES" : "NO");
    printf("   Env vars:    SONNET_4_5_API_KEY (optional)\n");

    printf("\n2. minimax-2.1 (MiniMax)\n");
    printf("   Description: %s\n", minimax_2_1_description());
    printf("   Configured:  %s\n", minimax_2_1_is_configured() ? "YES" : "NO");
    printf("   Env vars:    MINIMAX_API_KEY\n");

    printf("\n3. kimi-k2.5 (Moonshot AI)\n");
    printf("   Description: %s\n", kimi_k2_5_description());
    printf("   Configured:  %s\n", kimi_k2_5_is_configured() ? "YES" : "NO");
    printf("   Env vars:    MOONSHOT_AI_API_KEY\n");

    printf("\n4. bedrock (AWS Bedrock)\n");
    printf("   Description: %s\n", bedrock_description());
    printf("   Configured:  %s\n", bedrock_is_configured() ? "YES" : "NO");
    printf("   Env vars:    AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY\n");
    printf("                AWS_REGION (optional, defaults to us-west-2)\n");
    printf("                ANTHROPIC_MODEL (optional, has default)\n");
}

// Print usage
static void print_usage(const char *program) {
    printf("Usage: %s [options]\n\n", program);
    printf("Options:\n");
    printf("  --list        List all providers and their configuration status\n");
    printf("  --test-all    Test all configured providers\n");
    printf("  --sonnet      Test sonnet-4.5 (LM Studio local)\n");
    printf("  --minimax     Test minimax-2.1\n");
    printf("  --kimi        Test kimi-k2.5\n");
    printf("  --bedrock     Test AWS Bedrock\n");
    printf("  --help        Show this help\n");
    printf("\nIf no options specified, behaves like --list\n");
}

int main(int argc, char *argv[]) {
    int do_list = 0;
    int do_test_all = 0;
    int do_sonnet = 0;
    int do_minimax = 0;
    int do_kimi = 0;
    int do_bedrock = 0;

    // Parse arguments
    if (argc == 1) {
        do_list = 1;
    } else {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--list") == 0) {
                do_list = 1;
            } else if (strcmp(argv[i], "--test-all") == 0) {
                do_test_all = 1;
            } else if (strcmp(argv[i], "--sonnet") == 0) {
                do_sonnet = 1;
            } else if (strcmp(argv[i], "--minimax") == 0) {
                do_minimax = 1;
            } else if (strcmp(argv[i], "--kimi") == 0) {
                do_kimi = 1;
            } else if (strcmp(argv[i], "--bedrock") == 0) {
                do_bedrock = 1;
            } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                print_usage(argv[0]);
                return 0;
            } else {
                printf("Unknown option: %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        }
    }

    printf("=== Provider Configuration Test Suite ===\n");
    printf("Note: These tests validate configuration only and do NOT make API calls.\n");
    printf("      They are NOT run as part of the normal unit test suite.\n");

    if (do_list) {
        list_providers();
    }

    ProviderTestResult results[4] = {0};
    int result_count = 0;

    if (do_test_all || do_sonnet) {
        results[result_count++] = test_sonnet_4_5();
    }

    if (do_test_all || do_minimax) {
        results[result_count++] = test_minimax_2_1();
    }

    if (do_test_all || do_kimi) {
        results[result_count++] = test_kimi_k2_5();
    }

    if (do_test_all || do_bedrock) {
        results[result_count++] = test_bedrock();
    }

    // Print summary
    if (result_count > 0) {
        print_header("Summary");
        for (int i = 0; i < result_count; i++) {
            printf("  %s: ", results[i].name);
            if (!results[i].is_configured) {
                printf("NOT CONFIGURED\n");
            } else if (results[i].config_valid) {
                printf("CONFIG VALID\n");
            } else {
                printf("CONFIG INVALID - %s\n",
                       results[i].errors ? results[i].errors : "unknown error");
            }
            free(results[i].errors);
        }

        printf("\n  Test assertions: %d passed / %d run\n", tests_passed, tests_run);
    }

    return (tests_run == tests_passed) ? 0 : 1;
}
