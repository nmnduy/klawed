/*
 * uds_socket.c - Unix Domain Socket communication for Klawed
 *
 * Implementation:
 * - SOCK_STREAM server that accepts single client
 * - 4-byte network-order length header + JSON payload framing
 * - Synchronous blocking send (waits for peer response)
 * - Automatic client reconnection handling
 */

#include "uds_socket.h"
#include "logger.h"
#include "klawed_internal.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <time.h>

#include <bsd/string.h>
#include <cjson/cJSON.h>

// Helper: read exactly n bytes from socket
static ssize_t read_exact(int fd, void *buf, size_t count, int timeout_sec) {
    size_t total = 0;
    char *ptr = (char *)buf;
    
    LOG_DEBUG("UDS: read_exact: fd=%d, count=%zu, timeout=%d", fd, count, timeout_sec);

    // Track elapsed time for timeout
    time_t start_time = 0;
    if (timeout_sec > 0) {
        start_time = time(NULL);
    }

    while (total < count) {
        // Use select for timeout if specified
        if (timeout_sec > 0) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(fd, &readfds);

            // Calculate remaining timeout
            time_t now = time(NULL);
            time_t elapsed = now - start_time;
            if (elapsed >= timeout_sec) {
                LOG_DEBUG("UDS: read_exact: total timeout after %ld seconds", elapsed);
                errno = ETIMEDOUT;
                return -1;
            }
            
            int remaining_timeout = timeout_sec - (int)elapsed;
            struct timeval tv;
            tv.tv_sec = remaining_timeout;
            tv.tv_usec = 0;

            LOG_DEBUG("UDS: read_exact: calling select (remaining timeout %ds)", remaining_timeout);
            int sel = select(fd + 1, &readfds, NULL, NULL, &tv);
            if (sel < 0) {
                LOG_DEBUG("UDS: read_exact: select failed: %s", strerror(errno));
                if (errno == EINTR) continue;
                return -1;
            }
            if (sel == 0) {
                // Timeout
                LOG_DEBUG("UDS: read_exact: select timeout after %ds", remaining_timeout);
                errno = ETIMEDOUT;
                return -1;
            }
            LOG_DEBUG("UDS: read_exact: select returned %d (data available)", sel);
        }

        size_t remaining = count - total;
        LOG_DEBUG("UDS: read_exact: calling read for %zu bytes (have %zu/%zu)", remaining, total, count);
        ssize_t n = read(fd, ptr + total, remaining);
        LOG_DEBUG("UDS: read_exact: read returned %zd bytes", n);
        
        if (n < 0) {
            LOG_DEBUG("UDS: read_exact: read failed: %s", strerror(errno));
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block, retry with select
                LOG_DEBUG("UDS: read_exact: would block, retrying");
                continue;
            }
            return -1;
        }
        if (n == 0) {
            // Connection closed
            LOG_DEBUG("UDS: read_exact: connection closed (EOF)");
            errno = ECONNRESET;
            return -1;
        }
        total += (size_t)n;
        LOG_DEBUG("UDS: read_exact: progress: %zu/%zu bytes", total, count);
    }

    LOG_DEBUG("UDS: read_exact: completed successfully, read %zu bytes", total);
    return (ssize_t)total;
}

