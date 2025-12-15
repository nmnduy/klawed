/*
 * Test: Tool Details Display (Simple)
 * Purpose: Verify MCP tool name extraction with generic parameter handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <cjson/cJSON.h>

// Simplified version of the MCP tool details logic (matching new implementation)
static char* get_mcp_tool_details_simple(const char *tool_name, cJSON *arguments) {
    static char details[256];
    details[0] = '\0';

    if (strncmp(tool_name, "mcp_", 4) != 0) {
        return NULL;
    }

    // Extract the actual tool name after the server prefix for display
    const char *actual_tool = strchr(tool_name + 4, '_');
    if (!actual_tool) {
        // Fallback: show the full tool name without "mcp_" prefix
        snprintf(details, sizeof(details), "%s", tool_name + 4);
        details[sizeof(details) - 1] = '\0';
        return details;
    }
    actual_tool++; // Skip the underscore

    // Try to extract the most relevant argument for display
    // Common patterns: url, text, path, element, values, etc.
    cJSON *url = cJSON_GetObjectItem(arguments, "url");
    cJSON *text = cJSON_GetObjectItem(arguments, "text");
    cJSON *path = cJSON_GetObjectItem(arguments, "path");
    cJSON *element = cJSON_GetObjectItem(arguments, "element");

    if (cJSON_IsString(url)) {
        // Tools with URL parameter (navigate, fetch, etc.)
        snprintf(details, sizeof(details), "%s: %s", actual_tool, url->valuestring);
    } else if (cJSON_IsString(text) && strlen(text->valuestring) > 0) {
        // Tools with text parameter (type, search, etc.)
        snprintf(details, sizeof(details), "%s: %.30s%s", actual_tool,
                text->valuestring,
                strlen(text->valuestring) > 30 ? "..." : "");
    } else if (cJSON_IsString(path)) {
        // Tools with path parameter (read, write, etc.)
        snprintf(details, sizeof(details), "%s: %s", actual_tool, path->valuestring);
    } else if (cJSON_IsString(element)) {
        // Tools with element parameter (click, hover, etc.)
        snprintf(details, sizeof(details), "%s: %s", actual_tool, element->valuestring);
    } else {
        // Generic display: just show the tool name
        snprintf(details, sizeof(details), "%s", actual_tool);
    }
    details[sizeof(details) - 1] = '\0';

    return strlen(details) > 0 ? details : NULL;
}

// Test helper
static void assert_contains(const char *result, const char *expected, const char *test_name) {
    if (!result) {
        fprintf(stderr, "✗ FAIL %s: Expected result containing '%s', got NULL\n", test_name, expected);
        exit(1);
    }
    if (strstr(result, expected) == NULL) {
        fprintf(stderr, "✗ FAIL %s: Expected '%s' in result '%s'\n", test_name, expected, result);
        exit(1);
    }
    printf("✓ PASS %s: '%s' found in '%s'\n", test_name, expected, result);
}

// Test Playwright browser tools
static void test_playwright_click(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "element", "Submit button");
    cJSON_AddStringToObject(args, "ref", "button-123");

    char *result = get_mcp_tool_details_simple("mcp_playwright_browser_click", args);
    assert_contains(result, "browser_click", "Playwright browser_click tool name");
    assert_contains(result, "Submit button", "Playwright browser_click with element");

    cJSON_Delete(args);
}

static void test_playwright_type(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "element", "Email input");
    cJSON_AddStringToObject(args, "text", "test@example.com");

    char *result = get_mcp_tool_details_simple("mcp_playwright_browser_type", args);
    assert_contains(result, "browser_type", "Playwright browser_type tool name");
    assert_contains(result, "test@example.com", "Playwright browser_type with text");

    cJSON_Delete(args);
}

static void test_playwright_navigate(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "url", "https://example.com");

    char *result = get_mcp_tool_details_simple("mcp_playwright_browser_navigate", args);
    assert_contains(result, "browser_navigate", "Playwright browser_navigate tool name");
    assert_contains(result, "example.com", "Playwright browser_navigate with URL");

    cJSON_Delete(args);
}

static void test_playwright_snapshot(void) {
    cJSON *args = cJSON_CreateObject();

    char *result = get_mcp_tool_details_simple("mcp_playwright_browser_snapshot", args);
    assert_contains(result, "browser_snapshot", "Playwright browser_snapshot tool name");

    cJSON_Delete(args);
}

// Test generic MCP tools (non-playwright)
static void test_generic_fetch(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "url", "https://api.example.com/data");

    char *result = get_mcp_tool_details_simple("mcp_http_fetch", args);
    assert_contains(result, "fetch", "Generic fetch tool name");
    assert_contains(result, "api.example.com", "Generic fetch with URL");

    cJSON_Delete(args);
}

static void test_generic_search(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "text", "search query");

    char *result = get_mcp_tool_details_simple("mcp_search_query", args);
    assert_contains(result, "query", "Generic search tool name");
    assert_contains(result, "search query", "Generic search with text");

    cJSON_Delete(args);
}

static void test_generic_file_read(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "path", "/path/to/file.txt");

    char *result = get_mcp_tool_details_simple("mcp_fs_read", args);
    assert_contains(result, "read", "Generic file read tool name");
    assert_contains(result, "/path/to/file.txt", "Generic file read with path");

    cJSON_Delete(args);
}

static void test_generic_no_params(void) {
    cJSON *args = cJSON_CreateObject();

    char *result = get_mcp_tool_details_simple("mcp_server_status", args);
    assert_contains(result, "status", "Generic tool with no params");

    cJSON_Delete(args);
}

// Test text truncation
static void test_long_text_truncation(void) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "text", "This is a very long text that should be truncated when displayed in the UI");

    char *result = get_mcp_tool_details_simple("mcp_example_process", args);
    assert_contains(result, "process", "Long text tool name");
    assert_contains(result, "...", "Long text truncation marker");

    cJSON_Delete(args);
}

// Test tool without server prefix (edge case)
static void test_malformed_tool_name(void) {
    cJSON *args = cJSON_CreateObject();

    char *result = get_mcp_tool_details_simple("mcp_noserver", args);
    // Should return the part after "mcp_"
    assert_contains(result, "noserver", "Malformed tool name fallback");

    cJSON_Delete(args);
}

int main(void) {
    printf("Testing MCP tool details display (generic approach)...\n\n");

    // Test Playwright tools (should work with generic logic)
    printf("Testing Playwright tools:\n");
    test_playwright_click();
    test_playwright_type();
    test_playwright_navigate();
    test_playwright_snapshot();

    // Test generic MCP tools (any server)
    printf("\nTesting generic MCP tools:\n");
    test_generic_fetch();
    test_generic_search();
    test_generic_file_read();
    test_generic_no_params();

    // Test edge cases
    printf("\nTesting edge cases:\n");
    test_long_text_truncation();
    test_malformed_tool_name();

    printf("\n✅ All MCP tool details tests passed!\n");
    printf("   Generic parameter detection works for any MCP server/tool\n");
    return 0;
}
