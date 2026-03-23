/*
 * Tool Definitions - API tool schema definitions for Klawed
 *
 * This file contains all tool schema definitions in OpenAI-compatible format.
 * Tool selection is based on:
 * - plan_mode: Excludes write-capable tools
 * - is_subagent: Excludes Subagent tools to prevent recursion
 * - KLAWED_DISABLE_TOOLS: Environment variable for disabling specific tools
 * - MCP enabled: Includes MCP resource and dynamic tools
 * - Explore mode: Includes web search and Context7 tools
 */

#include "tool_definitions.h"
#include "../klawed_internal.h"
#include "../tool_utils.h"
#include "../logger.h"
#include "../api/api_builder.h"
#include "../explore_tools.h"
#include "../dynamic_tools.h"

#ifndef TEST_BUILD
#include "../mcp.h"
#endif

#include <string.h>
#include <strings.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Tool Duplicate Detection
// ---------------------------------------------------------------------------

/**
 * Check for duplicate tool names in the tool array
 * Returns: Pointer to duplicate tool name if found, NULL otherwise
 * Note: Returned pointer is to the name field in the JSON, caller must NOT free
 */
static int is_subagent_process(void) {
    const char *is_subagent_env = getenv("KLAWED_IS_SUBAGENT");
    return is_subagent_env && (strcmp(is_subagent_env, "1") == 0 ||
                               strcasecmp(is_subagent_env, "true") == 0 ||
                               strcasecmp(is_subagent_env, "yes") == 0);
}

static void add_function_tool(cJSON *tool_array, ToolSchemaFormat format,
                              const char *name, const char *description,
                              cJSON *parameters) {
    cJSON *tool = cJSON_CreateObject();
    if (!tool) {
        if (parameters) {
            cJSON_Delete(parameters);
        }
        return;
    }

    cJSON_AddStringToObject(tool, "type", "function");
    if (format == TOOL_SCHEMA_RESPONSES) {
        cJSON_AddStringToObject(tool, "name", name);
        cJSON_AddStringToObject(tool, "description", description);
        cJSON_AddItemToObject(tool, "parameters", parameters);
    } else {
        cJSON *func = cJSON_CreateObject();
        if (!func) {
            cJSON_Delete(tool);
            if (parameters) {
                cJSON_Delete(parameters);
            }
            return;
        }
        cJSON_AddStringToObject(func, "name", name);
        cJSON_AddStringToObject(func, "description", description);
        cJSON_AddItemToObject(func, "parameters", parameters);
        cJSON_AddItemToObject(tool, "function", func);
    }
    cJSON_AddItemToArray(tool_array, tool);
}

cJSON* get_openai_subscription_tool_definitions(int enable_caching,
                                                ToolSchemaFormat format) {
    (void)enable_caching;

    cJSON *tool_array = cJSON_CreateArray();
    cJSON *params = NULL;
    cJSON *props = NULL;
    cJSON *item = NULL;
    int is_subagent = is_subagent_process();

    if (!tool_array) {
        return NULL;
    }

    if (!is_tool_disabled("Bash")) {
        params = cJSON_CreateObject();
        props = cJSON_CreateObject();
        if (!params || !props) {
            cJSON_Delete(params);
            cJSON_Delete(props);
            cJSON_Delete(tool_array);
            return NULL;
        }
        cJSON_AddStringToObject(params, "type", "object");

        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "string");
        cJSON_AddStringToObject(item, "description", "The command to execute");
        cJSON_AddItemToObject(props, "command", item);

        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "integer");
        cJSON_AddStringToObject(item, "description", "Optional: Timeout in seconds. Default: 30 (from KLAWED_BASH_TIMEOUT env var). Set to 0 for no timeout.");
        cJSON_AddItemToObject(props, "timeout", item);

        cJSON_AddItemToObject(params, "properties", props);
        item = cJSON_CreateArray();
        cJSON_AddItemToArray(item, cJSON_CreateString("command"));
        cJSON_AddItemToObject(params, "required", item);
        add_function_tool(tool_array, format, "Bash",
                          "Executes bash commands. Note: stderr is automatically redirected to stdout to prevent terminal corruption, so both stdout and stderr output will be captured in the 'output' field. Commands have a configurable timeout (default: 30 seconds) to prevent hanging. Use the 'timeout' parameter to override the default or set to 0 for no timeout.",
                          params);
    } else {
        LOG_INFO("Tool 'Bash' is disabled via KLAWED_DISABLE_TOOLS");
    }

    if (!is_subagent && !is_tool_disabled("Subagent")) {
        params = cJSON_CreateObject();
        props = cJSON_CreateObject();
        if (!params || !props) {
            cJSON_Delete(params);
            cJSON_Delete(props);
            cJSON_Delete(tool_array);
            return NULL;
        }
        cJSON_AddStringToObject(params, "type", "object");

        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "string");
        cJSON_AddStringToObject(item, "description", "The task prompt for the subagent. Be specific and include all necessary context.");
        cJSON_AddItemToObject(props, "prompt", item);

        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "integer");
        cJSON_AddStringToObject(item, "description", "Optional: Timeout in seconds. Default: 300 (5 minutes). Set to 0 for no timeout.");
        cJSON_AddItemToObject(props, "timeout", item);

        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "integer");
        cJSON_AddStringToObject(item, "description", "Optional: Number of lines to return from end of log. Default: 100.");
        cJSON_AddItemToObject(props, "tail_lines", item);

        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "string");
        cJSON_AddStringToObject(item, "description", "Optional: LLM provider name to use for this subagent.");
        cJSON_AddItemToObject(props, "provider", item);

        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "string");
        cJSON_AddStringToObject(item, "description", "Optional: Working directory for the subagent. Must be an absolute path.");
        cJSON_AddItemToObject(props, "working_dir", item);

        cJSON_AddItemToObject(params, "properties", props);
        item = cJSON_CreateArray();
        cJSON_AddItemToArray(item, cJSON_CreateString("prompt"));
        cJSON_AddItemToObject(params, "required", item);
        add_function_tool(tool_array, format, "Subagent",
                          "Spawns a new instance of klawed with the same configuration to work on a delegated task in a fresh context. The subagent runs independently and writes all output to a log file.",
                          params);
    } else if (!is_subagent) {
        LOG_INFO("Tool 'Subagent' is disabled via KLAWED_DISABLE_TOOLS");
    }

    if (!is_subagent && !is_tool_disabled("CheckSubagentProgress")) {
        params = cJSON_CreateObject();
        props = cJSON_CreateObject();
        if (!params || !props) {
            cJSON_Delete(params);
            cJSON_Delete(props);
            cJSON_Delete(tool_array);
            return NULL;
        }
        cJSON_AddStringToObject(params, "type", "object");

        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "integer");
        cJSON_AddStringToObject(item, "description", "Process ID of the subagent (from Subagent tool response)");
        cJSON_AddItemToObject(props, "pid", item);

        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "string");
        cJSON_AddStringToObject(item, "description", "Path to subagent log file (from Subagent tool response)");
        cJSON_AddItemToObject(props, "log_file", item);

        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "integer");
        cJSON_AddStringToObject(item, "description", "Optional: Number of lines to read from end of log (default: 50)");
        cJSON_AddItemToObject(props, "tail_lines", item);

        cJSON_AddItemToObject(params, "properties", props);
        item = cJSON_CreateArray();
        cJSON_AddItemToArray(item, cJSON_CreateString("pid"));
        cJSON_AddItemToArray(item, cJSON_CreateString("log_file"));
        cJSON_AddItemToObject(params, "required", item);
        add_function_tool(tool_array, format, "CheckSubagentProgress",
                          "Checks the progress of a running subagent by reading its log file.",
                          params);
    } else if (!is_subagent) {
        LOG_INFO("Tool 'CheckSubagentProgress' is disabled via KLAWED_DISABLE_TOOLS");
    }

    if (!is_subagent && !is_tool_disabled("InterruptSubagent")) {
        params = cJSON_CreateObject();
        props = cJSON_CreateObject();
        if (!params || !props) {
            cJSON_Delete(params);
            cJSON_Delete(props);
            cJSON_Delete(tool_array);
            return NULL;
        }
        cJSON_AddStringToObject(params, "type", "object");

        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "integer");
        cJSON_AddStringToObject(item, "description", "Process ID of the subagent to interrupt (from Subagent tool response)");
        cJSON_AddItemToObject(props, "pid", item);

        cJSON_AddItemToObject(params, "properties", props);
        item = cJSON_CreateArray();
        cJSON_AddItemToArray(item, cJSON_CreateString("pid"));
        cJSON_AddItemToObject(params, "required", item);
        add_function_tool(tool_array, format, "InterruptSubagent",
                          "Interrupts and stops a running subagent. Use this to cancel a subagent that is stuck, taking too long, or no longer needed.",
                          params);
    } else if (!is_subagent) {
        LOG_INFO("Tool 'InterruptSubagent' is disabled via KLAWED_DISABLE_TOOLS");
    }

    return tool_array;
}
const char* detect_duplicate_tool_names(cJSON *tool_array) {
    if (!tool_array || !cJSON_IsArray(tool_array)) {
        return NULL;
    }

    cJSON *tool = NULL;
    // First pass: collect all tool names
    const char **names = NULL;
    int count = 0;
    int capacity = 0;

    cJSON_ArrayForEach(tool, tool_array) {
        // Get tool name - format is { type: "function", function: { name: "...", ... } }
        cJSON *func = cJSON_GetObjectItem(tool, "function");
        cJSON *name_obj = func ? cJSON_GetObjectItem(func, "name") : NULL;

        // Alternative format (simplified): { type: "function", name: "..." }
        if (!name_obj) {
            name_obj = cJSON_GetObjectItem(tool, "name");
        }

        if (!name_obj || !cJSON_IsString(name_obj)) {
            continue;
        }

        const char *tool_name = name_obj->valuestring;

        // Check against previously seen names
        for (int i = 0; i < count; i++) {
            if (strcmp(names[i], tool_name) == 0) {
                // Found duplicate - clean up and return
                free(names);
                return tool_name;
            }
        }

        // Add to tracking array
        if (count >= capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            const char **new_names = realloc(names, (size_t)capacity * sizeof(const char*));
            if (!new_names) {
                free(names);
                return NULL;
            }
            names = new_names;
        }

        // Store pointer to the name string in JSON (we don't own this)
        names[count++] = tool_name;
    }

    free(names);
    return NULL;
}

