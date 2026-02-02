/*
 * test_stubs.c - Stub implementations for manual API tests
 */

#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

// Stub for subagent_manager
void* subagent_manager_init(void) { return NULL; }
void subagent_manager_free(void *mgr) { (void)mgr; }
void register_subagent_manager_for_cleanup(void *mgr) { (void)mgr; }
int subagent_manager_add(void *mgr, void *process) { (void)mgr; (void)process; return 0; }

// Stub for todo
void* todo_init(void) { return NULL; }
void todo_free(void *todo) { (void)todo; }
void todo_clear(void *todo) { (void)todo; }
int todo_add(void *todo, const char *text) { (void)todo; (void)text; return 0; }
char* todo_render_to_string(void *todo) { (void)todo; return strdup("TODO list"); }

// Stub for explore mode tools
int is_explore_mode_enabled(void) { return 0; }
int is_web_agent_available(void) { return 0; }
const char* explore_tool_web_search_schema(void) { return "{}"; }
const char* explore_tool_web_read_schema(void) { return "{}"; }
const char* explore_tool_context7_search_schema(void) { return "{}"; }
const char* explore_tool_context7_docs_schema(void) { return "{}"; }

// Stub for web tools
char* tool_web_search(const char *query) { (void)query; return NULL; }
char* tool_web_read(const char *url) { (void)url; return NULL; }
char* tool_context7_search(const char *query) { (void)query; return NULL; }
char* tool_context7_docs(const char *id) { (void)id; return NULL; }

// Stub for memory tools
char* tool_memory_store(const char *entity, const char *kind, const char *slot, const char *value) {
    (void)entity; (void)kind; (void)slot; (void)value;
    return NULL;
}
char* tool_memory_recall(const char *entity, const char *slot) {
    (void)entity; (void)slot;
    return NULL;
}
char* tool_memory_search(const char *query) {
    (void)query;
    return NULL;
}

// Global theme variable
typedef struct { int dummy; } ThemeType;
ThemeType g_theme = {0};
int g_theme_loaded = 0;

// Global tool queue - must match the TLS definition in klawed.c
typedef struct TUIMessageQueue { int dummy; } TUIMessageQueue;
_Thread_local TUIMessageQueue *g_active_tool_queue = NULL;

// Stub for SQLite queue
int sqlite_queue_send(void *ctx, const char *msg) { (void)ctx; (void)msg; return 0; }
int sqlite_queue_send_compaction_notice(void *ctx) { (void)ctx; return 0; }

// Stub for system prompt
void* await_system_prompt_ready(void *state) { (void)state; return NULL; }

// Stub for compaction
int compaction_should_trigger(const void *state, const void *config) { (void)state; (void)config; return 0; }
int compaction_perform(void *state, void *config, const char *session_id, void *result) {
    (void)state; (void)config; (void)session_id; (void)result;
    return 0;
}
int compaction_update_token_count(const void *state, void *config) { (void)state; (void)config; return 0; }

// Stub for persistence
void persistence_log_api_call(void *db, const char *call, const char *response, long status) {
    (void)db; (void)call; (void)response; (void)status;
}

// Stub for TUI message posting
void post_tui_message(const char *msg) { (void)msg; }
void tui_update_status(void *tui, const char *status) { (void)tui; (void)status; }

// Stub for theme
const char* get_builtin_theme_content(const char *name) { (void)name; return NULL; }
