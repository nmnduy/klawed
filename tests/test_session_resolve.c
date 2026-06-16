/*
 * test_session_resolve.c - Unit tests for session_resolve_partial_id()
 *
 * Tests the partial session ID prefix resolution, including:
 *   - Exact match (full ID provided as "prefix")
 *   - True partial match (short prefix uniquely identifying one session)
 *   - No match (prefix that doesn't match any session)
 *   - Ambiguous match (prefix matching multiple sessions)
 *   - Empty prefix (invalid input)
 *   - NULL db/preix (invalid input)
 *   - Empty database (no sessions at all)
 */

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/persistence.h"
#include "../src/session.h"

/* ============================================================================
 * Test Helpers
 * =========================================================================== */

#define TEST(name) printf("\n=== Test: %s ===\n", name)
#define PASS() printf("  PASS\n")

/* Create an in-memory PersistenceDB with the api_calls table */
static PersistenceDB *create_test_db(void) {
    PersistenceDB *db = calloc(1, sizeof(PersistenceDB));
    assert(db != NULL);

    int rc = sqlite3_open(":memory:", &db->db);
    assert(rc == SQLITE_OK);

    const char *create_table =
        "CREATE TABLE IF NOT EXISTS api_calls ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id TEXT,"
        "  timestamp TEXT NOT NULL,"
        "  api_base_url TEXT NOT NULL,"
        "  request_json TEXT NOT NULL,"
        "  headers_json TEXT,"
        "  response_json TEXT,"
        "  model TEXT NOT NULL,"
        "  status TEXT NOT NULL,"
        "  http_status INTEGER,"
        "  error_message TEXT,"
        "  duration_ms INTEGER,"
        "  tool_count INTEGER DEFAULT 0,"
        "  created_at INTEGER NOT NULL"
        ")";

    char *err = NULL;
    rc = sqlite3_exec(db->db, create_table, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to create table: %s\n", err);
        sqlite3_free(err);
        assert(0);
    }

    return db;
}

/* Insert a test session row */
static void insert_session(PersistenceDB *db, const char *session_id) {
    const char *insert =
        "INSERT INTO api_calls (session_id, timestamp, api_base_url, "
        "request_json, response_json, model, status, created_at) "
        "VALUES (?, '2024-01-01T00:00:00', 'https://api.example.com', "
        "'{}', '{}', 'test-model', 'success', 1)";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, insert, -1, &stmt, NULL);
    assert(rc == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_DONE);
    sqlite3_finalize(stmt);
}

/* Free test database */
static void close_test_db(PersistenceDB *db) {
    if (db) {
        sqlite3_close(db->db);
        free(db);
    }
}

/* Check that errmsg contains the expected substring */
static void assert_errmsg_contains(const char *errmsg, const char *expected) {
    if (strstr(errmsg, expected) == NULL) {
        fprintf(stderr, "  FAIL: errmsg '%s' does not contain '%s'\n",
                errmsg, expected);
        assert(0);
    }
}

/* ============================================================================
 * Test Cases
 * =========================================================================== */

/* Test: exact match — full session ID resolves to itself */
static void test_exact_match(void) {
    TEST("Exact match");
    PersistenceDB *db = create_test_db();
    insert_session(db, "abc123def456");
    insert_session(db, "xyz789ghi012");

    char errmsg[256] = {0};
    char *result = session_resolve_partial_id(db, "abc123def456", errmsg);
    assert(result != NULL);
    assert(strcmp(result, "abc123def456") == 0);
    free(result);

    close_test_db(db);
    PASS();
}

/* Test: true partial match — short prefix that uniquely identifies one session */
static void test_partial_match(void) {
    TEST("Partial match (unique prefix)");
    PersistenceDB *db = create_test_db();
    insert_session(db, "abc123def456");
    insert_session(db, "xyz789ghi012");

    char errmsg[256] = {0};
    char *result = session_resolve_partial_id(db, "abc", errmsg);
    assert(result != NULL);
    assert(strcmp(result, "abc123def456") == 0);
    free(result);

    result = session_resolve_partial_id(db, "xyz", errmsg);
    assert(result != NULL);
    assert(strcmp(result, "xyz789ghi012") == 0);
    free(result);

    close_test_db(db);
    PASS();
}

/* Test: single character prefix that uniquely matches */
static void test_single_char_prefix(void) {
    TEST("Single character unique prefix");
    PersistenceDB *db = create_test_db();
    insert_session(db, "a-session-1");
    insert_session(db, "b-session-2");

    char errmsg[256] = {0};
    char *result = session_resolve_partial_id(db, "a", errmsg);
    assert(result != NULL);
    assert(strcmp(result, "a-session-1") == 0);
    free(result);

    result = session_resolve_partial_id(db, "b", errmsg);
    assert(result != NULL);
    assert(strcmp(result, "b-session-2") == 0);
    free(result);

    close_test_db(db);
    PASS();
}

