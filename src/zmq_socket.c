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
#include "zmq_thread_pool.h"
#include "logger.h"
#include "klawed_internal.h"
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <bsd/string.h>
#include <bsd/stdlib.h>
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

// Duplicate detection
#define MAX_SEEN_MESSAGES 1000
#define SEEN_MESSAGE_TTL_MS 30000  // 30 seconds

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
static int is_duplicate_message(ZMQContext *ctx, const char *message_id);
static void add_seen_message(ZMQContext *ctx, const char *message_id);
static void log_pending_queue_state(ZMQContext *ctx, const char *context);

// Message queue helper functions
static int message_queue_push(ZMQContext *ctx, const char *message) {
    pthread_mutex_lock(&ctx->message_queue.mutex);
    
    // Resize if needed
    if (ctx->message_queue.count >= ctx->message_queue.capacity) {
        int new_capacity = ctx->message_queue.capacity == 0 ? 16 : ctx->message_queue.capacity * 2;
        char **new_messages = reallocarray(ctx->message_queue.messages, 
                                          (size_t)new_capacity, sizeof(char*));
        if (!new_messages) {
            pthread_mutex_unlock(&ctx->message_queue.mutex);
            return -1;
        }
        ctx->message_queue.messages = new_messages;
        ctx->message_queue.capacity = new_capacity;
    }
    
    // Add message
    ctx->message_queue.messages[ctx->message_queue.count] = strdup(message);
    if (!ctx->message_queue.messages[ctx->message_queue.count]) {
        pthread_mutex_unlock(&ctx->message_queue.mutex);
        return -1;
    }
    ctx->message_queue.count++;
    
    // Signal waiting thread
    pthread_cond_signal(&ctx->message_queue.cond);
    
    pthread_mutex_unlock(&ctx->message_queue.mutex);
    return 0;
}

static char* message_queue_pop(ZMQContext *ctx, int timeout_ms) {
    struct timespec timeout;
    int result = 0;
    
    pthread_mutex_lock(&ctx->message_queue.mutex);
    
    // Wait for message or timeout
    while (ctx->message_queue.count == 0 && !ctx->should_exit) {
        if (timeout_ms > 0) {
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += timeout_ms / 1000;
            timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (timeout.tv_nsec >= 1000000000) {
                timeout.tv_sec++;
                timeout.tv_nsec -= 1000000000;
            }
            result = pthread_cond_timedwait(&ctx->message_queue.cond, 
                                           &ctx->message_queue.mutex, &timeout);
            if (result == ETIMEDOUT) {
                pthread_mutex_unlock(&ctx->message_queue.mutex);
                return NULL;
            }
        } else {
            pthread_cond_wait(&ctx->message_queue.cond, &ctx->message_queue.mutex);
        }
    }
    
    if (ctx->should_exit || ctx->message_queue.count == 0) {
        pthread_mutex_unlock(&ctx->message_queue.mutex);
        return NULL;
    }
    
    // Get first message
    char *message = ctx->message_queue.messages[0];
    
    // Shift remaining messages
    for (int i = 1; i < ctx->message_queue.count; i++) {
        ctx->message_queue.messages[i-1] = ctx->message_queue.messages[i];
    }
    ctx->message_queue.count--;
    
    pthread_mutex_unlock(&ctx->message_queue.mutex);
    return message;
}

// static int message_queue_peek(ZMQContext *ctx) {
//     pthread_mutex_lock(&ctx->message_queue.mutex);
//     int count = ctx->message_queue.count;
//     pthread_mutex_unlock(&ctx->message_queue.mutex);
//     return count;
// }

// Background polling thread function
static void* zmq_polling_thread(void *arg) {
    ZMQContext *ctx = (ZMQContext*)arg;
    
    LOG_INFO("ZMQ: Background polling thread started");
    printf("ZMQ: Background polling thread started\n");
    
    while (!ctx->should_exit) {
        zmq_pollitem_t items[1];
        items[0].socket = ctx->socket;
        items[0].events = ZMQ_POLLIN;
        
        // Poll with 100ms timeout
        int poll_result = zmq_poll(items, 1, 100);
        LOG_DEBUG("ZMQ: Poll result: %d, revents: %d", poll_result, items[0].revents);
        
        if (poll_result > 0 && items[0].revents & ZMQ_POLLIN) {
            // Receive message
            char buffer[ZMQ_BUFFER_SIZE];
            int received = zmq_recv(ctx->socket, buffer, sizeof(buffer) - 1, 0);
            
            if (received > 0) {
                buffer[received] = '\0';
                LOG_DEBUG("ZMQ: Background thread received %d bytes: %s", received, buffer);
                
                // Parse message to check type
                cJSON *json = cJSON_Parse(buffer);
                if (json) {
                    cJSON *type_obj = cJSON_GetObjectItem(json, "messageType");
                    if (type_obj && cJSON_IsString(type_obj)) {
                        const char *type = type_obj->valuestring;
                        
                        // Handle ACK messages immediately
                        if (strcmp(type, "ACK") == 0) {
                            cJSON *id_obj = cJSON_GetObjectItem(json, "messageId");
                            if (id_obj && cJSON_IsString(id_obj)) {
                                remove_from_pending_queue(ctx, id_obj->valuestring);
                            }
                        }
                        // Handle termination messages
                        else if (strcmp(type, "TERMINATE") == 0) {
                            LOG_INFO("ZMQ: Received termination message");
                            ctx->should_exit = true;
                        }
                        // Queue other messages for main thread
                        else {
                            message_queue_push(ctx, buffer);
                        }
                    }
                    cJSON_Delete(json);
                }
            }
        }
    }
    
    LOG_INFO("ZMQ: Background polling thread exiting");
    return NULL;
}

