/*
 * zmq_socket.c - Simple ZeroMQ socket implementation for Klawed
 *
 * Simplified implementation:
 * - ZMQ_PAIR socket with bind/connect
 * - Blocking receive (zmq_recv with infinite timeout)
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
#include <ctype.h>

// Include ZMQ headers if available
#ifdef HAVE_ZMQ
#include <zmq.h>
#include <cjson/cJSON.h>
#endif

// Default buffer size for ZMQ messages
#define ZMQ_BUFFER_SIZE 65536

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

    // Extract message type and content
    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
    cJSON *content = cJSON_GetObjectItem(json, "content");

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
    LOG_INFO("ZMQ: =========================================");

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
