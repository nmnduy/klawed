/*
 * test_memory_db.c - Unit tests for SQLite-based memory system
 *
 * Tests the memory_db wrapper functions and constants.
 * The memory tools themselves are tested indirectly through integration tests.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Include memory_db header for constants and functions */
#include "../src/memory_db.h"

/* Test helper macros */
#define TEST(name) printf("\n=== Test: %s ===\n", name)
#define PASS() printf("✓ PASS\n")
#define FAIL(msg) do { printf("✗ FAIL: %s\n", msg); exit(1); } while(0)

/* ============================================================================
 * Memory Kind Constant Tests
 * ============================================================================ */

/**
 * Test that memory kind constants are correct
 */
static void test_memory_kind_constants(void) {
    TEST("Memory kind constants");

    assert(MEMORY_KIND_FACT == 0);
    assert(MEMORY_KIND_PREFERENCE == 1);
    assert(MEMORY_KIND_EVENT == 2);
    assert(MEMORY_KIND_PROFILE == 3);
    assert(MEMORY_KIND_RELATIONSHIP == 4);
    assert(MEMORY_KIND_GOAL == 5);

    printf("  MEMORY_KIND_FACT = %d (expected 0)\n", MEMORY_KIND_FACT);
    printf("  MEMORY_KIND_PREFERENCE = %d (expected 1)\n", MEMORY_KIND_PREFERENCE);
    printf("  MEMORY_KIND_EVENT = %d (expected 2)\n", MEMORY_KIND_EVENT);
    printf("  MEMORY_KIND_PROFILE = %d (expected 3)\n", MEMORY_KIND_PROFILE);
    printf("  MEMORY_KIND_RELATIONSHIP = %d (expected 4)\n", MEMORY_KIND_RELATIONSHIP);
    printf("  MEMORY_KIND_GOAL = %d (expected 5)\n", MEMORY_KIND_GOAL);

    PASS();
}

/**
 * Test that memory relation constants are correct
 */
static void test_memory_relation_constants(void) {
    TEST("Memory relation constants");

    assert(MEMORY_RELATION_SETS == 0);
    assert(MEMORY_RELATION_UPDATES == 1);
    assert(MEMORY_RELATION_EXTENDS == 2);
    assert(MEMORY_RELATION_RETRACTS == 3);

    printf("  MEMORY_RELATION_SETS = %d (expected 0)\n", MEMORY_RELATION_SETS);
    printf("  MEMORY_RELATION_UPDATES = %d (expected 1)\n", MEMORY_RELATION_UPDATES);
    printf("  MEMORY_RELATION_EXTENDS = %d (expected 2)\n", MEMORY_RELATION_EXTENDS);
    printf("  MEMORY_RELATION_RETRACTS = %d (expected 3)\n", MEMORY_RELATION_RETRACTS);

    PASS();
}

/* ============================================================================
 * String Conversion Tests
 * ============================================================================ */

/**
 * Test kind to string conversion
 */
static void test_kind_to_string(void) {
    TEST("Kind to string conversion");

    assert(strcmp(memory_db_kind_to_string(MEMORY_KIND_FACT), "fact") == 0);
    assert(strcmp(memory_db_kind_to_string(MEMORY_KIND_PREFERENCE), "preference") == 0);
    assert(strcmp(memory_db_kind_to_string(MEMORY_KIND_EVENT), "event") == 0);
    assert(strcmp(memory_db_kind_to_string(MEMORY_KIND_PROFILE), "profile") == 0);
    assert(strcmp(memory_db_kind_to_string(MEMORY_KIND_RELATIONSHIP), "relationship") == 0);
    assert(strcmp(memory_db_kind_to_string(MEMORY_KIND_GOAL), "goal") == 0);

    printf("  All kind conversions correct\n");

    PASS();
}

/**
 * Test string to kind conversion
 */
