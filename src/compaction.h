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
    int threshold_percent;    // Trigger at this % of MAX_MESSAGES (default: 60)
    int keep_recent;          // Number of recent messages to keep (default: 20)
    int last_compacted_index; // Last message index that was compacted
} CompactionConfig;

/**
 * Initialize compaction configuration from environment variables
 *
 * @param config Compaction config structure to initialize
 * @param enabled Whether --auto-compact flag was passed
 */
void compaction_init_config(CompactionConfig *config, int enabled);

/**
 * Check if compaction should trigger based on current message count
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

#endif /* COMPACTION_H */
