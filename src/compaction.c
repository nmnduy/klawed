#include "klawed_internal.h"
#include "compaction.h"
#include "tool_utils.h"
#include "logger.h"
#include "persistence.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef HAVE_MEMVID
#include "memvid.h"
#endif

// Default configuration values
#define DEFAULT_COMPACT_THRESHOLD 60    // 60% of token limit
#define DEFAULT_KEEP_RECENT 20          // Keep last 20 messages
#define DEFAULT_TOKEN_LIMIT 125000      // Default model token limit (125k)

/**
 * Estimate token count for text
 * Simple heuristic: ~4 characters per token for English text
 */
static inline size_t estimate_tokens(const char *text) {
    if (!text) return 0;
    size_t char_count = strlen(text);
    return (char_count + 3) / 4; // Round up
}

void compaction_init_config(CompactionConfig *config, int enabled, const char *model_name) {
    if (!config) return;

    config->enabled = enabled;

    // Get threshold from environment or use default
    const char *threshold_str = getenv("KLAWED_COMPACT_THRESHOLD");
    if (threshold_str && atoi(threshold_str) > 0) {
        config->threshold_percent = atoi(threshold_str);
    } else {
        config->threshold_percent = DEFAULT_COMPACT_THRESHOLD;
    }

    // Get keep_recent from environment or use default
    const char *keep_recent_str = getenv("KLAWED_COMPACT_KEEP_RECENT");
    if (keep_recent_str && atoi(keep_recent_str) > 0) {
        config->keep_recent = atoi(keep_recent_str);
    } else {
        config->keep_recent = DEFAULT_KEEP_RECENT;
    }

    // Get model token limit from environment or use default
    const char *token_limit_str = getenv("KLAWED_CONTEXT_LIMIT");
    if (token_limit_str && atoi(token_limit_str) > 0) {
        config->model_token_limit = atoi(token_limit_str);
        LOG_DEBUG("Using token limit from KLAWED_CONTEXT_LIMIT: %d", config->model_token_limit);
    } else {
        config->model_token_limit = DEFAULT_TOKEN_LIMIT;
        LOG_DEBUG("Using default token limit: %d", DEFAULT_TOKEN_LIMIT);
    }
    (void)model_name; // Unused parameter

    config->last_compacted_index = -1; // No compaction yet
    config->current_tokens = 0;        // Will be updated during conversation

#ifndef HAVE_MEMVID
    if (enabled) {
        fprintf(stderr, "Warning: Auto-compact enabled but memvid not available. Compaction disabled.\n");
        config->enabled = 0;
    }
#endif
}

size_t compaction_estimate_message_tokens(const InternalMessage *msg) {
    if (!msg || msg->content_count == 0) {
        return 0;
    }

    size_t total = 0;

    // Add tokens for role (roughly 2-3 tokens)
    total += 3;

    // Estimate tokens for each content block
    for (int i = 0; i < msg->content_count; i++) {
        const InternalContent *content = &msg->contents[i];

        switch (content->type) {
            case INTERNAL_TEXT:
                if (content->text) {
                    total += estimate_tokens(content->text);
                }
                break;

            case INTERNAL_TOOL_CALL:
                // Tool call includes: tool name + parameters JSON
                total += 5; // tool call overhead
                if (content->tool_name) {
                    total += estimate_tokens(content->tool_name);
                }
                if (content->tool_params) {
                    char *params_str = cJSON_PrintUnformatted(content->tool_params);
                    if (params_str) {
                        total += estimate_tokens(params_str);
                        free(params_str);
                    }
                }
                break;

            case INTERNAL_TOOL_RESPONSE:
                // Tool response includes: tool_id + result content
                total += 5; // tool response overhead
                if (content->tool_id) {
                    total += estimate_tokens(content->tool_id);
                }
                if (content->tool_output) {
                    char *output_str = cJSON_PrintUnformatted(content->tool_output);
                    if (output_str) {
                        total += estimate_tokens(output_str);
                        free(output_str);
                    }
                }
                break;

            case INTERNAL_IMAGE:
                // Images are tokenized based on size
                // Rough estimate: 85 tokens per image tile (170 tokens for high-res)
                // For simplicity, use a fixed estimate
                total += 500; // Conservative estimate for an image
                break;

            default:
                // Unknown content type, skip
                break;
        }
    }

    // Add overhead for message formatting (JSON structure, etc.)
    total += 10;

    return total;
}

size_t compaction_update_token_count(const ConversationState *state, CompactionConfig *config) {
    if (!state || !config) {
        return 0;
    }

    size_t total_tokens = 0;

    // First try to get actual token count from API usage tracking
    if (state->persistence_db && state->session_id) {
        int prompt_tokens = 0;
        if (persistence_get_last_prompt_tokens(state->persistence_db, state->session_id, &prompt_tokens) == 0
            && prompt_tokens > 0) {
            total_tokens = (size_t)prompt_tokens;
            LOG_DEBUG("Updated token count from API call tracking: %zu tokens", total_tokens);
            config->current_tokens = total_tokens;
            return total_tokens;
        }
    }

    // Fallback: Estimate tokens from messages if no API data available
    for (int i = 0; i < state->count; i++) {
        total_tokens += compaction_estimate_message_tokens(&state->messages[i]);
    }

    // Add overhead for API request structure
    total_tokens += 100; // Request overhead, tool definitions, etc.

    LOG_DEBUG("Estimated token count from messages: %zu tokens", total_tokens);
    config->current_tokens = total_tokens;
    return total_tokens;
}

