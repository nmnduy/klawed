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
 * Extract text content from API response JSON
 * Supports both OpenAI and Anthropic response formats
 */
static char* extract_response_text(const char *response_body) {
    if (!response_body) {
        return NULL;
    }

    cJSON *json = cJSON_Parse(response_body);
    if (!json) {
        LOG_WARN("Failed to parse summarization response JSON");
        return NULL;
    }

    char *result = NULL;

    // Try OpenAI format: choices[0].message.content
    cJSON *choices = cJSON_GetObjectItem(json, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(first_choice, "message");
        if (message) {
            cJSON *content = cJSON_GetObjectItem(message, "content");
            if (content && cJSON_IsString(content) && content->valuestring) {
                result = strdup(content->valuestring);
            }
        }
    }

    // Try Anthropic format: content[0].text
    if (!result) {
        cJSON *content_array = cJSON_GetObjectItem(json, "content");
        if (content_array && cJSON_IsArray(content_array) && cJSON_GetArraySize(content_array) > 0) {
            cJSON *first_content = cJSON_GetArrayItem(content_array, 0);
            cJSON *text = cJSON_GetObjectItem(first_content, "text");
            if (text && cJSON_IsString(text) && text->valuestring) {
                result = strdup(text->valuestring);
            }
        }
    }

    cJSON_Delete(json);
    return result;
}

/**
 * Build a minimal API request JSON for summarization
 * Uses OpenAI-compatible format (works with Anthropic via compatibility layer)
 */
static char* build_summarization_request(const char *model, const char *conversation_text, int max_tokens) {
    cJSON *request = cJSON_CreateObject();
    if (!request) {
        return NULL;
    }

    // Add model
    cJSON_AddStringToObject(request, "model", model ? model : "claude-sonnet-4-20250514");

    // Add max_completion_tokens (target ~400 words = ~600 tokens, with buffer)
    // Using max_completion_tokens instead of deprecated max_tokens for compatibility
    // with reasoning models (o1, o3, kimi-k2, etc.)
    cJSON_AddNumberToObject(request, "max_completion_tokens", max_tokens > 0 ? max_tokens : 800);

    // Build messages array
    cJSON *messages = cJSON_CreateArray();
    if (!messages) {
        cJSON_Delete(request);
        return NULL;
    }

    // Single user message with summarization request
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");

    // Build full prompt
    size_t prompt_len = strlen(SUMMARIZATION_PROMPT) + strlen(conversation_text) + 1;
    char *full_prompt = malloc(prompt_len);
    if (!full_prompt) {
        cJSON_Delete(user_msg);
        cJSON_Delete(messages);
        cJSON_Delete(request);
        return NULL;
    }
    strlcpy(full_prompt, SUMMARIZATION_PROMPT, prompt_len);
    strlcat(full_prompt, conversation_text, prompt_len);

    cJSON_AddStringToObject(user_msg, "content", full_prompt);
    free(full_prompt);

    cJSON_AddItemToArray(messages, user_msg);
    cJSON_AddItemToObject(request, "messages", messages);

    char *result = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    return result;
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

    // Check if provider is available
    if (!state->provider || !state->api_url || !state->api_key) {
        LOG_WARN("compaction_generate_summary: provider not initialized");
        return -1;
    }

    LOG_INFO("Generating compaction summary for %d messages", message_count);

    // Build conversation text from messages
    char *conversation_text = build_conversation_text(messages, message_count);
    if (!conversation_text) {
        LOG_ERROR("Failed to build conversation text for summarization");
        return -1;
    }

    // Build API request
    char *request_json = build_summarization_request(state->model, conversation_text, 800);
    free(conversation_text);

    if (!request_json) {
        LOG_ERROR("Failed to build summarization request");
        return -1;
    }

    // Set up HTTP request headers
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // Add authorization header
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", state->api_key);
    headers = curl_slist_append(headers, auth_header);

    // Add Anthropic version header if using Anthropic API
    if (state->api_url && strstr(state->api_url, "anthropic.com")) {
        curl_slist_free_all(headers);
        headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", state->api_key);
        headers = curl_slist_append(headers, auth_header);
    }

    if (!headers) {
        LOG_ERROR("Failed to setup HTTP headers for summarization");
        free(request_json);
        return -1;
    }

    // Execute HTTP request
    HttpRequest req = {0};
    req.url = state->api_url;
    req.method = "POST";
    req.body = request_json;
    req.headers = headers;
    req.connect_timeout_ms = 30000;  // 30 seconds
    req.total_timeout_ms = 120000;   // 2 minutes for summarization

    LOG_DEBUG("Making summarization API call to %s", state->api_url);

    HttpResponse *http_resp = http_client_execute(&req, NULL, NULL);

    // Clean up request resources
    free(request_json);
    curl_slist_free_all(headers);

    if (!http_resp) {
        LOG_ERROR("Summarization HTTP request failed");
        return -1;
    }

    // Check for errors
    if (http_resp->error_message) {
        LOG_ERROR("Summarization API error: %s", http_resp->error_message);
        http_response_free(http_resp);
        return -1;
    }

    if (http_resp->status_code < 200 || http_resp->status_code >= 300) {
        LOG_ERROR("Summarization API returned status %ld", http_resp->status_code);
        if (http_resp->body) {
            LOG_DEBUG("Response body: %s", http_resp->body);
        }
        http_response_free(http_resp);
        return -1;
    }

    // Extract summary text from response
    char *summary_text = extract_response_text(http_resp->body);
    http_response_free(http_resp);

    if (!summary_text) {
        LOG_ERROR("Failed to extract summary from API response");
        return -1;
    }

    // Copy to output buffer, respecting size limit
    strlcpy(summary_out, summary_text, summary_size);
    free(summary_text);

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
}
