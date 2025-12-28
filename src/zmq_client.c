/*
 * zmq_client.c - Thread-based ZMQ client for Klawed
 *
 * Implements a ZMQ client with separate receiver thread and main interactive thread.
 * Uses thread-safe message queues for communication between threads.
 * Provides reliable message delivery with ID/ACK system.
 */

#include "zmq_client.h"
#include "zmq_message_queue.h"
#include "zmq_socket.h"
#include "logger.h"
#include "cjson/cJSON.h"
#include <zmq.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <bsd/string.h>
#include <time.h>
#include <arpa/inet.h>

// ZMQ client configuration
#define ZMQ_CLIENT_BUFFER_SIZE 65536
#define ZMQ_CLIENT_DEFAULT_TIMEOUT_MS 120000 // 2 minutes

// Pending queue defaults (client side)
#define ZMQ_CLIENT_DEFAULT_MAX_PENDING 50
#define ZMQ_CLIENT_DEFAULT_ACK_TIMEOUT_MS 3000    // 3 seconds
#define ZMQ_CLIENT_DEFAULT_MAX_RETRIES 5

// Duplicate detection
#define ZMQ_CLIENT_MAX_SEEN_MESSAGES 1000
#define ZMQ_CLIENT_SEEN_MESSAGE_TTL_MS 30000  // 30 seconds

// Message ID constants
#define ZMQ_CLIENT_MESSAGE_ID_HEX_LENGTH 33  // 128 bits = 32 hex chars + null terminator
#define ZMQ_CLIENT_HASH_SAMPLE_SIZE 256      // Number of characters to sample from message for hash

// Polling constants
#define ZMQ_POLL_TIMEOUT_MS 100
#define USER_INPUT_CHECK_TIMEOUT_MS 100

// Forward declarations for static helper functions
static int64_t get_current_time_ms(void);
static void init_pending_queue(ZMQClientContextThreaded *ctx);
static void free_pending_message(ZMQClientPendingMessage *msg);
static int add_to_pending_queue(ZMQClientContextThreaded *ctx, const char *message_id,
                               const char *message_json, int64_t sent_time_ms);
static int remove_from_pending_queue(ZMQClientContextThreaded *ctx, const char *message_id);
static void hash_message_id(int64_t timestamp, const char *message, size_t message_len,
                           uint32_t salt, uint8_t out_hash[16]);
static void hash_to_hex(const uint8_t hash[16], char *out_hex, size_t out_hex_size);
static int is_duplicate_message(ZMQClientContextThreaded *ctx, const char *message_id);
static void add_seen_message(ZMQClientContextThreaded *ctx, const char *message_id);
static void log_pending_queue_state(ZMQClientContextThreaded *ctx, const char *context);
static void* receiver_thread_func(void *arg);
static int check_and_resend_pending_internal(ZMQClientContextThreaded *ctx, int64_t current_time_ms);

/**
 * Get current time in milliseconds
 */
static int64_t get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
}

/**
 * Check if a message ID is a duplicate
 */
static int is_duplicate_message(ZMQClientContextThreaded *ctx, const char *message_id) {
    if (!ctx || !message_id) return 0;
    
    int64_t current_time = get_current_time_ms();
    
    for (int i = 0; i < ctx->seen_message_count; i++) {
        if (ctx->seen_messages[i].message_id && 
            strcmp(ctx->seen_messages[i].message_id, message_id) == 0) {
            // Check if entry is expired
            if (current_time - ctx->seen_messages[i].timestamp_ms > ZMQ_CLIENT_SEEN_MESSAGE_TTL_MS) {
                // Expired, free it
                free(ctx->seen_messages[i].message_id);
                ctx->seen_messages[i].message_id = NULL;
                return 0;
            }
            return 1; // Duplicate found
        }
    }
    return 0;
}

/**
 * Print usage information for ZMQ client mode
 */
void zmq_client_print_usage(const char *program_name) {
    printf("Usage: %s --zmq-client <endpoint>\n", program_name);
    printf("\n");
    printf("Connect to a Klawed daemon running with ZMQ enabled.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  <endpoint>    ZMQ endpoint to connect to (e.g., tcp://127.0.0.1:5555)\n");
    printf("\n");
    printf("Commands (while connected):\n");
    printf("  /help         Show this help message\n");
    printf("  /quit, /exit  Disconnect and exit\n");
    printf("\n");
    printf("Example:\n");
    printf("  %s --zmq-client tcp://127.0.0.1:5555\n", program_name);
}

/**
 * Generate a message ID for a message
 */
