/*
 * zmq_config.h - ZMQ configuration constants and defaults
 */

#ifndef ZMQ_CONFIG_H
#define ZMQ_CONFIG_H

#include <stdlib.h>
#include <time.h>

// Default timeout values (in milliseconds)
#define ZMQ_DEFAULT_RECEIVE_TIMEOUT 30000     // 30 seconds
#define ZMQ_DEFAULT_SEND_TIMEOUT 10000        // 10 seconds
#define ZMQ_DEFAULT_CONNECT_TIMEOUT 5000      // 5 seconds
#define ZMQ_DEFAULT_HEARTBEAT_INTERVAL 5000   // 5 seconds
#define ZMQ_DEFAULT_RECONNECT_INTERVAL 1000   // 1 second
#define ZMQ_DEFAULT_MAX_RECONNECT_ATTEMPTS 10

// Buffer sizes
#define ZMQ_DEFAULT_BUFFER_SIZE 65536
#define ZMQ_MAX_MESSAGE_SIZE 1048576          // 1MB max message size

// Queue sizes
#define ZMQ_DEFAULT_SEND_QUEUE_SIZE 100
#define ZMQ_DEFAULT_RECEIVE_QUEUE_SIZE 100

// Environment variable names
#define ZMQ_ENV_RECEIVE_TIMEOUT "KLAWED_ZMQ_RECEIVE_TIMEOUT"
#define ZMQ_ENV_SEND_TIMEOUT "KLAWED_ZMQ_SEND_TIMEOUT"
#define ZMQ_ENV_CONNECT_TIMEOUT "KLAWED_ZMQ_CONNECT_TIMEOUT"
#define ZMQ_ENV_HEARTBEAT_INTERVAL "KLAWED_ZMQ_HEARTBEAT_INTERVAL"
#define ZMQ_ENV_RECONNECT_INTERVAL "KLAWED_ZMQ_RECONNECT_INTERVAL"
#define ZMQ_ENV_MAX_RECONNECT_ATTEMPTS "KLAWED_ZMQ_MAX_RECONNECT_ATTEMPTS"
#define ZMQ_ENV_BUFFER_SIZE "KLAWED_ZMQ_BUFFER_SIZE"
#define ZMQ_ENV_MAX_MESSAGE_SIZE "KLAWED_ZMQ_MAX_MESSAGE_SIZE"
#define ZMQ_ENV_SEND_QUEUE_SIZE "KLAWED_ZMQ_SEND_QUEUE_SIZE"
#define ZMQ_ENV_RECEIVE_QUEUE_SIZE "KLAWED_ZMQ_RECEIVE_QUEUE_SIZE"
#define ZMQ_ENV_ENABLE_HEARTBEAT "KLAWED_ZMQ_ENABLE_HEARTBEAT"
#define ZMQ_ENV_ENABLE_RECONNECT "KLAWED_ZMQ_ENABLE_RECONNECT"

// Helper function to get timeout value from environment with fallback
static inline int zmq_get_timeout_from_env(const char *env_var, int default_value) {
    const char *env_value = getenv(env_var);
    if (env_value) {
        char *endptr;
        long value = strtol(env_value, &endptr, 10);
        if (endptr != env_value && *endptr == '\0' && value >= 0) {
            return (int)value;
        }
    }
    return default_value;
}

#endif // ZMQ_CONFIG_H
