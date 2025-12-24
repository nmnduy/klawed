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

// Logging macros
#define LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt "\\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) printf("[WARN] " fmt "\\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\\n", ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) printf("[TRACE] %s:%d: " fmt "\\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define BUFFER_SIZE 4096
#define MAX_RETRIES 3
#define INITIAL_RETRY_DELAY_MS 1000
#define MAX_RETRY_DELAY_MS 10000
#define HEARTBEAT_INTERVAL_MS 30000  // Send heartbeat every 30 seconds
#define CONNECTION_TIMEOUT_MS 120000 // 2 minute (120 second) timeout for operations
#define PING_TIMEOUT_MS 2000         // 2 second timeout for ping responses
#define NONBLOCKING_TIMEOUT_MS 120000 // 2 minute (120 second) timeout for additional messages

// Configuration structure
typedef struct {
    const char *endpoint;
    int max_retries;
    int initial_retry_delay_ms;
    int max_retry_delay_ms;
    int heartbeat_interval_ms;
    int connection_timeout_ms;
    int ping_timeout_ms;
    int nonblocking_timeout_ms; // Timeout for additional messages after initial response
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
void log_connection_state(const ConnectionState *conn, const char *context);
static void log_timeout_retry_details(const ConnectionState *conn, const ClientConfig *config,
                                     int attempt, int max_attempts, int delay_ms, 
                                     const char *operation, int error_code, const char *error_msg);
static void log_heartbeat_details(const ConnectionState *conn, const ClientConfig *config,
                                 time_t current_time, const char *phase);

void print_usage(const char *program_name) {
    printf("Usage: %s <endpoint> [options]\n", program_name);
    printf("Example: %s tcp://127.0.0.1:5555\n", program_name);
    printf("\nAvailable endpoints:\n");
    printf("  tcp://127.0.0.1:5555  - TCP socket on localhost port 5555\n");
    printf("  ipc:///tmp/klawed.sock - IPC socket file\n");
    printf("\nFeatures:\n");
    printf("  • Handles TOOL and TOOL_RESULT messages for interactive sessions\n");
    printf("  • Processes multiple response messages\n");
    printf("  • Displays formatted output for all message types\n");
    printf("\nCommands in interactive mode:\n");
    printf("  /help     - Show this help\n");
    printf("  /quit     - Exit the program\n");
    printf("  /clear    - Clear screen\n");
    printf("  /ping     - Send a test ping to check connection\n");
    printf("  /status   - Show connection status\n");
    printf("  <text>    - Send text message to Klawed (supports tool calls)\n");
    printf("\nMessage types displayed:\n");
    printf("  TEXT          - AI text responses\n");
    printf("  TOOL          - Tool execution requests\n");
    printf("  TOOL_RESULT   - Tool execution results\n");
    printf("  ERROR         - Error messages\n");
    printf("  HEARTBEAT_PONG - Connection health responses\n");
}

int initialize_connection(ConnectionState *conn, const ClientConfig *config) {
    LOG_TRACE("initialize_connection called");
    if (!conn || !config) {
        LOG_ERROR("Invalid parameters: conn=%p, config=%p", conn, config);
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
    LOG_DEBUG("ZMQ context created successfully");
    
    // Create PAIR socket (peer-to-peer communication)
    LOG_DEBUG("Creating ZMQ PAIR socket");
    conn->socket = zmq_socket(conn->context, ZMQ_PAIR);
    if (!conn->socket) {
        LOG_ERROR("Error creating ZMQ socket: %s", zmq_strerror(errno));
        zmq_ctx_term(conn->context);
        conn->context = NULL;
        return 0;
    }
    LOG_DEBUG("ZMQ socket created successfully");
    
    // Set timeout for receiving
    int timeout = config->connection_timeout_ms;
    LOG_DEBUG("Setting receive timeout to %d ms", timeout);
    zmq_setsockopt(conn->socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Connect to endpoint with retry logic
    LOG_INFO("Connecting to %s...", config->endpoint);
    
    int delay_ms = config->initial_retry_delay_ms;
    int connected = 0;
    
    for (int attempt = 1; attempt <= config->max_retries; attempt++) {
        LOG_INFO("Connection attempt %d/%d", attempt, config->max_retries);
        
        int rc = zmq_connect(conn->socket, config->endpoint);
        if (rc == 0) {
            LOG_INFO("Connection established to %s on attempt %d", config->endpoint, attempt);
            connected = 1;
            break;
        }
        
        int err = errno;
        LOG_WARN("Error connecting to %s (attempt %d): %s (errno=%d)", 
                 config->endpoint, attempt, zmq_strerror(err), err);
        
        if (attempt < config->max_retries) {
            LOG_WARN("Connection attempt %d failed. Retrying in %dms...", attempt, delay_ms);
            LOG_DEBUG("Sleeping for %d ms before next attempt", delay_ms);
            usleep(delay_ms * 1000);
            
            // Exponential backoff with cap
            int old_delay = delay_ms;
            delay_ms *= 2;
            if (delay_ms > config->max_retry_delay_ms) {
                delay_ms = config->max_retry_delay_ms;
            }
            LOG_DEBUG("Updated retry delay: %d ms -> %d ms", old_delay, delay_ms);
        }
    }
    
    if (!connected) {
        LOG_ERROR("Failed to connect to %s after %d attempts", config->endpoint, config->max_retries);
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
    
    LOG_DEBUG("Connection state initialized: is_connected=%d, endpoint=%s", 
              conn->is_connected, conn->endpoint);
    log_connection_state(conn, "after initialization");
    
    // Test connection with a ping
    LOG_DEBUG("Testing connection with heartbeat ping");
    if (test_connection(conn, config)) {
        LOG_INFO("Connection test successful - connected to %s", config->endpoint);
        log_connection_state(conn, "after successful connection test");
        return 1;
    } else {
        LOG_ERROR("Connection test failed");
        log_connection_state(conn, "after failed connection test");
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
    LOG_TRACE("send_heartbeat_ping called");
    if (!conn->is_connected || !conn->socket) {
        LOG_ERROR("Cannot send heartbeat: is_connected=%d, socket=%p", 
                  conn->is_connected, conn->socket);
        return 0;
    }
    
    time_t current_time = time(NULL);
    log_heartbeat_details(conn, config, current_time, "start of heartbeat ping");
    
    LOG_DEBUG("Creating heartbeat ping message");
    cJSON *ping = cJSON_CreateObject();
    cJSON_AddStringToObject(ping, "messageType", "HEARTBEAT_PING");
    cJSON_AddNumberToObject(ping, "timestamp", (double)current_time);
    
    char *ping_json = cJSON_PrintUnformatted(ping);
    LOG_DEBUG("Heartbeat ping JSON: %s", ping_json);
    
    // Save original timeout
    int original_timeout;
    size_t timeout_size = sizeof(original_timeout);
    zmq_getsockopt(conn->socket, ZMQ_RCVTIMEO, &original_timeout, &timeout_size);
    LOG_DEBUG("Original receive timeout: %d ms", original_timeout);
    
    // Set ping timeout
    int ping_timeout = config->ping_timeout_ms;
    LOG_DEBUG("Setting ping timeout to %d ms", ping_timeout);
    zmq_setsockopt(conn->socket, ZMQ_RCVTIMEO, &ping_timeout, sizeof(ping_timeout));
    
    int success = 0;
    LOG_DEBUG("Sending heartbeat ping");
    int rc = zmq_send(conn->socket, ping_json, strlen(ping_json), 0);
    if (rc >= 0) {
        LOG_DEBUG("Heartbeat ping sent successfully (%d bytes)", rc);
        conn->last_heartbeat_sent = current_time;
        
        LOG_DEBUG("Waiting for heartbeat pong response (timeout: %d ms)", ping_timeout);
        char buffer[BUFFER_SIZE];
        rc = zmq_recv(conn->socket, buffer, BUFFER_SIZE - 1, 0);
        if (rc >= 0) {
            LOG_DEBUG("Received heartbeat response (%d bytes)", rc);
            buffer[rc] = '\0';
            LOG_DEBUG("Heartbeat response raw: %s", buffer);
            
            cJSON *response = cJSON_Parse(buffer);
            if (response) {
                cJSON *msg_type = cJSON_GetObjectItem(response, "messageType");
                if (msg_type && cJSON_IsString(msg_type) && 
                    strcmp(msg_type->valuestring, "HEARTBEAT_PONG") == 0) {
                    LOG_DEBUG("Valid HEARTBEAT_PONG received");
                    conn->last_heartbeat_received = time(NULL);
                    conn->heartbeat_failures = 0;
                    success = 1;
                    LOG_INFO("Heartbeat successful - connection healthy");
                } else {
                    LOG_WARN("Invalid heartbeat response type: %s", 
                            msg_type ? msg_type->valuestring : "NULL");
                }
                cJSON_Delete(response);
            } else {
                LOG_WARN("Failed to parse heartbeat response JSON");
            }
        } else {
            int err = errno;
            LOG_WARN("Failed to receive heartbeat response: %s (errno=%d)", 
                    zmq_strerror(err), err);
            if (err == EAGAIN) {
                LOG_WARN("Heartbeat response timeout after %d ms", ping_timeout);
            }
        }
    } else {
        int err = errno;
        LOG_ERROR("Failed to send heartbeat ping: %s (errno=%d)", 
                 zmq_strerror(err), err);
    }
    
    // Restore original timeout
    LOG_DEBUG("Restoring original receive timeout: %d ms", original_timeout);
    zmq_setsockopt(conn->socket, ZMQ_RCVTIMEO, &original_timeout, sizeof(original_timeout));
    
    free(ping_json);
    cJSON_Delete(ping);
    
    LOG_DEBUG("Heartbeat ping completed with success=%d", success);
    log_connection_state(conn, success ? "after successful heartbeat" : "after failed heartbeat");
    return success;
}

int test_connection(ConnectionState *conn, const ClientConfig *config) {
    return send_heartbeat_ping(conn, config);
}

int reconnect(ConnectionState *conn, const ClientConfig *config) {
    LOG_TRACE("reconnect called");
    LOG_INFO("Attempting to reconnect to %s...", config->endpoint);
    
    int delay_ms = config->initial_retry_delay_ms;
    LOG_DEBUG("Initial retry delay: %d ms, max retries: %d, max delay: %d ms",
              delay_ms, config->max_retries, config->max_retry_delay_ms);
    
    for (int attempt = 1; attempt <= config->max_retries; attempt++) {
        LOG_INFO("Reconnection attempt %d/%d", attempt, config->max_retries);
        log_connection_state(conn, "before reconnection attempt");
        
        if (initialize_connection(conn, config)) {
            LOG_INFO("Reconnected successfully on attempt %d", attempt);
            log_connection_state(conn, "after successful reconnection");
            return 1;
        }
        
        if (attempt < config->max_retries) {
            LOG_WARN("Reconnection attempt %d failed. Retrying in %dms...", attempt, delay_ms);
            LOG_DEBUG("Sleeping for %d ms before next attempt", delay_ms);
            usleep(delay_ms * 1000);
            
            // Exponential backoff with cap
            int old_delay = delay_ms;
            delay_ms *= 2;
            if (delay_ms > config->max_retry_delay_ms) {
                delay_ms = config->max_retry_delay_ms;
            }
            LOG_DEBUG("Updated retry delay: %d ms -> %d ms", old_delay, delay_ms);
        }
    }
    
    LOG_ERROR("Failed to reconnect after %d attempts", config->max_retries);
    log_connection_state(conn, "after all reconnection attempts failed");
    return 0;
}

int send_message_with_retry(ConnectionState *conn, const ClientConfig *config, 
                           const char *message_json, char *response_buffer, size_t buffer_size) {
    LOG_TRACE("send_message_with_retry called");
    LOG_DEBUG("Message length: %zu, buffer size: %zu", strlen(message_json), buffer_size);
    log_connection_state(conn, "start of send_message_with_retry");
    
    if (!conn->is_connected) {
        LOG_WARN("Connection not marked as connected, attempting to reconnect");
        log_connection_state(conn, "before reconnection for send");
        if (!reconnect(conn, config)) {
            LOG_ERROR("Failed to reconnect before sending message");
            return -1;
        }
    }
    
    // Check if we need to send a heartbeat
    time_t now = time(NULL);
    time_t last_heartbeat_seconds = now - conn->last_heartbeat_sent;
    int heartbeat_interval_seconds = config->heartbeat_interval_ms / 1000;
    
    LOG_DEBUG("Heartbeat check: last sent %ld seconds ago, interval: %d seconds",
              last_heartbeat_seconds, heartbeat_interval_seconds);
    log_heartbeat_details(conn, config, now, "before heartbeat check");
    
    if (last_heartbeat_seconds > heartbeat_interval_seconds) {
        LOG_DEBUG("Sending heartbeat before message (last heartbeat: %ld seconds ago)",
                 last_heartbeat_seconds);
        if (!send_heartbeat_ping(conn, config)) {
            LOG_WARN("Heartbeat failed, attempting to reconnect...");
            log_heartbeat_details(conn, config, now, "after failed heartbeat");
            if (!reconnect(conn, config)) {
                LOG_ERROR("Failed to reconnect after heartbeat failure");
                return -1;
            }
        } else {
            log_heartbeat_details(conn, config, time(NULL), "after successful heartbeat");
        }
    } else {
        LOG_DEBUG("Heartbeat not needed yet (last sent %lds ago, interval %ds)",
                 last_heartbeat_seconds, heartbeat_interval_seconds);
    }
    
    int delay_ms = config->initial_retry_delay_ms;
    LOG_DEBUG("Starting send retry loop with initial delay: %d ms, max retries: %d",
              delay_ms, config->max_retries);
    
    for (int attempt = 1; attempt <= config->max_retries; attempt++) {
        LOG_INFO("Send attempt %d/%d", attempt, config->max_retries);
        
        // Send message
        LOG_DEBUG("Sending message (length: %zu)", strlen(message_json));
        int rc = zmq_send(conn->socket, message_json, strlen(message_json), 0);
        if (rc < 0) {
            int err = errno;
            LOG_ERROR("Error sending message (attempt %d): %s (errno=%d)", 
                     attempt, zmq_strerror(err), err);
            log_timeout_retry_details(conn, config, attempt, config->max_retries, 
                                     delay_ms, "zmq_send", err, zmq_strerror(err));
            goto retry;
        }
        LOG_DEBUG("Message sent successfully (%d bytes)", rc);
        
        // Wait for response
        LOG_DEBUG("Waiting for response (timeout: %d ms)", config->connection_timeout_ms);
        rc = zmq_recv(conn->socket, response_buffer, buffer_size - 1, 0);
        if (rc < 0) {
            int err = errno;
            LOG_ERROR("Error receiving response (attempt %d): %s (errno=%d)", 
                     attempt, zmq_strerror(err), err);
            log_timeout_retry_details(conn, config, attempt, config->max_retries,
                                     delay_ms, "zmq_recv", err, zmq_strerror(err));
            goto retry;
        }
        
        LOG_DEBUG("Response received successfully (%d bytes)", rc);
        response_buffer[rc] = '\0';
        
        // Log first 200 chars of response for debugging
        if (rc > 0 && rc < 1024) {
            char preview[256];
            size_t preview_len = (size_t)(rc < 200 ? rc : 200);
            strncpy(preview, response_buffer, preview_len);
            preview[preview_len] = '\0';
            LOG_DEBUG("Response preview: %s", preview);
        }
        
        LOG_INFO("Message sent and response received successfully on attempt %d", attempt);
        log_connection_state(conn, "after successful send");
        return rc;
        
    retry:
        if (attempt < config->max_retries) {
            LOG_WARN("Send attempt %d failed, retrying in %dms...", attempt, delay_ms);
            LOG_DEBUG("Sleeping for %d ms before retry", delay_ms);
            usleep(delay_ms * 1000);
            
            // Try to reconnect before next attempt
            LOG_DEBUG("Attempting to reconnect before next send attempt");
            if (!reconnect(conn, config)) {
                LOG_WARN("Reconnection failed, continuing with exponential backoff");
                // If reconnection fails, continue with exponential backoff
                int old_delay = delay_ms;
                delay_ms *= 2;
                if (delay_ms > config->max_retry_delay_ms) {
                    delay_ms = config->max_retry_delay_ms;
                }
                LOG_DEBUG("Updated retry delay: %d ms -> %d ms", old_delay, delay_ms);
                continue;
            }
            LOG_DEBUG("Reconnection successful, will retry send");
        }
    }
    
    LOG_ERROR("Failed to send message after %d attempts", config->max_retries);
    log_connection_state(conn, "after all send attempts failed");
    return -1;
}

// Function to process different message types
static void process_message_response(const char *response) {
    LOG_TRACE("process_message_response called");
    LOG_DEBUG("Processing response (length: %zu)", strlen(response));
    
    cJSON *json = cJSON_Parse(response);
    if (!json) {
        LOG_WARN("Failed to parse JSON response");
        printf("Response (raw): %s\n", response);
        return;
    }
    
    LOG_DEBUG("JSON parsed successfully");
    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
    if (!message_type || !cJSON_IsString(message_type)) {
        LOG_ERROR("Invalid response format (missing messageType)");
        printf("Invalid response format (missing messageType): %s\n", response);
        cJSON_Delete(json);
        return;
    }
    
    const char *msg_type = message_type->valuestring;
    LOG_DEBUG("Message type: %s", msg_type);
    
    if (strcmp(msg_type, "TEXT") == 0) {
        cJSON *content = cJSON_GetObjectItem(json, "content");
        if (content && cJSON_IsString(content)) {
            printf("\n=== AI Response ===\n%s\n=== End of AI Response ===\n", content->valuestring);
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
            printf("\n=== ERROR ===\n%s\n=== End of Error ===\n", content->valuestring);
        } else {
            printf("ERROR message received\n");
        }
    }
    else if (strcmp(msg_type, "HEARTBEAT_PONG") == 0) {
        // Silently handle heartbeat pongs
        printf("Heartbeat pong received\n");
    }
    else {
        printf("Unknown message type: %s\n", msg_type);
        char *pretty = cJSON_Print(json);
        printf("Full message: %s\n", pretty);
        free(pretty);
    }
    
    cJSON_Delete(json);
}

// Enhanced function to send message and handle multiple responses
void send_text_message_robust(ConnectionState *conn, const ClientConfig *config, const char *text) {
    LOG_TRACE("send_text_message_robust called");
    LOG_INFO("Sending text message (length: %zu)", strlen(text));
    
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "messageType", "TEXT");
    cJSON_AddStringToObject(message, "content", text);
    
    char *json_str = cJSON_PrintUnformatted(message);
    LOG_DEBUG("Message JSON: %s", json_str);
    
    LOG_INFO("Sending message...");
    
    // Send the message
    char response[BUFFER_SIZE];
    int rc = send_message_with_retry(conn, config, json_str, response, sizeof(response));
    
    if (rc >= 0) {
        LOG_INFO("✓ Message sent successfully");
        
        // Process the first response
        LOG_DEBUG("Processing first response");
        process_message_response(response);
        
        // Now check for additional responses (non-blocking)
        LOG_INFO("Waiting for additional responses (if any)...");
        
        // Set non-blocking mode for subsequent receives (waiting for additional tool results)
        int original_timeout;
        size_t timeout_size = sizeof(original_timeout);
        zmq_getsockopt(conn->socket, ZMQ_RCVTIMEO, &original_timeout, &timeout_size);
        LOG_DEBUG("Original receive timeout: %d ms", original_timeout);
        
        int nonblocking_timeout = config->nonblocking_timeout_ms; // Configurable timeout for additional messages
        LOG_DEBUG("Setting non-blocking timeout to %d ms for additional responses", nonblocking_timeout);
        zmq_setsockopt(conn->socket, ZMQ_RCVTIMEO, &nonblocking_timeout, sizeof(nonblocking_timeout));
        
        int response_count = 1;
        LOG_DEBUG("Starting additional response polling loop");
        while (1) {
            LOG_DEBUG("Polling for additional response (timeout: %d ms)", nonblocking_timeout);
            rc = zmq_recv(conn->socket, response, sizeof(response) - 1, 0);
            if (rc < 0) {
                int err = errno;
                if (err == EAGAIN) {
                    // Timeout - no more messages
                    LOG_INFO("No more responses (timeout after %d ms)", nonblocking_timeout);
                    LOG_DEBUG("Polling timeout - normal termination of additional response loop");
                    break;
                } else {
                    LOG_ERROR("Error receiving additional response: %s (errno=%d)", 
                             zmq_strerror(err), err);
                    log_timeout_retry_details(conn, config, response_count, 1,
                                             nonblocking_timeout, "additional_response_recv", 
                                             err, zmq_strerror(err));
                    break;
                }
            }
            
            LOG_DEBUG("Received additional response (%d bytes)", rc);
            response[rc] = '\0';
            response_count++;
            
            // Log first 200 chars of response for debugging
            if (rc > 0 && rc < 1024) {
                char preview[256];
                size_t preview_len = (size_t)(rc < 200 ? rc : 200);
                strncpy(preview, response, preview_len);
                preview[preview_len] = '\0';
                LOG_DEBUG("Additional response preview: %s", preview);
            }
            
            LOG_INFO("=== Response #%d ===", response_count);
            process_message_response(response);
        }
        
        // Restore original timeout
        LOG_DEBUG("Restoring original receive timeout: %d ms", original_timeout);
        zmq_setsockopt(conn->socket, ZMQ_RCVTIMEO, &original_timeout, sizeof(original_timeout));
        
        LOG_INFO("✓ Total responses received: %d", response_count);
    } else {
        LOG_ERROR("✗ Failed to send message");
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
    LOG_INFO("ZMQ Client starting");
    LOG_DEBUG("Program: %s, argc: %d", argv[0], argc);
    
    if (argc < 2) {
        LOG_ERROR("Missing endpoint argument");
        print_usage(argv[0]);
        return 1;
    }
    
    LOG_INFO("Using endpoint: %s", argv[1]);
    
    // Default configuration
    ClientConfig config = {
        .endpoint = argv[1],
        .max_retries = MAX_RETRIES,
        .initial_retry_delay_ms = INITIAL_RETRY_DELAY_MS,
        .max_retry_delay_ms = MAX_RETRY_DELAY_MS,
        .heartbeat_interval_ms = HEARTBEAT_INTERVAL_MS,
        .connection_timeout_ms = CONNECTION_TIMEOUT_MS,
        .ping_timeout_ms = PING_TIMEOUT_MS,
        .nonblocking_timeout_ms = NONBLOCKING_TIMEOUT_MS
    };
    
    LOG_DEBUG("Configuration: max_retries=%d, initial_delay=%dms, max_delay=%dms, "
              "heartbeat_interval=%dms, connection_timeout=%dms, ping_timeout=%dms, "
              "nonblocking_timeout=%dms",
              config.max_retries, config.initial_retry_delay_ms, config.max_retry_delay_ms,
              config.heartbeat_interval_ms, config.connection_timeout_ms, config.ping_timeout_ms,
              config.nonblocking_timeout_ms);
    
    ConnectionState conn = {0};
    
    // Initialize connection with retry feedback
    LOG_INFO("Initializing connection...");
    printf("Connecting to %s...\n", config.endpoint);
    printf("(Will retry up to %d times if needed)\n", config.max_retries);
    
    if (!initialize_connection(&conn, &config)) {
        LOG_ERROR("Failed to establish initial connection");
        printf("\n✗ Failed to connect to %s after %d attempts\n", config.endpoint, config.max_retries);
        printf("Make sure Klawed is running with ZMQ enabled and listening on this endpoint.\n");
        printf("Check: KLAWED_ZMQ_ENDPOINT=%s\n", config.endpoint);
        return 1;
    }
    
    LOG_INFO("Connected to %s", config.endpoint);
    printf("\n✓ Connected to %s\n", config.endpoint);
    printf("Type your messages (or /help for commands)\n");
    printf("-----------------------------------------\n");
    
    // Interactive loop
    LOG_INFO("Entering interactive loop");
    char input[BUFFER_SIZE];
    while (1) {
        printf("\n> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            LOG_INFO("EOF or error reading input, exiting");
            break; // EOF or error
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = '\0';
        
        // Skip empty input
        if (strlen(input) == 0) {
            LOG_DEBUG("Empty input, skipping");
            continue;
        }
        
        LOG_DEBUG("User input: %s (length: %zu)", input, strlen(input));
        
        // Check for commands
        if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
            LOG_INFO("User requested exit");
            printf("Goodbye!\n");
            break;
        } else if (strcmp(input, "/help") == 0) {
            LOG_DEBUG("User requested help");
            print_usage(argv[0]);
        } else if (strcmp(input, "/clear") == 0) {
            LOG_DEBUG("User requested clear screen");
            printf("\033[2J\033[H"); // Clear screen
        } else if (strcmp(input, "/ping") == 0) {
            LOG_INFO("User requested ping");
            printf("Sending ping...\n");
            if (send_heartbeat_ping(&conn, &config)) {
                LOG_INFO("Ping successful");
                printf("✓ Ping successful - connection is healthy\n");
            } else {
                LOG_WARN("Ping failed");
                printf("✗ Ping failed\n");
                handle_connection_loss(&conn, &config);
            }
        } else if (strcmp(input, "/status") == 0) {
            LOG_DEBUG("User requested status");
            printf("Connection status:\n");
            printf("  Endpoint: %s\n", conn.endpoint);
            printf("  Connected: %s\n", conn.is_connected ? "Yes" : "No");
            printf("  Last heartbeat sent: %ld seconds ago\n", 
                   time(NULL) - conn.last_heartbeat_sent);
            printf("  Last heartbeat received: %ld seconds ago\n", 
                   time(NULL) - conn.last_heartbeat_received);
            printf("  Heartbeat failures: %d\n", conn.heartbeat_failures);
            
            // Log status as well
            LOG_INFO("Connection status - Endpoint: %s, Connected: %s, "
                     "Last heartbeat sent: %lds ago, Last heartbeat received: %lds ago, "
                     "Heartbeat failures: %d",
                     conn.endpoint, conn.is_connected ? "Yes" : "No",
                     time(NULL) - conn.last_heartbeat_sent,
                     time(NULL) - conn.last_heartbeat_received,
                     conn.heartbeat_failures);
        } else if (input[0] == '/') {
            LOG_WARN("Unknown command: %s", input);
            printf("Unknown command: %s\n", input);
            printf("Type /help for available commands\n");
        } else {
            // Regular text message
            LOG_INFO("Processing text message: %.*s", 
                    (int)(strlen(input) > 100 ? 100 : strlen(input)), input);
            send_text_message_robust(&conn, &config, input);
        }
    }
    
    // Cleanup
    LOG_INFO("Cleaning up connection");
    cleanup_connection(&conn);
    
    LOG_INFO("ZMQ Client exiting");
    return 0;
}

// Function to log connection state for debugging
void log_connection_state(const ConnectionState *conn, const char *context) {
    if (!conn) {
        LOG_ERROR("log_connection_state: conn is NULL");
        return;
    }
    
    time_t now = time(NULL);
    LOG_INFO("=== Connection State (%s) ===", context ? context : "unknown");
    LOG_INFO("  Endpoint: %s", conn->endpoint ? conn->endpoint : "NULL");
    LOG_INFO("  Connected: %s", conn->is_connected ? "Yes" : "No");
    LOG_INFO("  Socket: %p", conn->socket);
    LOG_INFO("  Context: %p", conn->context);
    LOG_INFO("  Last heartbeat sent: %ld seconds ago", 
             conn->last_heartbeat_sent ? now - conn->last_heartbeat_sent : -1);
    LOG_INFO("  Last heartbeat received: %ld seconds ago", 
             conn->last_heartbeat_received ? now - conn->last_heartbeat_received : -1);
    LOG_INFO("  Heartbeat failures: %d", conn->heartbeat_failures);
    LOG_INFO("=== End Connection State ===");
}

// Function to log timeout and retry details
static void log_timeout_retry_details(const ConnectionState *conn, const ClientConfig *config,
                                     int attempt, int max_attempts, int delay_ms, 
                                     const char *operation, int error_code, const char *error_msg) {
    (void)config; // Unused parameter
    LOG_WARN("=== Timeout/Retry Details ===");
    LOG_WARN("  Operation: %s", operation);
    LOG_WARN("  Attempt: %d/%d", attempt, max_attempts);
    LOG_WARN("  Current delay: %d ms", delay_ms);
    LOG_WARN("  Error: %s (code: %d)", error_msg ? error_msg : "unknown", error_code);
    LOG_WARN("  Connection state: %s", conn->is_connected ? "connected" : "disconnected");
    LOG_WARN("  Endpoint: %s", conn->endpoint ? conn->endpoint : "NULL");
    LOG_WARN("=== End Timeout/Retry Details ===");
}

// Function to log heartbeat details
static void log_heartbeat_details(const ConnectionState *conn, const ClientConfig *config,
                                 time_t current_time, const char *phase) {
    LOG_DEBUG("=== Heartbeat Details (%s) ===", phase);
    LOG_DEBUG("  Current time: %ld", current_time);
    LOG_DEBUG("  Last heartbeat sent: %ld", conn->last_heartbeat_sent);
    LOG_DEBUG("  Last heartbeat received: %ld", conn->last_heartbeat_received);
    LOG_DEBUG("  Time since last sent: %ld seconds", 
             conn->last_heartbeat_sent ? current_time - conn->last_heartbeat_sent : -1);
    LOG_DEBUG("  Time since last received: %ld seconds", 
             conn->last_heartbeat_received ? current_time - conn->last_heartbeat_received : -1);
    LOG_DEBUG("  Heartbeat failures: %d", conn->heartbeat_failures);
    LOG_DEBUG("  Heartbeat interval: %d ms (%d seconds)", 
             config->heartbeat_interval_ms, config->heartbeat_interval_ms / 1000);
    LOG_DEBUG("  Ping timeout: %d ms", config->ping_timeout_ms);
    LOG_DEBUG("=== End Heartbeat Details ===");
}