int compaction_should_trigger(const ConversationState *state, const CompactionConfig *config) {
    if (!config || !config->enabled || !state) {
        return 0;
    }

#ifndef HAVE_MEMVID
    return 0; // Can't compact without memvid
#else
    // Calculate token threshold
    size_t threshold_tokens = (size_t)((config->model_token_limit * config->threshold_percent) / 100);

    // Get current token count from actual API usage tracking
    size_t current_tokens = config->current_tokens;

    // If not cached or zero, try to get from persistence database
    if (current_tokens == 0 && state->persistence_db && state->session_id) {
        int prompt_tokens = 0;
        if (persistence_get_last_prompt_tokens(state->persistence_db, state->session_id, &prompt_tokens) == 0) {
            current_tokens = (size_t)prompt_tokens;
            LOG_DEBUG("Using actual token count from API call: %zu tokens", current_tokens);
        }
    }

    // If still zero (no API calls yet, or persistence unavailable), estimate
    if (current_tokens == 0) {
        current_tokens = 0;
        for (int i = 0; i < state->count; i++) {
            current_tokens += compaction_estimate_message_tokens(&state->messages[i]);
        }
        current_tokens += 100; // API overhead
        LOG_DEBUG("Estimated token count from messages: %zu tokens", current_tokens);
    }

    LOG_DEBUG("Compaction check: %zu tokens / %d limit (threshold: %zu at %d%%)",
              current_tokens, config->model_token_limit, threshold_tokens, config->threshold_percent);

    // Trigger if we're at or above threshold
    return (current_tokens >= threshold_tokens);
#endif
}

#ifdef HAVE_MEMVID
// Helper function to get text content from a message
static const char* get_message_text(const InternalMessage *msg) {
    if (!msg || msg->content_count == 0) {
        return "";
    }

    // For simplicity, just get the first content block if it's text
    if (msg->contents[0].type == INTERNAL_TEXT && msg->contents[0].text) {
        return msg->contents[0].text;
    }

    return "";
}

// Helper function to free message contents
static void free_message_contents(InternalMessage *msg) {
    if (!msg) return;

    for (int j = 0; j < msg->content_count; j++) {
        if (msg->contents[j].text) {
            free(msg->contents[j].text);
            msg->contents[j].text = NULL;
        }
    }
    if (msg->contents) {
        free(msg->contents);
        msg->contents = NULL;
    }
    msg->content_count = 0;
}

static int compaction_store_message(const InternalMessage *msg, int msg_index, const char *session_id) {
    if (!msg) {
        return -1;
    }

    const char *text = get_message_text(msg);
    if (!text || text[0] == '\0') {
        // Empty message, skip but not an error
        return 0;
    }

    // Build entity name based on session
    char entity[256];
    if (session_id && session_id[0] != '\0') {
        snprintf(entity, sizeof(entity), "session.%s", session_id);
    } else {
        snprintf(entity, sizeof(entity), "conversation.history");
    }

    // Build slot name based on message index
    char slot[64];
    snprintf(slot, sizeof(slot), "msg_%d", msg_index);

    // Build value with role and content
    const char *role_str = "unknown";
    switch (msg->role) {
        case MSG_USER: role_str = "user"; break;
        case MSG_ASSISTANT: role_str = "assistant"; break;
        case MSG_SYSTEM: role_str = "system"; break;
        default: role_str = "unknown"; break;
    }

    // Store as simple text: "role: content"
    size_t value_len = strlen(role_str) + 2 + strlen(text) + 1;
    char *value = malloc(value_len);
    if (!value) {
        return -1;
    }
    snprintf(value, value_len, "%s: %s", role_str, text);

    // Store in memvid
    MemvidHandle *handle = memvid_get_global();
    if (!handle) {
        free(value);
        return -1;
    }

    int64_t card_id = memvid_put_memory(handle, entity, slot, value,
                                        MEMVID_KIND_EVENT, MEMVID_RELATION_SETS);
    free(value);

    return (card_id >= 0) ? 0 : -1;
}
#endif /* HAVE_MEMVID */

