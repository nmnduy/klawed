/*
 * zmq_client.c - ZMQ client implementation for Klawed
 *
 * Provides a standalone ZMQ client that can connect to a Klawed daemon
 * running with ZMQ enabled.
 *
 * Usage: ./klawed --zmq-client tcp://127.0.0.1:5555
 */

#ifdef HAVE_ZMQ
#include "zmq_client.h"
#include "logger.h"
#include <cjson/cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <bsd/string.h>

// ZMQ client configuration
#define ZMQ_CLIENT_BUFFER_SIZE 65536
#define ZMQ_CLIENT_DEFAULT_TIMEOUT_MS 120000 // 2 minutes

// Pending queue defaults (client side)
#define ZMQ_CLIENT_DEFAULT_MAX_PENDING 50
#define ZMQ_CLIENT_DEFAULT_ACK_TIMEOUT_MS 3000    // 3 seconds
#define ZMQ_CLIENT_DEFAULT_MAX_RETRIES 5

// Message ID constants
#define ZMQ_CLIENT_MESSAGE_ID_HEX_LENGTH 33  // 128 bits = 32 hex chars + null terminator
#define ZMQ_CLIENT_HASH_SAMPLE_SIZE 256      // Number of characters to sample from message for hash

// Forward declarations for static helper functions
static int64_t get_current_time_ms_client(void);
static void init_pending_queue_client(ZMQClientConnectionState *conn);
static void free_pending_message_client(ZMQClientPendingMessage *msg);
static int add_to_pending_queue_client(ZMQClientConnectionState *conn, const char *message_id,
                                       const char *message_json, int64_t sent_time_ms);
static int remove_from_pending_queue_client(ZMQClientConnectionState *conn, const char *message_id);
static void hash_message_id_client(int64_t timestamp, const char *message, size_t message_len,
                                   uint32_t salt, uint8_t out_hash[16]);
static void hash_to_hex_client(const uint8_t hash[16], char *out_hex, size_t out_hex_size);

/**
 * Get current time in milliseconds
 */
static int64_t get_current_time_ms_client(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
}

void zmq_client_print_usage(const char *program_name) {
    printf("Usage: %s <endpoint> [timeout_ms]\n", program_name);
    printf("Example: %s tcp://127.0.0.1:5555\n", program_name);
    printf("\nAvailable endpoints:\n");
    printf("  tcp://127.0.0.1:5555  - TCP socket on localhost port 5555\n");
    printf("  ipc:///tmp/klawed.sock - IPC socket file\n");
    printf("\nMessage types displayed:\n");
    printf("  TEXT          - AI text responses\n");
    printf("  TOOL          - Tool execution requests\n");
    printf("  TOOL_RESULT   - Tool execution results\n");
    printf("  ERROR         - Error messages\n");
    printf("\nCommands:\n");
    printf("  /help      - Show this help\n");
    printf("  /quit      - Exit the program\n");
    printf("  /interrupt - Cancel current conversation (when waiting for response)\n");
}

