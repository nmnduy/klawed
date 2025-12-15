/**
 * logger.h - Thread-safe file logging for TUI applications
 *
 * Usage:
 *   log_init();  // Initialize at startup
 *   LOG_INFO("Starting application");
 *   LOG_ERROR("Connection failed: %s", error_msg);
 *   log_shutdown();  // Cleanup at exit
 *
 * Environment Variables:
 *   CLAUDE_C_LOG_PATH - Full path to log file (e.g., /tmp/myapp.log)
 *   CLAUDE_C_LOG_DIR  - Directory for logs (uses claude.log as filename)
 *   CLAUDE_LOG_LEVEL  - Minimum log level: DEBUG, INFO, WARN, ERROR
 *   CLAUDE_LOG_FLUSH  - Flush mode: "buffered" (default) or "always"
 *                       "buffered" = better performance, "always" = immediate visibility
 *
 * Default Log Location Priority:
 *   1. $CLAUDE_C_LOG_PATH (if set)
 *   2. $CLAUDE_C_LOG_DIR/claude.log (if set)
 *   3. ./.claude-c/logs/claude.log (project-local, created automatically)
 *   4. ~/.local/share/claude-c/logs/claude.log
 *   5. /tmp/claude-c.log (fallback)
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

// Log levels
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

/**
 * Initialize the logging system
 * Creates log directory and opens log file
 * Returns 0 on success, -1 on failure
 */
int log_init(void);

/**
 * Initialize with custom log file path
 * Returns 0 on success, -1 on failure
 */
int log_init_with_path(const char *log_path);

/**
 * Set minimum log level (messages below this level are ignored)
 */
void log_set_level(LogLevel level);

/**
 * Configure log rotation
 * max_size_mb: Maximum log file size in megabytes before rotation (default: 10)
 * max_backups: Number of backup files to keep (default: 5)
 */
void log_set_rotation(int max_size_mb, int max_backups);

/**
 * Set the session ID for logging
 * All subsequent log messages will include this session ID
 */
void log_set_session_id(const char *session_id);

/**
 * Configure log flushing behavior
 * always_flush: If 1, flush after every log message (fallback for debugging)
 *                If 0, rely on stdio buffering (default: better performance)
 */
void log_set_flush_mode(int always_flush);

/**
 * Core logging function (use macros instead of calling directly)
 */
void log_message(LogLevel level, const char *file, int line,
                const char *func, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

/**
 * Flush log buffer to disk
 */
void log_flush(void);

/**
 * Close log file and cleanup
 */
void log_shutdown(void);

// Convenience macros that automatically include file/line/function info
#define LOG_DEBUG(...) log_message(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(...)  log_message(LOG_LEVEL_INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN(...)  log_message(LOG_LEVEL_WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(...) log_message(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

#endif // LOGGER_H
