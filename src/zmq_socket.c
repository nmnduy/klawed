/*
 * zmq_socket.c - ZeroMQ socket implementation for Klawed
 */

#include "zmq_socket.h"
#include "zmq_config.h"
#include "klawed_internal.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>

// Include ZMQ headers if available
#ifdef HAVE_ZMQ
#include <zmq.h>
#include <cjson/cJSON.h>
#include <ctype.h>
#endif

// Default buffer size for ZMQ messages
#define ZMQ_BUFFER_SIZE 65536

// Heartbeat message types
#define ZMQ_HEARTBEAT_PING "HEARTBEAT_PING"
#define ZMQ_HEARTBEAT_PONG "HEARTBEAT_PONG"

// Message queue structure
struct ZMQMessageQueue {
    char **messages;           // Array of message strings
    size_t *message_sizes;     // Array of message sizes
    size_t capacity;           // Maximum number of messages
    size_t size;               // Current number of messages
    size_t head;               // Index of first message
    size_t tail;               // Index where next message will be added
    size_t total_bytes;        // Total bytes in queue
};

// Forward declarations
#ifdef HAVE_ZMQ
static int zmq_process_interactive(ZMQContext *ctx, struct ConversationState *state, const char *user_input);
static void zmq_set_error(ZMQContext *ctx, ZMQErrorCode error_code, const char *format, ...);
static int zmq_check_connection_health(ZMQContext *ctx);
static int zmq_attempt_reconnect(ZMQContext *ctx);

static const char* zmq_error_to_string(ZMQErrorCode error_code);

// Message queue functions
static ZMQMessageQueue* zmq_message_queue_create(size_t capacity);
static void zmq_message_queue_free(ZMQMessageQueue *queue);
static int zmq_message_queue_push(ZMQMessageQueue *queue, const char *message, size_t message_len);
static char* zmq_message_queue_pop(ZMQMessageQueue *queue, size_t *message_len);
static size_t zmq_message_queue_size(const ZMQMessageQueue *queue);
static size_t zmq_message_queue_bytes(const ZMQMessageQueue *queue);
#endif