// Start background polling thread
static int start_polling_thread(ZMQContext *ctx) {
    if (ctx->polling_thread_running) {
        return 0; // Already running
    }
    
    ctx->should_exit = false;
    int result = pthread_create(&ctx->polling_thread, NULL, zmq_polling_thread, ctx);
    if (result != 0) {
        LOG_ERROR("ZMQ: Failed to create polling thread: %d", result);
        return -1;
    }
    
    ctx->polling_thread_running = true;
    LOG_INFO("ZMQ: Background polling thread started successfully");
    return 0;
}
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
    
    // Initialize duplicate detection
    ctx->seen_message_count = 0;
    memset(ctx->seen_messages, 0, sizeof(ctx->seen_messages));
    
    // Initialize thread pool for asynchronous tool execution
    ctx->thread_pool = NULL;
    
    // Initialize background polling thread fields
    ctx->polling_thread_running = false;
    ctx->should_exit = false;
    
    // Initialize message queue
    ctx->message_queue.messages = NULL;
    ctx->message_queue.capacity = 0;
    ctx->message_queue.count = 0;
    pthread_mutex_init(&ctx->message_queue.mutex, NULL);
    pthread_cond_init(&ctx->message_queue.cond, NULL);
    
    // Check if thread pool should be enabled (default: enabled for daemon mode)
    const char *disable_thread_pool = getenv("KLAWED_ZMQ_DISABLE_THREAD_POOL");
    if (!disable_thread_pool || strcmp(disable_thread_pool, "1") != 0) {
        // Create thread pool with reasonable defaults
        ctx->thread_pool = zmq_thread_pool_init(4, 50); // 4 threads, max 50 queued tasks
        if (ctx->thread_pool) {
            LOG_INFO("ZMQ: Thread pool initialized for asynchronous tool execution");
        } else {
            LOG_WARN("ZMQ: Failed to initialize thread pool");
        }
    } else {
        LOG_INFO("ZMQ: Thread pool disabled by environment variable");
    }

    LOG_INFO("ZMQ: Message ID/ACK system initialized (salt: 0x%08x)", ctx->salt);

    LOG_INFO("ZMQ: Socket initialization completed successfully");
    return ctx;
