/*
 * zmq_socket.c - Simple ZeroMQ socket implementation for Klawed
 *
 * Implementation:
 * - ZMQ_PAIR socket with bind/connect
 * - Message ID/ACK system for reliability
 * - Time-sharing loop for user input and message processing
 * - LINGER option for clean shutdown
 * - TCP keepalive enabled
 * - Basic error handling (exit on fatal errors)
 */

#include "zmq_socket.h"
#include "logger.h"
#include "klawed_internal.h"
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <bsd/string.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/select.h>

// Include ZMQ headers if available
#ifdef HAVE_ZMQ
#include <zmq.h>
#include <cjson/cJSON.h>
#endif

// Default buffer size for ZMQ messages
#define ZMQ_BUFFER_SIZE 65536

// Message ID constants
#define MESSAGE_ID_HEX_LENGTH 33  // 128 bits = 32 hex chars + null terminator
#define HASH_SAMPLE_SIZE 256      // Number of characters to sample from message for hash

// Pending queue defaults
#define DEFAULT_MAX_PENDING 50
#define DEFAULT_ACK_TIMEOUT_MS 3000    // 3 seconds
#define DEFAULT_MAX_RETRIES 5

// Time-sharing loop constants
#define DEFAULT_TIMESLICE_MS 100       // Check user input for 100ms, messages for 100ms

/**
 * Get current time in milliseconds
 */
static int64_t get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
}

/**
 * Simple hash function combining timestamp, message sample, and salt
 * Uses FNV-1a-like algorithm with additional mixing
 */