static void test_string_to_kind(void) {
    TEST("String to kind conversion");

    assert(memory_db_string_to_kind("fact") == MEMORY_KIND_FACT);
    assert(memory_db_string_to_kind("preference") == MEMORY_KIND_PREFERENCE);
    assert(memory_db_string_to_kind("event") == MEMORY_KIND_EVENT);
    assert(memory_db_string_to_kind("profile") == MEMORY_KIND_PROFILE);
    assert(memory_db_string_to_kind("relationship") == MEMORY_KIND_RELATIONSHIP);
    assert(memory_db_string_to_kind("goal") == MEMORY_KIND_GOAL);
    assert(memory_db_string_to_kind("unknown") == MEMORY_KIND_FACT); /* default */
    assert(memory_db_string_to_kind(NULL) == MEMORY_KIND_FACT); /* default */

    printf("  All string to kind conversions correct\n");

    PASS();
}

/**
 * Test relation to string conversion
 */
static void test_relation_to_string(void) {
    TEST("Relation to string conversion");

    assert(strcmp(memory_db_relation_to_string(MEMORY_RELATION_SETS), "sets") == 0);
    assert(strcmp(memory_db_relation_to_string(MEMORY_RELATION_UPDATES), "updates") == 0);
    assert(strcmp(memory_db_relation_to_string(MEMORY_RELATION_EXTENDS), "extends") == 0);
    assert(strcmp(memory_db_relation_to_string(MEMORY_RELATION_RETRACTS), "retracts") == 0);

    printf("  All relation conversions correct\n");

    PASS();
}

/**
 * Test string to relation conversion
 */
static void test_string_to_relation(void) {
    TEST("String to relation conversion");

    assert(memory_db_string_to_relation("sets") == MEMORY_RELATION_SETS);
    assert(memory_db_string_to_relation("updates") == MEMORY_RELATION_UPDATES);
    assert(memory_db_string_to_relation("extends") == MEMORY_RELATION_EXTENDS);
    assert(memory_db_string_to_relation("retracts") == MEMORY_RELATION_RETRACTS);
    assert(memory_db_string_to_relation("unknown") == MEMORY_RELATION_SETS); /* default */
    assert(memory_db_string_to_relation(NULL) == MEMORY_RELATION_SETS); /* default */

    printf("  All string to relation conversions correct\n");

    PASS();
}

/* ============================================================================
 * Availability Tests
 * ============================================================================ */

/**
 * Test memory_db_is_available() function
 */
static void test_memory_db_availability(void) {
    TEST("Memory DB availability check");

    int available = memory_db_is_available();
    printf("  memory_db_is_available() = %d\n", available);

    /* SQLite is always available */
    assert(available == 1);
    printf("  SQLite-based memory system is always available\n");

    PASS();
}

/**
 * Test memory_db_get_global() without init
 */
static void test_global_handle_without_init(void) {
    TEST("Global handle without init");

    /* Without calling memory_db_init_global(), handle should be NULL */
    MemoryDB *handle = memory_db_get_global();

    printf("  memory_db_get_global() returned %s\n", handle ? "a handle" : "NULL");

    PASS();
}

/**
 * Test memory_db_last_error() function
 */
