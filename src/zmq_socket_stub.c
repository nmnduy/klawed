/*
 * zmq_socket_stub.c - Stub implementation when ZMQ support is disabled
 */

#include "zmq_socket.h"
#include <stdbool.h>
#include <stdlib.h>

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
    return -1;
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