#else
    (void)endpoint;
    (void)socket_type;
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
    
    // Clean up seen messages
    for (int i = 0; i < ctx->seen_message_count; i++) {
        if (ctx->seen_messages[i].message_id) {
            free(ctx->seen_messages[i].message_id);
            ctx->seen_messages[i].message_id = NULL;
        }
    }
    ctx->seen_message_count = 0;

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
    
    // Clean up thread pool
    if (ctx->thread_pool) {
        zmq_thread_pool_cleanup(ctx->thread_pool);
        ctx->thread_pool = NULL;
    }
    
    // Signal polling thread to exit and wait for it
    if (ctx->polling_thread_running) {
        ctx->should_exit = true;
        pthread_join(ctx->polling_thread, NULL);
        ctx->polling_thread_running = false;
    }
    
    // Clean up message queue
    pthread_mutex_lock(&ctx->message_queue.mutex);
    for (int i = 0; i < ctx->message_queue.count; i++) {
        if (ctx->message_queue.messages[i]) {
            free(ctx->message_queue.messages[i]);
        }
    }
    if (ctx->message_queue.messages) {
        free(ctx->message_queue.messages);
    }
    pthread_mutex_unlock(&ctx->message_queue.mutex);
    
    pthread_mutex_destroy(&ctx->message_queue.mutex);
    pthread_cond_destroy(&ctx->message_queue.cond);

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

    int rc = zmq_send(ctx->socket, message, message_len, ZMQ_DONTWAIT);
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
            return ZMQ_ERROR_RECEIVE_TIMEOUT;
        } else {
            LOG_ERROR("ZMQ: Failed to receive message: %s", zmq_strerror(err));
            return ZMQ_ERROR_RECEIVE_FAILED;
        }
    }

    LOG_DEBUG("ZMQ: Received %d bytes from endpoint: %s", rc,
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
    LOG_INFO("ZMQ: Generated message ID %s (seq: %d, ts: %lld, salt: 0x%08x, message sample: %zu chars)",
              out_id, ctx->message_sequence, (long long)timestamp_ms, ctx->salt,
              (size_t)(message_len < HASH_SAMPLE_SIZE ? message_len : HASH_SAMPLE_SIZE));

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
        LOG_ERROR("ZMQ: Failed to generate message ID for %zu byte message", message_len);
        return ZMQ_ERROR_SEND_FAILED;
    }

    LOG_DEBUG("ZMQ: Wrapping message with ID %s", message_id);

    // Wrap message with ID field
    cJSON *json = cJSON_Parse(message);
    if (!json) {
        LOG_ERROR("ZMQ: Failed to parse message JSON for wrapping (message preview: %.*s)",
                 (int)(message_len > 100 ? 100 : message_len), message);
        return ZMQ_ERROR_SEND_FAILED;
    }

    // Add messageId field
    cJSON_AddStringToObject(json, "messageId", message_id);

    char *wrapped_message = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!wrapped_message) {
        LOG_ERROR("ZMQ: Failed to serialize wrapped message with ID %s", message_id);
        return ZMQ_ERROR_SEND_FAILED;
    }

    size_t wrapped_len = strlen(wrapped_message);
    LOG_DEBUG("ZMQ: Wrapped message length: %zu bytes (original: %zu bytes)", wrapped_len, message_len);

    // Send the wrapped message
    LOG_DEBUG("ZMQ: About to send message %s (wrapped length: %zu, original length: %zu)",
              message_id, wrapped_len, message_len);
    int rc = zmq_send(ctx->socket, wrapped_message, wrapped_len, ZMQ_DONTWAIT);

    if (rc >= 0) {
        LOG_DEBUG("ZMQ: Sent message %s (%zu bytes -> %d bytes sent, pending queue: %d/%d)",
                  message_id, wrapped_len, rc, ctx->pending_queue.count + 1, ctx->pending_queue.max_pending);
        LOG_DEBUG("ZMQ: Message content preview (first 500 chars): %.*s",
                 (int)(wrapped_len > 500 ? 500 : wrapped_len), wrapped_message);

        // Add to pending queue
        int64_t sent_time = get_current_time_ms();
        if (add_to_pending_queue(ctx, message_id, wrapped_message, sent_time) == 0) {
            LOG_DEBUG("ZMQ: Message %s added to pending queue at time %lld ms",
                     message_id, (long long)sent_time);
            LOG_DEBUG("ZMQ: Pending queue now has %d messages", ctx->pending_queue.count);
            log_pending_queue_state(ctx, "after_add");
            
            // Return message ID to caller if requested
            if (message_id_out && message_id_out_size >= MESSAGE_ID_HEX_LENGTH) {
                strlcpy(message_id_out, message_id, message_id_out_size);
                LOG_DEBUG("ZMQ: Returned message ID to caller: %s", message_id);
            }
            free(wrapped_message);
            return ZMQ_ERROR_NONE;
        } else {
            LOG_WARN("ZMQ: Message %s sent but failed to track in pending queue (queue full: %d/%d)",
                    message_id, ctx->pending_queue.count, ctx->pending_queue.max_pending);
            free(wrapped_message);
            return ZMQ_ERROR_NOMEM;
        }
    } else {
        int err = errno;
        LOG_ERROR("ZMQ: Failed to send message %s: %s (wrapped length: %zu)",
                 message_id, zmq_strerror(err), wrapped_len);
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
        LOG_ERROR("ZMQ: Invalid parameters for send_ack (ctx=%p, message_id=%p)", (void*)ctx, (const void*)message_id);
        return ZMQ_ERROR_INVALID_PARAM;
    }

    if (!ctx->socket) {
        LOG_ERROR("ZMQ: No socket available for sending ACK for message %s", message_id);
        return ZMQ_ERROR_SEND_FAILED;
    }

    LOG_DEBUG("ZMQ: Creating ACK for message %s", message_id);

    // Create ACK message
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        LOG_ERROR("ZMQ: Failed to create JSON object for ACK for message %s", message_id);
        return ZMQ_ERROR_SEND_FAILED;
    }

    cJSON_AddStringToObject(json, "messageType", "ACK");
    cJSON_AddStringToObject(json, "messageId", message_id);

    char *ack_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!ack_str) {
        LOG_ERROR("ZMQ: Failed to serialize ACK JSON for message %s", message_id);
        return ZMQ_ERROR_SEND_FAILED;
    }

    size_t ack_len = strlen(ack_str);
    LOG_DEBUG("ZMQ: ACK message length: %zu bytes", ack_len);

    int rc = zmq_send(ctx->socket, ack_str, ack_len, ZMQ_DONTWAIT);
    free(ack_str);

    if (rc >= 0) {
        LOG_DEBUG("ZMQ: Sent ACK for message %s (%d bytes)", message_id, rc);
        return ZMQ_ERROR_NONE;
    } else {
        int err = errno;
        LOG_ERROR("ZMQ: Failed to send ACK for message %s: %s", message_id, zmq_strerror(err));
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
        LOG_ERROR("ZMQ: Invalid parameters for process_ack (ctx=%p, message_id=%p)", (void*)ctx, (const void*)message_id);
        return ZMQ_ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("ZMQ: Processing ACK for message %s (pending queue size: %d)",
              message_id, ctx->pending_queue.count);
    
    log_pending_queue_state(ctx, "before_ack");

    int result = remove_from_pending_queue(ctx, message_id);
    if (result == 0) {
        LOG_INFO("ZMQ: Successfully processed ACK for message %s (pending queue size: %d)",
                 message_id, ctx->pending_queue.count);
        log_pending_queue_state(ctx, "after_ack");
    } else {
        LOG_WARN("ZMQ: Failed to process ACK for message %s (not found in pending queue)", message_id);
        log_pending_queue_state(ctx, "after_failed_ack");
    }

    return result;
#else
    (void)ctx;
    (void)message_id;
    return ZMQ_ERROR_NOT_SUPPORTED;
#endif
}