int zmq_client_generate_message_id(ZMQClientContextThreaded *ctx, const char *message,
                                          size_t message_len, char *out_id, size_t out_id_size) {
    if (!ctx || !message || !out_id || out_id_size < ZMQ_CLIENT_MESSAGE_ID_HEX_LENGTH) {
        return -1;
    }
    
    int64_t timestamp = get_current_time_ms();
    uint8_t hash[16];
    
    hash_message_id(timestamp, message, message_len, ctx->salt, hash);
    hash_to_hex(hash, out_id, out_id_size);
    
    return 0;
}

/**
 * Send a message with message ID for reliable delivery
 */
int zmq_client_send_message_with_id(ZMQClientContextThreaded *ctx, const char *message,
                                           char *message_id_out, size_t message_id_out_size) {
    if (!ctx || !message) {
        LOG_ERROR("ZMQ Client Threaded: Invalid parameters for send_message_with_id");
        return -1;
    }
    
    // Parse message to add message ID
    cJSON *json = cJSON_Parse(message);
    if (!json) {
        LOG_ERROR("ZMQ Client Threaded: Failed to parse JSON message");
        return -1;
    }
    
    // Generate message ID
    char message_id[ZMQ_CLIENT_MESSAGE_ID_HEX_LENGTH];
    if (zmq_client_generate_message_id(ctx, message, strlen(message), 
                                               message_id, sizeof(message_id)) != 0) {
        LOG_ERROR("ZMQ Client Threaded: Failed to generate message ID");
        cJSON_Delete(json);
        return -1;
    }
    
    // Add message ID to JSON
    cJSON_AddStringToObject(json, "messageId", message_id);
    
    // Convert back to string
    char *json_with_id = cJSON_PrintUnformatted(json);
    if (!json_with_id) {
        LOG_ERROR("ZMQ Client Threaded: Failed to serialize JSON with message ID");
        cJSON_Delete(json);
        return -1;
    }
    
    LOG_DEBUG("ZMQ Client Threaded: Sending message with ID %s: %s", message_id, json_with_id);
    
    // Send the message
    int rc = zmq_send(ctx->zmq_socket, json_with_id, strlen(json_with_id), 0);
    if (rc < 0) {
        LOG_ERROR("ZMQ Client Threaded: Failed to send message: %s", zmq_strerror(errno));
        free(json_with_id);
        cJSON_Delete(json);
        return -1;
    }
    
    // Add to pending queue
    int64_t sent_time = get_current_time_ms();
    if (add_to_pending_queue(ctx, message_id, json_with_id, sent_time) != 0) {
        LOG_WARN("ZMQ Client Threaded: Failed to add message to pending queue");
    }
    
    // Copy message ID to output buffer if requested
    if (message_id_out && message_id_out_size >= ZMQ_CLIENT_MESSAGE_ID_HEX_LENGTH) {
        strlcpy(message_id_out, message_id, message_id_out_size);
    }
    
    ctx->messages_sent++;
    
    free(json_with_id);
    cJSON_Delete(json);
    return 0;
}

/**
 * Process an ACK message
 */
int zmq_client_process_ack(ZMQClientContextThreaded *ctx, const char *message_id) {
    if (!ctx || !message_id) {
        LOG_ERROR("ZMQ Client Threaded: Invalid parameters for process_ack");
        return -1;
    }
    
    LOG_DEBUG("ZMQ Client Threaded: Processing ACK for message %s", message_id);
    
    // Remove from pending queue
    if (remove_from_pending_queue(ctx, message_id) == 0) {
        LOG_DEBUG("ZMQ Client Threaded: Message %s acknowledged and removed from pending queue", 
                 message_id);
        return 0;
    } else {
        LOG_DEBUG("ZMQ Client Threaded: Message %s not found in pending queue (may have timed out)", 
                 message_id);
        return -1;
    }
}

/**
 * Send an ACK message
 */
int zmq_client_send_ack(ZMQClientContextThreaded *ctx, const char *message_id) {
    if (!ctx || !message_id) {
        LOG_ERROR("ZMQ Client Threaded: Invalid parameters for send_ack");
        return -1;
    }
    
    // Create ACK message
    cJSON *ack = cJSON_CreateObject();
    cJSON_AddStringToObject(ack, "messageType", "ACK");
    cJSON_AddStringToObject(ack, "messageId", message_id);
    
    char *json_str = cJSON_PrintUnformatted(ack);
    LOG_DEBUG("ZMQ Client Threaded: Sending ACK for message %s: %s", message_id, json_str);
    
    int rc = zmq_send(ctx->zmq_socket, json_str, strlen(json_str), 0);
    if (rc < 0) {
        LOG_ERROR("ZMQ Client Threaded: Failed to send ACK: %s", zmq_strerror(errno));
        free(json_str);
        cJSON_Delete(ack);
        return -1;
    }
    
    free(json_str);
    cJSON_Delete(ack);
    return 0;
}

