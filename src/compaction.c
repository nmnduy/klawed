#include "klawed_internal.h"
#include "compaction.h"
#include "tool_utils.h"
#include "logger.h"
#include "persistence.h"
#include "http_client.h"
#include "provider.h"
#include "memory_db.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <bsd/string.h>

// Default configuration values
#define DEFAULT_COMPACT_THRESHOLD 75    // 75% of token limit
#define DEFAULT_KEEP_RECENT 100         // Keep last 100 messages
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

// ============================================================================
// Summarization for Compaction
// ============================================================================

// Summarization prompt template
static const char *SUMMARIZATION_PROMPT =
    "You are summarizing a conversation segment that is being archived. "
    "Provide a concise summary (250-400 words) that captures:\n\n"
    "1. **What was being worked on**: Summarize the main activities and tasks performed\n"
    "2. **Current goals/objectives**: What the user is trying to accomplish\n"
    "3. **Task state/progress**: Current status, what has been completed, what remains\n\n"
    "Focus on actionable context that would help continue the work. "
    "Be specific about file names, function names, and technical details mentioned. "
    "Write in a clear, professional style.\n\n"
    "Here is the conversation to summarize:\n\n";

/**
 * Helper to get role string for a message
 */
static const char* get_role_string(MessageRole role) {
    switch (role) {
        case MSG_USER: return "User";
        case MSG_ASSISTANT: return "Assistant";
        case MSG_SYSTEM: return "System";
        case MSG_AUTO_COMPACTION: return "System (Compaction)";
        default: return "Unknown";
    }
}

/**
 * Build conversation text from messages for summarization
 * Returns: Allocated string (caller must free), or NULL on error
 */
static char* build_conversation_text(const InternalMessage *messages, int message_count) {
    if (!messages || message_count <= 0) {
        return NULL;
    }

    // Estimate total size needed
    size_t total_size = 0;
    for (int i = 0; i < message_count; i++) {
        const InternalMessage *msg = &messages[i];
        total_size += 32; // Role header + newlines
        for (int j = 0; j < msg->content_count; j++) {
            if (msg->contents[j].type == INTERNAL_TEXT && msg->contents[j].text) {
                total_size += strlen(msg->contents[j].text) + 4;
            } else if (msg->contents[j].type == INTERNAL_TOOL_CALL && msg->contents[j].tool_name) {
                total_size += 64; // Tool call summary
            }
        }
    }

    // Add buffer for safety
    total_size += 1024;

    char *result = malloc(total_size);
    if (!result) {
        return NULL;
    }
    result[0] = '\0';
    size_t current_len = 0;

    for (int i = 0; i < message_count; i++) {
        const InternalMessage *msg = &messages[i];
        const char *role = get_role_string(msg->role);

        // Add role header
        char header[64];
        snprintf(header, sizeof(header), "**%s**:\n", role);
        strlcat(result, header, total_size);
        current_len = strlen(result);

        // Add content
        for (int j = 0; j < msg->content_count; j++) {
            const InternalContent *content = &msg->contents[j];
            if (content->type == INTERNAL_TEXT && content->text) {
                // Truncate very long content to avoid overwhelming the summarizer
                size_t text_len = strlen(content->text);
                if (text_len > 2000) {
                    // Use first 1000 and last 500 chars
                    strncat(result + current_len, content->text, 1000);
                    strlcat(result, "\n[... content truncated ...]\n", total_size);
                    strlcat(result, content->text + text_len - 500, total_size);
                } else {
                    strlcat(result, content->text, total_size);
                }
                strlcat(result, "\n", total_size);
                current_len = strlen(result);
            } else if (content->type == INTERNAL_TOOL_CALL && content->tool_name) {
                char tool_info[128];
                snprintf(tool_info, sizeof(tool_info), "[Used tool: %s]\n", content->tool_name);
                strlcat(result, tool_info, total_size);
                current_len = strlen(result);
            }
        }

        strlcat(result, "\n", total_size);
        current_len = strlen(result);
    }

    return result;
}

/**
 * Helper to create a temporary ConversationState for summarization
 * This creates a minimal state with just the system and user messages needed for summarization
 */
