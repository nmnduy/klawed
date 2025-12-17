/*
 * test_uds_socket.c - Unit tests for Unix Domain Socket utilities
 *
 * Tests the UDS socket module functionality including:
 * - Socket creation and cleanup
 * - Connection handling
 * - I/O operations
 * - Event streaming
 * - JSON serialization
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <cjson/cJSON.h>
#include <assert.h>

// Include test stubs
#include "test_stubs.h"

// Include the actual UDS socket module and logger
#include "../src/uds_socket.h"
#include "../src/logger.h"

// ============================================================================
// Test Utilities
// ============================================================================

// Custom assert with message
#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "Assertion failed: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

// Create a temporary socket path
static char* create_temp_socket_path(void) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/test_uds_socket_%d_%d.sock", getpid(), rand() % 10000);
    return path;
}

// Connect to a socket for testing
static int connect_to_socket(const char *socket_path) {
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(client_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(client_fd);
        return -1;
    }

    // Set non-blocking for consistency
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    }

    return client_fd;
}

// ============================================================================
// Test Cases
// ============================================================================

static void test_socket_creation(void) {
    printf("Testing socket creation...\n");
    
    const char *socket_path = create_temp_socket_path();
    
    // Test 1: Create socket successfully
    int server_fd = uds_create_unix_socket(socket_path);
    ASSERT(server_fd >= 0, "Failed to create socket");
    
    // Verify socket file exists
    ASSERT(access(socket_path, F_OK) == 0, "Socket file should exist");
    
    // Test 2: Socket should be listening
    struct sockaddr_un addr;
    int test_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT(test_fd >= 0, "Failed to create test socket");
    
    // Try to connect (should succeed)
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    
    int connect_result = connect(test_fd, (struct sockaddr*)&addr, sizeof(addr));
    ASSERT(connect_result == 0, "Should be able to connect to listening socket");
    
    close(test_fd);
    close(server_fd);
    unlink(socket_path);
    
    printf("  ✓ Socket creation test passed\n");
}

static void test_socket_cleanup(void) {
    printf("Testing socket cleanup...\n");
    
    const char *socket_path = create_temp_socket_path();
    
    // Create socket IPC structure
    SocketIPC socket_ipc = {0};
    socket_ipc.server_fd = uds_create_unix_socket(socket_path);
    ASSERT(socket_ipc.server_fd >= 0, "Failed to create socket");
    
    socket_ipc.client_fd = -1;
    socket_ipc.socket_path = strdup(socket_path);
    socket_ipc.enabled = 1;
    
    // Verify socket file exists
    ASSERT(access(socket_path, F_OK) == 0, "Socket file should exist before cleanup");
    
    // Perform cleanup
    uds_cleanup(&socket_ipc);
    
    // Verify cleanup
    ASSERT(socket_ipc.server_fd == -1, "Server fd should be -1 after cleanup");
    ASSERT(socket_ipc.client_fd == -1, "Client fd should be -1 after cleanup");
    ASSERT(socket_ipc.socket_path == NULL, "Socket path should be NULL after cleanup");
    ASSERT(socket_ipc.enabled == 0, "Socket should be disabled after cleanup");
    
    // Socket file should be removed
    ASSERT(access(socket_path, F_OK) != 0, "Socket file should not exist after cleanup");
    
    printf("  ✓ Socket cleanup test passed\n");
}

static void test_accept_connection(void) {
    printf("Testing accept connection...\n");
    
    const char *socket_path = create_temp_socket_path();
    
    // Create server socket
    int server_fd = uds_create_unix_socket(socket_path);
    ASSERT(server_fd >= 0, "Failed to create socket");
    
    // Test 1: Accept with no pending connections (non-blocking)
    int client_fd = uds_accept_connection(server_fd);
    ASSERT(client_fd == -1, "Should return -1 when no connections pending");
    
    // Test 2: Accept with pending connection
    int test_client = connect_to_socket(socket_path);
    ASSERT(test_client >= 0, "Failed to connect test client");
    
    // Give server time to see the connection
    usleep(10000); // 10ms
    
    client_fd = uds_accept_connection(server_fd);
    ASSERT(client_fd >= 0, "Should accept pending connection");
    
    // Verify client socket is non-blocking
    int flags = fcntl(client_fd, F_GETFL, 0);
    ASSERT((flags & O_NONBLOCK) != 0, "Accepted socket should be non-blocking");
    
    close(client_fd);
    close(test_client);
    close(server_fd);
    unlink(socket_path);
    
    printf("  ✓ Accept connection test passed\n");
}

static void test_socket_io(void) {
    printf("Testing socket I/O operations...\n");
    
    const char *socket_path = create_temp_socket_path();
    
    // Create server socket and accept connection
    int server_fd = uds_create_unix_socket(socket_path);
    ASSERT(server_fd >= 0, "Failed to create socket");
    
    int test_client = connect_to_socket(socket_path);
    ASSERT(test_client >= 0, "Failed to connect test client");
    
    usleep(10000); // 10ms
    int client_fd = uds_accept_connection(server_fd);
    ASSERT(client_fd >= 0, "Failed to accept connection");
    
    // Test 1: Write output
    const char *test_data = "Hello, socket!";
    int write_result = uds_write_output(client_fd, test_data, strlen(test_data));
    ASSERT(write_result == 0, "Should write data successfully");
    
    // Read from test client to verify
    char read_buffer[256];
    ssize_t bytes_read = read(test_client, read_buffer, sizeof(read_buffer) - 1);
    ASSERT(bytes_read == (ssize_t)strlen(test_data), "Should read correct number of bytes");
    read_buffer[bytes_read] = '\0';
    ASSERT(strcmp(read_buffer, test_data) == 0, "Should read correct data");
    
    // Test 2: Read input
    const char *response = "Response from client";
    ssize_t bytes_written = write(test_client, response, strlen(response));
    ASSERT(bytes_written == (ssize_t)strlen(response), "Should write response");
    
    usleep(10000); // 10ms for data to arrive
    
    char input_buffer[256];
    int read_result = uds_read_input(client_fd, input_buffer, sizeof(input_buffer));
    ASSERT(read_result == (int)strlen(response), "Should read response bytes");
    input_buffer[read_result] = '\0';
    ASSERT(strcmp(input_buffer, response) == 0, "Should read correct response");
    
    // Test 3: Check if socket has data
    int has_data = uds_has_data(client_fd);
    ASSERT(has_data == 0, "Should not have data after reading");
    
    // Write more data
    write(test_client, "more", 4);
    usleep(10000);
    
    has_data = uds_has_data(client_fd);
    ASSERT(has_data == 1, "Should have data after writing");
    
    // Read the buffered data first
    read_result = uds_read_input(client_fd, input_buffer, sizeof(input_buffer));
    ASSERT(read_result == 4, "Should read buffered data");
    input_buffer[read_result] = '\0';
    ASSERT(strcmp(input_buffer, "more") == 0, "Should read correct data");
    
    // Test 4: Read with client disconnect
    close(test_client);
    usleep(10000);
    
    // Now read should detect disconnect
    read_result = uds_read_input(client_fd, input_buffer, sizeof(input_buffer));
    ASSERT(read_result == -1, "Should return -1 on client disconnect");
    
    close(client_fd);
    close(server_fd);
    unlink(socket_path);
    
    printf("  ✓ Socket I/O test passed\n");
}

static void test_json_operations(void) {
    printf("Testing JSON operations...\n");
    
    const char *socket_path = create_temp_socket_path();
    
    // Create server socket and accept connection
    int server_fd = uds_create_unix_socket(socket_path);
    ASSERT(server_fd >= 0, "Failed to create socket");
    
    int test_client = connect_to_socket(socket_path);
    ASSERT(test_client >= 0, "Failed to connect test client");
    
    usleep(10000); // 10ms
    int client_fd = uds_accept_connection(server_fd);
    ASSERT(client_fd >= 0, "Failed to accept connection");
    
    // Test 1: Write JSON object
    cJSON *json_obj = cJSON_CreateObject();
    ASSERT(json_obj != NULL, "Failed to create JSON object");
    
    cJSON_AddStringToObject(json_obj, "test", "value");
    cJSON_AddNumberToObject(json_obj, "number", 42);
    
    int write_result = uds_write_json(client_fd, json_obj);
    ASSERT(write_result == 0, "Should write JSON successfully");
    
    // Read and verify JSON
    char json_buffer[256];
    ssize_t bytes_read = read(test_client, json_buffer, sizeof(json_buffer) - 1);
    ASSERT(bytes_read > 0, "Should read JSON data");
    json_buffer[bytes_read] = '\0';
    
    // Parse and verify
    cJSON *parsed = cJSON_Parse(json_buffer);
    ASSERT(parsed != NULL, "Should parse JSON");
    
    cJSON *test_field = cJSON_GetObjectItem(parsed, "test");
    ASSERT(test_field != NULL, "Should have test field");
    ASSERT(strcmp(test_field->valuestring, "value") == 0, "Test field should match");
    
    cJSON *number_field = cJSON_GetObjectItem(parsed, "number");
    ASSERT(number_field != NULL, "Should have number field");
    ASSERT(number_field->valueint == 42, "Number field should match");
    
    cJSON_Delete(parsed);
    cJSON_Delete(json_obj);
    
    // Test 2: Send error
    uds_send_error(client_fd, "Test error message");
    
    // Read error JSON
    bytes_read = read(test_client, json_buffer, sizeof(json_buffer) - 1);
    ASSERT(bytes_read > 0, "Should read error JSON");
    json_buffer[bytes_read] = '\0';
    
    parsed = cJSON_Parse(json_buffer);
    ASSERT(parsed != NULL, "Should parse error JSON");
    
    cJSON *type_field = cJSON_GetObjectItem(parsed, "type");
    ASSERT(type_field != NULL, "Should have type field");
    ASSERT(strcmp(type_field->valuestring, "error") == 0, "Type should be error");
    
    cJSON *message_field = cJSON_GetObjectItem(parsed, "message");
    ASSERT(message_field != NULL, "Should have message field");
    ASSERT(strcmp(message_field->valuestring, "Test error message") == 0, "Message should match");
    
    cJSON_Delete(parsed);
    
    close(test_client);
    close(client_fd);
    close(server_fd);
    unlink(socket_path);
    
    printf("  ✓ JSON operations test passed\n");
}

static void test_streaming_context(void) {
    printf("Testing streaming context...\n");
    
    // Test context initialization and cleanup
    SocketStreamingContext ctx;
    
    // Initialize with dummy fd
    uds_streaming_context_init(&ctx, 123);
    
    // Verify initialization
    ASSERT(ctx.client_fd == 123, "Client fd should be set");
    ASSERT(ctx.accumulated_text != NULL, "Accumulated text buffer should be allocated");
    ASSERT(ctx.accumulated_capacity == 4096, "Default capacity should be 4096");
    ASSERT(ctx.accumulated_size == 0, "Size should be 0 initially");
    ASSERT(ctx.accumulated_text[0] == '\0', "Buffer should be empty");
    
    ASSERT(ctx.tool_input_json != NULL, "Tool input buffer should be allocated");
    ASSERT(ctx.tool_input_capacity == 4096, "Tool input capacity should be 4096");
    ASSERT(ctx.tool_input_size == 0, "Tool input size should be 0");
    ASSERT(ctx.tool_input_json[0] == '\0', "Tool input buffer should be empty");
    
    ASSERT(ctx.content_block_index == -1, "Content block index should be -1 initially");
    ASSERT(ctx.content_block_type == NULL, "Content block type should be NULL");
    ASSERT(ctx.tool_use_id == NULL, "Tool use ID should be NULL");
    ASSERT(ctx.tool_use_name == NULL, "Tool use name should be NULL");
    ASSERT(ctx.message_start_data == NULL, "Message start data should be NULL");
    ASSERT(ctx.stop_reason == NULL, "Stop reason should be NULL");
    
    // Test cleanup
    uds_streaming_context_free(&ctx);
    
    // Verify cleanup (all pointers should be NULL after free)
    ASSERT(ctx.accumulated_text == NULL, "Accumulated text should be NULL after free");
    ASSERT(ctx.tool_input_json == NULL, "Tool input should be NULL after free");
    ASSERT(ctx.content_block_type == NULL, "Content block type should be NULL after free");
    ASSERT(ctx.tool_use_id == NULL, "Tool use ID should be NULL after free");
    ASSERT(ctx.tool_use_name == NULL, "Tool use name should be NULL after free");
    ASSERT(ctx.message_start_data == NULL, "Message start data should be NULL after free");
    ASSERT(ctx.stop_reason == NULL, "Stop reason should be NULL after free");
    
    printf("  ✓ Streaming context test passed\n");
}

static void test_invalid_operations(void) {
    printf("Testing invalid operations...\n");
    
    // Test with invalid file descriptors
    int result;
    
    // Test uds_write_output with invalid fd
    result = uds_write_output(-1, "test", 4);
    ASSERT(result == -1, "Should fail with invalid fd");
    
    // Test uds_read_input with invalid fd
    char buffer[256];
    result = uds_read_input(-1, buffer, sizeof(buffer));
    ASSERT(result == -1, "Should fail with invalid fd");
    
    // Test uds_has_data with invalid fd
    result = uds_has_data(-1);
    ASSERT(result == 0, "Should return 0 for invalid fd");
    
    // Test uds_write_json with invalid fd
    cJSON *json = cJSON_CreateObject();
    result = uds_write_json(-1, json);
    ASSERT(result == -1, "Should fail with invalid fd");
    cJSON_Delete(json);
    
    // Test uds_write_json with NULL json
    result = uds_write_json(123, NULL);
    ASSERT(result == -1, "Should fail with NULL json");
    
    // Test uds_send_error with invalid fd
    uds_send_error(-1, "test error"); // Should not crash
    
    // Test uds_send_error with NULL message
    uds_send_error(123, NULL); // Should not crash
    
    printf("  ✓ Invalid operations test passed\n");
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
    printf("Running UDS Socket unit tests...\n");
    printf("================================\n");
    
    // Initialize logger
    log_init();
    
    // Seed random for temp socket paths
    srand((unsigned int)time(NULL));
    
    // Run tests
    test_socket_creation();
    test_socket_cleanup();
    test_accept_connection();
    test_socket_io();
    test_json_operations();
    test_streaming_context();
    test_invalid_operations();
    
    printf("\n================================\n");
    printf("All UDS Socket tests passed! ✓\n");
    
    return 0;
}
