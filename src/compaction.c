#include "klawed_internal.h"
#include "compaction.h"
#include "tool_utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef HAVE_MEMVID
#include "memvid.h"
#endif

// Default configuration values
#define DEFAULT_COMPACT_THRESHOLD 60    // 60% of MAX_MESSAGES
#define DEFAULT_KEEP_RECENT 20          // Keep last 20 messages

void compaction_init_config(CompactionConfig *config, int enabled) {
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

    config->last_compacted_index = -1; // No compaction yet

#ifndef HAVE_MEMVID
    if (enabled) {
        fprintf(stderr, "Warning: Auto-compact enabled but memvid not available. Compaction disabled.\n");
        config->enabled = 0;
    }
#endif
}

int compaction_should_trigger(const ConversationState *state, const CompactionConfig *config) {
    if (!config || !config->enabled || !state) {
        return 0;
    }

#ifndef HAVE_MEMVID
    return 0; // Can't compact without memvid
#else
    // Calculate threshold count
    int threshold_count = (MAX_MESSAGES * config->threshold_percent) / 100;

    // Trigger if we're at or above threshold
    return (state->count >= threshold_count);
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
    }

    // Store as simple text: "role: content"
    size_t value_len = strlen(role_str) + 2 + strlen(text) + 1;
    char *value = malloc(value_len);
    if (!value) {
        return -1;
    }
    snprintf(value, value_len, "%s: %s", role_str, text);

    // Store in memvid
    int result = memvid_store(entity, slot, value, "event");
    free(value);

    return result;
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

    // Build compaction notice
    char notice[2048];

    snprintf(notice, sizeof(notice),
        "## Context Compaction Notice\n"
        "%d earlier messages have been stored in memory. "
        "Use MemorySearch to retrieve relevant past context if needed.\n\n"
        "Session: %s\n"
        "Messages compacted: %d-%d\n"
        "Tools used: Read (%d), Write (%d), Edit (%d), Bash (%d)\n",
        compacted_count,
        session_id ? session_id : "(no session)",
        compact_start, compact_end,
        read_count, write_count, edit_count, bash_count);

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

    // Update last compacted index
    config->last_compacted_index = compact_end;

    LOG_INFO("Compaction complete. New message count: %d", state->count);

    return 0;
#endif
}
