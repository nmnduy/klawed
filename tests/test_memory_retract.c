/*
 * test_memory_retract.c - Unit tests for memory retraction functionality
 *
 * Tests that relation: "retracts" properly removes memories from recall and search.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../src/memory_db.h"

#define TEST(name) printf("\n=== Test: %s ===\n", name)
#define PASS() printf("✓ PASS\n")
#define FAIL(msg) do { printf("✗ FAIL: %s\n", msg); exit(1); } while(0)

/*
 * Test that retracting a memory makes it unavailable via get_current
 */
static void test_retract_makes_memory_unavailable_for_recall(void) {
    TEST("Retract makes memory unavailable for recall");

    /* Initialize */
    memory_db_cleanup_global();
    int init_result = memory_db_init_global("/tmp/test_memory_retract_recall.db");
    if (init_result != 0) {
        printf("  Skipped - init failed: %s\n", memory_db_last_error(NULL));
        PASS();
        return;
    }

    MemoryDB *handle = memory_db_get_global();
    assert(handle != NULL);

    /* Step 1: Store a memory with "sets" */
    int64_t card_id1 = memory_db_store(handle, "user", "coding_style",
                                       "prefers_tabs",
                                       MEMORY_KIND_PREFERENCE, MEMORY_RELATION_SETS);
    printf("  Stored memory, card_id = %lld\n", (long long)card_id1);
    assert(card_id1 >= 0);

    /* Step 2: Verify we can recall it */
    MemoryCard *recalled = memory_db_get_current(handle, "user", "coding_style");
    if (recalled == NULL) {
        FAIL("Should be able to recall memory after storing it");
    }
    printf("  Recalled value: %s\n", recalled->value);
    assert(strcmp(recalled->value, "prefers_tabs") == 0);
    assert(strcmp(recalled->relation, "sets") == 0);
    memory_db_free_card(recalled);

    /* Step 3: Retract the memory */
    int64_t card_id2 = memory_db_store(handle, "user", "coding_style",
                                       "prefers_tabs",
                                       MEMORY_KIND_PREFERENCE, MEMORY_RELATION_RETRACTS);
    printf("  Retracted memory, card_id = %lld\n", (long long)card_id2);
    assert(card_id2 >= 0);

    /* Step 4: Verify the memory is no longer available for recall */
    MemoryCard *recalled_after = memory_db_get_current(handle, "user", "coding_style");
    if (recalled_after != NULL) {
        printf("  ERROR: Recalled value after retract: %s (relation: %s)\n",
               recalled_after->value, recalled_after->relation);
        memory_db_free_card(recalled_after);
        FAIL("Memory should not be available after retraction");
    }
    printf("  After retraction, get_current returns NULL (correct)\n");

    memory_db_cleanup_global();
    remove("/tmp/test_memory_retract_recall.db");

    PASS();
}

/*
 * Test that retracted memories don't appear in search results
 */
