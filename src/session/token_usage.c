/*
 * Token Usage Tracking
 * Track and report token usage per session
 */

#include "token_usage.h"
#include "../persistence.h"
#include "../logger.h"
#include <stdio.h>
#include <string.h>

void session_print_token_usage(ConversationState *state) {
    if (!state || !state->persistence_db) {
        return;
    }

    int prompt_tokens = 0;
    int completion_tokens = 0;
    int cached_tokens = 0;

    // Get token usage for this session
    int result = persistence_get_session_token_usage(
        state->persistence_db,
        state->session_id,
        &prompt_tokens,
        &completion_tokens,
        &cached_tokens
    );

    if (result == 0) {
        // Calculate total tokens (excluding cached tokens since they're free)
        int total_tokens = prompt_tokens + completion_tokens;

        // Print token usage summary
        fprintf(stderr, "\n=== Token Usage Summary ===\n");
        fprintf(stderr, "Session: %s\n", state->session_id ? state->session_id : "unknown");
        fprintf(stderr, "Prompt tokens: %d\n", prompt_tokens);
        fprintf(stderr, "Completion tokens: %d\n", completion_tokens);
        if (cached_tokens > 0) {
            fprintf(stderr, "Cached tokens (free): %d\n", cached_tokens);
            fprintf(stderr, "Total billed tokens: %d (excluding %d cached)\n",
                    total_tokens, cached_tokens);
        } else {
            fprintf(stderr, "Total tokens: %d\n", total_tokens);
        }
        fprintf(stderr, "===========================\n");
    } else {
        fprintf(stderr, "\nNote: Token usage statistics unavailable\n");
    }
}