// Helper: write exactly n bytes to socket
static ssize_t write_exact(int fd, const void *buf, size_t count, int timeout_sec) {
    size_t total = 0;
    const char *ptr = (const char *)buf;
    
    LOG_DEBUG("UDS: write_exact: fd=%d, count=%zu, timeout=%d", fd, count, timeout_sec);

    // Track elapsed time for timeout
    time_t start_time = 0;
    if (timeout_sec > 0) {
        start_time = time(NULL);
    }

    while (total < count) {
        // Use select for timeout if specified
        if (timeout_sec > 0) {
            fd_set writefds;
            FD_ZERO(&writefds);
            FD_SET(fd, &writefds);

            // Calculate remaining timeout
            time_t now = time(NULL);
            time_t elapsed = now - start_time;
            if (elapsed >= timeout_sec) {
                LOG_DEBUG("UDS: write_exact: total timeout after %ld seconds", elapsed);
                errno = ETIMEDOUT;
                return -1;
            }
            
            int remaining_timeout = timeout_sec - (int)elapsed;
            struct timeval tv;
            tv.tv_sec = remaining_timeout;
            tv.tv_usec = 0;

            LOG_DEBUG("UDS: write_exact: calling select for write (remaining timeout %ds)", remaining_timeout);
            int sel = select(fd + 1, NULL, &writefds, NULL, &tv);
            if (sel < 0) {
                LOG_DEBUG("UDS: write_exact: select failed: %s", strerror(errno));
                if (errno == EINTR) continue;
                return -1;
            }
            if (sel == 0) {
                LOG_DEBUG("UDS: write_exact: select timeout after %ds", remaining_timeout);
                errno = ETIMEDOUT;
                return -1;
            }
            LOG_DEBUG("UDS: write_exact: select returned %d (ready for write)", sel);
        }

        size_t remaining = count - total;
        LOG_DEBUG("UDS: write_exact: calling write for %zu bytes (have %zu/%zu)", remaining, total, count);
        ssize_t n = write(fd, ptr + total, remaining);
        LOG_DEBUG("UDS: write_exact: write returned %zd bytes", n);
        
        if (n < 0) {
            LOG_DEBUG("UDS: write_exact: write failed: %s", strerror(errno));
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_DEBUG("UDS: write_exact: would block, retrying");
                continue;
            }
            return -1;
        }
        total += (size_t)n;
        LOG_DEBUG("UDS: write_exact: progress: %zu/%zu bytes", total, count);
    }

    LOG_DEBUG("UDS: write_exact: completed successfully, wrote %zu bytes", total);
    return (ssize_t)total;
}

// Helper: send a framed message (4-byte length header + payload)
static int send_framed_message(int fd, const char *message, size_t message_len, int timeout_sec) {
    LOG_DEBUG("UDS: send_framed_message: fd=%d, message_len=%zu, timeout=%d", fd, message_len, timeout_sec);
    
    if (message_len > UDS_MAX_MESSAGE_SIZE) {
        LOG_ERROR("UDS: Message too large: %zu bytes (max: %d)", message_len, UDS_MAX_MESSAGE_SIZE);
        return UDS_ERROR_MESSAGE_TOO_LARGE;
    }

    // Send 4-byte length header (network byte order)
    uint32_t len_network = htonl((uint32_t)message_len);
    LOG_DEBUG("UDS: send_framed_message: sending length header: %u bytes (network: 0x%08x)", 
              (uint32_t)message_len, len_network);
    
    if (write_exact(fd, &len_network, sizeof(len_network), timeout_sec) != sizeof(len_network)) {
        LOG_ERROR("UDS: Failed to send length header: %s", strerror(errno));
        return UDS_ERROR_SEND_FAILED;
    }

    LOG_DEBUG("UDS: send_framed_message: sending payload of %zu bytes", message_len);
    // Send payload
    if (write_exact(fd, message, message_len, timeout_sec) != (ssize_t)message_len) {
        LOG_ERROR("UDS: Failed to send payload: %s", strerror(errno));
        return UDS_ERROR_PARTIAL_SEND;
    }

    LOG_DEBUG("UDS: send_framed_message: message sent successfully");
    return UDS_ERROR_NONE;
}