/**
 * Check and resend pending messages that have timed out
 */
int zmq_client_check_and_resend_pending(ZMQClientContextThreaded *ctx, int64_t current_time_ms) {
    if (!ctx) return 0;
    return check_and_resend_pending_internal(ctx, current_time_ms);
}

/**
 * Clean up pending message queue
 */
void zmq_client_cleanup_pending_queue(ZMQClientContextThreaded *ctx) {
    if (!ctx) return;
    
    ZMQClientPendingMessage *msg = ctx->pending_queue.head;
    while (msg) {
        ZMQClientPendingMessage *next = msg->next;
        free_pending_message(msg);
        msg = next;
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
}

/**
 * Process a received message
 */
void zmq_client_process_message(ZMQClientContextThreaded *ctx, const char *response) {
    if (!ctx || !response) return;
    
    LOG_DEBUG("ZMQ Client Threaded: Processing message: %s", response);
    
    cJSON *json = cJSON_Parse(response);
    if (!json) {
        LOG_ERROR("ZMQ Client Threaded: Failed to parse JSON response");
        return;
    }
    
    cJSON *message_type = cJSON_GetObjectItem(json, "messageType");
    cJSON *message_id = cJSON_GetObjectItem(json, "messageId");
    
    if (message_type && message_id) {
        const char *type_str = cJSON_GetStringValue(message_type);
        const char *id_str = cJSON_GetStringValue(message_id);
        
        if (type_str && id_str) {
            if (strcmp(type_str, "ACK") == 0) {
                // This is an ACK for a message we sent
                zmq_client_process_ack(ctx, id_str);
            } else {
                // This is a message from the daemon that needs an ACK
                // Check for duplicate
                if (is_duplicate_message(ctx, id_str)) {
                    LOG_DEBUG("ZMQ Client Threaded: Duplicate message %s, ignoring", id_str);
                } else {
                    // Add to seen messages
                    add_seen_message(ctx, id_str);
                    
                    // Send ACK
                    zmq_client_send_ack(ctx, id_str);
                    
                    // Handle different message types
                    if (strcmp(type_str, "TEXT") == 0) {
                        // Display TEXT message
                        cJSON *content = cJSON_GetObjectItem(json, "content");
                        if (content && cJSON_IsString(content)) {
                            printf("\n[Daemon]: %s\n", cJSON_GetStringValue(content));
                            fflush(stdout);
                        }
                    } else if (strcmp(type_str, "TOOL") == 0) {
                        // Display TOOL message (tool execution starting)
                        cJSON *tool_name = cJSON_GetObjectItem(json, "toolName");
                        cJSON *tool_id = cJSON_GetObjectItem(json, "toolId");
                        cJSON *tool_parameters = cJSON_GetObjectItem(json, "toolParameters");
                        
                        if (tool_name && cJSON_IsString(tool_name)) {
                            printf("\n[Tool]: Executing %s tool", cJSON_GetStringValue(tool_name));
                            
                            if (tool_id && cJSON_IsString(tool_id)) {
                                printf(" (ID: %s)", cJSON_GetStringValue(tool_id));
                            }
                            
                            if (tool_parameters && cJSON_IsObject(tool_parameters)) {
                                // Print parameters in a readable format
                                printf(" with parameters: ");
                                cJSON *param = tool_parameters->child;
                                int param_count = 0;
                                while (param) {
                                    if (param_count > 0) printf(", ");
                                    printf("%s=", param->string);
                                    if (cJSON_IsString(param)) {
                                        printf("\"%s\"", cJSON_GetStringValue(param));
                                    } else if (cJSON_IsNumber(param)) {
                                        printf("%g", param->valuedouble);
                                    } else if (cJSON_IsBool(param)) {
                                        printf("%s", param->valueint ? "true" : "false");
                                    } else if (cJSON_IsNull(param)) {
                                        printf("null");
                                    } else {
                                        printf("[object]");
                                    }
                                    param = param->next;
                                    param_count++;
                                }
                            }
                            printf("\n");
                            fflush(stdout);
                        }
                    } else if (strcmp(type_str, "TOOL_RESULT") == 0) {
                        // Display TOOL_RESULT message (tool execution completed)
                        cJSON *tool_name = cJSON_GetObjectItem(json, "toolName");
                        cJSON *tool_id = cJSON_GetObjectItem(json, "toolId");
                        cJSON *is_error = cJSON_GetObjectItem(json, "isError");
                        cJSON *tool_output = cJSON_GetObjectItem(json, "toolOutput");
                        
                        if (tool_name && cJSON_IsString(tool_name)) {
                            printf("\n[Tool Result]: %s tool ", cJSON_GetStringValue(tool_name));
                            
                            if (is_error && cJSON_IsBool(is_error) && is_error->valueint) {
                                printf("failed");
                            } else {
                                printf("completed successfully");
                            }
                            
                            if (tool_id && cJSON_IsString(tool_id)) {
                                printf(" (ID: %s)", cJSON_GetStringValue(tool_id));
                            }
                            printf("\n");
                            
                            // Display tool output if available
                            if (tool_output) {
                                if (cJSON_IsObject(tool_output) || cJSON_IsArray(tool_output)) {
                                    // For complex output, show a summary
                                    char *output_str = cJSON_PrintUnformatted(tool_output);
                                    if (output_str) {
                                        size_t output_len = strlen(output_str);
                                        if (output_len > 200) {
                                            printf("Output (truncated): %.200s...\n", output_str);
                                        } else {
                                            printf("Output: %s\n", output_str);
                                        }
                                        free(output_str);
                                    }
                                } else if (cJSON_IsString(tool_output)) {
                                    const char *output = cJSON_GetStringValue(tool_output);
                                    size_t output_len = strlen(output);
                                    if (output_len > 500) {
                                        printf("Output (truncated): %.500s...\n", output);
                                    } else {
                                        printf("Output: %s\n", output);
                                    }
                                }
                            }
                            fflush(stdout);
                        }
                    } else if (strcmp(type_str, "ERROR") == 0) {
                        // Display ERROR message
                        cJSON *content = cJSON_GetObjectItem(json, "content");
                        if (content && cJSON_IsString(content)) {
                            printf("\n[Error]: %s\n", cJSON_GetStringValue(content));
                            fflush(stdout);
                        }
                    } else if (strcmp(type_str, "NACK") == 0) {
                        // Display NACK message
                        cJSON *content = cJSON_GetObjectItem(json, "content");
                        if (content && cJSON_IsString(content)) {
                            printf("\n[NACK]: %s\n", cJSON_GetStringValue(content));
                            fflush(stdout);
                        }
                    } else {
                        LOG_DEBUG("ZMQ Client Threaded: Unknown message type: %s", type_str);
                    }
                }
            }
        }
    } else if (message_type) {
        // Message without ID (legacy or error)
        const char *type_str = cJSON_GetStringValue(message_type);
        if (type_str) {
            if (strcmp(type_str, "TEXT") == 0) {
                cJSON *content = cJSON_GetObjectItem(json, "content");
                if (content && cJSON_IsString(content)) {
                    printf("\n[Daemon]: %s\n", cJSON_GetStringValue(content));
                    fflush(stdout);
                }
            } else if (strcmp(type_str, "ERROR") == 0) {
                cJSON *content = cJSON_GetObjectItem(json, "content");
                if (content && cJSON_IsString(content)) {
                    printf("\n[Error]: %s\n", cJSON_GetStringValue(content));
                    fflush(stdout);
                }
            }
        }
    }
    
    cJSON_Delete(json);
}

/**
 * Check for user input (non-blocking)
 */
int zmq_client_check_user_input(char *buffer, size_t buffer_size, int timeout_ms) {
    if (!buffer || buffer_size == 0) return -1;
    
    fd_set readfds;
    struct timeval tv;
    
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    int rc = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
    if (rc < 0) {
        // Error
        return -1;
    } else if (rc == 0) {
        // Timeout
        return 0;
    } else {
        // Data available
        if (fgets(buffer, (int)buffer_size, stdin)) {
            // Remove newline
            buffer[strcspn(buffer, "\n")] = '\0';
            return 1;
        }
        return -1;
    }
}

/**
 * Main ZMQ client mode entry point
 */
int zmq_client_mode(const char *endpoint) {
    ZMQClientContextThreaded *ctx = zmq_client_init(endpoint);
    if (!ctx) {
        LOG_ERROR("Failed to initialize ZMQ client for %s", endpoint);
        printf("\nFailed to initialize ZMQ client for %s\n", endpoint);
        return 1;
    }
    
    // Initialize connection
    if (zmq_client_start(ctx) != 0) {
        LOG_ERROR("Failed to connect to %s", endpoint);
        printf("\nFailed to connect to %s\n", endpoint);
        printf("Make sure Klawed is running with ZMQ enabled and listening on this endpoint.\n");
        printf("Check: KLAWED_ZMQ_ENDPOINT=%s\n", endpoint);
        zmq_client_cleanup(ctx);
        return 1;
    }
    
    printf("\nConnected to %s\n", endpoint);
    printf("Type your messages (or /help for commands)\n");
    printf("-----------------------------------------\n");
    
    // Interactive loop
    char input[ZMQ_CLIENT_BUFFER_SIZE];
    bool prompt_printed = false;
    
    while (zmq_client_is_running(ctx)) {
        // Only print prompt if we haven't already printed one
        if (!prompt_printed) {
            printf("\n> ");
            fflush(stdout);
            prompt_printed = true;
        }
        
        int input_result = zmq_client_check_user_input(input, sizeof(input), USER_INPUT_CHECK_TIMEOUT_MS);
        if (input_result < 0) {
            // Error or EOF
            break;
        } else if (input_result == 0) {
            // Timeout, check for pending messages
            int64_t current_time = get_current_time_ms();
            zmq_client_check_and_resend_pending(ctx, current_time);
            continue;
        }
        
        // Got input - reset prompt flag for next iteration
        prompt_printed = false;
        
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
            zmq_client_send_text(ctx, input);
        }
    }
    
    // Cleanup
    zmq_client_stop(ctx);
    zmq_client_cleanup(ctx);
    
    LOG_INFO("ZMQ Client Threaded exiting");
    return 0;
}

