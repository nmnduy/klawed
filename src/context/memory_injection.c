/*
 * memory_injection.c - Memory context injection for system prompt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <libgen.h>
#include <cjson/cJSON.h>

#include "memory_injection.h"
#include "../logger.h"
#include "../memory_db.h"

/**
 * Build memory context string from memory database searches.
 * Queries for user preferences, active tasks, and project knowledge.
 */
char* build_memory_context(const char *working_dir) {
    MemoryDB *handle = memory_db_get_global();
    if (!handle) {
        LOG_DEBUG("Memory context: No memory database handle available");
        return NULL;
    }

    // Extract project name from working directory
    char project_name[256] = {0};
    if (working_dir) {
        // Make a copy since basename may modify the input
        char *dir_copy = strdup(working_dir);
        if (dir_copy) {
            char *name = basename(dir_copy);
            if (name && name[0] != '\0') {
                strlcpy(project_name, name, sizeof(project_name));
            }
            free(dir_copy);
        }
    }

    // Build context buffer - start with reasonable size
    size_t buf_size = 4096;
    char *context = malloc(buf_size);
    if (!context) {
        LOG_ERROR("Memory context: Failed to allocate buffer");
        return NULL;
    }
    context[0] = '\0';
    size_t offset = 0;
    int has_content = 0;

    // Helper macro to safely append to context buffer
    #define CONTEXT_APPEND(...) do { \
        int written = snprintf(context + offset, buf_size - offset, __VA_ARGS__); \
        if (written > 0 && (size_t)written < buf_size - offset) { \
            offset += (size_t)written; \
        } \
    } while(0)

    // 1. Search for user preferences
    MemorySearchResult *user_memories = memory_db_get_entity_memories(handle, "user");
    if (user_memories && user_memories->count > 0) {
        CONTEXT_APPEND("### User Preferences\n");
        for (size_t i = 0; i < user_memories->count && i < 10; i++) {  // Limit to 10 preferences
            MemoryCard *card = &user_memories->cards[i];
            if (card->slot && card->value) {
                CONTEXT_APPEND("- %s: %s\n", card->slot, card->value);
                has_content = 1;
            }
        }
        CONTEXT_APPEND("\n");
        memory_db_free_result(user_memories);
    }

    // 2. Search for active tasks/goals using FTS
    MemorySearchResult *task_results = memory_db_search(handle, "task goal", 10);
    if (task_results && task_results->count > 0) {
        CONTEXT_APPEND("### Active Tasks\n");
        int tasks_added = 0;
        for (size_t i = 0; i < task_results->count && tasks_added < 5; i++) {  // Limit to 5 tasks
            MemoryCard *card = &task_results->cards[i];
            // Check if entity starts with "task:" or "goal:"
            if (card->entity) {
                if (strncmp(card->entity, "task:", 5) == 0 || strncmp(card->entity, "goal:", 5) == 0) {
                    if (card->value) {
                        CONTEXT_APPEND("- %s\n", card->value);
                        has_content = 1;
                        tasks_added++;
                    }
                }
            }
        }
        if (tasks_added > 0) {
            CONTEXT_APPEND("\n");
        }
        memory_db_free_result(task_results);
    }

    // 3. Search for project-specific knowledge
    if (project_name[0] != '\0') {
        char project_entity[300];
        snprintf(project_entity, sizeof(project_entity), "project.%s", project_name);

        MemorySearchResult *project_memories = memory_db_get_entity_memories(handle, project_entity);
        if (project_memories && project_memories->count > 0) {
            CONTEXT_APPEND("### Project Knowledge (%s)\n", project_name);
            for (size_t i = 0; i < project_memories->count && i < 10; i++) {  // Limit to 10 items
                MemoryCard *card = &project_memories->cards[i];
                if (card->slot && card->value) {
                    CONTEXT_APPEND("- %s: %s\n", card->slot, card->value);
                    has_content = 1;
                }
            }
            CONTEXT_APPEND("\n");
            memory_db_free_result(project_memories);
        }
    }

    #undef CONTEXT_APPEND

    if (!has_content) {
        free(context);
        LOG_DEBUG("Memory context: No relevant memories found");
        return NULL;
    }

    LOG_DEBUG("Memory context: Built context with %zu bytes", offset);
    return context;
}

