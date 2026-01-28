/*
 * Stub implementations for compaction test suite
 *
 * Provides minimal implementations of dependencies needed by compaction.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include "../src/logger.h"
#include "../src/http_client.h"
#include "../src/persistence.h"

#ifdef HAVE_MEMVID
#include "../src/memvid.h"
#endif

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

// HTTP client stubs for testing
HttpResponse* http_client_execute(const HttpRequest *req,
                                 HttpProgressCallback progress_cb,
                                 void *progress_data) {
    (void)req;
    (void)progress_cb;
    (void)progress_data;
    // Return NULL to simulate network failure - summarization will use fallback
    return NULL;
}

void http_response_free(HttpResponse *resp) {
    if (resp) {
        free(resp->body);
        free(resp->error_message);
        free(resp);
    }
}

// Persistence stub
int persistence_get_last_prompt_tokens(PersistenceDB *db, const char *session_id, int *tokens_out) {
    (void)db;
    (void)session_id;
    if (tokens_out) {
        *tokens_out = 0;
    }
    return -1;  // Simulate no data available
}

#ifdef HAVE_MEMVID
// Memvid stub for get_global
MemvidHandle* memvid_get_global(void) {
    // Return a non-NULL value so compaction thinks memvid is available
    static int dummy_handle = 1;
    return (MemvidHandle*)&dummy_handle;
}

// Memvid stub for put_memory
int64_t memvid_put_memory(MemvidHandle *handle, const char *entity, const char *slot,
                          const char *value, uint8_t kind, uint8_t relation) {
    (void)handle;
    (void)entity;
    (void)slot;
    (void)value;
    (void)kind;
    (void)relation;
    // Return a valid card_id
    return 1;
}
#endif