/**
 * Add a message ID to the seen messages list
 */
static void add_seen_message(ZMQClientContextThreaded *ctx, const char *message_id) {
    if (!ctx || !message_id) return;
    
    // Check if we should remove this message ID first (in case it's a resend)
    for (int i = 0; i < ctx->seen_message_count; i++) {
        if (ctx->seen_messages[i].message_id && 
            strcmp(ctx->seen_messages[i].message_id, message_id) == 0) {
            free(ctx->seen_messages[i].message_id);
            ctx->seen_messages[i].message_id = NULL;
            break;
        }
    }
    
    // Find empty slot or oldest entry
    int empty_slot = -1;
    int64_t oldest_time = INT64_MAX;
    int oldest_index = 0;
    int64_t current_time = get_current_time_ms();
    
    for (int i = 0; i < ZMQ_CLIENT_MAX_SEEN_MESSAGES; i++) {
        if (i < ctx->seen_message_count) {
            if (!ctx->seen_messages[i].message_id) {
                empty_slot = i;
                break;
            } else {
                // Check if expired
                if (current_time - ctx->seen_messages[i].timestamp_ms > ZMQ_CLIENT_SEEN_MESSAGE_TTL_MS) {
                    free(ctx->seen_messages[i].message_id);
                    ctx->seen_messages[i].message_id = NULL;
                    empty_slot = i;
                    break;
                }
                // Track oldest
                if (ctx->seen_messages[i].timestamp_ms < oldest_time) {
                    oldest_time = ctx->seen_messages[i].timestamp_ms;
                    oldest_index = i;
                }
            }
        } else {
            // We haven't filled up to this index yet
            empty_slot = i;
            if (i >= ctx->seen_message_count) {
                ctx->seen_message_count = i + 1;
            }
            break;
        }
    }
    
    // Use empty slot or replace oldest
    int target_index = (empty_slot >= 0) ? empty_slot : oldest_index;
    
    // Allocate and copy message ID
    ctx->seen_messages[target_index].message_id = strdup(message_id);
    if (ctx->seen_messages[target_index].message_id) {
        ctx->seen_messages[target_index].timestamp_ms = current_time;
    }
}

