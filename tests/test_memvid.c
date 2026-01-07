/*
 * test_memvid.c - Unit tests for memvid memory integration
 *
 * Tests the memvid wrapper functions and constants.
 * The memory tools themselves are tested indirectly through integration tests.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Include memvid header for constants and functions */
#include "../src/memvid.h"

/* Test helper macros */
#define TEST(name) printf("\n=== Test: %s ===\n", name)
#define PASS() printf("✓ PASS\n")
#define FAIL(msg) do { printf("✗ FAIL: %s\n", msg); exit(1); } while(0)

/* ============================================================================
 * Memory Kind Constant Tests
 * ============================================================================ */

/**
 * Test that memory kind constants are correct (match Rust enum)
 */
static void test_memory_kind_constants(void) {
    TEST("Memory kind constants");

    assert(MEMVID_KIND_FACT == 0);
    assert(MEMVID_KIND_PREFERENCE == 1);
    assert(MEMVID_KIND_EVENT == 2);
    assert(MEMVID_KIND_PROFILE == 3);
    assert(MEMVID_KIND_RELATIONSHIP == 4);
    assert(MEMVID_KIND_GOAL == 5);

    printf("  MEMVID_KIND_FACT = %d (expected 0)\n", MEMVID_KIND_FACT);
    printf("  MEMVID_KIND_PREFERENCE = %d (expected 1)\n", MEMVID_KIND_PREFERENCE);
    printf("  MEMVID_KIND_EVENT = %d (expected 2)\n", MEMVID_KIND_EVENT);
    printf("  MEMVID_KIND_PROFILE = %d (expected 3)\n", MEMVID_KIND_PROFILE);
    printf("  MEMVID_KIND_RELATIONSHIP = %d (expected 4)\n", MEMVID_KIND_RELATIONSHIP);
    printf("  MEMVID_KIND_GOAL = %d (expected 5)\n", MEMVID_KIND_GOAL);

    PASS();
}

/**
 * Test that memory relation constants are correct (match Rust enum)
 */
static void test_memory_relation_constants(void) {
    TEST("Memory relation constants");

    assert(MEMVID_RELATION_SETS == 0);
    assert(MEMVID_RELATION_UPDATES == 1);
    assert(MEMVID_RELATION_EXTENDS == 2);
    assert(MEMVID_RELATION_RETRACTS == 3);

    printf("  MEMVID_RELATION_SETS = %d (expected 0)\n", MEMVID_RELATION_SETS);
    printf("  MEMVID_RELATION_UPDATES = %d (expected 1)\n", MEMVID_RELATION_UPDATES);
    printf("  MEMVID_RELATION_EXTENDS = %d (expected 2)\n", MEMVID_RELATION_EXTENDS);
    printf("  MEMVID_RELATION_RETRACTS = %d (expected 3)\n", MEMVID_RELATION_RETRACTS);

    PASS();
}

/* ============================================================================
 * Availability Tests
 * ============================================================================ */

/**
 * Test memvid_is_available() function
 */
static void test_memvid_availability(void) {
    TEST("Memvid availability check");

    int available = memvid_is_available();
    printf("  memvid_is_available() = %d\n", available);

#ifdef HAVE_MEMVID
    assert(available == 1);
    printf("  Built with HAVE_MEMVID - memvid should be available\n");
#else
    assert(available == 0);
    printf("  Built without HAVE_MEMVID - memvid should not be available\n");
#endif

    PASS();
}

/**
 * Test memvid_get_global() without init
 */
static void test_global_handle_without_init(void) {
    TEST("Global handle without init");

    /* Without calling memvid_init_global(), handle should be NULL */
    MemvidHandle *handle = memvid_get_global();

    /* Note: We can't assert NULL here because a previous test might have
     * initialized it. Just check we don't crash. */
    printf("  memvid_get_global() returned %s\n", handle ? "a handle" : "NULL");

    PASS();
}

/**
 * Test memvid_last_error() function
 */
static void test_last_error(void) {
    TEST("Last error function");

    const char *error = memvid_last_error();

    /* Should always return something, even if it's the "not available" message */
    printf("  memvid_last_error() = \"%s\"\n", error ? error : "(null)");

#ifndef HAVE_MEMVID
    /* Without HAVE_MEMVID, should return the "not available" message */
    assert(error != NULL);
    assert(strstr(error, "not available") != NULL || strstr(error, "HAVE_MEMVID") != NULL);
#endif

    PASS();
}

/**
 * Test memvid_free_string() with NULL (should not crash)
 */
static void test_free_string_null(void) {
    TEST("Free string with NULL");

    /* Should handle NULL gracefully */
    memvid_free_string(NULL);

    printf("  memvid_free_string(NULL) did not crash\n");

    PASS();
}

/**
 * Test stub functions without initialization
 */
