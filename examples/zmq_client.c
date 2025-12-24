/*
 * zmq_client.c - Robust Interactive ZMQ client for Klawed with ping-pong and reconnection
 * 
 * Features:
 * - Heartbeat ping-pong mechanism to monitor connection health
 * - Automatic reconnection on failure with exponential backoff
 * - Connection testing before sending messages
 * - Configurable retry limits and timeouts
 * 
 * Compile: gcc -o zmq_client zmq_client.c -lzmq -lcjson
 * Run: ./zmq_client tcp://127.0.0.1:5555
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <zmq.h>
#include <cjson/cJSON.h>

#define BUFFER_SIZE 4096
#define MAX_RETRIES 3
#define INITIAL_RETRY_DELAY_MS 1000
#define MAX_RETRY_DELAY_MS 10000
#define HEARTBEAT_INTERVAL_MS 30000  // Send heartbeat every 30 seconds
#define CONNECTION_TIMEOUT_MS 5000   // 5 second timeout for operations
#define PING_TIMEOUT_MS 2000         // 2 second timeout for ping responses

// Configuration structure
typedef struct {
    const char *endpoint;
    int max_retries;
    int initial_retry_delay_ms;
    int max_retry_delay_ms;
    int heartbeat_interval_ms;
    int connection_timeout_ms;
    int ping_timeout_ms;
} ClientConfig;

// Connection state structure
typedef struct {
    void *context;
    void *socket;
    const char *endpoint;
    int is_connected;
    time_t last_heartbeat_sent;
    time_t last_heartbeat_received;
    int heartbeat_failures;
} ConnectionState;

// Function prototypes
void print_usage(const char *program_name);
int initialize_connection(ConnectionState *conn, const ClientConfig *config);
void cleanup_connection(ConnectionState *conn);
int send_heartbeat_ping(ConnectionState *conn, const ClientConfig *config);
int test_connection(ConnectionState *conn, const ClientConfig *config);
int reconnect(ConnectionState *conn, const ClientConfig *config);
int send_message_with_retry(ConnectionState *conn, const ClientConfig *config, 
                           const char *message_json, char *response_buffer, size_t buffer_size);
void send_text_message_robust(ConnectionState *conn, const ClientConfig *config, const char *text);
void handle_connection_loss(ConnectionState *conn, const ClientConfig *config);

void print_usage(const char *program_name) {
    printf("Usage: %s <endpoint> [options]\n", program_name);
    printf("Example: %s tcp://127.0.0.1:5555\n", program_name);
    printf("\nAvailable endpoints:\n");
    printf("  tcp://127.0.0.1:5555  - TCP socket on localhost port 5555\n");
    printf("  ipc:///tmp/klawed.sock - IPC socket file\n");
    printf("\nCommands in interactive mode:\n");
    printf("  /help     - Show this help\n");
    printf("  /quit     - Exit the program\n");
    printf("  /clear    - Clear screen\n");
    printf("  /ping     - Send a test ping to check connection\n");
    printf("  /status   - Show connection status\n");
    printf("  <text>    - Send text message to Klawed\n");
}

int initialize_connection(ConnectionState *conn, const ClientConfig *config) {
    if (!conn || !config) return 0;
    
    // Clean up any existing connection
    cleanup_connection(conn);
    
    // Initialize ZMQ context
    conn->context = zmq_ctx_new();
    if (!conn->context) {
        fprintf(stderr, "Error creating ZMQ context\n");
        return 0;
    }
    
    // Create PAIR socket (peer-to-peer communication)
    conn->socket = zmq_socket(conn->context, ZMQ_PAIR);
    if (!conn->socket) {
        fprintf(stderr, "Error creating ZMQ socket: %s\n", zmq_strerror(errno));
        zmq_ctx_term(conn->context);
        conn->context = NULL;
        return 0;
    }
    
    // Set timeout for receiving
    int timeout = config->connection_timeout_ms;
    zmq_setsockopt(conn->socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Connect to endpoint
    printf("Connecting to %s...\n", config->endpoint);
    int rc = zmq_connect(conn->socket, config->endpoint);
    if (rc != 0) {
        fprintf(stderr, "Error connecting to %s: %s\n", config->endpoint, zmq_strerror(errno));
        zmq_close(conn->socket);
        zmq_ctx_term(conn->context);
        conn->socket = NULL;
        conn->context = NULL;
        return 0;
    }
    
    conn->endpoint = config->endpoint;
    conn->is_connected = 1;
    conn->last_heartbeat_sent = 0;
    conn->last_heartbeat_received = 0;
    conn->heartbeat_failures = 0;
    
    // Test connection with a ping
    if (test_connection(conn, config)) {
        printf("Connected to %s\n", config->endpoint);
        return 1;
    } else {
        fprintf(stderr, "Connection test failed\n");
        cleanup_connection(conn);
        return 0;
    }
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
    conn->is_connected = 0;
    conn->heartbeat_failures = 0;
}

int send_heartbeat_ping(ConnectionState *conn, const ClientConfig *config) {
    if (!conn->is_connected || !conn->socket) return 0;
    
    cJSON *ping = cJSON_CreateObject();
    cJSON_AddStringToObject(ping, "messageType", "HEARTBEAT_PING");
    cJSON_AddNumberToObject(ping, "timestamp", (double)time(NULL));
    
    char *ping_json = cJSON_PrintUnformatted(ping);
    
    // Save original timeout
    int original_timeout;
    size_t timeout_size = sizeof(original_timeout);
    zmq_getsockopt(conn->socket, ZMQ_RCVTIMEO, &original_timeout, &timeout_size);
    
    // Set ping timeout
    int ping_timeout = config->ping_timeout_ms;
    zmq_setsockopt(conn->socket, ZMQ_RCVTIMEO, &ping_timeout, sizeof(ping_timeout));
    
    int success = 0;
    int rc = zmq_send(conn->socket, ping_json, strlen(ping_json), 0);
    if (rc >= 0) {
        conn->last_heartbeat_sent = time(NULL);
        
        char buffer[BUFFER_SIZE];
        rc = zmq_recv(conn->socket, buffer, BUFFER_SIZE - 1, 0);
        if (rc >= 0) {
            buffer[rc] = '\0';
            cJSON *response = cJSON_Parse(buffer);
            if (response) {
                cJSON *msg_type = cJSON_GetObjectItem(response, "messageType");
                if (msg_type && cJSON_IsString(msg_type) && 
                    strcmp(msg_type->valuestring, "HEARTBEAT_PONG") == 0) {
                    conn->last_heartbeat_received = time(NULL);
                    conn->heartbeat_failures = 0;
                    success = 1;
                }
                cJSON_Delete(response);
            }
        }
    }
    
    // Restore original timeout
    zmq_setsockopt(conn->socket, ZMQ_RCVTIMEO, &original_timeout, sizeof(original_timeout));
    
    free(ping_json);
    cJSON_Delete(ping);
    
    return success;
}

int test_connection(ConnectionState *conn, const ClientConfig *config) {
    return send_heartbeat_ping(conn, config);
}

int reconnect(ConnectionState *conn, const ClientConfig *config) {
    printf("Attempting to reconnect to %s...\n", config->endpoint);
    
    int delay_ms = config->initial_retry_delay_ms;
    for (int attempt = 1; attempt <= config->max_retries; attempt++) {
        if (initialize_connection(conn, config)) {
            printf("Reconnected successfully on attempt %d\n", attempt);
            return 1;
        }
        
        if (attempt < config->max_retries) {
            printf("Reconnection attempt %d failed. Retrying in %dms...\n", attempt, delay_ms);
            usleep(delay_ms * 1000);
            
            // Exponential backoff with cap
            delay_ms *= 2;
            if (delay_ms > config->max_retry_delay_ms) {
                delay_ms = config->max_retry_delay_ms;
            }
        }
    }
    
    fprintf(stderr, "Failed to reconnect after %d attempts\n", config->max_retries);
    return 0;
}

int send_message_with_retry(ConnectionState *conn, const ClientConfig *config, 
                           const char *message_json, char *response_buffer, size_t buffer_size) {
    if (!conn->is_connected) {
        if (!reconnect(conn, config)) {
            return -1;
        }
    }
    
    // Check if we need to send a heartbeat
    time_t now = time(NULL);
    if (now - conn->last_heartbeat_sent > config->heartbeat_interval_ms / 1000) {
        if (!send_heartbeat_ping(conn, config)) {
            fprintf(stderr, "Heartbeat failed, attempting to reconnect...\n");
            if (!reconnect(conn, config)) {
                return -1;
            }
        }
    }
    
    int delay_ms = config->initial_retry_delay_ms;
    for (int attempt = 1; attempt <= config->max_retries; attempt++) {
        // Send message
        int rc = zmq_send(conn->socket, message_json, strlen(message_json), 0);
        if (rc < 0) {
            fprintf(stderr, "Error sending message (attempt %d): %s\n", attempt, zmq_strerror(errno));
            goto retry;
        }
        
        // Wait for response
        rc = zmq_recv(conn->socket, response_buffer, buffer_size - 1, 0);
        if (rc < 0) {
            fprintf(stderr, "Error receiving response (attempt %d): %s\n", attempt, zmq_strerror(errno));
            goto retry;
        }
        
        response_buffer[rc] = '\0';
        return rc;
        
    retry:
        if (attempt < config->max_retries) {
            printf("Retrying in %dms...\n", delay_ms);
            usleep(delay_ms * 1000);
            
            // Try to reconnect before next attempt
            if (!reconnect(conn, config)) {
                // If reconnection fails, continue with exponential backoff
                delay_ms *= 2;
                if (delay_ms > config->max_retry_delay_ms) {
                    delay_ms = config->max_retry_delay_ms;
                }
                continue;
            }
        }
    }
    
    fprintf(stderr, "Failed to send message after %d attempts\n", config->max_retries);
    return -1;
}

void send_text_message_robust(ConnectionState *conn, const ClientConfig *config, const char *text) {
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "messageType", "TEXT");
    cJSON_AddStringToObject(message, "content", text);
    
    char *json_str = cJSON_PrintUnformatted(message);
    
    char response[BUFFER_SIZE];
    int rc = send_message_with_retry(conn, config, json_str, response, sizeof(response));
    
    if (rc >= 0) {
        printf("✓ Message sent and response received\n");
        
        // Parse and pretty-print JSON response
        cJSON *json = cJSON_Parse(response);
        if (json) {
            char *pretty = cJSON_Print(json);
            printf("Response:\n%s\n", pretty);
            free(pretty);
            cJSON_Delete(json);
        } else {
            printf("Response (raw): %s\n", response);
        }
    } else {
        printf("✗ Failed to send message\n");
    }
    
    free(json_str);
    cJSON_Delete(message);
}

void handle_connection_loss(ConnectionState *conn, const ClientConfig *config) {
    printf("\nConnection lost! Attempting to recover...\n");
    conn->is_connected = 0;
    
    if (reconnect(conn, config)) {
        printf("Connection restored!\n");
    } else {
        printf("Failed to restore connection. You can try manually with /ping\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    // Default configuration
    ClientConfig config = {
        .endpoint = argv[1],
        .max_retries = MAX_RETRIES,
        .initial_retry_delay_ms = INITIAL_RETRY_DELAY_MS,
        .max_retry_delay_ms = MAX_RETRY_DELAY_MS,
        .heartbeat_interval_ms = HEARTBEAT_INTERVAL_MS,
        .connection_timeout_ms = CONNECTION_TIMEOUT_MS,
        .ping_timeout_ms = PING_TIMEOUT_MS
    };
    
    ConnectionState conn = {0};
    
    // Initialize connection
    if (!initialize_connection(&conn, &config)) {
        fprintf(stderr, "Failed to establish initial connection\n");
        return 1;
    }
    
    printf("Connected to %s\n", config.endpoint);
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
        } else if (strcmp(input, "/clear") == 0) {
            printf("\033[2J\033[H"); // Clear screen
        } else if (strcmp(input, "/ping") == 0) {
            printf("Sending ping...\n");
            if (send_heartbeat_ping(&conn, &config)) {
                printf("✓ Ping successful - connection is healthy\n");
            } else {
                printf("✗ Ping failed\n");
                handle_connection_loss(&conn, &config);
            }
        } else if (strcmp(input, "/status") == 0) {
            printf("Connection status:\n");
            printf("  Endpoint: %s\n", conn.endpoint);
            printf("  Connected: %s\n", conn.is_connected ? "Yes" : "No");
            printf("  Last heartbeat sent: %ld seconds ago\n", 
                   time(NULL) - conn.last_heartbeat_sent);
            printf("  Last heartbeat received: %ld seconds ago\n", 
                   time(NULL) - conn.last_heartbeat_received);
            printf("  Heartbeat failures: %d\n", conn.heartbeat_failures);
        } else if (input[0] == '/') {
            printf("Unknown command: %s\n", input);
            printf("Type /help for available commands\n");
        } else {
            // Regular text message
            send_text_message_robust(&conn, &config, input);
        }
    }
    
    // Cleanup
    cleanup_connection(&conn);
    
    return 0;
}