// ---------------------------------------------------------------------------
// Shared memory tool helpers
// ---------------------------------------------------------------------------

static cJSON* build_memory_store_params(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON *props = cJSON_CreateObject();

    cJSON *entity = cJSON_CreateObject();
    cJSON_AddStringToObject(entity, "type", "string");
    cJSON_AddStringToObject(entity, "description",
        "Who/what this is about (e.g., 'user', 'project.klawed', 'user.team')");
    cJSON_AddItemToObject(props, "entity", entity);

    cJSON *slot = cJSON_CreateObject();
    cJSON_AddStringToObject(slot, "type", "string");
    cJSON_AddStringToObject(slot, "description",
        "The attribute (e.g., 'employer', 'preferred_language', 'coding_style')");
    cJSON_AddItemToObject(props, "slot", slot);

    cJSON *value = cJSON_CreateObject();
    cJSON_AddStringToObject(value, "type", "string");
    cJSON_AddStringToObject(value, "description", "The value to store");
    cJSON_AddItemToObject(props, "value", value);

    cJSON *kind = cJSON_CreateObject();
    cJSON_AddStringToObject(kind, "type", "string");
    cJSON *kind_enum = cJSON_CreateArray();
    cJSON_AddItemToArray(kind_enum, cJSON_CreateString("fact"));
    cJSON_AddItemToArray(kind_enum, cJSON_CreateString("preference"));
    cJSON_AddItemToArray(kind_enum, cJSON_CreateString("event"));
    cJSON_AddItemToArray(kind_enum, cJSON_CreateString("profile"));
    cJSON_AddItemToArray(kind_enum, cJSON_CreateString("relationship"));
    cJSON_AddItemToArray(kind_enum, cJSON_CreateString("goal"));
    cJSON_AddItemToObject(kind, "enum", kind_enum);
    cJSON_AddStringToObject(kind, "description", "Type of memory");
    cJSON_AddItemToObject(props, "kind", kind);

    cJSON *relation = cJSON_CreateObject();
    cJSON_AddStringToObject(relation, "type", "string");
    cJSON *relation_enum = cJSON_CreateArray();
    cJSON_AddItemToArray(relation_enum, cJSON_CreateString("sets"));
    cJSON_AddItemToArray(relation_enum, cJSON_CreateString("updates"));
    cJSON_AddItemToArray(relation_enum, cJSON_CreateString("extends"));
    cJSON_AddItemToArray(relation_enum, cJSON_CreateString("retracts"));
    cJSON_AddItemToObject(relation, "enum", relation_enum);
    cJSON_AddStringToObject(relation, "description",
        "How this relates to existing values (default: sets)");
    cJSON_AddItemToObject(props, "relation", relation);

    cJSON *memory_file = cJSON_CreateObject();
    cJSON_AddStringToObject(memory_file, "type", "string");
    cJSON_AddStringToObject(memory_file, "description",
        "Optional path to a specific SQLite memory database file. Defaults to the configured memory file.");
    cJSON_AddItemToObject(props, "memory_file", memory_file);

    cJSON_AddItemToObject(params, "properties", props);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("entity"));
    cJSON_AddItemToArray(required, cJSON_CreateString("slot"));
    cJSON_AddItemToArray(required, cJSON_CreateString("value"));
    cJSON_AddItemToArray(required, cJSON_CreateString("kind"));
    cJSON_AddItemToObject(params, "required", required);

    return params;
}

static cJSON* build_memory_recall_params(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON *props = cJSON_CreateObject();

    cJSON *entity = cJSON_CreateObject();
    cJSON_AddStringToObject(entity, "type", "string");
    cJSON_AddStringToObject(entity, "description", "Who/what to look up");
    cJSON_AddItemToObject(props, "entity", entity);

    cJSON *slot = cJSON_CreateObject();
    cJSON_AddStringToObject(slot, "type", "string");
    cJSON_AddStringToObject(slot, "description", "The attribute to recall");
    cJSON_AddItemToObject(props, "slot", slot);

    cJSON *memory_file = cJSON_CreateObject();
    cJSON_AddStringToObject(memory_file, "type", "string");
    cJSON_AddStringToObject(memory_file, "description",
        "Optional path to a specific SQLite memory database file. Defaults to the configured memory file.");
    cJSON_AddItemToObject(props, "memory_file", memory_file);

    cJSON_AddItemToObject(params, "properties", props);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("entity"));
    cJSON_AddItemToArray(required, cJSON_CreateString("slot"));
    cJSON_AddItemToObject(params, "required", required);

    return params;
}