static ConversationState* create_temp_conversation_state(ConversationState *main_state,
                                                          const char *conversation_text) {
    if (!main_state || !conversation_text) {
        return NULL;
    }

    ConversationState *temp_state = calloc(1, sizeof(ConversationState));
    if (!temp_state) {
        LOG_ERROR("Failed to allocate temporary conversation state");
        return NULL;
    }

    // Copy essential configuration from main state
    temp_state->api_key = main_state->api_key;
    temp_state->api_url = main_state->api_url;
    temp_state->model = main_state->model;
    temp_state->provider = main_state->provider;
    temp_state->max_tokens = main_state->max_tokens;
    temp_state->max_retry_duration_ms = main_state->max_retry_duration_ms;
    temp_state->working_dir = main_state->working_dir;

    // Build system message content (the summarization prompt)
    size_t system_content_len = strlen(SUMMARIZATION_PROMPT) + strlen(conversation_text) + 1;
    char *system_content = malloc(system_content_len);
    if (!system_content) {
        LOG_ERROR("Failed to allocate system message content");
        free(temp_state);
        return NULL;
    }
    strlcpy(system_content, SUMMARIZATION_PROMPT, system_content_len);
    strlcat(system_content, conversation_text, system_content_len);

    // Create system message (index 0)
    temp_state->messages[0].role = MSG_SYSTEM;
    temp_state->messages[0].content_count = 1;
    temp_state->messages[0].contents = calloc(1, sizeof(InternalContent));
    if (!temp_state->messages[0].contents) {
        LOG_ERROR("Failed to allocate system message contents");
        free(system_content);
        free(temp_state);
        return NULL;
    }
    temp_state->messages[0].contents[0].type = INTERNAL_TEXT;
    temp_state->messages[0].contents[0].text = system_content;

    // Create user message (index 1) - just a simple request to summarize
    const char *user_text = "Please provide a summary of the above conversation.";
    temp_state->messages[1].role = MSG_USER;
    temp_state->messages[1].content_count = 1;
    temp_state->messages[1].contents = calloc(1, sizeof(InternalContent));
    if (!temp_state->messages[1].contents) {
        LOG_ERROR("Failed to allocate user message contents");
        free(temp_state->messages[0].contents[0].text);
        free(temp_state->messages[0].contents);
        free(temp_state);
        return NULL;
    }
    temp_state->messages[1].contents[0].type = INTERNAL_TEXT;
    temp_state->messages[1].contents[0].text = strdup(user_text);
    if (!temp_state->messages[1].contents[0].text) {
        LOG_ERROR("Failed to duplicate user message text");
        free(temp_state->messages[1].contents);
        free(temp_state->messages[0].contents[0].text);
        free(temp_state->messages[0].contents);
        free(temp_state);
        return NULL;
    }

    temp_state->count = 2;

    return temp_state;
}

/**
 * Free a temporary ConversationState (only frees what was allocated by create_temp_conversation_state)
 */
