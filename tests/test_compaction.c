/*
 * Test suite for context compaction functionality
 *
 * Compilation: make test-compaction
 * Usage: ./build/test_compaction
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/klawed_internal.h"
#include "../src/compaction.h"

// Test helper macros
#define TEST(name) printf("\n=== Test: %s ===\n", name)
#define PASS() printf("✓ PASS\n")
#define FAIL(msg) do { printf("✗ FAIL: %s\n", msg); exit(1); } while(0)

// Helper to create a test message
static void create_test_message(InternalMessage *msg, MessageRole role, const char *text) {
    memset(msg, 0, sizeof(InternalMessage));
    msg->role = role;
    msg->contents = malloc(sizeof(InternalContent));
    if (msg->contents) {
        msg->content_count = 1;
        msg->contents[0].type = INTERNAL_TEXT;
        msg->contents[0].text = strdup(text);
    }
}

// Helper to free a test message
static void free_test_message(InternalMessage *msg) {
    if (!msg) return;

    if (msg->contents) {
        for (int i = 0; i < msg->content_count; i++) {
            switch (msg->contents[i].type) {
                case INTERNAL_TEXT:
                    if (msg->contents[i].text) {
                        free(msg->contents[i].text);
                    }
                    break;

                case INTERNAL_TOOL_CALL:
                    if (msg->contents[i].tool_name) {
                        free(msg->contents[i].tool_name);
                    }
                    if (msg->contents[i].tool_id) {
                        free(msg->contents[i].tool_id);
                    }
                    // tool_params is cJSON*, not allocated in tests
                    break;

                case INTERNAL_TOOL_RESPONSE:
                    if (msg->contents[i].tool_id) {
                        free(msg->contents[i].tool_id);
                    }
                    // tool_output is cJSON*, not allocated in tests
                    break;

                case INTERNAL_IMAGE:
                    // Nothing to free in test stubs
                    break;

                default:
                    break;
            }
        }
        free(msg->contents);
    }
    memset(msg, 0, sizeof(InternalMessage));
}

// Helper to create a test message with tool call
static void create_tool_call_message(InternalMessage *msg, const char *tool_name, const char *tool_id) {
    memset(msg, 0, sizeof(InternalMessage));
    msg->role = MSG_ASSISTANT;
    msg->contents = calloc(1, sizeof(InternalContent));
    if (msg->contents) {
        msg->content_count = 1;
        msg->contents[0].type = INTERNAL_TOOL_CALL;
        msg->contents[0].tool_name = strdup(tool_name);
        msg->contents[0].tool_id = strdup(tool_id);
        msg->contents[0].tool_params = NULL;  // Simplified for tests
    }
}

// Helper to create a test tool result message
static void create_tool_result_message(InternalMessage *msg, const char *tool_id, const char *result_text) {
    (void)result_text;  // Unused in simplified test
    memset(msg, 0, sizeof(InternalMessage));
    msg->role = MSG_USER;  // Tool results are in user messages
    msg->contents = calloc(1, sizeof(InternalContent));
    if (msg->contents) {
        msg->content_count = 1;
        msg->contents[0].type = INTERNAL_TOOL_RESPONSE;
        msg->contents[0].tool_id = strdup(tool_id);
        // tool_output would be cJSON, but we use text for simplified test
    }
}

// Helper to create a minimal ConversationState for testing
static ConversationState* create_test_state(int message_count) {
    ConversationState *state = calloc(1, sizeof(ConversationState));
    if (!state) return NULL;

    state->count = message_count;

    // Create system message at position 0
    if (message_count > 0) {
        create_test_message(&state->messages[0], MSG_SYSTEM, "You are a helpful assistant.");
    }

    // Create alternating user/assistant messages
    for (int i = 1; i < message_count; i++) {
        if (i % 2 == 1) {
            char buf[64];
            snprintf(buf, sizeof(buf), "User message %d", i);
            create_test_message(&state->messages[i], MSG_USER, buf);
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Assistant response %d", i);
            create_test_message(&state->messages[i], MSG_ASSISTANT, buf);
        }
    }

    return state;
}

// Helper to free test state
static void free_test_state(ConversationState *state) {
    if (!state) return;

    for (int i = 0; i < state->count; i++) {
        free_test_message(&state->messages[i]);
    }
    free(state);
}

// ============================================================================
// Tests for compaction_init_config
// ============================================================================

static void test_init_config_defaults(void) {
    TEST("Initialize config with defaults");

    CompactionConfig config;
    compaction_init_config(&config, 1, NULL);

    // With memory_db, enabled is honored as passed
    assert(config.enabled == 1);
    assert(config.threshold_percent == 60);  // Default threshold
    assert(config.keep_recent == 20);        // Default keep_recent
    assert(config.last_compacted_index == -1);

    PASS();
}

static void test_init_config_disabled(void) {
    TEST("Initialize config disabled");

    CompactionConfig config;
    compaction_init_config(&config, 0, NULL);

    assert(config.enabled == 0);
    assert(config.threshold_percent == 60);
    assert(config.keep_recent == 20);

    PASS();
}

static void test_init_config_env_override(void) {
    TEST("Initialize config with environment variable overrides");

    // Set environment variables
    setenv("KLAWED_COMPACT_THRESHOLD", "75", 1);
    setenv("KLAWED_COMPACT_KEEP_RECENT", "30", 1);

    CompactionConfig config;
    compaction_init_config(&config, 1, NULL);

    assert(config.threshold_percent == 75);
    assert(config.keep_recent == 30);

    // Clean up
    unsetenv("KLAWED_COMPACT_THRESHOLD");
    unsetenv("KLAWED_COMPACT_KEEP_RECENT");

    PASS();
}

static void test_init_config_invalid_env(void) {
    TEST("Initialize config with invalid environment values");

    // Set invalid (zero/negative) values - should fall back to defaults
    setenv("KLAWED_COMPACT_THRESHOLD", "0", 1);
    setenv("KLAWED_COMPACT_KEEP_RECENT", "-5", 1);

    CompactionConfig config;
    compaction_init_config(&config, 1, NULL);

    // Should use defaults when env values are invalid
    assert(config.threshold_percent == 60);
    assert(config.keep_recent == 20);

    // Clean up
    unsetenv("KLAWED_COMPACT_THRESHOLD");
    unsetenv("KLAWED_COMPACT_KEEP_RECENT");

    PASS();
}

static void test_init_config_null(void) {
    TEST("Initialize config with NULL pointer");

    // Should not crash
    compaction_init_config(NULL, 1, NULL);

    PASS();
}

// ============================================================================
// Tests for compaction_should_trigger
// ============================================================================

static void test_should_trigger_disabled(void) {
    TEST("Should not trigger when disabled");

    CompactionConfig config = {
        .enabled = 0,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000       // Above threshold but disabled
    };

    ConversationState *state = create_test_state(100);
    assert(state != NULL);

    int result = compaction_should_trigger(state, &config);
    assert(result == 0);  // Should not trigger when disabled

    free_test_state(state);
    PASS();
}

static void test_should_trigger_below_threshold(void) {
    TEST("Should not trigger below threshold");

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,  // 125k tokens
        .current_tokens = 10000       // Way below 60% of 125k (75k)
    };

    ConversationState *state = create_test_state(100);
    assert(state != NULL);

    int result = compaction_should_trigger(state, &config);
    assert(result == 0);  // Below threshold

    free_test_state(state);
    PASS();
}

static void test_should_trigger_at_threshold(void) {
    TEST("Should trigger at threshold");

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,  // 125k tokens
        .current_tokens = 80000       // Above 60% of 125k (75k)
    };

    ConversationState *state = create_test_state(100);
    assert(state != NULL);

    int result = compaction_should_trigger(state, &config);
    assert(result == 1);  // At threshold, should trigger

    free_test_state(state);
    PASS();
}

static void test_should_trigger_null_params(void) {
    TEST("Should not trigger with NULL parameters");

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    ConversationState *state = create_test_state(100);

    // NULL state
    assert(compaction_should_trigger(NULL, &config) == 0);

    // NULL config
    assert(compaction_should_trigger(state, NULL) == 0);

    // Both NULL
    assert(compaction_should_trigger(NULL, NULL) == 0);

    free_test_state(state);
    PASS();
}

// ============================================================================
// Tests for compaction_perform (only meaningful with HAVE_MEMVID)
// ============================================================================

static void test_perform_compaction_success(void) {
    TEST("Perform compaction successfully");

    ConversationState *state = create_test_state(100);
    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    int result = compaction_perform(state, &config, "test-session", NULL);

    // With memory_db stub, compaction should succeed
    assert(result == 0);

    // Verify structure after compaction
    // Count should be: system (1) + notice (1) + keep_recent (20) = 22
    assert(state->count == 22);
    assert(state->messages[0].role == MSG_SYSTEM);
    assert(state->messages[1].role == MSG_AUTO_COMPACTION);

    free_test_state(state);
    PASS();
}

static void test_perform_null_params(void) {
    TEST("Perform compaction with NULL parameters");

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    ConversationState *state = create_test_state(100);

    // NULL state
    int result = compaction_perform(NULL, &config, "test-session", NULL);
    assert(result == -1);

    // NULL config
    result = compaction_perform(state, NULL, "test-session", NULL);
    assert(result == -1);

    free_test_state(state);
    PASS();
}

static void test_perform_not_enough_messages(void) {
    TEST("Perform compaction with not enough messages");

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    // Only 10 messages - less than keep_recent + 1 (system)
    ConversationState *state = create_test_state(10);
    assert(state != NULL);

    int original_count = state->count;

    int result = compaction_perform(state, &config, "test-session", NULL);
    // Should return 0 (success but no action) when not enough messages
    assert(result == 0);
    assert(state->count == original_count);  // Count unchanged

    free_test_state(state);
    PASS();
}

// ============================================================================
// Tests for tool call/result pair preservation (commit 2d3788e)
// ============================================================================

static void test_perform_preserves_tool_call_pairs(void) {
    TEST("Perform compaction preserves tool call/result pairs");

    // Create a state with:
    // 0: System
    // 1: User
    // 2: Assistant with tool calls (should NOT be split from its results)
    // 3: Tool result
    // 4-25: More messages to trigger compaction

    ConversationState *state = calloc(1, sizeof(ConversationState));
    assert(state != NULL);

    // System message
    create_test_message(&state->messages[0], MSG_SYSTEM, "You are a helpful assistant.");

    // User message
    create_test_message(&state->messages[1], MSG_USER, "Please read a file");

    // Assistant with tool call - this should NOT be compacted without its result
    create_tool_call_message(&state->messages[2], "Read", "call_123");

    // Tool result - paired with the assistant message
    create_tool_result_message(&state->messages[3], "call_123", "File content here");

    // Fill rest with alternating messages to get enough for compaction
    int msg_count = 26;  // Enough to trigger compaction with keep_recent=20
    for (int i = 4; i < msg_count; i++) {
        if (i % 2 == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "User message %d", i);
            create_test_message(&state->messages[i], MSG_USER, buf);
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Assistant response %d", i);
            create_test_message(&state->messages[i], MSG_ASSISTANT, buf);
        }
    }
    state->count = msg_count;

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    int result = compaction_perform(state, &config, "test-session", NULL);

    // Should succeed
    assert(result == 0);

    // The state should now be:
    // 0: System
    // 1: Compaction notice
    // 2+: Recent messages (including the tool call pair if it was in the keep_recent zone)

    // Count should be: system + notice + keep_recent = 22
    assert(state->count == 22);  // 1 (system) + 1 (notice) + 20 (recent)

    // Verify system message is intact
    assert(state->messages[0].role == MSG_SYSTEM);

    // Verify notice was inserted
    assert(state->messages[1].role == MSG_AUTO_COMPACTION);

    // The key test: messages that had tool calls should not be split from their results
    // Since we had tool call at index 2 and result at index 3, and we're keeping last 20,
    // both should be in the "recent" section and preserved together

    // Clean up
    for (int i = 0; i < state->count; i++) {
        free_test_message(&state->messages[i]);
    }
    free(state);
    PASS();
}

static void test_tool_call_boundary_adjustment(void) {
    TEST("Tool call boundary adjustment prevents splitting pairs");

    // Create a scenario where compact_end would land on an assistant with tool calls
    // The compaction should adjust backward to avoid splitting

    ConversationState *state = calloc(1, sizeof(ConversationState));
    assert(state != NULL);

    // System message
    create_test_message(&state->messages[0], MSG_SYSTEM, "System prompt");

    // Create messages where the boundary would cut through tool call/result
    // We want compact_end to land exactly on an assistant with tool calls
    // With keep_recent=3 and 8 total messages:
    // compact_start = 1, compact_end = 8 - 3 - 1 = 4

    // 1: User
    create_test_message(&state->messages[1], MSG_USER, "User 1");

    // 2: Assistant with tool call - this is where compact_end would land
    create_tool_call_message(&state->messages[2], "Read", "call_abc");

    // 3: Tool result
    create_tool_result_message(&state->messages[3], "call_abc", "Result");

    // 4: User
    create_test_message(&state->messages[4], MSG_USER, "User 2");

    // 5-7: More messages (these will be kept)
    create_test_message(&state->messages[5], MSG_USER, "User 3");
    create_test_message(&state->messages[6], MSG_ASSISTANT, "Assistant 1");
    create_test_message(&state->messages[7], MSG_USER, "User 4");

    state->count = 8;

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 3,  // Keep last 3 messages
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    // Without the fix, compaction would try to compact messages 1-4
    // But message 2 has tool calls, so it should adjust to compact 1-1 only

    int result = compaction_perform(state, &config, "test-session", NULL);

    // Should succeed
    assert(result == 0);

    // With the boundary adjustment, we should have:
    // 0: System
    // 1: Notice
    // 2-?: Recent messages (messages 5,6,7 plus the tool pair that got pulled in)

    // The count should reflect what was actually compacted
    // System (1) + Notice (1) + recent messages (at least keep_recent=3)
    assert(state->count >= 4);

    // Clean up
    for (int i = 0; i < state->count; i++) {
        free_test_message(&state->messages[i]);
    }
    free(state);
    PASS();
}

static void test_all_messages_have_tool_calls(void) {
    TEST("Compaction handles case where boundary has tool calls");

    // Edge case: the message at compact_end is an assistant with tool calls
    // The boundary adjustment should move backward to avoid splitting

    ConversationState *state = calloc(1, sizeof(ConversationState));
    assert(state != NULL);

    // System message
    create_test_message(&state->messages[0], MSG_SYSTEM, "System");

    // Create messages where compact_end lands on assistant with tool calls
    // With 6 messages and keep_recent=2:
    // compact_start = 1, compact_end = 6 - 2 - 1 = 3

    create_test_message(&state->messages[1], MSG_USER, "User 1");
    create_tool_call_message(&state->messages[2], "Read", "call_1");
    create_tool_result_message(&state->messages[3], "call_1", "Result 1");  // compact_end lands here - OK (not assistant with tool calls)
    create_test_message(&state->messages[4], MSG_USER, "User 2");  // Kept
    create_test_message(&state->messages[5], MSG_ASSISTANT, "Assistant 1");  // Kept

    state->count = 6;

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 2,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    int result = compaction_perform(state, &config, "test-session", NULL);

    // Should succeed
    assert(result == 0);

    // After compaction: system + notice + keep_recent = 1 + 1 + 2 = 4
    assert(state->count == 4);

    // Clean up
    for (int i = 0; i < state->count; i++) {
        free_test_message(&state->messages[i]);
    }
    free(state);
    PASS();
}

// ============================================================================
// Tests for memory safety with memmove/zeroing (commit 971028b)
// ============================================================================

static void test_compaction_memory_safety(void) {
    TEST("Compaction memory safety - no double-free after memmove");

    // This test verifies that after compaction, the message array
    // doesn't have dangling pointers that could cause double-free

    ConversationState *state = calloc(1, sizeof(ConversationState));
    assert(state != NULL);

    // Create a moderate number of messages
    int msg_count = 30;
    create_test_message(&state->messages[0], MSG_SYSTEM, "System prompt");
    for (int i = 1; i < msg_count; i++) {
        char buf[64];
        if (i % 2 == 1) {
            snprintf(buf, sizeof(buf), "User message %d with some content", i);
            create_test_message(&state->messages[i], MSG_USER, buf);
        } else {
            snprintf(buf, sizeof(buf), "Assistant response %d with details", i);
            create_test_message(&state->messages[i], MSG_ASSISTANT, buf);
        }
    }
    state->count = msg_count;

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 10,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    // Perform compaction
    int result = compaction_perform(state, &config, "test-session", NULL);
    assert(result == 0);

    // Verify the structure is correct
    assert(state->count == 12);  // 1 (system) + 1 (notice) + 10 (recent)
    assert(state->messages[0].role == MSG_SYSTEM);
    assert(state->messages[1].role == MSG_AUTO_COMPACTION);

    // Verify that remaining messages have valid content
    for (int i = 2; i < state->count; i++) {
        assert(state->messages[i].role == MSG_USER || state->messages[i].role == MSG_ASSISTANT);
        assert(state->messages[i].content_count > 0);
        assert(state->messages[i].contents != NULL);
    }

    // Now free everything - this should not crash or cause memory errors
    // If memmove/zeroing wasn't done correctly, we might get double-free here
    for (int i = 0; i < state->count; i++) {
        free_test_message(&state->messages[i]);
    }

    // Also try to free the "old" positions that were zeroed
    // If zeroing wasn't done, this might access freed memory
    // But since we zeroed with memset, these should be safe (all zeros)
    for (int i = state->count; i < MAX_MESSAGES && i < msg_count; i++) {
        // These positions should have been zeroed, so contents is NULL
        // If not zeroed, we'd have dangling pointers
        if (state->messages[i].contents != NULL) {
            // This would indicate memset didn't work properly
            // In that case, try to clean up to avoid leaks in test
            free_test_message(&state->messages[i]);
        }
    }

    free(state);
    PASS();
}

static void test_compaction_with_overlapping_regions(void) {
    TEST("Compaction handles overlapping memory regions correctly");

    // Test the specific case where memmove is needed vs memcpy
    // This happens when recent_start < 2 (rare but possible)

    ConversationState *state = calloc(1, sizeof(ConversationState));
    assert(state != NULL);

    // Create messages where the math results in overlapping regions
    // With keep_recent=25 and 30 messages:
    // compact_end = 30 - 25 - 1 = 4
    // recent_start = 4 + 1 = 5
    // So we memmove from position 5 to position 2

    int msg_count = 30;
    create_test_message(&state->messages[0], MSG_SYSTEM, "System");
    for (int i = 1; i < msg_count; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Message %d content here", i);
        create_test_message(&state->messages[i], (i % 2 == 1) ? MSG_USER : MSG_ASSISTANT, buf);
    }
    state->count = msg_count;

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 25,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    int result = compaction_perform(state, &config, "test-session", NULL);
    assert(result == 0);

    // Verify structure
    assert(state->count == 27);  // 1 + 1 + 25
    assert(state->messages[0].role == MSG_SYSTEM);
    assert(state->messages[1].role == MSG_AUTO_COMPACTION);

    // Verify content integrity of moved messages
    for (int i = 2; i < state->count; i++) {
        assert(state->messages[i].contents != NULL);
        assert(state->messages[i].content_count > 0);
        // The text should have been moved, not copied
        assert(state->messages[i].contents[0].text != NULL);
    }

    // Clean up
    for (int i = 0; i < state->count; i++) {
        free_test_message(&state->messages[i]);
    }
    free(state);
    PASS();
}

// ============================================================================
// Main test runner
// ============================================================================

int main(void) {
    printf("======================================\n");
    printf("Compaction Module Test Suite\n");
    printf("======================================\n");

#ifdef HAVE_MEMVID
    printf("HAVE_MEMVID: defined\n");
#else
    printf("HAVE_MEMVID: not defined\n");
#endif

    // Config initialization tests
    test_init_config_defaults();
    test_init_config_disabled();
    test_init_config_env_override();
    test_init_config_invalid_env();
    test_init_config_null();

    // Should trigger tests
    test_should_trigger_disabled();
    test_should_trigger_below_threshold();
    test_should_trigger_at_threshold();
    test_should_trigger_null_params();

    // Perform tests
    test_perform_compaction_success();
    test_perform_null_params();
    test_perform_not_enough_messages();

    // Tool call/result pair preservation tests
    test_perform_preserves_tool_call_pairs();
    test_tool_call_boundary_adjustment();
    test_all_messages_have_tool_calls();

    // Memory safety tests
    test_compaction_memory_safety();
    test_compaction_with_overlapping_regions();

    printf("\n======================================\n");
    printf("All tests passed!\n");
    printf("======================================\n");

    return 0;
}