// Helper: receive a framed message (4-byte length header + payload)
static int receive_framed_message(int fd, char *buffer, size_t buffer_size, int timeout_sec) {
    LOG_DEBUG("UDS: receive_framed_message: fd=%d, buffer_size=%zu, timeout=%d", fd, buffer_size, timeout_sec);
    
    // Read 4-byte length header
    uint32_t len_network;
    LOG_DEBUG("UDS: receive_framed_message: reading length header (4 bytes)");
    ssize_t n = read_exact(fd, &len_network, sizeof(len_network), timeout_sec);
    if (n < 0) {
        if (errno == ETIMEDOUT) {
            LOG_DEBUG("UDS: receive_framed_message: timeout reading length header");
            return UDS_ERROR_RECEIVE_TIMEOUT;
        }
        if (errno == ECONNRESET) {
            LOG_DEBUG("UDS: receive_framed_message: connection closed while reading length header");
            return UDS_ERROR_CONNECTION_CLOSED;
        }
        LOG_ERROR("UDS: Failed to read length header: %s", strerror(errno));
        return UDS_ERROR_RECEIVE_FAILED;
    }
    
    LOG_DEBUG("UDS: receive_framed_message: read length header: 0x%08x", len_network);

    uint32_t payload_len = ntohl(len_network);
    LOG_DEBUG("UDS: receive_framed_message: payload length: %u bytes", payload_len);

    if (payload_len > UDS_MAX_MESSAGE_SIZE) {
        LOG_ERROR("UDS: Received message too large: %u bytes", payload_len);
        return UDS_ERROR_MESSAGE_TOO_LARGE;
    }

    if (payload_len >= buffer_size) {
        LOG_ERROR("UDS: Buffer too small for message: %u bytes needed, %zu available",
                  payload_len, buffer_size);
        return UDS_ERROR_NOMEM;
    }

    LOG_DEBUG("UDS: receive_framed_message: reading payload of %u bytes", payload_len);
    // Read payload
    n = read_exact(fd, buffer, payload_len, timeout_sec);
    if (n < 0) {
        if (errno == ETIMEDOUT) {
            LOG_DEBUG("UDS: receive_framed_message: timeout reading payload");
            return UDS_ERROR_RECEIVE_TIMEOUT;
        }
        if (errno == ECONNRESET) {
            LOG_DEBUG("UDS: receive_framed_message: connection closed while reading payload");
            return UDS_ERROR_CONNECTION_CLOSED;
        }
        LOG_ERROR("UDS: Failed to read payload: %s", strerror(errno));
        return UDS_ERROR_RECEIVE_FAILED;
    }

    buffer[payload_len] = '\0';
    LOG_DEBUG("UDS: receive_framed_message: received %u byte payload, first 100 chars: %.100s%s", 
              payload_len, buffer, payload_len > 100 ? "..." : "");
    return (int)payload_len;
}