int zmq_client_initialize_connection(ZMQClientConnectionState *conn, const char *endpoint) {
    if (!conn || !endpoint) {
        LOG_ERROR("ZMQ Client: Invalid parameters (conn=%p, endpoint=%p)", (void*)conn, (const void*)endpoint);
        return 0;
    }

    LOG_INFO("ZMQ Client: Initializing connection to %s", endpoint);

    // Clean up any existing connection
    zmq_client_cleanup_connection(conn);

    // Initialize ZMQ context
    LOG_DEBUG("ZMQ Client: Creating ZMQ context");
    conn->context = zmq_ctx_new();
    if (!conn->context) {
        LOG_ERROR("ZMQ Client: Error creating ZMQ context: %s", zmq_strerror(errno));
        return 0;
    }
    LOG_DEBUG("ZMQ Client: ZMQ context created successfully");

    // Create PAIR socket (peer-to-peer communication)
    LOG_DEBUG("ZMQ Client: Creating ZMQ PAIR socket");
    conn->socket = zmq_socket(conn->context, ZMQ_PAIR);
    if (!conn->socket) {
        LOG_ERROR("ZMQ Client: Error creating ZMQ socket: %s", zmq_strerror(errno));
        zmq_ctx_term(conn->context);
        conn->context = NULL;
        return 0;
    }
    LOG_DEBUG("ZMQ Client: ZMQ PAIR socket created successfully");

    // Set linger option for clean shutdown
    int linger = 1000; // 1 second
    zmq_setsockopt(conn->socket, ZMQ_LINGER, &linger, sizeof(linger));
    LOG_DEBUG("ZMQ Client: Set ZMQ_LINGER to %d ms", linger);

    // Connect to endpoint
    LOG_INFO("ZMQ Client: Connecting to %s...", endpoint);
    int rc = zmq_connect(conn->socket, endpoint);
    if (rc != 0) {
        LOG_ERROR("ZMQ Client: Error connecting to %s: %s", endpoint, zmq_strerror(errno));
        zmq_close(conn->socket);
        zmq_ctx_term(conn->context);
        conn->socket = NULL;
        conn->context = NULL;
        return 0;
    }

    conn->endpoint = strdup(endpoint);
    conn->is_connected = 1;

    // Initialize pending queue and message ID system
    init_pending_queue_client(conn);

    // Generate random salt for message ID generation
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        size_t n = fread(&conn->salt, sizeof(conn->salt), 1, urandom);
        if (n != 1) {
            // Fallback to time-based salt
            conn->salt = (uint32_t)get_current_time_ms_client();
        }
        fclose(urandom);
    } else {
        // Fallback to time-based salt
        conn->salt = (uint32_t)get_current_time_ms_client();
    }

    conn->message_sequence = 0;

    LOG_INFO("ZMQ Client: Successfully connected to %s", endpoint);
    LOG_DEBUG("ZMQ Client: Connection state: context=%p, socket=%p, endpoint=%s, is_connected=%d",
              (void*)conn->context, (void*)conn->socket, conn->endpoint, conn->is_connected);
    return 1;
}

void zmq_client_cleanup_connection(ZMQClientConnectionState *conn) {
    if (!conn) {
        return;
    }

    LOG_DEBUG("ZMQ Client: Cleaning up connection");

    // Clean up pending message queue
    zmq_client_cleanup_pending_queue(conn);

    // Close socket
    if (conn->socket) {
        LOG_DEBUG("ZMQ Client: Closing socket");
        zmq_close(conn->socket);
        conn->socket = NULL;
    }

    // Terminate context
    if (conn->context) {
        LOG_DEBUG("ZMQ Client: Terminating context");
        zmq_ctx_term(conn->context);
        conn->context = NULL;
    }

    // Free endpoint string
    if (conn->endpoint) {
        LOG_DEBUG("ZMQ Client: Freeing endpoint");
        free(conn->endpoint);
        conn->endpoint = NULL;
    }

    conn->is_connected = 0;

    LOG_DEBUG("ZMQ Client: Connection cleaned up");
}

/**
 * Simple hash function combining timestamp, message sample, and salt
 * Uses FNV-1a-like algorithm with additional mixing
 */
static void hash_message_id_client(int64_t timestamp, const char *message, size_t message_len,
                                   uint32_t salt, uint8_t out_hash[16]) {
    // Combine timestamp and salt into 128-bit seed
    uint64_t seed1 = (uint64_t)timestamp ^ ((uint64_t)salt << 32);
    uint64_t seed2 = (uint64_t)timestamp * (uint64_t)salt;

    // Initialize with seeds
    uint64_t h1 = seed1;
    uint64_t h2 = seed2;

    // Sample first HASH_SAMPLE_SIZE characters (or entire message if shorter)
    size_t sample_len = message_len < ZMQ_CLIENT_HASH_SAMPLE_SIZE ?
                        message_len : ZMQ_CLIENT_HASH_SAMPLE_SIZE;

    // Process message sample
    for (size_t i = 0; i < sample_len; i++) {
        uint8_t c = (uint8_t)message[i];

        // FNV-1a style mixing on both 64-bit parts
        h1 ^= (uint64_t)c;
        h1 *= 1099511628211ULL;  // FNV prime

        // Mix into second half with different pattern
        h2 ^= (uint64_t)c << (i % 8);
        h2 *= 16777619ULL;  // Smaller FNV prime
    }

    // Add some additional mixing using salt
    h1 ^= salt;
    h2 ^= salt << 16;

    // Final mixing
    h1 ^= h1 >> 33;
    h1 *= 0xff51afd7ed558ccdULL;
    h1 ^= h1 >> 33;

    h2 ^= h2 >> 33;
    h2 *= 0xc4ceb9fe1a85ec53ULL;
    h2 ^= h2 >> 33;

    // Combine into 128-bit output
    memcpy(&out_hash[0], &h1, 8);
    memcpy(&out_hash[8], &h2, 8);
}

/**
 * Convert 128-bit hash to hex string
 */