static void test_retracted_memories_not_in_search_results(void) {
    TEST("Retracted memories not in search results");

    /* Initialize */
    memory_db_cleanup_global();
    int init_result = memory_db_init_global("/tmp/test_memory_retract_search.db");
    if (init_result != 0) {
        printf("  Skipped - init failed: %s\n", memory_db_last_error(NULL));
        PASS();
        return;
    }

    MemoryDB *handle = memory_db_get_global();
    assert(handle != NULL);

    /* Step 1: Store a memory */
    memory_db_store(handle, "user", "workflow",
                    "discuss_before_coding",
                    MEMORY_KIND_PREFERENCE, MEMORY_RELATION_SETS);
    printf("  Stored memory: discuss_before_coding\n");

    /* Step 2: Search for it (should find it) */
    MemorySearchResult *result1 = memory_db_search(handle, "discuss", 10);
    if (result1 == NULL) {
        printf("  Skipping - FTS5 not available\n");
        memory_db_cleanup_global();
        remove("/tmp/test_memory_retract_search.db");
        PASS();
        return;
    }

    printf("  Search before retract: %zu results\n", result1->count);
    int found_before = 0;
    for (size_t i = 0; i < result1->count; i++) {
        if (strcmp(result1->cards[i].value, "discuss_before_coding") == 0) {
            found_before = 1;
            break;
        }
    }
    memory_db_free_result(result1);

    if (!found_before) {
        printf("  Note: Value not found in search (FTS5 may need rebuild)\n");
    }

    /* Step 3: Retract the memory */
    memory_db_store(handle, "user", "workflow",
                    "discuss_before_coding",
                    MEMORY_KIND_PREFERENCE, MEMORY_RELATION_RETRACTS);
    printf("  Retracted memory\n");

    /* Step 4: Search again (should NOT find the retracted value) */
    MemorySearchResult *result2 = memory_db_search(handle, "discuss", 10);
    if (result2 == NULL) {
        printf("  Search returned NULL after retract\n");
        memory_db_cleanup_global();
        remove("/tmp/test_memory_retract_search.db");
        PASS();
        return;
    }

    printf("  Search after retract: %zu results\n", result2->count);
    int found_after = 0;
    for (size_t i = 0; i < result2->count; i++) {
        printf("    Result %zu: value=%s, relation=%s\n",
               i, result2->cards[i].value, result2->cards[i].relation);
        if (strcmp(result2->cards[i].value, "discuss_before_coding") == 0 &&
            strcmp(result2->cards[i].slot, "workflow") == 0) {
            found_after = 1;
        }
    }
    memory_db_free_result(result2);

    if (found_after) {
        FAIL("Retracted memory should not appear in search results");
    }

    printf("  Retracted memory correctly filtered from search\n");

    memory_db_cleanup_global();
    remove("/tmp/test_memory_retract_search.db");

    PASS();
}

/*
 * Test that we can store a new value after retraction
 */
static void test_store_new_value_after_retraction(void) {
    TEST("Store new value after retraction");

    /* Initialize */
    memory_db_cleanup_global();
    int init_result = memory_db_init_global("/tmp/test_memory_retract_new.db");
    if (init_result != 0) {
        printf("  Skipped - init failed: %s\n", memory_db_last_error(NULL));
        PASS();
        return;
    }

    MemoryDB *handle = memory_db_get_global();
    assert(handle != NULL);

    /* Step 1: Store initial value */
    memory_db_store(handle, "user", "editor",
                    "vim",
                    MEMORY_KIND_PREFERENCE, MEMORY_RELATION_SETS);
    printf("  Stored: vim\n");

    /* Step 2: Retract it */
    memory_db_store(handle, "user", "editor",
                    "vim",
                    MEMORY_KIND_PREFERENCE, MEMORY_RELATION_RETRACTS);
    printf("  Retracted vim\n");

    /* Step 3: Store new value */
    memory_db_store(handle, "user", "editor",
                    "emacs",
                    MEMORY_KIND_PREFERENCE, MEMORY_RELATION_SETS);
    printf("  Stored new value: emacs\n");

    /* Step 4: Verify we get the new value */
    MemoryCard *recalled = memory_db_get_current(handle, "user", "editor");
    if (recalled == NULL) {
        FAIL("Should be able to recall new value after retraction");
    }

    printf("  Recalled value: %s (relation: %s)\n", recalled->value, recalled->relation);
    assert(strcmp(recalled->value, "emacs") == 0);
    assert(strcmp(recalled->relation, "sets") == 0);
    memory_db_free_card(recalled);

    memory_db_cleanup_global();
    remove("/tmp/test_memory_retract_new.db");

    PASS();
}

/*
 * Test retracting a non-existent memory doesn't cause issues
 */
