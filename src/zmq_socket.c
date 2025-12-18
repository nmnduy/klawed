/*
 * zmq_socket.c - ZeroMQ socket implementation for Klawed
 */

#include "zmq_socket.h"
#include "klawed_internal.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

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
    
    LOG_INFO("ZMQ: Initializing ZMQ socket");
    LOG_DEBUG("ZMQ: Endpoint: %s", endpoint);
    LOG_DEBUG("ZMQ: Socket type: %d", socket_type);
    
    ZMQContext *ctx = calloc(1, sizeof(ZMQContext));
    if (!ctx) {
        LOG_ERROR("ZMQ: Failed to allocate context memory");
        return NULL;
    }
    LOG_DEBUG("ZMQ: Allocated ZMQ context structure");
    
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
    
    // Set socket options for better performance
    int linger = 0; // Don't linger on close
    zmq_setsockopt(ctx->socket, ZMQ_LINGER, &linger, sizeof(linger));
    LOG_DEBUG("ZMQ: Set ZMQ_LINGER option to %d", linger);
    
    // Bind or connect based on socket type
    int rc;
    if (socket_type == ZMQ_REP || socket_type == ZMQ_PUB || socket_type == ZMQ_PUSH) {
        // Server/binding sockets
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
    ctx->daemon_mode = (socket_type == ZMQ_REP);
    
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
    if (!ctx || !ctx->socket || !message) {
        LOG_ERROR("ZMQ: Invalid parameters for send");
        return -1;
    }
    
    LOG_DEBUG("ZMQ: Sending %zu bytes to endpoint: %s", message_len, ctx->endpoint ? ctx->endpoint : "unknown");
    
    int rc = zmq_send(ctx->socket, message, message_len, 0);
    if (rc < 0) {
        LOG_ERROR("ZMQ: Failed to send message: %s (endpoint: %s)", zmq_strerror(errno), ctx->endpoint ? ctx->endpoint : "unknown");
        return -1;
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
    
    LOG_DEBUG("ZMQ: Waiting for message on endpoint: %s (timeout: %d ms, buffer size: %zu)", 
              ctx->endpoint ? ctx->endpoint : "unknown", timeout_ms, buffer_size);
    
    // Set timeout if specified
    if (timeout_ms >= 0) {
        zmq_setsockopt(ctx->socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    }
    
    int rc = zmq_recv(ctx->socket, buffer, buffer_size - 1, 0);
    if (rc < 0) {
        if (errno == EAGAIN) {
            LOG_DEBUG("ZMQ: Receive timeout after %d ms", timeout_ms);
        } else {
            LOG_ERROR("ZMQ: Failed to receive message: %s (endpoint: %s)", 
                     zmq_strerror(errno), ctx->endpoint ? ctx->endpoint : "unknown");
        }
        return -1;
    }
    
    buffer[rc] = '\0'; // Null-terminate the received data
    LOG_DEBUG("ZMQ: Received %d bytes from endpoint: %s", rc, ctx->endpoint ? ctx->endpoint : "unknown");
    
    // Log first 200 characters of received data for debugging
    if (rc > 0 && rc < 1024) {
        char preview[256];
        size_t preview_len = (size_t)(rc < 200 ? rc : 200);
        strncpy(preview, buffer, preview_len);
        preview[preview_len] = '\0';
        LOG_DEBUG("ZMQ: Received data preview: %s", preview);
    }
    
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
                "{\"error\": \"Invalid JSON\", \"message\": \"Failed to parse request\"}");
        zmq_socket_send(ctx, error_response, strlen(error_response));
        return -1;
    }
    
    LOG_DEBUG("ZMQ: JSON parsed successfully");
    
    // Extract message type and content
    LOG_DEBUG("ZMQ: Extracting message fields from JSON");
    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
    cJSON *content = cJSON_GetObjectItem(json, "content");
    cJSON *action = cJSON_GetObjectItem(json, "action");
    
    char response[ZMQ_BUFFER_SIZE];
    response[0] = '\0';
    
    if (message_type && cJSON_IsString(message_type) && 
        strcmp(message_type->valuestring, "TEXT") == 0 && 
        content && cJSON_IsString(content)) {
        
        // Process text message
        LOG_INFO("ZMQ: Processing TEXT message (length: %zu)", strlen(content->valuestring));
        LOG_DEBUG("ZMQ: Message content: %.*s", 
                (int)(strlen(content->valuestring) > 200 ? 200 : strlen(content->valuestring)), 
                content->valuestring);
        
        // Clear previous conversation (keep system message)
        LOG_DEBUG("ZMQ: Clearing previous conversation");
        clear_conversation(state);
        
        // Add user message to conversation
        LOG_DEBUG("ZMQ: Adding user message to conversation");
        add_user_message(state, content->valuestring);
        
        // Call AI API
        LOG_INFO("ZMQ: Calling AI API for inference");
        ApiResponse *api_response = call_api_with_retries(state);
        
        if (!api_response) {
            LOG_ERROR("ZMQ: Failed to get response from AI API");
            snprintf(response, sizeof(response), 
                    "{\"error\": \"AI inference failed\", \"message\": \"Failed to get response from AI\", \"timestamp\": %ld}",
                    time(NULL));
        } else if (api_response->error_message) {
            LOG_ERROR("ZMQ: AI API returned error: %s", api_response->error_message);
            snprintf(response, sizeof(response), 
                    "{\"error\": \"AI inference error\", \"message\": \"%s\", \"timestamp\": %ld}",
                    api_response->error_message, time(NULL));
            api_response_free(api_response);
        } else {
            // Success! Get the assistant's text response
            LOG_INFO("ZMQ: AI inference successful");
            const char *assistant_text = api_response->message.text;
            if (!assistant_text || strlen(assistant_text) == 0) {
                LOG_WARN("ZMQ: AI response is empty");
                assistant_text = "(No text response from AI)";
            }
            
            LOG_DEBUG("ZMQ: Assistant response (first 200 chars): %.*s", 
                     (int)(strlen(assistant_text) > 200 ? 200 : strlen(assistant_text)), 
                     assistant_text);
            
            // Create JSON response with the AI's text
            // We need to escape the JSON string properly
            cJSON *response_json = cJSON_CreateObject();
            cJSON_AddStringToObject(response_json, "status", "success");
            cJSON_AddStringToObject(response_json, "message", assistant_text);
            cJSON_AddNumberToObject(response_json, "timestamp", time(NULL));
            
            // Add tool call info if any
            if (api_response->tool_count > 0) {
                cJSON_AddNumberToObject(response_json, "tool_count", api_response->tool_count);
                cJSON *tools_array = cJSON_CreateArray();
                for (int i = 0; i < api_response->tool_count; i++) {
                    cJSON *tool_obj = cJSON_CreateObject();
                    cJSON_AddStringToObject(tool_obj, "name", api_response->tools[i].name);
                    cJSON_AddItemToArray(tools_array, tool_obj);
                }
                cJSON_AddItemToObject(response_json, "tools", tools_array);
            }
            
            char *response_str = cJSON_PrintUnformatted(response_json);
            if (response_str) {
                size_t response_len = strlen(response_str);
                if (response_len < sizeof(response)) {
                    strlcpy(response, response_str, sizeof(response));
                } else {
                    LOG_WARN("ZMQ: Response too large (%zu bytes), truncating to %zu bytes", 
                            response_len, sizeof(response) - 1);
                    strlcpy(response, response_str, sizeof(response));
                    // Add truncation warning
                    response[sizeof(response) - 1] = '\0';
                    // Note: The JSON might be malformed due to truncation
                    // In a production system, we'd want to handle this better
                }
                free(response_str);
            } else {
                LOG_ERROR("ZMQ: Failed to serialize response JSON");
                snprintf(response, sizeof(response), 
                        "{\"error\": \"JSON serialization failed\", \"timestamp\": %ld}",
                        time(NULL));
            }
            
            cJSON_Delete(response_json);
            api_response_free(api_response);
        }
                
    } else if (action && cJSON_IsString(action)) {
        // Process action
        LOG_INFO("ZMQ: Processing ACTION message: %s", action->valuestring);
        
        if (strcmp(action->valuestring, "ping") == 0) {
            LOG_DEBUG("ZMQ: Handling ping action");
            snprintf(response, sizeof(response), 
                    "{\"status\": \"ok\", \"action\": \"pong\", \"timestamp\": %ld}", time(NULL));
        } else if (strcmp(action->valuestring, "status") == 0) {
            LOG_DEBUG("ZMQ: Handling status action");
            snprintf(response, sizeof(response), 
                    "{\"status\": \"ok\", \"mode\": \"zmq\", \"endpoint\": \"%s\", \"timestamp\": %ld}",
                    ctx->endpoint, time(NULL));
        } else {
            LOG_WARN("ZMQ: Unknown action requested: %s", action->valuestring);
            snprintf(response, sizeof(response), 
                    "{\"error\": \"Unknown action\", \"action\": \"%s\"}", action->valuestring);
        }
    } else {
        LOG_WARN("ZMQ: Invalid message format received");
        LOG_DEBUG("ZMQ: Available fields - messageType: %s, content: %s, action: %s",
                 message_type ? "present" : "missing",
                 content ? "present" : "missing",
                 action ? "present" : "missing");
        snprintf(response, sizeof(response), 
                "{\"error\": \"Invalid message format\", \"required\": \"messageType: TEXT and content or action field\"}");
    }
    
    cJSON_Delete(json);
    LOG_DEBUG("ZMQ: JSON object cleaned up");
    
    // Send response
    if (response[0] != '\0') {
        LOG_DEBUG("ZMQ: Preparing to send response (length: %zu)", strlen(response));
        LOG_DEBUG("ZMQ: Response content: %.*s", 
                 (int)(strlen(response) > 200 ? 200 : strlen(response)), response);
        
        int send_result = zmq_socket_send(ctx, response, strlen(response));
        if (send_result == 0) {
            LOG_INFO("ZMQ: Response sent successfully");
        } else {
            LOG_ERROR("ZMQ: Failed to send response");
        }
    } else {
        LOG_ERROR("ZMQ: Empty response generated, not sending");
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
    
    LOG_INFO("ZMQ: =========================================");
    LOG_INFO("ZMQ: Starting ZMQ daemon mode");
    LOG_INFO("ZMQ: Endpoint: %s", ctx->endpoint);
    LOG_INFO("ZMQ: Socket type: ZMQ_REP (Reply)");
    LOG_INFO("ZMQ: Buffer size: %d bytes", ZMQ_BUFFER_SIZE);
    LOG_INFO("ZMQ: =========================================");
    
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
    }
    
    LOG_INFO("ZMQ: =========================================");
    LOG_INFO("ZMQ: ZMQ daemon mode stopping");
    LOG_INFO("ZMQ: Total messages processed: %d", message_count);
    LOG_INFO("ZMQ: Total errors: %d", error_count);
    LOG_INFO("ZMQ: =========================================");
    
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
    
    LOG_DEBUG("ZMQ: Creating event: %s", event_type);
    LOG_DEBUG("ZMQ: Event data (first 200 chars): %.*s", 
             (int)(strlen(data) > 200 ? 200 : strlen(data)), data);
    
    cJSON *event = cJSON_CreateObject();
    if (!event) {
        LOG_ERROR("ZMQ: Failed to create JSON object for event");
        return -1;
    }
    LOG_DEBUG("ZMQ: Created JSON object for event");
    
    cJSON_AddStringToObject(event, "event", event_type);
    cJSON_AddStringToObject(event, "data", data);
    cJSON_AddNumberToObject(event, "timestamp", time(NULL));
    LOG_DEBUG("ZMQ: Added fields to event JSON");
    
    char *json_str = cJSON_PrintUnformatted(event);
    if (!json_str) {
        LOG_ERROR("ZMQ: Failed to serialize event to JSON");
        cJSON_Delete(event);
        return -1;
    }
    LOG_DEBUG("ZMQ: Serialized event to JSON (length: %zu)", strlen(json_str));
    
    int rc = zmq_socket_send(ctx, json_str, strlen(json_str));
    if (rc == 0) {
        LOG_INFO("ZMQ: Event '%s' sent successfully", event_type);
    } else {
        LOG_ERROR("ZMQ: Failed to send event '%s'", event_type);
    }
    
    free(json_str);
    cJSON_Delete(event);
    LOG_DEBUG("ZMQ: Cleaned up event resources");
    
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
