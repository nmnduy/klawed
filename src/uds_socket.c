/*
 * uds_socket.c - Unix Domain Socket utilities for Klawed
 *
 * Provides unified socket operations for IPC and streaming support.
 * Consolidates socket-related code from klawed.c and provider files.
 */

#include "uds_socket.h"
#include "klawed_internal.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <bsd/string.h>

// ============================================================================
// Socket Creation and Management
// ============================================================================

int uds_create_unix_socket(const char *socket_path) {
    LOG_DEBUG("uds_create_unix_socket: Starting socket creation for path: %s", socket_path);

    // Remove existing socket file if it exists
    LOG_DEBUG("uds_create_unix_socket: Removing existing socket file (if any)");
    unlink(socket_path);

    // Create socket
    LOG_DEBUG("uds_create_unix_socket: Creating AF_UNIX socket with SOCK_STREAM");
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    LOG_DEBUG("uds_create_unix_socket: Socket created successfully, fd: %d", server_fd);

    // Set socket to non-blocking
    LOG_DEBUG("uds_create_unix_socket: Setting socket to non-blocking mode");
    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags < 0) {
        LOG_ERROR("Failed to get socket flags: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
    if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_ERROR("Failed to set socket non-blocking: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
    LOG_DEBUG("uds_create_unix_socket: Socket set to non-blocking mode");

    // Bind socket
    LOG_DEBUG("uds_create_unix_socket: Binding socket to path: %s", socket_path);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strlcpy(addr.sun_path, socket_path, sizeof(addr.sun_path));

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind socket: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
    LOG_DEBUG("uds_create_unix_socket: Socket bound successfully");

    // Listen for connections
    LOG_DEBUG("uds_create_unix_socket: Listening for connections (backlog: 1)");
    if (listen(server_fd, 1) < 0) {
        LOG_ERROR("Failed to listen on socket: %s", strerror(errno));
        close(server_fd);
        unlink(socket_path);
        return -1;
    }

    LOG_INFO("Socket created and listening on: %s", socket_path);
    LOG_DEBUG("uds_create_unix_socket: Socket setup complete, returning fd: %d", server_fd);
    return server_fd;
}

int uds_accept_connection(int server_fd) {
    LOG_DEBUG("uds_accept_connection: Attempting to accept connection on server fd: %d", server_fd);

    struct sockaddr_un addr;
    socklen_t addr_len = sizeof(addr);

    int client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("Failed to accept connection: %s", strerror(errno));
        } else {
            LOG_DEBUG("uds_accept_connection: No pending connections (EAGAIN/EWOULDBLOCK)");
        }
        return -1;
    }

    LOG_DEBUG("uds_accept_connection: Connection accepted, client fd: %d", client_fd);

    // Set client socket to non-blocking
    LOG_DEBUG("uds_accept_connection: Setting client socket to non-blocking mode");
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        LOG_DEBUG("uds_accept_connection: Client socket set to non-blocking");
    } else {
        LOG_WARN("uds_accept_connection: Failed to get client socket flags, continuing anyway");
    }

    LOG_INFO("Accepted socket connection (client fd: %d)", client_fd);
    LOG_DEBUG("uds_accept_connection: Returning client fd: %d", client_fd);
    return client_fd;
}

void uds_cleanup(SocketIPC *socket_ipc) {
    LOG_DEBUG("uds_cleanup: Starting socket cleanup");

    if (!socket_ipc) {
        LOG_DEBUG("uds_cleanup: socket_ipc is NULL, returning");
        return;
    }

    LOG_DEBUG("uds_cleanup: Socket IPC state - enabled: %d, server_fd: %d, client_fd: %d, path: %s",
              socket_ipc->enabled, socket_ipc->server_fd, socket_ipc->client_fd,
              socket_ipc->socket_path ? socket_ipc->socket_path : "(null)");

    if (socket_ipc->client_fd >= 0) {
        LOG_DEBUG("uds_cleanup: Closing client fd: %d", socket_ipc->client_fd);
        close(socket_ipc->client_fd);
        socket_ipc->client_fd = -1;
        LOG_DEBUG("uds_cleanup: Client fd closed");
    }

    if (socket_ipc->server_fd >= 0) {
        LOG_DEBUG("uds_cleanup: Closing server fd: %d", socket_ipc->server_fd);
        close(socket_ipc->server_fd);
        socket_ipc->server_fd = -1;
        LOG_DEBUG("uds_cleanup: Server fd closed");
    }

    if (socket_ipc->socket_path) {
        LOG_DEBUG("uds_cleanup: Removing socket file: %s", socket_ipc->socket_path);
        unlink(socket_ipc->socket_path);
        free(socket_ipc->socket_path);
        socket_ipc->socket_path = NULL;
    }

    socket_ipc->enabled = 0;
    LOG_DEBUG("uds_cleanup: Socket cleanup complete");
}

