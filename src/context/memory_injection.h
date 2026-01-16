/*
 * memory_injection.h - Memory context injection for system prompt
 *
 * Functions to inject memory context from memvid into the system prompt.
 * Only compiled when HAVE_MEMVID is defined.
 */

#ifndef CONTEXT_MEMORY_INJECTION_H
#define CONTEXT_MEMORY_INJECTION_H

#include "../klawed_internal.h"

#ifdef HAVE_MEMVID

/**
 * Build memory context string from memvid searches.
 * Queries for user preferences, active tasks, and project knowledge.
 * Caller must free the returned string.
 *
 * Returns: Newly allocated string with formatted context, or NULL if no memories found
 */
char* build_memory_context(const char *working_dir);

/**
 * Inject memory context into conversation state.
 * Called after memvid is initialized and before the conversation loop starts.
 * The memory context is appended to the system prompt if available.
 *
 * Returns: 0 on success (or if no memories to inject), -1 on error
 */
int inject_memory_context(ConversationState *state);

#endif /* HAVE_MEMVID */

#endif /* CONTEXT_MEMORY_INJECTION_H */
