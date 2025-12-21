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
#include <ctype.h>

// Include ZMQ headers if available
#ifdef HAVE_ZMQ
#include <zmq.h>
#include <cjson/cJSON.h>
#include <ctype.h>
#endif

// Default buffer size for ZMQ messages
#define ZMQ_BUFFER_SIZE 65536

// Forward declaration
#ifdef HAVE_ZMQ
static int zmq_process_interactive(ZMQContext *ctx, struct ConversationState *state, const char *user_input);
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
    
    if (message_type && cJSON_IsString(message_type) && 
        strcmp(message_type->valuestring, "TEXT") == 0 && 
        content && cJSON_IsString(content)) {
        
        // Process text message with interactive tool call support
        LOG_INFO("ZMQ: Processing TEXT message with interactive mode (length: %zu)", strlen(content->valuestring));
        LOG_DEBUG("ZMQ: Message content: %.*s", 
                (int)(strlen(content->valuestring) > 200 ? 200 : strlen(content->valuestring)), 
                content->valuestring);
        
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
            // Send a final completion message
            snprintf(response, sizeof(response), 
                    "{\"messageType\": \"COMPLETED\", \"content\": \"Interactive processing completed successfully\"}");
        }
                
    } else {
        LOG_WARN("ZMQ: Invalid message format received");
        LOG_DEBUG("ZMQ: Available fields - messageType: %s, content: %s",
                 message_type ? "present" : "missing",
                 content ? "present" : "missing");
        snprintf(response, sizeof(response), 
                "{\"messageType\": \"ERROR\", \"content\": \"Invalid message format\"}");
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
                    continue;
                }
                
                LOG_INFO("ZMQ: Executing tool: %s (id: %s)", tool->name, tool->id);
                
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
                    
                    // Send error response
                    zmq_send_tool_result(ctx, tool->name, tool->id, results[i].tool_output, 1);
                    continue;
                }
                
                // Convert ToolCall to execute_tool parameters
                cJSON *input = tool->parameters
                    ? cJSON_Duplicate(tool->parameters, /*recurse*/1)
                    : cJSON_CreateObject();
                
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

bool zmq_socket_available(void) {
#ifdef HAVE_ZMQ
    return true;
#else
    return false;
#endif
}