// ============================================================================
// Socket I/O Operations
// ============================================================================

int uds_read_input(int client_fd, char *buffer, size_t buffer_size) {
    LOG_DEBUG("uds_read_input: Attempting to read from client fd: %d, buffer size: %zu",
              client_fd, buffer_size);

    // First check if there's data available with a short timeout
    struct pollfd pfd = {0};
    pfd.fd = client_fd;
    pfd.events = POLLIN;

    int poll_result = poll(&pfd, 1, 100); // 100ms timeout for poll
    if (poll_result < 0) {
        LOG_ERROR("Poll failed while reading from socket: %s", strerror(errno));
        return -1;
    }

    if (poll_result == 0) {
        LOG_DEBUG("uds_read_input: No data available within timeout");
        return 0; // No data available
    }

    if (!(pfd.revents & POLLIN)) {
        LOG_DEBUG("uds_read_input: Socket not ready for reading (revents: 0x%x)", pfd.revents);
        return 0;
    }

    ssize_t bytes_read = read(client_fd, buffer, buffer_size - 1);
    if (bytes_read < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("Failed to read from socket: %s", strerror(errno));
            LOG_DEBUG("uds_read_input: Read error, returning -1");
            return -1;
        }
        LOG_DEBUG("uds_read_input: No data available (EAGAIN/EWOULDBLOCK), returning 0");
        return 0; // No data available
    } else if (bytes_read == 0) {
        LOG_INFO("Socket client disconnected (fd: %d)", client_fd);
        LOG_DEBUG("uds_read_input: Client disconnected, returning -1");
        return -1; // Client disconnected
    }

    buffer[bytes_read] = '\0';
    LOG_DEBUG("uds_read_input: Read %zd bytes from socket fd: %d", bytes_read, client_fd);
    LOG_DEBUG("uds_read_input: Data: \"%.*s\"", (int)bytes_read > 50 ? 50 : (int)bytes_read, buffer);
    return (int)bytes_read;
}

int uds_write_output(int client_fd, const char *data, size_t data_len) {
    LOG_DEBUG("uds_write_output: Attempting to write %zu bytes to client fd: %d", data_len, client_fd);

    if (client_fd < 0) {
        LOG_DEBUG("uds_write_output: Invalid client fd, returning -1");
        return -1;
    }

    size_t total_written = 0;
    int retries = 0;
    const int max_retries = 10;
    const int write_timeout_ms = 5000; // 5 second timeout for write operations

    time_t start_time = time(NULL);

    while (total_written < data_len && retries < max_retries) {
        // Check for timeout
        time_t current_time = time(NULL);
        if (current_time - start_time > write_timeout_ms / 1000) {
            LOG_ERROR("Write operation timed out after %ld seconds", (long)(current_time - start_time));
            return -1;
        }

        ssize_t bytes_written = write(client_fd, data + total_written, data_len - total_written);
        if (bytes_written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                retries++;
                usleep(1000); // 1ms
                continue;
            } else {
                LOG_ERROR("Failed to write to socket: %s", strerror(errno));
                return -1;
            }
        }
        total_written += (size_t)bytes_written;
        retries = 0;
    }

    if (total_written < data_len) {
        LOG_ERROR("Failed to write all data to socket: wrote %zu of %zu bytes (max retries exceeded)", total_written, data_len);
        return -1;
    }

    LOG_DEBUG("uds_write_output: Wrote %zu bytes to socket fd: %d", total_written, client_fd);
    return 0;
}