// Markers to identify memory section in system prompt
#define MEMORY_START_MARKER "\n\n## Background Knowledge (from memory)\n\n"
#define MEMORY_END_MARKER "\n<!-- END_MEMORY_CONTEXT -->\n"

/**
 * Remove existing memory context from system prompt if present.
 * Returns the prompt with memory section removed (may be the original if no memory found).
 * Caller must NOT free the original prompt if it's returned unchanged.
 */
static char* remove_memory_context(const char *prompt) {
    if (!prompt) return NULL;

    // Find the memory section markers
    const char *start = strstr(prompt, MEMORY_START_MARKER);
    if (!start) {
        // No memory section, return NULL to indicate unchanged
        return NULL;
    }

    const char *end = strstr(start, MEMORY_END_MARKER);
    if (!end) {
        // Start marker found but no end marker - remove everything after start
        LOG_WARN("Memory context: Found start marker but no end marker, removing to end");
        end = prompt + strlen(prompt);
    } else {
        // Skip past the end marker
        end += strlen(MEMORY_END_MARKER);
    }

    // Calculate size for new prompt (before start + after end)
    size_t before_len = (size_t)(start - prompt);
    size_t after_len = strlen(end);
    size_t new_size = before_len + after_len + 1;

    char *new_prompt = malloc(new_size);
    if (!new_prompt) {
        LOG_ERROR("Memory context removal: Failed to allocate memory");
        return NULL;
    }

    // Copy parts before and after memory section
    size_t pos = 0;
    if (before_len > 0) {
        memcpy(new_prompt, prompt, before_len);
        pos = before_len;
    }
    if (after_len > 0) {
        memcpy(new_prompt + pos, end, after_len);
        pos += after_len;
    }
    new_prompt[pos] = '\0';

    LOG_DEBUG("Memory context removed from prompt (%zu bytes removed)", (start - prompt) + (end - start));
    return new_prompt;
}

/**
 * Extract the summary text from a compaction notice message.
 * Returns a newly allocated string with the summary, or NULL on failure.
 */
static char* extract_compaction_summary(const char *notice_text) {
    if (!notice_text) {
        return NULL;
    }

    const char *marker = "### Summary of Compacted Context";
    const char *start = strstr(notice_text, marker);
    if (!start) {
        return NULL;
    }
    start += strlen(marker);

    // Skip whitespace and newlines after the marker
    while (*start == '\n' || *start == '\r' || *start == ' ') {
        start++;
    }

    const char *end = strstr(start, "\n---\n");
    if (!end) {
        end = strstr(start, "\n---");
    }
    if (!end) {
        return NULL;
    }

    // Trim trailing whitespace before the separator
    while (end > start && (*(end - 1) == '\n' || *(end - 1) == '\r' || *(end - 1) == ' ')) {
        end--;
    }

    size_t len = (size_t)(end - start);
    if (len == 0 || len >= 4096) {
        return NULL;
    }

    char *summary = malloc(len + 1);
    if (!summary) {
        return NULL;
    }
    strlcpy(summary, start, len + 1);
    return summary;
}

/**
 * Build a memory context string from compacted conversation history.
 * Searches the memory database using the compaction summary and current user query.
 * Returns a newly allocated string, or NULL if no results.
 */