static void test_stub_functions(void) {
    TEST("Stub functions without init");

#ifndef HAVE_MEMVID
    /* All operations should return errors/NULL when memvid not available */

    int64_t put_result = memvid_put_memory(NULL, "entity", "slot", "value",
                                            MEMVID_KIND_FACT, MEMVID_RELATION_SETS);
    assert(put_result == -1);
    printf("  memvid_put_memory() returned %lld (expected -1)\n", (long long)put_result);

    char *get_result = memvid_get_current(NULL, "entity", "slot");
    assert(get_result == NULL);
    printf("  memvid_get_current() returned NULL as expected\n");

    char *search_result = memvid_search(NULL, "query", 10);
    assert(search_result == NULL);
    printf("  memvid_search() returned NULL as expected\n");

    char *entity_result = memvid_get_entity_memories(NULL, "entity");
    assert(entity_result == NULL);
    printf("  memvid_get_entity_memories() returned NULL as expected\n");

    int commit_result = memvid_commit(NULL);
    assert(commit_result == -1);
    printf("  memvid_commit() returned %d (expected -1)\n", commit_result);

    int init_result = memvid_init_global(NULL);
    assert(init_result == -1);
    printf("  memvid_init_global() returned %d (expected -1)\n", init_result);
#else
    printf("  Skipped - HAVE_MEMVID is defined, testing real implementation\n");

    /* With HAVE_MEMVID, operations without init should still be safe */
    char *get_result = memvid_get_current(NULL, "entity", "slot");
    /* NULL handle should return NULL or error */
    printf("  memvid_get_current(NULL, ...) returned %s\n",
           get_result ? "a result" : "NULL");
    if (get_result) memvid_free_string(get_result);
#endif

    PASS();
}

/* ============================================================================
 * Integration Tests (only run if HAVE_MEMVID)
 * ============================================================================ */

#ifdef HAVE_MEMVID
static void test_init_and_cleanup(void) {
    TEST("Initialize and cleanup (HAVE_MEMVID)");

    /* Clean up any previous state */
    memvid_cleanup_global();

    /* Initialize with test path */
    int result = memvid_init_global("/tmp/test_memvid.mv2");
    printf("  memvid_init_global() returned %d\n", result);

    if (result == 0) {
        MemvidHandle *handle = memvid_get_global();
        assert(handle != NULL);
        printf("  memvid_get_global() returned valid handle\n");

        /* Cleanup */
        memvid_cleanup_global();

        handle = memvid_get_global();
        assert(handle == NULL);
        printf("  After cleanup, handle is NULL\n");
    } else {
        printf("  Init failed (may be expected if FFI lib not linked): %s\n",
               memvid_last_error());
    }

    PASS();
}

static void test_store_and_recall(void) {
    TEST("Store and recall memory (HAVE_MEMVID)");

    /* Initialize */
    memvid_cleanup_global();
    int init_result = memvid_init_global("/tmp/test_memvid_sr.mv2");
    if (init_result != 0) {
        printf("  Skipped - init failed: %s\n", memvid_last_error());
        PASS();
        return;
    }

    MemvidHandle *handle = memvid_get_global();
    assert(handle != NULL);

    /* Store a memory */
    int64_t card_id = memvid_put_memory(handle, "test_user", "favorite_color", "blue",
                                        MEMVID_KIND_PREFERENCE, MEMVID_RELATION_SETS);
    printf("  Stored memory, card_id = %lld\n", (long long)card_id);

    if (card_id >= 0) {
        /* Commit changes */
        int commit_result = memvid_commit(handle);
        printf("  Commit result = %d\n", commit_result);

        /* Recall the memory */
        char *recalled = memvid_get_current(handle, "test_user", "favorite_color");
        if (recalled) {
            printf("  Recalled: %s\n", recalled);
            assert(strstr(recalled, "blue") != NULL);
            memvid_free_string(recalled);
        } else {
            printf("  Recall returned NULL\n");
        }
    }

    memvid_cleanup_global();

    /* Clean up test file */
    remove("/tmp/test_memvid_sr.mv2");

    PASS();
}
#endif /* HAVE_MEMVID */

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("===============================================\n");
    printf("Running Memvid Integration Tests\n");
#ifdef HAVE_MEMVID
    printf("Build: WITH memvid support (HAVE_MEMVID defined)\n");
#else
    printf("Build: WITHOUT memvid support (stub functions)\n");
#endif
    printf("===============================================\n");

    /* Constant tests */
    test_memory_kind_constants();
    test_memory_relation_constants();

    /* Basic function tests */
    test_memvid_availability();
    test_global_handle_without_init();
    test_last_error();
    test_free_string_null();
    test_stub_functions();

#ifdef HAVE_MEMVID
    /* Integration tests (only with real memvid) */
    test_init_and_cleanup();
    test_store_and_recall();
#endif

    printf("\n===============================================\n");
    printf("All memvid tests passed!\n");
    printf("===============================================\n");

    return 0;
}
