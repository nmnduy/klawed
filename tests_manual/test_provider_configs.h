/*
 * test_provider_configs.h - Provider configurations for manual testing
 *
 * These are real provider configurations that require actual API keys
 * and network access. They are NOT run as part of the normal test suite.
 *
 * Usage:
 *   make -C tests_manual test_provider_configs
 *   ./tests_manual/test_provider_configs
 *
 * Environment variables required:
 *   - SONNET_4_5_API_KEY      (for LM Studio local instance, optional if no auth)
 *   - MINIMAX_API_KEY         (for MiniMax API)
 *   - MOONSHOT_AI_API_KEY     (for Moonshot/Kimi API)
 *   - AWS_ACCESS_KEY_ID       (for Bedrock)
 *   - AWS_SECRET_ACCESS_KEY   (for Bedrock)
 */

#ifndef TEST_PROVIDER_CONFIGS_H
#define TEST_PROVIDER_CONFIGS_H

#include "../src/config.h"

// Provider: sonnet-4.5 (Local LM Studio - OpenAI compatible)
// Environment: SONNET_4_5_API_KEY (optional, LM Studio may not require auth)
// Provider Type: openai
// Model: sonnet-4-5
// API Base: http://192.168.1.45:8085/v1/chat/completions
static inline void get_sonnet_4_5_config(LLMProviderConfig *config) {
    memset(config, 0, sizeof(*config));
    config->provider_type = PROVIDER_OPENAI;
    strlcpy(config->provider_name, "Local LM Studio", sizeof(config->provider_name));
    strlcpy(config->model, "sonnet-4-5", sizeof(config->model));
    strlcpy(config->api_base, "http://192.168.1.45:8085/v1/chat/completions", sizeof(config->api_base));

    // Use environment variable for API key if available
    const char *api_key = getenv("SONNET_4_5_API_KEY");
    if (api_key) {
        strlcpy(config->api_key, api_key, sizeof(config->api_key));
    }
}

// Provider: minimax-2.1 (MiniMax via Anthropic-compatible API)
// Environment: MINIMAX_API_KEY
// Provider Type: anthropic
// Model: MiniMax-M2.1
// API Base: https://api.minimax.io/anthropic/v1/messages
static inline void get_minimax_2_1_config(LLMProviderConfig *config) {
    memset(config, 0, sizeof(*config));
    config->provider_type = PROVIDER_ANTHROPIC;
    strlcpy(config->provider_name, "MiniMax", sizeof(config->provider_name));
    strlcpy(config->model, "MiniMax-M2.1", sizeof(config->model));
    strlcpy(config->api_base, "https://api.minimax.io/anthropic/v1/messages", sizeof(config->api_base));
    strlcpy(config->api_key_env, "MINIMAX_API_KEY", sizeof(config->api_key_env));

    // Load API key from environment
    const char *api_key = getenv("MINIMAX_API_KEY");
    if (api_key) {
        strlcpy(config->api_key, api_key, sizeof(config->api_key));
    }
}

// Provider: kimi-k2.5 (Moonshot AI - Moonshot/Kimi API)
// Environment: MOONSHOT_AI_API_KEY
// Provider Type: moonshot
// Model: kimi-k2.5
// API Base: https://api.moonshot.ai
static inline void get_kimi_k2_5_config(LLMProviderConfig *config) {
    memset(config, 0, sizeof(*config));
    config->provider_type = PROVIDER_MOONSHOT;
    strlcpy(config->provider_name, "Moonshot AI", sizeof(config->provider_name));
    strlcpy(config->model, "kimi-k2.5", sizeof(config->model));
    strlcpy(config->api_base, "https://api.moonshot.ai", sizeof(config->api_base));
    strlcpy(config->api_key_env, "MOONSHOT_AI_API_KEY", sizeof(config->api_key_env));

    // Load API key from environment
    const char *api_key = getenv("MOONSHOT_AI_API_KEY");
    if (api_key) {
        strlcpy(config->api_key, api_key, sizeof(config->api_key));
    }
}

// Provider: bedrock (AWS Bedrock - Claude Sonnet 4.5)
// Environment: AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, AWS_REGION
// Provider Type: bedrock
// Model: us.anthropic.claude-sonnet-4-5-20250929-v1:0
// Region: us-west-2
static inline void get_bedrock_config(LLMProviderConfig *config) {
    memset(config, 0, sizeof(*config));
    config->provider_type = PROVIDER_BEDROCK;
    config->use_bedrock = 1;
    strlcpy(config->provider_name, "AWS Bedrock", sizeof(config->provider_name));

    // Model from environment or default
    const char *model = getenv("ANTHROPIC_MODEL");
    if (!model) {
        model = "us.anthropic.claude-sonnet-4-5-20250929-v1:0";
    }
    strlcpy(config->model, model, sizeof(config->model));

    // Region is set via AWS_REGION env var, handled by AWS SDK
    // No api_key needed - uses AWS credential chain
}

// Check if a provider has required credentials
static inline int sonnet_4_5_is_configured(void) {
    // LM Studio local instance may not require auth
    return 1;
}

static inline int minimax_2_1_is_configured(void) {
    return getenv("MINIMAX_API_KEY") != NULL;
}

static inline int kimi_k2_5_is_configured(void) {
    return getenv("MOONSHOT_AI_API_KEY") != NULL;
}

static inline int bedrock_is_configured(void) {
    return getenv("AWS_ACCESS_KEY_ID") != NULL &&
           getenv("AWS_SECRET_ACCESS_KEY") != NULL;
}

// Get human-readable description of each provider
static inline const char* sonnet_4_5_description(void) {
    return "sonnet-4.5 (Local LM Studio @ 192.168.1.45:8085)";
}

static inline const char* minimax_2_1_description(void) {
    return "minimax-2.1 (MiniMax API via Anthropic-compatible endpoint)";
}

static inline const char* kimi_k2_5_description(void) {
    return "kimi-k2.5 (Moonshot AI API)";
}

static inline const char* bedrock_description(void) {
    static char desc[256];
    const char *model = getenv("ANTHROPIC_MODEL");
    if (!model) {
        model = "us.anthropic.claude-sonnet-4-5-20250929-v1:0";
    }
    const char *region = getenv("AWS_REGION");
    if (!region) {
        region = "us-west-2";
    }
    snprintf(desc, sizeof(desc),
             "bedrock (%s @ %s)",
             model, region);
    return desc;
}

#endif // TEST_PROVIDER_CONFIGS_H