/**
 * Check if a message ID has been seen recently (duplicate detection)
 */
static int is_duplicate_message(ZMQContext *ctx, const char *message_id) {
#ifdef HAVE_ZMQ
    if (!ctx || !message_id) return 0;
    
    // Simple linear search for now - could be optimized if needed
    for (int i = 0; i < ctx->seen_message_count; i++) {
        if (strcmp(ctx->seen_messages[i].message_id, message_id) == 0) {
            int64_t current_time = get_current_time_ms();
            int64_t age = current_time - ctx->seen_messages[i].timestamp_ms;
            
            if (age < SEEN_MESSAGE_TTL_MS) {
                LOG_WARN("ZMQ: Duplicate message detected! ID=%s (age=%lldms, TTL=%dms)",
                         message_id, (long long)age, SEEN_MESSAGE_TTL_MS);
                return 1;
            } else {
                // Expired, can be removed
                free(ctx->seen_messages[i].message_id);
                // Shift remaining elements
                for (int j = i; j < ctx->seen_message_count - 1; j++) {
                    ctx->seen_messages[j] = ctx->seen_messages[j + 1];
                }
                ctx->seen_message_count--;
                i--; // Check current position again
            }
        }
    }
    return 0;
#else
    (void)ctx;
    (void)message_id;
    return 0;
#endif
}

/**
 * Add a message ID to the seen messages list
 */
static void add_seen_message(ZMQContext *ctx, const char *message_id) {
#ifdef HAVE_ZMQ
    if (!ctx || !message_id || ctx->seen_message_count >= MAX_SEEN_MESSAGES) {
        return;
    }
    
    // Check if we need to make room
    if (ctx->seen_message_count == MAX_SEEN_MESSAGES) {
        // Remove oldest message
        free(ctx->seen_messages[0].message_id);
        for (int i = 0; i < ctx->seen_message_count - 1; i++) {
            ctx->seen_messages[i] = ctx->seen_messages[i + 1];
        }
        ctx->seen_message_count--;
    }
    
    // Add new message
    ctx->seen_messages[ctx->seen_message_count].message_id = strdup(message_id);
    if (ctx->seen_messages[ctx->seen_message_count].message_id) {
        ctx->seen_messages[ctx->seen_message_count].timestamp_ms = get_current_time_ms();
        ctx->seen_message_count++;
        LOG_DEBUG("ZMQ: Added message %s to seen messages list (count: %d)",
                  message_id, ctx->seen_message_count);
    }
#else
    (void)ctx;
    (void)message_id;
#endif
}

/**
 * Log current state of pending queue for debugging
 */