static cJSON* build_memory_search_params(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON *props = cJSON_CreateObject();

    cJSON *query = cJSON_CreateObject();
    cJSON_AddStringToObject(query, "type", "string");
    cJSON_AddStringToObject(query, "description", "Search query");
    cJSON_AddItemToObject(props, "query", query);

    cJSON *top_k = cJSON_CreateObject();
    cJSON_AddStringToObject(top_k, "type", "integer");
    cJSON_AddStringToObject(top_k, "description", "Number of results (default: 10)");
    cJSON_AddItemToObject(props, "top_k", top_k);

    cJSON *memory_file_search = cJSON_CreateObject();
    cJSON_AddStringToObject(memory_file_search, "type", "string");
    cJSON_AddStringToObject(memory_file_search, "description",
        "Optional path to a specific SQLite memory database file. Defaults to the configured memory file.");
    cJSON_AddItemToObject(props, "memory_file", memory_file_search);

    cJSON_AddItemToObject(params, "properties", props);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("query"));
    cJSON_AddItemToObject(params, "required", required);

    return params;
}

static void append_memory_tool(cJSON *tool_array,
                               ToolSchemaFormat format,
                               const char *name,
                               const char *description,
                               cJSON *params) {
    if (!tool_array || !name || !description || !params) {
        if (params) {
            cJSON_Delete(params);
        }
        return;
    }

    if (format == TOOL_SCHEMA_MESSAGES) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "type", "function");
        cJSON *func = cJSON_CreateObject();
        cJSON_AddStringToObject(func, "name", name);
        cJSON_AddStringToObject(func, "description", description);
        cJSON_AddItemToObject(func, "parameters", params);
        cJSON_AddItemToObject(tool, "function", func);
        cJSON_AddItemToArray(tool_array, tool);
    } else {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "type", "function");
        cJSON_AddStringToObject(tool, "name", name);
        cJSON_AddStringToObject(tool, "description", description);
        cJSON_AddItemToObject(tool, "parameters", params);
        cJSON_AddItemToArray(tool_array, tool);
    }
}

void add_memory_tools(cJSON *tool_array, ToolSchemaFormat format) {
    const char *store_desc =
        "Store a memory about the user or project. Memories persist across sessions. "
        "BE PROACTIVE - Store important information when you notice: "
        "(1) User preferences: 'I prefer...', 'I don't like...', 'always use...', 'never use...'; "
        "(2) User facts: 'I work at...', 'I'm learning...', 'my team uses...'; "
        "(3) Project constraints: 'we use tabs', 'we follow style X', 'this project requires Y'; "
        "(4) Recurring patterns in their coding style or requests; "
        "(5) User explicitly asks 'remember that...' or 'keep in mind...'. "
        "DO NOT store: temporary context, sensitive data (API keys/passwords), transient state, or information already in KLAWED.md.";

    const char *recall_desc =
        "Recall the current value for an entity's attribute from persistent memory. "
        "Use this when: (1) You need to check what you already know about user preferences or project details; "
        "(2) Starting a new conversation and want to recall context from previous sessions; "
        "(3) User mentions something you may have stored before.";

    const char *search_desc =
        "Search all memories by text query using FTS5 full-text search. "
        "The query can contain: plain words (matches any field), quoted phrases for exact matches, "
        "AND/OR operators, and field prefixes like 'entity:project*' or 'slot:deploy'. "
        "Use this when: (1) You need to find related past context but don't know the specific entity/slot; "
        "(2) User asks about something you may have discussed before; "
        "(3) After auto-compaction notice - search for relevant past conversation context; "
        "(4) Starting a complex task and want to check for relevant project knowledge; "
        "(5) After a context compaction event, use this tool to retrieve potentially relevant context.";

    if (!is_tool_disabled("MemoryStore")) {
        append_memory_tool(tool_array, format, "MemoryStore", store_desc, build_memory_store_params());
    } else {
        LOG_INFO("Tool 'MemoryStore' is disabled via KLAWED_DISABLE_TOOLS");
    }
    if (!is_tool_disabled("MemoryRecall")) {
        append_memory_tool(tool_array, format, "MemoryRecall", recall_desc, build_memory_recall_params());
    } else {
        LOG_INFO("Tool 'MemoryRecall' is disabled via KLAWED_DISABLE_TOOLS");
    }
    if (!is_tool_disabled("MemorySearch")) {
        append_memory_tool(tool_array, format, "MemorySearch", search_desc, build_memory_search_params());
    } else {
        LOG_INFO("Tool 'MemorySearch' is disabled via KLAWED_DISABLE_TOOLS");
    }
}