/**
 * Log pending queue state for debugging
 */
static void log_pending_queue_state(ZMQClientContextThreaded *ctx, const char *context) {
    if (!ctx) return;
    
    LOG_DEBUG("ZMQ Client Threaded: Pending queue state (%s): %d messages pending", 
              context, ctx->pending_queue.count);
    
    ZMQClientPendingMessage *msg = ctx->pending_queue.head;
    int i = 0;
    while (msg) {
        LOG_DEBUG("  [%d] ID: %s, retries: %d, age: %lld ms", 
                  i++, msg->message_id, msg->retry_count,
                  get_current_time_ms() - msg->sent_time_ms);
        msg = msg->next;
    }
}

/**
 * Simple hash function for message IDs
 */
static void hash_message_id(int64_t timestamp, const char *message, size_t message_len,
                           uint32_t salt, uint8_t out_hash[16]) {
    // Simple hash combining timestamp, message content, and salt
    // In a real implementation, this would use a proper cryptographic hash
    uint32_t hash_parts[4] = {0};
    
    // Use timestamp
    hash_parts[0] = (uint32_t)(timestamp >> 32);
    hash_parts[1] = (uint32_t)timestamp;
    
    // Use salt
    hash_parts[2] = salt;
    
    // Simple hash of message content
    uint32_t message_hash = 0;
    size_t sample_size = (message_len < ZMQ_CLIENT_HASH_SAMPLE_SIZE) ? 
                         message_len : ZMQ_CLIENT_HASH_SAMPLE_SIZE;
    for (size_t i = 0; i < sample_size; i++) {
        message_hash = message_hash * 31 + (uint8_t)message[i];
    }
    hash_parts[3] = message_hash;
    
    // Copy to output
    memcpy(out_hash, hash_parts, 16);
}

