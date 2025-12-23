/*
 * klawed_internal.h - Internal API for Claude Code modules
 *
 * Shared types and functions used across modules
 */

#ifndef KLAWED_INTERNAL_H
#define KLAWED_INTERNAL_H

#include <cjson/cJSON.h>
#include <pthread.h>
#include <signal.h>
#include "version.h"

#ifdef HAVE_ZMQ
#include "zmq_socket.h"
#endif

#include "sqlite_queue.h"

// ============================================================================
// Configuration Constants
// ============================================================================

// Use centralized version from version.h
#define VERSION KLAWED_VERSION

// API Configuration - defaults can be overridden by environment variables
// Note: For OpenAI, the provider will automatically append "/v1/chat/completions" if needed
// unless the URL already contains a versioned endpoint (e.g., /v1/, /v2/, /v3/, /v4/)
#define API_BASE_URL "https://api.openai.com"
#define DEFAULT_MODEL "o4-mini"
#define MAX_TOKENS 16384
#define MAX_TOOLS 10
#define BUFFER_SIZE 8192
#define BASH_OUTPUT_MAX_SIZE 12228  // 12,228 byte limit for bash output
#define MAX_MESSAGES 10000

// Retry configuration for rate limiting (429 errors)
#define MAX_RETRY_DURATION_MS 600000     // Maximum retry duration (10 minutes)
#define INITIAL_BACKOFF_MS 1000          // Initial backoff delay in milliseconds
#define MAX_BACKOFF_MS 60000             // Maximum backoff delay in milliseconds (60 seconds)
#define BACKOFF_MULTIPLIER 2.0           // Exponential backoff multiplier

// ============================================================================
// Forward Declarations
// ============================================================================

// PersistenceDB is defined in persistence.h (or stubbed in TEST_BUILD)
struct PersistenceDB;

// TodoList is defined in todo.h
struct TodoList;

// BedrockConfig is defined in aws_bedrock.h (opaque pointer)
struct BedrockConfigStruct;

// MCPConfig is defined in mcp.h (opaque pointer)
struct MCPConfig;

// Provider is defined in provider.h
typedef struct Provider Provider;

// TUIState is an incomplete type (defined in tui.h)
// We only use pointers to it, so forward declaration is sufficient
typedef struct TUIStateStruct TUIState;

// ============================================================================
// Enums
// ============================================================================

typedef enum {
    MSG_USER,
    MSG_ASSISTANT,
    MSG_SYSTEM
} MessageRole;

// ============================================================================
// Internal (Vendor-Agnostic) Content Types
// ============================================================================

/**
 * Internal content types - vendor-agnostic representation
 * These are converted to/from provider-specific formats (OpenAI, Anthropic, etc.)
 */
typedef enum {
    INTERNAL_TEXT,           // Plain text content
    INTERNAL_TOOL_CALL,      // Agent requesting tool execution
    INTERNAL_TOOL_RESPONSE,  // Result from tool execution
    INTERNAL_IMAGE           // Image content for upload
} InternalContentType;

// ============================================================================
// Structs
// ============================================================================

/**
 * Internal content representation (vendor-agnostic)
 * Providers convert this to/from their specific API formats
 */
typedef struct {
    InternalContentType type;

    // For all types
    char *text;              // Plain text (for INTERNAL_TEXT) or NULL

    // For INTERNAL_TOOL_CALL and INTERNAL_TOOL_RESPONSE
    char *tool_id;           // Unique ID for this tool call/response
    char *tool_name;         // Tool name (e.g., "Bash", "Read", "Write")
    cJSON *tool_params;      // Tool parameters (for TOOL_CALL)
    cJSON *tool_output;      // Tool execution result (for TOOL_RESPONSE)
    int is_error;            // Whether tool execution failed (for TOOL_RESPONSE)

    // For INTERNAL_IMAGE
    char *image_path;        // Path to the image file
    char *mime_type;         // MIME type of the image
    char *base64_data;       // Base64 encoded image data
    size_t image_size;       // Size of the image in bytes
} InternalContent;

/**
 * Vendor-agnostic tool call representation
 * Extracted from provider-specific response formats
 */
typedef struct {
    char *id;                // Unique ID for this tool call
    char *name;              // Tool name (e.g., "Bash", "Read", "Write")
    cJSON *parameters;       // Tool parameters (owned by this struct, must be freed)
} ToolCall;

/**
 * Vendor-agnostic assistant message representation
 * Contains text content from the assistant's response
 */
typedef struct {
    char *text;              // Text content (may be NULL if only tools, owned by this struct)
} AssistantMessage;

/**
 * Vendor-agnostic API response
 * Returned by call_api() - contains parsed tools and assistant message
 */
typedef struct {
    AssistantMessage message;  // Assistant's text response
    ToolCall *tools;          // Array of tool calls (NULL if no tools)
    int tool_count;           // Number of tool calls
    cJSON *raw_response;      // Raw response for adding to history (owned, must be freed)
    char *error_message;      // Error message if API call failed (owned, must be freed)
} ApiResponse;

/**
 * Internal message representation (vendor-agnostic)
 * Contains one or more content blocks
 */
typedef struct {
    MessageRole role;
    InternalContent *contents;
    int content_count;
} InternalMessage;

// ============================================================================
// Legacy Types (Deprecated - for backward compatibility during migration)
// ============================================================================

typedef enum {
    CONTENT_TEXT,
    CONTENT_TOOL_USE,
    CONTENT_TOOL_RESULT
} ContentType;

typedef struct {
    ContentType type;
    char *text;              // For TEXT
    char *tool_use_id;       // For TOOL_USE and TOOL_RESULT
    char *tool_name;         // For TOOL_USE
    cJSON *tool_input;       // For TOOL_USE
    cJSON *tool_result;      // For TOOL_RESULT
    int is_error;            // For TOOL_RESULT
} ContentBlock;