cJSON* get_tool_definitions(ConversationState *state, int enable_caching) {
    cJSON *tool_array = cJSON_CreateArray();
    int plan_mode = state ? state->plan_mode : 0;

    // Check if we're running as a subagent - if so, exclude the Subagent tool to prevent recursion
    const char *is_subagent_env = getenv("KLAWED_IS_SUBAGENT");
    int is_subagent = is_subagent_env && (strcmp(is_subagent_env, "1") == 0 ||
                                         strcasecmp(is_subagent_env, "true") == 0 ||
                                         strcasecmp(is_subagent_env, "yes") == 0);

    LOG_DEBUG("[TOOLS] get_tool_definitions: plan_mode=%d, is_subagent=%d", plan_mode, is_subagent);

    // Sleep tool
    if (!is_tool_disabled("Sleep")) {
    cJSON *sleep_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(sleep_tool, "type", "function");
    cJSON *sleep_func = cJSON_CreateObject();
    cJSON_AddStringToObject(sleep_func, "name", "Sleep");
    cJSON_AddStringToObject(sleep_func, "description", "Pauses execution for specified duration (seconds)");
    cJSON *sleep_params = cJSON_CreateObject();
    cJSON_AddStringToObject(sleep_params, "type", "object");
    cJSON *sleep_props = cJSON_CreateObject();
    cJSON *duration_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(duration_prop, "type", "integer");
    cJSON_AddStringToObject(duration_prop, "description", "Duration to sleep in seconds");
    cJSON_AddItemToObject(sleep_props, "duration", duration_prop);
    cJSON_AddItemToObject(sleep_params, "properties", sleep_props);
    cJSON *sleep_req = cJSON_CreateArray();
    cJSON_AddItemToArray(sleep_req, cJSON_CreateString("duration"));
    cJSON_AddItemToObject(sleep_params, "required", sleep_req);
    cJSON_AddItemToObject(sleep_func, "parameters", sleep_params);
    cJSON_AddItemToObject(sleep_tool, "function", sleep_func);
    if (enable_caching) {
        add_cache_control(sleep_tool);
    }
    cJSON_AddItemToArray(tool_array, sleep_tool);
    } else {
        LOG_INFO("Tool 'Sleep' is disabled via KLAWED_DISABLE_TOOLS");
    }

    // Read tool
    if (!is_tool_disabled("Read")) {
    cJSON *read = cJSON_CreateObject();
    cJSON_AddStringToObject(read, "type", "function");
    cJSON *read_func = cJSON_CreateObject();
    cJSON_AddStringToObject(read_func, "name", "Read");
    cJSON_AddStringToObject(read_func, "description",
        "Reads a file from the filesystem with optional line range support");
    cJSON *read_params = cJSON_CreateObject();
    cJSON_AddStringToObject(read_params, "type", "object");
    cJSON *read_props = cJSON_CreateObject();
    cJSON *read_path = cJSON_CreateObject();
    cJSON_AddStringToObject(read_path, "type", "string");
    cJSON_AddStringToObject(read_path, "description", "The absolute path to the file");
    cJSON_AddItemToObject(read_props, "file_path", read_path);
    cJSON *read_start = cJSON_CreateObject();
    cJSON_AddStringToObject(read_start, "type", "integer");
    cJSON_AddStringToObject(read_start, "description",
        "Optional: Starting line number (1-indexed, inclusive)");
    cJSON_AddItemToObject(read_props, "start_line", read_start);
    cJSON *read_end = cJSON_CreateObject();
    cJSON_AddStringToObject(read_end, "type", "integer");
    cJSON_AddStringToObject(read_end, "description",
        "Optional: Ending line number (1-indexed, inclusive)");
    cJSON_AddItemToObject(read_props, "end_line", read_end);
    cJSON_AddItemToObject(read_params, "properties", read_props);
    cJSON *read_req = cJSON_CreateArray();
    cJSON_AddItemToArray(read_req, cJSON_CreateString("file_path"));
    cJSON_AddItemToObject(read_params, "required", read_req);
    cJSON_AddItemToObject(read_func, "parameters", read_params);
    cJSON_AddItemToObject(read, "function", read_func);
    cJSON_AddItemToArray(tool_array, read);
    } else {
        LOG_INFO("Tool 'Read' is disabled via KLAWED_DISABLE_TOOLS");
    }

    // Bash, Subagent, Write, and Edit tools - excluded in plan mode
    if (!plan_mode) {
        // Bash tool
        if (!is_tool_disabled("Bash")) {
        cJSON *bash = cJSON_CreateObject();
        cJSON_AddStringToObject(bash, "type", "function");
        cJSON *bash_func = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_func, "name", "Bash");
        cJSON_AddStringToObject(bash_func, "description",
            "Executes bash commands. Note: stderr is automatically redirected to stdout "
            "to prevent terminal corruption, so both stdout and stderr output will be "
            "captured in the 'output' field. Commands have a configurable timeout "
            "(default: 30 seconds) to prevent hanging. Use the 'timeout' parameter to "
            "override the default or set to 0 for no timeout. If the output exceeds "
            "12,228 bytes, it will be truncated and a 'truncation_warning' field "
            "will be added to the result.");
        cJSON *bash_params = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_params, "type", "object");
        cJSON *bash_props = cJSON_CreateObject();
        cJSON *bash_cmd = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_cmd, "type", "string");
        cJSON_AddStringToObject(bash_cmd, "description", "The command to execute");
        cJSON_AddItemToObject(bash_props, "command", bash_cmd);
        cJSON *bash_timeout = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_timeout, "type", "integer");
        cJSON_AddStringToObject(bash_timeout, "description",
            "Optional: Timeout in seconds. Default: 30 (from KLAWED_BASH_TIMEOUT env var). "
            "Set to 0 for no timeout. Commands that timeout will return exit code -2.");
        cJSON_AddItemToObject(bash_props, "timeout", bash_timeout);
        cJSON_AddItemToObject(bash_params, "properties", bash_props);
        cJSON *bash_req = cJSON_CreateArray();
        cJSON_AddItemToArray(bash_req, cJSON_CreateString("command"));
        cJSON_AddItemToObject(bash_params, "required", bash_req);
        cJSON_AddItemToObject(bash_func, "parameters", bash_params);
        cJSON_AddItemToObject(bash, "function", bash_func);
        cJSON_AddItemToArray(tool_array, bash);
        } else {
            LOG_INFO("Tool 'Bash' is disabled via KLAWED_DISABLE_TOOLS");
        }

        // Subagent tool - exclude if running as subagent to prevent recursion
        if (!is_subagent && !is_tool_disabled("Subagent")) {
            cJSON *subagent = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent, "type", "function");
            cJSON *subagent_func = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_func, "name", "Subagent");
            cJSON_AddStringToObject(subagent_func, "description",
                "Spawns a new instance of klawed with the same configuration to work on a "
                "delegated task in a fresh context. The subagent runs independently and writes "
                "all output (stdout and stderr) to a log file. Returns the tail of the log "
                "(last 100 lines by default) which typically contains the task summary. "
                "Use this when: (1) you need a fresh context without conversation history, "
                "(2) delegating a complex independent task, (3) avoiding context limit issues. "
                "Note: The subagent has full tool access including Write, Edit, and Bash. "
                "IMPORTANT: Give the subagent adequate time to complete its work before "
                "interrupting. The subagent will report its progress and either complete "
                "successfully or stop and ask for further instructions if it cannot proceed.");
            cJSON *subagent_params = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_params, "type", "object");
            cJSON *subagent_props = cJSON_CreateObject();
            cJSON *subagent_prompt = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_prompt, "type", "string");
            cJSON_AddStringToObject(subagent_prompt, "description",
                "The task prompt for the subagent. Be specific and include all necessary context.");
            cJSON_AddItemToObject(subagent_props, "prompt", subagent_prompt);
            cJSON *subagent_timeout = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_timeout, "type", "integer");
            cJSON_AddStringToObject(subagent_timeout, "description",
                "Optional: Timeout in seconds. Default: 300 (5 minutes). Set to 0 for no timeout.");
            cJSON_AddItemToObject(subagent_props, "timeout", subagent_timeout);
            cJSON *subagent_tail = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_tail, "type", "integer");
            cJSON_AddStringToObject(subagent_tail, "description",
                "Optional: Number of lines to return from end of log. Default: 100. "
                "The summary is usually at the end.");
            cJSON_AddItemToObject(subagent_props, "tail_lines", subagent_tail);
            cJSON *subagent_provider = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_provider, "type", "string");
            cJSON_AddStringToObject(subagent_provider, "description",
                "Optional: LLM provider name to use for this subagent (e.g., 'gpt-4-turbo', 'sonnet-4.5-bedrock'). "
                "If not specified, subagent inherits the parent's provider configuration.");
            cJSON_AddItemToObject(subagent_props, "provider", subagent_provider);
            cJSON *subagent_working_dir = cJSON_CreateObject();
            cJSON_AddStringToObject(subagent_working_dir, "type", "string");
            cJSON_AddStringToObject(subagent_working_dir, "description",
                "Optional: Working directory for the subagent. If not specified, subagent inherits "
                "the parent's working directory. Must be an absolute path.");
            cJSON_AddItemToObject(subagent_props, "working_dir", subagent_working_dir);
            cJSON_AddItemToObject(subagent_params, "properties", subagent_props);
            cJSON *subagent_req = cJSON_CreateArray();
            cJSON_AddItemToArray(subagent_req, cJSON_CreateString("prompt"));
            cJSON_AddItemToObject(subagent_params, "required", subagent_req);
            cJSON_AddItemToObject(subagent_func, "parameters", subagent_params);
            cJSON_AddItemToObject(subagent, "function", subagent_func);
            cJSON_AddItemToArray(tool_array, subagent);

            // CheckSubagentProgress tool
            if (!is_tool_disabled("CheckSubagentProgress")) {
            cJSON *check_progress = cJSON_CreateObject();
            cJSON_AddStringToObject(check_progress, "type", "function");
            cJSON *check_progress_func = cJSON_CreateObject();
            cJSON_AddStringToObject(check_progress_func, "name", "CheckSubagentProgress");
            cJSON_AddStringToObject(check_progress_func, "description",
                "Checks the progress of a running subagent by reading its log file. "
                "Returns whether the subagent is still running and the tail of its output. "
                "Use this to monitor long-running subagent tasks. The subagent will log "
                "its progress and will stop and report if it encounters issues or needs "
                "further instructions. To conserve tokens, avoid checking too frequently: "
                "each check reads and returns log tail content, costing tokens. Sleep "
                "longer between checks for tasks that take a while.");
            cJSON *check_progress_params = cJSON_CreateObject();
            cJSON_AddStringToObject(check_progress_params, "type", "object");
            cJSON *check_progress_props = cJSON_CreateObject();
            cJSON *check_pid = cJSON_CreateObject();
            cJSON_AddStringToObject(check_pid, "type", "integer");
            cJSON_AddStringToObject(check_pid, "description",
                "Process ID of the subagent (from Subagent tool response)");
            cJSON_AddItemToObject(check_progress_props, "pid", check_pid);
            cJSON *check_log = cJSON_CreateObject();
            cJSON_AddStringToObject(check_log, "type", "string");
            cJSON_AddStringToObject(check_log, "description",
                "Path to subagent log file (from Subagent tool response)");
            cJSON_AddItemToObject(check_progress_props, "log_file", check_log);
            cJSON *check_tail = cJSON_CreateObject();
            cJSON_AddStringToObject(check_tail, "type", "integer");
            cJSON_AddStringToObject(check_tail, "description",
                "Optional: Number of lines to read from end of log (default: 50)");
            cJSON_AddItemToObject(check_progress_props, "tail_lines", check_tail);
            cJSON_AddItemToObject(check_progress_params, "properties", check_progress_props);
            cJSON_AddItemToObject(check_progress_func, "parameters", check_progress_params);
            cJSON_AddItemToObject(check_progress, "function", check_progress_func);
            cJSON_AddItemToArray(tool_array, check_progress);
            } else {
                LOG_INFO("Tool 'CheckSubagentProgress' is disabled via KLAWED_DISABLE_TOOLS");
            }

            // InterruptSubagent tool
            if (!is_tool_disabled("InterruptSubagent")) {
            cJSON *interrupt = cJSON_CreateObject();
            cJSON_AddStringToObject(interrupt, "type", "function");
            cJSON *interrupt_func = cJSON_CreateObject();
            cJSON_AddStringToObject(interrupt_func, "name", "InterruptSubagent");
            cJSON_AddStringToObject(interrupt_func, "description",
                "Interrupts and stops a running subagent. Use this only when the subagent "
                "is clearly stuck (no progress for extended period, repeated errors) or "
                "when the task is no longer needed. The subagent will report its status "
                "and either complete or ask for further instructions if it cannot proceed. "
                "Only interrupt if the subagent has been given adequate time to work.");
            cJSON *interrupt_params = cJSON_CreateObject();
            cJSON_AddStringToObject(interrupt_params, "type", "object");
            cJSON *interrupt_props = cJSON_CreateObject();
            cJSON *interrupt_pid = cJSON_CreateObject();
            cJSON_AddStringToObject(interrupt_pid, "type", "integer");
            cJSON_AddStringToObject(interrupt_pid, "description",
                "Process ID of the subagent to interrupt (from Subagent tool response)");
            cJSON_AddItemToObject(interrupt_props, "pid", interrupt_pid);
            cJSON_AddItemToObject(interrupt_params, "properties", interrupt_props);
            cJSON *interrupt_req = cJSON_CreateArray();
            cJSON_AddItemToArray(interrupt_req, cJSON_CreateString("pid"));
            cJSON_AddItemToObject(interrupt_params, "required", interrupt_req);
            cJSON_AddItemToObject(interrupt_func, "parameters", interrupt_params);
            cJSON_AddItemToObject(interrupt, "function", interrupt_func);
            cJSON_AddItemToArray(tool_array, interrupt);
            } else {
                LOG_INFO("Tool 'InterruptSubagent' is disabled via KLAWED_DISABLE_TOOLS");
            }
        } else if (!is_subagent) {
            LOG_INFO("Tool 'Subagent' is disabled via KLAWED_DISABLE_TOOLS");
        }

        // Write tool
        if (!is_tool_disabled("Write")) {
        cJSON *write = cJSON_CreateObject();
        cJSON_AddStringToObject(write, "type", "function");
        cJSON *write_func = cJSON_CreateObject();
        cJSON_AddStringToObject(write_func, "name", "Write");
        cJSON_AddStringToObject(write_func, "description",
            "Writes content to a file. Requires both 'file_path' (path string) and 'content' (file content string) parameters.");
        cJSON *write_params = cJSON_CreateObject();
        cJSON_AddStringToObject(write_params, "type", "object");
        cJSON *write_props = cJSON_CreateObject();
        cJSON *write_path = cJSON_CreateObject();
        cJSON_AddStringToObject(write_path, "type", "string");
        cJSON_AddStringToObject(write_path, "description", "Path to the file to write");
        cJSON_AddItemToObject(write_props, "file_path", write_path);
        cJSON *write_content = cJSON_CreateObject();
        cJSON_AddStringToObject(write_content, "type", "string");
        cJSON_AddStringToObject(write_content, "description", "Content to write to the file");
        cJSON_AddItemToObject(write_props, "content", write_content);
        cJSON_AddItemToObject(write_params, "properties", write_props);
        cJSON *write_req = cJSON_CreateArray();
        cJSON_AddItemToArray(write_req, cJSON_CreateString("file_path"));
        cJSON_AddItemToArray(write_req, cJSON_CreateString("content"));
        cJSON_AddItemToObject(write_params, "required", write_req);
        cJSON_AddItemToObject(write_func, "parameters", write_params);
        cJSON_AddItemToObject(write, "function", write_func);
        cJSON_AddItemToArray(tool_array, write);
        } else {
            LOG_INFO("Tool 'Write' is disabled via KLAWED_DISABLE_TOOLS");
        }

        // Edit tool (simplified - simple string replacement only)
        if (!is_tool_disabled("Edit")) {
        cJSON *edit = cJSON_CreateObject();
        cJSON_AddStringToObject(edit, "type", "function");
        cJSON *edit_func = cJSON_CreateObject();
        cJSON_AddStringToObject(edit_func, "name", "Edit");
        cJSON_AddStringToObject(edit_func, "description",
            "Performs simple string replacement in files. Replaces the first occurrence of old_string with new_string. "
            "To avoid hitting token limits, make smaller changes and call the Edit tool multiple times with focused edits instead of making massive changes in a single call. "
            "Break large operations into logical chunks (e.g., edit one function at a time, one section at a time).");
        cJSON *edit_params = cJSON_CreateObject();
        cJSON_AddStringToObject(edit_params, "type", "object");
        cJSON *edit_props = cJSON_CreateObject();
        cJSON *edit_path = cJSON_CreateObject();
        cJSON_AddStringToObject(edit_path, "type", "string");
        cJSON_AddStringToObject(edit_path, "description", "Path to the file to edit");
        cJSON_AddItemToObject(edit_props, "file_path", edit_path);
        cJSON *old_str = cJSON_CreateObject();
        cJSON_AddStringToObject(old_str, "type", "string");
        cJSON_AddStringToObject(old_str, "description",
            "Exact text to find and replace. Only simple string matching is supported.");
        cJSON_AddItemToObject(edit_props, "old_string", old_str);
        cJSON *new_str = cJSON_CreateObject();
        cJSON_AddStringToObject(new_str, "type", "string");
        cJSON_AddStringToObject(new_str, "description",
            "Replacement text. Will replace the first occurrence of old_string.");
        cJSON_AddItemToObject(edit_props, "new_string", new_str);
        cJSON_AddItemToObject(edit_params, "properties", edit_props);
        cJSON *edit_req = cJSON_CreateArray();
        cJSON_AddItemToArray(edit_req, cJSON_CreateString("file_path"));
        cJSON_AddItemToArray(edit_req, cJSON_CreateString("old_string"));
        cJSON_AddItemToArray(edit_req, cJSON_CreateString("new_string"));
        cJSON_AddItemToObject(edit_params, "required", edit_req);
        cJSON_AddItemToObject(edit_func, "parameters", edit_params);
        cJSON_AddItemToObject(edit, "function", edit_func);
        cJSON_AddItemToArray(tool_array, edit);
        } else {
            LOG_INFO("Tool 'Edit' is disabled via KLAWED_DISABLE_TOOLS");
        }

        // MultiEdit tool
        if (!is_tool_disabled("MultiEdit")) {
        cJSON *multiedit = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit, "type", "function");
        cJSON *multiedit_func = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_func, "name", "MultiEdit");
        cJSON_AddStringToObject(multiedit_func, "description",
            "Performs multiple string replacements in a file. Applies edits sequentially in the order provided. "
            "Each edit replaces the first occurrence of old_string with new_string. "
            "Returns counts of successful and failed edits. "
            "To avoid hitting token limits, make smaller changes and break large operations into logical chunks.");
        cJSON *multiedit_params = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_params, "type", "object");
        cJSON *multiedit_props = cJSON_CreateObject();
        cJSON *multiedit_path = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_path, "type", "string");
        cJSON_AddStringToObject(multiedit_path, "description", "Path to the file to edit");
        cJSON_AddItemToObject(multiedit_props, "file_path", multiedit_path);
        cJSON *multiedit_edits = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_edits, "type", "array");
        cJSON_AddStringToObject(multiedit_edits, "description",
            "Array of edit objects, each with old_string and new_string fields");
        cJSON *multiedit_edit_items = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_edit_items, "type", "object");
        cJSON *multiedit_edit_props = cJSON_CreateObject();
        cJSON *multiedit_old = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_old, "type", "string");
        cJSON_AddStringToObject(multiedit_old, "description", "Exact text to find and replace");
        cJSON_AddItemToObject(multiedit_edit_props, "old_string", multiedit_old);
        cJSON *multiedit_new = cJSON_CreateObject();
        cJSON_AddStringToObject(multiedit_new, "type", "string");
        cJSON_AddStringToObject(multiedit_new, "description", "Replacement text");
        cJSON_AddItemToObject(multiedit_edit_props, "new_string", multiedit_new);
        cJSON_AddItemToObject(multiedit_edit_items, "properties", multiedit_edit_props);
        cJSON *multiedit_edit_req = cJSON_CreateArray();
        cJSON_AddItemToArray(multiedit_edit_req, cJSON_CreateString("old_string"));
        cJSON_AddItemToArray(multiedit_edit_req, cJSON_CreateString("new_string"));
        cJSON_AddItemToObject(multiedit_edit_items, "required", multiedit_edit_req);
        cJSON_AddItemToObject(multiedit_edits, "items", multiedit_edit_items);
        cJSON_AddItemToObject(multiedit_props, "edits", multiedit_edits);
        cJSON_AddItemToObject(multiedit_params, "properties", multiedit_props);
        cJSON *multiedit_req = cJSON_CreateArray();
        cJSON_AddItemToArray(multiedit_req, cJSON_CreateString("file_path"));
        cJSON_AddItemToArray(multiedit_req, cJSON_CreateString("edits"));
        cJSON_AddItemToObject(multiedit_params, "required", multiedit_req);
        cJSON_AddItemToObject(multiedit_func, "parameters", multiedit_params);
        cJSON_AddItemToObject(multiedit, "function", multiedit_func);
        cJSON_AddItemToArray(tool_array, multiedit);
        } else {
            LOG_INFO("Tool 'MultiEdit' is disabled via KLAWED_DISABLE_TOOLS");
        }
    }

    // Glob tool
    if (!is_tool_disabled("Glob")) {
    cJSON *glob_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_tool, "type", "function");
    cJSON *glob_func = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_func, "name", "Glob");
    cJSON_AddStringToObject(glob_func, "description", "Finds files matching a pattern");
    cJSON *glob_params = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_params, "type", "object");
    cJSON *glob_props = cJSON_CreateObject();
    cJSON *glob_pattern = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_pattern, "type", "string");
    cJSON_AddStringToObject(glob_pattern, "description", "Glob pattern to match files against");
    cJSON_AddItemToObject(glob_props, "pattern", glob_pattern);
    cJSON_AddItemToObject(glob_params, "properties", glob_props);
    cJSON *glob_req = cJSON_CreateArray();
    cJSON_AddItemToArray(glob_req, cJSON_CreateString("pattern"));
    cJSON_AddItemToObject(glob_params, "required", glob_req);
    cJSON_AddItemToObject(glob_func, "parameters", glob_params);
    cJSON_AddItemToObject(glob_tool, "function", glob_func);
    cJSON_AddItemToArray(tool_array, glob_tool);
    } else {
        LOG_INFO("Tool 'Glob' is disabled via KLAWED_DISABLE_TOOLS");
    }

    // Grep tool
    if (!is_tool_disabled("Grep")) {
    cJSON *grep_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_tool, "type", "function");
    cJSON *grep_func = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_func, "name", "Grep");
    cJSON_AddStringToObject(grep_func, "description",
        "Searches for patterns in files. Results limited to 100 matches by default. "
        "Use max_results parameter to request more (warning printed if >300). "
        "Automatically excludes common build directories, dependencies, and binary files "
        "(.git, node_modules, build/, *.min.js, etc). Returns 'match_count', 'total_matches', "
        "and 'warning' if truncated.");
    cJSON *grep_params = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_params, "type", "object");
    cJSON *grep_props = cJSON_CreateObject();
    cJSON *grep_pattern = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_pattern, "type", "string");
    cJSON_AddStringToObject(grep_pattern, "description", "Pattern to search for");
    cJSON_AddItemToObject(grep_props, "pattern", grep_pattern);
    cJSON *grep_path = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_path, "type", "string");
    cJSON_AddStringToObject(grep_path, "description", "Path to search in (default: .)");
    cJSON_AddItemToObject(grep_props, "path", grep_path);
    cJSON *grep_max_results = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_max_results, "type", "integer");
    cJSON_AddStringToObject(grep_max_results, "description", "Optional: Maximum number of matches to return (default: 100). Values >300 will print a warning.");
    cJSON_AddItemToObject(grep_props, "max_results", grep_max_results);
    cJSON_AddItemToObject(grep_params, "properties", grep_props);
    cJSON *grep_req = cJSON_CreateArray();
    cJSON_AddItemToArray(grep_req, cJSON_CreateString("pattern"));
    cJSON_AddItemToObject(grep_params, "required", grep_req);
    cJSON_AddItemToObject(grep_func, "parameters", grep_params);
    cJSON_AddItemToObject(grep_tool, "function", grep_func);
    cJSON_AddItemToArray(tool_array, grep_tool);
    } else {
        LOG_INFO("Tool 'Grep' is disabled via KLAWED_DISABLE_TOOLS");
    }

    // UploadImage tool (conditionally added based on KLAWED_DISABLE_TOOLS)
    if (!is_tool_disabled("UploadImage")) {
        cJSON *upload_image_tool = cJSON_CreateObject();
        cJSON_AddStringToObject(upload_image_tool, "type", "function");
        cJSON *upload_image_func = cJSON_CreateObject();
        cJSON_AddStringToObject(upload_image_func, "name", "UploadImage");
        cJSON_AddStringToObject(upload_image_func, "description",
            "Uploads an image file to be included in the conversation context using OpenAI-compatible format. Supports common image formats (PNG, JPEG, GIF, WebP).");
        cJSON *upload_image_params = cJSON_CreateObject();
        cJSON_AddStringToObject(upload_image_params, "type", "object");
        cJSON *upload_image_props = cJSON_CreateObject();
        cJSON *image_path_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(image_path_prop, "type", "string");
        cJSON_AddStringToObject(image_path_prop, "description", "Path to the image file to upload");
        cJSON_AddItemToObject(upload_image_props, "file_path", image_path_prop);
        cJSON_AddItemToObject(upload_image_params, "properties", upload_image_props);
        cJSON *upload_image_req = cJSON_CreateArray();
        cJSON_AddItemToArray(upload_image_req, cJSON_CreateString("file_path"));
        cJSON_AddItemToObject(upload_image_params, "required", upload_image_req);
        cJSON_AddItemToObject(upload_image_func, "parameters", upload_image_params);
        cJSON_AddItemToObject(upload_image_tool, "function", upload_image_func);
        cJSON_AddItemToArray(tool_array, upload_image_tool);
    } else {
        LOG_INFO("Tool 'UploadImage' is disabled via KLAWED_DISABLE_TOOLS");
    }

    // TodoWrite tool
    if (!is_tool_disabled("TodoWrite")) {
    cJSON *todo_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_tool, "type", "function");
    cJSON *todo_func = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_func, "name", "TodoWrite");
    cJSON_AddStringToObject(todo_func, "description",
        "Creates and updates a task list to track progress on multi-step tasks");
    cJSON *todo_params = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_params, "type", "object");
    cJSON *todo_props = cJSON_CreateObject();

    // Define the todos array parameter
    cJSON *todos_array = cJSON_CreateObject();
    cJSON_AddStringToObject(todos_array, "type", "array");
    cJSON_AddStringToObject(todos_array, "description",
        "Array of todo items to display. Replaces the entire todo list.");

    // Define the items schema for the array
    cJSON *todos_items = cJSON_CreateObject();
    cJSON_AddStringToObject(todos_items, "type", "object");
    cJSON *item_props = cJSON_CreateObject();

    cJSON *content_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(content_prop, "type", "string");
    cJSON_AddStringToObject(content_prop, "description",
        "Task description in imperative form (e.g., 'Run tests')");
    cJSON_AddItemToObject(item_props, "content", content_prop);

    cJSON *active_form_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(active_form_prop, "type", "string");
    cJSON_AddStringToObject(active_form_prop, "description",
        "Task description in present continuous form (e.g., 'Running tests')");
    cJSON_AddItemToObject(item_props, "activeForm", active_form_prop);

    cJSON *status_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(status_prop, "type", "string");
    cJSON *status_enum = cJSON_CreateArray();
    cJSON_AddItemToArray(status_enum, cJSON_CreateString("pending"));
    cJSON_AddItemToArray(status_enum, cJSON_CreateString("in_progress"));
    cJSON_AddItemToArray(status_enum, cJSON_CreateString("completed"));
    cJSON_AddItemToObject(status_prop, "enum", status_enum);
    cJSON_AddStringToObject(status_prop, "description",
        "Current status of the task");
    cJSON_AddItemToObject(item_props, "status", status_prop);

    cJSON_AddItemToObject(todos_items, "properties", item_props);
    cJSON *item_required = cJSON_CreateArray();
    cJSON_AddItemToArray(item_required, cJSON_CreateString("content"));
    cJSON_AddItemToArray(item_required, cJSON_CreateString("activeForm"));
    cJSON_AddItemToArray(item_required, cJSON_CreateString("status"));
    cJSON_AddItemToObject(todos_items, "required", item_required);

    cJSON_AddItemToObject(todos_array, "items", todos_items);
    cJSON_AddItemToObject(todo_props, "todos", todos_array);

    cJSON_AddItemToObject(todo_params, "properties", todo_props);
    cJSON *todo_req = cJSON_CreateArray();
    cJSON_AddItemToArray(todo_req, cJSON_CreateString("todos"));
    cJSON_AddItemToObject(todo_params, "required", todo_req);
    cJSON_AddItemToObject(todo_func, "parameters", todo_params);
    cJSON_AddItemToObject(todo_tool, "function", todo_func);

    // Add cache_control to the last tool (TodoWrite) if caching is enabled
    // This is the second cache breakpoint (tool definitions)
    if (enable_caching) {
        add_cache_control(todo_tool);
    }

    cJSON_AddItemToArray(tool_array, todo_tool);
    } else {
        LOG_INFO("Tool 'TodoWrite' is disabled via KLAWED_DISABLE_TOOLS");
    }

    add_memory_tools(tool_array, TOOL_SCHEMA_MESSAGES);