UDSContext* uds_socket_init(const char *socket_path) {
    if (!socket_path || strlen(socket_path) == 0) {
        LOG_ERROR("UDS: Socket path cannot be NULL or empty");
        return NULL;
    }

    LOG_INFO("UDS: Initializing Unix socket at %s", socket_path);

    UDSContext *ctx = calloc(1, sizeof(UDSContext));
    if (!ctx) {
        LOG_ERROR("UDS: Failed to allocate context");
        return NULL;
    }

    ctx->server_fd = -1;
    ctx->client_fd = -1;
    ctx->client_connected = false;
    ctx->message_seq = 0;

    // Copy socket path
    ctx->socket_path = strdup(socket_path);
    if (!ctx->socket_path) {
        LOG_ERROR("UDS: Failed to duplicate socket path");
        free(ctx);
        return NULL;
    }

    // Load configuration from environment (using strtol for proper error handling)
    const char *retries_str = getenv("KLAWED_UNIX_SOCKET_RETRIES");
    if (retries_str) {
        char *endptr = NULL;
        long val = strtol(retries_str, &endptr, 10);
        if (endptr != retries_str && *endptr == '\0' && val >= 0 && val <= INT_MAX) {
            ctx->max_retries = (int)val;
        } else {
            ctx->max_retries = UDS_DEFAULT_RETRIES;
        }
    } else {
        ctx->max_retries = UDS_DEFAULT_RETRIES;
    }

    const char *timeout_str = getenv("KLAWED_UNIX_SOCKET_TIMEOUT");
    if (timeout_str) {
        char *endptr = NULL;
        long val = strtol(timeout_str, &endptr, 10);
        if (endptr != timeout_str && *endptr == '\0' && val >= 0 && val <= INT_MAX) {
            ctx->timeout_sec = (int)val;
        } else {
            ctx->timeout_sec = UDS_DEFAULT_TIMEOUT_SEC;
        }
    } else {
        ctx->timeout_sec = UDS_DEFAULT_TIMEOUT_SEC;
    }

    LOG_INFO("UDS: Configuration - retries: %d, timeout: %ds", ctx->max_retries, ctx->timeout_sec);

    // Remove existing socket file if it exists
    unlink(socket_path);

    // Create socket
    LOG_DEBUG("UDS: Creating socket (AF_UNIX, SOCK_STREAM)");
    ctx->server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ctx->server_fd < 0) {
        LOG_ERROR("UDS: Failed to create socket: %s", strerror(errno));
        free(ctx->socket_path);
        free(ctx);
        return NULL;
    }
    LOG_DEBUG("UDS: Socket created successfully, fd=%d", ctx->server_fd);

    // Bind to socket path
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    LOG_DEBUG("UDS: Binding to socket path: %s", socket_path);
    if (strlcpy(addr.sun_path, socket_path, sizeof(addr.sun_path)) >= sizeof(addr.sun_path)) {
        LOG_ERROR("UDS: Socket path too long: %s", socket_path);
        close(ctx->server_fd);
        free(ctx->socket_path);
        free(ctx);
        return NULL;
    }

    LOG_DEBUG("UDS: Calling bind() with path: %s", addr.sun_path);
    if (bind(ctx->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("UDS: Failed to bind to %s: %s", socket_path, strerror(errno));
        close(ctx->server_fd);
        free(ctx->socket_path);
        free(ctx);
        return NULL;
    }
    LOG_DEBUG("UDS: Bind successful");

    // Listen for connections (backlog of 1 - single client)
    LOG_DEBUG("UDS: Calling listen() with backlog=%d", UDS_DEFAULT_BACKLOG);
    if (listen(ctx->server_fd, UDS_DEFAULT_BACKLOG) < 0) {
        LOG_ERROR("UDS: Failed to listen: %s", strerror(errno));
        close(ctx->server_fd);
        unlink(socket_path);
        free(ctx->socket_path);
        free(ctx);
        return NULL;
    }
    LOG_DEBUG("UDS: Listen successful, server ready to accept connections");

    ctx->enabled = true;
    LOG_INFO("UDS: Server listening on %s", socket_path);

    return ctx;
}

void uds_socket_cleanup(UDSContext *ctx) {
    if (!ctx) return;

    LOG_INFO("UDS: Cleaning up context for %s", ctx->socket_path ? ctx->socket_path : "unknown");

    if (ctx->client_fd >= 0) {
        close(ctx->client_fd);
        ctx->client_fd = -1;
    }

    if (ctx->server_fd >= 0) {
        close(ctx->server_fd);
        ctx->server_fd = -1;
    }

    if (ctx->socket_path) {
        unlink(ctx->socket_path);
        free(ctx->socket_path);
        ctx->socket_path = NULL;
    }

    ctx->enabled = false;
    free(ctx);
}

int uds_socket_accept(UDSContext *ctx, int timeout_sec) {
    if (!ctx || ctx->server_fd < 0) {
        LOG_ERROR("UDS: Invalid context for accept");
        return UDS_ERROR_INVALID_PARAM;
    }

    // If client already connected, disconnect first
    if (ctx->client_connected && ctx->client_fd >= 0) {
        LOG_INFO("UDS: Disconnecting previous client before accepting new one");
        close(ctx->client_fd);
        ctx->client_fd = -1;
        ctx->client_connected = false;
    }

    LOG_INFO("UDS: Waiting for client connection (timeout: %ds)", timeout_sec);

    // Use select for timeout
    if (timeout_sec > 0) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(ctx->server_fd, &readfds);

        struct timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;

        int sel = select(ctx->server_fd + 1, &readfds, NULL, NULL, &tv);
        if (sel < 0) {
            LOG_ERROR("UDS: select() failed: %s", strerror(errno));
            return UDS_ERROR_ACCEPT_FAILED;
        }
        if (sel == 0) {
            LOG_DEBUG("UDS: Accept timeout after %ds", timeout_sec);
            return UDS_ERROR_RECEIVE_TIMEOUT;
        }
    }

    // Accept connection
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    LOG_DEBUG("UDS: Calling accept() on server fd %d", ctx->server_fd);
    ctx->client_fd = accept(ctx->server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (ctx->client_fd < 0) {
        LOG_ERROR("UDS: Failed to accept connection: %s", strerror(errno));
        return UDS_ERROR_ACCEPT_FAILED;
    }

    ctx->client_connected = true;
    LOG_INFO("UDS: Client connected (fd: %d)", ctx->client_fd);
    
    // Log client address if available
    if (client_addr.sun_family == AF_UNIX && client_addr.sun_path[0] != '\0') {
        LOG_DEBUG("UDS: Client connected from: %s", client_addr.sun_path);
    } else {
        LOG_DEBUG("UDS: Client connected (abstract socket or unnamed)"); 
    }

    return UDS_ERROR_NONE;
}

