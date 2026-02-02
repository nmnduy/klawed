#ifndef COMPACTION_H
#define COMPACTION_H

/**
 * Context Compaction System
 *
 * When enabled and context approaches threshold:
 * 1. Store older messages in SQLite memory database (searchable long-term memory)
 * 2. Replace with a compaction notice message
 * 3. Continue - AI can use MemorySearch to retrieve past context
 *
 * Requirements:
 * - SQLite memory database (always available)
 * - Auto-compact flag enabled (--auto-compact or KLAWED_AUTO_COMPACT=1)
 */

/**
 * Configuration for auto-compaction behavior
 */
typedef struct CompactionConfig {
    int enabled;              // Whether auto-compaction is enabled
    int threshold_percent;    // Trigger at this % of model token limit (default: 60)
    int keep_recent;          // Number of recent messages to keep (default: 20)
    int last_compacted_index; // Last message index that was compacted
    int model_token_limit;    // Model's context limit in tokens (default: 125000)
    size_t current_tokens;    // Current estimated token count in conversation
} CompactionConfig;

/**
 * Result of a compaction operation (for UI notification)
 */
typedef struct CompactionResult {
    int success;              // 1 if compaction succeeded, 0 if failed/skipped
    int messages_compacted;   // Number of messages stored to memory
    size_t tokens_before;     // Token count before compaction
    size_t tokens_after;      // Token count after compaction
    double usage_before_pct;  // Context usage % before
    double usage_after_pct;   // Context usage % after
} CompactionResult;

/**
 * Initialize compaction configuration from environment variables
 *
 * @param config Compaction config structure to initialize
 * @param enabled Whether --auto-compact flag was passed
 * @param model_name Model name to determine context limit (NULL uses default)
 */
void compaction_init_config(CompactionConfig *config, int enabled, const char *model_name);

/**
 * Check if compaction should trigger based on current token count
 *
 * @param state Current conversation state
 * @param config Compaction configuration
 * @return 1 if compaction should trigger, 0 otherwise
 */
int compaction_should_trigger(const struct ConversationState *state, const CompactionConfig *config);

/**
 * Perform compaction:
 * 1. Store old messages to SQLite memory database
 * 2. Generate structured extraction (files modified, tools used)
 * 3. Inject compaction notice message
 * 4. Remove compacted messages from state
 *
 * @param state Current conversation state (will be modified)
 * @param config Compaction configuration (will update last_compacted_index)
 * @param session_id Current session ID (for memory storage)
 * @param result Optional: receives compaction statistics for UI notification (can be NULL)
 * @return 0 on success, -1 on error
 */
int compaction_perform(struct ConversationState *state, CompactionConfig *config, const char *session_id, CompactionResult *result);

/**
 * Update token count in compaction config based on current conversation state
 *
 * @param state Current conversation state
 * @param config Compaction configuration (will update current_tokens)
 * @return Estimated token count
 */
size_t compaction_update_token_count(const struct ConversationState *state, CompactionConfig *config);

/**
 * Estimate tokens for a single message
 *
 * @param msg Message to estimate
 * @return Estimated token count
 */
size_t compaction_estimate_message_tokens(const InternalMessage *msg);

/**
 * Generate a summary of the compacted messages using an API call.
 * The summary includes:
 * - What was being worked on (summary of recent activity)
 * - Current goals/objectives
 * - Task state/progress
 *
 * @param state Conversation state (for provider access)
 * @param messages Array of messages to summarize
 * @param message_count Number of messages
 * @param summary_out Output buffer for the summary (caller provides)
 * @param summary_size Size of output buffer
 * @return 0 on success, -1 on error (summary_out will be empty string on error)
 */
int compaction_generate_summary(struct ConversationState *state,
                                const InternalMessage *messages,
                                int message_count,
                                char *summary_out,
                                size_t summary_size);

#endif /* COMPACTION_H */