int compaction_perform(ConversationState *state, CompactionConfig *config, const char *session_id) {
#ifndef HAVE_MEMVID
    (void)state;
    (void)config;
    (void)session_id;
    fprintf(stderr, "Warning: Compaction requested but memvid not available. Skipping.\n");
    return -1;
#else
    if (!state || !config) {
        return -1;
    }

    if (state->count <= config->keep_recent + 1) { // +1 for system message
        // Not enough messages to compact
        return 0;
    }

    // Calculate which messages to compact
    // Always keep: message 0 (system) + last keep_recent messages
    int compact_start = 1; // Start after system message
    int compact_end = state->count - config->keep_recent - 1; // Last index to compact

    if (compact_end < compact_start) {
        // Nothing to compact
        return 0;
    }

    int compacted_count = compact_end - compact_start + 1;

    LOG_INFO("Compacting messages %d-%d (keeping last %d messages)",
           compact_start, compact_end, config->keep_recent);

    // Track structured information
    int write_count = 0, edit_count = 0, bash_count = 0, read_count = 0;

    // Store each message to memvid and count tool usage
    for (int i = compact_start; i <= compact_end; i++) {
        if (compaction_store_message(&state->messages[i], i, session_id) != 0) {
            LOG_WARN("Failed to store message %d to memvid", i);
        }

        // Extract structured information from assistant messages (tool calls)
        const InternalMessage *msg = &state->messages[i];
        const char *text = get_message_text(msg);

        // Simple tool usage counting - look for tool call patterns
        if (msg->role == MSG_ASSISTANT && text) {
            if (strstr(text, "\"name\": \"Write\"") != NULL ||
                strstr(text, "\"name\":\"Write\"") != NULL) write_count++;
            if (strstr(text, "\"name\": \"Edit\"") != NULL ||
                strstr(text, "\"name\":\"Edit\"") != NULL) edit_count++;
            if (strstr(text, "\"name\": \"Bash\"") != NULL ||
                strstr(text, "\"name\":\"Bash\"") != NULL) bash_count++;
            if (strstr(text, "\"name\": \"Read\"") != NULL ||
                strstr(text, "\"name\":\"Read\"") != NULL) read_count++;
        }
    }

    // Calculate token information
    size_t tokens_before = config->current_tokens;

    // Estimate tokens being removed
    size_t tokens_compacted = 0;
    for (int i = compact_start; i <= compact_end; i++) {
        tokens_compacted += compaction_estimate_message_tokens(&state->messages[i]);
    }

    // Estimate remaining tokens after compaction
    size_t tokens_after = 0;
    // System message
    tokens_after += compaction_estimate_message_tokens(&state->messages[0]);
    // Recent messages
    for (int i = compact_end + 1; i < state->count; i++) {
        tokens_after += compaction_estimate_message_tokens(&state->messages[i]);
    }
    // Notice message (estimate ~50 tokens)
    tokens_after += 50;
    // API overhead
    tokens_after += 100;

    double usage_percent = (double)tokens_before / config->model_token_limit * 100.0;
    double after_percent = (double)tokens_after / config->model_token_limit * 100.0;

    // Build compaction notice
    char notice[2048];

    snprintf(notice, sizeof(notice),
        "## Context Compaction Notice\n"
        "%d earlier messages have been stored in memory. "
        "Use MemorySearch to retrieve relevant past context if needed.\n\n"
        "Session: %s\n"
        "Messages compacted: %d-%d\n"
        "Tools used: Read (%d), Write (%d), Edit (%d), Bash (%d)\n"
        "Tokens: %zu → %zu (freed ~%zu tokens)\n"
        "Context usage: %.1f%% → %.1f%% of %d token limit\n",
        compacted_count,
        session_id ? session_id : "(no session)",
        compact_start, compact_end,
        read_count, write_count, edit_count, bash_count,
        tokens_before, tokens_after, tokens_compacted,
        usage_percent, after_percent, config->model_token_limit);

    // Free the messages being compacted
    for (int i = compact_start; i <= compact_end; i++) {
        free_message_contents(&state->messages[i]);
    }

    // Create compaction notice message
    InternalMessage notice_msg = {0};
    notice_msg.role = MSG_SYSTEM;
    notice_msg.contents = malloc(sizeof(InternalContent));
    if (!notice_msg.contents) {
        LOG_ERROR("Failed to allocate memory for compaction notice");
        return -1;
    }
    notice_msg.content_count = 1;
    notice_msg.contents[0].type = INTERNAL_TEXT;
    notice_msg.contents[0].text = strdup(notice);
    if (!notice_msg.contents[0].text) {
        LOG_ERROR("Failed to allocate memory for compaction notice text");
        free(notice_msg.contents);
        return -1;
    }

    // Reorganize messages:
    // [0: system] [1..compact_end: compacted] [compact_end+1..count-1: recent]
    // becomes:
    // [0: system] [1: notice] [2..keep_recent+1: recent messages]

    // Recent messages are from (compact_end + 1) to (state->count - 1)
    int recent_start = compact_end + 1;
    int recent_count = state->count - recent_start;

    // Move recent messages right after the notice (position 2 onwards)
    for (int i = 0; i < recent_count; i++) {
        state->messages[2 + i] = state->messages[recent_start + i];
    }

    // Insert notice at position 1 (right after system message)
    state->messages[1] = notice_msg;

    // Update count: system + notice + recent messages
    state->count = 1 + 1 + recent_count;

    // Update last compacted index and token count
    config->last_compacted_index = compact_end;
    config->current_tokens = tokens_after;

    LOG_INFO("Compaction complete. Messages: %d, Tokens: %zu (%.1f%% of limit)",
             state->count, tokens_after, after_percent);

    return 0;
#endif
}
