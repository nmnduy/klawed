/*
 * zmq_message_id.h - Message ID generation and tracking for ZMQ communication
 *
 * Provides reliable message delivery with ACK and retry mechanisms.
 * Uses timestamp + partial content hash + random salt for unique message IDs.
 */

#ifndef ZMQ_MESSAGE_ID_H
#define ZMQ_MESSAGE_ID_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef HAVE_ZMQ
#include <cjson/cJSON.h>
#endif

// Message ID length (hex string representation)
#define ZMQ_MESSAGE_ID_LENGTH 32

// Maximum retry attempts for unacknowledged messages
#define ZMQ_MAX_RETRY_ATTEMPTS 3

// Retry delay in milliseconds
#define ZMQ_RETRY_DELAY_MS 1000

// Time-sharing constants
#define ZMQ_TIME_SHARING_INTERVAL_MS 100  // Check both input and receive every 100ms

// Message types with ACK support
typedef enum {
    ZMQ_MSG_TYPE_TEXT = 0,
    ZMQ_MSG_TYPE_ERROR,
    ZMQ_MSG_TYPE_TOOL,
    ZMQ_MSG_TYPE_TOOL_RESULT,
    ZMQ_MSG_TYPE_ACK,           // Acknowledgment message
    ZMQ_MSG_TYPE_NACK           // Negative acknowledgment (error in processing)
} ZMQMessageType;

// Message tracking structure
typedef struct {
    char message_id[ZMQ_MESSAGE_ID_LENGTH + 1];  // Null-terminated message ID
    time_t timestamp;                            // When message was sent
    int retry_count;                             // Number of retry attempts
    int acknowledged;                            // Whether ACK received
    char *message_data;                          // Original message data (for retry)
    size_t message_len;                          // Length of message data
} ZMQMessageTracker;

// Message ID generation context
typedef struct {
    ZMQMessageTracker *pending_messages;  // Array of pending messages
    size_t pending_count;                 // Number of pending messages
    size_t pending_capacity;              // Capacity of pending array
    uint32_t random_seed;                 // Random seed for salt generation
} ZMQMessageContext;

/**
 * Generate a unique message ID from timestamp, partial content, and random salt
 * @param content Message content (can be NULL for empty messages)
 * @param content_len Length of content (0 for empty messages)
 * @param buffer Buffer to store the message ID (must be at least ZMQ_MESSAGE_ID_LENGTH+1 bytes)
 * @return 0 on success, -1 on failure
 */
int zmq_generate_message_id(const char *content, size_t content_len, char *buffer);

/**
 * Initialize message tracking context
 * @return New message context or NULL on failure
 */
ZMQMessageContext* zmq_message_context_init(void);

/**
 * Clean up message tracking context
 * @param ctx Message context to clean up
 */
void zmq_message_context_cleanup(ZMQMessageContext *ctx);

/**
 * Add a message to the tracking context
 * @param ctx Message context
 * @param message_id Message ID to track
 * @param message_data Message data (will be copied)
 * @param message_len Length of message data
 * @return 0 on success, -1 on failure
 */
int zmq_track_message(ZMQMessageContext *ctx, const char *message_id,
                      const char *message_data, size_t message_len);

/**
 * Mark a message as acknowledged
 * @param ctx Message context
 * @param message_id Message ID to acknowledge
 * @return 0 if found and acknowledged, -1 if not found
 */
int zmq_acknowledge_message(ZMQMessageContext *ctx, const char *message_id);

/**
 * Check for unacknowledged messages that need retry
 * @param ctx Message context
 * @param current_time Current time for timeout calculation
 * @return Number of messages needing retry
 */
int zmq_check_retry_messages(ZMQMessageContext *ctx, time_t current_time);

/**
 * Get the next message that needs retry
 * @param ctx Message context
 * @param message_data Output: pointer to message data (do not free)
 * @param message_len Output: length of message data
 * @return Message ID or NULL if no messages need retry
 */
const char* zmq_get_next_retry_message(ZMQMessageContext *ctx,
                                       const char **message_data, size_t *message_len);

/**
 * Create a JSON message with message ID field
 * @param message_type Message type (TEXT, ERROR, TOOL, TOOL_RESULT, ACK, NACK)
 * @param content Message content (can be NULL)
 * @param message_id Message ID (if NULL, will be generated)
 * @return JSON string (must be freed by caller) or NULL on failure
 */
char* zmq_create_message_with_id(ZMQMessageType message_type, const char *content,
                                 const char *message_id);

/**
 * Parse a JSON message and extract message ID
 * @param json_str JSON message string
 * @param message_id Output buffer for message ID (must be at least ZMQ_MESSAGE_ID_LENGTH+1 bytes)
 * @param message_type Output: message type
 * @param content Output: pointer to content within JSON (do not free)
 * @return 0 on success, -1 on failure
 */
int zmq_parse_message_with_id(const char *json_str, char *message_id,
                              ZMQMessageType *message_type, const char **content);

/**
 * Create an ACK message for a given message ID
 * @param message_id Message ID to acknowledge
 * @return JSON ACK message (must be freed by caller) or NULL on failure
 */
char* zmq_create_ack_message(const char *message_id);

/**
 * Create a NACK message for a given message ID
 * @param message_id Message ID to negatively acknowledge
 * @param error_reason Reason for NACK
 * @return JSON NACK message (must be freed by caller) or NULL on failure
 */
char* zmq_create_nack_message(const char *message_id, const char *error_reason);

#endif // ZMQ_MESSAGE_ID_H