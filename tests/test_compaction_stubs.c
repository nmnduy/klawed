/*
 * Stub implementations for compaction test suite
 *
 * Provides minimal implementations of dependencies needed by compaction.c
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

#ifdef HAVE_MEMVID
// Memvid stubs for testing
int memvid_store(const char *entity, const char *slot, const char *value, const char *kind) {
    (void)entity;
    (void)slot;
    (void)value;
    (void)kind;
    // Stub always succeeds
    return 0;
}

char* memvid_recall(const char *entity, const char *slot) {
    (void)entity;
    (void)slot;
    return NULL;
}

char* memvid_search(const char *query, int top_k) {
    (void)query;
    (void)top_k;
    return NULL;
}
#endif