static void hash_message_id(int64_t timestamp, const char *message, size_t message_len,
                            uint32_t salt, uint8_t out_hash[16]) {
    // Combine timestamp and salt into 128-bit seed
    uint64_t seed1 = (uint64_t)timestamp ^ ((uint64_t)salt << 32);
    uint64_t seed2 = (uint64_t)timestamp * (uint64_t)salt;
    
    // Initialize with seeds
    uint64_t h1 = seed1;
    uint64_t h2 = seed2;
    
    // Sample first HASH_SAMPLE_SIZE characters (or entire message if shorter)
    size_t sample_len = message_len < HASH_SAMPLE_SIZE ? message_len : HASH_SAMPLE_SIZE;
    
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
static void hash_to_hex(const uint8_t hash[16], char *out_hex, size_t out_hex_size) {
    if (out_hex_size < MESSAGE_ID_HEX_LENGTH) {
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
static void init_pending_queue(ZMQContext *ctx) {
    ctx->pending_queue.head = NULL;
    ctx->pending_queue.tail = NULL;
    ctx->pending_queue.count = 0;
    ctx->pending_queue.max_pending = DEFAULT_MAX_PENDING;
    ctx->pending_queue.timeout_ms = DEFAULT_ACK_TIMEOUT_MS;
    ctx->pending_queue.max_retries = DEFAULT_MAX_RETRIES;
}

/**
 * Free a pending message node
 */
static void free_pending_message(ZMQPendingMessage *msg) {
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
static int add_to_pending_queue(ZMQContext *ctx, const char *message_id, 
                                const char *message_json, int64_t sent_time_ms) {
    // Check if queue is full
    if (ctx->pending_queue.count >= ctx->pending_queue.max_pending) {
        LOG_WARN("ZMQ: Pending queue full (max %d), cannot add message",
                 ctx->pending_queue.max_pending);
        return ZMQ_ERROR_NOMEM;
    }
    
    ZMQPendingMessage *pending = calloc(1, sizeof(ZMQPendingMessage));
    if (!pending) {
        LOG_ERROR("ZMQ: Failed to allocate pending message");
        return ZMQ_ERROR_NOMEM;
    }
    
    pending->message_id = strdup(message_id);
    if (!pending->message_id) {
        free(pending);
        return ZMQ_ERROR_NOMEM;
    }
    
    pending->message_json = strdup(message_json);
    if (!pending->message_json) {
        free(pending->message_id);
        free(pending);
        return ZMQ_ERROR_NOMEM;
    }
    
    pending->sent_time_ms = sent_time_ms;
    pending->retry_count = 0;
    pending->next = NULL;
    
    // Add to tail of queue
    if (ctx->pending_queue.tail) {
        ctx->pending_queue.tail->next = pending;
    }
    ctx->pending_queue.tail = pending;
    
    if (!ctx->pending_queue.head) {
        ctx->pending_queue.head = pending;
    }
    
    ctx->pending_queue.count++;
    
    LOG_DEBUG("ZMQ: Added message %s to pending queue (count: %d)",
              message_id, ctx->pending_queue.count);
    
    return ZMQ_ERROR_NONE;
}

/**
 * Remove message from pending queue by ID
 */
static int remove_from_pending_queue(ZMQContext *ctx, const char *message_id) {
    ZMQPendingMessage *prev = NULL;
    ZMQPendingMessage *curr = ctx->pending_queue.head;
    
    while (curr) {
        if (strcmp(curr->message_id, message_id) == 0) {
            // Found it, remove from list
            if (prev) {
                prev->next = curr->next;
            } else {
                ctx->pending_queue.head = curr->next;
            }
            
            if (curr == ctx->pending_queue.tail) {
                ctx->pending_queue.tail = prev;
            }
            
            ctx->pending_queue.count--;
            LOG_DEBUG("ZMQ: Removed message %s from pending queue (count: %d)",
                      message_id, ctx->pending_queue.count);
            
            free_pending_message(curr);
            return 0;
        }
        
        prev = curr;
        curr = curr->next;
    }
    
    LOG_WARN("ZMQ: Message %s not found in pending queue", message_id);
    return -1;
}

// Forward declarations
#ifdef HAVE_ZMQ
static int zmq_process_interactive(ZMQContext *ctx, struct ConversationState *state, const char *user_input);
static int zmq_send_json_response(ZMQContext *ctx, const char *message_type, const char *content);
static int zmq_send_tool_result(ZMQContext *ctx, const char *tool_name, const char *tool_id,
                                cJSON *tool_output, int is_error);
static int zmq_send_tool_request(ZMQContext *ctx, const char *tool_name, const char *tool_id,
                                 cJSON *tool_parameters);
#endif

ZMQContext* zmq_socket_init(const char *endpoint, int socket_type) {
#ifdef HAVE_ZMQ
    struct timespec timestamp = {0};
    clock_gettime(CLOCK_REALTIME, &timestamp);
    
    if (!endpoint) {
        LOG_ERROR("ZMQ: Endpoint cannot be NULL");
        return NULL;
    }

    LOG_INFO("ZMQ: Initializing ZMQ socket");
    LOG_DEBUG("ZMQ: Endpoint: %s", endpoint);
    LOG_DEBUG("ZMQ: Socket type: %d", socket_type);

    ZMQContext *ctx = calloc(1, sizeof(ZMQContext));
    if (!ctx) {
        LOG_ERROR("ZMQ: Failed to allocate context memory");
        return NULL;
    }

    // Create ZMQ context
    ctx->context = zmq_ctx_new();
    if (!ctx->context) {
        LOG_ERROR("ZMQ: Failed to create ZMQ context: %s", zmq_strerror(errno));
        free(ctx);
        return NULL;
    }

    // Create socket
    ctx->socket = zmq_socket(ctx->context, socket_type);
    if (!ctx->socket) {
        LOG_ERROR("ZMQ: Failed to create ZMQ socket: %s", zmq_strerror(errno));
        zmq_ctx_term(ctx->context);
        free(ctx);
        return NULL;
    }

    // Set socket options for clean shutdown and reliability
    int linger = 1000; // 1 second linger to allow pending messages to be sent
    zmq_setsockopt(ctx->socket, ZMQ_LINGER, &linger, sizeof(linger));

    // Enable TCP keepalive for better connection monitoring
    int keepalive = 1;
    zmq_setsockopt(ctx->socket, ZMQ_TCP_KEEPALIVE, &keepalive, sizeof(keepalive));

    int keepalive_idle = 60; // Start keepalive after 60 seconds of idle
    zmq_setsockopt(ctx->socket, ZMQ_TCP_KEEPALIVE_IDLE, &keepalive_idle, sizeof(keepalive_idle));

    int keepalive_intvl = 5; // Send keepalive every 5 seconds
    zmq_setsockopt(ctx->socket, ZMQ_TCP_KEEPALIVE_INTVL, &keepalive_intvl, sizeof(keepalive_intvl));

    int keepalive_cnt = 3; // Consider dead after 3 failed keepalives
    zmq_setsockopt(ctx->socket, ZMQ_TCP_KEEPALIVE_CNT, &keepalive_cnt, sizeof(keepalive_cnt));

    // Bind or connect based on socket type
    int rc;
    if (socket_type == ZMQ_PAIR) {
        // Server/binding sockets (PAIR can bind or connect, but typically one side binds)
        LOG_DEBUG("ZMQ: Binding socket to endpoint: %s", endpoint);
        rc = zmq_bind(ctx->socket, endpoint);
        if (rc != 0) {
            LOG_ERROR("ZMQ: Failed to bind to %s: %s", endpoint, zmq_strerror(errno));
            zmq_close(ctx->socket);
            zmq_ctx_term(ctx->context);
            free(ctx);
            return NULL;
        }
        LOG_INFO("ZMQ: Successfully bound to %s", endpoint);
    } else {
        // Client/connecting sockets
        LOG_DEBUG("ZMQ: Connecting socket to endpoint: %s", endpoint);
        rc = zmq_connect(ctx->socket, endpoint);
        if (rc != 0) {
            LOG_ERROR("ZMQ: Failed to connect to %s: %s", endpoint, zmq_strerror(errno));
            zmq_close(ctx->socket);
            zmq_ctx_term(ctx->context);
            free(ctx);
            return NULL;
        }
        LOG_INFO("ZMQ: Successfully connected to %s", endpoint);
    }

    ctx->endpoint = strdup(endpoint);
    if (!ctx->endpoint) {
        LOG_ERROR("ZMQ: Failed to duplicate endpoint string");
        zmq_close(ctx->socket);
        zmq_ctx_term(ctx->context);
        free(ctx);
        return NULL;
    }

    ctx->socket_type = socket_type;
    ctx->enabled = true;
    
    // Initialize message ID/ACK system
    init_pending_queue(ctx);
    
    // Generate random salt for message ID generation
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        size_t n = fread(&ctx->salt, sizeof(ctx->salt), 1, urandom);
        if (n != 1) {
            // Fallback to time-based salt
            ctx->salt = (uint32_t)get_current_time_ms();
        }
        fclose(urandom);
    } else {
        // Fallback to time-based salt
        ctx->salt = (uint32_t)get_current_time_ms();
    }
    
    ctx->message_sequence = 0;
    
    LOG_INFO("ZMQ: Message ID/ACK system initialized (salt: 0x%08x)", ctx->salt);

    LOG_INFO("ZMQ: Socket initialization completed successfully");
    return ctx;
#else
    LOG_ERROR("ZMQ: ZeroMQ support not compiled in");
    return NULL;
#endif
}

void zmq_socket_cleanup(ZMQContext *ctx) {
#ifdef HAVE_ZMQ
    if (!ctx) return;

    LOG_INFO("ZMQ: Cleaning up ZMQ context for endpoint: %s", ctx->endpoint ? ctx->endpoint : "unknown");

    // Clean up pending message queue
    ZMQPendingMessage *curr = ctx->pending_queue.head;
    while (curr) {
        ZMQPendingMessage *next = curr->next;
        free_pending_message(curr);
        curr = next;
    }
    ctx->pending_queue.head = NULL;
    ctx->pending_queue.tail = NULL;
    ctx->pending_queue.count = 0;

    if (ctx->socket) {
        zmq_close(ctx->socket);
        ctx->socket = NULL;
    }

    if (ctx->context) {
        zmq_ctx_term(ctx->context);
        ctx->context = NULL;
    }

    if (ctx->endpoint) {
        free(ctx->endpoint);
        ctx->endpoint = NULL;
    }

    free(ctx);
#else
    (void)ctx; // Unused parameter
#endif
}

void zmq_cleanup_pending_queue(ZMQContext *ctx) {
#ifdef HAVE_ZMQ
    if (!ctx) return;

    ZMQPendingMessage *curr = ctx->pending_queue.head;
    while (curr) {
        ZMQPendingMessage *next = curr->next;
        free_pending_message(curr);
        curr = next;
    }
    ctx->pending_queue.head = NULL;
    ctx->pending_queue.tail = NULL;
    ctx->pending_queue.count = 0;

    LOG_DEBUG("ZMQ: Pending queue cleaned up");
#else
    (void)ctx;
#endif
}

int zmq_socket_send(ZMQContext *ctx, const char *message, size_t message_len) {
#ifdef HAVE_ZMQ
    if (!ctx || !message) {
        LOG_ERROR("ZMQ: Invalid parameters for send");
        return ZMQ_ERROR_INVALID_PARAM;
    }

    if (!ctx->socket) {
        LOG_ERROR("ZMQ: No socket available for send");
        return ZMQ_ERROR_SEND_FAILED;
    }

    LOG_DEBUG("ZMQ: Sending %zu bytes to endpoint: %s",
              message_len, ctx->endpoint ? ctx->endpoint : "unknown");

    int rc = zmq_send(ctx->socket, message, message_len, 0);
    if (rc < 0) {
        int err = errno;
        LOG_ERROR("ZMQ: Failed to send message: %s", zmq_strerror(err));
        return ZMQ_ERROR_SEND_FAILED;
    }

    LOG_DEBUG("ZMQ: Successfully sent %zu bytes", message_len);
    return ZMQ_ERROR_NONE;
#else
    (void)ctx;
    (void)message;
    (void)message_len;
    return ZMQ_ERROR_NOT_SUPPORTED;
#endif
}

int zmq_socket_receive(ZMQContext *ctx, char *buffer, size_t buffer_size, int timeout_ms) {
#ifdef HAVE_ZMQ
    if (!ctx || !buffer || buffer_size == 0) {
        LOG_ERROR("ZMQ: Invalid parameters for receive");
        return ZMQ_ERROR_INVALID_PARAM;
    }

    if (!ctx->socket) {
        LOG_ERROR("ZMQ: No socket available for receive");
        return ZMQ_ERROR_RECEIVE_FAILED;
    }

    // Set timeout (special case: -1 means infinite timeout in ZMQ)
    zmq_setsockopt(ctx->socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));

    LOG_DEBUG("ZMQ: Waiting for message on endpoint: %s (timeout: %d ms, buffer size: %zu)",
              ctx->endpoint ? ctx->endpoint : "unknown", timeout_ms, buffer_size);

    int rc = zmq_recv(ctx->socket, buffer, buffer_size - 1, 0);
    if (rc < 0) {
        int err = errno;
        if (err == EAGAIN) {
            LOG_DEBUG("ZMQ: Receive timeout after %d ms", timeout_ms);
        } else {
            LOG_ERROR("ZMQ: Failed to receive message: %s", zmq_strerror(err));
        }
        return (err == EAGAIN) ? ZMQ_ERROR_RECEIVE_FAILED : ZMQ_ERROR_RECEIVE_FAILED;
    }

    LOG_INFO("ZMQ: Received %d bytes from endpoint: %s", rc,
             ctx->endpoint ? ctx->endpoint : "unknown");
    buffer[rc] = '\0'; // Null-terminate the received data
    return rc;
#else
    (void)ctx;
    (void)buffer;
    (void)buffer_size;
    (void)timeout_ms;
    return ZMQ_ERROR_NOT_SUPPORTED;
#endif
}

bool zmq_socket_available(void) {
#ifdef HAVE_ZMQ
    return true;
#else
    return false;
#endif
}

/**
 * Generate a unique message ID based on timestamp, partial content, and random salt
 */
int zmq_generate_message_id(ZMQContext *ctx, const char *message, size_t message_len,
                           char *out_id, size_t out_id_size) {
#ifdef HAVE_ZMQ
    if (!ctx || !message || !out_id || out_id_size < MESSAGE_ID_HEX_LENGTH) {
        return ZMQ_ERROR_INVALID_PARAM;
    }

    // Get current timestamp
    int64_t timestamp_ms = get_current_time_ms();
    
    // Generate 128-bit hash
    uint8_t hash[16];
    hash_message_id(timestamp_ms, message, message_len, ctx->salt, hash);
    
    // Convert to hex string
    hash_to_hex(hash, out_id, out_id_size);
    
    ctx->message_sequence++;
    LOG_DEBUG("ZMQ: Generated message ID %s (seq: %d, ts: %lld)",
              out_id, ctx->message_sequence, (long long)timestamp_ms);
    
    return ZMQ_ERROR_NONE;
#else
    (void)ctx;
    (void)message;
    (void)message_len;
    (void)out_id;
    (void)out_id_size;
    return ZMQ_ERROR_NOT_SUPPORTED;
#endif
}

/**
 * Send message with ID and track for ACK
 */
int zmq_socket_send_with_id(ZMQContext *ctx, const char *message, size_t message_len,
                           char *message_id_out, size_t message_id_out_size) {
#ifdef HAVE_ZMQ
    if (!ctx || !message) {
        LOG_ERROR("ZMQ: Invalid parameters for send_with_id");
        return ZMQ_ERROR_INVALID_PARAM;
    }
    
    if (!ctx->socket) {
        LOG_ERROR("ZMQ: No socket available for send");
        return ZMQ_ERROR_SEND_FAILED;
    }
    
    // Generate message ID
    char message_id[MESSAGE_ID_HEX_LENGTH];
    if (zmq_generate_message_id(ctx, message, message_len, message_id, sizeof(message_id)) != 0) {
        LOG_ERROR("ZMQ: Failed to generate message ID");
        return ZMQ_ERROR_SEND_FAILED;
    }
    
    // Wrap message with ID field
    cJSON *json = cJSON_Parse(message);
    if (!json) {
        LOG_ERROR("ZMQ: Failed to parse message JSON for wrapping");
        return ZMQ_ERROR_SEND_FAILED;
    }
    
    // Add messageId field
    cJSON_AddStringToObject(json, "messageId", message_id);
    
    char *wrapped_message = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    if (!wrapped_message) {
        LOG_ERROR("ZMQ: Failed to serialize wrapped message");
        return ZMQ_ERROR_SEND_FAILED;
    }
    
    // Send the wrapped message
    int rc = zmq_send(ctx->socket, wrapped_message, strlen(wrapped_message), 0);
    
    if (rc >= 0) {
        LOG_DEBUG("ZMQ: Sent message %s (%zu bytes wrapped -> %d bytes sent)",
                  message_id, message_len, rc);
        
        // Add to pending queue
        int64_t sent_time = get_current_time_ms();
        if (add_to_pending_queue(ctx, message_id, wrapped_message, sent_time) == 0) {
            // Return message ID to caller if requested
            if (message_id_out && message_id_out_size >= MESSAGE_ID_HEX_LENGTH) {
                strlcpy(message_id_out, message_id, message_id_out_size);
            }
            free(wrapped_message);
            return ZMQ_ERROR_NONE;
        } else {
            LOG_WARN("ZMQ: Message sent but failed to track in pending queue");
            free(wrapped_message);
            return ZMQ_ERROR_NOMEM;
        }
    } else {
        int err = errno;
        LOG_ERROR("ZMQ: Failed to send message %s: %s", message_id, zmq_strerror(err));
        free(wrapped_message);
        return ZMQ_ERROR_SEND_FAILED;
    }
#else
    (void)ctx;
    (void)message;
    (void)message_len;
    (void)message_id_out;
    (void)message_id_out_size;
    return ZMQ_ERROR_NOT_SUPPORTED;
#endif
}

/**
 * Send ACK for a received message
 */
int zmq_send_ack(ZMQContext *ctx, const char *message_id) {
#ifdef HAVE_ZMQ
    if (!ctx || !message_id) {
        return ZMQ_ERROR_INVALID_PARAM;
    }
    
    if (!ctx->socket) {
        return ZMQ_ERROR_SEND_FAILED;
    }
    
    // Create ACK message
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return ZMQ_ERROR_SEND_FAILED;
    }
    
    cJSON_AddStringToObject(json, "messageType", "ACK");
    cJSON_AddStringToObject(json, "messageId", message_id);
    
    char *ack_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    if (!ack_str) {
        return ZMQ_ERROR_SEND_FAILED;
    }
    
    int rc = zmq_send(ctx->socket, ack_str, strlen(ack_str), 0);
    free(ack_str);
    
    if (rc >= 0) {
        LOG_DEBUG("ZMQ: Sent ACK for message %s", message_id);
        return ZMQ_ERROR_NONE;
    } else {
        LOG_ERROR("ZMQ: Failed to send ACK for message %s", message_id);
        return ZMQ_ERROR_SEND_FAILED;
    }
#else
    (void)ctx;
    (void)message_id;
    return ZMQ_ERROR_NOT_SUPPORTED;
#endif
}

/**
 * Process ACK message and remove from pending queue
 */
int zmq_process_ack(ZMQContext *ctx, const char *message_id) {
#ifdef HAVE_ZMQ
    if (!ctx || !message_id) {
        return ZMQ_ERROR_INVALID_PARAM;
    }
    
    return remove_from_pending_queue(ctx, message_id);
#else
    (void)ctx;
    (void)message_id;
    return ZMQ_ERROR_NOT_SUPPORTED;
#endif
}

/**
 * Check and resend pending messages that have timed out
 */
int zmq_check_and_resend_pending(ZMQContext *ctx, int64_t current_time_ms) {
#ifdef HAVE_ZMQ
    if (!ctx) {
        return ZMQ_ERROR_INVALID_PARAM;
    }
    
    if (!ctx->pending_queue.head) {
        return 0; // No pending messages
    }
    
    int resent_count = 0;
    ZMQPendingMessage *curr = ctx->pending_queue.head;
    ZMQPendingMessage *prev = NULL;
    
    while (curr) {
        int64_t elapsed = current_time_ms - curr->sent_time_ms;
        
        if (elapsed >= ctx->pending_queue.timeout_ms) {
            // Timeout exceeded, check retry count
            if (curr->retry_count >= ctx->pending_queue.max_retries) {
                // Max retries exceeded, give up and remove
                LOG_ERROR("ZMQ: Message %s exceeded max retries (%d), dropping",
                         curr->message_id, ctx->pending_queue.max_retries);
                
                ZMQPendingMessage *to_delete = curr;
                if (prev) {
                    prev->next = curr->next;
                    curr = curr->next;
                } else {
                    ctx->pending_queue.head = curr->next;
                    curr = curr->next;
                }
                
                if (to_delete == ctx->pending_queue.tail) {
                    ctx->pending_queue.tail = prev;
                }
                
                ctx->pending_queue.count--;
                free_pending_message(to_delete);
                continue;
            }
            
            // Retry sending
            curr->retry_count++;
            curr->sent_time_ms = current_time_ms;
            
            int rc = zmq_send(ctx->socket, curr->message_json,
                             strlen(curr->message_json), 0);
            
            if (rc >= 0) {
                LOG_INFO("ZMQ: Resent message %s (attempt %d/%d)",
                        curr->message_id, curr->retry_count, ctx->pending_queue.max_retries);
                resent_count++;
            } else {
                LOG_ERROR("ZMQ: Failed to resend message %s", curr->message_id);
            }
        }
        
        prev = curr;
        curr = curr->next;
    }
    
    return resent_count;
#else
    (void)ctx;
    (void)current_time_ms;
    return ZMQ_ERROR_NOT_SUPPORTED;
#endif
}

/**
 * Check for user input with timeout using select()
 */
int zmq_check_user_input(char *buffer, size_t buffer_size, int timeout_ms) {
    fd_set readfds;
    struct timeval tv;
    int retval;
    
    if (!buffer || buffer_size == 0) {
        return -1;
    }
    
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

int zmq_socket_process_message(ZMQContext *ctx, struct ConversationState *state, struct TUIState *tui) {
#ifdef HAVE_ZMQ
    if (!ctx || !state) {
        LOG_ERROR("ZMQ: Invalid parameters for process_message");
        return -1;
    }
    (void)tui; // Unused parameter for now

    LOG_DEBUG("ZMQ: Waiting for incoming message on endpoint: %s",
              ctx->endpoint ? ctx->endpoint : "unknown");

    char buffer[ZMQ_BUFFER_SIZE];
    // Use infinite timeout (-1) for daemon mode to wait indefinitely
    int received = zmq_socket_receive(ctx, buffer, sizeof(buffer), -1);
    if (received <= 0) {
        LOG_WARN("ZMQ: Failed to receive message or connection closed");
        return -1;
    }

    LOG_INFO("ZMQ: Received %d bytes, processing message", received);
    LOG_DEBUG("ZMQ: Raw message (first 500 chars): %.*s",
             (int)(received > 500 ? 500 : received), buffer);

    // Print to console
    printf("ZMQ: Received %d bytes\n", received);
    fflush(stdout);

    // Parse JSON message
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        LOG_ERROR("ZMQ: Failed to parse JSON message");
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            LOG_ERROR("ZMQ: JSON error near: %s", error_ptr);
        }

        // Send error response
        char error_response[256];
        snprintf(error_response, sizeof(error_response),
                 "{\"messageType\": \"ERROR\", \"content\": \"Invalid JSON\"}");
        zmq_socket_send(ctx, error_response, strlen(error_response));
        return -1;
    }

    // Extract message type and message ID
    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
    cJSON *message_id = cJSON_GetObjectItem(json, "messageId");
    cJSON *content = cJSON_GetObjectItem(json, "content");

    // Handle ACK messages
    if (message_type && cJSON_IsString(message_type) &&
        strcmp(message_type->valuestring, "ACK") == 0) {
        if (message_id && cJSON_IsString(message_id)) {
            LOG_DEBUG("ZMQ: Received ACK for message %s", message_id->valuestring);
            zmq_process_ack(ctx, message_id->valuestring);
        }
        cJSON_Delete(json);
        return 0; // ACK processed successfully
    }

    // Send ACK for all non-ACK messages
    if (message_id && cJSON_IsString(message_id)) {
        zmq_send_ack(ctx, message_id->valuestring);
    }

    char response[ZMQ_BUFFER_SIZE];
    response[0] = '\0';

    if (message_type && cJSON_IsString(message_type)) {
        if (strcmp(message_type->valuestring, "TEXT") == 0 &&
            content && cJSON_IsString(content)) {

            // Process text message with interactive tool call support
            LOG_INFO("ZMQ: Processing TEXT message (length: %zu)",
                     strlen(content->valuestring));

            printf("ZMQ: Processing TEXT message (length: %zu)\n",
                   strlen(content->valuestring));
            fflush(stdout);

            // Process interactively (handles tool calls recursively)
            int interactive_result = zmq_process_interactive(ctx, state, content->valuestring);

            if (interactive_result != 0) {
                LOG_ERROR("ZMQ: Interactive processing failed");
                snprintf(response, sizeof(response),
                         "{\"messageType\": \"ERROR\", \"content\": \"Interactive processing failed\"}");
            }
        } else {
            LOG_WARN("ZMQ: Unsupported message type received: %s",
                     message_type->valuestring);
            snprintf(response, sizeof(response),
                     "{\"messageType\": \"ERROR\", \"content\": \"Unsupported message type\"}");
        }
    } else {
        LOG_WARN("ZMQ: Invalid message format received - missing messageType");
        snprintf(response, sizeof(response),
                 "{\"messageType\": \"ERROR\", \"content\": \"Invalid message format - missing messageType\"}");
    }

    cJSON_Delete(json);

    // Send response
    if (response[0] != '\0') {
        LOG_DEBUG("ZMQ: Sending response (length: %zu)", strlen(response));

        printf("ZMQ: Sending response (length: %zu)\n", strlen(response));
        fflush(stdout);

        int send_result = zmq_socket_send(ctx, response, strlen(response));
        if (send_result != 0) {
            LOG_ERROR("ZMQ: Failed to send response");
            printf("ZMQ: Failed to send response\n");
            fflush(stdout);
            return -1;
        }
    }

    LOG_INFO("ZMQ: Message processing completed");
    return 0;
#else
    (void)ctx;
    (void)state;
    (void)tui;
    return -1;
#endif
}

