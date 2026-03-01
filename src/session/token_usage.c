/*
 * Token Usage Tracking
 * Track and report token usage per session
 */

#include "token_usage.h"
#include "../persistence.h"
#include "../logger.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

void session_print_token_usage(ConversationState *state) {
    if (!state || !state->persistence_db) {
        return;
    }

    int64_t prompt_tokens = 0;
    int64_t completion_tokens = 0;
    int64_t cached_tokens = 0;

    // Get cumulative token totals for this session
    int result = persistence_get_session_token_totals(
        state->persistence_db,
        state->session_id,
        &prompt_tokens,
        &completion_tokens,
        &cached_tokens
    );

    if (result == 0) {
        // Calculate total tokens (excluding cached tokens since they're free)
        int64_t total_tokens = prompt_tokens + completion_tokens;

        // Print token usage summary
        fprintf(stderr, "\n=== Token Usage Summary ===\n");
        fprintf(stderr, "Session: %s\n", state->session_id ? state->session_id : "unknown");
        fprintf(stderr, "Prompt tokens: %ld\n", (long)prompt_tokens);
        fprintf(stderr, "Completion tokens: %ld\n", (long)completion_tokens);
        if (cached_tokens > 0) {
            fprintf(stderr, "Cached tokens (free): %ld\n", (long)cached_tokens);
            fprintf(stderr, "Total billed tokens: %ld (excluding %ld cached)\n",
                    (long)total_tokens, (long)cached_tokens);
        } else {
            fprintf(stderr, "Total tokens: %ld\n", (long)total_tokens);
        }
        fprintf(stderr, "===========================\n");
    } else {
        fprintf(stderr, "\nNote: Token usage statistics unavailable\n");
    }
}