ZMQContext* zmq_socket_init(const char *endpoint, int socket_type) {
#ifdef HAVE_ZMQ
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
    LOG_DEBUG("ZMQ: Allocated ZMQ context structure");

    // Initialize timeout configuration from environment variables
    ctx->receive_timeout = zmq_get_timeout_from_env(ZMQ_ENV_RECEIVE_TIMEOUT, ZMQ_DEFAULT_RECEIVE_TIMEOUT);
    ctx->send_timeout = zmq_get_timeout_from_env(ZMQ_ENV_SEND_TIMEOUT, ZMQ_DEFAULT_SEND_TIMEOUT);
    ctx->connect_timeout = zmq_get_timeout_from_env(ZMQ_ENV_CONNECT_TIMEOUT, ZMQ_DEFAULT_CONNECT_TIMEOUT);
    ctx->heartbeat_interval = zmq_get_timeout_from_env(ZMQ_ENV_HEARTBEAT_INTERVAL, ZMQ_DEFAULT_HEARTBEAT_INTERVAL);
    ctx->reconnect_interval = zmq_get_timeout_from_env(ZMQ_ENV_RECONNECT_INTERVAL, ZMQ_DEFAULT_RECONNECT_INTERVAL);
    ctx->max_reconnect_attempts = zmq_get_timeout_from_env(ZMQ_ENV_MAX_RECONNECT_ATTEMPTS, ZMQ_DEFAULT_MAX_RECONNECT_ATTEMPTS);

    // Initialize queue sizes
    ctx->send_queue_size = zmq_get_timeout_from_env(ZMQ_ENV_SEND_QUEUE_SIZE, ZMQ_DEFAULT_SEND_QUEUE_SIZE);
    ctx->receive_queue_size = zmq_get_timeout_from_env(ZMQ_ENV_RECEIVE_QUEUE_SIZE, ZMQ_DEFAULT_RECEIVE_QUEUE_SIZE);

    // Initialize message queues if enabled
    ctx->send_queue = NULL;
    ctx->receive_queue = NULL;

    // Initialize state tracking
    ctx->reconnect_attempts = 0;
    ctx->last_heartbeat = time(NULL);
    ctx->last_heartbeat_response = time(NULL);
    ctx->last_activity = time(NULL);

    // Initialize error state
    ctx->last_error = ZMQ_ERROR_NONE;
    ctx->error_message[0] = '\0';
    ctx->error_time = 0;

    // Check if heartbeat and reconnect are enabled
    const char *heartbeat_env = getenv(ZMQ_ENV_ENABLE_HEARTBEAT);
    ctx->heartbeat_enabled = (heartbeat_env && (strcmp(heartbeat_env, "1") == 0 ||
                                               strcasecmp(heartbeat_env, "true") == 0 ||
                                               strcasecmp(heartbeat_env, "yes") == 0));

    const char *reconnect_env = getenv(ZMQ_ENV_ENABLE_RECONNECT);
    ctx->reconnect_enabled = (reconnect_env && (strcmp(reconnect_env, "1") == 0 ||
                                               strcasecmp(reconnect_env, "true") == 0 ||
                                               strcasecmp(reconnect_env, "yes") == 0));

    LOG_DEBUG("ZMQ: Timeout configuration - receive: %dms, send: %dms, connect: %dms",
              ctx->receive_timeout, ctx->send_timeout, ctx->connect_timeout);
    LOG_DEBUG("ZMQ: Heartbeat: %s (interval: %dms), Reconnect: %s (max attempts: %d)",
              ctx->heartbeat_enabled ? "enabled" : "disabled", ctx->heartbeat_interval,
              ctx->reconnect_enabled ? "enabled" : "disabled", ctx->max_reconnect_attempts);

    // Create ZMQ context
    ctx->context = zmq_ctx_new();
    if (!ctx->context) {
        LOG_ERROR("ZMQ: Failed to create ZMQ context: %s", zmq_strerror(errno));
        free(ctx);
        return NULL;
    }
    LOG_DEBUG("ZMQ: Created ZMQ context");

    // Create socket
    ctx->socket = zmq_socket(ctx->context, socket_type);
    if (!ctx->socket) {
        LOG_ERROR("ZMQ: Failed to create ZMQ socket: %s", zmq_strerror(errno));
        zmq_ctx_term(ctx->context);
        free(ctx);
        return NULL;
    }
    LOG_DEBUG("ZMQ: Created ZMQ socket");

    // Set socket options for better performance and reliability
    int linger = 1000; // 1 second linger to allow pending messages to be sent
    zmq_setsockopt(ctx->socket, ZMQ_LINGER, &linger, sizeof(linger));
    LOG_DEBUG("ZMQ: Set ZMQ_LINGER option to %d", linger);

    // Set timeouts
    zmq_setsockopt(ctx->socket, ZMQ_RCVTIMEO, &ctx->receive_timeout, sizeof(ctx->receive_timeout));
    zmq_setsockopt(ctx->socket, ZMQ_SNDTIMEO, &ctx->send_timeout, sizeof(ctx->send_timeout));

    // Set high water marks to prevent memory exhaustion
    int hwm = 1000;
    zmq_setsockopt(ctx->socket, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(ctx->socket, ZMQ_RCVHWM, &hwm, sizeof(hwm));

    // Enable TCP keepalive for better connection monitoring
    int keepalive = 1;
    zmq_setsockopt(ctx->socket, ZMQ_TCP_KEEPALIVE, &keepalive, sizeof(keepalive));

    int keepalive_idle = 60; // Start keepalive after 60 seconds of idle
    zmq_setsockopt(ctx->socket, ZMQ_TCP_KEEPALIVE_IDLE, &keepalive_idle, sizeof(keepalive_idle));

    int keepalive_intvl = 5; // Send keepalive every 5 seconds
    zmq_setsockopt(ctx->socket, ZMQ_TCP_KEEPALIVE_INTVL, &keepalive_intvl, sizeof(keepalive_intvl));

    int keepalive_cnt = 3; // Consider dead after 3 failed keepalives
    zmq_setsockopt(ctx->socket, ZMQ_TCP_KEEPALIVE_CNT, &keepalive_cnt, sizeof(keepalive_cnt));

    LOG_DEBUG("ZMQ: Socket options configured - RCVTIMEO: %dms, SNDTIMEO: %dms, HWM: %d",
              ctx->receive_timeout, ctx->send_timeout, hwm);

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
    LOG_DEBUG("ZMQ: Duplicated endpoint string: %s", ctx->endpoint);

    ctx->socket_type = socket_type;
    ctx->enabled = true;
    ctx->daemon_mode = (socket_type == ZMQ_PAIR);

    // Create message queues if enabled
    if (ctx->message_queue_enabled) {
        ctx->send_queue = zmq_message_queue_create((size_t)ctx->send_queue_size);
        if (!ctx->send_queue) {
            LOG_WARN("ZMQ: Failed to create send message queue");
            // Continue without message queue
            ctx->message_queue_enabled = false;
        }
        
        ctx->receive_queue = zmq_message_queue_create((size_t)ctx->receive_queue_size);
        if (!ctx->receive_queue) {
            LOG_WARN("ZMQ: Failed to create receive message queue");
            // Clean up send queue if it was created
            if (ctx->send_queue) {
                zmq_message_queue_free(ctx->send_queue);
                ctx->send_queue = NULL;
            }
            ctx->message_queue_enabled = false;
        }
        
        if (ctx->message_queue_enabled) {
            LOG_DEBUG("ZMQ: Message queues created (send: %d, receive: %d)", 
                     ctx->send_queue_size, ctx->receive_queue_size);
        }
    }

    LOG_INFO("ZMQ: Socket initialization completed successfully");
    LOG_DEBUG("ZMQ: Context enabled: %s", ctx->enabled ? "true" : "false");
    LOG_DEBUG("ZMQ: Daemon mode: %s", ctx->daemon_mode ? "true" : "false");

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

    if (ctx->socket) {
        LOG_DEBUG("ZMQ: Closing socket");
        zmq_close(ctx->socket);
        ctx->socket = NULL;
    }

    if (ctx->context) {
        LOG_DEBUG("ZMQ: Terminating ZMQ context");
        zmq_ctx_term(ctx->context);
        ctx->context = NULL;
    }

    // Clean up message queues
    if (ctx->send_queue) {
        LOG_DEBUG("ZMQ: Freeing send message queue");
        zmq_message_queue_free(ctx->send_queue);
        ctx->send_queue = NULL;
    }
    
    if (ctx->receive_queue) {
        LOG_DEBUG("ZMQ: Freeing receive message queue");
        zmq_message_queue_free(ctx->receive_queue);
        ctx->receive_queue = NULL;
    }

    if (ctx->endpoint) {
        LOG_DEBUG("ZMQ: Freeing endpoint string: %s", ctx->endpoint);
        free(ctx->endpoint);
        ctx->endpoint = NULL;
    }

    LOG_DEBUG("ZMQ: Freeing ZMQ context structure");
    free(ctx);
    LOG_INFO("ZMQ: Cleanup completed");
#else
    (void)ctx; // Unused parameter
#endif
}

int zmq_socket_send(ZMQContext *ctx, const char *message, size_t message_len) {
#ifdef HAVE_ZMQ
    if (!ctx || !message) {
        zmq_set_error(ctx, ZMQ_ERROR_INVALID_PARAM, "Invalid parameters for send");
        return ZMQ_ERROR_INVALID_PARAM;
    }

    // Check if we should queue the message
    bool should_queue = ctx->message_queue_enabled && ctx->send_queue;
    
    // Check connection health
    if (ctx->socket) {
        int health = zmq_check_connection_health(ctx);
        if (health < 0) {
            LOG_WARN("ZMQ: Connection health check failed, attempting reconnect");
            if (zmq_attempt_reconnect(ctx) != 0) {
                // Reconnect failed, queue message if enabled
                if (should_queue) {
                    if (zmq_message_queue_push(ctx->send_queue, message, message_len) == 0) {
                        LOG_INFO("ZMQ: Message queued (queue size: %zu)", 
                                 zmq_message_queue_size(ctx->send_queue));
                        zmq_set_error(ctx, ZMQ_ERROR_QUEUE_FULL, 
                                     "Connection failed, message queued for later delivery");
                        return ZMQ_ERROR_QUEUE_FULL; // Special error code for queued messages
                    } else {
                        LOG_ERROR("ZMQ: Failed to queue message, queue may be full");
                        zmq_set_error(ctx, ZMQ_ERROR_QUEUE_FULL, 
                                     "Connection failed and queue is full");
                        return ZMQ_ERROR_QUEUE_FULL;
                    }
                } else {
                    zmq_set_error(ctx, ZMQ_ERROR_RECONNECT_FAILED, 
                                 "Reconnect failed, cannot send message");
                    return ZMQ_ERROR_RECONNECT_FAILED;
                }
            }
            // Reconnect succeeded, continue to send
        }
        // health == 0: connection exists but may be stale, try to send anyway
        // health > 0: connection healthy, continue to send
    } else if (ctx->reconnect_enabled) {
        // Socket doesn't exist, try to reconnect
        if (zmq_attempt_reconnect(ctx) != 0) {
            // Reconnect failed, queue message if enabled
            if (should_queue) {
                if (zmq_message_queue_push(ctx->send_queue, message, message_len) == 0) {
                    LOG_INFO("ZMQ: Message queued (queue size: %zu)", 
                             zmq_message_queue_size(ctx->send_queue));
                    zmq_set_error(ctx, ZMQ_ERROR_QUEUE_FULL, 
                                 "No socket, message queued for later delivery");
                    return ZMQ_ERROR_QUEUE_FULL;
                } else {
                    LOG_ERROR("ZMQ: Failed to queue message, queue may be full");
                    zmq_set_error(ctx, ZMQ_ERROR_QUEUE_FULL, 
                                 "No socket and queue is full");
                    return ZMQ_ERROR_QUEUE_FULL;
                }
            } else {
                zmq_set_error(ctx, ZMQ_ERROR_NO_SOCKET, 
                             "Cannot send message - no socket and reconnect failed");
                return ZMQ_ERROR_NO_SOCKET;
            }
        }
        // Reconnect succeeded, continue to send
    } else {
        // No reconnect enabled
        if (should_queue) {
            if (zmq_message_queue_push(ctx->send_queue, message, message_len) == 0) {
                LOG_INFO("ZMQ: Message queued (queue size: %zu)", 
                         zmq_message_queue_size(ctx->send_queue));
                zmq_set_error(ctx, ZMQ_ERROR_QUEUE_FULL, 
                             "No socket available, message queued");
                return ZMQ_ERROR_QUEUE_FULL;
            } else {
                LOG_ERROR("ZMQ: Failed to queue message, queue may be full");
                zmq_set_error(ctx, ZMQ_ERROR_QUEUE_FULL, 
                             "No socket available and queue is full");
                return ZMQ_ERROR_QUEUE_FULL;
            }
        } else {
            zmq_set_error(ctx, ZMQ_ERROR_NO_SOCKET, 
                         "No socket available and reconnect disabled");
            return ZMQ_ERROR_NO_SOCKET;
        }
    }

    // Try to send queued messages first if we have any
    if (should_queue && zmq_message_queue_size(ctx->send_queue) > 0) {
        LOG_DEBUG("ZMQ: Attempting to send %zu queued messages", 
                 zmq_message_queue_size(ctx->send_queue));
        
        while (zmq_message_queue_size(ctx->send_queue) > 0) {
            size_t queued_msg_len;
            char *queued_msg = zmq_message_queue_pop(ctx->send_queue, &queued_msg_len);
            if (!queued_msg) break;
            
            LOG_DEBUG("ZMQ: Sending queued message (%zu bytes)", queued_msg_len);
            int rc = zmq_send(ctx->socket, queued_msg, queued_msg_len, 0);
            free(queued_msg);
            
            if (rc < 0) {
                int err = errno;
                if (err == EAGAIN) {
                    LOG_WARN("ZMQ: Queued message send timeout, re-queueing");
                    // Re-queue the message at the front
                    // For simplicity, we'll just break and let the new message be queued
                    // In a production system, we'd want to re-queue at the front
                    break;
                } else {
                    LOG_ERROR("ZMQ: Failed to send queued message: %s", zmq_strerror(err));
                    // Connection may be broken, stop trying to send queued messages
                    zmq_close(ctx->socket);
                    ctx->socket = NULL;
                    // Queue the current message and exit
                    if (zmq_message_queue_push(ctx->send_queue, message, message_len) == 0) {
                        LOG_INFO("ZMQ: Current message queued after send failure");
                        zmq_set_error(ctx, ZMQ_ERROR_SEND_FAILED, 
                                     "Send failed, message queued");
                        return ZMQ_ERROR_SEND_FAILED;
                    }
                    break;
                }
            } else {
                LOG_DEBUG("ZMQ: Successfully sent queued message");
                ctx->last_activity = time(NULL);
            }
        }
    }

    LOG_DEBUG("ZMQ: Sending %zu bytes to endpoint: %s (timeout: %dms)",
              message_len, ctx->endpoint ? ctx->endpoint : "unknown", ctx->send_timeout);

    // Update last activity time
    ctx->last_activity = time(NULL);

    int rc = zmq_send(ctx->socket, message, message_len, 0);
    if (rc < 0) {
        int err = errno;

        // Check if it's a timeout error
        if (err == EAGAIN) {
            zmq_set_error(ctx, ZMQ_ERROR_TIMEOUT, "Send timeout after %dms: %s",
                         ctx->send_timeout, zmq_strerror(err));
            LOG_WARN("ZMQ: Send timeout after %dms", ctx->send_timeout);
            
            // Queue message if enabled
            if (should_queue) {
                if (zmq_message_queue_push(ctx->send_queue, message, message_len) == 0) {
                    LOG_INFO("ZMQ: Timed out message queued");
                    return ZMQ_ERROR_TIMEOUT; // Message was queued
                }
            }
        } else {
            zmq_set_error(ctx, ZMQ_ERROR_SEND_FAILED, "Failed to send message: %s (endpoint: %s)",
                         zmq_strerror(err), ctx->endpoint ? ctx->endpoint : "unknown");
            
            // Queue message if enabled
            if (should_queue) {
                if (zmq_message_queue_push(ctx->send_queue, message, message_len) == 0) {
                    LOG_INFO("ZMQ: Failed message queued for retry");
                    return ZMQ_ERROR_SEND_FAILED; // Message was queued
                }
            }
        }

        // If send failed and reconnect is enabled, mark for reconnection
        if (ctx->reconnect_enabled && err != EAGAIN) {
            LOG_INFO("ZMQ: Send failure, will attempt reconnect on next operation");
            zmq_close(ctx->socket);
            ctx->socket = NULL;
        }

        return (err == EAGAIN) ? ZMQ_ERROR_TIMEOUT : ZMQ_ERROR_SEND_FAILED;
    }

    LOG_DEBUG("ZMQ: Successfully sent %zu bytes (return code: %d)", message_len, rc);

    // Log first 200 characters of message for debugging (if it's not too large)
    if (message_len > 0 && message_len < 1024) {
        char preview[256];
        size_t preview_len = message_len < 200 ? message_len : 200;
        strncpy(preview, message, preview_len);
        preview[preview_len] = '\0';
        LOG_DEBUG("ZMQ: Message preview: %s", preview);
    }

    return ZMQ_ERROR_NONE;
#else
    (void)ctx;
    (void)message;
    (void)message_len;
    return ZMQ_ERROR_NOT_SUPPORTED;
#endif
}

const char* zmq_socket_last_error(ZMQContext *ctx) {
    if (!ctx || ctx->last_error == ZMQ_ERROR_NONE) {
        return zmq_error_to_string(ZMQ_ERROR_NONE);
    }

    return ctx->error_message;
}

void zmq_socket_clear_error(ZMQContext *ctx) {
    if (!ctx) return;

    ctx->last_error = ZMQ_ERROR_NONE;
    ctx->error_message[0] = '\0';
    ctx->error_time = 0;
}

int zmq_socket_receive(ZMQContext *ctx, char *buffer, size_t buffer_size, int timeout_ms) {
#ifdef HAVE_ZMQ
    LOG_DEBUG("ZMQ: zmq_socket_receive called (trace)");
    if (!ctx || !buffer || buffer_size == 0) {
        LOG_ERROR("ZMQ: Invalid parameters for receive: ctx=%p, buffer=%p, buffer_size=%zu",
                  (void*)ctx, (void*)buffer, buffer_size);
        zmq_set_error(ctx, ZMQ_ERROR_INVALID_PARAM, "Invalid parameters for receive");
        return ZMQ_ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("ZMQ: Receive parameters - buffer: %p, buffer_size: %zu, timeout_ms: %d, endpoint: %s",
              (void*)buffer, buffer_size, timeout_ms, ctx->endpoint ? ctx->endpoint : "unknown");

    // Check connection health
    if (ctx->socket) {
        LOG_DEBUG("ZMQ: Socket exists (%p), checking connection health", ctx->socket);
        int health = zmq_check_connection_health(ctx);
        if (health < 0) {
            LOG_WARN("ZMQ: Connection health check failed, attempting reconnect");
            if (zmq_attempt_reconnect(ctx) != 0) {
                LOG_ERROR("ZMQ: Reconnect failed, cannot receive message");
                zmq_set_error(ctx, ZMQ_ERROR_RECONNECT_FAILED, "Reconnect failed, cannot receive message");
                return ZMQ_ERROR_RECONNECT_FAILED;
            }
            LOG_DEBUG("ZMQ: Reconnect successful, proceeding with receive");
        } else {
            LOG_DEBUG("ZMQ: Connection health check passed (health=%d)", health);
        }
    } else if (ctx->reconnect_enabled) {
        // Socket doesn't exist, try to reconnect
        LOG_WARN("ZMQ: Socket doesn't exist but reconnect is enabled, attempting reconnect");
        if (zmq_attempt_reconnect(ctx) != 0) {
            LOG_ERROR("ZMQ: Cannot receive message - no socket and reconnect failed");
            zmq_set_error(ctx, ZMQ_ERROR_NO_SOCKET, "Cannot receive message - no socket and reconnect failed");
            return ZMQ_ERROR_NO_SOCKET;
        }
        LOG_DEBUG("ZMQ: Reconnect successful, socket created");
    } else {
        LOG_ERROR("ZMQ: No socket available and reconnect disabled");
        zmq_set_error(ctx, ZMQ_ERROR_NO_SOCKET, "No socket available and reconnect disabled");
        return ZMQ_ERROR_NO_SOCKET;
    }

    // Use provided timeout or context default
    int actual_timeout = (timeout_ms >= 0) ? timeout_ms : ctx->receive_timeout;
    LOG_DEBUG("ZMQ: Using timeout: %d ms (provided: %d, default: %d)",
              actual_timeout, timeout_ms, ctx->receive_timeout);

    LOG_INFO("ZMQ: Waiting for message on endpoint: %s (timeout: %d ms, buffer size: %zu)",
              ctx->endpoint ? ctx->endpoint : "unknown", actual_timeout, buffer_size);

    // Set timeout
    LOG_DEBUG("ZMQ: Setting socket receive timeout to %d ms", actual_timeout);
    zmq_setsockopt(ctx->socket, ZMQ_RCVTIMEO, &actual_timeout, sizeof(actual_timeout));

    LOG_DEBUG("ZMQ: Calling zmq_recv (buffer size: %zu)", buffer_size - 1);
    int rc = zmq_recv(ctx->socket, buffer, buffer_size - 1, 0);
    if (rc < 0) {
        int err = errno;
        if (err == EAGAIN) {
            LOG_WARN("ZMQ: Receive timeout after %d ms (EAGAIN)", actual_timeout);
            zmq_set_error(ctx, ZMQ_ERROR_TIMEOUT, "Receive timeout after %d ms", actual_timeout);
            LOG_DEBUG("ZMQ: Receive timeout after %d ms", actual_timeout);
        } else {
            LOG_ERROR("ZMQ: Failed to receive message: %s (errno=%d, endpoint: %s)",
                     zmq_strerror(err), err, ctx->endpoint ? ctx->endpoint : "unknown");
            zmq_set_error(ctx, ZMQ_ERROR_RECEIVE_FAILED, "Failed to receive message: %s (endpoint: %s)",
                         zmq_strerror(err), ctx->endpoint ? ctx->endpoint : "unknown");

            // If receive failed and reconnect is enabled, mark for reconnection
            if (ctx->reconnect_enabled && err != EAGAIN) {
                LOG_INFO("ZMQ: Receive failure, closing socket and marking for reconnection");
                zmq_close(ctx->socket);
                ctx->socket = NULL;
                LOG_DEBUG("ZMQ: Socket closed, will attempt reconnect on next operation");
            }
        }
        return (err == EAGAIN) ? ZMQ_ERROR_TIMEOUT : ZMQ_ERROR_RECEIVE_FAILED;
    }

    LOG_INFO("ZMQ: Received %d bytes from endpoint: %s", rc, ctx->endpoint ? ctx->endpoint : "unknown");
    buffer[rc] = '\0'; // Null-terminate the received data

    // Update last activity time
    time_t now = time(NULL);
    ctx->last_activity = now;
    LOG_DEBUG("ZMQ: Updated last activity time to %ld", now);

    // Log first 200 characters of received data for debugging
    if (rc > 0 && rc < 1024) {
        char preview[256];
        size_t preview_len = (size_t)(rc < 200 ? rc : 200);
        strncpy(preview, buffer, preview_len);
        preview[preview_len] = '\0';
        LOG_DEBUG("ZMQ: Received data preview (first %zu chars): %s", preview_len, preview);
        
        // Also log message type if it's JSON
        if (rc > 10 && buffer[0] == '{') {
            // Try to parse just enough to get messageType
            char *message_type_start = strstr(buffer, "\"messageType\"");
            if (message_type_start) {
                char *colon = strchr(message_type_start, ':');
                if (colon) {
                    char *quote1 = strchr(colon, '\"');
                    if (quote1) {
                        char *quote2 = strchr(quote1 + 1, '\"');
                        if (quote2 && (quote2 - quote1 - 1) < 50) {
                            char msg_type[64];
                            size_t len = (size_t)(quote2 - quote1 - 1);
                            strncpy(msg_type, quote1 + 1, len);
                            msg_type[len] = '\0';
                            LOG_DEBUG("ZMQ: Detected message type: %s", msg_type);
                        }
                    }
                }
            }
        }
    } else if (rc >= 1024) {
        LOG_DEBUG("ZMQ: Received large message (%d bytes), first 200 chars: %.*s",
                 rc, 200, buffer);
    }

    LOG_DEBUG("ZMQ: Receive completed successfully, returning %d bytes", rc);
    return rc;
#else
    (void)ctx;
    (void)buffer;
    (void)buffer_size;
    (void)timeout_ms;
    return ZMQ_ERROR_NOT_SUPPORTED;
#endif
}

int zmq_socket_process_message(ZMQContext *ctx, struct ConversationState *state, struct TUIState *tui) {
#ifdef HAVE_ZMQ
    if (!ctx || !state) {
        LOG_ERROR("ZMQ: Invalid parameters for process_message");
        return -1;
    }
    (void)tui; // Unused parameter for now

    LOG_DEBUG("ZMQ: Waiting for incoming message on endpoint: %s", ctx->endpoint ? ctx->endpoint : "unknown");

    char buffer[ZMQ_BUFFER_SIZE];
    int received = zmq_socket_receive(ctx, buffer, sizeof(buffer), -1); // Blocking receive
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
    LOG_DEBUG("ZMQ: Parsing JSON message");
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        LOG_ERROR("ZMQ: Failed to parse JSON message");
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            LOG_ERROR("ZMQ: JSON error near: %s", error_ptr);
        }

        // Send error response
        LOG_WARN("ZMQ: Sending JSON parse error response");
        char error_response[256];
        snprintf(error_response, sizeof(error_response),
                "{\"messageType\": \"ERROR\", \"content\": \"Invalid JSON\"}");
        zmq_socket_send(ctx, error_response, strlen(error_response));
        return -1;
    }

    LOG_DEBUG("ZMQ: JSON parsed successfully");

    // Extract message type and content
    LOG_DEBUG("ZMQ: Extracting message fields from JSON");
    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
    cJSON *content = cJSON_GetObjectItem(json, "content");

    char response[ZMQ_BUFFER_SIZE];
    response[0] = '\0';

    if (message_type && cJSON_IsString(message_type)) {
        if (strcmp(message_type->valuestring, "TEXT") == 0 &&
            content && cJSON_IsString(content)) {

            // Process text message with interactive tool call support
            LOG_INFO("ZMQ: Processing TEXT message with interactive mode (length: %zu)", strlen(content->valuestring));
            LOG_DEBUG("ZMQ: Message content: %.*s",
                    (int)(strlen(content->valuestring) > 200 ? 200 : strlen(content->valuestring)),
                    content->valuestring);

            // Print to console
            printf("ZMQ: Processing TEXT message (length: %zu)\n", strlen(content->valuestring));
            // Print first 100 chars of the message
            int preview_len = (int)(strlen(content->valuestring) > 100 ? 100 : strlen(content->valuestring));
            printf("Message preview: %.*s%s\n", preview_len, content->valuestring,
                   strlen(content->valuestring) > 100 ? "..." : "");
            fflush(stdout);

            // Don't clear conversation - maintain context across messages in daemon mode
            // This allows multi-turn conversations like interactive mode

            // Process interactively (handles tool calls recursively)
            int interactive_result = zmq_process_interactive(ctx, state, content->valuestring);

            if (interactive_result != 0) {
                LOG_ERROR("ZMQ: Interactive processing failed");
                snprintf(response, sizeof(response),
                        "{\"messageType\": \"ERROR\", \"content\": \"Interactive processing failed\"}");
            } else {
                // Success - responses are sent during interactive processing
                // No completion message - client detects completion when no pending TOOL messages
                // without corresponding TOOL_RESULT messages
                response[0] = '\0';  // Empty response
            }

        } else if (strcmp(message_type->valuestring, ZMQ_HEARTBEAT_PING) == 0) {
            // Handle heartbeat ping
            LOG_INFO("ZMQ: Processing HEARTBEAT_PING message");
            
            // Get timestamp from the ping
            cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");
            time_t ping_time = time(NULL);
            if (timestamp && cJSON_IsNumber(timestamp)) {
                ping_time = (time_t)timestamp->valuedouble;
            }
            
            // Send heartbeat pong response
            snprintf(response, sizeof(response),
                    "{\"messageType\": \"%s\", \"pingTimestamp\": %ld}",
                    ZMQ_HEARTBEAT_PONG, (long)ping_time);
            
            LOG_DEBUG("ZMQ: Sending HEARTBEAT_PONG response with timestamp %ld", (long)ping_time);
            
        } else {
            LOG_WARN("ZMQ: Unsupported message type received: %s", message_type->valuestring);
            LOG_DEBUG("ZMQ: Available fields - messageType: %s, content: %s",
                     message_type ? "present" : "missing",
                     content ? "present" : "missing");
            snprintf(response, sizeof(response),
                    "{\"messageType\": \"ERROR\", \"content\": \"Unsupported message type\"}");
        }
    } else {
        LOG_WARN("ZMQ: Invalid message format received - missing messageType");
        snprintf(response, sizeof(response),
                "{\"messageType\": \"ERROR\", \"content\": \"Invalid message format - missing messageType\"}");
    }

    cJSON_Delete(json);
    LOG_DEBUG("ZMQ: JSON object cleaned up");

    // Send response
    if (response[0] != '\0') {
        LOG_DEBUG("ZMQ: Preparing to send response (length: %zu)", strlen(response));
        LOG_DEBUG("ZMQ: Response content: %.*s",
                 (int)(strlen(response) > 200 ? 200 : strlen(response)), response);

        // Print to console
        printf("ZMQ: Sending response (length: %zu)\n", strlen(response));
        fflush(stdout);

        int send_result = zmq_socket_send(ctx, response, strlen(response));
        if (send_result == 0) {
            LOG_INFO("ZMQ: Response sent successfully");
            printf("ZMQ: Response sent successfully\n");
            fflush(stdout);
        } else {
            LOG_ERROR("ZMQ: Failed to send response");
            printf("ZMQ: Failed to send response\n");
            fflush(stdout);
        }
    } else {
        LOG_ERROR("ZMQ: Empty response generated, not sending");
        printf("ZMQ: Empty response generated, not sending\n");
        fflush(stdout);
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
static int zmq_send_tool_result(ZMQContext *ctx, const char *tool_name, const char *tool_id, cJSON *tool_output, int is_error) {
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

// Helper function to check connection health
static int zmq_check_connection_health(ZMQContext *ctx) {
#ifdef HAVE_ZMQ
    LOG_DEBUG("ZMQ: Checking connection health");
    if (!ctx) {
        LOG_ERROR("ZMQ: Connection health check failed - ctx is NULL");
        return -1;
    }

    if (!ctx->socket) {
        LOG_WARN("ZMQ: Connection health check failed - socket is NULL (ctx=%p)",
                 (void*)ctx);
        return -1;
    }

    time_t now = time(NULL);
    LOG_DEBUG("ZMQ: Current time: %ld, last_heartbeat: %ld, last_activity: %ld",
              now, ctx->last_heartbeat, ctx->last_activity);

    // Check if heartbeat is needed
    time_t time_since_last_heartbeat = now - ctx->last_heartbeat;
    int heartbeat_interval_seconds = ctx->heartbeat_interval / 1000;
    
    LOG_DEBUG("ZMQ: Heartbeat check - time since last: %lds, interval: %ds, enabled: %d",
              time_since_last_heartbeat, heartbeat_interval_seconds, ctx->heartbeat_enabled);
    
    if (ctx->heartbeat_enabled &&
        time_since_last_heartbeat * 1000 >= ctx->heartbeat_interval) {

        LOG_INFO("ZMQ: Sending heartbeat ping (last heartbeat %ld seconds ago)",
                 time_since_last_heartbeat);

        // Try to send a small ping message
        const char *ping_msg = "PING";
        LOG_DEBUG("ZMQ: Sending heartbeat ping message: %s", ping_msg);
        int rc = zmq_send(ctx->socket, ping_msg, strlen(ping_msg), ZMQ_DONTWAIT);

        ctx->last_heartbeat = now;
        LOG_DEBUG("ZMQ: Updated last_heartbeat to %ld", now);

        if (rc < 0) {
            int err = errno;
            if (err == EAGAIN) {
                LOG_DEBUG("ZMQ: Heartbeat send would block (EAGAIN), normal for non-blocking send");
            } else {
                LOG_WARN("ZMQ: Heartbeat failed: %s (errno=%d)", zmq_strerror(err), err);
                return -1;
            }
        } else {
            LOG_DEBUG("ZMQ: Heartbeat ping sent successfully (%d bytes)", rc);
        }
    } else if (ctx->heartbeat_enabled) {
        LOG_DEBUG("ZMQ: Heartbeat not needed yet (%lds < %ds)",
                 time_since_last_heartbeat, heartbeat_interval_seconds);
    }

    // Check if connection is stale (no activity for 2x heartbeat interval)
    time_t time_since_last_activity = now - ctx->last_activity;
    int stale_threshold_seconds = (ctx->heartbeat_interval * 2) / 1000;
    
    LOG_DEBUG("ZMQ: Stale check - time since last activity: %lds, threshold: %ds",
              time_since_last_activity, stale_threshold_seconds);
    
    if (ctx->heartbeat_enabled &&
        time_since_last_activity * 1000 > ctx->heartbeat_interval * 2) {
        LOG_WARN("ZMQ: Connection appears stale (no activity for %ld seconds, threshold: %d seconds)",
                 time_since_last_activity, stale_threshold_seconds);
        return 0; // Connection exists but is stale
    }

    LOG_DEBUG("ZMQ: Connection health check passed - connection is healthy");
    return 1; // Connection healthy
#else
    (void)ctx;
    return -1;
#endif
}

// Helper function to attempt reconnection
static int zmq_attempt_reconnect(ZMQContext *ctx) {
#ifdef HAVE_ZMQ
    LOG_DEBUG("ZMQ: Attempting reconnection");
    if (!ctx || !ctx->reconnect_enabled) {
        LOG_WARN("ZMQ: Reconnect not attempted - ctx=%p, reconnect_enabled=%d",
                 (void*)ctx, ctx ? ctx->reconnect_enabled : 0);
        return -1;
    }

    LOG_DEBUG("ZMQ: Reconnect state - attempts: %d/%d, endpoint: %s, socket_type: %d",
              ctx->reconnect_attempts, ctx->max_reconnect_attempts,
              ctx->endpoint ? ctx->endpoint : "NULL", ctx->socket_type);

    if (ctx->reconnect_attempts >= ctx->max_reconnect_attempts) {
        LOG_ERROR("ZMQ: Maximum reconnect attempts (%d) reached, giving up",
                  ctx->max_reconnect_attempts);
        return -1;
    }

    LOG_INFO("ZMQ: Attempting reconnect (%d/%d) to %s (socket type: %d)",
             ctx->reconnect_attempts + 1, ctx->max_reconnect_attempts,
             ctx->endpoint, ctx->socket_type);

    // Close existing socket
    if (ctx->socket) {
        LOG_DEBUG("ZMQ: Closing existing socket %p before reconnect", (void*)ctx->socket);
        zmq_close(ctx->socket);
        ctx->socket = NULL;
        LOG_DEBUG("ZMQ: Socket closed");
    } else {
        LOG_DEBUG("ZMQ: No existing socket to close");
    }

    // Wait before retry
    if (ctx->reconnect_interval > 0) {
        LOG_DEBUG("ZMQ: Waiting %d ms before reconnect attempt", ctx->reconnect_interval);
        struct timespec sleep_time = {
            0,
            ctx->reconnect_interval * 1000000L // Convert ms to ns
        };
        nanosleep(&sleep_time, NULL);
        LOG_DEBUG("ZMQ: Wait completed");
    } else {
        LOG_DEBUG("ZMQ: No wait interval configured, proceeding immediately");
    }

    // Create new socket
    LOG_DEBUG("ZMQ: Creating new socket (type: %d)", ctx->socket_type);
    ctx->socket = zmq_socket(ctx->context, ctx->socket_type);
    if (!ctx->socket) {
        int err = errno;
        LOG_ERROR("ZMQ: Failed to create socket during reconnect: %s (errno=%d)",
                 zmq_strerror(err), err);
        ctx->reconnect_attempts++;
        LOG_DEBUG("ZMQ: Reconnect attempts incremented to %d", ctx->reconnect_attempts);
        return -1;
    }
    LOG_DEBUG("ZMQ: New socket created successfully: %p", (void*)ctx->socket);

    // Reapply socket options
    LOG_DEBUG("ZMQ: Reapplying socket options");
    int linger = 1000;
    zmq_setsockopt(ctx->socket, ZMQ_LINGER, &linger, sizeof(linger));
    LOG_DEBUG("ZMQ: Set ZMQ_LINGER to %d ms", linger);
    
    zmq_setsockopt(ctx->socket, ZMQ_RCVTIMEO, &ctx->receive_timeout, sizeof(ctx->receive_timeout));
    LOG_DEBUG("ZMQ: Set ZMQ_RCVTIMEO to %d ms", ctx->receive_timeout);
    
    zmq_setsockopt(ctx->socket, ZMQ_SNDTIMEO, &ctx->send_timeout, sizeof(ctx->send_timeout));
    LOG_DEBUG("ZMQ: Set ZMQ_SNDTIMEO to %d ms", ctx->send_timeout);

    int hwm = 1000;
    zmq_setsockopt(ctx->socket, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(ctx->socket, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    LOG_DEBUG("ZMQ: Set high water mark to %d", hwm);

    // Reconnect or rebind
    int rc;
    if (ctx->socket_type == ZMQ_PAIR) {
        LOG_DEBUG("ZMQ: Binding socket to endpoint: %s", ctx->endpoint);
        rc = zmq_bind(ctx->socket, ctx->endpoint);
    } else {
        LOG_DEBUG("ZMQ: Connecting socket to endpoint: %s", ctx->endpoint);
        rc = zmq_connect(ctx->socket, ctx->endpoint);
    }

    if (rc != 0) {
        int err = errno;
        LOG_ERROR("ZMQ: Reconnect failed: %s (errno=%d)", zmq_strerror(err), err);
        LOG_DEBUG("ZMQ: Closing failed socket %p", (void*)ctx->socket);
        zmq_close(ctx->socket);
        ctx->socket = NULL;
        ctx->reconnect_attempts++;
        LOG_DEBUG("ZMQ: Reconnect attempts incremented to %d", ctx->reconnect_attempts);
        return -1;
    }

    LOG_INFO("ZMQ: Reconnect successful to %s", ctx->endpoint);
    ctx->reconnect_attempts = 0;
    
    time_t now = time(NULL);
    ctx->last_activity = now;
    ctx->last_heartbeat = now;
    LOG_DEBUG("ZMQ: Reset timestamps - last_activity: %ld, last_heartbeat: %ld",
              now, now);
    LOG_DEBUG("ZMQ: Reconnect attempts reset to 0");

    return 0;
#else
    (void)ctx;
    return -1;
#endif
}

// Helper function to set error state
__attribute__((format(printf, 3, 4)))
static void zmq_set_error(ZMQContext *ctx, ZMQErrorCode error_code, const char *format, ...) {
    if (!ctx) return;

    ctx->last_error = error_code;

    va_list args;
    va_start(args, format);
    vsnprintf(ctx->error_message, sizeof(ctx->error_message), format, args);
    va_end(args);

    ctx->error_time = time(NULL);

    LOG_ERROR("ZMQ Error [%d]: %s", error_code, ctx->error_message);
}

// Helper function to get error string from error code
static const char* zmq_error_to_string(ZMQErrorCode error_code) {
    switch (error_code) {
        case ZMQ_ERROR_NONE: return "No error";
        case ZMQ_ERROR_INVALID_PARAM: return "Invalid parameter";
        case ZMQ_ERROR_NO_SOCKET: return "No socket available";
        case ZMQ_ERROR_CONNECTION_FAILED: return "Connection failed";
        case ZMQ_ERROR_SEND_FAILED: return "Send failed";
        case ZMQ_ERROR_RECEIVE_FAILED: return "Receive failed";
        case ZMQ_ERROR_TIMEOUT: return "Timeout";
        case ZMQ_ERROR_QUEUE_FULL: return "Queue full";
        case ZMQ_ERROR_NOT_CONNECTED: return "Not connected";
        case ZMQ_ERROR_RECONNECT_FAILED: return "Reconnect failed";
        case ZMQ_ERROR_NOT_SUPPORTED: return "Not supported";
        default: return "Unknown error";
    }
}

// Helper function to send a user prompt request
__attribute__((unused)) static int zmq_send_user_prompt(ZMQContext *ctx, const char *prompt) {
#ifdef HAVE_ZMQ
    if (!ctx) {
        LOG_ERROR("ZMQ: Invalid parameters for send_user_prompt");
        return -1;
    }

    cJSON *response_json = cJSON_CreateObject();
    if (!response_json) {
        LOG_ERROR("ZMQ: Failed to create user prompt JSON object");
        return -1;
    }

    cJSON_AddStringToObject(response_json, "messageType", "USER_PROMPT");
    if (prompt) {
        cJSON_AddStringToObject(response_json, "content", prompt);
    } else {
        cJSON_AddStringToObject(response_json, "content", "Please provide additional information or confirm the action:");
    }

    char *response_str = cJSON_PrintUnformatted(response_json);
    if (!response_str) {
        LOG_ERROR("ZMQ: Failed to serialize user prompt JSON");
        cJSON_Delete(response_json);
        return -1;
    }

    int result = zmq_socket_send(ctx, response_str, strlen(response_str));
    free(response_str);
    cJSON_Delete(response_json);

    return result;
#else
    (void)ctx;
    (void)prompt;
    return -1;
#endif
}

// Process ZMQ message with interactive tool call support
static int zmq_process_interactive(ZMQContext *ctx, struct ConversationState *state, const char *user_input) {
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

                // Print to console
                printf("\n--- AI Response ---\n");
                // Print first 200 chars of the response
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
                    cJSON_AddStringToObject(results[i].tool_output, "error", "Tool call missing name or id");
                    results[i].is_error = 1;
                    
                    // Send TOOL request message (even though it will fail)
                    zmq_send_tool_request(ctx, tool->name ? tool->name : "unknown", 
                                         tool->id ? tool->id : "unknown", NULL);
                    
                    // Send error response
                    zmq_send_tool_result(ctx, tool->name ? tool->name : "unknown",
                                        tool->id ? tool->id : "unknown", results[i].tool_output, 1);
                    continue;
                }

                LOG_INFO("ZMQ: Executing tool: %s (id: %s)", tool->name, tool->id);

                // Print to console
                printf("ZMQ: Executing tool: %s\n", tool->name);
                fflush(stdout);

                // Validate that the tool is in the allowed tools list (prevent hallucination)
                if (!is_tool_allowed(tool->name, state)) {
                    LOG_ERROR("ZMQ: Tool validation failed: '%s' was not provided in tools list", tool->name);
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

                    // Send TOOL request message (even though it will fail)
                    zmq_send_tool_request(ctx, tool->name, tool->id, NULL);

                    // Send error response
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
                // Results were already freed by add_tool_results
                results = NULL;
                zmq_send_json_response(ctx, "ERROR", "Failed to add tool results to conversation");
                api_response_free(api_response);
                return -1;
            }

            // Continue loop to process next AI response with tool results
            api_response_free(api_response);
            continue;
        }

        // Check if we need user input (e.g., assistant is asking a question)
        // For now, we'll just finish after processing all tool calls
        // In the future, we could analyze the response to detect questions

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
    LOG_INFO("ZMQ: Buffer size: %d bytes", ZMQ_BUFFER_SIZE);
    LOG_INFO("ZMQ: =========================================");

    // Print to console as well
    printf("ZMQ daemon started on %s\n", ctx->endpoint);
    printf("Waiting for connections...\n");
    fflush(stdout);

    int message_count = 0;
    int error_count = 0;

    while (ctx->enabled) {
        LOG_DEBUG("ZMQ: Waiting for next message (message #%d)", message_count + 1);

        int result = zmq_socket_process_message(ctx, state, NULL);
        if (result != 0) {
            error_count++;
            LOG_WARN("ZMQ: Message processing failed (error #%d)", error_count);

            // Check if we should continue or exit
            if (error_count > 10) {
                LOG_ERROR("ZMQ: Too many consecutive errors (%d), stopping daemon", error_count);
                printf("ZMQ: Too many consecutive errors (%d), stopping daemon\n", error_count);
                break;
            }

            // Small delay before retrying to avoid tight loop on errors
            struct timespec sleep_time = {0, 100000000}; // 100ms
            nanosleep(&sleep_time, NULL);
            continue;
        }

        message_count++;
        error_count = 0; // Reset error count on successful processing
        LOG_DEBUG("ZMQ: Successfully processed message #%d", message_count);
        printf("ZMQ: Successfully processed message #%d\n", message_count);
        fflush(stdout);
    }

    LOG_INFO("ZMQ: =========================================");
    LOG_INFO("ZMQ: ZMQ daemon mode stopping");
    LOG_INFO("ZMQ: Total messages processed: %d", message_count);
    LOG_INFO("ZMQ: Total errors: %d", error_count);
    LOG_INFO("ZMQ: =========================================");

    // Print to console as well
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

bool zmq_socket_available(void) {
#ifdef HAVE_ZMQ
    return true;
#else
    return false;
#endif
}

int zmq_socket_get_status(ZMQContext *ctx, char *buffer, size_t buffer_size) {
#ifdef HAVE_ZMQ
    if (!ctx || !buffer || buffer_size == 0) {
        return -1;
    }

    time_t now = time(NULL);
    const char *socket_type_str = "UNKNOWN";

    switch (ctx->socket_type) {
        case ZMQ_PAIR: socket_type_str = "PAIR"; break;
        default: socket_type_str = "UNKNOWN"; break;
    }

    const char *connection_state = "DISCONNECTED";
    if (ctx->socket) {
        // Simple check - try to get socket option
        int fd;
        size_t fd_size = sizeof(fd);
        if (zmq_getsockopt(ctx->socket, ZMQ_FD, &fd, &fd_size) == 0) {
            connection_state = "CONNECTED";
        } else {
            connection_state = "ERROR";
        }
    }

    // Format status string
    snprintf(buffer, buffer_size,
             "ZMQ Status:\n"
             "  Endpoint: %s\n"
             "  Socket Type: %s\n"
             "  Connection: %s\n"
             "  Last Activity: %ld seconds ago\n"
             "  Reconnect Attempts: %d/%d\n"
             "  Heartbeat: %s\n"
             "  Auto-reconnect: %s",
             ctx->endpoint ? ctx->endpoint : "unknown",
             socket_type_str,
             connection_state,
             now - ctx->last_activity,
             ctx->reconnect_attempts, ctx->max_reconnect_attempts,
             ctx->heartbeat_enabled ? "enabled" : "disabled",
             ctx->reconnect_enabled ? "enabled" : "disabled");

    return 0;
#else
    (void)ctx;
    (void)buffer;
    (void)buffer_size;
    return -1;
#endif
}

int zmq_socket_get_queue_stats(ZMQContext *ctx,
                               size_t *send_queue_count, size_t *send_queue_bytes,
                               size_t *recv_queue_count, size_t *recv_queue_bytes) {
#ifdef HAVE_ZMQ
    if (!ctx) {
        return -1;
    }

    if (send_queue_count) {
        *send_queue_count = ctx->send_queue ? zmq_message_queue_size(ctx->send_queue) : 0;
    }

    if (send_queue_bytes) {
        *send_queue_bytes = ctx->send_queue ? zmq_message_queue_bytes(ctx->send_queue) : 0;
    }

    if (recv_queue_count) {
        *recv_queue_count = ctx->receive_queue ? zmq_message_queue_size(ctx->receive_queue) : 0;
    }

    if (recv_queue_bytes) {
        *recv_queue_bytes = ctx->receive_queue ? zmq_message_queue_bytes(ctx->receive_queue) : 0;
    }

    return 0;
#else
    (void)ctx;
    (void)send_queue_count;
    (void)send_queue_bytes;
    (void)recv_queue_count;
    (void)recv_queue_bytes;
    return -1;
#endif
}

// Message queue implementation
static ZMQMessageQueue* zmq_message_queue_create(size_t capacity) {
    LOG_DEBUG("ZMQ: Creating message queue with capacity %zu", capacity);
    ZMQMessageQueue *queue = calloc(1, sizeof(ZMQMessageQueue));
    if (!queue) {
        LOG_ERROR("ZMQ: Failed to allocate message queue structure");
        return NULL;
    }
    LOG_DEBUG("ZMQ: Allocated queue structure at %p", (void*)queue);

    queue->messages = calloc(capacity, sizeof(char*));
    queue->message_sizes = calloc(capacity, sizeof(size_t));
    if (!queue->messages || !queue->message_sizes) {
        LOG_ERROR("ZMQ: Failed to allocate message queue arrays");
        free(queue->messages);
        free(queue->message_sizes);
        free(queue);
        return NULL;
    }
    LOG_DEBUG("ZMQ: Allocated message arrays (messages: %p, sizes: %p)",
              (void*)queue->messages, (void*)queue->message_sizes);

    queue->capacity = capacity;
    queue->size = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->total_bytes = 0;

    LOG_INFO("ZMQ: Created message queue with capacity %zu at %p", capacity, (void*)queue);
    return queue;
}

static void zmq_message_queue_free(ZMQMessageQueue *queue) {
    LOG_DEBUG("ZMQ: Freeing message queue (queue=%p)", (void*)queue);
    if (!queue) {
        LOG_DEBUG("ZMQ: Queue is NULL, nothing to free");
        return;
    }

    LOG_INFO("ZMQ: Freeing message queue (size: %zu/%zu, bytes: %zu, head: %zu, tail: %zu)", 
              queue->size, queue->capacity, queue->total_bytes, queue->head, queue->tail);

    if (queue->size > 0) {
        LOG_DEBUG("ZMQ: Freeing %zu queued messages", queue->size);
        for (size_t i = 0; i < queue->size; i++) {
            size_t idx = (queue->head + i) % queue->capacity;
            LOG_DEBUG("ZMQ: Freeing message at index %zu (size: %zu bytes)",
                      idx, queue->message_sizes[idx]);
            free(queue->messages[idx]);
        }
    } else {
        LOG_DEBUG("ZMQ: Queue is empty, no messages to free");
    }

    LOG_DEBUG("ZMQ: Freeing message arrays (messages: %p, sizes: %p)",
              (void*)queue->messages, (void*)queue->message_sizes);
    free(queue->messages);
    free(queue->message_sizes);
    
    LOG_DEBUG("ZMQ: Freeing queue structure at %p", (void*)queue);
    free(queue);
    LOG_DEBUG("ZMQ: Message queue freed successfully");
}

static int zmq_message_queue_push(ZMQMessageQueue *queue, const char *message, size_t message_len) {
    LOG_DEBUG("ZMQ: Attempting to push message to queue (queue=%p, message_len=%zu)",
              (void*)queue, message_len);
    
    if (!queue || !message) {
        LOG_ERROR("ZMQ: Invalid parameters for queue push (queue=%p, message=%p)",
                  (void*)queue, (const void*)message);
        return -1;
    }

    LOG_DEBUG("ZMQ: Queue state before push - size: %zu/%zu, head: %zu, tail: %zu, total_bytes: %zu",
              queue->size, queue->capacity, queue->head, queue->tail, queue->total_bytes);

    if (queue->size >= queue->capacity) {
        LOG_WARN("ZMQ: Message queue full (size: %zu, capacity: %zu)", 
                 queue->size, queue->capacity);
        return -1;
    }

    // Allocate and copy message
    LOG_DEBUG("ZMQ: Allocating %zu bytes for message copy", message_len + 1);
    char *message_copy = malloc(message_len + 1);
    if (!message_copy) {
        LOG_ERROR("ZMQ: Failed to allocate message copy of size %zu", message_len + 1);
        return -1;
    }
    LOG_DEBUG("ZMQ: Message copy allocated at %p", (void*)message_copy);

    memcpy(message_copy, message, message_len);
    message_copy[message_len] = '\0';

    // Store in queue
    LOG_DEBUG("ZMQ: Storing message at tail index %zu", queue->tail);
    queue->messages[queue->tail] = message_copy;
    queue->message_sizes[queue->tail] = message_len;
    
    size_t old_tail = queue->tail;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->size++;
    queue->total_bytes += message_len;

    LOG_INFO("ZMQ: Pushed message to queue (size: %zu bytes, queue size: %zu/%zu, total bytes: %zu)",
              message_len, queue->size, queue->capacity, queue->total_bytes);
    LOG_DEBUG("ZMQ: Queue state after push - tail: %zu->%zu, size: %zu, total_bytes: %zu",
              old_tail, queue->tail, queue->size, queue->total_bytes);
    
    // Log message preview if it's small
    if (message_len > 0 && message_len < 200) {
        char preview[256];
        size_t preview_len = message_len < 100 ? message_len : 100;
        strncpy(preview, message_copy, preview_len);
        preview[preview_len] = '\0';
        LOG_DEBUG("ZMQ: Message preview: %s", preview);
    }
    
    return 0;
}

static char* zmq_message_queue_pop(ZMQMessageQueue *queue, size_t *message_len) {
    LOG_DEBUG("ZMQ: Attempting to pop message from queue (queue=%p)", (void*)queue);
    
    if (!queue || queue->size == 0) {
        LOG_DEBUG("ZMQ: Queue empty or invalid (queue=%p, size=%zu)", 
                  (void*)queue, queue ? queue->size : 0);
        if (message_len) *message_len = 0;
        return NULL;
    }

    LOG_DEBUG("ZMQ: Queue state before pop - size: %zu/%zu, head: %zu, tail: %zu, total_bytes: %zu",
              queue->size, queue->capacity, queue->head, queue->tail, queue->total_bytes);

    char *message = queue->messages[queue->head];
    size_t len = queue->message_sizes[queue->head];
    LOG_DEBUG("ZMQ: Popping message from head index %zu (message=%p, len=%zu)",
              queue->head, (void*)message, len);

    // Clear slot
    queue->messages[queue->head] = NULL;
    queue->message_sizes[queue->head] = 0;

    size_t old_head = queue->head;
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;
    queue->total_bytes -= len;

    if (message_len) *message_len = len;

    LOG_INFO("ZMQ: Popped message from queue (size: %zu bytes, remaining: %zu/%zu, total bytes: %zu)",
              len, queue->size, queue->capacity, queue->total_bytes);
    LOG_DEBUG("ZMQ: Queue state after pop - head: %zu->%zu, size: %zu, total_bytes: %zu",
              old_head, queue->head, queue->size, queue->total_bytes);
    
    // Log message preview if it's small
    if (len > 0 && len < 200 && message) {
        char preview[256];
        size_t preview_len = len < 100 ? len : 100;
        strncpy(preview, message, preview_len);
        preview[preview_len] = '\0';
        LOG_DEBUG("ZMQ: Popped message preview: %s", preview);
    }
    
    return message;
}

static size_t zmq_message_queue_size(const ZMQMessageQueue *queue) {
    size_t size = queue ? queue->size : 0;
    LOG_DEBUG("ZMQ: Message queue size query - queue=%p, size=%zu", (const void*)queue, size);
    return size;
}

static size_t zmq_message_queue_bytes(const ZMQMessageQueue *queue) {
    size_t bytes = queue ? queue->total_bytes : 0;
    LOG_DEBUG("ZMQ: Message queue bytes query - queue=%p, bytes=%zu", (const void*)queue, bytes);
    return bytes;
}

int zmq_socket_test_connection(ZMQContext *ctx, int timeout_ms) {
#ifdef HAVE_ZMQ
    if (!ctx || !ctx->socket) {
        LOG_ERROR("ZMQ: Cannot test connection - no socket");
        return -1;
    }

    LOG_INFO("ZMQ: Testing connection to %s (timeout: %d ms)", 
             ctx->endpoint ? ctx->endpoint : "unknown", timeout_ms);

    // Save current timeout settings
    int original_receive_timeout = ctx->receive_timeout;
    int original_send_timeout = ctx->send_timeout;

    // Use test timeout
    ctx->receive_timeout = timeout_ms;
    ctx->send_timeout = timeout_ms;
    zmq_setsockopt(ctx->socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    zmq_setsockopt(ctx->socket, ZMQ_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));

    // Send ping
    time_t start_time = time(NULL);
    char ping_msg[256];
    snprintf(ping_msg, sizeof(ping_msg),
             "{\"messageType\":\"%s\",\"timestamp\":%ld,\"test\":true}",
             ZMQ_HEARTBEAT_PING, (long)start_time);

    LOG_DEBUG("ZMQ: Sending test ping: %s", ping_msg);
    int rc = zmq_send(ctx->socket, ping_msg, strlen(ping_msg), 0);
    if (rc < 0) {
        LOG_ERROR("ZMQ: Failed to send test ping: %s", zmq_strerror(errno));
        // Restore original timeouts
        ctx->receive_timeout = original_receive_timeout;
        ctx->send_timeout = original_send_timeout;
        zmq_setsockopt(ctx->socket, ZMQ_RCVTIMEO, &original_receive_timeout, sizeof(original_receive_timeout));
        zmq_setsockopt(ctx->socket, ZMQ_SNDTIMEO, &original_send_timeout, sizeof(original_send_timeout));
        return -1;
    }

    // Wait for pong response
    char buffer[ZMQ_BUFFER_SIZE];
    rc = zmq_recv(ctx->socket, buffer, sizeof(buffer) - 1, 0);
    
    // Restore original timeouts
    ctx->receive_timeout = original_receive_timeout;
    ctx->send_timeout = original_send_timeout;
    zmq_setsockopt(ctx->socket, ZMQ_RCVTIMEO, &original_receive_timeout, sizeof(original_receive_timeout));
    zmq_setsockopt(ctx->socket, ZMQ_SNDTIMEO, &original_send_timeout, sizeof(original_send_timeout));

    if (rc < 0) {
        if (errno == EAGAIN) {
            LOG_ERROR("ZMQ: Connection test timeout after %d ms", timeout_ms);
        } else {
            LOG_ERROR("ZMQ: Failed to receive pong: %s", zmq_strerror(errno));
        }
        return -1;
    }

    buffer[rc] = '\0';
    
    // Parse response
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        LOG_ERROR("ZMQ: Failed to parse test response");
        return -1;
    }

    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
    cJSON *ping_timestamp = cJSON_GetObjectItem(json, "pingTimestamp");
    
    int success = 0;
    if (message_type && cJSON_IsString(message_type) &&
        strcmp(message_type->valuestring, ZMQ_HEARTBEAT_PONG) == 0 &&
        ping_timestamp && cJSON_IsNumber(ping_timestamp) &&
        (long)ping_timestamp->valuedouble == start_time) {
        
        LOG_INFO("ZMQ: Connection test successful (round-trip: %ld ms)", 
                 (long)(time(NULL) - start_time) * 1000);
        success = 0;
    } else {
        LOG_ERROR("ZMQ: Invalid pong response received");
        success = -1;
    }

    cJSON_Delete(json);
    return success;
#else
    (void)ctx;
    (void)timeout_ms;
    return -1;
#endif
}
