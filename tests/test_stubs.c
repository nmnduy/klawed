/**
 * test_stubs.c - Stub implementations for test programs
 */

#include "test_stubs.h"
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

// Include logger.h to get proper LogLevel and log_message signatures
#include "../src/logger.h"

// Define globals before including colorscheme.h
#include "../src/colorscheme.h"

// Global theme variables are now defined via the header

// Stubs for tool functions needed by patch_parser
char* read_file(const char *path) {
    (void)path;
    return NULL;
}

int write_file(const char *path, const char *content) {
    (void)path;
    (void)content;
    return -1;
}

char* resolve_path(const char *path, const char *working_dir) {
    (void)working_dir;
    if (!path) return NULL;
    return strdup(path);
}

// Stubs for tool_definitions.c dependencies (TEST_BUILD mode)
int is_web_agent_available(void) {
    return 0;
}

int is_explore_mode_enabled(void) {
    return 0;
}

const char* explore_tool_web_search_schema(void) {
    static const char *empty_schema = "{}";
    return empty_schema;
}

const char* explore_tool_web_read_schema(void) {
    static const char *empty_schema = "{}";
    return empty_schema;
}

int mcp_is_enabled(void) {
    return 0;
}

void* mcp_get_all_tools(void *config) {
    (void)config;
    return cJSON_CreateArray();
}

void add_cache_control(void *obj) {
    (void)obj;
}

// Stub for tool_utils functions
int is_tool_disabled(const char *tool_name) {
    (void)tool_name;
    return 0;
}