/* Test: no match — prefix doesn't match any session */
static void test_no_match(void) {
    TEST("No match");
    PersistenceDB *db = create_test_db();
    insert_session(db, "abc123def456");

    char errmsg[256] = {0};
    char *result = session_resolve_partial_id(db, "zzz", errmsg);
    assert(result == NULL);
    assert_errmsg_contains(errmsg, "No session found matching prefix");

    close_test_db(db);
    PASS();
}

/* Test: ambiguous match — prefix matches multiple sessions */
static void test_ambiguous_match(void) {
    TEST("Ambiguous match");
    PersistenceDB *db = create_test_db();
    insert_session(db, "abc-session-1");
    insert_session(db, "abc-session-2");
    insert_session(db, "abc-session-3");
    insert_session(db, "xyz-other");

    char errmsg[256] = {0};
    char *result = session_resolve_partial_id(db, "abc", errmsg);
    assert(result == NULL);
    assert_errmsg_contains(errmsg, "Ambiguous prefix");
    assert_errmsg_contains(errmsg, "abc-session-1");
    assert_errmsg_contains(errmsg, "abc-session-2");
    assert_errmsg_contains(errmsg, "abc-session-3");

    close_test_db(db);
    PASS();
}

/* Test: empty database */
static void test_empty_database(void) {
    TEST("Empty database");
    PersistenceDB *db = create_test_db();

    char errmsg[256] = {0};
    char *result = session_resolve_partial_id(db, "anything", errmsg);
    assert(result == NULL);
    assert_errmsg_contains(errmsg, "No session found");

    close_test_db(db);
    PASS();
}

/* Test: NULL db parameter */
static void test_null_db(void) {
    TEST("NULL database");
    char errmsg[256] = {0};
    char *result = session_resolve_partial_id(NULL, "abc", errmsg);
    assert(result == NULL);
    assert_errmsg_contains(errmsg, "Invalid session ID prefix");
    PASS();
}

/* Test: NULL prefix parameter */
static void test_null_prefix(void) {
    TEST("NULL prefix");
    PersistenceDB *db = create_test_db();

    char errmsg[256] = {0};
    char *result = session_resolve_partial_id(db, NULL, errmsg);
    assert(result == NULL);
    assert_errmsg_contains(errmsg, "Invalid session ID prefix");

    close_test_db(db);
    PASS();
}

/* Test: empty string prefix */
static void test_empty_prefix(void) {
    TEST("Empty prefix");
    PersistenceDB *db = create_test_db();
    insert_session(db, "abc123");

    char errmsg[256] = {0};
    char *result = session_resolve_partial_id(db, "", errmsg);
    assert(result == NULL);
    assert_errmsg_contains(errmsg, "Invalid session ID prefix");

    close_test_db(db);
    PASS();
}

/* Test: NULL errmsg parameter (should not crash) */
static void test_null_errmsg(void) {
    TEST("NULL errmsg (no crash)");
    PersistenceDB *db = create_test_db();
    insert_session(db, "abc123");

    /* Should work fine without errmsg */
    char *result = session_resolve_partial_id(db, "abc", NULL);
    assert(result != NULL);
    assert(strcmp(result, "abc123") == 0);
    free(result);

    /* Should not crash with no match + NULL errmsg */
    result = session_resolve_partial_id(db, "zzz", NULL);
    assert(result == NULL);

    close_test_db(db);
    PASS();
}

/* Test: prefix with special SQL characters (LIKE wildcards) */
static void test_special_chars(void) {
    TEST("Special characters in prefix");
    PersistenceDB *db = create_test_db();

    /* Session IDs with special characters */
    insert_session(db, "abc%def_test");
    insert_session(db, "abc_def_other");

    /* % is a LIKE wildcard — but since we bind as a parameter, it should
     * be treated literally (SQLite parameter binding escapes it) */
    char errmsg[256] = {0};
    char *result = session_resolve_partial_id(db, "abc%def", errmsg);
    assert(result != NULL);
    assert(strcmp(result, "abc%def_test") == 0);
    free(result);

    close_test_db(db);
    PASS();
}

/* ============================================================================
 * Main
 * =========================================================================== */

int main(void) {
    printf("\n========================================\n");
    printf("  Session Resolve Partial ID Test Suite\n");
    printf("========================================\n");

    test_exact_match();
    test_partial_match();
    test_single_char_prefix();
    test_no_match();
    test_ambiguous_match();
    test_empty_database();
    test_null_db();
    test_null_prefix();
    test_empty_prefix();
    test_null_errmsg();
    test_special_chars();

    printf("\n========================================\n");
    printf("  All tests passed!\n");
    printf("========================================\n\n");
    return 0;
}
