/*
 * zmq_socket_stub.c - Stub implementation when ZMQ support is disabled
 */

#include "zmq_socket.h"
#include <stdbool.h>
#include <stdlib.h>
#include "cjson/cJSON.h"

ZMQContext* zmq_socket_init(const char *endpoint, int socket_type) {
    (void)endpoint;
    (void)socket_type;
    return NULL;
}

void zmq_socket_cleanup(ZMQContext *ctx) {
    (void)ctx;
}

int zmq_socket_send(ZMQContext *ctx, const char *message, size_t message_len) {
    (void)ctx;
    (void)message;
    (void)message_len;
    return -1;
}

int zmq_socket_receive(ZMQContext *ctx, char *buffer, size_t buffer_size, int timeout_ms) {
    (void)ctx;
    (void)buffer;
    (void)buffer_size;
    (void)timeout_ms;
    return ZMQ_ERROR_NOT_SUPPORTED;
}

int zmq_socket_process_message(ZMQContext *ctx, struct ConversationState *state, struct TUIState *tui) {
    (void)ctx;
    (void)state;
    (void)tui;
    return -1;
}

int zmq_socket_daemon_mode(ZMQContext *ctx, struct ConversationState *state) {
    (void)ctx;
    (void)state;
    return -1;
}

bool zmq_socket_available(void) {
    return false;
}

int zmq_send_tool_request(ZMQContext *ctx, const char *tool_name, const char *tool_id,
                          cJSON *tool_parameters) {
    (void)ctx;
    (void)tool_name;
    (void)tool_id;
    (void)tool_parameters;
    return -1;
}

int zmq_send_tool_result(ZMQContext *ctx, const char *tool_name, const char *tool_id,
                         cJSON *tool_output, int is_error) {
    (void)ctx;
    (void)tool_name;
    (void)tool_id;
    (void)tool_output;
    (void)is_error;
    return -1;
}