int uds_socket_send_receive(UDSContext *ctx, const char *message, size_t message_len,
                            char *response_buf, size_t response_buf_size) {
    if (!ctx || !message || !response_buf) {
        LOG_ERROR("UDS: Invalid parameters for send_receive");
        return UDS_ERROR_INVALID_PARAM;
    }

    if (!ctx->client_connected || ctx->client_fd < 0) {
        LOG_ERROR("UDS: No client connected");
        return UDS_ERROR_CONNECTION_CLOSED;
    }

    ctx->message_seq++;
    LOG_DEBUG("UDS: Sending message #%u (%zu bytes)", ctx->message_seq, message_len);

    // Send the message
    int result = send_framed_message(ctx->client_fd, message, message_len, ctx->timeout_sec);
    if (result != UDS_ERROR_NONE) {
        LOG_ERROR("UDS: Failed to send message #%u", ctx->message_seq);
        return result;
    }

    LOG_DEBUG("UDS: Message #%u sent, waiting for response", ctx->message_seq);

    // Wait for response (blocking)
    int response_len = receive_framed_message(ctx->client_fd, response_buf, response_buf_size,
                                               ctx->timeout_sec);
    if (response_len < 0) {
        LOG_ERROR("UDS: Failed to receive response for message #%u: %d", ctx->message_seq, response_len);

        // Handle connection closed
        if (response_len == UDS_ERROR_CONNECTION_CLOSED) {
            ctx->client_connected = false;
            close(ctx->client_fd);
            ctx->client_fd = -1;
        }

        return response_len;
    }

    LOG_DEBUG("UDS: Received response for message #%u (%d bytes)", ctx->message_seq, response_len);
    return response_len;
}

int uds_socket_send(UDSContext *ctx, const char *message, size_t message_len) {
    if (!ctx || !message) {
        LOG_ERROR("UDS: Invalid parameters for send");
        return UDS_ERROR_INVALID_PARAM;
    }

    if (!ctx->client_connected || ctx->client_fd < 0) {
        LOG_ERROR("UDS: No client connected");
        return UDS_ERROR_CONNECTION_CLOSED;
    }

    ctx->message_seq++;
    LOG_DEBUG("UDS: Sending message #%u (%zu bytes)", ctx->message_seq, message_len);

    int result = send_framed_message(ctx->client_fd, message, message_len, ctx->timeout_sec);
    if (result != UDS_ERROR_NONE) {
        LOG_ERROR("UDS: Failed to send message #%u", ctx->message_seq);
    }

    return result;
}

int uds_socket_receive(UDSContext *ctx, char *buffer, size_t buffer_size, int timeout_sec) {
    if (!ctx || !buffer || buffer_size == 0) {
        LOG_ERROR("UDS: Invalid parameters for receive");
        return UDS_ERROR_INVALID_PARAM;
    }

    if (!ctx->client_connected || ctx->client_fd < 0) {
        LOG_ERROR("UDS: No client connected");
        return UDS_ERROR_CONNECTION_CLOSED;
    }

    int actual_timeout = timeout_sec;
    if (actual_timeout == 0) {
        actual_timeout = ctx->timeout_sec;  // Use default from config
    }

    LOG_DEBUG("UDS: Waiting for message (timeout: %ds)", actual_timeout);

    int result = receive_framed_message(ctx->client_fd, buffer, buffer_size, actual_timeout);

    if (result < 0) {
        if (result == UDS_ERROR_CONNECTION_CLOSED) {
            LOG_INFO("UDS: Client disconnected");
            ctx->client_connected = false;
            close(ctx->client_fd);
            ctx->client_fd = -1;
        }
    }

    return result;
}

