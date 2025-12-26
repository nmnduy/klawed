/*
 * zmq_client.c - Simple ZMQ client for Klawed
 *
 * Simplified client matching the simplified zmq_socket implementation.
 * - No heartbeat mechanism
 * - Basic send/receive with simple timeouts
 * - Handles TEXT, TOOL, TOOL_RESULT, ERROR message types
 *
 * Compile on macOS:
 *   gcc -o zmq_client zmq_client.c -lzmq -lcjson -I.. -I/opt/homebrew/include \
 *   -L/opt/homebrew/lib $(pkg-config --cflags libzmq 2>/dev/null) -Wall -Wextra
 *
 * Compile on Linux:
 *   gcc -o zmq_client zmq_client.c -lzmq -lcjson -I.. $(pkg-config --cflags libzmq) \
 *   $(pkg-config --libs libzmq) -Wall -Wextra
 *
 * Run: ./zmq_client tcp://127.0.0.1:5555
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <zmq.h>
#include <cjson/cJSON.h>

#define BUFFER_SIZE 65536
#define DEFAULT_TIMEOUT_MS 120000 // 2 minutes

// Logging macros
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)

// Connection state
typedef struct {
    void *context;
    void *socket;
    char *endpoint;
    int is_connected;
} ConnectionState;

// Function prototypes
void print_usage(const char *program_name);
int initialize_connection(ConnectionState *conn, const char *endpoint);
void cleanup_connection(ConnectionState *conn);
int send_message(ConnectionState *conn, const char *message);
int receive_message(ConnectionState *conn, char *buffer, size_t buffer_size, int timeout_ms);
void process_message(const char *response);
void send_text_message(ConnectionState *conn, const char *text);

void print_usage(const char *program_name) {
    printf("Usage: %s <endpoint> [timeout_ms]\n", program_name);
    printf("Example: %s tcp://127.0.0.1:5555\n", program_name);
    printf("\nAvailable endpoints:\n");
    printf("  tcp://127.0.0.1:5555  - TCP socket on localhost port 5555\n");
    printf("  ipc:///tmp/klawed.sock - IPC socket file\n");
    printf("\nMessage types displayed:\n");
    printf("  TEXT          - AI text responses\n");
    printf("  TOOL          - Tool execution requests\n");
    printf("  TOOL_RESULT   - Tool execution results\n");
    printf("  ERROR         - Error messages\n");
    printf("\nCommands:\n");
    printf("  /help  - Show this help\n");
    printf("  /quit  - Exit the program\n");
}

int initialize_connection(ConnectionState *conn, const char *endpoint) {
    if (!conn || !endpoint) {
        LOG_ERROR("Invalid parameters");
        return 0;
    }
    
    // Clean up any existing connection
    cleanup_connection(conn);
    
    // Initialize ZMQ context
    LOG_DEBUG("Creating ZMQ context");
    conn->context = zmq_ctx_new();
    if (!conn->context) {
        LOG_ERROR("Error creating ZMQ context: %s", zmq_strerror(errno));
        return 0;
    }
    
    // Create PAIR socket (peer-to-peer communication)
    LOG_DEBUG("Creating ZMQ PAIR socket");
    conn->socket = zmq_socket(conn->context, ZMQ_PAIR);
    if (!conn->socket) {
        LOG_ERROR("Error creating ZMQ socket: %s", zmq_strerror(errno));
        zmq_ctx_term(conn->context);
        conn->context = NULL;
        return 0;
    }
    
    // Set linger option for clean shutdown
    int linger = 1000; // 1 second
    zmq_setsockopt(conn->socket, ZMQ_LINGER, &linger, sizeof(linger));
    LOG_DEBUG("Set ZMQ_LINGER to %d ms", linger);
    
    // Connect to endpoint
    LOG_INFO("Connecting to %s...", endpoint);
    int rc = zmq_connect(conn->socket, endpoint);
    if (rc != 0) {
        LOG_ERROR("Error connecting to %s: %s", endpoint, zmq_strerror(errno));
        zmq_close(conn->socket);
        zmq_ctx_term(conn->context);
        conn->socket = NULL;
        conn->context = NULL;
        return 0;
    }
    
    conn->endpoint = strdup(endpoint);
    conn->is_connected = 1;
    
    LOG_INFO("Connected to %s", endpoint);
    return 1;
}

void cleanup_connection(ConnectionState *conn) {
    if (conn->socket) {
        zmq_close(conn->socket);
        conn->socket = NULL;
    }
    if (conn->context) {
        zmq_ctx_term(conn->context);
        conn->context = NULL;
    }
    if (conn->endpoint) {
        free(conn->endpoint);
        conn->endpoint = NULL;
    }
    conn->is_connected = 0;
}

int send_message(ConnectionState *conn, const char *message) {
    if (!conn || !conn->is_connected || !conn->socket || !message) {
        LOG_ERROR("Cannot send: connection not ready");
        return -1;
    }
    
    int rc = zmq_send(conn->socket, message, strlen(message), 0);
    if (rc < 0) {
        LOG_ERROR("Error sending message: %s", zmq_strerror(errno));
        return -1;
    }
    
    LOG_DEBUG("Sent %d bytes", rc);
    return rc;
}

int receive_message(ConnectionState *conn, char *buffer, size_t buffer_size, int timeout_ms) {
    if (!conn || !conn->is_connected || !conn->socket || !buffer) {
        LOG_ERROR("Cannot receive: connection not ready");
        return -1;
    }
    
    // Set receive timeout
    zmq_setsockopt(conn->socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    
    int rc = zmq_recv(conn->socket, buffer, buffer_size - 1, 0);
    if (rc < 0) {
        if (errno == EAGAIN) {
            LOG_DEBUG("Receive timeout after %d ms", timeout_ms);
        } else {
            LOG_ERROR("Error receiving message: %s", zmq_strerror(errno));
        }
        return -1;
    }
    
    buffer[rc] = '\0';
    LOG_DEBUG("Received %d bytes", rc);
    return rc;
}

void process_message(const char *response) {
    if (!response) {
        LOG_WARN("Received NULL message");
        return;
    }
    
    cJSON *json = cJSON_Parse(response);
    if (!json) {
        LOG_WARN("Failed to parse JSON response");
        printf("Response (raw): %s\n", response);
        return;
    }
    
    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
    if (!message_type || !cJSON_IsString(message_type)) {
        LOG_ERROR("Invalid response format (missing messageType)");
        printf("Invalid response format: %s\n", response);
        cJSON_Delete(json);
        return;
    }
    
    const char *msg_type = message_type->valuestring;
    
    if (strcmp(msg_type, "TEXT") == 0) {
        cJSON *content = cJSON_GetObjectItem(json, "content");
        if (content && cJSON_IsString(content)) {
            printf("\n=== AI Response ===\n%s\n=== End of AI Response ===\n", 
                   content->valuestring);
        } else {
            printf("TEXT message missing content\n");
        }
    }
    else if (strcmp(msg_type, "TOOL") == 0) {
        cJSON *tool_name = cJSON_GetObjectItem(json, "toolName");
        cJSON *tool_id = cJSON_GetObjectItem(json, "toolId");
        cJSON *tool_params = cJSON_GetObjectItem(json, "toolParameters");
        
        printf("\n=== TOOL Execution Request ===\n");
        if (tool_name && cJSON_IsString(tool_name)) {
            printf("Tool: %s\n", tool_name->valuestring);
        }
        if (tool_id && cJSON_IsString(tool_id)) {
            printf("ID: %s\n", tool_id->valuestring);
        }
        if (tool_params) {
            char *params_str = cJSON_Print(tool_params);
            printf("Parameters: %s\n", params_str);
            free(params_str);
        }
        printf("=== Tool will be executed ===\n");
    }
    else if (strcmp(msg_type, "TOOL_RESULT") == 0) {
        cJSON *tool_name = cJSON_GetObjectItem(json, "toolName");
        cJSON *tool_id = cJSON_GetObjectItem(json, "toolId");
        cJSON *tool_output = cJSON_GetObjectItem(json, "toolOutput");
        cJSON *is_error = cJSON_GetObjectItem(json, "isError");
        
        printf("\n=== TOOL Execution Result ===\n");
        if (tool_name && cJSON_IsString(tool_name)) {
            printf("Tool: %s\n", tool_name->valuestring);
        }
        if (tool_id && cJSON_IsString(tool_id)) {
            printf("ID: %s\n", tool_id->valuestring);
        }
        if (is_error && cJSON_IsBool(is_error)) {
            printf("Status: %s\n", is_error->valueint ? "ERROR" : "SUCCESS");
        }
        if (tool_output) {
            char *output_str = cJSON_Print(tool_output);
            printf("Output: %s\n", output_str);
            free(output_str);
        }
        printf("=== End of Tool Result ===\n");
    }
    else if (strcmp(msg_type, "ERROR") == 0) {
        cJSON *content = cJSON_GetObjectItem(json, "content");
        if (content && cJSON_IsString(content)) {
            printf("\n=== ERROR ===\n%s\n=== End of Error ===\n", 
                   content->valuestring);
        } else {
            printf("ERROR message received\n");
        }
    }
    else {
        printf("Unknown message type: %s\n", msg_type);
        char *pretty = cJSON_Print(json);
        printf("Full message: %s\n", pretty);
        free(pretty);
    }
    
    cJSON_Delete(json);
}

void send_text_message(ConnectionState *conn, const char *text) {
    if (!conn || !text) {
        LOG_ERROR("Invalid parameters");
        return;
    }
    
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "messageType", "TEXT");
    cJSON_AddStringToObject(message, "content", text);
    
    char *json_str = cJSON_PrintUnformatted(message);
    LOG_DEBUG("Message JSON: %s", json_str);
    
    // Send the message
    int rc = send_message(conn, json_str);
    if (rc < 0) {
        LOG_ERROR("Failed to send message");
        free(json_str);
        cJSON_Delete(message);
        return;
    }
    
    // Receive and process the response
    char response[BUFFER_SIZE];
    rc = receive_message(conn, response, sizeof(response), DEFAULT_TIMEOUT_MS);
    
    if (rc >= 0) {
        process_message(response);
    } else {
        LOG_ERROR("Failed to receive response");
    }
    
    free(json_str);
    cJSON_Delete(message);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *endpoint = argv[1];
    int timeout_ms = DEFAULT_TIMEOUT_MS;
    
    if (argc >= 3) {
        timeout_ms = atoi(argv[2]);
        if (timeout_ms <= 0) {
            timeout_ms = DEFAULT_TIMEOUT_MS;
        }
    }
    
    LOG_INFO("Using endpoint: %s (timeout: %d ms)", endpoint, timeout_ms);
    
    ConnectionState conn = {0};
    
    // Initialize connection
    if (!initialize_connection(&conn, endpoint)) {
        LOG_ERROR("Failed to connect to %s", endpoint);
        printf("\nFailed to connect to %s\n", endpoint);
        printf("Make sure Klawed is running with ZMQ enabled and listening on this endpoint.\n");
        printf("Check: KLAWED_ZMQ_ENDPOINT=%s\n", endpoint);
        return 1;
    }
    
    printf("\nConnected to %s\n", endpoint);
    printf("Type your messages (or /help for commands)\n");
    printf("-----------------------------------------\n");
    
    // Interactive loop
    char input[BUFFER_SIZE];
    while (1) {
        printf("\n> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break; // EOF or error
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = '\0';
        
        // Skip empty input
        if (strlen(input) == 0) {
            continue;
        }
        
        // Check for commands
        if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
            printf("Goodbye!\n");
            break;
        } else if (strcmp(input, "/help") == 0) {
            print_usage(argv[0]);
        } else if (input[0] == '/') {
            printf("Unknown command: %s\n", input);
            printf("Type /help for available commands\n");
        } else {
            // Regular text message
            send_text_message(&conn, input);
        }
    }
    
    // Cleanup
    cleanup_connection(&conn);
    
    LOG_INFO("ZMQ Client exiting");
    return 0;
}