// Helper function to send a JSON response
static int zmq_send_json_response(ZMQContext *ctx, const char *message_type, const char *content) {
#ifdef HAVE_ZMQ
    if (!ctx || !message_type) {
        LOG_ERROR("ZMQ: Invalid parameters for send_json_response");
        return -1;
    }

    cJSON *response_json = cJSON_CreateObject();
    if (!response_json) {
        LOG_ERROR("ZMQ: Failed to create response JSON object");
        return -1;
    }

    cJSON_AddStringToObject(response_json, "messageType", message_type);
    if (content) {
        cJSON_AddStringToObject(response_json, "content", content);
    }

    char *response_str = cJSON_PrintUnformatted(response_json);
    if (!response_str) {
        LOG_ERROR("ZMQ: Failed to serialize response JSON");
        cJSON_Delete(response_json);
        return -1;
    }

    int result = zmq_socket_send(ctx, response_str, strlen(response_str));
    free(response_str);
    cJSON_Delete(response_json);

    return result;
#else
    (void)ctx;
    (void)message_type;
    (void)content;
    return -1;
#endif
}

// Helper function to send a tool result response
static int zmq_send_tool_result(ZMQContext *ctx, const char *tool_name, const char *tool_id,
                                cJSON *tool_output, int is_error) {
#ifdef HAVE_ZMQ
    if (!ctx || !tool_name || !tool_id) {
        LOG_ERROR("ZMQ: Invalid parameters for send_tool_result");
        return -1;
    }

    cJSON *response_json = cJSON_CreateObject();
    if (!response_json) {
        LOG_ERROR("ZMQ: Failed to create tool result JSON object");
        return -1;
    }

    cJSON_AddStringToObject(response_json, "messageType", "TOOL_RESULT");
    cJSON_AddStringToObject(response_json, "toolName", tool_name);
    cJSON_AddStringToObject(response_json, "toolId", tool_id);

    if (tool_output) {
        cJSON_AddItemToObject(response_json, "toolOutput", cJSON_Duplicate(tool_output, 1));
    } else {
        cJSON_AddNullToObject(response_json, "toolOutput");
    }

    cJSON_AddBoolToObject(response_json, "isError", is_error ? 1 : 0);

    char *response_str = cJSON_PrintUnformatted(response_json);
    if (!response_str) {
        LOG_ERROR("ZMQ: Failed to serialize tool result JSON");
        cJSON_Delete(response_json);
        return -1;
    }

    int result = zmq_socket_send(ctx, response_str, strlen(response_str));
    free(response_str);
    cJSON_Delete(response_json);

    return result;
#else
    (void)ctx;
    (void)tool_name;
    (void)tool_id;
    (void)tool_output;
    (void)is_error;
    return -1;
#endif
}