bool uds_socket_available(void) {
    return true;
}

int uds_socket_get_fd(UDSContext *ctx) {
    if (!ctx || !ctx->client_connected) {
        return -1;
    }
    return ctx->client_fd;
}

bool uds_socket_is_connected(UDSContext *ctx) {
    return ctx && ctx->client_connected && ctx->client_fd >= 0;
}

void uds_socket_disconnect_client(UDSContext *ctx) {
    if (!ctx) return;

    if (ctx->client_fd >= 0) {
        close(ctx->client_fd);
        ctx->client_fd = -1;
    }
    ctx->client_connected = false;
    LOG_INFO("UDS: Client disconnected");
}

// Helper: send JSON response
static int uds_send_json_response(UDSContext *ctx, const char *message_type, const char *content) {
    if (!ctx || !message_type) {
        return UDS_ERROR_INVALID_PARAM;
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        LOG_ERROR("UDS: Failed to create JSON object");
        return UDS_ERROR_NOMEM;
    }

    cJSON_AddStringToObject(json, "messageType", message_type);
    if (content) {
        cJSON_AddStringToObject(json, "content", content);
    }

    char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!json_str) {
        LOG_ERROR("UDS: Failed to serialize JSON");
        return UDS_ERROR_NOMEM;
    }

    int result = uds_socket_send(ctx, json_str, strlen(json_str));
    free(json_str);

    return result;
}

// Helper: populate tool result with error
static void uds_populate_tool_error(InternalContent *result, const char *tool_id,
                                    const char *tool_name, const char *error_msg) {
    result->type = INTERNAL_TOOL_RESPONSE;
    result->tool_id = strdup(tool_id ? tool_id : "unknown");
    result->tool_name = strdup(tool_name ? tool_name : "tool");
    result->tool_output = cJSON_CreateObject();
    cJSON_AddStringToObject(result->tool_output, "error", error_msg);
    result->is_error = 1;
}

// Helper: execute a single tool and populate result
// Note: input ownership is transferred to execute_tool
static void uds_execute_single_tool(ToolCall *tool, InternalContent *result,
                                    struct ConversationState *state) {
    LOG_INFO("UDS: Executing tool: %s (id: %s)", tool->name, tool->id);

    // Validate tool
    if (!is_tool_allowed(tool->name, state)) {
        LOG_ERROR("UDS: Tool '%s' not allowed", tool->name);
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg),
                 "ERROR: Tool '%s' does not exist or was not provided.", tool->name);
        uds_populate_tool_error(result, tool->id, tool->name, error_msg);
        return;
    }

    // Execute tool (input ownership transfers to execute_tool)
    cJSON *input = tool->parameters
        ? cJSON_Duplicate(tool->parameters, 1)
        : cJSON_CreateObject();

    cJSON *tool_result = execute_tool(tool->name, input, state);

    // Populate result structure
    result->type = INTERNAL_TOOL_RESPONSE;
    result->tool_id = strdup(tool->id);
    result->tool_name = strdup(tool->name);
    result->tool_output = tool_result;
    result->is_error = tool_result ? cJSON_HasObjectItem(tool_result, "error") : 1;
}