#ifndef TEST_BUILD
    // Add MCP tools if MCP is enabled and configured
    if (state && state->mcp_config && mcp_is_enabled()) {
        LOG_DEBUG("get_tool_definitions: Adding MCP tools to tool definitions");

        // 1) Dynamic MCP tools discovered from servers
        cJSON *mcp_tools = mcp_get_all_tools(state->mcp_config);
        if (mcp_tools && cJSON_IsArray(mcp_tools)) {
            int mcp_tool_count = cJSON_GetArraySize(mcp_tools);
            LOG_DEBUG("get_tool_definitions: Found %d dynamic MCP tools", mcp_tool_count);
            cJSON *t = NULL;
            int idx = 0;
            cJSON_ArrayForEach(t, mcp_tools) {
                // Each t is already a full Claude tool definition object { type: "function", function: { name, ... } }
                cJSON *func = cJSON_GetObjectItem(t, "function");
                cJSON *name_obj = func ? cJSON_GetObjectItem(func, "name") : NULL;
                const char *tool_name = name_obj && cJSON_IsString(name_obj) ? name_obj->valuestring : "unknown";
                LOG_DEBUG("get_tool_definitions: Adding dynamic MCP tool %d: '%s'", idx, tool_name);
                cJSON_AddItemToArray(tool_array, cJSON_Duplicate(t, 1));
                idx++;
            }
            cJSON_Delete(mcp_tools);
        } else {
            LOG_DEBUG("get_tool_definitions: No dynamic MCP tools found");
        }

        // 2) Built-in helper tools for MCP resources and generic invocation
        LOG_DEBUG("get_tool_definitions: Adding built-in MCP resource tools");

        // ListMcpResources tool
        cJSON *list_res_tool = cJSON_CreateObject();
        cJSON_AddStringToObject(list_res_tool, "type", "function");
        cJSON *list_res_func = cJSON_CreateObject();
        cJSON_AddStringToObject(list_res_func, "name", "ListMcpResources");
        cJSON_AddStringToObject(list_res_func, "description",
            "Lists available resources from configured MCP servers. "
            "Each resource object includes a 'server' field indicating which server it's from.");
        cJSON *list_res_params = cJSON_CreateObject();
        cJSON_AddStringToObject(list_res_params, "type", "object");
        cJSON *list_res_props = cJSON_CreateObject();
        cJSON *server_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(server_prop, "type", "string");
        cJSON_AddStringToObject(server_prop, "description",
            "Optional server name to filter resources by. If not provided, resources from all servers will be returned.");
        cJSON_AddItemToObject(list_res_props, "server", server_prop);
        cJSON_AddItemToObject(list_res_params, "properties", list_res_props);
        cJSON_AddItemToObject(list_res_func, "parameters", list_res_params);
        cJSON_AddItemToObject(list_res_tool, "function", list_res_func);
        cJSON_AddItemToArray(tool_array, list_res_tool);

        // ReadMcpResource tool
        cJSON *read_res_tool = cJSON_CreateObject();
        cJSON_AddStringToObject(read_res_tool, "type", "function");
        cJSON *read_res_func = cJSON_CreateObject();
        cJSON_AddStringToObject(read_res_func, "name", "ReadMcpResource");
        cJSON_AddStringToObject(read_res_func, "description",
            "Reads a specific resource from an MCP server, identified by server name and resource URI.");
        cJSON *read_res_params = cJSON_CreateObject();
        cJSON_AddStringToObject(read_res_params, "type", "object");
        cJSON *read_res_props = cJSON_CreateObject();
        cJSON *read_server_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(read_server_prop, "type", "string");
        cJSON_AddStringToObject(read_server_prop, "description", "The name of the MCP server to read from");
        cJSON_AddItemToObject(read_res_props, "server", read_server_prop);
        cJSON *uri_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(uri_prop, "type", "string");
        cJSON_AddStringToObject(uri_prop, "description", "The URI of the resource to read");
        cJSON_AddItemToObject(read_res_props, "uri", uri_prop);
        cJSON_AddItemToObject(read_res_params, "properties", read_res_props);
        cJSON *read_res_req = cJSON_CreateArray();
        cJSON_AddItemToArray(read_res_req, cJSON_CreateString("server"));
        cJSON_AddItemToArray(read_res_req, cJSON_CreateString("uri"));
        cJSON_AddItemToObject(read_res_params, "required", read_res_req);
        cJSON_AddItemToObject(read_res_func, "parameters", read_res_params);
        cJSON_AddItemToObject(read_res_tool, "function", read_res_func);
        cJSON_AddItemToArray(tool_array, read_res_tool);

        // CallMcpTool tool (generic MCP tool invoker)
        cJSON *call_tool = cJSON_CreateObject();
        cJSON_AddStringToObject(call_tool, "type", "function");
        cJSON *call_func = cJSON_CreateObject();
        cJSON_AddStringToObject(call_func, "name", "CallMcpTool");
        cJSON_AddStringToObject(call_func, "description",
            "Calls a specific MCP tool by server and tool name with JSON arguments.");
        cJSON *call_params = cJSON_CreateObject();
        cJSON_AddStringToObject(call_params, "type", "object");
        cJSON *call_props = cJSON_CreateObject();
        cJSON *call_server_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(call_server_prop, "type", "string");
        cJSON_AddStringToObject(call_server_prop, "description", "The MCP server name (as in config)");
        cJSON_AddItemToObject(call_props, "server", call_server_prop);
        cJSON *call_tool_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(call_tool_prop, "type", "string");
        cJSON_AddStringToObject(call_tool_prop, "description", "The tool name exposed by the server");
        cJSON_AddItemToObject(call_props, "tool", call_tool_prop);
        cJSON *call_args_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(call_args_prop, "type", "object");
        cJSON_AddStringToObject(call_args_prop, "description", "Arguments object per the tool's JSON schema");
        cJSON_AddItemToObject(call_props, "arguments", call_args_prop);
        cJSON_AddItemToObject(call_params, "properties", call_props);
        cJSON *call_req = cJSON_CreateArray();
        cJSON_AddItemToArray(call_req, cJSON_CreateString("server"));
        cJSON_AddItemToArray(call_req, cJSON_CreateString("tool"));
        cJSON_AddItemToObject(call_params, "required", call_req);
        cJSON_AddItemToObject(call_func, "parameters", call_params);
        cJSON_AddItemToObject(call_tool, "function", call_func);
        cJSON_AddItemToArray(tool_array, call_tool);

        LOG_INFO("Added MCP resource tools (ListMcpResources, ReadMcpResource)");
    }
