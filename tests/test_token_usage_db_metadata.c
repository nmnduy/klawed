/*
 * test_token_usage_db_metadata.c - Test token usage metadata tracking
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../src/token_usage_db.h"

#define TEST_DB_PATH "./test_token_usage_metadata.db"

static void cleanup(void) {
    unlink(TEST_DB_PATH);
    char wal_path[256];
    char shm_path[256];
    snprintf(wal_path, sizeof(wal_path), "%s-wal", TEST_DB_PATH);
    snprintf(shm_path, sizeof(shm_path), "%s-shm", TEST_DB_PATH);
    unlink(wal_path);
    unlink(shm_path);
}

static void test_no_metadata_env(void) {
    printf("test_no_metadata_env...\n");

    unsetenv("KLAWED_TOKEN_USAGE_METADATA");
    TokenUsageDB *db = token_usage_db_init(TEST_DB_PATH);
    assert(db != NULL);
    assert(db->metadata_id == 0);

    int rc = token_usage_db_log(db, 1, "session-no-meta",
                                10, 20, 30, 5, 2, 3);
    assert(rc == 0);

    token_usage_db_close(db);
    cleanup();

    printf("  PASSED\n");
}

static void test_metadata_attached(void) {
    printf("test_metadata_attached...\n");

    cleanup();
    const char *meta = "{\"user_id\":\"user-123\",\"org_id\":\"org-456\"}";
    setenv("KLAWED_TOKEN_USAGE_METADATA", meta, 1);

    TokenUsageDB *db = token_usage_db_init(TEST_DB_PATH);
    assert(db != NULL);
    assert(db->metadata_id > 0);

    int rc = token_usage_db_log(db, 2, "session-with-meta",
                                100, 200, 300, 50, 25, 25);
    assert(rc == 0);

    // Verify metadata can be retrieved
    char *out_json = NULL;
    rc = token_usage_db_get_metadata_by_id(db, db->metadata_id, &out_json);
    assert(rc == 0);
    assert(out_json != NULL);
    assert(strcmp(out_json, meta) == 0);
    free(out_json);

    token_usage_db_close(db);
    cleanup();
    unsetenv("KLAWED_TOKEN_USAGE_METADATA");

    printf("  PASSED\n");
}

static void test_metadata_deduplication(void) {
    printf("test_metadata_deduplication...\n");

    cleanup();
    const char *meta = "{\"user_id\":\"user-abc\"}";
    setenv("KLAWED_TOKEN_USAGE_METADATA", meta, 1);

    TokenUsageDB *db1 = token_usage_db_init(TEST_DB_PATH);
    assert(db1 != NULL);
    int64_t id1 = db1->metadata_id;
    assert(id1 > 0);

    token_usage_db_log(db1, 1, "s1", 1, 1, 1, 0, 0, 0);
    token_usage_db_close(db1);

    // Re-open same DB with same metadata - should get same ID
    TokenUsageDB *db2 = token_usage_db_init(TEST_DB_PATH);
    assert(db2 != NULL);
    int64_t id2 = db2->metadata_id;
    assert(id2 == id1);

    token_usage_db_close(db2);
    cleanup();
    unsetenv("KLAWED_TOKEN_USAGE_METADATA");

    printf("  PASSED\n");
}

static void test_multiple_different_metadata(void) {
    printf("test_multiple_different_metadata...\n");

    cleanup();
    const char *meta1 = "{\"user_id\":\"user-1\"}";
    setenv("KLAWED_TOKEN_USAGE_METADATA", meta1, 1);

    TokenUsageDB *db = token_usage_db_init(TEST_DB_PATH);
    assert(db != NULL);
    int64_t id1 = db->metadata_id;

    token_usage_db_log(db, 1, "s1", 10, 10, 10, 0, 0, 0);
    token_usage_db_close(db);

    // Change env var and re-open
    const char *meta2 = "{\"user_id\":\"user-2\"}";
    setenv("KLAWED_TOKEN_USAGE_METADATA", meta2, 1);

    db = token_usage_db_init(TEST_DB_PATH);
    assert(db != NULL);
    int64_t id2 = db->metadata_id;
    assert(id2 != id1);

    token_usage_db_log(db, 2, "s2", 20, 20, 20, 0, 0, 0);

    // Verify both metadata rows exist
    char *json1 = NULL;
    char *json2 = NULL;
    assert(token_usage_db_get_metadata_by_id(db, id1, &json1) == 0);
    assert(token_usage_db_get_metadata_by_id(db, id2, &json2) == 0);
    assert(strcmp(json1, meta1) == 0);
    assert(strcmp(json2, meta2) == 0);
    free(json1);
    free(json2);

    token_usage_db_close(db);
    cleanup();
    unsetenv("KLAWED_TOKEN_USAGE_METADATA");

    printf("  PASSED\n");
}

int main(void) {
    printf("=== Token Usage Metadata Tests ===\n\n");

    test_no_metadata_env();
    test_metadata_attached();
    test_metadata_deduplication();
    test_multiple_different_metadata();

    printf("\n=== All tests passed! ===\n");
    return 0;
}
