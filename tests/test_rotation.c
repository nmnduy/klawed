/*
 * Test database rotation functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include "../src/persistence.h"
#include "../src/logger.h"

// Test database path
#define TEST_DB_PATH "/tmp/test_rotation.db"

// Helper function to count records in database
static int count_records(PersistenceDB *db) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT COUNT(*) FROM api_calls;";

    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    return count;
}

// Helper function to insert test records
static void insert_test_records(PersistenceDB *db, int count, int days_old) {
    time_t base_time = time(NULL) - (days_old * 86400);

    for (int i = 0; i < count; i++) {
        // Create a simple test record
        char request[256];
        snprintf(request, sizeof(request), "{\"test\": \"request_%d\"}", i);

        const char *sql =
            "INSERT INTO api_calls "
            "(timestamp, session_id, api_base_url, request_json, response_json, "
            "model, status, http_status, duration_ms, tool_count, created_at) "
            "VALUES (?, 'test-session', 'https://test.api', ?, NULL, 'test-model', "
            "'success', 200, 100, 0, ?);";

        sqlite3_stmt *stmt;
        int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
        assert(rc == SQLITE_OK);

        char timestamp[32];
        struct tm *tm_info = localtime(&base_time);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

        sqlite3_bind_text(stmt, 1, timestamp, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, request, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, base_time + i);

        rc = sqlite3_step(stmt);
        assert(rc == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
}

// Test rotation by age
static void test_rotate_by_age(void) {
    printf("Testing rotation by age...\n");

    // Remove old test database
    unlink(TEST_DB_PATH);

    // Initialize database
    PersistenceDB *db = persistence_init(TEST_DB_PATH);
    assert(db != NULL);

    // Insert 10 old records (40 days old) and 5 new records (10 days old)
    insert_test_records(db, 10, 40);
    insert_test_records(db, 5, 10);

    int initial_count = count_records(db);
    printf("  Initial records: %d\n", initial_count);
    assert(initial_count == 15);

    // Rotate: keep only records from last 30 days
    int deleted = persistence_rotate_by_age(db, 30);
    printf("  Deleted by age: %d\n", deleted);
    assert(deleted == 10);

    int final_count = count_records(db);
    printf("  Remaining records: %d\n", final_count);
    assert(final_count == 5);

    persistence_close(db);
    printf("  ✓ Rotation by age test passed\n\n");
}

// Test rotation by count
static void test_rotate_by_count(void) {
    printf("Testing rotation by count...\n");

    // Remove old test database
    unlink(TEST_DB_PATH);

    // Initialize database
    PersistenceDB *db = persistence_init(TEST_DB_PATH);
    assert(db != NULL);

    // Insert 100 records
    insert_test_records(db, 100, 1);

    int initial_count = count_records(db);
    printf("  Initial records: %d\n", initial_count);
    assert(initial_count == 100);

    // Keep only last 20 records
    int deleted = persistence_rotate_by_count(db, 20);
    printf("  Deleted by count: %d\n", deleted);
    assert(deleted == 80);

    int final_count = count_records(db);
    printf("  Remaining records: %d\n", final_count);
    assert(final_count == 20);

    persistence_close(db);
    printf("  ✓ Rotation by count test passed\n\n");
}

// Test database size
static void test_db_size(void) {
    printf("Testing database size query...\n");

    // Remove old test database
    unlink(TEST_DB_PATH);

    // Initialize database
    PersistenceDB *db = persistence_init(TEST_DB_PATH);
    assert(db != NULL);

    // Insert some records
    insert_test_records(db, 50, 1);

    long size = persistence_get_db_size(db);
    printf("  Database size: %ld bytes\n", size);
    assert(size > 0);

    persistence_close(db);
    printf("  ✓ Database size test passed\n\n");
}

// Test vacuum
static void test_vacuum(void) {
    printf("Testing VACUUM...\n");

    // Remove old test database
    unlink(TEST_DB_PATH);

    // Initialize database
    PersistenceDB *db = persistence_init(TEST_DB_PATH);
    assert(db != NULL);

    // Insert and delete records
    insert_test_records(db, 100, 1);
    persistence_rotate_by_count(db, 10);

    long size_before = persistence_get_db_size(db);
    printf("  Size before vacuum: %ld bytes\n", size_before);

    int rc = persistence_vacuum(db);
    assert(rc == 0);

    long size_after = persistence_get_db_size(db);
    printf("  Size after vacuum: %ld bytes\n", size_after);

    // Size should be same or smaller after vacuum
    assert(size_after <= size_before);

    persistence_close(db);
    printf("  ✓ VACUUM test passed\n\n");
}

// Test auto-rotation (with environment variables)
static void test_auto_rotate(void) {
    printf("Testing auto-rotation...\n");

    // Remove old test database
    unlink(TEST_DB_PATH);

    // Set test environment variables
    setenv("CLAUDE_C_DB_MAX_DAYS", "20", 1);
    setenv("CLAUDE_C_DB_MAX_RECORDS", "30", 1);
    setenv("CLAUDE_C_DB_AUTO_ROTATE", "1", 1);

    // Initialize database (auto-rotate should not run on empty DB)
    PersistenceDB *db = persistence_init(TEST_DB_PATH);
    assert(db != NULL);

    // Insert 50 old records (30 days old) and 20 new records (10 days old)
    insert_test_records(db, 50, 30);
    insert_test_records(db, 20, 10);

    int count_before = count_records(db);
    printf("  Records before auto-rotation: %d\n", count_before);
    assert(count_before == 70);

    // Run auto-rotation
    int rc = persistence_auto_rotate(db);
    assert(rc == 0);

    int count_after = count_records(db);
    printf("  Records after auto-rotation: %d\n", count_after);

    // Should have deleted old records (> 20 days) and kept max 30 records
    // So we should have at most 30 records, and all should be <= 20 days old
    assert(count_after <= 30);
    assert(count_after == 20); // Only the 20 recent records should remain

    persistence_close(db);

    // Cleanup env vars
    unsetenv("CLAUDE_C_DB_MAX_DAYS");
    unsetenv("CLAUDE_C_DB_MAX_RECORDS");
    unsetenv("CLAUDE_C_DB_AUTO_ROTATE");

    printf("  ✓ Auto-rotation test passed\n\n");
}

int main(void) {
    printf("=== Database Rotation Tests ===\n\n");

    // Initialize logger
    log_init();

    // Run tests
    test_rotate_by_age();
    test_rotate_by_count();
    test_db_size();
    test_vacuum();
    test_auto_rotate();

    // Cleanup
    unlink(TEST_DB_PATH);

    printf("=== All rotation tests passed! ===\n");
    return 0;
}