static void log_pending_queue_state(ZMQContext *ctx, const char *context) {
#ifdef HAVE_ZMQ
    if (!ctx) return;
    
    LOG_DEBUG("ZMQ: Pending queue state (%s): count=%d, max=%d", 
              context, ctx->pending_queue.count, ctx->pending_queue.max_pending);
    
    ZMQPendingMessage *curr = ctx->pending_queue.head;
    int i = 0;
    while (curr) {
        int64_t elapsed = get_current_time_ms() - curr->sent_time_ms;
        LOG_DEBUG("ZMQ:   [%d] ID=%s, elapsed=%lldms, retries=%d/%d",
                  i++, curr->message_id, (long long)elapsed,
                  curr->retry_count, ctx->pending_queue.max_retries);
        curr = curr->next;
    }
#else
    (void)ctx;
    (void)context;
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
        LOG_DEBUG("ZMQ: No pending messages to check");
        return 0; // No pending messages
    }

    LOG_DEBUG("ZMQ: Checking pending messages (count: %d, current time: %lld)",
              ctx->pending_queue.count, (long long)current_time_ms);
    log_pending_queue_state(ctx, "before_resend_check");
    
    int resent_count = 0;
    ZMQPendingMessage *curr = ctx->pending_queue.head;
    ZMQPendingMessage *prev = NULL;
    int checked_count = 0;

    while (curr) {
        checked_count++;
        int64_t elapsed = current_time_ms - curr->sent_time_ms;
        LOG_DEBUG("ZMQ: Checking message %s (elapsed: %lld ms, timeout: %lld ms, retries: %d/%d)",
                  curr->message_id, (long long)elapsed, (long long)ctx->pending_queue.timeout_ms,
                  curr->retry_count, ctx->pending_queue.max_retries);

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
                LOG_DEBUG("ZMQ: Dropped message %s from pending queue (new count: %d)",
                          to_delete->message_id, ctx->pending_queue.count);
                continue;
            }

            // Retry sending
            curr->retry_count++;
            curr->sent_time_ms = current_time_ms;

            LOG_INFO("ZMQ: Resending message %s (attempt %d/%d, elapsed: %lld ms)",
                    curr->message_id, curr->retry_count, ctx->pending_queue.max_retries,
                    (long long)elapsed);
            
            int rc = zmq_send(ctx->socket, curr->message_json,
                             strlen(curr->message_json), 0);

            if (rc >= 0) {
                LOG_INFO("ZMQ: Resent message %s (attempt %d/%d, %d bytes)",
                        curr->message_id, curr->retry_count, ctx->pending_queue.max_retries, rc);
                resent_count++;
            } else {
                int err = errno;
                LOG_ERROR("ZMQ: Failed to resend message %s: %s",
                         curr->message_id, zmq_strerror(err));
            }
        }

        prev = curr;
        curr = curr->next;
    }

    LOG_DEBUG("ZMQ: Checked %d pending messages, resent %d", checked_count, resent_count);
    if (resent_count > 0) {
        log_pending_queue_state(ctx, "after_resend");
    }
    return resent_count;
#else
    (void)ctx;
    (void)current_time_ms;
    return ZMQ_ERROR_NOT_SUPPORTED;
#endif
}

/**
 * Get file descriptor for ZMQ socket (for use with select/poll)
 */
int zmq_socket_get_fd(ZMQContext *ctx) {
#ifdef HAVE_ZMQ
    if (!ctx || !ctx->socket) {
        return -1;
    }

    int fd = -1;
    size_t fd_size = sizeof(fd);
    if (zmq_getsockopt(ctx->socket, ZMQ_FD, &fd, &fd_size) != 0) {
        LOG_ERROR("ZMQ: Failed to get socket file descriptor: %s", zmq_strerror(errno));
        return -1;
    }

    LOG_DEBUG("ZMQ: Socket file descriptor: %d", fd);
    return fd;
#else
    (void)ctx;
    return -1;
#endif
}



/**
 * Process a message from a buffer (already received)
 */