// Helper: process all tool calls from API response
// Returns 0 on success, -1 on failure
static int uds_process_tool_calls(UDSContext *ctx, struct ConversationState *state,
                                  ToolCall *tool_calls_array, int tool_count) {
    LOG_INFO("UDS: Processing %d tool call(s)", tool_count);

    InternalContent *results = calloc((size_t)tool_count, sizeof(InternalContent));
    if (!results) {
        LOG_ERROR("UDS: Failed to allocate tool result buffer");
        uds_send_json_response(ctx, "ERROR", "Failed to allocate tool result buffer");
        return -1;
    }

    for (int i = 0; i < tool_count; i++) {
        ToolCall *tool = &tool_calls_array[i];
        if (!tool->name || !tool->id) {
            LOG_WARN("UDS: Tool call missing name or id, skipping");
            uds_populate_tool_error(&results[i], tool->id, tool->name,
                                   "Tool call missing name or id");
            continue;
        }
        uds_execute_single_tool(tool, &results[i], state);
    }

    // Add tool results to conversation
    if (add_tool_results(state, results, tool_count) != 0) {
        LOG_ERROR("UDS: Failed to add tool results");
        uds_send_json_response(ctx, "ERROR", "Failed to add tool results");
        // Results were already freed by add_tool_results on failure
        return -1;
    }

    // Clean up results array only - contents now owned by conversation
    free(results);
    return 0;
}

// Helper: send assistant text response if present
static void uds_send_assistant_text(UDSContext *ctx, ApiResponse *api_response) {
    if (!api_response->message.text || api_response->message.text[0] == '\0') {
        return;
    }

    const char *p = api_response->message.text;
    while (*p && isspace((unsigned char)*p)) p++;

    if (*p != '\0') {
        LOG_INFO("UDS: Sending assistant text response");
        uds_send_json_response(ctx, "TEXT", p);
    }
}

// Helper: add assistant message to conversation history
static void uds_add_assistant_to_history(struct ConversationState *state,
                                         ApiResponse *api_response) {
    if (!api_response->raw_response) {
        return;
    }

    cJSON *choices = cJSON_GetObjectItem(api_response->raw_response, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (message) {
            add_assistant_message_openai(state, message);
        }
    }
}

// Process interactive message with AI
static int uds_process_interactive(UDSContext *ctx, struct ConversationState *state,
                                   const char *user_input) {
    if (!ctx || !state || !user_input) {
        LOG_ERROR("UDS: Invalid parameters for process_interactive");
        return -1;
    }

    LOG_INFO("UDS: Processing message: %.200s%s",
             user_input, strlen(user_input) > 200 ? "..." : "");

    add_user_message(state, user_input);

    int iteration = 0;
    const int MAX_ITERATIONS = 500;

    while (iteration < MAX_ITERATIONS) {
        iteration++;
        LOG_DEBUG("UDS: Interactive loop iteration %d", iteration);

        LOG_INFO("UDS: Calling AI API");
        ApiResponse *api_response = call_api_with_retries(state);

        if (!api_response) {
            LOG_ERROR("UDS: Failed to get response from AI API");
            uds_send_json_response(ctx, "ERROR", "AI inference failed");
            return -1;
        }

        if (api_response->error_message) {
            LOG_ERROR("UDS: AI API returned error: %s", api_response->error_message);
            uds_send_json_response(ctx, "ERROR", api_response->error_message);
            api_response_free(api_response);
            return -1;
        }

        uds_send_assistant_text(ctx, api_response);
        uds_add_assistant_to_history(state, api_response);

        // Process tool calls if present
        if (api_response->tool_count > 0) {
            int result = uds_process_tool_calls(ctx, state, api_response->tools,
                                                api_response->tool_count);
            api_response_free(api_response);
            if (result != 0) {
                return -1;
            }
            continue;
        }

        api_response_free(api_response);
        break;
    }

    if (iteration >= MAX_ITERATIONS) {
        LOG_WARN("UDS: Reached max iterations (%d)", MAX_ITERATIONS);
        uds_send_json_response(ctx, "ERROR", "Maximum iteration limit reached");
        return -1;
    }

    LOG_INFO("UDS: Interactive processing completed");
    return 0;
}

