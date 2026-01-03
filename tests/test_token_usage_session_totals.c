/*
 * test_token_usage_session_totals.c - Verify session token aggregation
 */

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/persistence.h"

static const char *TEST_DB_PATH = "/tmp/test_token_usage_session_totals.db";

static void insert_api_call(sqlite3 *db, const char *session_id, long created_at, int *out_id) {
    const char *sql =
        "INSERT INTO api_calls (timestamp, session_id, api_base_url, request_json, model, status, created_at) "
        "VALUES (?, ?, 'https://api.test', '{""request"":true}', 'test-model', 'ok', ?);";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    assert(rc == SQLITE_OK);

    rc = sqlite3_bind_text(stmt, 1, "2025-01-01T00:00:00Z", -1, SQLITE_TRANSIENT);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_TRANSIENT);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 3, created_at);
    assert(rc == SQLITE_OK);

    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_DONE);
    sqlite3_finalize(stmt);

    *out_id = (int)sqlite3_last_insert_rowid(db);
}

static void insert_token_usage(sqlite3 *db, int api_call_id, const char *session_id,
                               int prompt, int completion, int cached, long created_at) {
    const char *sql =
        "INSERT INTO token_usage (api_call_id, session_id, prompt_tokens, completion_tokens, total_tokens, cached_tokens, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    assert(rc == SQLITE_OK);

    rc = sqlite3_bind_int(stmt, 1, api_call_id);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_TRANSIENT);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int(stmt, 3, prompt);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int(stmt, 4, completion);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int(stmt, 5, prompt + completion);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int(stmt, 6, cached);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 7, created_at);
    assert(rc == SQLITE_OK);

    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_DONE);
    sqlite3_finalize(stmt);
}

int main(void) {
    unlink(TEST_DB_PATH);

    PersistenceDB *pdb = persistence_init(TEST_DB_PATH);
    assert(pdb != NULL);
    sqlite3 *db = pdb->db;
    assert(db != NULL);

    // Insert two API calls for the same session with different token counts
    int api_call_id1 = 0;
    int api_call_id2 = 0;
    insert_api_call(db, "session-1", 1, &api_call_id1);
    insert_api_call(db, "session-1", 2, &api_call_id2);

    insert_token_usage(db, api_call_id1, "session-1", 100, 25, 10, 1);
    insert_token_usage(db, api_call_id2, "session-1", 40, 15, 5, 2);

    int prompt_tokens = -1;
    int completion_tokens = -1;
    int cached_tokens = -1;

    // Per-session totals
    int rc = persistence_get_session_token_usage(pdb, "session-1",
                                                 &prompt_tokens, &completion_tokens, &cached_tokens);
    assert(rc == 0);
    assert(prompt_tokens == 140);      // 100 + 40
    assert(completion_tokens == 40);   // 25 + 15
    assert(cached_tokens == 15);       // 10 + 5

    // All sessions (should match since only one session exists)
    prompt_tokens = completion_tokens = cached_tokens = -1;
    rc = persistence_get_session_token_usage(pdb, NULL,
                                             &prompt_tokens, &completion_tokens, &cached_tokens);
    assert(rc == 0);
    assert(prompt_tokens == 140);
    assert(completion_tokens == 40);
    assert(cached_tokens == 15);

    persistence_close(pdb);
    unlink(TEST_DB_PATH);
    printf("✓ test_token_usage_session_totals passed\n");
    return 0;
}