static void hash_to_hex_client(const uint8_t hash[16], char *out_hex, size_t out_hex_size) {
    if (out_hex_size < ZMQ_CLIENT_MESSAGE_ID_HEX_LENGTH) {
        return;
    }

    const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out_hex[i * 2] = hex_chars[hash[i] >> 4];
        out_hex[i * 2 + 1] = hex_chars[hash[i] & 0x0f];
    }
    out_hex[32] = '\0';
}

/**
 * Initialize pending message queue
 */
static void init_pending_queue_client(ZMQClientConnectionState *conn) {
    conn->pending_queue.head = NULL;
    conn->pending_queue.tail = NULL;
    conn->pending_queue.count = 0;
    conn->pending_queue.max_pending = ZMQ_CLIENT_DEFAULT_MAX_PENDING;
    conn->pending_queue.timeout_ms = ZMQ_CLIENT_DEFAULT_ACK_TIMEOUT_MS;
    conn->pending_queue.max_retries = ZMQ_CLIENT_DEFAULT_MAX_RETRIES;
}

/**
 * Free a pending message node
 */
static void free_pending_message_client(ZMQClientPendingMessage *msg) {
    if (!msg) return;

    if (msg->message_id) {
        free(msg->message_id);
    }
    if (msg->message_json) {
        free(msg->message_json);
    }
    free(msg);
}

/**
 * Add message to pending queue
 */
static int add_to_pending_queue_client(ZMQClientConnectionState *conn, const char *message_id,
                                        const char *message_json, int64_t sent_time_ms) {
    // Check if queue is full
    if (conn->pending_queue.count >= conn->pending_queue.max_pending) {
        LOG_WARN("ZMQ Client: Pending queue full (max %d), cannot add message",
                 conn->pending_queue.max_pending);
        return -1;
    }

    ZMQClientPendingMessage *pending = calloc(1, sizeof(ZMQClientPendingMessage));
    if (!pending) {
        LOG_ERROR("ZMQ Client: Failed to allocate pending message");
        return -1;
    }

    pending->message_id = strdup(message_id);
    if (!pending->message_id) {
        free(pending);
        return -1;
    }

    pending->message_json = strdup(message_json);
    if (!pending->message_json) {
        free(pending->message_id);
        free(pending);
        return -1;
    }

    pending->sent_time_ms = sent_time_ms;
    pending->retry_count = 0;
    pending->next = NULL;

    // Add to tail of queue
    if (conn->pending_queue.tail) {
        conn->pending_queue.tail->next = pending;
    }
    conn->pending_queue.tail = pending;

    if (!conn->pending_queue.head) {
        conn->pending_queue.head = pending;
    }

    conn->pending_queue.count++;

    LOG_DEBUG("ZMQ Client: Added message %s to pending queue (count: %d)",
              message_id, conn->pending_queue.count);

    return 0;
}

/**
 * Remove message from pending queue by ID
 */
static int remove_from_pending_queue_client(ZMQClientConnectionState *conn, const char *message_id) {
    ZMQClientPendingMessage *prev = NULL;
    ZMQClientPendingMessage *curr = conn->pending_queue.head;

    while (curr) {
        if (strcmp(curr->message_id, message_id) == 0) {
            // Found it, remove from list
            if (prev) {
                prev->next = curr->next;
            } else {
                conn->pending_queue.head = curr->next;
            }

            if (curr == conn->pending_queue.tail) {
                conn->pending_queue.tail = prev;
            }

            conn->pending_queue.count--;
            LOG_DEBUG("ZMQ Client: Removed message %s from pending queue (count: %d)",
                      message_id, conn->pending_queue.count);

            free_pending_message_client(curr);
            return 0;
        }

        prev = curr;
        curr = curr->next;
    }

    LOG_WARN("ZMQ Client: Message %s not found in pending queue", message_id);
    return -1;
}

/**
 * Generate a unique message ID based on timestamp, partial content, and random salt
 */
int zmq_client_generate_message_id(ZMQClientConnectionState *conn, const char *message,
                                    size_t message_len, char *out_id, size_t out_id_size) {
    if (!conn || !message || !out_id || out_id_size < ZMQ_CLIENT_MESSAGE_ID_HEX_LENGTH) {
        return -1;
    }

    // Get current timestamp
    int64_t timestamp_ms = get_current_time_ms_client();

    // Generate 128-bit hash
    uint8_t hash[16];
    hash_message_id_client(timestamp_ms, message, message_len, conn->salt, hash);

    // Convert to hex string
    hash_to_hex_client(hash, out_id, out_id_size);

    conn->message_sequence++;
    LOG_INFO("ZMQ Client: Generated message ID %s (seq: %d, ts: %lld, salt: 0x%08x)",
              out_id, conn->message_sequence, (long long)timestamp_ms, conn->salt);

    return 0;
}