static char* build_compaction_memory_context(const char *summary, const char *user_text) {
    if (!summary) {
        return NULL;
    }

    size_t query_size = strlen(summary) + 1;
    if (user_text) {
        query_size += strlen(user_text) + 16;
    }
    char *query = malloc(query_size);
    if (!query) {
        return NULL;
    }

    if (user_text) {
        snprintf(query, query_size, "%s user: %s", summary, user_text);
    } else {
        strlcpy(query, summary, query_size);
    }

    MemoryDB *handle = memory_db_get_global();
    if (!handle) {
        free(query);
        return NULL;
    }

    MemorySearchResult *results = memory_db_search(handle, query, 10);
    free(query);

    if (!results || results->count == 0) {
        if (results) {
            memory_db_free_result(results);
        }
        return NULL;
    }

    size_t buf_size = 4096;
    char *context = malloc(buf_size);
    if (!context) {
        memory_db_free_result(results);
        return NULL;
    }
    context[0] = '\0';
    size_t offset = 0;

    #define CTX_APPEND(...) do { \
        int written = snprintf(context + offset, buf_size - offset, __VA_ARGS__); \
        if (written > 0 && (size_t)written < buf_size - offset) { \
            offset += (size_t)written; \
        } \
    } while(0)

    CTX_APPEND("<memory-context>\n");
    CTX_APPEND("[System note: The following is recalled memory context from compacted "
               "conversation history. This is NOT new user input. "
               "Use it to maintain continuity with the user's prior work.]\n\n");

    for (size_t i = 0; i < results->count && i < 10; i++) {
        MemoryCard *card = &results->cards[i];
        if (card->value) {
            CTX_APPEND("%s\n", card->value);
        }
    }

    CTX_APPEND("</memory-context>\n");

    #undef CTX_APPEND

    memory_db_free_result(results);
    return context;
}

/**
 * Inject compacted memory context as a system message when the user sends
 * the first message after a context compaction event.
 * Returns: 0 on success (or nothing to do), -1 on error.
 */
static int inject_compaction_memory_context(ConversationState *state) {
    if (!state || state->count < 2) {
        return 0;
    }

    // Only act when the last message is a fresh user message (not a tool response)
    const InternalMessage *last_msg = &state->messages[state->count - 1];
    if (last_msg->role != MSG_USER) {
        return 0;
    }

    int is_tool_response = 0;
    for (int j = 0; j < last_msg->content_count; j++) {
        if (last_msg->contents[j].type == INTERNAL_TOOL_RESPONSE) {
            is_tool_response = 1;
            break;
        }
    }
    if (is_tool_response) {
        return 0;
    }

    // Find the most recent compaction notice
    int compaction_idx = -1;
    const char *notice_text = NULL;
    for (int i = state->count - 2; i >= 0; i--) {
        if (state->messages[i].role == MSG_AUTO_COMPACTION) {
            compaction_idx = i;
            for (int j = 0; j < state->messages[i].content_count; j++) {
                if (state->messages[i].contents[j].type == INTERNAL_TEXT &&
                    state->messages[i].contents[j].text) {
                    notice_text = state->messages[i].contents[j].text;
                    break;
                }
            }
            break;
        }
    }

    if (compaction_idx < 0 || !notice_text) {
        return 0;
    }

    // Only inject for the first user turn after compaction.
    // If an assistant has already responded since compaction, skip.
    for (int i = compaction_idx + 1; i < state->count - 1; i++) {
        if (state->messages[i].role == MSG_ASSISTANT) {
            return 0;
        }
    }

    // Check if we already injected for this turn
    if (state->count >= 2) {
        const InternalMessage *prev = &state->messages[state->count - 2];
        if (prev->role == MSG_SYSTEM && prev->content_count > 0 &&
            prev->contents[0].text &&
            strstr(prev->contents[0].text, "<memory-context>") != NULL) {
            return 0;
        }
    }

    // Extract summary and build search query
    char *summary = extract_compaction_summary(notice_text);
    if (!summary) {
        return 0;
    }

    // Get user text
    const char *user_text = NULL;
    for (int j = 0; j < last_msg->content_count; j++) {
        if (last_msg->contents[j].type == INTERNAL_TEXT && last_msg->contents[j].text) {
            user_text = last_msg->contents[j].text;
            break;
        }
    }

    char *context = build_compaction_memory_context(summary, user_text);
    free(summary);

    if (!context) {
        return 0;
    }

    // Insert as a system message before the user message
    if (state->count >= MAX_MESSAGES) {
        free(context);
        return -1;
    }

    int insert_pos = state->count - 1;
    for (int i = state->count; i > insert_pos; i--) {
        state->messages[i] = state->messages[i - 1];
    }

    memset(&state->messages[insert_pos], 0, sizeof(InternalMessage));
    state->messages[insert_pos].role = MSG_SYSTEM;
    state->messages[insert_pos].content_count = 1;
    state->messages[insert_pos].contents = calloc(1, sizeof(InternalContent));
    if (!state->messages[insert_pos].contents) {
        // Undo shift
        for (int i = insert_pos; i < state->count; i++) {
            state->messages[i] = state->messages[i + 1];
        }
        free(context);
        return -1;
    }

    state->messages[insert_pos].contents[0].type = INTERNAL_TEXT;
    state->messages[insert_pos].contents[0].text = context;
    state->count++;

    LOG_INFO("Injected compaction memory context (%zu bytes) at position %d",
             strlen(context), insert_pos);

    return 0;
}