/**
 * Convert hash to hex string
 */
static void hash_to_hex(const uint8_t hash[16], char *out_hex, size_t out_hex_size) {
    if (out_hex_size < ZMQ_CLIENT_MESSAGE_ID_HEX_LENGTH) return;
    
    static const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out_hex[i*2] = hex_chars[(hash[i] >> 4) & 0xF];
        out_hex[i*2+1] = hex_chars[hash[i] & 0xF];
    }
    out_hex[32] = '\0';
}

/**
 * Initialize pending queue
 */
static void init_pending_queue(ZMQClientContextThreaded *ctx) {
    if (!ctx) return;
    
    ctx->pending_queue.head = NULL;
    ctx->pending_queue.tail = NULL;
    ctx->pending_queue.count = 0;
    ctx->pending_queue.max_pending = ZMQ_CLIENT_DEFAULT_MAX_PENDING;
    ctx->pending_queue.timeout_ms = ZMQ_CLIENT_DEFAULT_ACK_TIMEOUT_MS;
    ctx->pending_queue.max_retries = ZMQ_CLIENT_DEFAULT_MAX_RETRIES;
}

/**
 * Free a pending message
 */
static void free_pending_message(ZMQClientPendingMessage *msg) {
    if (!msg) return;
    
    if (msg->message_id) free(msg->message_id);
    if (msg->message_json) free(msg->message_json);
    free(msg);
}

/**
 * Add message to pending queue
 */
static int add_to_pending_queue(ZMQClientContextThreaded *ctx, const char *message_id,
                               const char *message_json, int64_t sent_time_ms) {
    if (!ctx || !message_id || !message_json) return -1;
    
    // Check if queue is full
    if (ctx->pending_queue.count >= ctx->pending_queue.max_pending) {
        LOG_WARN("ZMQ Client Threaded: Pending queue full (%d messages), dropping oldest",
                 ctx->pending_queue.count);
        
        // Remove oldest message
        ZMQClientPendingMessage *oldest = ctx->pending_queue.head;
        if (oldest) {
            ctx->pending_queue.head = oldest->next;
            if (!ctx->pending_queue.head) {
                ctx->pending_queue.tail = NULL;
            }
            free_pending_message(oldest);
            ctx->pending_queue.count--;
        }
    }
    
    // Create new pending message
    ZMQClientPendingMessage *msg = calloc(1, sizeof(ZMQClientPendingMessage));
    if (!msg) {
        LOG_ERROR("ZMQ Client Threaded: Failed to allocate pending message");
        return -1;
    }
    
    msg->message_id = strdup(message_id);
    msg->message_json = strdup(message_json);
    msg->sent_time_ms = sent_time_ms;
    msg->retry_count = 0;
    msg->next = NULL;
    
    if (!msg->message_id || !msg->message_json) {
        LOG_ERROR("ZMQ Client Threaded: Failed to allocate message strings");
        free_pending_message(msg);
        return -1;
    }
    
    // Add to queue
    if (!ctx->pending_queue.tail) {
        ctx->pending_queue.head = msg;
        ctx->pending_queue.tail = msg;
    } else {
        ctx->pending_queue.tail->next = msg;
        ctx->pending_queue.tail = msg;
    }
    
    ctx->pending_queue.count++;
    log_pending_queue_state(ctx, "after add");
    
    return 0;
}

/**
 * Remove message from pending queue by ID
 */
static int remove_from_pending_queue(ZMQClientContextThreaded *ctx, const char *message_id) {
    if (!ctx || !message_id || !ctx->pending_queue.head) return -1;
    
    ZMQClientPendingMessage *prev = NULL;
    ZMQClientPendingMessage *current = ctx->pending_queue.head;
    
    while (current) {
        if (strcmp(current->message_id, message_id) == 0) {
            // Found it
            if (prev) {
                prev->next = current->next;
            } else {
                ctx->pending_queue.head = current->next;
            }
            
            if (current == ctx->pending_queue.tail) {
                ctx->pending_queue.tail = prev;
            }
            
            free_pending_message(current);
            ctx->pending_queue.count--;
            log_pending_queue_state(ctx, "after remove");
            return 0;
        }
        
        prev = current;
        current = current->next;
    }
    
    LOG_DEBUG("ZMQ Client Threaded: Message ID %s not found in pending queue", message_id);
    return -1;
}

/**
 * Receiver thread function
 */