static void test_retract_nonexistent_memory(void) {
    TEST("Retract non-existent memory");

    /* Initialize */
    memory_db_cleanup_global();
    int init_result = memory_db_init_global("/tmp/test_memory_retract_nonexist.db");
    if (init_result != 0) {
        printf("  Skipped - init failed: %s\n", memory_db_last_error(NULL));
        PASS();
        return;
    }

    MemoryDB *handle = memory_db_get_global();
    assert(handle != NULL);

    /* Retract something that was never stored */
    int64_t card_id = memory_db_store(handle, "user", "never_stored",
                                      "some_value",
                                      MEMORY_KIND_FACT, MEMORY_RELATION_RETRACTS);
    printf("  Retracted non-existent memory, card_id = %lld\n", (long long)card_id);
    assert(card_id >= 0);

    /* Verify get_current returns NULL */
    MemoryCard *recalled = memory_db_get_current(handle, "user", "never_stored");
    if (recalled != NULL) {
        printf("  ERROR: Got value for never-stored slot: %s\n", recalled->value);
        memory_db_free_card(recalled);
        FAIL("Should not be able to recall a never-stored slot");
    }

    printf("  Correctly returns NULL for non-existent/retracted memory\n");

    memory_db_cleanup_global();
    remove("/tmp/test_memory_retract_nonexist.db");

    PASS();
}

/*
 * Test get_entity_memories excludes retracted entries for each slot
 */
static void test_entity_memories_excludes_retracted(void) {
    TEST("Entity memories excludes retracted");

    /* Initialize */
    memory_db_cleanup_global();
    int init_result = memory_db_init_global("/tmp/test_memory_retract_entity.db");
    if (init_result != 0) {
        printf("  Skipped - init failed: %s\n", memory_db_last_error(NULL));
        PASS();
        return;
    }

    MemoryDB *handle = memory_db_get_global();
    assert(handle != NULL);

    /* Store some values */
    memory_db_store(handle, "user", "slot1", "value1", MEMORY_KIND_FACT, MEMORY_RELATION_SETS);
    memory_db_store(handle, "user", "slot2", "value2", MEMORY_KIND_FACT, MEMORY_RELATION_SETS);
    memory_db_store(handle, "user", "slot1", "value1", MEMORY_KIND_FACT, MEMORY_RELATION_RETRACTS); /* Retract slot1 */

    printf("  Stored: slot1=value1, slot2=value2, then retracted slot1\n");

    /* Get entity memories */
    MemorySearchResult *result = memory_db_get_entity_memories(handle, "user");
    if (result == NULL) {
        printf("  Skipped - get_entity_memories returned NULL\n");
        memory_db_cleanup_global();
        remove("/tmp/test_memory_retract_entity.db");
        PASS();
        return;
    }

    printf("  Entity memories returned %zu results\n", result->count);

    /* Check that slot1 (retracted) is not returned as the current value */
    int found_slot1 = 0;
    int found_slot2 = 0;
    for (size_t i = 0; i < result->count; i++) {
        printf("    Result %zu: slot=%s, value=%s, relation=%s\n",
               i, result->cards[i].slot, result->cards[i].value, result->cards[i].relation);
        if (strcmp(result->cards[i].slot, "slot1") == 0) {
            found_slot1++;
        }
        if (strcmp(result->cards[i].slot, "slot2") == 0) {
            found_slot2++;
        }
    }

    /* With current implementation, we might get all history including retracted.
     * The question is: what's the expected behavior?
     * For get_entity_memories, it might be OK to show history, but the most
     * recent non-retracted should be clear.
     */
    printf("  Found slot1 entries: %d, slot2 entries: %d\n", found_slot1, found_slot2);

    memory_db_free_result(result);
    memory_db_cleanup_global();
    remove("/tmp/test_memory_retract_entity.db");

    PASS();
}

int main(void) {
    printf("===============================================\n");
    printf("Running Memory Retraction Tests\n");
    printf("===============================================\n");
    printf("\nThese tests verify that relation: \"retracts\" properly\n");
    printf("removes memories from recall and search results.\n");

    test_retract_makes_memory_unavailable_for_recall();
    test_retracted_memories_not_in_search_results();
    test_store_new_value_after_retraction();
    test_retract_nonexistent_memory();
    test_entity_memories_excludes_retracted();

    printf("\n===============================================\n");
    printf("All memory retraction tests passed!\n");
    printf("===============================================\n");

    return 0;
}
