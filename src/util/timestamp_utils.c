/*
 * Timestamp Utilities
 * Helper functions for timestamp generation and formatting
 */

#include "timestamp_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <bsd/stdlib.h>
#include <time.h>
#include <stdint.h>

/**
 * Get current timestamp in YYYY-MM-DD HH:MM:SS format
 */
void get_current_timestamp(char *buffer, size_t buffer_size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/**
 * Generate timestamped filename
 */
void generate_timestamped_filename(char *buffer, size_t buffer_size,
                                   const char *prefix, const char *mime_type) {
    if (!buffer || buffer_size == 0) return;

    // Get current time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    // Determine file extension from MIME type
    const char *extension = "bin";
    if (mime_type) {
        if (strcmp(mime_type, "image/png") == 0) {
            extension = "png";
        } else if (strcmp(mime_type, "image/jpeg") == 0 || strcmp(mime_type, "image/jpg") == 0) {
            extension = "jpg";
        } else if (strcmp(mime_type, "image/gif") == 0) {
            extension = "gif";
        } else if (strcmp(mime_type, "image/webp") == 0) {
            extension = "webp";
        } else if (strncmp(mime_type, "image/", 6) == 0) {
            // Generic image type
            extension = "img";
        }
    }

    // Generate filename
    snprintf(buffer, buffer_size, "%s_%04d%02d%02d_%02d%02d%02d.%s",
             prefix ? prefix : "file",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             extension);
}

/**
 * Get current date in YYYY-MM-DD format
 */
char* get_current_date(void) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    char *date = malloc(11); // "YYYY-MM-DD\0"
    if (!date) return NULL;

    strftime(date, 11, "%Y-%m-%d", tm_info);
    return date;
}

/**
 * Generate unique session ID
 * Format: 10-character random hex string (40 bits of entropy)
 */
char* generate_session_id(void) {
    // 10 hex chars + null = 11 bytes
    char *session_id = malloc(16);
    if (!session_id) {
        return NULL;
    }

    // Generate 40 bits (10 hex chars) of cryptographically secure randomness
    uint64_t random_val = ((uint64_t)arc4random() << 32) | arc4random();
    random_val &= 0xFFFFFFFFFFULL;  // Mask to 40 bits
    snprintf(session_id, 16, "%010llx", (unsigned long long)random_val);

    return session_id;
}