static void* receiver_thread_func(void *arg) {
    ZMQClientContextThreaded *ctx = (ZMQClientContextThreaded *)arg;
    if (!ctx) return NULL;
    
    LOG_DEBUG("ZMQ Client Threaded: Receiver thread started");
    
    zmq_pollitem_t items[1];
    items[0].socket = ctx->zmq_socket;
    items[0].events = ZMQ_POLLIN;
    items[0].fd = 0;
    items[0].revents = 0;
    
    while (ctx->running) {
        int rc = zmq_poll(items, 1, ZMQ_POLL_TIMEOUT_MS);
        if (rc < 0) {
            LOG_ERROR("ZMQ Client Threaded: Poll error: %s", zmq_strerror(errno));
            break;
        }
        
        if (items[0].revents & ZMQ_POLLIN) {
            char buffer[ZMQ_CLIENT_BUFFER_SIZE];
            int bytes = zmq_recv(ctx->zmq_socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes < 0) {
                LOG_ERROR("ZMQ Client Threaded: Receive error: %s", zmq_strerror(errno));
                ctx->errors++;
                continue;
            }
            
            buffer[bytes] = '\0';
            ctx->messages_received++;
            
            LOG_DEBUG("ZMQ Client Threaded: Received %d bytes: %s", bytes, buffer);
            
            // Process the message
            zmq_client_process_message(ctx, buffer);
        }
        
        // Check for pending messages that need resending
        int64_t current_time = get_current_time_ms();
        check_and_resend_pending_internal(ctx, current_time);
    }
    
    LOG_DEBUG("ZMQ Client Threaded: Receiver thread exiting");
    return NULL;
}

/**
 * Internal function to check and resend pending messages
 */
static int check_and_resend_pending_internal(ZMQClientContextThreaded *ctx, int64_t current_time_ms) {
    if (!ctx || !ctx->pending_queue.head) return 0;
    
    int resent_count = 0;
    ZMQClientPendingMessage *msg = ctx->pending_queue.head;
    
    while (msg) {
        int64_t age = current_time_ms - msg->sent_time_ms;
        
        if (age > ctx->pending_queue.timeout_ms) {
            if (msg->retry_count >= ctx->pending_queue.max_retries) {
                LOG_WARN("ZMQ Client Threaded: Message %s exceeded max retries (%d), giving up",
                         msg->message_id, ctx->pending_queue.max_retries);
                
                // Remove from queue
                ZMQClientPendingMessage *to_remove = msg;
                msg = msg->next;
                remove_from_pending_queue(ctx, to_remove->message_id);
                continue;
            }
            
            // Resend the message
            LOG_DEBUG("ZMQ Client Threaded: Resending message %s (retry %d, age %lld ms)",
                     msg->message_id, msg->retry_count + 1, age);
            
            int rc = zmq_send(ctx->zmq_socket, msg->message_json, strlen(msg->message_json), 0);
            if (rc < 0) {
                LOG_ERROR("ZMQ Client Threaded: Failed to resend message: %s", zmq_strerror(errno));
                ctx->errors++;
            } else {
                msg->sent_time_ms = current_time_ms;
                msg->retry_count++;
                resent_count++;
                ctx->messages_sent++; // Count resend as a new send
            }
        }
        
        msg = msg->next;
    }
    
    if (resent_count > 0) {
        LOG_DEBUG("ZMQ Client Threaded: Resent %d pending messages", resent_count);
    }
    
    return resent_count;
}

/**
 * Send a text message to the daemon
 */
int zmq_client_send_text(ZMQClientContextThreaded *ctx, const char *text) {
    if (!ctx || !text) {
        LOG_ERROR("ZMQ Client Threaded: Invalid parameters for send_text");
        return -1;
    }

    // Create JSON message
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "messageType", "TEXT");
    cJSON_AddStringToObject(message, "content", text);

    char *json_str = cJSON_PrintUnformatted(message);
    LOG_DEBUG("ZMQ Client Threaded: Message JSON: %s", json_str);

    // Send the message with ID for reliable delivery
    int rc = zmq_client_send_message_with_id(ctx, json_str, NULL, 0);
    
    free(json_str);
    cJSON_Delete(message);
    
    return rc;
}



/**
 * Initialize ZMQ client context (threaded version)
 */
