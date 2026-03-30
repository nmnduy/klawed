/*
 * model_capabilities.c - Model-specific capability implementations
 */

#define _POSIX_C_SOURCE 200809L

#include "model_capabilities.h"
#include "logger.h"
#include <string.h>
#include <ctype.h>

/**
 * Perform case-insensitive prefix match
 */
static int strcasecmp_prefix(const char *model, const char *prefix) {
    if (!model || !prefix) return -1;

    size_t prefix_len = strlen(prefix);
    size_t model_len = strlen(model);

    if (model_len < prefix_len) return -1;

    // Compare case-insensitively
    for (size_t i = 0; i < prefix_len; i++) {
        int mc = tolower((unsigned char)model[i]);
        int pc = tolower((unsigned char)prefix[i]);
        if (mc != pc) return (mc < pc) ? -1 : 1;
    }

    return 0;  // Match
}

ModelCapabilities get_model_capabilities(const char *model,
                                        int default_context_limit,
                                        int default_max_output_tokens) {
    ModelCapabilities caps = {
        .context_limit = default_context_limit,
        .max_output_tokens = default_max_output_tokens
    };

    if (!model || model[0] == '\0') {
        LOG_DEBUG("model_capabilities: null or empty model, using defaults ctx=%d max_out=%d",
                 default_context_limit, default_max_output_tokens);
        return caps;
    }

    // Iterate through table and find first prefix match
    for (size_t i = 0; i < MODEL_CAPABILITIES_TABLE_SIZE; i++) {
        const ModelCapabilityEntry *entry = &MODEL_CAPABILITIES_TABLE[i];
        if (entry->model_prefix && strcasecmp_prefix(model, entry->model_prefix) == 0) {
            caps.context_limit = entry->context_limit;
            caps.max_output_tokens = entry->max_output_tokens;
            LOG_DEBUG("model_capabilities: model '%s' matched '%s', ctx=%d max_out=%d",
                     model, entry->model_prefix, caps.context_limit, caps.max_output_tokens);
            return caps;
        }
    }

    // No match found - use defaults
    LOG_DEBUG("model_capabilities: no match for model '%s', using defaults ctx=%d max_out=%d",
             model, default_context_limit, default_max_output_tokens);
    return caps;
}

int get_model_max_tokens(const char *model, int default_max_tokens) {
    ModelCapabilities caps = get_model_capabilities(model, 0, default_max_tokens);
    return caps.max_output_tokens > 0 ? caps.max_output_tokens : default_max_tokens;
}

int get_model_context_limit(const char *model, int default_context_limit) {
    ModelCapabilities caps = get_model_capabilities(model, default_context_limit, 0);
    return caps.context_limit > 0 ? caps.context_limit : default_context_limit;
}

int get_safe_max_tokens(const char *model, int prompt_tokens,
                       int original_max_tokens, int context_buffer) {
    // Get model context limit
    int context_limit = get_model_context_limit(model, 128000);  // Safe default

    // Calculate remaining space
    int remaining = context_limit - prompt_tokens - context_buffer;

    // Cap max_tokens to remaining space
    if (remaining <= 0) {
        LOG_WARN("model_capabilities: context full (prompt=%d, limit=%d, buffer=%d), setting max_tokens=0",
                 prompt_tokens, context_limit, context_buffer);
        return 0;
    }

    int safe_max = remaining < original_max_tokens ? remaining : original_max_tokens;

    if (safe_max < original_max_tokens) {
        LOG_INFO("model_capabilities: capping max_tokens %d -> %d (prompt=%d, limit=%d, buffer=%d)",
                 original_max_tokens, safe_max, prompt_tokens, context_limit, context_buffer);
    }

    return safe_max;
}