static int zmq_process_message_from_buffer(ZMQContext *ctx, struct ConversationState *state,
                                          struct TUIState *tui, const char *buffer, int buffer_len) {
#ifdef HAVE_ZMQ
    if (!ctx || !state || !buffer || buffer_len <= 0) {
        LOG_ERROR("ZMQ: Invalid parameters for process_message_from_buffer");
        return -1;
    }
    (void)tui; // Unused parameter for now

    LOG_INFO("ZMQ: Processing %d byte message from buffer", buffer_len);
    LOG_DEBUG("ZMQ: Raw message (first 1000 chars): %.*s",
             (int)(buffer_len > 1000 ? 1000 : buffer_len), buffer);

    // Parse JSON message
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        LOG_ERROR("ZMQ: Failed to parse JSON message");
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            LOG_ERROR("ZMQ: JSON error near: %s", error_ptr);
        }

        // Send error response with message ID
        zmq_send_json_response(ctx, "ERROR", "Invalid JSON");
        return -1;
    }

    // Extract message type and message ID
    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
    cJSON *message_id = cJSON_GetObjectItem(json, "messageId");
    cJSON *content = cJSON_GetObjectItem(json, "content");

    // Log message details
    if (message_type && cJSON_IsString(message_type)) {
        LOG_INFO("ZMQ: Message type: %s", message_type->valuestring);
    } else {
        LOG_WARN("ZMQ: Message missing or invalid messageType field");
    }

    if (message_id && cJSON_IsString(message_id)) {
        LOG_DEBUG("ZMQ: Message ID: %s", message_id->valuestring);
    } else {
        LOG_DEBUG("ZMQ: Message has no ID (or ID is not a string)");
    }

    // Handle ACK messages
    if (message_type && cJSON_IsString(message_type) &&
        strcmp(message_type->valuestring, "ACK") == 0) {
        if (message_id && cJSON_IsString(message_id)) {
            LOG_INFO("ZMQ: Received ACK for message %s", message_id->valuestring);
            
            // Check for duplicate ACK
            if (is_duplicate_message(ctx, message_id->valuestring)) {
                LOG_WARN("ZMQ: Duplicate ACK received for message %s, ignoring", message_id->valuestring);
                cJSON_Delete(json);
                return 0;
            }
            
            LOG_DEBUG("ZMQ: Calling zmq_process_ack for message %s", message_id->valuestring);
            zmq_process_ack(ctx, message_id->valuestring);
            // Mark this ACK as seen
            add_seen_message(ctx, message_id->valuestring);
        } else {
            LOG_WARN("ZMQ: ACK message missing messageId");
        }
        cJSON_Delete(json);
        return 0; // ACK processed successfully
    }

    // Send ACK for all non-ACK messages
    if (message_id && cJSON_IsString(message_id)) {
        // Check for duplicate message
        if (is_duplicate_message(ctx, message_id->valuestring)) {
            LOG_WARN("ZMQ: Duplicate message detected! ID=%s, type=%s, ignoring",
                     message_id->valuestring,
                     message_type && cJSON_IsString(message_type) ? message_type->valuestring : "unknown");
            // Still send ACK for duplicate to prevent retries
            LOG_INFO("ZMQ: Sending ACK for duplicate message %s", message_id->valuestring);
            int ack_result = zmq_send_ack(ctx, message_id->valuestring);
            if (ack_result != ZMQ_ERROR_NONE) {
                LOG_ERROR("ZMQ: Failed to send ACK for duplicate message %s (error: %d)",
                         message_id->valuestring, ack_result);
                // Don't delete JSON yet - let it be processed as a duplicate again on retry
            }
            cJSON_Delete(json);
            return 0;
        }
        
        LOG_INFO("ZMQ: Sending ACK for message %s (type: %s)",
                 message_id->valuestring,
                 message_type && cJSON_IsString(message_type) ? message_type->valuestring : "unknown");
        int ack_result = zmq_send_ack(ctx, message_id->valuestring);
        if (ack_result != ZMQ_ERROR_NONE) {
            LOG_ERROR("ZMQ: Failed to send ACK for message %s (error: %d)",
                     message_id->valuestring, ack_result);
            // Don't mark as seen if ACK failed - let client retry
            cJSON_Delete(json);
            return -1;
        } else {
            LOG_DEBUG("ZMQ: ACK sent successfully for message %s", message_id->valuestring);
        }
        // Mark this message as seen (only if ACK succeeded)
        add_seen_message(ctx, message_id->valuestring);
    } else {
        LOG_DEBUG("ZMQ: No message ID to ACK");
    }

    if (message_type && cJSON_IsString(message_type)) {
        if (strcmp(message_type->valuestring, "TEXT") == 0 &&
            content && cJSON_IsString(content)) {

            // Process text message with interactive tool call support
            size_t content_len = strlen(content->valuestring);
            LOG_INFO("ZMQ: Processing TEXT message (length: %zu)", content_len);

            // Log a preview of the content (first 200 chars)
            if (content_len > 0) {
                int preview_len = (int)(content_len > 200 ? 200 : content_len);
                LOG_DEBUG("ZMQ: TEXT message preview: %.*s%s",
                         preview_len, content->valuestring,
                         content_len > 200 ? "..." : "");
            }

            // Print the full TEXT message content to console (daemon messages should be shown)
            // Message content is logged via LOG_INFO, no console output needed for daemon
            fflush(stdout);

            // Process interactively (handles tool calls recursively)
            LOG_DEBUG("ZMQ: Starting interactive processing");
            int interactive_result = zmq_process_interactive(ctx, state, content->valuestring);

            if (interactive_result != 0) {
                LOG_ERROR("ZMQ: Interactive processing failed with error code: %d", interactive_result);
                // Send error response with message ID
                zmq_send_json_response(ctx, "ERROR", "Interactive processing failed");
            } else {
                LOG_INFO("ZMQ: Interactive processing completed successfully");
            }
        } else {
            LOG_WARN("ZMQ: Unsupported message type received: %s",
                     message_type->valuestring);
            // Log all available fields for debugging
            cJSON *child = json->child;
            while (child) {
                if (cJSON_IsString(child)) {
                    LOG_DEBUG("ZMQ: Field '%s' = '%s'", child->string, child->valuestring);
                } else if (cJSON_IsNumber(child)) {
                    LOG_DEBUG("ZMQ: Field '%s' = %f", child->string, child->valuedouble);
                } else if (cJSON_IsBool(child)) {
                    LOG_DEBUG("ZMQ: Field '%s' = %s", child->string, child->valueint ? "true" : "false");
                } else {
                    LOG_DEBUG("ZMQ: Field '%s' (type: %d)", child->string, child->type);
                }
                child = child->next;
            }
            // Send error response with message ID
            zmq_send_json_response(ctx, "ERROR", "Unsupported message type");
        }
    } else {
        LOG_WARN("ZMQ: Invalid message format received - missing messageType");
        // Send error response with message ID
        zmq_send_json_response(ctx, "ERROR", "Invalid message format - missing messageType");
    }

    cJSON_Delete(json);

    LOG_INFO("ZMQ: Message processing completed");
    return 0;
#else
    (void)ctx;
    (void)state;
    (void)tui;
    (void)buffer;
    (void)buffer_len;
    return -1;
