/*
 * Unit Tests for Unix Domain Socket (UDS) Functionality
 *
 * Tests the UDS socket communication including:
 * - Server initialization and cleanup
 * - Client connection handling
 * - Message framing (length prefix + payload)
 * - Send and receive operations
 * - Timeout handling
 * - Error conditions
 *
 * Compilation: make test-uds-socket
 * Usage: ./test_uds_socket
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <bsd/string.h>

// Minimal cJSON type for test purposes (we don't use it directly here)
#include <cjson/cJSON.h>

#include "../src/uds_socket.h"

// Forward declarations for stub functions
struct ConversationState;
struct ApiResponse;
struct InternalContent;
struct ToolCall;

// Stub function prototypes - these satisfy linker requirements
// for uds_socket.c which includes klawed_internal.h
void add_user_message(struct ConversationState *state, const char *msg);
void add_assistant_message_openai(struct ConversationState *state, cJSON *msg);
int add_tool_results(struct ConversationState *state, struct InternalContent *results, int count);
struct ApiResponse* call_api_with_retries(struct ConversationState *state);
void api_response_free(struct ApiResponse *resp);
int is_tool_allowed(const char *name, struct ConversationState *state);
cJSON* execute_tool(const char *name, cJSON *params, struct ConversationState *state);

// Stub implementations - these are only needed because uds_socket.c includes
// klawed_internal.h which pulls in references to conversation state functions.
// We don't call daemon mode in tests, so these stubs are fine.
void add_user_message(struct ConversationState *state __attribute__((unused)),
                      const char *msg __attribute__((unused))) {}
void add_assistant_message_openai(struct ConversationState *state __attribute__((unused)),
                                  cJSON *msg __attribute__((unused))) {}
int add_tool_results(struct ConversationState *state __attribute__((unused)),
                     struct InternalContent *results __attribute__((unused)),
                     int count __attribute__((unused))) { return 0; }
struct ApiResponse* call_api_with_retries(struct ConversationState *state __attribute__((unused))) { return NULL; }
void api_response_free(struct ApiResponse *resp __attribute__((unused))) {}
int is_tool_allowed(const char *name __attribute__((unused)),
                    struct ConversationState *state __attribute__((unused))) { return 0; }
cJSON* execute_tool(const char *name __attribute__((unused)),
                    cJSON *params __attribute__((unused)),
                    struct ConversationState *state __attribute__((unused))) { return NULL; }

// Test framework colors
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Test socket path template
#define TEST_SOCKET_PATH "/tmp/klawed_uds_test_%d.sock"
static char test_socket_path[256];
static int test_id = 0;

// Test utilities
static void setup_test_socket_path(void) {
    snprintf(test_socket_path, sizeof(test_socket_path), TEST_SOCKET_PATH, test_id++);
    unlink(test_socket_path);  // Clean up any existing socket
}

static void cleanup_test_socket(void) {
    unlink(test_socket_path);
}

// Test assertion macros
#define ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf(COLOR_GREEN "✓ %s\n" COLOR_RESET, message); \
    } else { \
        tests_failed++; \
        printf(COLOR_RED "✗ %s\n" COLOR_RESET, message); \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr, message) do { \
    tests_run++; \
    if ((ptr) != NULL) { \
        tests_passed++; \
        printf(COLOR_GREEN "✓ %s\n" COLOR_RESET, message); \
    } else { \
        tests_failed++; \
        printf(COLOR_RED "✗ %s (got NULL)\n" COLOR_RESET, message); \
    } \
} while(0)

#define ASSERT_NULL(ptr, message) do { \
    tests_run++; \
    if ((ptr) == NULL) { \
        tests_passed++; \
        printf(COLOR_GREEN "✓ %s\n" COLOR_RESET, message); \
    } else { \
        tests_failed++; \
        printf(COLOR_RED "✗ %s (expected NULL)\n" COLOR_RESET, message); \
    } \
} while(0)

#define ASSERT_INT_EQUAL(actual, expected, message) do { \
    tests_run++; \
    if ((actual) == (expected)) { \
        tests_passed++; \
        printf(COLOR_GREEN "✓ %s\n" COLOR_RESET, message); \
    } else { \
        tests_failed++; \
        printf(COLOR_RED "✗ %s (expected %d, got %d)\n" COLOR_RESET, message, (expected), (actual)); \
    } \
} while(0)

#define ASSERT_STRING_EQUAL(actual, expected, message) do { \
    tests_run++; \
    if (strcmp((actual), (expected)) == 0) { \
        tests_passed++; \
        printf(COLOR_GREEN "✓ %s\n" COLOR_RESET, message); \
    } else { \
        tests_failed++; \
        printf(COLOR_RED "✗ %s (expected '%s', got '%s')\n" COLOR_RESET, message, (expected), (actual)); \
    } \
} while(0)

// ============================================================================
// Test: UDS Socket Availability
// ============================================================================
static void test_uds_available(void) {
    printf(COLOR_CYAN "\nTest: UDS Socket Availability\n" COLOR_RESET);

    bool available = uds_socket_available();
    ASSERT(available == true, "UDS socket should be available");
}

// ============================================================================
// Test: UDS Initialization with valid path
// ============================================================================
static void test_uds_init_valid(void) {
    printf(COLOR_CYAN "\nTest: UDS Initialization (valid path)\n" COLOR_RESET);

    setup_test_socket_path();

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (ctx) {
        ASSERT(ctx->enabled == true, "Context should be enabled");
        ASSERT(ctx->server_fd >= 0, "Server FD should be valid");
        ASSERT(ctx->client_fd == -1, "Client FD should be -1 (no client)");
        ASSERT(ctx->client_connected == false, "Client should not be connected");
        ASSERT_STRING_EQUAL(ctx->socket_path, test_socket_path, "Socket path should match");

        uds_socket_cleanup(ctx);
    }

    cleanup_test_socket();
}

// ============================================================================
// Test: UDS Initialization with NULL path
// ============================================================================
static void test_uds_init_null_path(void) {
    printf(COLOR_CYAN "\nTest: UDS Initialization (NULL path)\n" COLOR_RESET);

    UDSContext *ctx = uds_socket_init(NULL);
    ASSERT_NULL(ctx, "UDS context should be NULL for NULL path");
}

// ============================================================================
// Test: UDS Initialization with empty path
// ============================================================================
static void test_uds_init_empty_path(void) {
    printf(COLOR_CYAN "\nTest: UDS Initialization (empty path)\n" COLOR_RESET);

    UDSContext *ctx = uds_socket_init("");
    ASSERT_NULL(ctx, "UDS context should be NULL for empty path");
}

// ============================================================================
// Test: UDS Cleanup with NULL context
// ============================================================================
static void test_uds_cleanup_null(void) {
    printf(COLOR_CYAN "\nTest: UDS Cleanup (NULL context)\n" COLOR_RESET);

    // This should not crash
    uds_socket_cleanup(NULL);
    ASSERT(true, "Cleanup of NULL context should not crash");
}

// ============================================================================
// Test: UDS Get FD with no client
// ============================================================================
static void test_uds_get_fd_no_client(void) {
    printf(COLOR_CYAN "\nTest: UDS Get FD (no client)\n" COLOR_RESET);

    setup_test_socket_path();

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (ctx) {
        int fd = uds_socket_get_fd(ctx);
        ASSERT_INT_EQUAL(fd, -1, "FD should be -1 when no client connected");

        uds_socket_cleanup(ctx);
    }

    cleanup_test_socket();
}

// ============================================================================
// Test: UDS Is Connected (no client)
// ============================================================================
static void test_uds_is_connected_no_client(void) {
    printf(COLOR_CYAN "\nTest: UDS Is Connected (no client)\n" COLOR_RESET);

    setup_test_socket_path();

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (ctx) {
        bool connected = uds_socket_is_connected(ctx);
        ASSERT(connected == false, "Should not be connected without client");

        uds_socket_cleanup(ctx);
    }

    cleanup_test_socket();
}

// ============================================================================
// Test: UDS Is Connected (NULL context)
// ============================================================================
static void test_uds_is_connected_null(void) {
    printf(COLOR_CYAN "\nTest: UDS Is Connected (NULL context)\n" COLOR_RESET);

    bool connected = uds_socket_is_connected(NULL);
    ASSERT(connected == false, "Should return false for NULL context");
}

// ============================================================================
// Test: UDS Accept with timeout
// ============================================================================
static void test_uds_accept_timeout(void) {
    printf(COLOR_CYAN "\nTest: UDS Accept (timeout)\n" COLOR_RESET);

    setup_test_socket_path();

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (ctx) {
        // Accept with 1 second timeout - should timeout since no client
        int result = uds_socket_accept(ctx, 1);
        ASSERT_INT_EQUAL(result, UDS_ERROR_RECEIVE_TIMEOUT, "Accept should timeout");

        uds_socket_cleanup(ctx);
    }

    cleanup_test_socket();
}

// ============================================================================
// Test: UDS Accept with invalid context
// ============================================================================
static void test_uds_accept_invalid_context(void) {
    printf(COLOR_CYAN "\nTest: UDS Accept (invalid context)\n" COLOR_RESET);

    int result = uds_socket_accept(NULL, 1);
    ASSERT_INT_EQUAL(result, UDS_ERROR_INVALID_PARAM, "Accept should fail with invalid param");
}

// ============================================================================
// Test: UDS Send with no client
// ============================================================================
static void test_uds_send_no_client(void) {
    printf(COLOR_CYAN "\nTest: UDS Send (no client)\n" COLOR_RESET);

    setup_test_socket_path();

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (ctx) {
        const char *message = "{\"test\": \"hello\"}";
        int result = uds_socket_send(ctx, message, strlen(message));
        ASSERT_INT_EQUAL(result, UDS_ERROR_CONNECTION_CLOSED, "Send should fail with no client");

        uds_socket_cleanup(ctx);
    }

    cleanup_test_socket();
}

// ============================================================================
// Test: UDS Receive with no client
// ============================================================================
static void test_uds_receive_no_client(void) {
    printf(COLOR_CYAN "\nTest: UDS Receive (no client)\n" COLOR_RESET);

    setup_test_socket_path();

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (ctx) {
        char buffer[1024];
        int result = uds_socket_receive(ctx, buffer, sizeof(buffer), 1);
        ASSERT_INT_EQUAL(result, UDS_ERROR_CONNECTION_CLOSED, "Receive should fail with no client");

        uds_socket_cleanup(ctx);
    }

    cleanup_test_socket();
}

// ============================================================================
// Test: UDS Send with NULL parameters
// ============================================================================
static void test_uds_send_null_params(void) {
    printf(COLOR_CYAN "\nTest: UDS Send (NULL params)\n" COLOR_RESET);

    setup_test_socket_path();

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (ctx) {
        // NULL context
        int result = uds_socket_send(NULL, "test", 4);
        ASSERT_INT_EQUAL(result, UDS_ERROR_INVALID_PARAM, "Send should fail with NULL context");

        // NULL message
        result = uds_socket_send(ctx, NULL, 4);
        ASSERT_INT_EQUAL(result, UDS_ERROR_INVALID_PARAM, "Send should fail with NULL message");

        uds_socket_cleanup(ctx);
    }

    cleanup_test_socket();
}

// ============================================================================
// Test: UDS Receive with NULL parameters
// ============================================================================
static void test_uds_receive_null_params(void) {
    printf(COLOR_CYAN "\nTest: UDS Receive (NULL params)\n" COLOR_RESET);

    setup_test_socket_path();

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (ctx) {
        char buffer[1024];

        // NULL context
        int result = uds_socket_receive(NULL, buffer, sizeof(buffer), 1);
        ASSERT_INT_EQUAL(result, UDS_ERROR_INVALID_PARAM, "Receive should fail with NULL context");

        // NULL buffer
        result = uds_socket_receive(ctx, NULL, sizeof(buffer), 1);
        ASSERT_INT_EQUAL(result, UDS_ERROR_INVALID_PARAM, "Receive should fail with NULL buffer");

        // Zero buffer size
        result = uds_socket_receive(ctx, buffer, 0, 1);
        ASSERT_INT_EQUAL(result, UDS_ERROR_INVALID_PARAM, "Receive should fail with zero buffer size");

        uds_socket_cleanup(ctx);
    }

    cleanup_test_socket();
}

// ============================================================================
// Test: UDS Disconnect client (no client)
// ============================================================================
static void test_uds_disconnect_no_client(void) {
    printf(COLOR_CYAN "\nTest: UDS Disconnect (no client)\n" COLOR_RESET);

    setup_test_socket_path();

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (ctx) {
        // Should not crash
        uds_socket_disconnect_client(ctx);
        ASSERT(true, "Disconnect with no client should not crash");
        ASSERT(ctx->client_connected == false, "Client should remain disconnected");

        uds_socket_cleanup(ctx);
    }

    cleanup_test_socket();
}

// ============================================================================
// Test: UDS Disconnect NULL context
// ============================================================================
static void test_uds_disconnect_null(void) {
    printf(COLOR_CYAN "\nTest: UDS Disconnect (NULL context)\n" COLOR_RESET);

    // Should not crash
    uds_socket_disconnect_client(NULL);
    ASSERT(true, "Disconnect with NULL context should not crash");
}

// ============================================================================
// Test: Client connection and message exchange
// ============================================================================

// Helper: Client thread data
typedef struct {
    const char *socket_path;
    const char *message_to_send;
    char received_message[4096];
    int received_len;
    int error_code;
} ClientThreadData;

// Helper: Connect to UDS server and exchange messages
static void* client_thread(void *arg) {
    ClientThreadData *data = (ClientThreadData*)arg;
    data->error_code = 0;
    data->received_len = 0;

    // Give server time to start listening
    usleep(100000);  // 100ms

    // Create client socket
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0) {
        data->error_code = -1;
        return NULL;
    }

    // Connect to server
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strlcpy(addr.sun_path, data->socket_path, sizeof(addr.sun_path));

    if (connect(client_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(client_fd);
        data->error_code = -2;
        return NULL;
    }

    // Receive message from server (framed: 4-byte length + payload)
    uint32_t len_network;
    ssize_t n = read(client_fd, &len_network, sizeof(len_network));
    if (n != sizeof(len_network)) {
        close(client_fd);
        data->error_code = -3;
        return NULL;
    }

    uint32_t payload_len = ntohl(len_network);
    if (payload_len >= sizeof(data->received_message)) {
        close(client_fd);
        data->error_code = -4;
        return NULL;
    }

    n = read(client_fd, data->received_message, payload_len);
    if (n != (ssize_t)payload_len) {
        close(client_fd);
        data->error_code = -5;
        return NULL;
    }

    data->received_message[payload_len] = '\0';
    data->received_len = (int)payload_len;

    // Send response back (framed)
    if (data->message_to_send) {
        size_t send_len = strlen(data->message_to_send);
        len_network = htonl((uint32_t)send_len);

        if (write(client_fd, &len_network, sizeof(len_network)) != sizeof(len_network)) {
            close(client_fd);
            data->error_code = -6;
            return NULL;
        }

        if (write(client_fd, data->message_to_send, send_len) != (ssize_t)send_len) {
            close(client_fd);
            data->error_code = -7;
            return NULL;
        }
    }

    close(client_fd);
    return NULL;
}

static void test_uds_send_receive_exchange(void) {
    printf(COLOR_CYAN "\nTest: UDS Send/Receive Message Exchange\n" COLOR_RESET);

    setup_test_socket_path();

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (!ctx) {
        cleanup_test_socket();
        return;
    }

    // Setup client thread data
    ClientThreadData client_data = {
        .socket_path = test_socket_path,
        .message_to_send = "{\"response\": \"ack\"}",
        .received_message = {0},
        .received_len = 0,
        .error_code = 0
    };

    // Start client thread
    pthread_t client_tid;
    if (pthread_create(&client_tid, NULL, client_thread, &client_data) != 0) {
        printf(COLOR_RED "✗ Failed to create client thread\n" COLOR_RESET);
        tests_run++;
        tests_failed++;
        uds_socket_cleanup(ctx);
        cleanup_test_socket();
        return;
    }

    // Accept client connection
    int result = uds_socket_accept(ctx, 5);
    ASSERT_INT_EQUAL(result, UDS_ERROR_NONE, "Accept should succeed");

    if (result == UDS_ERROR_NONE) {
        ASSERT(uds_socket_is_connected(ctx), "Client should be connected");

        // Send message to client
        const char *server_msg = "{\"test\": \"hello from server\"}";
        char response_buf[4096];

        int response_len = uds_socket_send_receive(ctx, server_msg, strlen(server_msg),
                                                   response_buf, sizeof(response_buf));

        ASSERT(response_len > 0, "Should receive response from client");

        if (response_len > 0) {
            ASSERT_STRING_EQUAL(response_buf, client_data.message_to_send,
                               "Response should match client message");
        }
    }

    // Wait for client thread
    pthread_join(client_tid, NULL);

    ASSERT_INT_EQUAL(client_data.error_code, 0, "Client should complete without error");
    ASSERT(client_data.received_len > 0, "Client should receive message");

    if (client_data.received_len > 0) {
        ASSERT_STRING_EQUAL(client_data.received_message, "{\"test\": \"hello from server\"}",
                           "Client should receive server message");
    }

    uds_socket_cleanup(ctx);
    cleanup_test_socket();
}

// ============================================================================
// Test: Configuration from environment
// ============================================================================
static void test_uds_config_from_env(void) {
    printf(COLOR_CYAN "\nTest: UDS Configuration from Environment\n" COLOR_RESET);

    setup_test_socket_path();

    // Set environment variables
    setenv("KLAWED_UNIX_SOCKET_RETRIES", "10", 1);
    setenv("KLAWED_UNIX_SOCKET_TIMEOUT", "60", 1);

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (ctx) {
        ASSERT_INT_EQUAL(ctx->max_retries, 10, "Max retries should match env var");
        ASSERT_INT_EQUAL(ctx->timeout_sec, 60, "Timeout should match env var");

        uds_socket_cleanup(ctx);
    }

    // Clean up environment
    unsetenv("KLAWED_UNIX_SOCKET_RETRIES");
    unsetenv("KLAWED_UNIX_SOCKET_TIMEOUT");

    cleanup_test_socket();
}

// ============================================================================
// Test: Default configuration
// ============================================================================
static void test_uds_config_defaults(void) {
    printf(COLOR_CYAN "\nTest: UDS Default Configuration\n" COLOR_RESET);

    setup_test_socket_path();

    // Ensure env vars are not set
    unsetenv("KLAWED_UNIX_SOCKET_RETRIES");
    unsetenv("KLAWED_UNIX_SOCKET_TIMEOUT");

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (ctx) {
        ASSERT_INT_EQUAL(ctx->max_retries, UDS_DEFAULT_RETRIES, "Max retries should be default");
        ASSERT_INT_EQUAL(ctx->timeout_sec, UDS_DEFAULT_TIMEOUT_SEC, "Timeout should be default");

        uds_socket_cleanup(ctx);
    }

    cleanup_test_socket();
}

// ============================================================================
// Test: Multiple socket cleanup
// ============================================================================
static void test_uds_double_cleanup(void) {
    printf(COLOR_CYAN "\nTest: UDS Double Cleanup\n" COLOR_RESET);

    setup_test_socket_path();

    UDSContext *ctx = uds_socket_init(test_socket_path);
    ASSERT_NOT_NULL(ctx, "UDS context should be created");

    if (ctx) {
        uds_socket_cleanup(ctx);
        // Second cleanup should be safe (ctx is already freed, but we're testing
        // that cleanup with NULL works)
        uds_socket_cleanup(NULL);
        ASSERT(true, "Double cleanup should not crash");
    }

    cleanup_test_socket();
}

// ============================================================================
// Test: Send/receive with invalid context
// ============================================================================
static void test_uds_send_receive_invalid(void) {
    printf(COLOR_CYAN "\nTest: UDS Send/Receive with Invalid Context\n" COLOR_RESET);

    char response[1024];

    // NULL context
    int result = uds_socket_send_receive(NULL, "test", 4, response, sizeof(response));
    ASSERT_INT_EQUAL(result, UDS_ERROR_INVALID_PARAM, "Send/receive should fail with NULL context");

    // NULL message
    setup_test_socket_path();
    UDSContext *ctx = uds_socket_init(test_socket_path);
    if (ctx) {
        result = uds_socket_send_receive(ctx, NULL, 4, response, sizeof(response));
        ASSERT_INT_EQUAL(result, UDS_ERROR_INVALID_PARAM, "Send/receive should fail with NULL message");

        // NULL response buffer
        result = uds_socket_send_receive(ctx, "test", 4, NULL, sizeof(response));
        ASSERT_INT_EQUAL(result, UDS_ERROR_INVALID_PARAM, "Send/receive should fail with NULL response");

        uds_socket_cleanup(ctx);
    }
    cleanup_test_socket();
}

// ============================================================================
// Main test runner
// ============================================================================
int main(void) {
    printf(COLOR_YELLOW "\nRunning Unix Domain Socket (UDS) Tests\n" COLOR_RESET);
    printf("========================================\n");

    // Run all tests
    test_uds_available();
    test_uds_init_valid();
    test_uds_init_null_path();
    test_uds_init_empty_path();
    test_uds_cleanup_null();
    test_uds_get_fd_no_client();
    test_uds_is_connected_no_client();
    test_uds_is_connected_null();
    test_uds_accept_timeout();
    test_uds_accept_invalid_context();
    test_uds_send_no_client();
    test_uds_receive_no_client();
    test_uds_send_null_params();
    test_uds_receive_null_params();
    test_uds_disconnect_no_client();
    test_uds_disconnect_null();
    test_uds_send_receive_exchange();
    test_uds_config_from_env();
    test_uds_config_defaults();
    test_uds_double_cleanup();
    test_uds_send_receive_invalid();

    // Print summary
    printf(COLOR_YELLOW "\nTest Summary\n" COLOR_RESET);
    printf("=============\n");
    printf("Tests Run: %d\n", tests_run);
    printf(COLOR_GREEN "Tests Passed: %d\n" COLOR_RESET, tests_passed);

    if (tests_failed > 0) {
        printf(COLOR_RED "Tests Failed: %d\n" COLOR_RESET, tests_failed);
        return 1;
    } else {
        printf(COLOR_GREEN "All tests passed!\n" COLOR_RESET);
        return 0;
    }
}
