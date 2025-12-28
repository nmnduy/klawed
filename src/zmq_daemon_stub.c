/*
 * zmq_daemon_stub.c - Stub implementation when ZMQ support is disabled
 */

#include "zmq_daemon.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

ZMQDaemonContext* zmq_daemon_init(const char *endpoint, struct ConversationState *conv_state) {
    (void)endpoint;
    (void)conv_state;
    return NULL;
}

int zmq_daemon_start(ZMQDaemonContext *ctx) {
    (void)ctx;
    return -1;
}

void zmq_daemon_stop(ZMQDaemonContext *ctx) {
    (void)ctx;
}

void zmq_daemon_cleanup(ZMQDaemonContext *ctx) {
    (void)ctx;
}

bool zmq_daemon_is_running(ZMQDaemonContext *ctx) {
    (void)ctx;
    return false;
}

void zmq_daemon_get_stats(ZMQDaemonContext *ctx, uint64_t *messages_received,
                          uint64_t *messages_sent, uint64_t *errors) {
    (void)ctx;
    if (messages_received) *messages_received = 0;
    if (messages_sent) *messages_sent = 0;
    if (errors) *errors = 0;
}