/**
 * Inject memory context into conversation state.
 * Can be called before each API request to refresh memory context.
 * Removes any existing memory section before adding a new one.
 */
int inject_memory_context(ConversationState *state) {
    if (!state) {
        return -1;
    }

    // Inject compacted memory context if this is the first user turn after compaction
    if (inject_compaction_memory_context(state) != 0) {
        LOG_WARN("Memory context injection: Compaction memory injection failed");
    }

    // Check if memory database is available
    if (!memory_db_get_global()) {
        LOG_DEBUG("Memory context injection: Memory database not available");
        return 0;  // Not an error, just nothing to inject
    }

    // Get the current system message (should be the first message)
    if (state->count == 0 || state->messages[0].role != MSG_SYSTEM) {
        LOG_WARN("Memory context injection: No system message found");
        return 0;
    }

    // Get current system prompt text
    InternalMessage *sys_msg = &state->messages[0];
    if (sys_msg->content_count == 0 || sys_msg->contents[0].type != INTERNAL_TEXT ||
        !sys_msg->contents[0].text) {
        LOG_WARN("Memory context injection: System message has no text content");
        return 0;
    }

    const char *current_prompt = sys_msg->contents[0].text;

    // Remove any existing memory context first
    char *base_prompt = remove_memory_context(current_prompt);
    int base_prompt_allocated = (base_prompt != NULL);
    if (!base_prompt) {
        // No existing memory context, duplicate current prompt as base
        base_prompt = strdup(current_prompt);
        if (!base_prompt) {
            LOG_ERROR("Memory context injection: Failed to duplicate prompt");
            return -1;
        }
        base_prompt_allocated = 1;
    }

    // Build fresh memory context
    char *memory_context = build_memory_context(state->working_dir);
    if (!memory_context) {
        // No memories to inject
        if (base_prompt_allocated) {
            // We removed old memory but have no new memory to add
            // Update the prompt to the cleaned version
            free(sys_msg->contents[0].text);
            sys_msg->contents[0].text = base_prompt;
            LOG_DEBUG("Memory context removed, no new memories to inject");
        }
        return 0;
    }

    // Calculate new prompt size
    // Format: base_prompt + MEMORY_START_MARKER + memory_context + MEMORY_END_MARKER
    size_t base_len = strlen(base_prompt);
    size_t start_len = strlen(MEMORY_START_MARKER);
    size_t context_len = strlen(memory_context);
    size_t end_len = strlen(MEMORY_END_MARKER);
    size_t new_size = base_len + start_len + context_len + end_len + 1;

    char *new_prompt = malloc(new_size);
    if (!new_prompt) {
        LOG_ERROR("Memory context injection: Failed to allocate memory for new prompt");
        free(memory_context);
        if (base_prompt_allocated) {
            free(base_prompt);
        }
        return -1;
    }

    // Build the new prompt
    size_t pos = strlcpy(new_prompt, base_prompt, new_size);
    if (pos < new_size) {
        pos += strlcpy(new_prompt + pos, MEMORY_START_MARKER, new_size - pos);
    }
    if (pos < new_size) {
        pos += strlcpy(new_prompt + pos, memory_context, new_size - pos);
    }
    if (pos < new_size) {
        strlcpy(new_prompt + pos, MEMORY_END_MARKER, new_size - pos);
    }

    // Free the old prompt and base_prompt if it was allocated
    free(sys_msg->contents[0].text);
    if (base_prompt_allocated) {
        free(base_prompt);
    }

    sys_msg->contents[0].text = new_prompt;

    LOG_INFO("Memory context injected into system prompt (%zu bytes added)", start_len + context_len + end_len);
    free(memory_context);
    return 0;
}
