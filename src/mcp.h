/*
 * mcp.h - Model Context Protocol (MCP) client implementation
 *
 * This module provides MCP server support for claude-c, allowing it to
 * connect to and use external MCP servers for additional tools and resources.
 *
 * MCP Specification: https://spec.modelcontextprotocol.io/
 *
 * Features:
 * - Multiple MCP server connections via stdio/SSE transports
 * - Dynamic tool discovery from connected servers
 * - Seamless integration with existing claude-c tool system
 * - Configuration via JSON config file
 *
 * Configuration example (~/.config/claude-c/mcp_servers.json):
 * {
 *   "mcpServers": {
 *     "filesystem": {
 *       "command": "npx",
 *       "args": ["-y", "@modelcontextprotocol/server-filesystem", "/path/to/allowed/files"],
 *       "env": {}
 *     }
 *   }
 * }
 */

#ifndef MCP_H
#define MCP_H

#include <cjson/cJSON.h>

/*
 * Transport types for MCP servers
 */
typedef enum {
    MCP_TRANSPORT_STDIO,   // Standard input/output (local process)
    MCP_TRANSPORT_SSE      // Server-Sent Events (HTTP)
} MCPTransportType;

/*
 * MCP server connection
 */
typedef struct MCPServer {
    char *name;                  // Server identifier (e.g., "filesystem")
    MCPTransportType transport;  // Transport type

    // For stdio transport
    char *command;               // Command to execute (e.g., "npx")
    char **args;                 // Command arguments
    int args_count;              // Number of arguments
    char **env;                  // Environment variables (key=value pairs)
    int env_count;               // Number of env vars
    pid_t pid;                   // Process ID (if running)
    int stdin_fd;                // Write to server's stdin
    int stdout_fd;               // Read from server's stdout
    int stderr_fd;               // Read from server's stderr (for logging)

    // For SSE transport
    char *url;                   // Server URL

    // Server capabilities
    char **tools;                // List of tool names
    int tool_count;              // Number of tools
    cJSON *tool_schemas;         // Tool JSON schemas from server

    // State
    int connected;               // Connection status
    int message_id;              // Message ID counter for JSON-RPC
    FILE *stderr_log;            // File handle for logging stderr output
} MCPServer;

/*
 * MCP client configuration
 */
typedef struct MCPConfig {
    MCPServer **servers;         // Array of server configurations
    int server_count;            // Number of servers
} MCPConfig;

/*
 * MCP tool call result
 */
typedef struct MCPToolResult {
    char *tool_name;             // Name of the tool that was called
    char *result;                // Text result content (for text-based responses)
    void *blob;                  // Binary content (for image/binary responses)
    size_t blob_size;            // Size of binary content
    char *mime_type;             // MIME type of the response
    int is_error;                // 1 if error, 0 if success
} MCPToolResult;

/*
 * MCP resource (returned by list_resources)
 */
typedef struct MCPResource {
    char *server;                // Server name that provides this resource
    char *uri;                   // Resource URI
    char *name;                  // Resource name
    char *description;           // Optional description
    char *mime_type;             // Optional MIME type
} MCPResource;

/*
 * MCP resource list result
 */
typedef struct MCPResourceList {
    MCPResource **resources;     // Array of resources
    int count;                   // Number of resources
    int is_error;                // 1 if error, 0 if success
    char *error_message;         // Error message if is_error is 1
} MCPResourceList;

/*
 * MCP resource content result
 */
typedef struct MCPResourceContent {
    char *uri;                   // Resource URI
    char *mime_type;             // MIME type
    char *text;                  // Text content (if text-based)
    void *blob;                  // Binary content (if binary)
    size_t blob_size;            // Size of binary content
    int is_error;                // 1 if error, 0 if success
    char *error_message;         // Error message if is_error is 1
} MCPResourceContent;

/*
 * Initialize MCP subsystem
 * Returns: 0 on success, -1 on error
 */
int mcp_init(void);

/*
 * Clean up MCP subsystem
 */
void mcp_cleanup(void);

/*
 * Load MCP server configuration from JSON file
 * Config file format follows Claude Desktop's mcp_servers.json format
 *
 * Default location: ~/.config/claude-c/mcp_servers.json
 * Can be overridden with CLAUDE_MCP_CONFIG env var
 *
 * Returns: MCPConfig* on success, NULL on error
 */
MCPConfig* mcp_load_config(const char *config_path);

/*
 * Free MCP configuration
 */
void mcp_free_config(MCPConfig *config);

/*
 * Connect to an MCP server
 * Returns: 0 on success, -1 on error
 */
int mcp_connect_server(MCPServer *server);

/*
 * Disconnect from an MCP server
 */
void mcp_disconnect_server(MCPServer *server);

/*
 * Discover tools from a connected MCP server
 * Calls the tools/list method and populates server->tools
 * Returns: Number of tools discovered, -1 on error
 */
int mcp_discover_tools(MCPServer *server);

/*
 * Call an MCP tool
 *
 * Parameters:
 *   server: Connected MCP server
 *   tool_name: Name of the tool to call
 *   arguments: JSON object with tool arguments
 *
 * Returns: MCPToolResult* on success, NULL on error
 */
MCPToolResult* mcp_call_tool(MCPServer *server, const char *tool_name, cJSON *arguments);

/*
 * Free MCP tool result
 */
void mcp_free_tool_result(MCPToolResult *result);

/*
 * Get JSON schema for a tool from an MCP server
 * Returns: cJSON* (must be freed by caller), NULL if not found
 */
cJSON* mcp_get_tool_schema(MCPServer *server, const char *tool_name);

/*
 * Get all tools from all connected servers as Claude API tool definitions
 * Returns: cJSON array of tool definitions (must be freed by caller)
 */
cJSON* mcp_get_all_tools(MCPConfig *config);

/*
 * Find which server provides a given tool
 * Returns: MCPServer* or NULL if not found
 */
MCPServer* mcp_find_tool_server(MCPConfig *config, const char *tool_name);

/*
 * Check if MCP is enabled
 * Returns: 1 if enabled, 0 if disabled
 */
int mcp_is_enabled(void);

/*
 * Get MCP server status (for debugging/logging)
 * Returns: Human-readable status string (must be freed by caller)
 */
char* mcp_get_status(MCPConfig *config);

/*
 * List resources from MCP servers
 *
 * Parameters:
 *   config: MCP configuration with connected servers
 *   server_name: Optional server name to filter by (NULL for all servers)
 *
 * Returns: MCPResourceList* (must be freed with mcp_free_resource_list)
 */
MCPResourceList* mcp_list_resources(MCPConfig *config, const char *server_name);

/*
 * Read a resource from an MCP server
 *
 * Parameters:
 *   config: MCP configuration with connected servers
 *   server_name: Name of the server to read from
 *   uri: URI of the resource to read
 *
 * Returns: MCPResourceContent* (must be freed with mcp_free_resource_content)
 */
MCPResourceContent* mcp_read_resource(MCPConfig *config, const char *server_name, const char *uri);

/*
 * Free MCP resource list
 */
void mcp_free_resource_list(MCPResourceList *list);

/*
 * Free MCP resource content
 */
void mcp_free_resource_content(MCPResourceContent *content);

#ifdef TEST_BUILD
/*
 * Test-only: Create directory recursively (like mkdir -p)
 * Returns: 0 on success, -1 on error
 */
int mcp_mkdir_p(const char *path);
#endif

#endif // MCP_H