// Check if socket connection is still alive by attempting a non-blocking write test
int uds_check_connection(int client_fd) {
    LOG_DEBUG("uds_check_connection: Checking connection health for fd: %d", client_fd);

    if (client_fd < 0) {
        LOG_DEBUG("uds_check_connection: Invalid fd, returning 0");
        return 0;
    }

    // Try to write a zero-length buffer (just checks if socket is writable)
    struct pollfd pfd = {0};
    pfd.fd = client_fd;
    pfd.events = POLLOUT | POLLERR | POLLHUP;
    pfd.revents = 0;

    int result = poll(&pfd, 1, 0); // Non-blocking poll
    if (result < 0) {
        LOG_ERROR("Poll failed while checking connection: %s", strerror(errno));
        return 0;
    }

    if (result == 0) {
        LOG_DEBUG("uds_check_connection: Socket is writable (no errors)");
        return 1; // Socket is writable
    }

    // Check for errors
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        LOG_WARN("Socket connection has error or hung up (revents: 0x%x)", pfd.revents);
        return 0;
    }

    LOG_DEBUG("uds_check_connection: Socket is healthy (revents: 0x%x)", pfd.revents);
    return 1;
}

// Send a ping/heartbeat message to keep connection alive
int uds_send_ping(int client_fd) {
    LOG_DEBUG("uds_send_ping: Sending ping to client fd: %d", client_fd);

    if (client_fd < 0) {
        LOG_DEBUG("uds_send_ping: Invalid fd, returning -1");
        return -1;
    }

    cJSON *ping_json = cJSON_CreateObject();
    if (!ping_json) {
        LOG_ERROR("Failed to create ping JSON object");
        return -1;
    }

    cJSON_AddStringToObject(ping_json, "type", "ping");
    cJSON_AddNumberToObject(ping_json, "timestamp", (double)(long)time(NULL));

    int result = uds_write_json(client_fd, ping_json);
    cJSON_Delete(ping_json);

    if (result < 0) {
        LOG_WARN("Failed to send ping to socket");
        return -1;
    }

    LOG_DEBUG("uds_send_ping: Ping sent successfully");
    return 0;
}

// Attempt to gracefully handle write failures by checking connection and optionally reconnecting
int uds_handle_write_failure(int client_fd, SocketIPC *socket_ipc) {
    LOG_DEBUG("uds_handle_write_failure: Handling write failure for fd: %d", client_fd);

    if (client_fd < 0 || !socket_ipc) {
        LOG_DEBUG("uds_handle_write_failure: Invalid parameters");
        return -1;
    }

    // First check if connection is still alive
    if (uds_check_connection(client_fd)) {
        LOG_DEBUG("uds_handle_write_failure: Connection appears healthy, write failure may be temporary");
        return 0; // Connection is fine, failure might be temporary
    }

    LOG_WARN("Write failure due to broken connection, attempting to clean up");

    // Close the broken connection
    if (socket_ipc->client_fd >= 0) {
        close(socket_ipc->client_fd);
        socket_ipc->client_fd = -1;
    }

    // Try to accept a new connection if server is still running
    if (socket_ipc->server_fd >= 0) {
        LOG_INFO("Attempting to accept new connection after write failure");
        int new_client = uds_accept_connection(socket_ipc->server_fd);
        if (new_client >= 0) {
            socket_ipc->client_fd = new_client;
            LOG_INFO("New connection accepted after write failure (fd: %d)", new_client);
            return 1; // New connection established
        }
    }

    LOG_DEBUG("uds_handle_write_failure: No new connection available");
    return -1; // Could not recover
}

int uds_has_data(int fd) {
    LOG_DEBUG("uds_has_data: Checking if fd %d has data", fd);

    if (fd < 0) {
        LOG_DEBUG("uds_has_data: Invalid fd, returning 0");
        return 0;
    }

    struct pollfd pfd = {0};
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int result = poll(&pfd, 1, 0);
    if (result < 0) {
        LOG_ERROR("Poll failed: %s", strerror(errno));
        return 0;
    }

    int has_data = (result > 0 && (pfd.revents & POLLIN));
    LOG_DEBUG("uds_has_data: fd %d has data: %s", fd, has_data ? "yes" : "no");
    return has_data;
}

