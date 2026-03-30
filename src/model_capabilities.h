/*
 * model_capabilities.h - Model-specific capability definitions
 *
 * This module provides model-specific configuration values, including
 * maximum output tokens and context limits. The system uses pattern 
 * matching to find appropriate values when a model is configured.
 *
 * Important: max_tokens is a ceiling, not a target. The model may output
 * less if it completes naturally. But if context is full, output will be
 * truncated. Use get_safe_max_tokens() to cap max_tokens based on
 * remaining context space.
 */

#ifndef MODEL_CAPABILITIES_H
#define MODEL_CAPABILITIES_H

#include <stddef.h>

// Model capability entry with context limit and max output tokens
// Values are sourced from OpenRouter API and model documentation
typedef struct {
    const char *model_prefix;   // Model name prefix (case-insensitive prefix match)
    int context_limit;          // Maximum context (input + output) in tokens
    int max_output_tokens;      // Maximum output tokens for this model
} ModelCapabilityEntry;

// Table of model-specific capabilities
// Order matters: more specific prefixes should come before general ones
// If no match found, returns default context_limit and max_output_tokens
static const ModelCapabilityEntry MODEL_CAPABILITIES_TABLE[] = {
    // ========================================================================
    // OpenAI o-series (reasoning models)
    // ========================================================================
    { "o4-mini-high", 200000, 100000 },
    { "o4-mini", 200000, 100000 },
    { "o4-mini-deep-research", 200000, 100000 },
    { "o3-pro", 200000, 100000 },
    { "o3-deep-research", 200000, 100000 },
    { "o3", 200000, 100000 },
    { "o1-pro", 200000, 100000 },
    { "o1", 200000, 100000 },
    { "o3-mini-high", 200000, 100000 },
    { "o3-mini", 200000, 100000 },
    { "o1-mini", 200000, 100000 },

    // ========================================================================
    // OpenAI GPT-5 series (latest generation)
    // ========================================================================
    { "gpt-5.4-pro", 1050000, 128000 },
    { "gpt-5.4", 1050000, 128000 },
    { "gpt-5.4-nano", 400000, 128000 },
    { "gpt-5.4-mini", 400000, 128000 },
    { "gpt-5.3-codex", 400000, 128000 },
    { "gpt-5.3-chat", 128000, 16384 },
    { "gpt-5.2-pro", 400000, 128000 },
    { "gpt-5.2-codex", 400000, 128000 },
    { "gpt-5.2-chat", 128000, 16384 },
    { "gpt-5.2", 400000, 128000 },
    { "gpt-5.1-codex-max", 400000, 128000 },
    { "gpt-5.1-codex-mini", 400000, 100000 },
    { "gpt-5.1-codex", 400000, 128000 },
    { "gpt-5.1", 400000, 128000 },
    { "gpt-5.1-chat", 128000, 16384 },
    { "gpt-5-image", 400000, 128000 },
    { "gpt-5-image-mini", 400000, 128000 },
    { "gpt-5-pro", 400000, 128000 },
    { "gpt-5-codex", 400000, 128000 },
    { "gpt-5-chat", 128000, 16384 },
    { "gpt-5-mini", 400000, 128000 },
    { "gpt-5-nano", 400000, 128000 },
    { "gpt-5", 400000, 128000 },

    // ========================================================================
    // OpenAI GPT-4 series
    // ========================================================================
    { "gpt-4.1", 1047576, 32768 },
    { "gpt-4.1-mini", 1047576, 32768 },
    { "gpt-4.1-nano", 1047576, 32768 },
    { "gpt-4o-2024-11-20", 128000, 16384 },
    { "gpt-4o-2024-08-06", 128000, 16384 },
    { "gpt-4o-2024-05-13", 128000, 4096 },
    { "gpt-4o:extended", 128000, 64000 },
    { "gpt-4o-search-preview", 128000, 16384 },
    { "gpt-4o-mini-search-preview", 128000, 16384 },
    { "gpt-4o-mini", 128000, 16384 },
    { "gpt-4o-mini-2024-07-18", 128000, 16384 },
    { "gpt-4o", 128000, 16384 },
    { "gpt-4-turbo-preview", 128000, 4096 },
    { "gpt-4-turbo-2024-04-29", 128000, 4096 },
    { "gpt-4-turbo", 128000, 4096 },
    { "gpt-4-1106-preview", 128000, 4096 },
    { "gpt-4-32k", 32768, 8192 },
    { "gpt-4", 8191, 4096 },

    // ========================================================================
    // Anthropic Claude 4 series
    // ========================================================================
    { "claude-opus-4.6", 1000000, 128000 },
    { "claude-sonnet-4.6", 1000000, 128000 },
    { "claude-opus-4.5", 200000, 64000 },
    { "claude-sonnet-4.5", 1000000, 64000 },
    { "claude-haiku-4.5", 200000, 64000 },
    { "claude-opus-4", 200000, 32000 },
    { "claude-sonnet-4", 200000, 64000 },
    { "claude-3.7-sonnet", 200000, 64000 },
    { "claude-3.5-sonnet", 200000, 8192 },
    { "claude-3.5-haiku", 200000, 8192 },
    { "claude-3-haiku", 200000, 4096 },
    { "claude-opus-4.1", 200000, 32000 },

    // ========================================================================
    // Google Gemini series
    // ========================================================================
    { "gemini-3.1-pro-preview-customtools", 1048576, 65536 },
    { "gemini-3.1-pro-preview", 1048576, 65536 },
    { "gemini-3.1-flash-lite-preview", 1048576, 65536 },
    { "gemini-3.1-flash-image-preview", 65536, 65536 },
    { "gemini-3-flash-preview", 1048576, 65536 },
    { "gemini-3-pro-image-preview", 65536, 32768 },
    { "gemini-2.5-pro-preview-05-06", 1048576, 65535 },
    { "gemini-2.5-pro-preview", 1048576, 65536 },
    { "gemini-2.5-pro", 1048576, 65536 },
    { "gemini-2.5-flash-lite-preview-09-2025", 1048576, 65535 },
    { "gemini-2.5-flash-lite", 1048576, 65535 },
    { "gemini-2.5-flash-image", 32768, 32768 },
    { "gemini-2.5-flash", 1048576, 65535 },
    { "gemini-2.0-flash-001", 1048576, 8192 },
    { "gemini-2.0-flash-lite-001", 1048576, 8192 },

    // ========================================================================
    // Kimi/Moonshot models
    // ========================================================================
    { "kimi-k2.5", 262144, 65535 },
    { "kimi-k2", 131072, 131072 },
    { "kimi-for-coding", 128000, 16384 },

    // ========================================================================
    // Minimax models
    // ========================================================================
    { "minimax-m2.7", 204800, 131072 },
    { "minimax-m2.5", 196608, 65536 },
    { "minimax-m2", 196608, 196608 },
    { "minimax-m1", 1000000, 40000 },
    { "minimax-01", 1000192, 1000192 },
    { "minimax-m2-her", 65536, 2048 },

    // ========================================================================
    // Z.ai / GLM models
    // ========================================================================
    { "glm-5-turbo", 202752, 131072 },
    { "glm-5", 80000, 131072 },
    { "glm-4.7", 202752, 65535 },
    { "glm-4.6v", 131072, 131072 },
    { "glm-4.6", 204800, 204800 },
    { "glm-4.5v", 65536, 16384 },
    { "glm-4.5-air", 131072, 98304 },
    { "glm-4.5", 131072, 98304 },

    // ========================================================================
    // DeepSeek models
    // ========================================================================
    { "deepseek-v3.2-speciale", 163840, 163840 },
    { "deepseek-v3.1-nex-n1", 131072, 163840 },
    { "deepseek-chat-v3.1", 32768, 7168 },
    { "deepseek-r1-0528", 163840, 65536 },
    { "deepseek-r1-distill-qwen-32b", 32768, 32768 },
    { "deepseek-r1-distill-llama-70b", 131072, 16384 },
    { "deepseek-r1", 64000, 16000 },
    { "deepseek-chat", 163840, 163840 },

    // ========================================================================
    // Qwen models
    // ========================================================================
    { "qwen3-max", 262144, 32768 },
    { "qwen3-coder-30b-a3b-instruct", 160000, 32768 },
    { "qwen3-30b-a3b-instruct-2507", 262144, 262144 },
    { "qwen3-30b-a3b", 40960, 40960 },
    { "qwen3-235b-a22b", 131072, 8192 },
    { "qwen3-max-thinking", 262144, 32768 },
    { "qwen3-coder-next", 262144, 65536 },
    { "qwen3-coder-plus", 1000000, 65536 },
    { "qwen3-coder-flash", 1000000, 65536 },
    { "qwen3-coder", 262000, 262000 },
    { "qwen3-plus-2025-07-28:thinking", 1000000, 32768 },
    { "qwen3-plus-2025-07-28", 1000000, 32768 },
    { "qwen3-8b", 40960, 8192 },
    { "qwen3-14b", 40960, 40960 },
    { "qwen3-32b", 40960, 40960 },
    { "qwen3-vl-235b-a22b-thinking", 131072, 32768 },
    { "qwen3-vl-30b-a3b-thinking", 131072, 32768 },
    { "qwen3-vl-30b-a3b-instruct", 131072, 32768 },
    { "qwen3-vl-32b-instruct", 131072, 32768 },
    { "qwen3-vl-8b-thinking", 131072, 32768 },
    { "qwen3-vl-8b-instruct", 131072, 32768 },
    { "qwen3-35b-a3b", 262144, 65536 },
    { "qwen3-27b", 262144, 65536 },
    { "qwen3-122b-a10b", 262144, 65536 },
    { "qwen3-397b-a17b", 262144, 65536 },
    { "qwen3-flash-02-23", 1000000, 65536 },
    { "qwen3-plus-02-15", 1000000, 65536 },
    { "qwen3.5-flash-02-23", 1000000, 65536 },
    { "qwen3.5-9b", 256000, 65536 },
    { "qwen3.5-35b-a3b", 262144, 65536 },
    { "qwen3.5-27b", 262144, 65536 },
    { "qwen3.5-122b-a10b", 262144, 65536 },
    { "qwen3.5-397b-a17b", 262144, 65536 },
    { "qwen3.5-plus-02-15", 1000000, 65536 },
    { "qwen-2.5-7b-instruct", 32768, 32768 },
    { "qwen-2.5-72b-instruct", 32768, 16384 },
    { "qwen-2.5-vl-72b-instruct", 32768, 32768 },
    { "qwen-turbo", 131072, 8192 },
    { "qwen-max", 32768, 8192 },
    { "qwen-plus", 1000000, 32768 },
    { "qwen-vl-plus", 131072, 8192 },
    { "qwen-vl-max", 131072, 32768 },
    { "qwq-32b", 131072, 131072 },

    // ========================================================================
    // Mistral models
    // ========================================================================
    { "mistral-small-4-119b-2603", 262144, 131072 },
    { "mistral-small-3.1-24b-instruct", 131072, 131072 },
    { "mistral-small-24b-instruct-2501", 32768, 16384 },
    { "mistral-small", 131072, 16384 },
    { "mistral-nemo", 131072, 16384 },
    { "mixtral-8x7b-instruct", 32768, 16384 },

    // ========================================================================
    // Other notable models
    // ========================================================================
    { "nova-premier", 1000000, 32000 },
    { "nova-2-lite-v1", 1000000, 65535 },

    // ========================================================================
    // Default fallback for unknown models (defined in klawed_internal.h)
    // ========================================================================
    // Context limit and max_output_tokens handled by get_model_capabilities() default params
};