/**
 * Send message with ID and track for ACK
 */
int zmq_client_send_message_with_id(ZMQClientConnectionState *conn, const char *message,
                                     char *message_id_out, size_t message_id_out_size) {
    if (!conn || !message) {
        LOG_ERROR("ZMQ Client: Invalid parameters for send_with_id");
        return -1;
    }

    if (!conn->socket) {
        LOG_ERROR("ZMQ Client: No socket available for send");
        return -1;
    }

    // Generate message ID
    char message_id[ZMQ_CLIENT_MESSAGE_ID_HEX_LENGTH];
    if (zmq_client_generate_message_id(conn, message, strlen(message),
                                        message_id, sizeof(message_id)) != 0) {
        LOG_ERROR("ZMQ Client: Failed to generate message ID for %zu byte message", strlen(message));
        return -1;
    }

    LOG_DEBUG("ZMQ Client: Wrapping message with ID %s", message_id);

    // Wrap message with ID field
    cJSON *json = cJSON_Parse(message);
    if (!json) {
        LOG_ERROR("ZMQ Client: Failed to parse message JSON for wrapping (message preview: %.*s)",
                 (int)(strlen(message) > 100 ? 100 : strlen(message)), message);
        return -1;
    }

    // Add messageId field
    cJSON_AddStringToObject(json, "messageId", message_id);

    char *wrapped_message = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!wrapped_message) {
        LOG_ERROR("ZMQ Client: Failed to serialize wrapped message with ID %s", message_id);
        return -1;
    }

    size_t wrapped_len = strlen(wrapped_message);
    LOG_DEBUG("ZMQ Client: Wrapped message length: %zu bytes (original: %zu bytes)",
              wrapped_len, strlen(message));

    // Send the wrapped message
    int rc = zmq_send(conn->socket, wrapped_message, wrapped_len, 0);

    if (rc >= 0) {
        LOG_INFO("ZMQ Client: Sent message %s (%zu bytes -> %d bytes sent, pending queue: %d/%d)",
                  message_id, wrapped_len, rc, conn->pending_queue.count + 1,
                  conn->pending_queue.max_pending);

        // Add to pending queue
        int64_t sent_time = get_current_time_ms_client();
        if (add_to_pending_queue_client(conn, message_id, wrapped_message, sent_time) == 0) {
            LOG_DEBUG("ZMQ Client: Message %s added to pending queue at time %lld ms",
                     message_id, (long long)sent_time);
            // Return message ID to caller if requested
            if (message_id_out && message_id_out_size >= ZMQ_CLIENT_MESSAGE_ID_HEX_LENGTH) {
                strlcpy(message_id_out, message_id, message_id_out_size);
                LOG_DEBUG("ZMQ Client: Returned message ID to caller: %s", message_id);
            }
            free(wrapped_message);
            return 0;
        } else {
            LOG_WARN("ZMQ Client: Message %s sent but failed to track in pending queue (queue full: %d/%d)",
                    message_id, conn->pending_queue.count, conn->pending_queue.max_pending);
            free(wrapped_message);
            return -1;
        }
    } else {
        int err = errno;
        LOG_ERROR("ZMQ Client: Failed to send message %s: %s (wrapped length: %zu)",
                 message_id, zmq_strerror(err), wrapped_len);
        free(wrapped_message);
        return -1;
    }
}

/**
 * Send ACK for a received message
 */
int zmq_client_send_ack(ZMQClientConnectionState *conn, const char *message_id) {
    if (!conn || !message_id) {
        LOG_ERROR("ZMQ Client: Invalid parameters for send_ack (conn=%p, message_id=%p)",
                  (void*)conn, (const void*)message_id);
        return -1;
    }

    if (!conn->socket) {
        LOG_ERROR("ZMQ Client: No socket available for sending ACK for message %s", message_id);
        return -1;
    }

    LOG_DEBUG("ZMQ Client: Creating ACK for message %s", message_id);

    // Create ACK message
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        LOG_ERROR("ZMQ Client: Failed to create JSON object for ACK for message %s", message_id);
        return -1;
    }

    cJSON_AddStringToObject(json, "messageType", "ACK");
    cJSON_AddStringToObject(json, "messageId", message_id);

    char *ack_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!ack_str) {
        LOG_ERROR("ZMQ Client: Failed to serialize ACK JSON for message %s", message_id);
        return -1;
    }

    size_t ack_len = strlen(ack_str);
    LOG_DEBUG("ZMQ Client: ACK message length: %zu bytes", ack_len);

    int rc = zmq_send(conn->socket, ack_str, ack_len, 0);
    free(ack_str);

    if (rc >= 0) {
        LOG_INFO("ZMQ Client: Sent ACK for message %s (%d bytes)", message_id, rc);
        return 0;
    } else {
        int err = errno;
        LOG_ERROR("ZMQ Client: Failed to send ACK for message %s: %s", message_id, zmq_strerror(err));
        return -1;
    }
}

