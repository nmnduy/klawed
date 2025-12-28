/*
 * zmq_client_stub.c - Stub implementation when ZMQ support is disabled
 */

#include "zmq_client.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

ZMQClientContextThreaded* zmq_client_init(const char *endpoint) {
    (void)endpoint;
    return NULL;
}

int zmq_client_start(ZMQClientContextThreaded *ctx) {
    (void)ctx;
    return -1;
}

void zmq_client_stop(ZMQClientContextThreaded *ctx) {
    (void)ctx;
}

void zmq_client_cleanup(ZMQClientContextThreaded *ctx) {
    (void)ctx;
}

bool zmq_client_is_running(ZMQClientContextThreaded *ctx) {
    (void)ctx;
    return false;
}

int zmq_client_send_text(ZMQClientContextThreaded *ctx, const char *text) {
    (void)ctx;
    (void)text;
    return -1;
}

void zmq_client_get_stats(ZMQClientContextThreaded *ctx, uint64_t *messages_sent,
                          uint64_t *messages_received, uint64_t *errors) {
    (void)ctx;
    if (messages_sent) *messages_sent = 0;
    if (messages_received) *messages_received = 0;
    if (errors) *errors = 0;
}

void zmq_client_print_usage(const char *program_name) {
    (void)program_name;
}

int zmq_client_mode(const char *endpoint) {
    (void)endpoint;
    return -1;
}

int zmq_client_send_message_with_id(ZMQClientContextThreaded *ctx, const char *message,
                                    size_t message_len, char *out_id, size_t out_id_size) {
    (void)ctx;
    (void)message;
    (void)message_len;
    (void)out_id;
    (void)out_id_size;
    return -1;
}

int zmq_client_process_ack(ZMQClientContextThreaded *ctx, const char *message_id) {
    (void)ctx;
    (void)message_id;
    return -1;
}

int zmq_client_send_ack(ZMQClientContextThreaded *ctx, const char *message_id) {
    (void)ctx;
    (void)message_id;
    return -1;
}

int zmq_client_check_and_resend_pending(ZMQClientContextThreaded *ctx, int64_t current_time_ms) {
    (void)ctx;
    (void)current_time_ms;
    return -1;
}

void zmq_client_cleanup_pending_queue(ZMQClientContextThreaded *ctx) {
    (void)ctx;
}

int zmq_client_generate_message_id(ZMQClientContextThreaded *ctx, const char *message,
                                   size_t message_len, char *out_id, size_t out_id_size) {
    (void)ctx;
    (void)message;
    (void)message_len;
    (void)out_id;
    (void)out_id_size;
    return -1;
}

void zmq_client_process_message(ZMQClientContextThreaded *ctx, const char *response) {
    (void)ctx;
    (void)response;
}

int zmq_client_check_user_input(char *buffer, size_t buffer_size, int timeout_ms) {
    (void)buffer;
    (void)buffer_size;
    (void)timeout_ms;
    return -1;
}

