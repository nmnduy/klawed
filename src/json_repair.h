/*
 * json_repair.h - Simple JSON repair utilities
 */

#ifndef JSON_REPAIR_H
#define JSON_REPAIR_H

#include <stddef.h>

/**
 * Try to repair truncated JSON string
 * 
 * @param json The truncated JSON string
 * @param max_len Maximum length for repaired JSON (including null terminator)
 * @param repaired Buffer to store repaired JSON (must be at least max_len bytes)
 * @return 1 if repair attempted, 0 if JSON appears valid or can't be repaired
 */
int repair_truncated_json(const char *json, size_t max_len, char *repaired);

/**
 * Check if JSON appears to be a truncated Write tool arguments
 * 
 * @param json The JSON string to check
 * @return 1 if it looks like truncated Write tool args, 0 otherwise
 */
int is_truncated_write_args(const char *json);

#endif // JSON_REPAIR_H
