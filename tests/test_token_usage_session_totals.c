/*
 * test_token_usage_session_totals.c - Verify session token aggregation
 *
 * Updated to use the separate token_usage database via persistence layer
 */

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../src/persistence.h"
#include "../src/token_usage_db.h"

static const char *TEST_DB_PATH = "/tmp/test_token_usage_session_totals.db";
static const char *TEST_TOKEN_DB_PATH = "/tmp/test_token_usage_session_totals_tokens.db";

int main(void) {
    unlink(TEST_DB_PATH);
    unlink(TEST_TOKEN_DB_PATH);

    // Initialize the token usage database directly for testing
    TokenUsageDB *token_db = token_usage_db_init(TEST_TOKEN_DB_PATH);
    assert(token_db != NULL);

    // Insert cumulative token usage records for a session
    // First call: 100 prompt, 25 completion, 10 cached
    int rc = token_usage_db_log(token_db, 1, "session-1", 100, 25, 125, 10, 0, 0);
    assert(rc == 0);

    // Second call: adds more tokens (cumulative totals: 140, 40, 15)
    rc = token_usage_db_log(token_db, 2, "session-1", 140, 40, 180, 15, 0, 0);
    assert(rc == 0);

    int64_t prompt_tokens = -1;
    int64_t completion_tokens = -1;
    int64_t cached_tokens = -1;

    // Per-session totals - should get latest (cumulative) record
    rc = token_usage_db_get_session_usage(token_db, "session-1",
                                          &prompt_tokens, &completion_tokens, &cached_tokens);
    assert(rc == 0);
    printf("Session-1: prompt=%ld, completion=%ld, cached=%ld\n",
           (long)prompt_tokens, (long)completion_tokens, (long)cached_tokens);
    assert(prompt_tokens == 140);      // Latest record (cumulative)
    assert(completion_tokens == 40);   // Latest record (cumulative)
    assert(cached_tokens == 15);       // Latest record (cumulative)

    // All sessions (should match since only one session exists)
    prompt_tokens = completion_tokens = cached_tokens = -1;
    rc = token_usage_db_get_session_usage(token_db, NULL,
                                          &prompt_tokens, &completion_tokens, &cached_tokens);
    assert(rc == 0);
    printf("All sessions: prompt=%ld, completion=%ld, cached=%ld\n",
           (long)prompt_tokens, (long)completion_tokens, (long)cached_tokens);
    assert(prompt_tokens == 140);
    assert(completion_tokens == 40);
    assert(cached_tokens == 15);

    // Test get_last_prompt_tokens
    int64_t last_prompt = -1;
    rc = token_usage_db_get_last_prompt_tokens(token_db, "session-1", &last_prompt);
    assert(rc == 0);
    assert(last_prompt == 140);

    token_usage_db_close(token_db);
    unlink(TEST_DB_PATH);
    unlink(TEST_TOKEN_DB_PATH);
    printf("✓ test_token_usage_session_totals passed\n");
    return 0;
}