/*
 * provider.c - API Provider initialization and selection
 */

#define _POSIX_C_SOURCE 200809L

#include "provider.h"
#include "openai_provider.h"
#include "bedrock_provider.h"
#include "anthropic_provider.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>

// Default Anthropic API URL
#define DEFAULT_ANTHROPIC_URL "https://api.anthropic.com/v1/messages"

/**
 * Check if Bedrock mode is enabled via environment variable
 */
static int is_bedrock_enabled(void) {
    const char *use_bedrock = getenv("CLAUDE_CODE_USE_BEDROCK");
    return (use_bedrock &&
           (strcmp(use_bedrock, "1") == 0 ||
            strcmp(use_bedrock, "true") == 0 ||
            strcmp(use_bedrock, "TRUE") == 0));
}

/**
 * Get API URL from environment, or return default
 */
static char *get_api_url_from_env(void) {
    const char *env_url = getenv("OPENAI_API_BASE");
    if (!env_url || env_url[0] == '\0') {
        env_url = getenv("ANTHROPIC_API_URL");
    }
    if (env_url && env_url[0] != '\0') {
        char *url = strdup(env_url);
        if (url) LOG_INFO("Using API URL from environment: %s", url);
        return url;
    }
    char *default_url = strdup(DEFAULT_ANTHROPIC_URL);
    if (default_url) LOG_INFO("Using default API URL: %s", default_url);
    return default_url;
}

/**
 * Initialize the appropriate provider based on environment configuration.
 * Populates the provided result struct.
 */
void provider_init(const char *model,
                   const char *api_key,
                   ProviderInitResult *result) {
    // Initialize output
    result->provider = NULL;
    result->api_url = NULL;
    result->error_message = NULL;

    LOG_DEBUG("Initializing provider (model: %s)...", model ? model : "(null)");

    // Validate model argument
    if (!model || model[0] == '\0') {
        result->error_message = strdup("Model name is required");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        return;
    }

    // Bedrock provider selection
    if (is_bedrock_enabled()) {
        const char *use_bedrock_env = getenv("CLAUDE_CODE_USE_BEDROCK");
        const char *aws_profile = getenv("AWS_PROFILE");
        const char *aws_region = getenv("AWS_REGION");
        const char *aws_config_file = getenv("AWS_CONFIG_FILE");
        const char *aws_shared_creds = getenv("AWS_SHARED_CREDENTIALS_FILE");
        const char *aws_access_key = getenv("AWS_ACCESS_KEY_ID");
        const char *aws_secret_key = getenv("AWS_SECRET_ACCESS_KEY");
        const char *aws_session_token = getenv("AWS_SESSION_TOKEN");

        LOG_INFO("Bedrock mode is enabled, creating Bedrock provider...");
        LOG_INFO("Bedrock env summary: CLAUDE_CODE_USE_BEDROCK=%s",
                 use_bedrock_env ? use_bedrock_env : "(not set)");
        LOG_INFO("Bedrock env summary: AWS_PROFILE=%s, AWS_REGION=%s",
                 aws_profile ? aws_profile : "(not set)",
                 aws_region ? aws_region : "(not set)");
        LOG_INFO("Bedrock env summary: AWS_CONFIG_FILE=%s, AWS_SHARED_CREDENTIALS_FILE=%s",
                 aws_config_file ? aws_config_file : "(not set)",
                 aws_shared_creds ? aws_shared_creds : "(not set)");
        LOG_INFO("Bedrock env summary: Credentials present? access_key=%s secret_key=%s session_token=%s",
                 aws_access_key ? "yes" : "no",
                 aws_secret_key ? "yes" : "no",
                 aws_session_token ? "yes" : "no");

        Provider *prov = bedrock_provider_create(model);
        if (!prov) {
            result->error_message = strdup(
                "Failed to initialize Bedrock provider (check logs for details)");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            return;
        }
        BedrockConfig *cfg = (BedrockConfig *)prov->config;
        if (!cfg || !cfg->endpoint) {
            result->error_message = strdup(
                "Bedrock provider initialized but endpoint is missing");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(cfg->endpoint);
        if (!result->api_url) {
            result->error_message = strdup(
                "Failed to allocate memory for Bedrock endpoint");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        LOG_INFO("Provider initialization successful: Bedrock (endpoint: %s)",
                 result->api_url);
        return;
    }

    // Non-Bedrock: pick provider based on API URL
    if (!api_key || api_key[0] == '\0') {
        result->error_message = strdup("API key is required for API provider");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        return;
    }

    char *base_url = get_api_url_from_env();
    if (!base_url) {
        result->error_message = strdup("Failed to allocate memory for API URL");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        return;
    }

    int use_anthropic = 0;
    const char *anth_env = getenv("ANTHROPIC_API_URL");
    if (anth_env && anth_env[0] != '\0') {
        use_anthropic = 1;
    }
    if (strstr(base_url, "anthropic.com") != NULL) {
        use_anthropic = 1;
    }

    if (use_anthropic) {
        LOG_INFO("Using Anthropic provider (direct API)...");
        Provider *prov = anthropic_provider_create(api_key, base_url);
        free(base_url);
        if (!prov) {
            result->error_message = strdup("Failed to initialize Anthropic provider (check logs for details)");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            return;
        }
        AnthropicConfig *cfg = (AnthropicConfig *)prov->config;
        if (!cfg || !cfg->base_url) {
            result->error_message = strdup("Anthropic provider initialized but base URL is missing");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        result->provider = prov;
        result->api_url = strdup(cfg->base_url);
        if (!result->api_url) {
            result->error_message = strdup("Failed to allocate memory for API URL");
            LOG_ERROR("Provider init failed: %s", result->error_message);
            prov->cleanup(prov);
            return;
        }
        LOG_INFO("Provider initialization successful: Anthropic (endpoint: %s)", result->api_url);
        return;
    }

    LOG_INFO("Using OpenAI-compatible provider...");

    Provider *prov = openai_provider_create(api_key, base_url);
    free(base_url);  // openai_provider_create made its own copy
    if (!prov) {
        result->error_message = strdup(
            "Failed to initialize OpenAI provider (check logs for details)");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        return;
    }
    OpenAIConfig *cfg = (OpenAIConfig *)prov->config;
    if (!cfg || !cfg->base_url) {
        result->error_message = strdup(
            "OpenAI provider initialized but base URL is missing");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        prov->cleanup(prov);
        return;
    }
    result->provider = prov;
    result->api_url = strdup(cfg->base_url);
    if (!result->api_url) {
        result->error_message = strdup(
            "Failed to allocate memory for API URL");
        LOG_ERROR("Provider init failed: %s", result->error_message);
        prov->cleanup(prov);
        return;
    }
    LOG_INFO("Provider initialization successful: OpenAI (base URL: %s)",
             result->api_url);
}
