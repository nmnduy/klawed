/*
 * Environment Utilities
 * Helper functions for environment variables and system information
 */

#include "env_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifndef TEST_BUILD
#include "../logger.h"
#else
// Test build stubs
#define LOG_WARN(fmt, ...) ((void)0)
#endif

/**
 * Get integer value from environment variable with retry and validation
 */
int get_env_int_retry(const char *name, int default_value) {
    const char *value = getenv(name);
    if (!value || value[0] == '\0') {
        return default_value;
    }

    char *endptr;
    long result = strtol(value, &endptr, 10);
    if (*endptr != '\0' || result < 0 || result > INT_MAX) {
        LOG_WARN("Invalid value for %s: '%s', using default %d", name, value, default_value);
        return default_value;
    }

    return (int)result;
}

/**
 * Get platform identifier
 */
const char* get_platform(void) {
#ifdef __APPLE__
    return "darwin";
#elif defined(__linux__)
    return "linux";
#elif defined(_WIN32) || defined(_WIN64)
    return "win32";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#else
    return "unknown";
#endif
}

/**
 * Execute shell command and return output
 */
char* exec_shell_command(const char *command) {
    FILE *fp = popen(command, "r");
    if (!fp) return NULL;

    char *output = NULL;
    size_t output_size = 0;
    char buffer[1024];

    while (fgets(buffer, sizeof(buffer), fp)) {
        size_t len = strlen(buffer);
        char *new_output = realloc(output, output_size + len + 1);
        if (!new_output) {
            free(output);
            pclose(fp);
            return NULL;
        }
        output = new_output;
        memcpy(output + output_size, buffer, len);
        output_size += len;
        output[output_size] = '\0';
    }

    pclose(fp);

    // Trim trailing newline
    if (output && output_size > 0 && output[output_size-1] == '\n') {
        output[output_size-1] = '\0';
    }

    return output;
}

/**
 * Get OS version string
 */
char* get_os_version(void) {
    char *os_version = exec_shell_command("uname -sr 2>/dev/null");
    if (!os_version) {
        os_version = strdup("Unknown");
    }
    return os_version;
}