// Process incoming message from client
static int uds_process_message(UDSContext *ctx, struct ConversationState *state,
                               const char *buffer, int buffer_len) {
    if (!ctx || !state || !buffer || buffer_len <= 0) {
        LOG_ERROR("UDS: Invalid parameters for process_message");
        return -1;
    }

    LOG_INFO("UDS: Processing %d byte message", buffer_len);

    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        LOG_ERROR("UDS: Failed to parse JSON message");
        uds_send_json_response(ctx, "ERROR", "Invalid JSON");
        return -1;
    }

    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
    cJSON *content = cJSON_GetObjectItem(json, "content");

    if (!message_type || !cJSON_IsString(message_type)) {
        LOG_ERROR("UDS: Message missing messageType");
        uds_send_json_response(ctx, "ERROR", "Missing messageType");
        cJSON_Delete(json);
        return -1;
    }

    const char *type = message_type->valuestring;
    LOG_INFO("UDS: Message type: %s", type);

    if (strcmp(type, "TEXT") == 0) {
        if (!content || !cJSON_IsString(content)) {
            LOG_ERROR("UDS: TEXT message missing content");
            uds_send_json_response(ctx, "ERROR", "TEXT message missing content");
            cJSON_Delete(json);
            return -1;
        }

        int result = uds_process_interactive(ctx, state, content->valuestring);
        cJSON_Delete(json);
        return result;
    } else if (strcmp(type, "TERMINATE") == 0) {
        LOG_INFO("UDS: Received termination request");
        ctx->enabled = false;
        cJSON_Delete(json);
        return 0;
    } else {
        LOG_WARN("UDS: Unknown message type: %s", type);
        uds_send_json_response(ctx, "ERROR", "Unknown message type");
        cJSON_Delete(json);
        return -1;
    }
}

int uds_socket_daemon_mode(UDSContext *ctx, struct ConversationState *state) {
    if (!ctx || !state) {
        LOG_ERROR("UDS: Invalid parameters for daemon_mode");
        return -1;
    }

    LOG_INFO("UDS: =========================================");
    LOG_INFO("UDS: Starting Unix socket daemon mode");
    LOG_INFO("UDS: Socket path: %s", ctx->socket_path);
    LOG_INFO("UDS: Max retries: %d", ctx->max_retries);
    LOG_INFO("UDS: Timeout: %d seconds", ctx->timeout_sec);
    LOG_INFO("UDS: =========================================");

    int message_count = 0;
    int error_count = 0;

    while (ctx->enabled) {
        // Wait for client connection if not connected
        if (!ctx->client_connected) {
            LOG_INFO("UDS: Waiting for client connection...");
            int result = uds_socket_accept(ctx, 0);  // 0 = infinite timeout
            if (result != UDS_ERROR_NONE) {
                if (result == UDS_ERROR_RECEIVE_TIMEOUT) {
                    continue;
                }
                LOG_ERROR("UDS: Failed to accept client: %d", result);
                error_count++;
                if (error_count >= 10) {
                    LOG_ERROR("UDS: Too many errors, stopping daemon");
                    break;
                }
                continue;
            }
            error_count = 0;
        }

        // Receive message from client
        char buffer[UDS_BUFFER_SIZE];
        int received = uds_socket_receive(ctx, buffer, sizeof(buffer), ctx->timeout_sec);

        if (received < 0) {
            if (received == UDS_ERROR_CONNECTION_CLOSED) {
                LOG_INFO("UDS: Client disconnected, waiting for new connection");
                continue;
            }
            if (received == UDS_ERROR_RECEIVE_TIMEOUT) {
                // Timeout is ok, just keep waiting
                continue;
            }
            LOG_ERROR("UDS: Receive error: %d", received);
            error_count++;
            if (error_count >= 10) {
                LOG_ERROR("UDS: Too many errors, stopping daemon");
                break;
            }
            continue;
        }

        message_count++;
        error_count = 0;

        LOG_INFO("UDS: Processing message #%d", message_count);
        int result = uds_process_message(ctx, state, buffer, received);
        if (result != 0) {
            LOG_WARN("UDS: Message processing failed");
        }
    }

    LOG_INFO("UDS: =========================================");
    LOG_INFO("UDS: Daemon mode stopped");
    LOG_INFO("UDS: Total messages processed: %d", message_count);
    LOG_INFO("UDS: =========================================");

    return 0;
}
