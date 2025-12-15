/**
 * test_stubs.c - Stub implementations for test programs
 */

#include "test_stubs.h"
#include <stdlib.h>
#include <string.h>

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