// Helper function to send a tool execution request
static int zmq_send_tool_request(ZMQContext *ctx, const char *tool_name, const char *tool_id,
                                 cJSON *tool_parameters) {
#ifdef HAVE_ZMQ
    if (!ctx || !tool_name || !tool_id) {
        LOG_ERROR("ZMQ: Invalid parameters for send_tool_request");
        return -1;
    }

    cJSON *request_json = cJSON_CreateObject();
    if (!request_json) {
        LOG_ERROR("ZMQ: Failed to create tool request JSON object");
        return -1;
    }

    cJSON_AddStringToObject(request_json, "messageType", "TOOL");
    cJSON_AddStringToObject(request_json, "toolName", tool_name);
    cJSON_AddStringToObject(request_json, "toolId", tool_id);

    if (tool_parameters) {
        cJSON_AddItemToObject(request_json, "toolParameters", cJSON_Duplicate(tool_parameters, 1));
    } else {
        cJSON_AddNullToObject(request_json, "toolParameters");
    }

    char *request_str = cJSON_PrintUnformatted(request_json);
    if (!request_str) {
        LOG_ERROR("ZMQ: Failed to serialize tool request JSON");
        cJSON_Delete(request_json);
        return -1;
    }

    LOG_INFO("ZMQ: Sending TOOL request for %s (id: %s)", tool_name, tool_id);
    int result = zmq_socket_send(ctx, request_str, strlen(request_str));
    free(request_str);
    cJSON_Delete(request_json);

    return result;
#else
    (void)ctx;
    (void)tool_name;
    (void)tool_id;
    (void)tool_parameters;
    return -1;
#endif
}

