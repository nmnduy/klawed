/*
 * Dynamic Tools - Load tool definitions from JSON files
 *
 * Allows klawed to load custom tool definitions from external JSON files
 * without requiring recompilation. Tools must provide their own execution
 * handlers externally (e.g., via MCP or other mechanisms).
 */

#ifndef DYNAMIC_TOOLS_H
#define DYNAMIC_TOOLS_H

#include <cjson/cJSON.h>

// Maximum number of dynamic tools that can be loaded
#define DYNAMIC_TOOLS_MAX 32

// Maximum path length for tool definitions file
#define DYNAMIC_TOOLS_PATH_MAX 1024

// Maximum length for tool name
#define DYNAMIC_TOOL_NAME_MAX 128

// Maximum length for tool description
#define DYNAMIC_TOOL_DESC_MAX 4096

/**
 * Dynamic tool definition structure
 */
typedef struct {
    char name[DYNAMIC_TOOL_NAME_MAX];       // Tool name
    char description[DYNAMIC_TOOL_DESC_MAX]; // Tool description
    cJSON *parameters;                       // JSON schema for parameters (owned by this struct)
    cJSON *cache_control;                    // Optional cache control (owned by this struct)
} DynamicToolDef;

/**
 * Dynamic tools registry
 */
typedef struct {
    DynamicToolDef tools[DYNAMIC_TOOLS_MAX];
    int count;
    char source_path[DYNAMIC_TOOLS_PATH_MAX]; // Path to the loaded JSON file
    int loaded;                                // Whether tools have been loaded successfully
} DynamicToolsRegistry;

/**
 * Initialize the dynamic tools registry
 *
 * @param registry Pointer to registry to initialize
 */
void dynamic_tools_init(DynamicToolsRegistry *registry);

/**
 * Free all resources in the dynamic tools registry
 *
 * @param registry Pointer to registry to cleanup
 */
void dynamic_tools_cleanup(DynamicToolsRegistry *registry);

/**
 * Load tool definitions from a JSON file
 *
 * The JSON file should contain an array of OpenAI-compatible tool definitions:
 * [
 *   {
 *     "type": "function",
 *     "function": {
 *       "name": "tool_name",
 *       "description": "Tool description",
 *       "parameters": { ... JSON schema ... }
 *     }
 *   }
 * ]
 *
 * @param registry Pointer to registry to populate
 * @param file_path Path to the JSON file
 * @return Number of tools loaded (0 or more), -1 on error
 */
int dynamic_tools_load_from_file(DynamicToolsRegistry *registry, const char *file_path);

/**
 * Load tool definitions from a JSON string
 *
 * @param registry Pointer to registry to populate
 * @param json_str JSON string containing tool definitions array
 * @return Number of tools loaded (0 or more), -1 on error
 */
int dynamic_tools_load_from_string(DynamicToolsRegistry *registry, const char *json_str);

/**
 * Add tools from the registry to a cJSON array
 *
 * @param registry Pointer to loaded registry
 * @param tool_array cJSON array to append tools to
 * @return Number of tools added
 */
int dynamic_tools_add_to_array(const DynamicToolsRegistry *registry, cJSON *tool_array);

/**
 * Get the path to the dynamic tools file from environment or config
 * Checks (in order):
 *   1. KLAWED_DYNAMIC_TOOLS environment variable
 *   2. .klawed/dynamic_tools.json (local project)
 *   3. ~/.klawed/dynamic_tools.json (global)
 *
 * @param buf Buffer to store the path
 * @param buf_size Size of the buffer
 * @return 0 if a path was found, -1 if no dynamic tools file exists
 */
int dynamic_tools_get_path(char *buf, size_t buf_size);

/**
 * Check if a tool name exists in the dynamic registry
 *
 * @param registry Pointer to registry
 * @param name Tool name to check
 * @return 1 if found, 0 if not found
 */
int dynamic_tools_has_tool(const DynamicToolsRegistry *registry, const char *name);

/**
 * Get a tool definition by name
 *
 * @param registry Pointer to registry
 * @param name Tool name to find
 * @return Pointer to tool definition, or NULL if not found
 */
const DynamicToolDef* dynamic_tools_get_tool(const DynamicToolsRegistry *registry, const char *name);

#endif // DYNAMIC_TOOLS_H
