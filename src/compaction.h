#ifndef COMPACTION_H
#define COMPACTION_H

/**
 * Context Compaction System
 *
 * When enabled and context approaches threshold:
 * 1. Store older messages in memvid (searchable long-term memory)
 * 2. Replace with a compaction notice message
 * 3. Continue - AI can use MemorySearch to retrieve past context
 *
 * Requirements:
 * - Memvid must be available (HAVE_MEMVID)
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
 * 1. Store old messages to memvid
 * 2. Generate structured extraction (files modified, tools used)
 * 3. Inject compaction notice message
 * 4. Remove compacted messages from state
 *
 * @param state Current conversation state (will be modified)
 * @param config Compaction configuration (will update last_compacted_index)
 * @param session_id Current session ID (for memvid storage)
 * @return 0 on success, -1 on error
 */
int compaction_perform(struct ConversationState *state, CompactionConfig *config, const char *session_id);

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

#endif /* COMPACTION_H */