// ============================================================================
// Socket Event Streaming
// ============================================================================
// Socket Message Formatting (New Interface)
// ============================================================================

/**
 * Send a message with the standard format: {"messageType": "...", "content": ...}
 */
static int uds_send_message_internal(int client_fd, const char *message_type, cJSON *content) {
    if (client_fd < 0 || !message_type || !content) {
        return -1;
    }

    cJSON *message_json = cJSON_CreateObject();
    if (!message_json) {
        return -1;
    }

    cJSON_AddStringToObject(message_json, "messageType", message_type);
    cJSON_AddItemToObject(message_json, "content", content);

    // Add timestamp if available
    // Could add: cJSON_AddStringToObject(message_json, "timestamp", iso_timestamp);

    char *json_str = cJSON_PrintUnformatted(message_json);
    if (!json_str) {
        cJSON_Delete(message_json);
        return -1;
    }

    int result = uds_write_output(client_fd, json_str, strlen(json_str));
    if (result == 0) {
        // Add newline separator
        uds_write_output(client_fd, "\n", 1);
    }

    free(json_str);
    cJSON_Delete(message_json);
    return result;
}

// ============================================================================

void uds_send_event(ConversationState *state, const char *event_type, cJSON *event_data) {
    // In simplified socket mode, we don't send streaming events
    // However, we might want to extract text from text_delta events
    if (!state || state->socket_streaming_fd < 0 || !event_type || !event_data) {
        return;
    }

    // Check if this is a text_delta event that we should forward as TEXT
    if (strcmp(event_type, "content_block_delta") == 0) {
        cJSON *data_type = cJSON_GetObjectItem(event_data, "type");
        if (data_type && cJSON_IsString(data_type) && strcmp(data_type->valuestring, "text_delta") == 0) {
            cJSON *text = cJSON_GetObjectItem(event_data, "text");
            if (text && cJSON_IsString(text) && text->valuestring[0] != '\0') {
                // Send text delta as TEXT message
                cJSON *text_content = cJSON_CreateString(text->valuestring);
                if (text_content) {
                    uds_send_message_internal(state->socket_streaming_fd, "TEXT", text_content);
                    cJSON_Delete(text_content);
                }
            }
        }
    }
    // For other event types, don't send anything
}

/**
 * Send an error message
 * Note: In simplified socket mode, errors are not sent to socket
 */
void uds_send_error(int client_fd, const char *error_message) {
    // In simplified socket mode, we don't send error messages
    // Just log them internally
    (void)client_fd; // Unused parameter
    if (error_message) {
        LOG_DEBUG("Socket error (not sent): %s", error_message);
    }
}



/**
 * Send complete API response in new format
 * In simplified socket mode, extract text content and send as TEXT message
 */
int uds_send_api_response(int client_fd, cJSON *response) {
    if (client_fd < 0 || !response) {
        return -1;
    }

    // In simplified socket mode, extract text content from API response
    // and send as TEXT message instead of full apiResponse

    // Try to extract text from Anthropic format
    cJSON *content_array = cJSON_GetObjectItem(response, "content");
    if (content_array && cJSON_IsArray(content_array)) {
        cJSON *content_item = NULL;
        cJSON_ArrayForEach(content_item, content_array) {
            cJSON *type = cJSON_GetObjectItem(content_item, "type");
            if (type && cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0) {
                cJSON *text = cJSON_GetObjectItem(content_item, "text");
                if (text && cJSON_IsString(text)) {
                    // Found text content, send as TEXT message
                    cJSON *text_content = cJSON_CreateString(text->valuestring);
                    if (text_content) {
                        int result = uds_send_message_internal(client_fd, "TEXT", text_content);
                        cJSON_Delete(text_content);
                        return result;
                    }
                }
            }
        }
    }

    // Try OpenAI format
    cJSON *choices = cJSON_GetObjectItem(response, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (message) {
            cJSON *content = cJSON_GetObjectItem(message, "content");
            if (content && cJSON_IsString(content)) {
                // Found text content, send as TEXT message
                cJSON *text_content = cJSON_CreateString(content->valuestring);
                if (text_content) {
                    int result = uds_send_message_internal(client_fd, "TEXT", text_content);
                    cJSON_Delete(text_content);
                    return result;
                }
            }
        }
    }

    // If no text content found, don't send anything
    LOG_DEBUG("uds_send_api_response: No text content found in API response");
    return 0;
}

