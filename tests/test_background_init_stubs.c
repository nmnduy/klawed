/*
 * Stub implementations for background_init test suite
 *
 * Provides minimal implementations of dependencies needed by background_init.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "../src/logger.h"
#include "../src/persistence.h"
#include "../src/memory_db.h"
#include "../src/klawed_internal.h"
#include "../src/data_dir.h"

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

// Data directory stubs
int data_dir_is_no_storage_mode(void) {
    return 0;  // Storage enabled by default
}

// Persistence stubs
PersistenceDB* persistence_init(const char *path) {
    (void)path;
    return NULL;
}

void persistence_close(PersistenceDB *db) {
    (void)db;
}

// Memory database stubs
int memory_db_init_global(const char *path) {
    (void)path;
    return 0;  // Success
}

// System prompt stub
char* build_system_prompt(ConversationState *state) {
    (void)state;
    return strdup("Test system prompt");
}

// Conversation state lock/unlock stubs
int conversation_state_lock(ConversationState *state) {
    (void)state;
    return 0;  // Success
}

void conversation_state_unlock(ConversationState *state) {
    (void)state;
}
