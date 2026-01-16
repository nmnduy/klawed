#include "tool_registry.h"
#include "tool_bash.h"
#include "tool_filesystem.h"
#include "tool_search.h"
#include "tool_subagent.h"
#include "tool_image.h"
#include "tool_sleep.h"
#include "tool_todo.h"
#include "../explore_tools.h"
#include "../klawed_internal.h"

// Forward declarations are already in klawed_internal.h

// Wrapper functions for explore tools (they use void* for state)
static cJSON* tool_web_search_wrapper(cJSON *params, ConversationState *state) {
    return tool_web_search(params, state);
}

static cJSON* tool_web_read_wrapper(cJSON *params, ConversationState *state) {
    return tool_web_read(params, state);
}

static cJSON* tool_context7_search_wrapper(cJSON *params, ConversationState *state) {
    return tool_context7_search(params, state);
}

static cJSON* tool_context7_docs_wrapper(cJSON *params, ConversationState *state) {
    return tool_context7_docs(params, state);
}

// Tool registry - all available tools
static Tool tools[] = {
    {"Sleep", tool_sleep},
    {"Bash", tool_bash},
    {"Subagent", tool_subagent},
    {"CheckSubagentProgress", tool_check_subagent_progress},
    {"InterruptSubagent", tool_interrupt_subagent},
    {"Read", tool_read},
    {"Write", tool_write},
    {"Edit", tool_edit},
    {"MultiEdit", tool_multiedit},
    {"Glob", tool_glob},
    {"Grep", tool_grep},
    {"TodoWrite", tool_todo_write},
    {"UploadImage", tool_upload_image},
    // Memory tools (always registered, but return error if HAVE_MEMVID not defined)
    {"MemoryStore", tool_memory_store},
    {"MemoryRecall", tool_memory_recall},
    {"MemorySearch", tool_memory_search},
    // Explore tools (only work when KLAWED_EXPLORE_MODE=1)
    {"web_search", tool_web_search_wrapper},
    {"web_read", tool_web_read_wrapper},
    {"context7_search", tool_context7_search_wrapper},
    {"context7_docs", tool_context7_docs_wrapper},
#ifndef TEST_BUILD
    {"ListMcpResources", tool_list_mcp_resources},
    {"ReadMcpResource", tool_read_mcp_resource},
    {"CallMcpTool", tool_call_mcp_tool},
#endif
};

static const int num_tools = sizeof(tools) / sizeof(Tool);

const Tool* get_registered_tools(void) {
    return tools;
}

int get_num_registered_tools(void) {
    return num_tools;
}

const Tool* find_tool_by_name(const char *name) {
    if (!name) {
        return NULL;
    }

    for (int i = 0; i < num_tools; i++) {
        if (strcmp(tools[i].name, name) == 0) {
            return &tools[i];
        }
    }

    return NULL;
}