/**
 * Process ACK message and remove from pending queue
 */
int zmq_client_process_ack(ZMQClientConnectionState *conn, const char *message_id) {
    if (!conn || !message_id) {
        LOG_ERROR("ZMQ Client: Invalid parameters for process_ack (conn=%p, message_id=%p)",
                  (void*)conn, (const void*)message_id);
        return -1;
    }

    LOG_DEBUG("ZMQ Client: Processing ACK for message %s (pending queue size: %d)",
              message_id, conn->pending_queue.count);

    int result = remove_from_pending_queue_client(conn, message_id);
    if (result == 0) {
        LOG_INFO("ZMQ Client: Successfully processed ACK for message %s (pending queue size: %d)",
                 message_id, conn->pending_queue.count);
    } else {
        LOG_WARN("ZMQ Client: Failed to process ACK for message %s (not found in pending queue)",
                 message_id);
    }

    return result;
}

/**
 * Check and resend pending messages that have timed out
 */
int zmq_client_check_and_resend_pending(ZMQClientConnectionState *conn, int64_t current_time_ms) {
    if (!conn) {
        return -1;
    }

    if (!conn->pending_queue.head) {
        return 0; // No pending messages
    }

    int resent_count = 0;
    ZMQClientPendingMessage *curr = conn->pending_queue.head;
    ZMQClientPendingMessage *prev = NULL;

    while (curr) {
        int64_t elapsed = current_time_ms - curr->sent_time_ms;

        if (elapsed >= conn->pending_queue.timeout_ms) {
            // Timeout exceeded, check retry count
            if (curr->retry_count >= conn->pending_queue.max_retries) {
                // Max retries exceeded, give up and remove
                LOG_ERROR("ZMQ Client: Message %s exceeded max retries (%d), dropping",
                         curr->message_id, conn->pending_queue.max_retries);

                ZMQClientPendingMessage *to_delete = curr;
                if (prev) {
                    prev->next = curr->next;
                    curr = curr->next;
                } else {
                    conn->pending_queue.head = curr->next;
                    curr = curr->next;
                }

                if (to_delete == conn->pending_queue.tail) {
                    conn->pending_queue.tail = prev;
                }

                conn->pending_queue.count--;
                free_pending_message_client(to_delete);
                continue;
            }

            // Retry sending
            curr->retry_count++;
            curr->sent_time_ms = current_time_ms;

            int rc = zmq_send(conn->socket, curr->message_json,
                             strlen(curr->message_json), 0);

            if (rc >= 0) {
                LOG_INFO("ZMQ Client: Resent message %s (attempt %d/%d)",
                        curr->message_id, curr->retry_count, conn->pending_queue.max_retries);
                resent_count++;
            } else {
                LOG_ERROR("ZMQ Client: Failed to resend message %s", curr->message_id);
            }
        }

        prev = curr;
        curr = curr->next;
    }

    return resent_count;
}

/**
 * Clean up pending message queue
 */
void zmq_client_cleanup_pending_queue(ZMQClientConnectionState *conn) {
    if (!conn) return;

    ZMQClientPendingMessage *curr = conn->pending_queue.head;
    while (curr) {
        ZMQClientPendingMessage *next = curr->next;
        free_pending_message_client(curr);
        curr = next;
    }
    conn->pending_queue.head = NULL;
    conn->pending_queue.tail = NULL;
    conn->pending_queue.count = 0;

    LOG_DEBUG("ZMQ Client: Pending queue cleaned up");
}

