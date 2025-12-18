/*
 * zmq_socket.c - ZeroMQ socket implementation for Klawed
 */

#include "zmq_socket.h"
#include "klawed_internal.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

// Include ZMQ headers if available
#ifdef HAVE_ZMQ
#include <zmq.h>
#include <cjson/cJSON.h>
#endif

// Default buffer size for ZMQ messages
#define ZMQ_BUFFER_SIZE 65536

ZMQContext* zmq_socket_init(const char *endpoint, int socket_type) {
#ifdef HAVE_ZMQ
    if (!endpoint) {
        LOG_ERROR("ZMQ: Endpoint cannot be NULL");
        return NULL;
    }
    
    ZMQContext *ctx = calloc(1, sizeof(ZMQContext));
    if (!ctx) {
        LOG_ERROR("ZMQ: Failed to allocate context");
        return NULL;
    }
    
    // Create ZMQ context
    ctx->context = zmq_ctx_new();
    if (!ctx->context) {
        LOG_ERROR("ZMQ: Failed to create context: %s", zmq_strerror(errno));
        free(ctx);
        return NULL;
    }
    
    // Create socket
    ctx->socket = zmq_socket(ctx->context, socket_type);
    if (!ctx->socket) {
        LOG_ERROR("ZMQ: Failed to create socket: %s", zmq_strerror(errno));
        zmq_ctx_term(ctx->context);
        free(ctx);
        return NULL;
    }
    
    // Set socket options for better performance
    int linger = 0; // Don't linger on close
    zmq_setsockopt(ctx->socket, ZMQ_LINGER, &linger, sizeof(linger));
    
    // Bind or connect based on socket type
    int rc;
    if (socket_type == ZMQ_REP || socket_type == ZMQ_PUB || socket_type == ZMQ_PUSH) {
        // Server/binding sockets
        rc = zmq_bind(ctx->socket, endpoint);
        if (rc != 0) {
            LOG_ERROR("ZMQ: Failed to bind to %s: %s", endpoint, zmq_strerror(errno));
            zmq_close(ctx->socket);
            zmq_ctx_term(ctx->context);
            free(ctx);
            return NULL;
        }
        LOG_INFO("ZMQ: Bound to %s", endpoint);
    } else {
        // Client/connecting sockets
        rc = zmq_connect(ctx->socket, endpoint);
        if (rc != 0) {
            LOG_ERROR("ZMQ: Failed to connect to %s: %s", endpoint, zmq_strerror(errno));
            zmq_close(ctx->socket);
            zmq_ctx_term(ctx->context);
            free(ctx);
            return NULL;
        }
        LOG_INFO("ZMQ: Connected to %s", endpoint);
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
    ctx->daemon_mode = (socket_type == ZMQ_REP);
    
    return ctx;
#else
    LOG_ERROR("ZMQ: ZeroMQ support not compiled in");
    return NULL;
#endif
}

void zmq_socket_cleanup(ZMQContext *ctx) {
#ifdef HAVE_ZMQ
    if (!ctx) return;
    
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

int zmq_socket_send(ZMQContext *ctx, const char *message, size_t message_len) {
#ifdef HAVE_ZMQ
    if (!ctx || !ctx->socket || !message) {
        LOG_ERROR("ZMQ: Invalid parameters for send");
        return -1;
    }
    
    int rc = zmq_send(ctx->socket, message, message_len, 0);
    if (rc < 0) {
        LOG_ERROR("ZMQ: Failed to send message: %s", zmq_strerror(errno));
        return -1;
    }
    
    LOG_DEBUG("ZMQ: Sent %zu bytes", message_len);
    return 0;
#else
    (void)ctx;
    (void)message;
    (void)message_len;
    return -1;
#endif
}

int zmq_socket_receive(ZMQContext *ctx, char *buffer, size_t buffer_size, int timeout_ms) {
#ifdef HAVE_ZMQ
    if (!ctx || !ctx->socket || !buffer || buffer_size == 0) {
        LOG_ERROR("ZMQ: Invalid parameters for receive");
        return -1;
    }
    
    // Set timeout if specified
    if (timeout_ms >= 0) {
        zmq_setsockopt(ctx->socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    }
    
    int rc = zmq_recv(ctx->socket, buffer, buffer_size - 1, 0);
    if (rc < 0) {
        if (errno == EAGAIN) {
            LOG_DEBUG("ZMQ: Receive timeout");
        } else {
            LOG_ERROR("ZMQ: Failed to receive message: %s", zmq_strerror(errno));
        }
        return -1;
    }
    
    buffer[rc] = '\0'; // Null-terminate the received data
    LOG_DEBUG("ZMQ: Received %d bytes", rc);
    return rc;
#else
    (void)ctx;
    (void)buffer;
    (void)buffer_size;
    (void)timeout_ms;
    return -1;
#endif
}

int zmq_socket_process_message(ZMQContext *ctx, struct ConversationState *state, struct TUIState *tui) {
#ifdef HAVE_ZMQ
    if (!ctx || !state) {
        LOG_ERROR("ZMQ: Invalid parameters for process_message");
        return -1;
    }
    (void)tui; // Unused parameter for now
    
    char buffer[ZMQ_BUFFER_SIZE];
    int received = zmq_socket_receive(ctx, buffer, sizeof(buffer), -1); // Blocking receive
    if (received <= 0) {
        return -1;
    }
    
    LOG_INFO("ZMQ: Processing message: %.*s", (int)(received > 100 ? 100 : received), buffer);
    
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
                "{\"error\": \"Invalid JSON\", \"message\": \"Failed to parse request\"}");
        zmq_socket_send(ctx, error_response, strlen(error_response));
        return -1;
    }
    
    // Extract message type and content
    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
    cJSON *content = cJSON_GetObjectItem(json, "content");
    cJSON *action = cJSON_GetObjectItem(json, "action");
    
    char response[ZMQ_BUFFER_SIZE];
    response[0] = '\0';
    
    if (message_type && cJSON_IsString(message_type) && 
        strcmp(message_type->valuestring, "TEXT") == 0 && 
        content && cJSON_IsString(content)) {
        
        // Process text message
        LOG_INFO("ZMQ: Processing text message: %.*s", 
                (int)(strlen(content->valuestring) > 100 ? 100 : strlen(content->valuestring)), 
                content->valuestring);
        
        // TODO: Actually process the message through the AI
        // For now, just echo back
        snprintf(response, sizeof(response), 
                "{\"status\": \"received\", \"message\": \"Processing: %s\", \"timestamp\": %ld}",
                content->valuestring, time(NULL));
                
    } else if (action && cJSON_IsString(action)) {
        // Process action
        if (strcmp(action->valuestring, "ping") == 0) {
            snprintf(response, sizeof(response), 
                    "{\"status\": \"ok\", \"action\": \"pong\", \"timestamp\": %ld}", time(NULL));
        } else if (strcmp(action->valuestring, "status") == 0) {
            snprintf(response, sizeof(response), 
                    "{\"status\": \"ok\", \"mode\": \"zmq\", \"endpoint\": \"%s\", \"timestamp\": %ld}",
                    ctx->endpoint, time(NULL));
        } else {
            snprintf(response, sizeof(response), 
                    "{\"error\": \"Unknown action\", \"action\": \"%s\"}", action->valuestring);
        }
    } else {
        snprintf(response, sizeof(response), 
                "{\"error\": \"Invalid message format\", \"required\": \"messageType: TEXT and content or action field\"}");
    }
    
    cJSON_Delete(json);
    
    // Send response
    if (response[0] != '\0') {
        zmq_socket_send(ctx, response, strlen(response));
    }
    
    return 0;
#else
    (void)ctx;
    (void)state;
    (void)tui;
    return -1;
#endif
}

int zmq_socket_daemon_mode(ZMQContext *ctx, struct ConversationState *state) {
#ifdef HAVE_ZMQ
    if (!ctx || !state) {
        LOG_ERROR("ZMQ: Invalid parameters for daemon_mode");
        return -1;
    }
    
    if (ctx->socket_type != ZMQ_REP) {
        LOG_ERROR("ZMQ: Daemon mode requires ZMQ_REP socket type");
        return -1;
    }
    
    LOG_INFO("ZMQ: Starting daemon mode on %s", ctx->endpoint);
    
    while (ctx->enabled) {
        if (zmq_socket_process_message(ctx, state, NULL) != 0) {
            // Non-fatal error, continue listening
            continue;
        }
    }
    
    return 0;
#else
    (void)ctx;
    (void)state;
    return -1;
#endif
}

int zmq_socket_send_event(ZMQContext *ctx, const char *event_type, const char *data) {
#ifdef HAVE_ZMQ
    if (!ctx || !ctx->socket || !event_type || !data) {
        LOG_ERROR("ZMQ: Invalid parameters for send_event");
        return -1;
    }
    
    if (ctx->socket_type != ZMQ_PUB) {
        LOG_ERROR("ZMQ: send_event requires ZMQ_PUB socket type");
        return -1;
    }
    
    cJSON *event = cJSON_CreateObject();
    if (!event) {
        LOG_ERROR("ZMQ: Failed to create JSON object for event");
        return -1;
    }
    
    cJSON_AddStringToObject(event, "event", event_type);
    cJSON_AddStringToObject(event, "data", data);
    cJSON_AddNumberToObject(event, "timestamp", time(NULL));
    
    char *json_str = cJSON_PrintUnformatted(event);
    if (!json_str) {
        LOG_ERROR("ZMQ: Failed to serialize event to JSON");
        cJSON_Delete(event);
        return -1;
    }
    
    int rc = zmq_socket_send(ctx, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(event);
    
    return rc;
#else
    (void)ctx;
    (void)event_type;
    (void)data;
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