#define MODEL_CAPABILITIES_TABLE_SIZE (sizeof(MODEL_CAPABILITIES_TABLE) / sizeof(ModelCapabilityEntry))

// Legacy alias for backwards compatibility
#define MODEL_MAX_TOKENS_TABLE MODEL_CAPABILITIES_TABLE
#define ModelMaxTokensEntry ModelCapabilityEntry
#define MODEL_MAX_TOKENS_TABLE_SIZE MODEL_CAPABILITIES_TABLE_SIZE

/**
 * Model capabilities structure returned by get_model_capabilities()
 */
typedef struct {
    int context_limit;      // Maximum context (input + output) in tokens
    int max_output_tokens;  // Maximum output tokens for this model
} ModelCapabilities;

/**
 * Get model capabilities (context limit and max output tokens)
 *
 * Performs a case-insensitive prefix match against the model name.
 * More specific prefixes should be listed earlier in the table.
 *
 * @param model The model name (can be NULL)
 * @param default_context_limit Fallback context limit (e.g., from klawed_internal.h CONTEXT_LIMIT)
 * @param default_max_output_tokens Fallback max output (e.g., from klawed_internal.h MAX_TOKENS)
 * @return ModelCapabilities with context_limit and max_output_tokens
 */
ModelCapabilities get_model_capabilities(const char *model, 
                                        int default_context_limit, 
                                        int default_max_output_tokens);

/**
 * Get the maximum output tokens for a given model (legacy function)
 *
 * @param model The model name (can be NULL, returns default)
 * @param default_max_tokens Fallback value if no match is found
 * @return The maximum output tokens for the model, or default_max_tokens if not found
 */
int get_model_max_tokens(const char *model, int default_max_tokens);

/**
 * Get the context limit for a given model
 *
 * @param model The model name (can be NULL)
 * @param default_context_limit Fallback value if no match is found
 * @return The context limit for the model, or default_context_limit if not found
 */
int get_model_context_limit(const char *model, int default_context_limit);

/**
 * Calculate safe max_tokens based on actual prompt tokens used
 *
 * This function uses the actual prompt token count from the API response
 * to calculate a safe max_tokens that won't exceed the context limit.
 *
 * @param model The model name
 * @param prompt_tokens Actual tokens used in the prompt (from API response)
 * @param original_max_tokens Original max_tokens to cap
 * @param context_buffer Extra buffer to reserve for system prompts, etc.
 * @return Safe max_tokens that won't exceed context limit
 */
int get_safe_max_tokens(const char *model, int prompt_tokens, 
                        int original_max_tokens, int context_buffer);

#endif /* MODEL_CAPABILITIES_H */