ZMQClientContextThreaded* zmq_client_init(const char *endpoint) {
    if (!endpoint) {
        LOG_ERROR("ZMQ Client Threaded: Invalid endpoint for init");
        return NULL;
    }

    ZMQClientContextThreaded *ctx = calloc(1, sizeof(ZMQClientContextThreaded));
    if (!ctx) {
        LOG_ERROR("ZMQ Client Threaded: Failed to allocate context");
        return NULL;
    }

    ctx->endpoint = strdup(endpoint);
    if (!ctx->endpoint) {
        LOG_ERROR("ZMQ Client Threaded: Failed to allocate endpoint string");
        free(ctx);
        return NULL;
    }

    ctx->socket_type = ZMQ_PAIR;
    ctx->input_pos = 0;
    ctx->seen_message_count = 0;
    ctx->salt = (uint32_t)time(NULL) ^ (uint32_t)getpid();
    ctx->message_sequence = 0;
    
    // Initialize pending queue
    init_pending_queue(ctx);
    
    // Allocate and initialize message queue
    ctx->incoming_queue = calloc(1, sizeof(ZMQMessageQueue));
    if (!ctx->incoming_queue) {
        LOG_ERROR("ZMQ Client Threaded: Failed to allocate message queue");
        free(ctx->endpoint);
        free(ctx);
        return NULL;
    }
    
    if (zmq_queue_init(ctx->incoming_queue, 100) != 0) {
        LOG_ERROR("ZMQ Client Threaded: Failed to initialize message queue");
        free(ctx->incoming_queue);
        free(ctx->endpoint);
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

/**
 * Clean up ZMQ client resources
 */
void zmq_client_cleanup(ZMQClientContextThreaded *ctx) {
    if (!ctx) return;
    
    // Stop if running
    if (ctx->running) {
        zmq_client_stop(ctx);
    }
    
    // Clean up ZMQ resources
    if (ctx->zmq_socket) {
        zmq_close(ctx->zmq_socket);
    }
    if (ctx->zmq_context) {
        zmq_ctx_destroy(ctx->zmq_context);
    }
    
    // Clean up message queue
    if (ctx->incoming_queue) {
        zmq_queue_destroy(ctx->incoming_queue);
        free(ctx->incoming_queue);
    }
    
    // Clean up pending queue and seen messages
    zmq_client_cleanup_pending_queue(ctx);
    
    // Free endpoint string
    if (ctx->endpoint) {
        free(ctx->endpoint);
    }
    
    free(ctx);
}

/**
 * Check if client is running
 */
bool zmq_client_is_running(ZMQClientContextThreaded *ctx) {
    return ctx && ctx->running;
}

/**
 * Get client statistics
 */
void zmq_client_get_stats(ZMQClientContextThreaded *ctx, uint64_t *messages_sent,
                                   uint64_t *messages_received, uint64_t *errors) {
    if (!ctx) return;
    
    if (messages_sent) *messages_sent = ctx->messages_sent;
    if (messages_received) *messages_received = ctx->messages_received;
    if (errors) *errors = ctx->errors;
}

/**
 * Stop ZMQ client
 */
void zmq_client_stop(ZMQClientContextThreaded *ctx) {
    if (!ctx) return;
    
    ctx->running = false;
    
    if (ctx->thread_started) {
        pthread_join(ctx->receiver_thread, NULL);
        ctx->thread_started = false;
    }
}

/**
 * Start ZMQ client
 */
int zmq_client_start(ZMQClientContextThreaded *ctx) {
    if (!ctx) {
        LOG_ERROR("ZMQ Client Threaded: Invalid context for start");
        return -1;
    }
    
    // Initialize ZMQ context
    ctx->zmq_context = zmq_ctx_new();
    if (!ctx->zmq_context) {
        LOG_ERROR("ZMQ Client Threaded: Failed to create ZMQ context");
        return -1;
    }
    
    // Create ZMQ socket
    ctx->zmq_socket = zmq_socket(ctx->zmq_context, ctx->socket_type);
    if (!ctx->zmq_socket) {
        LOG_ERROR("ZMQ Client Threaded: Failed to create ZMQ socket: %s", zmq_strerror(errno));
        zmq_ctx_destroy(ctx->zmq_context);
        ctx->zmq_context = NULL;
        return -1;
    }
    
    // Connect to socket
    if (zmq_connect(ctx->zmq_socket, ctx->endpoint) != 0) {
        LOG_ERROR("ZMQ Client Threaded: Failed to connect to %s: %s", ctx->endpoint, zmq_strerror(errno));
        zmq_close(ctx->zmq_socket);
        zmq_ctx_destroy(ctx->zmq_context);
        ctx->zmq_socket = NULL;
        ctx->zmq_context = NULL;
        return -1;
    }
    
    LOG_INFO("ZMQ Client Threaded: Connected to %s", ctx->endpoint);
    
    // Mark as running
    ctx->running = true;
    
    // Start receiver thread
    if (pthread_create(&ctx->receiver_thread, NULL, receiver_thread_func, ctx) != 0) {
        LOG_ERROR("ZMQ Client Threaded: Failed to create receiver thread");
        zmq_close(ctx->zmq_socket);
        zmq_ctx_destroy(ctx->zmq_context);
        ctx->zmq_socket = NULL;
        ctx->zmq_context = NULL;
        ctx->running = false;
        return -1;
    }
    
    ctx->thread_started = true;
    
    return 0;
}