int zmq_client_send_message(ZMQClientConnectionState *conn, const char *message) {
    if (!conn || !conn->is_connected || !conn->socket || !message) {
        LOG_ERROR("ZMQ Client: Cannot send: connection not ready (conn=%p, is_connected=%d, socket=%p, message=%p)",
                  (void*)conn, conn ? conn->is_connected : 0, (void*)(conn ? conn->socket : NULL), (const void*)message);
        return -1;
    }

    size_t message_len = strlen(message);
    LOG_DEBUG("ZMQ Client: Sending %zu bytes to %s", message_len, conn->endpoint);

    // Log message preview (first 200 chars)
    if (message_len > 0) {
        int preview_len = (int)(message_len > 200 ? 200 : message_len);
        LOG_DEBUG("ZMQ Client: Message preview: %.*s%s",
                 preview_len, message,
                 message_len > 200 ? "..." : "");
    }

    int rc = zmq_send(conn->socket, message, message_len, 0);
    if (rc < 0) {
        LOG_ERROR("ZMQ Client: Error sending message: %s", zmq_strerror(errno));
        return -1;
    }

    LOG_INFO("ZMQ Client: Successfully sent %d bytes", rc);
    return rc;
}
int zmq_client_receive_message(ZMQClientConnectionState *conn, char *buffer, size_t buffer_size, int timeout_ms) {
    if (!conn || !conn->is_connected || !conn->socket || !buffer) {
        LOG_ERROR("ZMQ Client: Cannot receive: connection not ready (conn=%p, is_connected=%d, socket=%p, buffer=%p)",
                  (void*)conn, conn ? conn->is_connected : 0, (void*)(conn ? conn->socket : NULL), (void*)buffer);
        return -1;
    }

    LOG_DEBUG("ZMQ Client: Waiting for message from %s (timeout: %d ms, buffer size: %zu)",
              conn->endpoint, timeout_ms, buffer_size);

    // Set receive timeout
    zmq_setsockopt(conn->socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));

    int rc = zmq_recv(conn->socket, buffer, buffer_size - 1, 0);
    if (rc < 0) {
        if (errno == EAGAIN) {
            LOG_DEBUG("ZMQ Client: Receive timeout after %d ms (no message available)", timeout_ms);
        } else {
            LOG_ERROR("ZMQ Client: Error receiving message: %s", zmq_strerror(errno));
        }
        return -1;
    }

    buffer[rc] = '\0';
    LOG_INFO("ZMQ Client: Received %d bytes from %s", rc, conn->endpoint);

    // Log message preview (first 200 chars)
    if (rc > 0) {
        int preview_len = rc > 200 ? 200 : rc;
        LOG_DEBUG("ZMQ Client: Message preview: %.*s%s",
                 preview_len, buffer,
                 rc > 200 ? "..." : "");
    }

    return rc;
}