// Process ZMQ message with interactive tool call support
static int zmq_process_interactive(ZMQContext *ctx, struct ConversationState *state,
                                   const char *user_input) {
#ifdef HAVE_ZMQ
    if (!ctx || !state || !user_input) {
        LOG_ERROR("ZMQ: Invalid parameters for process_interactive");
        return -1;
    }

    LOG_INFO("ZMQ: Processing interactive message: %.*s",
             (int)(strlen(user_input) > 200 ? 200 : strlen(user_input)), user_input);

    // Add user message to conversation
    add_user_message(state, user_input);

    // Main interactive loop
    int iteration = 0;
    const int MAX_ITERATIONS = 50; // Safety limit

    while (iteration < MAX_ITERATIONS) {
        iteration++;
        LOG_DEBUG("ZMQ: Interactive loop iteration %d", iteration);

        // Call AI API
        LOG_INFO("ZMQ: Calling AI API");
        ApiResponse *api_response = call_api_with_retries(state);

        if (!api_response) {
            LOG_ERROR("ZMQ: Failed to get response from AI API");
            zmq_send_json_response(ctx, "ERROR", "AI inference failed");
            return -1;
        }

        if (api_response->error_message) {
            LOG_ERROR("ZMQ: AI API returned error: %s", api_response->error_message);
            zmq_send_json_response(ctx, "ERROR", api_response->error_message);
            api_response_free(api_response);
            return -1;
        }

        // Send assistant's text response if present
        if (api_response->message.text && api_response->message.text[0] != '\0') {
            // Skip whitespace-only content
            const char *p = api_response->message.text;
            while (*p && isspace((unsigned char)*p)) p++;

            if (*p != '\0') {  // Has non-whitespace content
                LOG_INFO("ZMQ: Sending assistant text response");

                printf("\n--- AI Response ---\n");
                int preview_len = (int)(strlen(p) > 200 ? 200 : strlen(p));
                printf("%.*s%s\n", preview_len, p, strlen(p) > 200 ? "..." : "");
                printf("--- End of AI Response ---\n");
                fflush(stdout);

                zmq_send_json_response(ctx, "TEXT", p);
            }
        }

        // Add assistant message to conversation history
        if (api_response->raw_response) {
            cJSON *choices = cJSON_GetObjectItem(api_response->raw_response, "choices");
            if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                cJSON *choice = cJSON_GetArrayItem(choices, 0);
                cJSON *message = cJSON_GetObjectItem(choice, "message");
                if (message) {
                    add_assistant_message_openai(state, message);
                }
            }
        }

        // Process tool calls
        int tool_count = api_response->tool_count;
        ToolCall *tool_calls_array = api_response->tools;

        if (tool_count > 0) {
            LOG_INFO("ZMQ: Processing %d tool call(s)", tool_count);

            // Allocate results array
            InternalContent *results = calloc((size_t)tool_count, sizeof(InternalContent));
            if (!results) {
                LOG_ERROR("ZMQ: Failed to allocate tool result buffer");
                zmq_send_json_response(ctx, "ERROR", "Failed to allocate tool result buffer");
                api_response_free(api_response);
                return -1;
            }

            // Execute tools synchronously
            for (int i = 0; i < tool_count; i++) {
                ToolCall *tool = &tool_calls_array[i];
                if (!tool->name || !tool->id) {
                    LOG_WARN("ZMQ: Tool call missing name or id, skipping");
                    results[i].type = INTERNAL_TOOL_RESPONSE;
                    results[i].tool_id = tool->id ? strdup(tool->id) : strdup("unknown");
                    results[i].tool_name = tool->name ? strdup(tool->name) : strdup("tool");
                    results[i].tool_output = cJSON_CreateObject();
                    cJSON_AddStringToObject(results[i].tool_output, "error",
                                            "Tool call missing name or id");
                    results[i].is_error = 1;

                    zmq_send_tool_result(ctx, tool->name ? tool->name : "unknown",
                                        tool->id ? tool->id : "unknown",
                                        results[i].tool_output, 1);
                    continue;
                }

                LOG_INFO("ZMQ: Executing tool: %s (id: %s)", tool->name, tool->id);
                printf("ZMQ: Executing tool: %s\n", tool->name);
                fflush(stdout);

                // Validate that the tool is in the allowed tools list
                if (!is_tool_allowed(tool->name, state)) {
                    LOG_ERROR("ZMQ: Tool validation failed: '%s' was not provided in tools list",
                              tool->name);
                    results[i].type = INTERNAL_TOOL_RESPONSE;
                    results[i].tool_id = strdup(tool->id);
                    results[i].tool_name = strdup(tool->name);
                    results[i].tool_output = cJSON_CreateObject();
                    char error_msg[512];
                    snprintf(error_msg, sizeof(error_msg),
                             "ERROR: Tool '%s' does not exist or was not provided to you. "
                             "Please check the list of available tools and try again with a valid tool name.",
                             tool->name);
                    cJSON_AddStringToObject(results[i].tool_output, "error", error_msg);
                    results[i].is_error = 1;

                    zmq_send_tool_result(ctx, tool->name, tool->id, results[i].tool_output, 1);
                    continue;
                }

                // Convert ToolCall to execute_tool parameters
                cJSON *input = tool->parameters
                    ? cJSON_Duplicate(tool->parameters, /*recurse*/1)
                    : cJSON_CreateObject();

                // Send TOOL request message before execution
                zmq_send_tool_request(ctx, tool->name, tool->id, input);

                // Execute tool synchronously
                cJSON *tool_result = execute_tool(tool->name, input, state);

                // Send tool result response
                zmq_send_tool_result(ctx, tool->name, tool->id, tool_result, 0);

                // Store tool result
                results[i].type = INTERNAL_TOOL_RESPONSE;
                results[i].tool_id = strdup(tool->id);
                results[i].tool_name = strdup(tool->name);
                results[i].tool_output = tool_result ? cJSON_Duplicate(tool_result, 1) : cJSON_CreateObject();
                results[i].is_error = 0;

                // Clean up
                if (input) cJSON_Delete(input);
                if (tool_result) cJSON_Delete(tool_result);
            }

            // Add tool results to conversation
            if (add_tool_results(state, results, tool_count) != 0) {
                LOG_ERROR("ZMQ: Failed to add tool results to conversation");
                zmq_send_json_response(ctx, "ERROR", "Failed to add tool results to conversation");
                api_response_free(api_response);
                return -1;
            }

            // Continue loop to process next AI response with tool results
            api_response_free(api_response);
            continue;
        }

        api_response_free(api_response);
        break;
    }

    if (iteration >= MAX_ITERATIONS) {
        LOG_WARN("ZMQ: Reached maximum iterations (%d), stopping interactive loop", MAX_ITERATIONS);
        zmq_send_json_response(ctx, "ERROR", "Maximum iteration limit reached");
        return -1;
    }

    LOG_INFO("ZMQ: Interactive processing completed successfully");
    return 0;