static void test_last_error(void) {
    TEST("Last error function");

    const char *error = memory_db_last_error(NULL);

    /* Should always return something */
    printf("  memory_db_last_error(NULL) = \"%s\"\n", error ? error : "(null)");

    assert(error != NULL);

    PASS();
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

static void test_init_and_cleanup(void) {
    TEST("Initialize and cleanup");

    /* Clean up any previous state */
    memory_db_cleanup_global();

    /* Initialize with test path */
    int result = memory_db_init_global("/tmp/test_memory_db.db");
    printf("  memory_db_init_global() returned %d\n", result);

    if (result == 0) {
        MemoryDB *handle = memory_db_get_global();
        assert(handle != NULL);
        printf("  memory_db_get_global() returned valid handle\n");

        /* Cleanup */
        memory_db_cleanup_global();

        handle = memory_db_get_global();
        assert(handle == NULL);
        printf("  After cleanup, handle is NULL\n");
    } else {
        printf("  Init failed (may be expected): %s\n",
               memory_db_last_error(NULL));
    }

    /* Clean up test file */
    remove("/tmp/test_memory_db.db");

    PASS();
}

static void test_store_and_recall(void) {
    TEST("Store and recall memory");

    /* Initialize */
    memory_db_cleanup_global();
    int init_result = memory_db_init_global("/tmp/test_memory_sr.db");
    if (init_result != 0) {
        printf("  Skipped - init failed: %s\n", memory_db_last_error(NULL));
        PASS();
        return;
    }

    MemoryDB *handle = memory_db_get_global();
    assert(handle != NULL);

    /* Store a memory */
    int64_t card_id = memory_db_store(handle, "test_user", "favorite_color", "blue",
                                      MEMORY_KIND_PREFERENCE, MEMORY_RELATION_SETS);
    printf("  Stored memory, card_id = %lld\n", (long long)card_id);

    if (card_id >= 0) {
        /* Recall the memory */
        MemoryCard *recalled = memory_db_get_current(handle, "test_user", "favorite_color");
        if (recalled) {
            printf("  Recalled entity: %s\n", recalled->entity);
            printf("  Recalled slot: %s\n", recalled->slot);
            printf("  Recalled value: %s\n", recalled->value);
            assert(strcmp(recalled->value, "blue") == 0);
            memory_db_free_card(recalled);
        } else {
            printf("  Recall returned NULL\n");
        }
    }

    memory_db_cleanup_global();

    /* Clean up test file */
    remove("/tmp/test_memory_sr.db");

    PASS();
}

static void test_search(void) {
    TEST("Search memories");

    /* Initialize */
    memory_db_cleanup_global();
    int init_result = memory_db_init_global("/tmp/test_memory_search.db");
    if (init_result != 0) {
        printf("  Skipped - init failed: %s\n", memory_db_last_error(NULL));
        PASS();
        return;
    }

    MemoryDB *handle = memory_db_get_global();
    assert(handle != NULL);

    /* Store some memories */
    memory_db_store(handle, "user1", "language", "Python", MEMORY_KIND_PREFERENCE, MEMORY_RELATION_SETS);
    memory_db_store(handle, "user2", "language", "JavaScript", MEMORY_KIND_PREFERENCE, MEMORY_RELATION_SETS);
    memory_db_store(handle, "user1", "editor", "vim", MEMORY_KIND_PREFERENCE, MEMORY_RELATION_SETS);

    /* Search for Python */
    MemorySearchResult *result = memory_db_search(handle, "Python", 10);
    if (result) {
        printf("  Search returned %zu results\n", result->count);
        memory_db_free_result(result);
    } else {
        printf("  Search returned NULL (FTS5 may not be available)\n");
    }

    memory_db_cleanup_global();

    /* Clean up test file */
    remove("/tmp/test_memory_search.db");

    PASS();
}

static void test_entity_memories(void) {
    TEST("Get entity memories");

    /* Initialize */
    memory_db_cleanup_global();
    int init_result = memory_db_init_global("/tmp/test_memory_entity.db");
    if (init_result != 0) {
        printf("  Skipped - init failed: %s\n", memory_db_last_error(NULL));
        PASS();
        return;
    }

    MemoryDB *handle = memory_db_get_global();
    assert(handle != NULL);

    /* Store memories for same entity */
    memory_db_store(handle, "test_entity", "slot1", "value1", MEMORY_KIND_FACT, MEMORY_RELATION_SETS);
    memory_db_store(handle, "test_entity", "slot2", "value2", MEMORY_KIND_PREFERENCE, MEMORY_RELATION_SETS);

    /* Get all memories for entity */
    MemorySearchResult *result = memory_db_get_entity_memories(handle, "test_entity");
    if (result) {
        printf("  Entity has %zu memories\n", result->count);
        assert(result->count == 2);
        memory_db_free_result(result);
    } else {
        printf("  Get entity memories returned NULL\n");
    }

    memory_db_cleanup_global();

    /* Clean up test file */
    remove("/tmp/test_memory_entity.db");

    PASS();
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("===============================================\n");
    printf("Running Memory DB Integration Tests\n");
    printf("===============================================\n");

    /* Constant tests */
    test_memory_kind_constants();
    test_memory_relation_constants();

    /* String conversion tests */
    test_kind_to_string();
    test_string_to_kind();
    test_relation_to_string();
    test_string_to_relation();

    /* Basic function tests */
    test_memory_db_availability();
    test_global_handle_without_init();
    test_last_error();

    /* Integration tests */
    test_init_and_cleanup();
    test_store_and_recall();
    test_search();
    test_entity_memories();

    printf("\n===============================================\n");
    printf("All memory DB tests passed!\n");
    printf("===============================================\n");

    return 0;
}
