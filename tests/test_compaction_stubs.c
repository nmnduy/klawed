/*
 * Stub implementations for compaction test suite
 *
 * Provides minimal implementations of dependencies needed by compaction.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "../src/logger.h"
#include "../src/http_client.h"
#include "../src/persistence.h"
#include "../src/memory_db.h"
#include "../src/klawed_internal.h"
#include "../src/provider.h"

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

// ============================================================================
// Memory DB stubs for testing
// ============================================================================

// MemoryDB is opaque, so we just use a static pointer
static char g_test_memory_db_placeholder = 1;

MemoryDB* memory_db_get_global(void) {
    // Return a non-NULL value so compaction thinks memory db is available
    return (MemoryDB*)&g_test_memory_db_placeholder;
}

int memory_db_is_available(void) {
    return 1;  // Always available in tests
}

int64_t memory_db_store(MemoryDB *db, const char *entity,
                        const char *slot, const char *value,
                        MemoryKind kind, MemoryRelation relation) {
    (void)db;
    (void)entity;
    (void)slot;
    (void)value;
    (void)kind;
    (void)relation;
    // Return a valid card_id
    return 1;
}

// Stub for provider (needed by compaction_generate_summary)
// This is used via an extern declaration in the compaction code
Provider* provider_get_current(ConversationState *state);
Provider* provider_get_current(ConversationState *state) {
    (void)state;
    return NULL;
}

// Stub for call_api_with_retries - simulate failure so compaction uses fallback
ApiResponse* call_api_with_retries(ConversationState *state) {
    (void)state;
    // Return NULL to trigger fallback summary
    return NULL;
}

// Stub for api_response_free
void api_response_free(ApiResponse *resp) {
    if (!resp) return;
    // Free any dynamically allocated fields
    free(resp->error_message);
    if (resp->tools) {
        for (int i = 0; i < resp->tool_count; i++) {
            free(resp->tools[i].id);
            free(resp->tools[i].name);
            if (resp->tools[i].parameters) {
                cJSON_Delete(resp->tools[i].parameters);
            }
        }
        free(resp->tools);
    }
    free(resp->message.text);
    if (resp->raw_response) {
        cJSON_Delete(resp->raw_response);
    }
    free(resp);
}