void zmq_client_process_message(ZMQClientConnectionState *conn, const char *response) {
    if (!response) {
        LOG_WARN("ZMQ Client: Received NULL message");
        return;
    }

    size_t response_len = strlen(response);
    LOG_DEBUG("ZMQ Client: Processing message (length: %zu)", response_len);

    // Log raw response preview
    if (response_len > 0) {
        int preview_len = (int)(response_len > 200 ? 200 : response_len);
        LOG_DEBUG("ZMQ Client: Raw message preview: %.*s%s",
                 preview_len, response,
                 response_len > 200 ? "..." : "");
    }

    cJSON *json = cJSON_Parse(response);
    if (!json) {
        LOG_WARN("ZMQ Client: Failed to parse JSON response");
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            LOG_DEBUG("ZMQ Client: JSON error near: %s", error_ptr);
        }
        printf("Response (raw): %s\n", response);
        return;
    }

    LOG_DEBUG("ZMQ Client: Successfully parsed JSON");

    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
    cJSON *message_id = cJSON_GetObjectItem(json, "messageId");

    if (!message_type || !cJSON_IsString(message_type)) {
        LOG_ERROR("ZMQ Client: Invalid response format (missing messageType)");
        cJSON_Delete(json);
        return;
    }

    const char *msg_type = message_type->valuestring;
    LOG_DEBUG("ZMQ Client: Message type: %s", msg_type);

    // Extract message ID if present
    const char *msg_id = NULL;
    if (message_id && cJSON_IsString(message_id)) {
        msg_id = message_id->valuestring;
        LOG_DEBUG("ZMQ Client: Message ID: %s", msg_id);
    }

    // Handle ACK messages
    if (strcmp(msg_type, "ACK") == 0) {
        if (msg_id) {
            LOG_INFO("ZMQ Client: Received ACK for message %s", msg_id);
            if (conn) {
                zmq_client_process_ack(conn, msg_id);
            }
        } else {
            LOG_WARN("ZMQ Client: ACK message missing messageId");
        }
        cJSON_Delete(json);
        return;
    }

    // Send ACK for all non-ACK messages that have a message ID
    if (conn && msg_id) {
        LOG_DEBUG("ZMQ Client: Sending ACK for message %s", msg_id);
        zmq_client_send_ack(conn, msg_id);
    } else {
        LOG_DEBUG("ZMQ Client: No message ID to ACK or no connection available");
    }

    if (strcmp(msg_type, "TEXT") == 0) {
        cJSON *content = cJSON_GetObjectItem(json, "content");
        if (content && cJSON_IsString(content)) {
            printf("\n=== AI Response ===\n%s\n=== End of AI Response ===\n",
                   content->valuestring);
        } else {
            printf("TEXT message missing content\n");
        }
    }
    else if (strcmp(msg_type, "TOOL") == 0) {
        cJSON *tool_name = cJSON_GetObjectItem(json, "toolName");
        cJSON *tool_id = cJSON_GetObjectItem(json, "toolId");
        cJSON *tool_params = cJSON_GetObjectItem(json, "toolParameters");

        printf("\n=== TOOL Execution Request ===\n");
        if (tool_name && cJSON_IsString(tool_name)) {
            printf("Tool: %s\n", tool_name->valuestring);
        }
        if (tool_id && cJSON_IsString(tool_id)) {
            printf("ID: %s\n", tool_id->valuestring);
        }
        if (tool_params) {
            char *params_str = cJSON_Print(tool_params);
            printf("Parameters: %s\n", params_str);
            free(params_str);
        }
        printf("=== Tool will be executed ===\n");
    }
    else if (strcmp(msg_type, "TOOL_RESULT") == 0) {
        cJSON *tool_name = cJSON_GetObjectItem(json, "toolName");
        cJSON *tool_id = cJSON_GetObjectItem(json, "toolId");
        cJSON *tool_output = cJSON_GetObjectItem(json, "toolOutput");
        cJSON *is_error = cJSON_GetObjectItem(json, "isError");

        printf("\n=== TOOL Execution Result ===\n");
        if (tool_name && cJSON_IsString(tool_name)) {
            printf("Tool: %s\n", tool_name->valuestring);
        }
        if (tool_id && cJSON_IsString(tool_id)) {
            printf("ID: %s\n", tool_id->valuestring);
        }
        if (is_error && cJSON_IsBool(is_error)) {
            printf("Status: %s\n", is_error->valueint ? "ERROR" : "SUCCESS");
        }
        if (tool_output) {
            char *output_str = cJSON_Print(tool_output);
            printf("Output: %s\n", output_str);
            free(output_str);
        }
        printf("=== End of Tool Result ===\n");
    }
    else if (strcmp(msg_type, "ERROR") == 0) {
        cJSON *content = cJSON_GetObjectItem(json, "content");
        if (content && cJSON_IsString(content)) {
            printf("\n=== ERROR ===\n%s\n=== End of Error ===\n",
                   content->valuestring);
        } else {
            printf("ERROR message received\n");
        }
    }
    else {
        printf("Unknown message type: %s\n", msg_type);
    }

    cJSON_Delete(json);
}
int zmq_client_check_user_input(char *buffer, size_t buffer_size, int timeout_ms) {
    fd_set readfds;
    struct timeval tv;
    int retval;

    // Set timeout
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    // Watch stdin (file descriptor 0) to see when it has input
    FD_ZERO(&readfds);
    FD_SET(0, &readfds);

    // Wait for input with timeout
    retval = select(1, &readfds, NULL, NULL, &tv);

    if (retval == -1) {
        LOG_ERROR("select() error: %s", strerror(errno));
        return -1;
    } else if (retval == 0) {
        // Timeout occurred
        return 0;
    } else {
        // Data is available on stdin
        if (FD_ISSET(0, &readfds)) {
            if (fgets(buffer, (int)buffer_size, stdin)) {
                // Remove newline
                buffer[strcspn(buffer, "\n")] = '\0';
                return 1;
            } else {
                // EOF or error
                return -1;
            }
        }
    }

    return 0;
}