#endif
}

int zmq_socket_process_message(ZMQContext *ctx, struct ConversationState *state, struct TUIState *tui) {
#ifdef HAVE_ZMQ
    if (!ctx || !state) {
        LOG_ERROR("ZMQ: Invalid parameters for process_message");
        return -1;
    }

    LOG_DEBUG("ZMQ: Waiting for incoming message on endpoint: %s",
              ctx->endpoint ? ctx->endpoint : "unknown");

    char buffer[ZMQ_BUFFER_SIZE];
    // Use infinite timeout (-1) for daemon mode to wait indefinitely
    int received = zmq_socket_receive(ctx, buffer, sizeof(buffer), -1);
    if (received <= 0) {
        LOG_WARN("ZMQ: Failed to receive message or connection closed");
        return -1;
    }

    // Null-terminate the buffer
    if (received < (int)sizeof(buffer)) {
        buffer[received] = '\0';
    } else {
        buffer[sizeof(buffer) - 1] = '\0';
    }

    return zmq_process_message_from_buffer(ctx, state, tui, buffer, received);
#else
    (void)ctx;
    (void)state;
    (void)tui;
    return -1;
#endif
}

// Helper function to send a JSON response with message ID for reliable delivery
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

    // Send with message ID for reliable delivery
    char message_id[MESSAGE_ID_HEX_LENGTH];
    int result = zmq_socket_send_with_id(ctx, response_str, strlen(response_str),
                                         message_id, sizeof(message_id));
    if (result != 0) {
        LOG_ERROR("ZMQ: Failed to send JSON response with ID");
    } else {
        LOG_DEBUG("ZMQ: Sent %s response with message ID: %s", message_type, message_id);
    }

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

