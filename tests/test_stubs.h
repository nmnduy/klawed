/**
 * test_stubs.h - Declarations for test stub functions
 */

#ifndef TEST_STUBS_H
#define TEST_STUBS_H

// Stubs for tool functions needed by patch_parser
char* read_file(const char *path);
int write_file(const char *path, const char *content);
char* resolve_path(const char *path, const char *working_dir);

// Stubs for tool_definitions.c dependencies (TEST_BUILD mode)
int is_web_agent_available(void);
int is_explore_mode_enabled(void);
const char* explore_tool_web_browse_agent_schema(void);
const char* explore_tool_web_search_schema(void);
const char* explore_tool_web_read_schema(void);
const char* explore_tool_context7_search_schema(void);
const char* explore_tool_context7_docs_schema(void);
int mcp_is_enabled(void);
void* mcp_get_all_tools(void *config);
void add_cache_control(void *obj);

// Stub for tool_utils functions
int is_tool_disabled(const char *tool_name);

#endif // TEST_STUBS_H