void zmq_client_send_text_message(ZMQClientConnectionState *conn, const char *text) {
    if (!conn || !text) {
        LOG_ERROR("Invalid parameters");
        return;
    }

    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "messageType", "TEXT");
    cJSON_AddStringToObject(message, "content", text);

    char *json_str = cJSON_PrintUnformatted(message);
    LOG_DEBUG("Message JSON: %s", json_str);

    // Send the message with ID for reliable delivery
    int rc = zmq_client_send_message_with_id(conn, json_str, NULL, 0);
    if (rc < 0) {
        LOG_ERROR("Failed to send message with ID");
        free(json_str);
        cJSON_Delete(message);
        return;
    }

    // Receive and process responses using time-sharing
    const int ZMQ_POLL_TIMEOUT_MS = 500;
    const int USER_INPUT_CHECK_TIMEOUT_MS = 100;
    const int RESEND_CHECK_INTERVAL_MS = 1000;

    int message_count = 0;
    const int ZMQ_CLIENT_MAX_MESSAGES = 1000;
    int in_conversation = 1;
    int consecutive_timeouts = 0;
    const int MAX_CONSECUTIVE_TIMEOUTS = 6;
    int prompt_shown = 0;
    int64_t last_resend_check = 0;

    while (message_count < ZMQ_CLIENT_MAX_MESSAGES && in_conversation) {
        int64_t current_time = get_current_time_ms_client();

        // Periodically check and resend pending messages
        if (current_time - last_resend_check >= RESEND_CHECK_INTERVAL_MS) {
            int resent = zmq_client_check_and_resend_pending(conn, current_time);
            if (resent > 0) {
                LOG_INFO("ZMQ Client: Resent %d pending message(s)", resent);
            }
            last_resend_check = current_time;
        }

        char response[ZMQ_CLIENT_BUFFER_SIZE];
        rc = zmq_client_receive_message(conn, response, sizeof(response), ZMQ_POLL_TIMEOUT_MS);

        if (rc >= 0) {
            message_count++;
            consecutive_timeouts = 0;
            zmq_client_process_message(conn, response);
        } else if (errno == EAGAIN) {
            consecutive_timeouts++;

            if (consecutive_timeouts == 1 && !prompt_shown) {
                printf("\n[Waiting for response... Type '/interrupt' to cancel or '/help' for commands]\n");
                fflush(stdout);
                prompt_shown = 1;
            }

            char user_input[ZMQ_CLIENT_BUFFER_SIZE];
            int input_result = zmq_client_check_user_input(user_input, sizeof(user_input), USER_INPUT_CHECK_TIMEOUT_MS);

            if (input_result == 1) {
                prompt_shown = 0;

                if (strcmp(user_input, "/interrupt") == 0 || strcmp(user_input, "/cancel") == 0) {
                    printf("\n=== Conversation interrupted by user ===\n");
                    break;
                } else if (strcmp(user_input, "/help") == 0) {
                    printf("\n=== Available commands during conversation ===\n");
                    printf("/interrupt or /cancel - Stop current conversation and return to prompt\n");
                    printf("/help - Show this help message\n");
                }
            }

            if (consecutive_timeouts >= MAX_CONSECUTIVE_TIMEOUTS) {
                break;
            }
        } else {
            LOG_ERROR("Error receiving message: %s", zmq_strerror(errno));
            break;
        }
    }

    free(json_str);
    cJSON_Delete(message);
}
int zmq_client_mode(const char *endpoint) {
    ZMQClientConnectionState conn = {0};

    // Initialize connection
    if (!zmq_client_initialize_connection(&conn, endpoint)) {
        LOG_ERROR("Failed to connect to %s", endpoint);
        printf("\nFailed to connect to %s\n", endpoint);
        printf("Make sure Klawed is running with ZMQ enabled and listening on this endpoint.\n");
        printf("Check: KLAWED_ZMQ_ENDPOINT=%s\n", endpoint);
        return 1;
    }

    printf("\nConnected to %s\n", endpoint);
    printf("Type your messages (or /help for commands)\n");
    printf("-----------------------------------------\n");

    // Interactive loop
    char input[ZMQ_CLIENT_BUFFER_SIZE];
    while (1) {
        printf("\n> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        // Remove newline
        input[strcspn(input, "\n")] = '\0';

        // Skip empty input
        if (strlen(input) == 0) {
            continue;
        }

        // Check for commands
        if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
            printf("Goodbye!\n");
            break;
        } else if (strcmp(input, "/help") == 0) {
            zmq_client_print_usage("klawed");
        } else if (input[0] == '/') {
            printf("Unknown command: %s\n", input);
            printf("Type /help for available commands\n");
        } else {
            // Regular text message
            zmq_client_send_text_message(&conn, input);
        }
    }

    // Cleanup
    zmq_client_cleanup_connection(&conn);

    LOG_INFO("ZMQ Client exiting");
    return 0;
}

#endif // HAVE_ZMQ
