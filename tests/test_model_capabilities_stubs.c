/*
 * Stub implementations for model_capabilities test suite
 *
 * Provides minimal implementations of dependencies needed by model_capabilities.c
 */

#include <stdio.h>
#include <stdarg.h>
#include "../src/logger.h"

// Logger stub - matches signature from logger.h
void log_message(LogLevel level, const char *file, int line,
                const char *func, const char *fmt, ...) {
    (void)level;
    (void)file;
    (void)line;
    (void)func;
    (void)fmt;
    // Suppress log output in tests
}