/**
 * Send tool call(s) request
 * Note: In simplified socket mode, tool calls are not sent to socket
 */
int uds_send_tool_call(int client_fd, ToolCall *tools, int tool_count) {
    // In simplified socket mode, we don't send tool calls to socket
    // Just log them internally
    (void)client_fd; // Unused parameter
    if (tools && tool_count > 0) {
        LOG_DEBUG("Socket tool calls (not sent): %d tool(s)", tool_count);
        for (int i = 0; i < tool_count; i++) {
            ToolCall *tool = &tools[i];
            LOG_DEBUG("  Tool[%d]: %s (id: %s)", i,
                     tool->name ? tool->name : "NULL",
                     tool->id ? tool->id : "NULL");
        }
    }
    return 0;
}

/**
 * Send tool execution result
 */
int uds_send_tool_result(int client_fd, const char *tool_call_id, const char *tool_name, cJSON *result) {
    // In simplified socket mode, we don't send tool results to socket
    // Just log them internally
    (void)client_fd; // Unused parameter
    (void)result;    // Unused parameter
    LOG_DEBUG("Socket tool result (not sent): %s -> %s",
             tool_name ? tool_name : "NULL",
             tool_call_id ? tool_call_id : "NULL");
    return 0;
}

/**
 * Send final response text (after all tool executions)
 */
int uds_send_final_response(int client_fd, const char *text_response) {
    if (client_fd < 0 || !text_response) {
        return -1;
    }

    cJSON *content = cJSON_CreateString(text_response);
    if (!content) {
        return -1;
    }

    int result = uds_send_message_internal(client_fd, "TEXT", content);
    cJSON_Delete(content);

    return result;
}

// ============================================================================
// Socket Streaming Context Management
// ============================================================================

void uds_streaming_context_init(SocketStreamingContext *ctx, int client_fd) {
    if (!ctx) return;

    memset(ctx, 0, sizeof(*ctx));
    ctx->client_fd = client_fd;

    // Initialize buffers with reasonable defaults
    ctx->accumulated_capacity = 4096;
    ctx->accumulated_text = malloc(ctx->accumulated_capacity);
    if (ctx->accumulated_text) {
        ctx->accumulated_text[0] = '\0';
    }

    ctx->tool_input_capacity = 4096;
    ctx->tool_input_json = malloc(ctx->tool_input_capacity);
    if (ctx->tool_input_json) {
        ctx->tool_input_json[0] = '\0';
    }

    ctx->content_block_index = -1;
    ctx->content_block_type = NULL;
    ctx->tool_use_id = NULL;
    ctx->tool_use_name = NULL;
    ctx->message_start_data = NULL;
    ctx->stop_reason = NULL;
}

void uds_streaming_context_free(SocketStreamingContext *ctx) {
    if (!ctx) return;

    free(ctx->accumulated_text);
    free(ctx->tool_input_json);
    free(ctx->content_block_type);
    free(ctx->tool_use_id);
    free(ctx->tool_use_name);
    free(ctx->stop_reason);

    if (ctx->message_start_data) {
        cJSON_Delete(ctx->message_start_data);
    }

    memset(ctx, 0, sizeof(*ctx));
}

// ============================================================================
// Helper Functions
// ============================================================================

int uds_write_json(int client_fd, cJSON *json_obj) {
    if (client_fd < 0 || !json_obj) {
        return -1;
    }

    char *json_str = cJSON_PrintUnformatted(json_obj);
    if (!json_str) {
        return -1;
    }

    int result = uds_write_output(client_fd, json_str, strlen(json_str));
    if (result == 0) {
        result = uds_write_output(client_fd, "\n", 1);
    }

    free(json_str);
    return result;
}
