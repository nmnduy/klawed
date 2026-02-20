/*
 * conversation_state.c - ConversationState lifecycle management implementation
 */

#include "conversation_state.h"
#include "content_types.h"
#include "../logger.h"
#include "../todo.h"
#include "../subagent_manager.h"
#include "../compaction.h"
#include "../api/api_timing.h"
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <bsd/stdlib.h>
#include <pthread.h>
#include <sys/stat.h>
#include <limits.h>

int conversation_state_init(ConversationState *state) {
    if (!state) {
        return -1;
    }

    if (state->conv_mutex_initialized) {
        return 0;
    }

    if (pthread_mutex_init(&state->conv_mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize conversation mutex");
        return -1;
    }

    state->conv_mutex_initialized = 1;
    state->interrupt_requested = 0;  // Initialize interrupt flag

    // Initialize compaction config (NULL by default)
    state->compaction_config = NULL;

    // Initialize API timing tracker for dynamic spinner speed
    api_timing_init(&state->api_timing);

    // Initialize subagent manager
    state->subagent_manager = malloc(sizeof(SubagentManager));
    if (state->subagent_manager) {
        if (subagent_manager_init(state->subagent_manager) != 0) {
            LOG_ERROR("Failed to initialize subagent manager");
            free(state->subagent_manager);
            state->subagent_manager = NULL;
            // Continue anyway - not a critical failure
        } else {
            LOG_DEBUG("Subagent manager initialized successfully");
            // Register for emergency cleanup on unexpected termination
            register_subagent_manager_for_cleanup(state->subagent_manager);
        }
    } else {
        LOG_ERROR("Failed to allocate memory for subagent manager");
        // Continue anyway - not a critical failure
    }

    return 0;
}

void conversation_state_destroy(ConversationState *state) {
    if (!state || !state->conv_mutex_initialized) {
        return;
    }

    // Clean up compaction config
    if (state->compaction_config) {
        free(state->compaction_config);
        state->compaction_config = NULL;
    }

    // Clean up subagent manager
    if (state->subagent_manager) {
        // Unregister from emergency cleanup
        register_subagent_manager_for_cleanup(NULL);

        subagent_manager_free(state->subagent_manager);
        free(state->subagent_manager);
        state->subagent_manager = NULL;
    }

    // Try to lock the mutex before destroying it to ensure it's not locked by another thread.
    // If we can't lock it (e.g., it's already locked or in a bad state), we still proceed
    // with destruction but log a warning. This is a best-effort approach to avoid undefined
    // behavior when destroying a locked mutex.
    int lock_result = pthread_mutex_trylock(&state->conv_mutex);
    if (lock_result != 0) {
        LOG_WARN("Mutex may be locked by another thread during destroy (result=%d)", lock_result);
    } else {
        // Successfully locked, now unlock before destroying
        pthread_mutex_unlock(&state->conv_mutex);
    }

    pthread_mutex_destroy(&state->conv_mutex);
    state->conv_mutex_initialized = 0;
}

int conversation_state_lock(ConversationState *state) {
    if (!state) {
        return -1;
    }

    if (!state->conv_mutex_initialized) {
        if (conversation_state_init(state) != 0) {
            return -1;
        }
    }

    if (pthread_mutex_lock(&state->conv_mutex) != 0) {
        LOG_ERROR("Failed to lock conversation mutex");
        return -1;
    }
    return 0;
}

void conversation_state_unlock(ConversationState *state) {
    if (!state || !state->conv_mutex_initialized) {
        return;
    }
    pthread_mutex_unlock(&state->conv_mutex);
}

void clear_conversation(ConversationState *state) {
    if (conversation_state_lock(state) != 0) {
        return;
    }

    // Keep the system/compaction message at position 0 (if present)
    int preserve_count = 0;

    if (state->count > 0 && (state->messages[0].role == MSG_SYSTEM ||
                              state->messages[0].role == MSG_AUTO_COMPACTION)) {
        // System message or compaction notice remains intact at position 0
        preserve_count = 1;
    }

    // Free all other message content
    for (int i = preserve_count; i < state->count; i++) {
        for (int j = 0; j < state->messages[i].content_count; j++) {
            InternalContent *cb = &state->messages[i].contents[j];
            free(cb->text);
            cb->text = NULL;
            free(cb->tool_id);
            cb->tool_id = NULL;
            free(cb->tool_name);
            cb->tool_name = NULL;
            if (cb->tool_params) {
                cJSON_Delete(cb->tool_params);
                cb->tool_params = NULL;
            }
            if (cb->tool_output) {
                cJSON_Delete(cb->tool_output);
                cb->tool_output = NULL;
            }
        }
        free(state->messages[i].contents);
        state->messages[i].contents = NULL;
        state->messages[i].content_count = 0;
    }

    // Reset message count (keeping system/compaction message)
    state->count = preserve_count;

    // Clear todo list
    if (state->todo_list) {
        todo_free(state->todo_list);
        // Reset the todo list to a known safe state before reinitializing
        state->todo_list->items = NULL;
        state->todo_list->count = 0;
        state->todo_list->capacity = 0;
        if (todo_init(state->todo_list) != 0) {
            LOG_ERROR("Failed to reinitialize todo list after clear - todo operations may fail");
            // The list is in a safe empty state (items is NULL, count/capacity are 0)
            // todo_add handles this gracefully by reallocating when needed
        } else {
            LOG_DEBUG("Todo list cleared and reinitialized");
        }
    }

    conversation_state_unlock(state);
}

void conversation_free(ConversationState *state) {
    if (conversation_state_lock(state) != 0) {
        return;
    }

    // Free all messages
    for (int i = 0; i < state->count; i++) {
        for (int j = 0; j < state->messages[i].content_count; j++) {
            InternalContent *cb = &state->messages[i].contents[j];
            free(cb->text);
            cb->text = NULL;
            free(cb->tool_id);
            cb->tool_id = NULL;
            free(cb->tool_name);
            cb->tool_name = NULL;
            if (cb->tool_params) {
                cJSON_Delete(cb->tool_params);
                cb->tool_params = NULL;
            }
            if (cb->tool_output) {
                cJSON_Delete(cb->tool_output);
                cb->tool_output = NULL;
            }
        }
        free(state->messages[i].contents);
        state->messages[i].contents = NULL;
        state->messages[i].content_count = 0;
    }
    state->count = 0;

    // Note: todo_list is freed separately in main cleanup
    // Do not call todo_free() here to avoid double-free

    conversation_state_unlock(state);
}

int add_directory(ConversationState *state, const char *path) {
    if (!state || !path) {
        return -1;
    }

    if (conversation_state_lock(state) != 0) {
        return -1;
    }

    // Validate that directory exists
    int result = -1;
    struct stat st;
    char *resolved_path = NULL;

    // Resolve path (handle relative paths)
    if (path[0] == '/') {
        resolved_path = realpath(path, NULL);
    } else {
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", state->working_dir, path);
        resolved_path = realpath(full_path, NULL);
    }

    if (!resolved_path) {
        goto out;  // Path doesn't exist or can't be resolved
    }

    if (stat(resolved_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        goto out;  // Not a directory
    }

    // Check if directory is already in the list (avoid duplicates)
    if (strcmp(resolved_path, state->working_dir) == 0) {
        goto out;  // Already the main working directory
    }

    for (int i = 0; i < state->additional_dirs_count; i++) {
        if (strcmp(resolved_path, state->additional_dirs[i]) == 0) {
            goto out;  // Already in additional directories
        }
    }

    // Expand array if needed
    if (state->additional_dirs_count >= state->additional_dirs_capacity) {
        int new_capacity = state->additional_dirs_capacity == 0 ? 4 : state->additional_dirs_capacity * 2;
        char **new_array = reallocarray(state->additional_dirs, (size_t)new_capacity, sizeof(char*));
        if (!new_array) {
            goto out;  // Out of memory
        }
        state->additional_dirs = new_array;
        state->additional_dirs_capacity = new_capacity;
    }

    // Add directory to list
    state->additional_dirs[state->additional_dirs_count++] = resolved_path;
    resolved_path = NULL;
    result = 0;

out:
    conversation_state_unlock(state);
    free(resolved_path);
    return result;
}