#else
    (void)ctx;
    (void)state;
    (void)user_input;
    return -1;
#endif
}

int zmq_socket_daemon_mode(ZMQContext *ctx, struct ConversationState *state) {
#ifdef HAVE_ZMQ
    if (!ctx || !state) {
        LOG_ERROR("ZMQ: Invalid parameters for daemon_mode");
        return -1;
    }

    if (ctx->socket_type != ZMQ_PAIR) {
        LOG_ERROR("ZMQ: Daemon mode requires ZMQ_PAIR socket type");
        return -1;
    }

    LOG_INFO("ZMQ: =========================================");
    LOG_INFO("ZMQ: Starting ZMQ daemon mode");
    LOG_INFO("ZMQ: Endpoint: %s", ctx->endpoint);
    LOG_INFO("ZMQ: Socket type: ZMQ_PAIR (Peer-to-peer)");
    LOG_INFO("ZMQ: Time-sharing: user input=%dms, messages=%dms",
             DEFAULT_TIMESLICE_MS, DEFAULT_TIMESLICE_MS);
    LOG_INFO("ZMQ: =========================================");

    printf("ZMQ daemon started on %s\n", ctx->endpoint);
    printf("Type '/help' for commands or '/quit' to exit\n");
    printf("----------------------------------------\n");
    fflush(stdout);

    int message_count = 0;
    int error_count = 0;
    int64_t last_resend_check = 0;
    const int64_t RESEND_CHECK_INTERVAL_MS = 1000; // Check pending messages every second

    // Time-sharing loop
    while (ctx->enabled) {
        int64_t current_time = get_current_time_ms();
        
        // 1. Check and resend pending messages periodically
        if (current_time - last_resend_check >= RESEND_CHECK_INTERVAL_MS) {
            int resent = zmq_check_and_resend_pending(ctx, current_time);
            if (resent > 0) {
                LOG_INFO("ZMQ: Resent %d pending message(s)", resent);
            }
            last_resend_check = current_time;
        }

        // 2. Check for user input
        char user_input[1024];
        int input_result = zmq_check_user_input(user_input, sizeof(user_input),
                                                DEFAULT_TIMESLICE_MS);
        
        if (input_result == 1) {
            // User entered something
            if (strlen(user_input) > 0) {
                if (strcmp(user_input, "/quit") == 0 || strcmp(user_input, "/exit") == 0) {
                    printf("Goodbye!\n");
                    break;
                } else if (strcmp(user_input, "/help") == 0) {
                    printf("Available commands:\n");
                    printf("  /quit, /exit - Stop the daemon\n");
                    printf("  /status - Show pending messages and statistics\n");
                    printf("  /help - Show this help message\n");
                } else if (strcmp(user_input, "/status") == 0) {
                    printf("=== ZMQ Daemon Status ===\n");
                    printf("Total messages processed: %d\n", message_count);
                    printf("Total errors: %d\n", error_count);
                    printf("Pending messages: %d\n", ctx->pending_queue.count);
                    printf("Endpoint: %s\n", ctx->endpoint);
                } else {
                    printf("Unknown command: %s\n", user_input);
                    printf("Type /help for available commands\n");
                }
            }
        } else if (input_result == -1) {
            // Error reading input
            LOG_ERROR("Error reading user input");
            break;
        }

        // 3. Check for incoming messages
        char buffer[ZMQ_BUFFER_SIZE];
        int received = zmq_socket_receive(ctx, buffer, sizeof(buffer),
                                         DEFAULT_TIMESLICE_MS);
        
        if (received > 0) {
            message_count++;
            error_count = 0; // Reset error count on successful receive
            
            LOG_INFO("ZMQ: Received %d bytes (message #%d)", received, message_count);
            printf("> Received message #%d\n", message_count);
            fflush(stdout);
            
            // Process the message
            int result = zmq_socket_process_message(ctx, state, NULL);
            if (result != 0) {
                error_count++;
                LOG_WARN("ZMQ: Message processing failed (error #%d)", error_count);
                
                if (error_count > 10) {
                    LOG_ERROR("ZMQ: Too many consecutive errors (%d), stopping daemon",
                              error_count);
                    printf("ZMQ: Too many consecutive errors (%d), stopping daemon\n",
                           error_count);
                    break;
                }
            } else {
                LOG_DEBUG("ZMQ: Successfully processed message #%d", message_count);
            }
        } else if (received == ZMQ_ERROR_RECEIVE_FAILED) {
            // Timeout is expected in non-blocking mode, continue loop
            continue;
        }
    }

    LOG_INFO("ZMQ: =========================================");
    LOG_INFO("ZMQ: ZMQ daemon mode stopping");
    LOG_INFO("ZMQ: Total messages processed: %d", message_count);
    LOG_INFO("ZMQ: Total errors: %d", error_count);
    LOG_INFO("ZMQ: =========================================");

    printf("\nZMQ daemon stopped\n");
    printf("Total messages processed: %d\n", message_count);
    printf("Total errors: %d\n", error_count);
    fflush(stdout);

    return 0;
#else
    (void)ctx;
    (void)state;
    return -1;
#endif
}
