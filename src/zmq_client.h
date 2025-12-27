/*
 * zmq_client.h - ZMQ client implementation for Klawed
 *
 * Provides a standalone ZMQ client that can connect to a Klawed daemon
 * running with ZMQ enabled.
 *
 * Usage: ./klawed --zmq-client tcp://127.0.0.1:5555
 */

#ifndef ZMQ_CLIENT_H
#define ZMQ_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef HAVE_ZMQ
#include <zmq.h>

// Message ID constants (must match zmq_socket.h)
#define ZMQ_CLIENT_MESSAGE_ID_HEX_LENGTH 33  // 128 bits = 32 hex chars + null terminator

// Pending message waiting for ACK (client side)
typedef struct ZMQClientPendingMessage {
    char *message_id;           // Message ID (hex string)
    char *message_json;         // Full JSON message string
    int64_t sent_time_ms;       // Timestamp when message was sent (milliseconds)
    int retry_count;            // Number of retries attempted
    struct ZMQClientPendingMessage *next;
} ZMQClientPendingMessage;

// Pending message queue (client side)
typedef struct ZMQClientPendingQueue {
    ZMQClientPendingMessage *head;
    ZMQClientPendingMessage *tail;
    int count;
    int max_pending;             // Maximum number of pending messages
    int64_t timeout_ms;         // Time before resend (milliseconds)
    int max_retries;            // Maximum number of retries before giving up
} ZMQClientPendingQueue;

// ZMQ client connection state
typedef struct {
    void *context;
    void *socket;
    char *endpoint;
    int is_connected;

    // Message ID/ACK system
    ZMQClientPendingQueue pending_queue;   // Queue of messages waiting for ACK
    uint32_t salt;                         // Random salt for message ID generation
    int message_sequence;                  // Simple counter for debugging
} ZMQClientConnectionState;

// Function prototypes for ZMQ client
void zmq_client_print_usage(const char *program_name);
int zmq_client_initialize_connection(ZMQClientConnectionState *conn, const char *endpoint);
void zmq_client_cleanup_connection(ZMQClientConnectionState *conn);
int zmq_client_send_message(ZMQClientConnectionState *conn, const char *message);
int zmq_client_receive_message(ZMQClientConnectionState *conn, char *buffer, size_t buffer_size, int timeout_ms);
void zmq_client_process_message(ZMQClientConnectionState *conn, const char *response);
void zmq_client_send_text_message(ZMQClientConnectionState *conn, const char *text);
int zmq_client_mode(const char *endpoint);
int zmq_client_check_user_input(char *buffer, size_t buffer_size, int timeout_ms);

// Reliability functions
int zmq_client_send_message_with_id(ZMQClientConnectionState *conn, const char *message,
                                   char *message_id_out, size_t message_id_out_size);
int zmq_client_send_ack(ZMQClientConnectionState *conn, const char *message_id);
int zmq_client_process_ack(ZMQClientConnectionState *conn, const char *message_id);
int zmq_client_check_and_resend_pending(ZMQClientConnectionState *conn, int64_t current_time_ms);
void zmq_client_cleanup_pending_queue(ZMQClientConnectionState *conn);
int zmq_client_generate_message_id(ZMQClientConnectionState *conn, const char *message,
                                  size_t message_len, char *out_id, size_t out_id_size);

#endif // HAVE_ZMQ

#endif // ZMQ_CLIENT_H
