/*
 * uds_socket.h - Unix Domain Socket utilities for Klawed
 *
 * Provides unified socket operations for IPC and streaming support.
 * Consolidates socket-related code from klawed.c and provider files.
 */

#ifndef UDS_SOCKET_H
#define UDS_SOCKET_H

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <cjson/cJSON.h>

#include "klawed_internal.h"

// Forward declarations
typedef struct ConversationState ConversationState;

// Socket IPC structure for managing Unix domain socket connections
typedef struct {
    int server_fd;           // Listening socket file descriptor
    int client_fd;           // Connected client file descriptor (-1 if none)
    char *socket_path;       // Path to Unix domain socket
    int enabled;             // Whether socket IPC is enabled
} SocketIPC;

// Socket streaming context for real-time response streaming
typedef struct {
    int client_fd;           // Socket client file descriptor to write to
    char *accumulated_text;  // Accumulated text from deltas
    size_t accumulated_size;
    size_t accumulated_capacity;
    int content_block_index; // Current content block being streamed
    char *content_block_type; // Type of current block ("text" or "tool_use")
    char *tool_use_id;       // Tool use ID for current block
    char *tool_use_name;     // Tool name for current block
    char *tool_input_json;   // Accumulated tool input JSON
    size_t tool_input_size;
    size_t tool_input_capacity;
    cJSON *message_start_data; // Message metadata from message_start
    char *stop_reason;       // Stop reason from message_delta
} SocketStreamingContext;

// Socket creation and management
int uds_create_unix_socket(const char *socket_path);
int uds_accept_connection(int server_fd);
void uds_cleanup(SocketIPC *socket_ipc);

// Socket I/O operations
int uds_read_input(int client_fd, char *buffer, size_t buffer_size);
int uds_write_output(int client_fd, const char *data, size_t data_len);
int uds_has_data(int fd);

// Socket event streaming
void uds_send_event(ConversationState *state, const char *event_type, cJSON *event_data);
void uds_send_error(int client_fd, const char *error_message);

// New socket message interface
int uds_send_api_response(int client_fd, cJSON *response);
int uds_send_tool_call(int client_fd, ToolCall *tools, int tool_count);
int uds_send_tool_result(int client_fd, const char *tool_call_id, const char *tool_name, cJSON *result);
int uds_send_final_response(int client_fd, const char *text_response);

// Socket streaming context management
void uds_streaming_context_init(SocketStreamingContext *ctx, int client_fd);
void uds_streaming_context_free(SocketStreamingContext *ctx);

// Helper functions
int uds_write_json(int client_fd, cJSON *json_obj);
int uds_check_connection(int client_fd);
int uds_send_ping(int client_fd);
int uds_handle_write_failure(int client_fd, SocketIPC *socket_ipc);

#endif // UDS_SOCKET_H
