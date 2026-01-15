/*
 * Format Utilities
 * Helper functions for formatting values
 */

#include "format_utils.h"
#include <stdio.h>

/**
 * Format file size in human-readable format
 * Returns: Static buffer with formatted size (not thread-safe)
 */
const char* format_file_size(size_t size) {
    static char buffer[32];

    if (size < 1024) {
        snprintf(buffer, sizeof(buffer), "%zu B", size);
    } else if (size < 1024 * 1024) {
        snprintf(buffer, sizeof(buffer), "%.1f KB", (double)size / 1024);
    } else if (size < 1024 * 1024 * 1024) {
        snprintf(buffer, sizeof(buffer), "%.1f MB", (double)size / (1024 * 1024));
    } else {
        snprintf(buffer, sizeof(buffer), "%.1f GB", (double)size / (1024 * 1024 * 1024));
    }

    return buffer;
}
