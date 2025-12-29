/*
 * test_zmq_socket.c - Test ZMQ socket functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/zmq_socket.h"
#ifdef HAVE_ZMQ
#include <zmq.h>
#endif

// Mock ConversationState for testing
typedef struct ConversationState {
    char *session_id;
} ConversationState;

int main(void) {
    printf("Testing ZMQ socket functionality...\n");

    // Test 1: Check if ZMQ is available
    bool available = zmq_socket_available();
    printf("ZMQ available: %s\n", available ? "yes" : "no");

    if (!available) {
        printf("ZMQ not compiled in, skipping tests\n");
        return 0;
    }

    // Test 2: Try to initialize ZMQ context with invalid endpoint
    // This should fail gracefully
    ZMQContext *ctx = zmq_socket_init(NULL, ZMQ_PAIR);
    if (ctx) {
        printf("ERROR: zmq_socket_init with NULL endpoint should return NULL\n");
        zmq_socket_cleanup(ctx);
        return 1;
    } else {
        printf("zmq_socket_init with NULL endpoint correctly returned NULL\n");
    }

    // Test 3: Test stub functions with NULL context
    ConversationState state = {0};
    state.session_id = strdup("test_session");

    int result = zmq_socket_process_message(NULL, &state, NULL);
    printf("zmq_socket_process_message returned: %d\n", result);

    result = zmq_socket_daemon_mode(NULL, &state);
    printf("zmq_socket_daemon_mode returned: %d\n", result);

    free(state.session_id);

    printf("All ZMQ socket tests passed!\n");
    return 0;
}
