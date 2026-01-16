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

#ifdef HAVE_MEMVID
#include "../memvid.h"

/**
 * Build memory context string from memvid searches.
 * Queries for user preferences, active tasks, and project knowledge.
 */
char* build_memory_context(const char *working_dir) {
    MemvidHandle *handle = memvid_get_global();
    if (!handle) {
        LOG_DEBUG("Memory context: No memvid handle available");
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
    char *user_memories = memvid_get_entity_memories(handle, "user");
    if (user_memories) {
        cJSON *memories = cJSON_Parse(user_memories);
        memvid_free_string(user_memories);

        if (memories && cJSON_IsArray(memories) && cJSON_GetArraySize(memories) > 0) {
            CONTEXT_APPEND("### User Preferences\n");
            int count = cJSON_GetArraySize(memories);
            for (int i = 0; i < count && i < 10; i++) {  // Limit to 10 preferences
                cJSON *mem = cJSON_GetArrayItem(memories, i);
                cJSON *slot = cJSON_GetObjectItem(mem, "slot");
                cJSON *value = cJSON_GetObjectItem(mem, "value");
                if (slot && cJSON_IsString(slot) && value && cJSON_IsString(value)) {
                    CONTEXT_APPEND("- %s: %s\n", slot->valuestring, value->valuestring);
                    has_content = 1;
                }
            }
            CONTEXT_APPEND("\n");
        }
        if (memories) cJSON_Delete(memories);
    }

    // 2. Search for active tasks/goals
    char *task_results = memvid_search(handle, "task: goal:", 10);
    if (task_results) {
        cJSON *results = cJSON_Parse(task_results);
        memvid_free_string(task_results);

        if (results && cJSON_IsArray(results) && cJSON_GetArraySize(results) > 0) {
            CONTEXT_APPEND("### Active Tasks\n");
            int count = cJSON_GetArraySize(results);
            for (int i = 0; i < count && i < 5; i++) {  // Limit to 5 tasks
                cJSON *mem = cJSON_GetArrayItem(results, i);
                cJSON *entity = cJSON_GetObjectItem(mem, "entity");
                cJSON *value = cJSON_GetObjectItem(mem, "value");
                // Check if entity starts with "task:" or "goal:"
                if (entity && cJSON_IsString(entity)) {
                    const char *ent = entity->valuestring;
                    if (strncmp(ent, "task:", 5) == 0 || strncmp(ent, "goal:", 5) == 0) {
                        if (value && cJSON_IsString(value)) {
                            CONTEXT_APPEND("- %s\n", value->valuestring);
                            has_content = 1;
                        }
                    }
                }
            }
            CONTEXT_APPEND("\n");
        }
        if (results) cJSON_Delete(results);
    }

    // 3. Search for project-specific knowledge
    if (project_name[0] != '\0') {
        char project_entity[300];
        snprintf(project_entity, sizeof(project_entity), "project.%s", project_name);

        char *project_memories = memvid_get_entity_memories(handle, project_entity);
        if (project_memories) {
            cJSON *memories = cJSON_Parse(project_memories);
            memvid_free_string(project_memories);

            if (memories && cJSON_IsArray(memories) && cJSON_GetArraySize(memories) > 0) {
                CONTEXT_APPEND("### Project Knowledge (%s)\n", project_name);
                int count = cJSON_GetArraySize(memories);
                for (int i = 0; i < count && i < 10; i++) {  // Limit to 10 items
                    cJSON *mem = cJSON_GetArrayItem(memories, i);
                    cJSON *slot = cJSON_GetObjectItem(mem, "slot");
                    cJSON *value = cJSON_GetObjectItem(mem, "value");
                    if (slot && cJSON_IsString(slot) && value && cJSON_IsString(value)) {
                        CONTEXT_APPEND("- %s: %s\n", slot->valuestring, value->valuestring);
                        has_content = 1;
                    }
                }
                CONTEXT_APPEND("\n");
            }
            if (memories) cJSON_Delete(memories);
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

/**
 * Inject memory context into conversation state.
 * Called after memvid is initialized and before the conversation loop starts.
 * The memory context is appended to the system prompt if available.
 */
int inject_memory_context(ConversationState *state) {
    if (!state) {
        return -1;
    }

    // Check if memvid is available
    if (!memvid_is_available() || !memvid_get_global()) {
        LOG_DEBUG("Memory context injection: Memvid not available");
        return 0;  // Not an error, just nothing to inject
    }

    // Build memory context
    char *memory_context = build_memory_context(state->working_dir);
    if (!memory_context) {
        return 0;  // No memories to inject
    }

    // Get the current system message (should be the first message)
    if (state->count == 0 || state->messages[0].role != MSG_SYSTEM) {
        LOG_WARN("Memory context injection: No system message found");
        free(memory_context);
        return 0;
    }

    // Get current system prompt text
    InternalMessage *sys_msg = &state->messages[0];
    if (sys_msg->content_count == 0 || sys_msg->contents[0].type != INTERNAL_TEXT ||
        !sys_msg->contents[0].text) {
        LOG_WARN("Memory context injection: System message has no text content");
        free(memory_context);
        return 0;
    }

    const char *current_prompt = sys_msg->contents[0].text;

    // Calculate new prompt size
    // Format: current_prompt + "\n\n## Background Knowledge (from memory)\n\n" + memory_context
    const char *header = "\n\n## Background Knowledge (from memory)\n\n";
    size_t header_len = strlen(header);
    size_t current_len = strlen(current_prompt);
    size_t context_len = strlen(memory_context);
    size_t new_size = current_len + header_len + context_len + 1;

    char *new_prompt = malloc(new_size);
    if (!new_prompt) {
        LOG_ERROR("Memory context injection: Failed to allocate memory for new prompt");
        free(memory_context);
        return -1;
    }

    // Build the new prompt
    size_t pos = strlcpy(new_prompt, current_prompt, new_size);
    if (pos < new_size) {
        pos += strlcpy(new_prompt + pos, header, new_size - pos);
    }
    if (pos < new_size) {
        strlcpy(new_prompt + pos, memory_context, new_size - pos);
    }

    // Replace the system prompt
    free(sys_msg->contents[0].text);
    sys_msg->contents[0].text = new_prompt;

    LOG_INFO("Memory context injected into system prompt (%zu bytes added)", header_len + context_len);
    free(memory_context);
    return 0;
}

#endif /* HAVE_MEMVID */