#else
    (void)state;  // Suppress unused parameter warning in test builds
#endif

    // Add Explore tools if explore mode is enabled
    if (is_explore_mode_enabled()) {
        LOG_INFO("Explore mode enabled - adding explore tools");

        // web_search and web_read require web_browse_agent binary
        int web_agent_available = is_web_agent_available();
        if (web_agent_available) {
            // web_search tool
            cJSON *web_search_json = cJSON_Parse(explore_tool_web_search_schema());
            if (web_search_json) {
                cJSON_AddItemToArray(tool_array, web_search_json);
            }

            // web_read tool
            cJSON *web_read_json = cJSON_Parse(explore_tool_web_read_schema());
            if (web_read_json) {
                cJSON_AddItemToArray(tool_array, web_read_json);
            }
            LOG_INFO("Added web tools (web_search, web_read)");
        } else {
            LOG_WARN("web_browse_agent binary not found - web_search and web_read tools disabled");
        }
    }

    // Load and add dynamic tools from JSON file
    DynamicToolsRegistry dynamic_registry;
    dynamic_tools_init(&dynamic_registry);

    char dynamic_tools_path[DYNAMIC_TOOLS_PATH_MAX];
    if (dynamic_tools_get_path(dynamic_tools_path, sizeof(dynamic_tools_path)) == 0) {
        LOG_INFO("Loading dynamic tools from: %s", dynamic_tools_path);
        int loaded = dynamic_tools_load_from_file(&dynamic_registry, dynamic_tools_path);
        if (loaded > 0) {
            int added = dynamic_tools_add_to_array(&dynamic_registry, tool_array);
            LOG_INFO("Added %d dynamic tool(s) to tool definitions", added);
        } else if (loaded == 0) {
            LOG_DEBUG("No dynamic tools found in %s", dynamic_tools_path);
        } else {
            LOG_WARN("Failed to load dynamic tools from %s", dynamic_tools_path);
        }
    } else {
        LOG_DEBUG("No dynamic tools file found (checked KLAWED_DYNAMIC_TOOLS env, local .klawed/dynamic_tools.json, and ~/.klawed/dynamic_tools.json)");
    }

    dynamic_tools_cleanup(&dynamic_registry);

    return tool_array;
}