typedef struct {
    MessageRole role;
    ContentBlock *content;
    int content_count;
} Message;
// Forward declarations for subagent types (full definitions in subagent_manager.h)
typedef struct SubagentProcess SubagentProcess;
typedef struct SubagentManager SubagentManager;

typedef struct ConversationState {
    InternalMessage messages[MAX_MESSAGES];  // Vendor-agnostic internal format
    int count;
    char *api_key;
    char *api_url;
    char *model;
    char *working_dir;
    char **additional_dirs;         // Array of additional working directory paths
    int additional_dirs_count;      // Number of additional directories
    int additional_dirs_capacity;   // Capacity of additional_dirs array
    char *session_id;               // Unique session identifier for this conversation
    struct PersistenceDB *persistence_db;  // For logging API calls to SQLite
    struct TodoList *todo_list;     // Task tracking list
    Provider *provider;             // API provider abstraction (OpenAI, Bedrock, etc.)
    int max_retry_duration_ms;      // Maximum retry duration in milliseconds (configurable via env var)
    pthread_mutex_t conv_mutex;     // Synchronize access to conversation data
    int conv_mutex_initialized;     // Tracks mutex initialization
    volatile sig_atomic_t interrupt_requested;  // Flag to interrupt ongoing API calls
    struct MCPConfig *mcp_config;   // MCP server configuration (NULL if not enabled)

    // Planning mode flag
    int plan_mode;                  // Whether planning mode is enabled

    // TUI reference for streaming updates (NULL if not using TUI)
    TUIState *tui;                  // TUI state for real-time streaming display

#ifdef HAVE_ZMQ
    // ZMQ socket context for IPC communication
    struct ZMQContext *zmq_context; // ZMQ socket context (NULL if not using ZMQ)
#endif

    // SQLite queue context for IPC communication
    SQLiteQueueContext *sqlite_queue_context; // SQLite queue context (NULL if not using SQLite queue)

    // Subagent process management
    SubagentManager *subagent_manager;  // Tracks running subagent processes
} ConversationState;

// ============================================================================
// Function Declarations
// ============================================================================

/**
 * Add a directory to the additional working directories list
 * Returns: 0 on success, -1 on error
 */
int add_directory(ConversationState *state, const char *path);

/**
 * Add a user message to the conversation
 */
void add_user_message(ConversationState *state, const char *text);

/**
 * Clear conversation history (keeps system message)
 */
void clear_conversation(ConversationState *state);

// Free all messages and their contents (including system message). Use at program shutdown.
void conversation_free(ConversationState *state);

/**
 * Build system prompt with environment context
 * Returns: Newly allocated string (caller must free), or NULL on error
 */
char* build_system_prompt(ConversationState *state);

/**
 * Add tool results to conversation state
 * Returns: 0 on success, -1 on error
 */
int add_tool_results(ConversationState *state, InternalContent *results, int count);

/**
 * Check if a tool is allowed (present in the tools list sent to API)
 * Returns: 1 if allowed, 0 if not allowed
 */
int is_tool_allowed(const char *tool_name, ConversationState *state);

/**
 * Execute a tool with given input
 * Returns: JSON result object (caller must free with cJSON_Delete)
 */
cJSON* execute_tool(const char *tool_name, cJSON *input, ConversationState *state);

/**
 * Add assistant message from OpenAI format JSON
 */
void add_assistant_message_openai(ConversationState *state, cJSON *message);

/**
 * Check for ESC key press without blocking
 * Returns: 1 if ESC was pressed, 0 otherwise
 */
int check_for_esc(void);

/**
 * Build request JSON from conversation state (in OpenAI format)
 * Used by providers to get the request body with messages, tools, and cache markers
 * Returns: Newly allocated JSON string (caller must free), or NULL on error
 */
char* build_request_json_from_state(ConversationState *state);

/**
 * Initialize conversation state synchronization primitives.
 * Must be called before using ConversationState from multiple threads.
 * Returns 0 on success, -1 on failure.
 */
int conversation_state_init(ConversationState *state);

/**
 * Destroy synchronization primitives for a ConversationState.
 * Safe to call on partially initialized structures.
 */
void conversation_state_destroy(ConversationState *state);

/**
 * Acquire the conversation mutex, initializing it if needed.
 * Returns 0 on success, -1 on failure.
 */
int conversation_state_lock(ConversationState *state);

/**
 * Release the conversation mutex if initialized.
 */
void conversation_state_unlock(ConversationState *state);

/**
 * Free an ApiResponse structure and all its owned resources
 */
void api_response_free(ApiResponse *response);

/**
 * Get tool definitions for the API request
 * enable_caching: whether to add cache_control markers
 * Returns: cJSON array of tool definitions (caller must free)
 */
// Add cache_control marker to a content block
void add_cache_control(cJSON *obj);

// Get tool definitions for the API request
cJSON* get_tool_definitions(ConversationState *state, int enable_caching);

/**
 * Extract and accumulate token usage from API response
 * Updates the token counters in ConversationState
 *
 * Parameters:
 *   state: Conversation state to update
 *   raw_response: Raw JSON response string from API
 */

/**
 * Call API with retry logic (generic wrapper around provider->call_api)
 * Handles rate limiting, network errors, and other transient failures
 *
 * Parameters:
 *   state: Conversation state with messages and provider configuration
 * Returns: ApiResponse* on success (caller must free with api_response_free),
 *          NULL on fatal error
 */
ApiResponse* call_api_with_retries(ConversationState *state);


#endif // KLAWED_INTERNAL_H
