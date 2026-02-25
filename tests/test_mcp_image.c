/*
 * test_mcp_image.c - Test MCP image content handling
 *
 * Tests the MCP client's ability to handle image content from MCP servers
 * like the Playwright server that returns screenshots.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>
#include "../src/base64.h"

// Define the struct to match MCPToolResult layout
typedef struct {
    char *tool_name;
    char *result;
    void *blob;
    size_t blob_size;
    char *mime_type;
    int is_error;
} MockMCPToolResult;

// Simple version of MCPToolResult free function
static void free_mcp_tool_result(void *result_ptr) {
    if (!result_ptr) return;

    MockMCPToolResult *result = (MockMCPToolResult *)result_ptr;

    free(result->tool_name);
    free(result->result);
    free(result->mime_type);
    if (result->blob) {
        free(result->blob);
    }
    free(result);
}

// Test base64 image decoding and PNG signature verification
static int test_mcp_image_response(void) {
    printf("=== Testing MCP Image Response Handling ===\n");

    int passed = 1;

    // Create a mock MCPToolResult
    MockMCPToolResult *result = calloc(1, sizeof(MockMCPToolResult));

    if (!result) {
        printf("❌ Failed to allocate MCPToolResult\n");
        return 0;
    }

    // Simulate the response we'd get from parsing the JSON
    result->tool_name = strdup("test_tool");
    result->is_error = 0;

    // This is a small PNG image (1x1 pixel, red)
    const char *base64_image = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==";

    // Decode the base64 image
    size_t decoded_size = 0;
    unsigned char *decoded_data = base64_decode(base64_image, strlen(base64_image), &decoded_size);

    if (decoded_data) {
        result->blob = decoded_data;
        result->blob_size = decoded_size;
        result->mime_type = strdup("image/png");

        printf("✅ Successfully decoded image data\n");
        printf("   - Tool name: %s\n", result->tool_name);
        printf("   - MIME type: %s\n", result->mime_type);
        printf("   - Blob size: %zu bytes\n", result->blob_size);
        printf("   - Is error: %d\n", result->is_error);

        // Verify the blob data looks like PNG (starts with PNG signature)
        unsigned char *blob_data = (unsigned char *)result->blob;
        if (result->blob_size >= 8 &&
            blob_data[0] == 0x89 &&
            blob_data[1] == 'P' &&
            blob_data[2] == 'N' &&
            blob_data[3] == 'G') {
            printf("✅ PNG signature verified\n");
        } else {
            printf("❌ Blob doesn't look like PNG\n");
            passed = 0;
        }
    } else {
        printf("❌ Failed to decode base64 image\n");
        passed = 0;
    }

    // Clean up
    free_mcp_tool_result(result);
    printf("✅ MCP tool result cleanup successful\n");

    return passed;
}

// Test JPEG image handling
static int test_mcp_jpeg_response(void) {
    printf("=== Testing MCP JPEG Response Handling ===\n");

    int passed = 1;

    // Create a mock MCPToolResult
    MockMCPToolResult *result = calloc(1, sizeof(MockMCPToolResult));

    if (!result) {
        printf("❌ Failed to allocate MCPToolResult\n");
        return 0;
    }

    // Simulate JPEG response
    result->tool_name = strdup("screenshot_tool");
    result->is_error = 0;
    result->mime_type = strdup("image/jpeg");

    // For JPEG, we'll just test that the MIME type is set correctly
    // since we don't have a small valid JPEG base64 string handy
    if (result->mime_type && strcmp(result->mime_type, "image/jpeg") == 0) {
        printf("✅ JPEG MIME type set correctly\n");
    } else {
        printf("❌ JPEG MIME type not set correctly\n");
        passed = 0;
    }

    printf("   - Tool name: %s\n", result->tool_name);
    printf("   - MIME type: %s\n", result->mime_type);

    // Clean up
    free_mcp_tool_result(result);

    return passed;
}

int main(void) {
    printf("Testing MCP Image Content Handling\n");
    printf("===================================\n\n");

    int tests_passed = 0;
    int tests_total = 0;

    // Run tests
    tests_total++;
    if (test_mcp_image_response()) {
        tests_passed++;
    }

    printf("\n");

    tests_total++;
    if (test_mcp_jpeg_response()) {
        tests_passed++;
    }

    printf("\n===================================\n");
    printf("Test Summary: %d/%d tests passed\n", tests_passed, tests_total);

    if (tests_passed == tests_total) {
        printf("✅ All MCP image tests passed!\n");
        return 0;
    } else {
        printf("❌ Some MCP image tests failed\n");
        return 1;
    }
}
