/*
 * uds_socket_stub.c - Stub implementation when Unix socket support is disabled
 */

#include "uds_socket.h"
#include <stdbool.h>
#include <stdlib.h>

UDSContext* uds_socket_init(const char *socket_path) {
    (void)socket_path;
    return NULL;
}

void uds_socket_cleanup(UDSContext *ctx) {
    (void)ctx;
}

int uds_socket_accept(UDSContext *ctx, int timeout_sec) {
    (void)ctx;
    (void)timeout_sec;
    return UDS_ERROR_NOT_SUPPORTED;
}

int uds_socket_send_receive(UDSContext *ctx, const char *message, size_t message_len,
                            char *response_buf, size_t response_buf_size) {
    (void)ctx;
    (void)message;
    (void)message_len;
    (void)response_buf;
    (void)response_buf_size;
    return UDS_ERROR_NOT_SUPPORTED;
}

int uds_socket_send(UDSContext *ctx, const char *message, size_t message_len) {
    (void)ctx;
    (void)message;
    (void)message_len;
    return UDS_ERROR_NOT_SUPPORTED;
}

int uds_socket_receive(UDSContext *ctx, char *buffer, size_t buffer_size, int timeout_sec) {
    (void)ctx;
    (void)buffer;
    (void)buffer_size;
    (void)timeout_sec;
    return UDS_ERROR_NOT_SUPPORTED;
}

bool uds_socket_available(void) {
    return false;
}

int uds_socket_daemon_mode(UDSContext *ctx, struct ConversationState *state) {
    (void)ctx;
    (void)state;
    return -1;
}

int uds_socket_get_fd(UDSContext *ctx) {
    (void)ctx;
    return -1;
}

bool uds_socket_is_connected(UDSContext *ctx) {
    (void)ctx;
    return false;
}

void uds_socket_disconnect_client(UDSContext *ctx) {
    (void)ctx;
}