// Helper function to send a tool result response with message ID for reliable delivery
int zmq_send_tool_result(ZMQContext *ctx, const char *tool_name, const char *tool_id,
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

    // Send with message ID for reliable delivery
    char message_id[MESSAGE_ID_HEX_LENGTH];
    int result = zmq_socket_send_with_id(ctx, response_str, strlen(response_str),
                                         message_id, sizeof(message_id));
    if (result != 0) {
        LOG_ERROR("ZMQ: Failed to send TOOL_RESULT with ID (tool: %s, id: %s)",
                  tool_name, tool_id);
    } else {
        LOG_DEBUG("ZMQ: Sent TOOL_RESULT with message ID: %s (tool: %s, id: %s)",
                  message_id, tool_name, tool_id);
    }

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

// Helper function to send a tool execution request with message ID for reliable delivery
int zmq_send_tool_request(ZMQContext *ctx, const char *tool_name, const char *tool_id,
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

    // Send with message ID for reliable delivery
    char message_id[MESSAGE_ID_HEX_LENGTH];
    int result = zmq_socket_send_with_id(ctx, request_str, strlen(request_str),
                                         message_id, sizeof(message_id));
    if (result != 0) {
        LOG_ERROR("ZMQ: Failed to send TOOL request with ID (tool: %s, id: %s)",
                  tool_name, tool_id);
    } else {
        LOG_INFO("ZMQ: Sent TOOL request with message ID: %s (tool: %s, id: %s)",
                 message_id, tool_name, tool_id);
    }

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

                // AI response is logged via LOG_INFO, no console output needed for daemon
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
            LOG_DEBUG("ZMQ: Daemon mode tool execution - checking tool availability");

            // Allocate results array
            InternalContent *results = calloc((size_t)tool_count, sizeof(InternalContent));
            if (!results) {
                LOG_ERROR("ZMQ: Failed to allocate tool result buffer");
                zmq_send_json_response(ctx, "ERROR", "Failed to allocate tool result buffer");
                api_response_free(api_response);
                return -1;
            }

            // Submit all tools to thread pool
            int tools_submitted = 0;
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

                // Validate that the tool is in the allowed tools list
                if (!is_tool_allowed(tool->name, state)) {
                    LOG_ERROR("ZMQ: Tool validation failed: '%s' was not provided in tools list (cannot execute tool in daemon mode)",
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

                // Submit task to thread pool for asynchronous execution
                LOG_INFO("ZMQ: Submitting tool '%s' (id: %s) to thread pool",
                         tool->name, tool->id);
                
                int submit_result = zmq_thread_pool_submit_task(ctx->thread_pool,
                                                                tool->name,
                                                                tool->id,
                                                                input,
                                                                state,
                                                                ctx);
                
                if (submit_result == 0) {
                    tools_submitted++;
                    // Note: input is now owned by the thread pool task, don't delete it here
                } else {
                    LOG_ERROR("ZMQ: Failed to submit tool '%s' to thread pool", tool->name);
                    // Clean up input since submission failed
                    if (input) cJSON_Delete(input);
                }
            }

            // Wait for all tools to complete if any were submitted
            if (tools_submitted > 0 && ctx->thread_pool) {
                LOG_INFO("ZMQ: Waiting for %d tool(s) to complete", tools_submitted);
                int wait_result = zmq_thread_pool_wait_for_completion(ctx->thread_pool, 0);
                if (wait_result != 0) {
                    LOG_WARN("ZMQ: Timeout or error waiting for tool completion");
                } else {
                    LOG_INFO("ZMQ: All tools completed successfully");
                }
            }

            // Tool results are sent by worker threads via ZMQ
            // We need to collect them from the message queue
            for (int i = 0; i < tool_count; i++) {
                ToolCall *tool = &tool_calls_array[i];
                if (!tool->name || !tool->id) continue;

                // Create placeholder result (actual result sent by worker thread)
                results[i].type = INTERNAL_TOOL_RESPONSE;
                results[i].tool_id = strdup(tool->id);
                results[i].tool_name = strdup(tool->name);
                results[i].tool_output = cJSON_CreateObject();
                cJSON_AddStringToObject(results[i].tool_output, "status", "completed");
                cJSON_AddStringToObject(results[i].tool_output, "message", "Tool executed asynchronously");
                results[i].is_error = 0;
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
    LOG_INFO("ZMQ: Starting ZMQ daemon mode with background polling thread");
    LOG_INFO("ZMQ: Endpoint: %s", ctx->endpoint);
    LOG_INFO("ZMQ: Socket type: ZMQ_PAIR (Peer-to-peer)");
    LOG_INFO("ZMQ: Message ID/ACK system enabled (salt: 0x%08x)", ctx->salt);
    LOG_INFO("ZMQ: Pending queue configuration:");
    LOG_INFO("ZMQ:   Max pending messages: %d", ctx->pending_queue.max_pending);
    LOG_INFO("ZMQ:   ACK timeout: %lld ms", (long long)ctx->pending_queue.timeout_ms);
    LOG_INFO("ZMQ:   Max retries: %d", ctx->pending_queue.max_retries);
    LOG_INFO("ZMQ: Architecture: Main thread handles LLM calls, background thread polls ZMQ");
    LOG_INFO("ZMQ: =========================================");

    // Daemon startup is logged via LOG_INFO, no console output needed
    fflush(stdout);

    // Start background polling thread
    if (start_polling_thread(ctx) != 0) {
        LOG_ERROR("ZMQ: Failed to start background polling thread");
        return -1;
    }

    int message_count = 0;
    int error_count = 0;
    int64_t last_resend_check = 0;
    int64_t last_queue_log = 0;
    const int64_t RESEND_CHECK_INTERVAL_MS = 1000; // Check pending messages every second
    const int64_t QUEUE_LOG_INTERVAL_MS = 5000; // Log queue state every 5 seconds

    // Main event loop - uses message queue from background thread
    LOG_INFO("ZMQ: Entering main event loop");
    while (ctx->enabled && !ctx->should_exit) {
        int64_t current_time = get_current_time_ms();
        LOG_DEBUG("ZMQ: Main loop iteration");

        // 1. Check and resend pending messages periodically
        if (current_time - last_resend_check >= RESEND_CHECK_INTERVAL_MS) {
            int resent = zmq_check_and_resend_pending(ctx, current_time);
            if (resent > 0) {
                LOG_INFO("ZMQ: Resent %d pending message(s)", resent);
            }
            last_resend_check = current_time;
        }

        // Log pending queue state periodically
        if (current_time - last_queue_log >= QUEUE_LOG_INTERVAL_MS) {
            if (ctx->pending_queue.count > 0) {
                LOG_INFO("ZMQ: Pending queue: %d message(s) waiting for ACK", ctx->pending_queue.count);
                // Log detailed state at DEBUG level
                log_pending_queue_state(ctx, "periodic_check");
            }
            last_queue_log = current_time;
        }

        // 2. Check for messages from background polling thread
        char *message = message_queue_pop(ctx, 100); // 100ms timeout
        if (message) {
            message_count++;
            error_count = 0; // Reset error count on successful receive

            LOG_INFO("ZMQ: Processing message #%d from background thread", message_count);
            
            // Process the message
            int result = zmq_process_message_from_buffer(ctx, state, NULL, message, (int)strlen(message));
            free(message);
            
            if (result != 0) {
                LOG_ERROR("ZMQ: Failed to process message #%d", message_count);
                error_count++;
                if (error_count >= 10) {
                    LOG_ERROR("ZMQ: Too many consecutive errors (%d), stopping daemon", error_count);
                    break;
                }
            }
        }
    }

    LOG_INFO("ZMQ: =========================================");
    LOG_INFO("ZMQ: ZMQ daemon mode stopping");
    LOG_INFO("ZMQ: Total messages processed: %d", message_count);
    LOG_INFO("ZMQ: Total errors: %d", error_count);
    LOG_INFO("ZMQ: =========================================");

    // Daemon stop is logged via LOG_INFO, no console output needed
    fflush(stdout);

    return 0;
#else
    (void)ctx;
    (void)state;
    return -1;
#endif
}