#ifdef TEST_BUILD
void free_temp_conversation_state(ConversationState *temp_state) {
#else
static void free_temp_conversation_state(ConversationState *temp_state) {
#endif
    if (!temp_state) {
        return;
    }

    // Free message contents
    for (int i = 0; i < temp_state->count; i++) {
        InternalMessage *msg = &temp_state->messages[i];
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

    free(temp_state);
}

int compaction_generate_summary(ConversationState *state,
                                const InternalMessage *messages,
                                int message_count,
                                char *summary_out,
                                size_t summary_size) {
    if (!summary_out || summary_size == 0) {
        return -1;
    }
    summary_out[0] = '\0';

    if (!state || !messages || message_count <= 0) {
        LOG_WARN("compaction_generate_summary: invalid parameters");
        return -1;
    }

    // Check if provider is available (don't need to check state->provider as call_api_with_retries
    // will handle provider lazy initialization)
    if (!state->api_url || !state->api_key) {
        LOG_WARN("compaction_generate_summary: API URL or key not initialized");
        return -1;
    }

    LOG_INFO("Generating compaction summary for %d messages", message_count);

    // Build conversation text from messages
    char *conversation_text = build_conversation_text(messages, message_count);
    if (!conversation_text) {
        LOG_ERROR("Failed to build conversation text for summarization");
        return -1;
    }

    // Create temporary conversation state for summarization
    ConversationState *temp_state = create_temp_conversation_state(state, conversation_text);
    free(conversation_text);

    if (!temp_state) {
        LOG_ERROR("Failed to create temporary conversation state for summarization");
        return -1;
    }

    // Make API call using the main API client with retry logic
    // This reuses all the retry, authentication, and response handling from normal API calls
    LOG_DEBUG("Making summarization API call via call_api_with_retries");

    ApiResponse *response = call_api_with_retries(temp_state);

    // Free the temporary conversation state (don't use conversation_free as it assumes
    // full initialization and ownership of all fields)
    free_temp_conversation_state(temp_state);

    if (!response) {
        LOG_ERROR("Summarization API call failed");
        return -1;
    }

    // Check for errors in the response
    if (response->error_message) {
        LOG_ERROR("Summarization API error: %s", response->error_message);
        api_response_free(response);
        return -1;
    }

    // Extract summary text from the assistant's message
    char *summary_text = response->message.text;
    if (!summary_text || summary_text[0] == '\0') {
        LOG_ERROR("Failed to extract summary from API response (no text content)");
        api_response_free(response);
        return -1;
    }

    // Copy to output buffer, respecting size limit
    // We need to strdup since api_response_free will free the message.text
    char *summary_copy = strdup(summary_text);
    api_response_free(response);

    if (!summary_copy) {
        LOG_ERROR("Failed to duplicate summary text");
        return -1;
    }

    strlcpy(summary_out, summary_copy, summary_size);
    free(summary_copy);

    LOG_INFO("Successfully generated compaction summary (%zu chars)", strlen(summary_out));

    return 0;
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
}

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

/**
 * Store a message to the SQLite memory database
 */
static int compaction_store_message(MemoryDB *db, const InternalMessage *msg, int msg_index, const char *session_id) {
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
        case MSG_AUTO_COMPACTION: role_str = "auto_compaction"; break;
        default: role_str = "unknown"; break;
    }

    // Store as simple text: "role: content"
    size_t value_len = strlen(role_str) + 2 + strlen(text) + 1;
    char *value = malloc(value_len);
    if (!value) {
        return -1;
    }
    snprintf(value, value_len, "%s: %s", role_str, text);

    // Store in SQLite memory database
    int64_t card_id = memory_db_store(db, entity, slot, value, MEMORY_KIND_EVENT, MEMORY_RELATION_SETS);
    free(value);

    return (card_id >= 0) ? 0 : -1;
}

int compaction_perform(ConversationState *state, CompactionConfig *config, const char *session_id, CompactionResult *result) {
    // Initialize result to indicate no compaction
    if (result) {
        memset(result, 0, sizeof(*result));
    }

    if (!state || !config) {
        return -1;
    }

    // Calculate which messages to compact
    // Find the last compaction notice - we never compact past that point
    // This ensures the first user message and compaction notice are preserved
    int compact_start = 1; // Default: start after message 0
    for (int i = state->count - 1; i >= 0; i--) {
        if (state->messages[i].role == MSG_AUTO_COMPACTION) {
            // Found a compaction notice, start after it
            compact_start = i + 1;
            LOG_DEBUG("Found compaction notice at position %d, starting compaction from position %d", i, compact_start);
            break;
        }
    }

    // Need enough messages to compact: keep_recent + the compaction boundary message(s)
    // After first compaction, we have: [0: first user] [1: compaction notice] [2+: recent]
    // So we need at least keep_recent + 2 messages before compacting again
    int min_messages_to_preserve = (compact_start > 1) ? compact_start + config->keep_recent
                                                       : 1 + config->keep_recent;
    if (state->count <= min_messages_to_preserve) {
        LOG_DEBUG("Not enough messages to compact: count=%d, need > %d", state->count, min_messages_to_preserve);
        return 0;
    }

    // Calculate compact_end to keep keep_recent messages at the end
    // The messages we keep are: [0:compact_start-1] (preserved) + [compact_end+1:count-1] (recent)
    int compact_end = state->count - config->keep_recent - 1; // Last index to compact

    if (compact_end < compact_start) {
        // Nothing to compact
        return 0;
    }

    // Adjust compact_end to ensure we don't split tool call/result pairs
    // If the message at compact_end is an assistant with tool calls, we need to keep
    // it and all subsequent tool results. Move compact_end backward until we find
    // a safe boundary (either a USER message without pending tool calls, or the start).
    while (compact_end >= compact_start) {
        InternalMessage *msg = &state->messages[compact_end];

        // Check if this is an assistant message with tool calls
        int has_tool_calls = 0;
        if (msg->role == MSG_ASSISTANT) {
            for (int j = 0; j < msg->content_count; j++) {
                if (msg->contents[j].type == INTERNAL_TOOL_CALL) {
                    has_tool_calls = 1;
                    break;
                }
            }
        }

        if (has_tool_calls) {
            // Can't compact this message - it has tool calls that need their results
            // Move compact_end backward and check again
            LOG_DEBUG("Cannot compact message %d (assistant with tool calls), adjusting boundary", compact_end);
            compact_end--;
        } else {
            // Safe to compact up to this message
            break;
        }
    }

    // Check if we still have anything to compact after adjustment
    if (compact_end < compact_start) {
        LOG_INFO("Cannot compact - all candidate messages have pending tool calls");
        return 0;
    }

    int compacted_count = compact_end - compact_start + 1;

    LOG_INFO("Compacting messages %d-%d (keeping last %d messages)",
           compact_start, compact_end, config->keep_recent);

    // Get memory database handle
    MemoryDB *memdb = memory_db_get_global();
    if (!memdb) {
        LOG_ERROR("Memory database not available for compaction");
        return -1;
    }

    // Track structured information
    int write_count = 0, edit_count = 0, bash_count = 0, read_count = 0;

    // Store each message to memory database and count tool usage
    for (int i = compact_start; i <= compact_end; i++) {
        if (compaction_store_message(memdb, &state->messages[i], i, session_id) != 0) {
            LOG_WARN("Failed to store message %d to memory database", i);
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
    // Notice message with summary (estimate ~800 tokens for ~400 word summary + metadata)
    tokens_after += 800;
    // API overhead
    tokens_after += 100;

    double usage_percent = (double)tokens_before / config->model_token_limit * 100.0;
    double after_percent = (double)tokens_after / config->model_token_limit * 100.0;

    // Generate AI summary of the compacted messages (before freeing them)
    // Summary buffer: 400 words * ~6 chars/word + formatting = ~3000 chars
    char summary[4096] = "";
    int summary_result = compaction_generate_summary(
        state,
        &state->messages[compact_start],
        compacted_count,
        summary,
        sizeof(summary)
    );

    if (summary_result != 0 || summary[0] == '\0') {
        LOG_WARN("Failed to generate compaction summary, using fallback notice");
        strlcpy(summary, "(Summary generation failed - use MemorySearch to retrieve context)", sizeof(summary));
    }

    // Build compaction notice with summary
    // Notice buffer: summary (~3000) + metadata (~500) + formatting (~500) = ~4000 chars
    char notice[6144];

    snprintf(notice, sizeof(notice),
        "## Context Compaction Notice\n\n"
        "%d earlier messages have been stored in memory. "
        "Use MemorySearch to retrieve relevant past context if needed.\n\n"
        "### Summary of Compacted Context\n\n"
        "%s\n\n"
        "---\n"
        "**Session**: %s\n"
        "**Messages compacted**: %d-%d\n"
        "**Tools used**: Read (%d), Write (%d), Edit (%d), Bash (%d)\n"
        "**Tokens**: %zu → %zu (freed ~%zu tokens)\n"
        "**Context usage**: %.1f%% → %.1f%% of %d token limit\n",
        compacted_count,
        summary,
        session_id ? session_id : "(no session)",
        compact_start, compact_end,
        read_count, write_count, edit_count, bash_count,
        tokens_before, tokens_after, tokens_compacted,
        usage_percent, after_percent, config->model_token_limit);

    // Populate result for caller to handle UI notification
    if (result) {
        result->success = 1;
        result->messages_compacted = compacted_count;
        result->tokens_before = tokens_before;
        result->tokens_after = tokens_after;
        result->usage_before_pct = usage_percent;
        result->usage_after_pct = after_percent;
        strlcpy(result->summary, summary, sizeof(result->summary));
    }

    // Free the messages being compacted
    for (int i = compact_start; i <= compact_end; i++) {
        free_message_contents(&state->messages[i]);
    }

    // Create compaction notice message with MSG_AUTO_COMPACTION type
    InternalMessage notice_msg = {0};
    notice_msg.role = MSG_AUTO_COMPACTION;
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
    // Use memmove to handle overlapping memory regions safely
    if (recent_count > 0 && recent_start != 2) {
        memmove(&state->messages[2], &state->messages[recent_start],
                (size_t)recent_count * sizeof(InternalMessage));
        // Zero out the old positions to prevent double-free issues
        memset(&state->messages[2 + recent_count], 0,
               (size_t)(state->count - (2 + recent_count)) * sizeof(InternalMessage));
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
}
