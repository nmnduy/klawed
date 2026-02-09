/*
 * Test suite for TUI Tool Output Connector (Tree Drawing) functionality
 *
 * Tests the tree connector feature that uses "└─" to connect consecutive
 * tool output lines from the same tool, rather than repeating the full prefix.
 *
 * Tests:
 * - Tool name extraction from prefixes
 * - Tree connector logic (same tool vs different tool)
 * - Tool tracking reset on message type changes
 * - Edge cases (NULL, empty strings, malformed prefixes)
 *
 * Compilation: make test-tui-tool-connector
 * Usage: ./build/test_tui_tool_connector
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Minimal TUIState structure for testing
typedef struct TUIStateStruct {
    char *last_tool_name;
    // Other fields not needed for these tests
} TUIState;

// Function prototypes (to avoid missing-prototypes warnings)
char* tui_conversation_extract_tool_name(const char *prefix);
int tui_conversation_is_tool_message(const char *prefix);
void tui_conversation_reset_tool_tracking(TUIState *tui);
const char* tui_conversation_get_tool_display_prefix(TUIState *tui, const char *prefix);

// Include the function declarations (we'll define them inline for testing)
// These are the actual implementations from tui_conversation.c

// Extract tool name from tool prefix
char* tui_conversation_extract_tool_name(const char *prefix) {
    if (!prefix || prefix[0] == '\0') {
        return NULL;
    }

    // Check for "● " prefix (circle + space)
    // ● in UTF-8 is 0xE2 0x97 0x8F, followed by space 0x20
    const char *CIRCLE_PREFIX = "\xe2\x97\x8f ";
    size_t circle_len = 4; // 3 bytes for ● + 1 byte for space

    if (strncmp(prefix, CIRCLE_PREFIX, circle_len) != 0) {
        return NULL;  // Not a circle-prefixed tool message
    }

    // Extract the tool name (everything after "● ")
    const char *tool_name_start = prefix + circle_len;

    // Find the end of the tool name (look for colon or end of string)
    const char *colon = strchr(tool_name_start, ':');
    size_t name_len;
    if (colon) {
        name_len = (size_t)(colon - tool_name_start);
    } else {
        name_len = strlen(tool_name_start);
    }

    // Skip leading whitespace in tool name
    while (name_len > 0 && (*tool_name_start == ' ' || *tool_name_start == '\t')) {
        tool_name_start++;
        name_len--;
    }

    // Skip trailing whitespace in tool name
    while (name_len > 0 && (tool_name_start[name_len - 1] == ' ' ||
                            tool_name_start[name_len - 1] == '\t')) {
        name_len--;
    }

    if (name_len == 0) {
        return NULL;
    }

    // Allocate and copy the tool name
    char *tool_name = malloc(name_len + 1);
    if (!tool_name) {
        return NULL;
    }

    memcpy(tool_name, tool_name_start, name_len);
    tool_name[name_len] = '\0';

    return tool_name;
}

// Check if a prefix is a tool message (starts with ●)
int tui_conversation_is_tool_message(const char *prefix) {
    if (!prefix || prefix[0] == '\0') {
        return 0;
    }

    // The ● character is UTF-8: 0xE2 0x97 0x8F (3 bytes)
    return (prefix[0] == '\xe2' && prefix[1] == '\x97' && prefix[2] == '\x8f') ? 1 : 0;
}

// Reset tool tracking state
void tui_conversation_reset_tool_tracking(TUIState *tui) {
    if (!tui) {
        return;
    }
    free(tui->last_tool_name);
    tui->last_tool_name = NULL;
}

// Determine the display prefix for a tool message
const char* tui_conversation_get_tool_display_prefix(TUIState *tui, const char *prefix) {
    if (!tui || !prefix) {
        return prefix;
    }

    // Check if this is a tool message
    if (!tui_conversation_is_tool_message(prefix)) {
        // Not a tool message - reset tracking and return original
        tui_conversation_reset_tool_tracking(tui);
        return prefix;
    }

    // Extract the tool name from current prefix
    char *current_tool = tui_conversation_extract_tool_name(prefix);
    if (!current_tool) {
        // Could not extract tool name - use original prefix
        tui_conversation_reset_tool_tracking(tui);
        return prefix;
    }

    const char *result;

    // Compare with last tool name
    if (tui->last_tool_name && strcmp(tui->last_tool_name, current_tool) == 0) {
        // Same tool - use tree connector (└─)
        result = "\xe2\x94\x94\xe2\x94\x80 ";  // └─ followed by space
    } else {
        // Different tool or first tool - use full prefix
        result = prefix;
        // Update last_tool_name
        free(tui->last_tool_name);
        tui->last_tool_name = strdup(current_tool);
    }

    free(current_tool);
    return result;
}

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Test framework macros
#define TEST(name) printf("\n=== Test: %s ===\n", name)
#define PASS() do { tests_passed++; printf("✓ PASS\n"); } while(0)
#define FAIL(msg) do { \
    tests_failed++; \
    printf("✗ FAIL: %s\n", msg); \
} while(0)
#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { FAIL(msg); } else { PASS(); } \
} while(0)
#define ASSERT_STR_EQ(a, b, msg) do { \
    tests_run++; \
    if (strcmp((a), (b)) != 0) { \
        printf("  Expected: '%s'\n", b); \
        printf("  Actual:   '%s'\n", a); \
        FAIL(msg); \
    } else { PASS(); } \
} while(0)

// ============================================================================
// Test: Tool Name Extraction
// ============================================================================

static void test_extract_tool_name_basic(void) {
    TEST("Extract tool name from basic prefix");

    char *result;

    // Standard tool prefix format: "● ToolName"
    result = tui_conversation_extract_tool_name("● Bash");
    ASSERT(result != NULL, "Should extract tool name");
    if (result) {
        ASSERT_STR_EQ(result, "Bash", "Should extract 'Bash'");
        free(result);
    }

    // Tool with description after colon
    result = tui_conversation_extract_tool_name("● Read: file.txt");
    ASSERT(result != NULL, "Should extract tool name with colon");
    if (result) {
        ASSERT_STR_EQ(result, "Read", "Should extract 'Read' before colon");
        free(result);
    }

    // Multi-word tool name (extracts entire name before space, not just first word)
    // Note: The implementation extracts everything before colon or end of string
    result = tui_conversation_extract_tool_name("● File Search");
    ASSERT(result != NULL, "Should extract multi-word tool name");
    if (result) {
        ASSERT_STR_EQ(result, "File Search", "Should extract entire tool name");
        free(result);
    }
}

static void test_extract_tool_name_edge_cases(void) {
    TEST("Extract tool name - edge cases");

    char *result;

    // NULL input
    result = tui_conversation_extract_tool_name(NULL);
    ASSERT(result == NULL, "NULL input should return NULL");

    // Empty string
    result = tui_conversation_extract_tool_name("");
    ASSERT(result == NULL, "Empty string should return NULL");

    // No circle prefix
    result = tui_conversation_extract_tool_name("Bash: command");
    ASSERT(result == NULL, "Non-tool prefix should return NULL");

    // Circle only, no tool name
    result = tui_conversation_extract_tool_name("●");
    ASSERT(result == NULL, "Circle only should return NULL");

    // Circle with space but no tool name
    result = tui_conversation_extract_tool_name("● ");
    ASSERT(result == NULL, "Circle with space only should return NULL");

    // Regular bracket prefix (not a tool)
    result = tui_conversation_extract_tool_name("[User]");
    ASSERT(result == NULL, "[User] prefix should return NULL");

    result = tui_conversation_extract_tool_name("[Assistant]");
    ASSERT(result == NULL, "[Assistant] prefix should return NULL");
}

static void test_extract_tool_name_whitespace(void) {
    TEST("Extract tool name - whitespace handling");

    char *result;

    // Leading whitespace in tool name
    result = tui_conversation_extract_tool_name("●   Bash");
    ASSERT(result != NULL, "Should handle leading whitespace");
    if (result) {
        ASSERT_STR_EQ(result, "Bash", "Should trim leading whitespace");
        free(result);
    }

    // Trailing whitespace before colon
    result = tui_conversation_extract_tool_name("● Bash   : output");
    ASSERT(result != NULL, "Should handle trailing whitespace before colon");
    if (result) {
        ASSERT_STR_EQ(result, "Bash", "Should trim trailing whitespace");
        free(result);
    }
}

// ============================================================================
// Test: Tool Message Detection
// ============================================================================

static void test_is_tool_message(void) {
    TEST("Is tool message detection");

    // Tool messages (circle prefix)
    ASSERT(tui_conversation_is_tool_message("● Bash") == 1,
           "● Bash should be detected as tool message");
    ASSERT(tui_conversation_is_tool_message("● Read: file.txt") == 1,
           "● Read should be detected as tool message");

    // Non-tool messages
    ASSERT(tui_conversation_is_tool_message("[User]") == 0,
           "[User] should NOT be detected as tool message");
    ASSERT(tui_conversation_is_tool_message("[Assistant]") == 0,
           "[Assistant] should NOT be detected as tool message");
    ASSERT(tui_conversation_is_tool_message("[System]") == 0,
           "[System] should NOT be detected as tool message");
    ASSERT(tui_conversation_is_tool_message("Bash: command") == 0,
           "Plain text should NOT be detected as tool message");

    // Edge cases
    ASSERT(tui_conversation_is_tool_message(NULL) == 0,
           "NULL should return 0");
    ASSERT(tui_conversation_is_tool_message("") == 0,
           "Empty string should return 0");
    ASSERT(tui_conversation_is_tool_message("●") == 1,
           "Circle only should be detected as tool message");
}

// ============================================================================
// Test: Tool Display Prefix Selection
// ============================================================================

static void test_tool_display_prefix_first_occurrence(void) {
    TEST("Tool display prefix - first occurrence");

    TUIState tui;
    memset(&tui, 0, sizeof(TUIState));

    const char *prefix = "● Bash";
    const char *result = tui_conversation_get_tool_display_prefix(&tui, prefix);

    // First occurrence should return the original prefix
    ASSERT(result == prefix, "First occurrence should return original prefix");

    // last_tool_name should be set
    ASSERT(tui.last_tool_name != NULL, "last_tool_name should be set after first tool");
    if (tui.last_tool_name) {
        ASSERT_STR_EQ(tui.last_tool_name, "Bash", "last_tool_name should be 'Bash'");
    }

    free(tui.last_tool_name);
}

static void test_tool_display_prefix_consecutive_same_tool(void) {
    TEST("Tool display prefix - consecutive same tool");

    TUIState tui;
    memset(&tui, 0, sizeof(TUIState));

    const char *prefix1 = "● Bash";
    const char *prefix2 = "● Bash: output";

    // First occurrence
    const char *result1 = tui_conversation_get_tool_display_prefix(&tui, prefix1);
    ASSERT(result1 == prefix1, "First occurrence should return original prefix");

    // Second occurrence (same tool)
    const char *result2 = tui_conversation_get_tool_display_prefix(&tui, prefix2);
    ASSERT(result2 != prefix2, "Second occurrence should return different prefix");
    ASSERT(result2 != NULL, "Result should not be NULL");

    // Check that it's the tree connector (└─)
    if (result2) {
        // The tree connector should start with "└" (UTF-8: 0xE2 0x94 0x94)
        ASSERT((unsigned char)result2[0] == 0xE2 &&
               (unsigned char)result2[1] == 0x94 &&
               (unsigned char)result2[2] == 0x94,
               "Second occurrence should use tree connector (└)");
    }

    free(tui.last_tool_name);
}

static void test_tool_display_prefix_different_tools(void) {
    TEST("Tool display prefix - different tools");

    TUIState tui;
    memset(&tui, 0, sizeof(TUIState));

    const char *prefix1 = "● Bash";
    const char *prefix2 = "● Read";

    // First tool
    const char *result1 = tui_conversation_get_tool_display_prefix(&tui, prefix1);
    ASSERT(result1 == prefix1, "First tool should return original prefix");

    // Second tool (different)
    const char *result2 = tui_conversation_get_tool_display_prefix(&tui, prefix2);
    ASSERT(result2 == prefix2, "Different tool should return original prefix");

    // last_tool_name should be updated to new tool
    ASSERT(tui.last_tool_name != NULL, "last_tool_name should be set");
    if (tui.last_tool_name) {
        ASSERT_STR_EQ(tui.last_tool_name, "Read", "last_tool_name should be updated to 'Read'");
    }

    free(tui.last_tool_name);
}

static void test_tool_display_prefix_non_tool_resets(void) {
    TEST("Tool display prefix - non-tool message resets tracking");

    TUIState tui;
    memset(&tui, 0, sizeof(TUIState));

    const char *tool_prefix = "● Bash";
    const char *user_prefix = "[User]";

    // Set up tool tracking
    tui_conversation_get_tool_display_prefix(&tui, tool_prefix);
    ASSERT(tui.last_tool_name != NULL, "Tool tracking should be set");

    // Non-tool message should reset tracking
    const char *result = tui_conversation_get_tool_display_prefix(&tui, user_prefix);
    ASSERT(strcmp(result, user_prefix) == 0, "Should return original prefix for non-tool");
    ASSERT(tui.last_tool_name == NULL, "Tool tracking should be reset after non-tool message");
}

static void test_tool_display_prefix_null_safety(void) {
    TEST("Tool display prefix - NULL safety");

    TUIState tui;
    memset(&tui, 0, sizeof(TUIState));

    // NULL TUIState - returns the prefix unchanged since we can't do tracking
    const char *test_prefix = "● Bash";
    const char *result = tui_conversation_get_tool_display_prefix(NULL, test_prefix);
    ASSERT(result == test_prefix, "NULL TUIState should return original prefix");

    // NULL prefix - returns NULL
    result = tui_conversation_get_tool_display_prefix(&tui, NULL);
    ASSERT(result == NULL, "NULL prefix should return NULL");
}

// ============================================================================
// Test: Tool Tracking Reset
// ============================================================================

static void test_reset_tool_tracking(void) {
    TEST("Reset tool tracking");

    TUIState tui;
    memset(&tui, 0, sizeof(TUIState));

    // Set up tracking
    tui.last_tool_name = strdup("Bash");
    ASSERT(tui.last_tool_name != NULL, "Setup: tool tracking should be set");

    // Reset
    tui_conversation_reset_tool_tracking(&tui);
    ASSERT(tui.last_tool_name == NULL, "Tool tracking should be NULL after reset");

    // Reset on NULL TUIState (should not crash)
    tui_conversation_reset_tool_tracking(NULL);
    PASS();  // If we get here, no crash occurred
}

// ============================================================================
// Test: Full Conversation Flow
// ============================================================================

static void test_conversation_flow_mixed_messages(void) {
    TEST("Conversation flow - mixed message types");

    TUIState tui;
    memset(&tui, 0, sizeof(TUIState));

    // User message
    const char *result = tui_conversation_get_tool_display_prefix(&tui, "[User]");
    ASSERT(strcmp(result, "[User]") == 0, "User message should return original");
    ASSERT(tui.last_tool_name == NULL, "No tool tracking after user message");

    // First tool
    const char *bash_prefix = "● Bash: ls -la";
    result = tui_conversation_get_tool_display_prefix(&tui, bash_prefix);
    ASSERT(result == bash_prefix, "First tool should return original");
    ASSERT(tui.last_tool_name != NULL, "Tool tracking should be set");

    // Second tool (same)
    result = tui_conversation_get_tool_display_prefix(&tui, "● Bash: output");
    ASSERT(result != bash_prefix, "Same tool should use tree connector");

    // Third tool (different)
    const char *read_prefix = "● Read: file.txt";
    result = tui_conversation_get_tool_display_prefix(&tui, read_prefix);
    ASSERT(result == read_prefix, "Different tool should return original");
    if (tui.last_tool_name) {
        ASSERT_STR_EQ(tui.last_tool_name, "Read", "Tracking should be updated to 'Read'");
    }

    // Same tool again
    result = tui_conversation_get_tool_display_prefix(&tui, "● Read: more content");
    ASSERT(result != read_prefix, "Same tool again should use tree connector");

    // Assistant message (resets tracking)
    result = tui_conversation_get_tool_display_prefix(&tui, "[Assistant]");
    ASSERT(strcmp(result, "[Assistant]") == 0, "Assistant message should return original");
    ASSERT(tui.last_tool_name == NULL, "Tracking should be reset after assistant");

    free(tui.last_tool_name);
}

static void test_conversation_flow_multiple_tools(void) {
    TEST("Conversation flow - alternating tools");

    TUIState tui;
    memset(&tui, 0, sizeof(TUIState));

    const char *bash_prefix = "● Bash";
    const char *read_prefix = "● Read";
    const char *write_prefix = "● Write";

    // Tool 1
    const char *r1 = tui_conversation_get_tool_display_prefix(&tui, bash_prefix);
    ASSERT(r1 == bash_prefix, "First Bash should return original");

    // Tool 2
    const char *r2 = tui_conversation_get_tool_display_prefix(&tui, read_prefix);
    ASSERT(r2 == read_prefix, "Read should return original (different tool)");

    // Tool 3
    const char *r3 = tui_conversation_get_tool_display_prefix(&tui, write_prefix);
    ASSERT(r3 == write_prefix, "Write should return original (different tool)");

    // Back to Tool 1
    const char *r4 = tui_conversation_get_tool_display_prefix(&tui, bash_prefix);
    ASSERT(r4 == bash_prefix, "Bash should return original (different from Write)");

    // Same as Tool 1 again
    const char *bash_output = "● Bash: output";
    const char *r5 = tui_conversation_get_tool_display_prefix(&tui, bash_output);
    ASSERT(r5 != bash_output, "Second Bash should use tree connector");

    free(tui.last_tool_name);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("\n========================================\n");
    printf("TUI Tool Connector Test Suite\n");
    printf("========================================\n");

    // Tool name extraction tests
    test_extract_tool_name_basic();
    test_extract_tool_name_edge_cases();
    test_extract_tool_name_whitespace();

    // Tool message detection tests
    test_is_tool_message();

    // Display prefix selection tests
    test_tool_display_prefix_first_occurrence();
    test_tool_display_prefix_consecutive_same_tool();
    test_tool_display_prefix_different_tools();
    test_tool_display_prefix_non_tool_resets();
    test_tool_display_prefix_null_safety();

    // Tool tracking reset tests
    test_reset_tool_tracking();

    // Full conversation flow tests
    test_conversation_flow_mixed_messages();
    test_conversation_flow_multiple_tools();

    printf("\n========================================\n");
    printf("Test Results: %d passed, %d failed (total: %d)\n",
           tests_passed, tests_failed, tests_run);
    if (tests_failed == 0) {
        printf("✓ All tests passed!\n");
    } else {
        printf("✗ Some tests failed!\n");
    }
    printf("========================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
