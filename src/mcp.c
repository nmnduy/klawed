/*
 * mcp.c - Model Context Protocol (MCP) client implementation
 *
 * This implements a JSON-RPC 2.0 client for communicating with MCP servers.
 * Supports stdio transport (process spawning) and basic server management.
 */

#ifndef __APPLE__
    #define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#ifdef __APPLE__
#include <spawn.h>
/* For posix_spawn file actions */
extern char **environ;
#endif
#include <cjson/cJSON.h>
#include <bsd/string.h>
#include "mcp.h"
#include "base64.h"
#include "data_dir.h"

#ifndef TEST_BUILD
#include "logger.h"
#else
// Stub logger for test builds
#define LOG_INFO(...)
#define LOG_DEBUG(...)
#define LOG_WARN(...)
#define LOG_ERROR(...)
#endif

// Global MCP state
static int mcp_initialized = 0;
static int mcp_enabled = 0;

// Initial buffer size for MCP responses (4KB, grows as needed)
#define MCP_INITIAL_BUFFER_SIZE 4096
// Maximum buffer size for MCP responses (16MB to handle very large base64 images)
#define MCP_MAX_BUFFER_SIZE (16 * 1024 * 1024)

// Forward declarations
static int is_complete_json(const char *buf, size_t len);

/*
 * Create directory recursively (like mkdir -p)
 * Only used for test builds - production uses data_dir_ensure()
 */
#ifdef TEST_BUILD
int mcp_mkdir_p(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    // Remove trailing slash
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    // Create directories recursively
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    // Create final directory
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}
#endif /* TEST_BUILD */

/*
 * Initialize MCP subsystem
 */
int mcp_init(void) {
    if (mcp_initialized) {
        return 0;
    }

    // Disabled by default; allow opt-in via KLAWED_MCP_ENABLED=1/true/on
    const char *enabled = getenv("KLAWED_MCP_ENABLED");
    if (enabled && (strcmp(enabled, "1") == 0 || strcasecmp(enabled, "true") == 0 || strcasecmp(enabled, "on") == 0)) {
        mcp_enabled = 1;
        LOG_INFO("MCP subsystem initialized and enabled");
    } else {
        mcp_enabled = 0;
        LOG_DEBUG("MCP subsystem initialized but disabled (set KLAWED_MCP_ENABLED=1 to enable)");
    }

    mcp_initialized = 1;
    return 0;
}

/*
 * Clean up MCP subsystem
 */
void mcp_cleanup(void) {
    if (!mcp_initialized) {
        return;
    }

    mcp_initialized = 0;
    mcp_enabled = 0;
    LOG_DEBUG("MCP subsystem cleaned up");
}

/*
 * Check if MCP is enabled
 */
int mcp_is_enabled(void) {
    return mcp_enabled;
}

/*
 * Load MCP server configuration from JSON file
 */
MCPConfig* mcp_load_config(const char *config_path) {
    FILE *fp = NULL;
    char *content = NULL;
    long file_size = 0;
    cJSON *root = NULL;
    cJSON *servers_obj = NULL;
    MCPConfig *config = NULL;
    char *allocated_path = NULL;

    if (!config_path) {
        // Try default locations
        const char *home = getenv("HOME");
        if (!home) {
            LOG_ERROR("MCP: Cannot determine HOME directory");
            return NULL;
        }

        // Allocate on heap to avoid stack-use-after-scope
        allocated_path = malloc(1024);
        if (!allocated_path) {
            LOG_ERROR("MCP: Failed to allocate memory for default path");
            return NULL;
        }
        snprintf(allocated_path, 1024, "%s/.config/klawed/mcp_servers.json", home);
        config_path = allocated_path;
    }

    // Check if file exists
    if (access(config_path, F_OK) != 0) {
        LOG_DEBUG("MCP: Config file not found: %s", config_path);
        goto cleanup;
    }

    LOG_INFO("MCP: Loading configuration from %s", config_path);

    // Read file
    fp = fopen(config_path, "r");
    if (!fp) {
        LOG_ERROR("MCP: Failed to open config file: %s", strerror(errno));
        goto cleanup;
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 1024 * 1024) {  // Max 1MB config
        LOG_ERROR("MCP: Invalid config file size: %ld", file_size);
        fclose(fp);
        fp = NULL;
        goto cleanup;
    }

    content = malloc((size_t)file_size + 1);
    if (!content) {
        LOG_ERROR("MCP: Failed to allocate memory for config");
        fclose(fp);
        fp = NULL;
        goto cleanup;
    }

    size_t read_size = fread(content, 1, (size_t)file_size, fp);
    fclose(fp);
    fp = NULL;

    if ((long)read_size != file_size) {
        LOG_ERROR("MCP: Failed to read config file");
        free(content);
        content = NULL;
        goto cleanup;
    }
    content[file_size] = '\0';

    // Parse JSON
    root = cJSON_Parse(content);
    free(content);
    content = NULL;

    if (!root) {
        LOG_ERROR("MCP: Failed to parse config JSON: %s", cJSON_GetErrorPtr());
        goto cleanup;
    }

    // Get mcpServers object
    servers_obj = cJSON_GetObjectItem(root, "mcpServers");
    if (!servers_obj || !cJSON_IsObject(servers_obj)) {
        LOG_ERROR("MCP: Config missing 'mcpServers' object");
        cJSON_Delete(root);
        root = NULL;
        goto cleanup;
    }

    // Count servers
    int server_count = cJSON_GetArraySize(servers_obj);
    if (server_count <= 0) {
        LOG_WARN("MCP: No servers configured");
        cJSON_Delete(root);
        root = NULL;
        goto cleanup;
    }

    // Allocate config
    config = calloc(1, sizeof(MCPConfig));
    if (!config) {
        LOG_ERROR("MCP: Failed to allocate config");
        cJSON_Delete(root);
        root = NULL;
        goto cleanup;
    }

    config->servers = calloc((size_t)server_count, sizeof(MCPServer*));
    if (!config->servers) {
        LOG_ERROR("MCP: Failed to allocate server array");
        free(config);
        config = NULL;
        cJSON_Delete(root);
        root = NULL;
        goto cleanup;
    }

    // Parse each server
    int idx = 0;
    cJSON *server_item = NULL;
    cJSON_ArrayForEach(server_item, servers_obj) {
        const char *server_name = server_item->string;
        if (!server_name) continue;

        MCPServer *server = calloc(1, sizeof(MCPServer));
        if (!server) {
            LOG_ERROR("MCP: Failed to allocate server");
            continue;
        }

        server->name = strdup(server_name);
        server->transport = MCP_TRANSPORT_STDIO;  // Default to stdio
        server->stdin_fd = -1;
        server->stdout_fd = -1;
        server->connected = 0;
        server->message_id = 1;

        // Parse command
        cJSON *command = cJSON_GetObjectItem(server_item, "command");
        if (command && cJSON_IsString(command)) {
            server->command = strdup(command->valuestring);
        }

        // Parse args
        cJSON *args = cJSON_GetObjectItem(server_item, "args");
        if (args && cJSON_IsArray(args)) {
            server->args_count = cJSON_GetArraySize(args);
            server->args = calloc((size_t)server->args_count, sizeof(char*));
            if (server->args) {
                int i = 0;
                cJSON *arg = NULL;
                cJSON_ArrayForEach(arg, args) {
                    if (cJSON_IsString(arg)) {
                        server->args[i++] = strdup(arg->valuestring);
                    }
                }
            }
        }

        // Parse cwd (working directory)
        cJSON *cwd = cJSON_GetObjectItem(server_item, "cwd");
        if (cwd && cJSON_IsString(cwd)) {
            server->cwd = strdup(cwd->valuestring);
        }

        // Parse env
        cJSON *env = cJSON_GetObjectItem(server_item, "env");
        if (env && cJSON_IsObject(env)) {
            server->env_count = cJSON_GetArraySize(env);
            if (server->env_count > 0) {
                server->env = calloc((size_t)server->env_count, sizeof(char*));
                if (server->env) {
                    int i = 0;
                    cJSON *env_item = NULL;
                    cJSON_ArrayForEach(env_item, env) {
                        const char *key = env_item->string;
                        if (key && cJSON_IsString(env_item)) {
                            char env_str[1024];
                            snprintf(env_str, sizeof(env_str), "%s=%s", key, env_item->valuestring);
                            server->env[i] = strdup(env_str);
                            if (!server->env[i]) {
                                LOG_WARN("MCP: Failed to allocate env string, skipping");
                                continue;
                            }
                            i++;
                        }
                    }
                }
            }
        }

        // Parse timeouts with defaults
        server->init_timeout = 10;      // Default: 10 seconds for initialization
        server->request_timeout = 30;   // Default: 30 seconds for requests

        cJSON *init_timeout = cJSON_GetObjectItem(server_item, "initTimeout");
        if (init_timeout && cJSON_IsNumber(init_timeout)) {
            server->init_timeout = init_timeout->valueint;
            if (server->init_timeout < 0) {
                server->init_timeout = 0;  // 0 means no timeout
            }
        }

        cJSON *request_timeout = cJSON_GetObjectItem(server_item, "requestTimeout");
        if (request_timeout && cJSON_IsNumber(request_timeout)) {
            server->request_timeout = request_timeout->valueint;
            if (server->request_timeout < 0) {
                server->request_timeout = 0;  // 0 means no timeout
            }
        }

        // Check for environment variable overrides
        const char *env_init_timeout = getenv("KLAWED_MCP_INIT_TIMEOUT");
        if (env_init_timeout) {
            int timeout_val = atoi(env_init_timeout);
            if (timeout_val >= 0) {
                server->init_timeout = timeout_val;
                LOG_DEBUG("MCP: Overriding init_timeout for server '%s' to %d seconds via KLAWED_MCP_INIT_TIMEOUT",
                         server->name, server->init_timeout);
            }
        }

        const char *env_request_timeout = getenv("KLAWED_MCP_REQUEST_TIMEOUT");
        if (env_request_timeout) {
            int timeout_val = atoi(env_request_timeout);
            if (timeout_val >= 0) {
                server->request_timeout = timeout_val;
                LOG_DEBUG("MCP: Overriding request_timeout for server '%s' to %d seconds via KLAWED_MCP_REQUEST_TIMEOUT",
                         server->name, server->request_timeout);
            }
        }

        config->servers[idx++] = server;
        LOG_INFO("MCP: Configured server '%s' (command: %s)", server->name, server->command ? server->command : "none");
    }

    config->server_count = idx;
    cJSON_Delete(root);
    root = NULL;

    LOG_INFO("MCP: Loaded %d server(s) from config", config->server_count);
    // Debug summary of configured servers for local troubleshooting
    LOG_DEBUG("MCP: Configured servers summary (logging to help debug)");
    for (int i = 0; i < config->server_count; i++) {
        MCPServer *s = config->servers[i];
        if (!s) continue;
        char args_buf[768] = {0};
        size_t off = 0;
        for (int a = 0; a < s->args_count && off + 2 < sizeof(args_buf); a++) {
            const char *arg = s->args[a] ? s->args[a] : "";
            size_t n = strlen(arg);
            if (off + n + 1 >= sizeof(args_buf)) break;
            memcpy(args_buf + off, arg, n);
            off += n;
            if (a < s->args_count - 1 && off + 1 < sizeof(args_buf)) {
                args_buf[off++] = ' ';
            }
        }
        args_buf[off] = '\0';
        LOG_DEBUG("  - %s: cmd='%s'%s%s%s%s%s%s init_timeout=%d request_timeout=%d",
                  s->name ? s->name : "<noname>",
                  s->command ? s->command : "<none>",
                  (s->args_count > 0 ? " args=[" : ""),
                  args_buf,
                  (s->args_count > 0 ? "]" : ""),
                  (s->cwd ? " cwd='" : ""),
                  (s->cwd ? s->cwd : ""),
                  (s->cwd ? "'" : ""),
                  s->init_timeout,
                  s->request_timeout);
    }

cleanup:
    // Clean up allocated resources
    if (allocated_path) {
        free(allocated_path);
    }
    if (fp) {
        fclose(fp);
    }
    if (content) {
        free(content);
    }
    if (root) {
        cJSON_Delete(root);
    }

    return config;
}

/*
 * Free MCP configuration
 */
void mcp_free_config(MCPConfig *config) {
    if (!config) return;

    for (int i = 0; i < config->server_count; i++) {
        MCPServer *server = config->servers[i];
        if (!server) continue;

        // Disconnect if connected
        if (server->connected) {
            mcp_disconnect_server(server);
        }

        free(server->name);
        free(server->command);
        free(server->cwd);
        free(server->url);

        if (server->args) {
            for (int j = 0; j < server->args_count; j++) {
                free(server->args[j]);
            }
            free(server->args);
        }

        if (server->env) {
            for (int j = 0; j < server->env_count; j++) {
                free(server->env[j]);
            }
            free(server->env);
        }

        if (server->tools) {
            for (int j = 0; j < server->tool_count; j++) {
                free(server->tools[j]);
            }
            free(server->tools);
        }

        if (server->tool_schemas) {
            cJSON_Delete(server->tool_schemas);
        }

        free(server);
    }

    free(config->servers);
    free(config);
}

/*
 * Forward declaration for stderr reading function
 */
static void mcp_read_stderr(MCPServer *server);

/*
 * Connect to an MCP server (stdio transport)
 */
int mcp_connect_server(MCPServer *server) {
    if (!server || !server->command) {
        LOG_ERROR("MCP: Invalid server or missing command");
        return -1;
    }

    if (server->connected) {
        LOG_WARN("MCP: Server '%s' already connected", server->name);
        return 0;
    }

    LOG_INFO("MCP: Connecting to server '%s'...", server->name);

    // Create pipes for stdin/stdout/stderr
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    if (pipe(stdin_pipe) < 0) {
        LOG_ERROR("MCP: Failed to create stdin pipe: %s", strerror(errno));
        return -1;
    }

    if (pipe(stdout_pipe) < 0) {
        LOG_ERROR("MCP: Failed to create stdout pipe: %s", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return -1;
    }

    if (pipe(stderr_pipe) < 0) {
        LOG_ERROR("MCP: Failed to create stderr pipe: %s", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return -1;
    }

#ifdef __APPLE__
    /*
     * macOS: Use posix_spawn() instead of fork() for thread safety.
     *
     * When fork() is called in a multi-threaded program on macOS, only the
     * calling thread survives in the child. If other threads held locks
     * (malloc, SQLite, log mutex, etc.), those locks remain locked forever
     * in the child, causing deadlocks.
     *
     * posix_spawn() avoids this by creating a new process without copying
     * the parent's memory space and mutex states.
     */
    pid_t pid;
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t spawnattr;

    int rc = posix_spawn_file_actions_init(&file_actions);
    if (rc != 0) {
        LOG_ERROR("MCP: Failed to init file actions: %s", strerror(rc));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }

    rc = posix_spawnattr_init(&spawnattr);
    if (rc != 0) {
        LOG_ERROR("MCP: Failed to init spawn attributes: %s", strerror(rc));
        posix_spawn_file_actions_destroy(&file_actions);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }

    // Set up file actions for pipe redirection
    posix_spawn_file_actions_addclose(&file_actions, stdin_pipe[1]);   // Close write end of stdin
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipe[0]);  // Close read end of stdout
    posix_spawn_file_actions_addclose(&file_actions, stderr_pipe[0]);  // Close read end of stderr
    posix_spawn_file_actions_adddup2(&file_actions, stdin_pipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, stdin_pipe[0]);   // Close original after dup
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipe[1]);  // Close original after dup
    posix_spawn_file_actions_addclose(&file_actions, stderr_pipe[1]);  // Close original after dup

    // Change working directory if provided
    // macOS 26.0+ has posix_spawn_file_actions_addchdir; older macOS only has _np
    if (server->cwd) {
#if defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && (__MAC_OS_X_VERSION_MAX_ALLOWED >= 260000)
        posix_spawn_file_actions_addchdir(&file_actions, server->cwd);
#else
        posix_spawn_file_actions_addchdir_np(&file_actions, server->cwd);
#endif
    }

    // Set spawn attributes: create new process group
    short flags = POSIX_SPAWN_SETPGROUP;
    posix_spawnattr_setflags(&spawnattr, flags);
    posix_spawnattr_setpgroup(&spawnattr, 0);  // New process group

    // Build argv
    char **argv = calloc((size_t)(server->args_count + 2), sizeof(char*));
    if (!argv) {
        posix_spawn_file_actions_destroy(&file_actions);
        posix_spawnattr_destroy(&spawnattr);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }

    argv[0] = server->command;
    for (int i = 0; i < server->args_count; i++) {
        argv[i + 1] = server->args[i];
    }
    argv[server->args_count + 1] = NULL;

    // Build custom environment if needed
    char **new_environ = environ;
    char **env_vars_to_free = NULL;
    int env_vars_count = 0;

    if (server->env_count > 0) {
        // Count current environment
        int env_count = 0;
        for (char **e = environ; *e; e++) {
            env_count++;
        }

        // Allocate new environment array
        new_environ = malloc((size_t)(env_count + server->env_count + 1) * sizeof(char *));
        if (!new_environ) {
            free(argv);
            posix_spawn_file_actions_destroy(&file_actions);
            posix_spawnattr_destroy(&spawnattr);
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            return -1;
        }

        // Copy current environment
        int idx = 0;
        for (int i = 0; i < env_count; i++) {
            new_environ[idx++] = environ[i];
        }

        // Add custom environment variables (need to dup since putenv format)
        env_vars_to_free = malloc((size_t)server->env_count * sizeof(char *));
        if (env_vars_to_free) {
            for (int i = 0; i < server->env_count; i++) {
                env_vars_to_free[i] = strdup(server->env[i]);
                if (env_vars_to_free[i]) {
                    new_environ[idx++] = env_vars_to_free[i];
                    env_vars_count++;
                }
            }
        }
        new_environ[idx] = NULL;
    }

    rc = posix_spawn(&pid, server->command, &file_actions, &spawnattr, argv, new_environ);

    // Cleanup
    free(argv);
    if (env_vars_to_free) {
        for (int i = 0; i < env_vars_count; i++) {
            free(env_vars_to_free[i]);
        }
        free(env_vars_to_free);
    }
    if (new_environ != environ) {
        free(new_environ);
    }
    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&spawnattr);

    if (rc != 0) {
        LOG_ERROR("MCP: Failed to spawn: %s", strerror(rc));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }
#else
    // Fork process
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("MCP: Failed to fork: %s", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        // Child process

        // Redirect stdin/stdout/stderr
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        // Close unused pipe ends
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);

        // Build argv
        char **argv = calloc((size_t)(server->args_count + 2), sizeof(char*));
        if (!argv) {
            exit(1);
        }

        argv[0] = server->command;
        for (int i = 0; i < server->args_count; i++) {
            argv[i + 1] = server->args[i];
        }
        argv[server->args_count + 1] = NULL;

        // Set environment if provided
        if (server->env_count > 0) {
            for (int i = 0; i < server->env_count; i++) {
                putenv(server->env[i]);
            }
        }

        // Change working directory if provided
        if (server->cwd) {
            if (chdir(server->cwd) != 0) {
                fprintf(stderr, "MCP: Failed to chdir to %s: %s\n", server->cwd, strerror(errno));
                exit(1);
            }
        }

        // Execute command
        execvp(server->command, argv);

        // If we get here, exec failed
        fprintf(stderr, "MCP: Failed to exec %s: %s\n", server->command, strerror(errno));
        exit(1);
    }
#endif

    // Parent process
    close(stdin_pipe[0]);   // Close read end of stdin pipe
    close(stdout_pipe[1]);  // Close write end of stdout pipe
    close(stderr_pipe[1]);  // Close write end of stderr pipe

    server->pid = pid;
    server->stdin_fd = stdin_pipe[1];
    server->stdout_fd = stdout_pipe[0];
    server->stderr_fd = stderr_pipe[0];
    server->connected = 1;

    // Set non-blocking mode for stdout and stderr
    int fd_flags = fcntl(server->stdout_fd, F_GETFL, 0);
    if (fd_flags >= 0) {
        fcntl(server->stdout_fd, F_SETFL, fd_flags | O_NONBLOCK);
    }

    fd_flags = fcntl(server->stderr_fd, F_GETFL, 0);
    if (fd_flags >= 0) {
        fcntl(server->stderr_fd, F_SETFL, fd_flags | O_NONBLOCK);
    }

    // Open log file for stderr output
    // Use data_dir/mcp/<server-name>.log
    char log_path[512];
    char mcp_dir[512];
    char mcp_subpath[256];
    snprintf(mcp_subpath, sizeof(mcp_subpath), "mcp/%s.log", server->name);
    if (data_dir_build_path(log_path, sizeof(log_path), mcp_subpath) != 0) {
        // Fallback: build path with default base directory
        const char *base_dir = data_dir_get_base();
        snprintf(log_path, sizeof(log_path), "%s/mcp/%s.log", base_dir, server->name);
    }

    // Create directory if it doesn't exist
    if (data_dir_build_path(mcp_dir, sizeof(mcp_dir), "mcp") != 0) {
        // Fallback for directory creation
        const char *base_dir = data_dir_get_base();
        snprintf(mcp_dir, sizeof(mcp_dir), "%s/mcp", base_dir);
    }
#ifdef TEST_BUILD
    if (mcp_mkdir_p(mcp_dir) != 0) {
#else
    if (data_dir_ensure("mcp") != 0) {
#endif
        LOG_WARN("MCP: Failed to create mcp log directory: %s", strerror(errno));
    }

    server->stderr_log = fopen(log_path, "w");
    if (server->stderr_log) {
        LOG_DEBUG("MCP: Logging stderr for '%s' to %s", server->name, log_path);
    } else {
        LOG_WARN("MCP: Failed to open stderr log file %s: %s", log_path, strerror(errno));
    }

    LOG_INFO("MCP: Connected to server '%s' (pid: %d)", server->name, server->pid);

    // Send initialize request
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(request, "id", server->message_id++);
    cJSON_AddStringToObject(request, "method", "initialize");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "protocolVersion", "2024-11-05");

    // clientInfo should be an object with name and version
    cJSON *clientInfo = cJSON_CreateObject();
    cJSON_AddStringToObject(clientInfo, "name", "klawed");
    cJSON_AddStringToObject(clientInfo, "version", "1.0");
    cJSON_AddItemToObject(params, "clientInfo", clientInfo);

    // capabilities is required (can be empty for basic client)
    cJSON *capabilities = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "capabilities", capabilities);

    cJSON_AddItemToObject(request, "params", params);

    char *request_str = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    if (request_str) {
        // Write request with newline (JSON-RPC over stdio uses line-delimited JSON)
        dprintf(server->stdin_fd, "%s\n", request_str);
        free(request_str);

        // Read initialize response
        size_t buffer_size = MCP_INITIAL_BUFFER_SIZE;
        char *buffer = malloc(buffer_size);
        size_t total_read = 0;
        int got_response = 0;

        if (!buffer) {
            LOG_ERROR("MCP: Failed to allocate initialization buffer");
            // Continue without reading response
        } else {
            // Wait for response (with timeout)
            int max_iterations = 50;  // Default: 5 seconds (50 * 100ms)
            if (server->init_timeout > 0) {
                // Calculate iterations based on init_timeout (in seconds)
                // Each iteration is 100ms, so iterations = timeout * 10
                max_iterations = server->init_timeout * 10;
            }

            for (int i = 0; i < max_iterations; i++) {
                // Read any stderr output during initialization
                mcp_read_stderr(server);

                // Check if we need to grow buffer (unlikely for init, but be safe)
                if (total_read >= buffer_size - 1) {
                    size_t new_size = buffer_size * 2;
                    if (new_size > MCP_MAX_BUFFER_SIZE) {
                        LOG_ERROR("MCP: Initialization response too large");
                        break;
                    }

                    char *new_buffer = realloc(buffer, new_size);
                    if (!new_buffer) {
                        LOG_ERROR("MCP: Failed to grow initialization buffer");
                        break;
                    }
                    buffer = new_buffer;
                    buffer_size = new_size;
                }

                ssize_t n = read(server->stdout_fd, buffer + total_read, buffer_size - total_read - 1);
                if (n > 0) {
                    total_read += (size_t)n;

                    // Check if we have a complete JSON object
                    if (is_complete_json(buffer, total_read)) {
                        got_response = 1;
                        break;
                    }
                } else if (n == 0) {
                    // EOF
                    break;
                }
                usleep(100000);  // 100ms
            }

            // Read any remaining stderr after initialization
            mcp_read_stderr(server);

            if (got_response) {
                buffer[total_read] = '\0';
                LOG_DEBUG("MCP: Initialize response: %s", buffer);

                // Send "initialized" notification to complete handshake
                cJSON *notification = cJSON_CreateObject();
                cJSON_AddStringToObject(notification, "jsonrpc", "2.0");
                cJSON_AddStringToObject(notification, "method", "notifications/initialized");
                cJSON_AddItemToObject(notification, "params", cJSON_CreateObject());

                char *notif_str = cJSON_PrintUnformatted(notification);
                cJSON_Delete(notification);

                if (notif_str) {
                    dprintf(server->stdin_fd, "%s\n", notif_str);
                    free(notif_str);
                    LOG_DEBUG("MCP: Sent initialized notification");
                }
            } else {
                LOG_WARN("MCP: No initialize response received from server '%s'", server->name);
            }

            free(buffer);
        }
    }

    return 0;
}

/*
 * Read and log stderr output from MCP server (non-blocking)
 * This helps capture debug logs and errors from the server.
 * Logs are written both to the main log and to a server-specific file.
 */
static void mcp_read_stderr(MCPServer *server) {
    if (!server || server->stderr_fd < 0) {
        return;
    }

    char buffer[4096];
    ssize_t n;

    while ((n = read(server->stderr_fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';

        // Write raw output to server's log file if open
        if (server->stderr_log) {
            fwrite(buffer, 1, (size_t)n, server->stderr_log);
            fflush(server->stderr_log);
        }

        // Also log each line to main debug log for convenience
        char *line = buffer;
        char *next_line;

        while ((next_line = strchr(line, '\n')) != NULL) {
            *next_line = '\0';
            if (strlen(line) > 0) {
                LOG_DEBUG("MCP[%s stderr]: %s", server->name, line);
            }
            line = next_line + 1;
        }

        // Log any remaining partial line
        if (strlen(line) > 0) {
            LOG_DEBUG("MCP[%s stderr]: %s", server->name, line);
        }
    }
}

/*
 * Disconnect from an MCP server
 */
void mcp_disconnect_server(MCPServer *server) {
    if (!server || !server->connected) {
        return;
    }

    LOG_INFO("MCP: Disconnecting from server '%s'", server->name);

    // Close pipes
    if (server->stdin_fd >= 0) {
        close(server->stdin_fd);
        server->stdin_fd = -1;
    }

    if (server->stdout_fd >= 0) {
        close(server->stdout_fd);
        server->stdout_fd = -1;
    }

    if (server->stderr_fd >= 0) {
        close(server->stderr_fd);
        server->stderr_fd = -1;
    }

    // Close stderr log file
    if (server->stderr_log) {
        fclose(server->stderr_log);
        server->stderr_log = NULL;
    }

    // Kill process if still running
    if (server->pid > 0) {
        kill(server->pid, SIGTERM);

        // Wait for process to exit (with timeout)
        int status = 0;
        for (int i = 0; i < 10; i++) {
            if (waitpid(server->pid, &status, WNOHANG) > 0) {
                break;
            }
            usleep(100000);  // 100ms
        }

        // Force kill if still running
        kill(server->pid, SIGKILL);
        waitpid(server->pid, &status, 0);

        server->pid = 0;
    }

    server->connected = 0;
    LOG_INFO("MCP: Disconnected from server '%s'", server->name);
}

/*
 * Check if a buffer contains a complete JSON object
 */
static int is_complete_json(const char *buf, size_t len) {
    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_string = false;
    bool escape = false;

    for (size_t i = 0; i < len; i++) {
        char c = buf[i];

        if (escape) {
            escape = false;
            continue;
        }

        if (c == '\\') {
            escape = true;
            continue;
        }

        if (c == '"' && !escape) {
            in_string = !in_string;
            continue;
        }

        if (!in_string) {
            if (c == '{') brace_depth++;
            else if (c == '}') brace_depth--;
            else if (c == '[') bracket_depth++;
            else if (c == ']') bracket_depth--;
        }
    }

    return (brace_depth == 0 && bracket_depth == 0 && !in_string && !escape);
}

/*
 * Send JSON-RPC request and read response
 */
static cJSON* mcp_send_request(MCPServer *server, const char *method, cJSON *params) {
    if (!server || !server->connected) {
        LOG_ERROR("MCP: Server not connected");
        return NULL;
    }

    // Build request
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(request, "id", server->message_id++);
    cJSON_AddStringToObject(request, "method", method);

    // Always include params field (even if empty) per JSON-RPC 2.0 spec
    if (params) {
        cJSON_AddItemToObject(request, "params", cJSON_Duplicate(params, 1));
    } else {
        cJSON_AddItemToObject(request, "params", cJSON_CreateObject());
    }

    char *request_str = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    if (!request_str) {
        LOG_ERROR("MCP: Failed to serialize request");
        return NULL;
    }

    // Send request
    LOG_DEBUG("MCP: Sending request to '%s': %s", server->name, request_str);
    dprintf(server->stdin_fd, "%s\n", request_str);
    free(request_str);

    // Read response (line-delimited JSON)
    size_t buffer_size = MCP_INITIAL_BUFFER_SIZE;
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        LOG_ERROR("MCP: Failed to allocate response buffer");
        return NULL;
    }
    size_t total_read = 0;

    // Wait for response (with timeout)
    int max_iterations = 50;  // Default: 5 seconds (50 * 100ms)
    if (server->request_timeout > 0) {
        // Calculate iterations based on request_timeout (in seconds)
        // Each iteration is 100ms, so iterations = timeout * 10
        max_iterations = server->request_timeout * 10;
    }

    for (int i = 0; i < max_iterations; i++) {
        // Read any stderr output (for logging/debugging)
        mcp_read_stderr(server);

        // Check if we need to grow buffer
        if (total_read >= buffer_size - 1) {
            // Double buffer size, but cap at maximum
            size_t new_size = buffer_size * 2;
            if (new_size > MCP_MAX_BUFFER_SIZE) {
                LOG_ERROR("MCP: Response too large (max %d bytes)", (int)MCP_MAX_BUFFER_SIZE);
                free(buffer);
                return NULL;
            }

            char *new_buffer = realloc(buffer, new_size);
            if (!new_buffer) {
                LOG_ERROR("MCP: Failed to grow response buffer to %zu bytes", new_size);
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
            buffer_size = new_size;
        }

        // Read chunk
        ssize_t n = read(server->stdout_fd, buffer + total_read, buffer_size - total_read - 1);
        if (n > 0) {
            total_read += (size_t)n;

            // Check if we have a complete JSON object
            if (is_complete_json(buffer, total_read)) {
                break;
            }
        } else if (n == 0) {
            // EOF
            break;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // Read error (not just would-block)
            LOG_ERROR("MCP: Read error from server '%s': %s", server->name, strerror(errno));
            break;
        }
        usleep(100000);  // 100ms
    }

    // Read any remaining stderr output
    mcp_read_stderr(server);

    if (total_read == 0) {
        LOG_ERROR("MCP: No response from server '%s'", server->name);
        free(buffer);
        return NULL;
    }

    buffer[total_read] = '\0';
    LOG_DEBUG("MCP: Received %zu bytes from '%s'", total_read, server->name);

    // Only log full response if it's small enough
    if (total_read < 1024) {
        LOG_DEBUG("MCP: Response: %s", buffer);
    } else {
        LOG_DEBUG("MCP: First 1KB of response: %.1024s...", buffer);
    }

    // Parse response
    cJSON *response = cJSON_Parse(buffer);

    if (!response) {
        // Show first 200 chars of response for debugging
        char preview[201];
        snprintf(preview, sizeof(preview), "%.200s", buffer);
        LOG_ERROR("MCP: Failed to parse JSON response from '%s'. First 200 chars: %s%s",
                 server->name, preview, total_read > 200 ? "..." : "");
        free(buffer);
        return NULL;
    }

    free(buffer);  // Free buffer after parsing - cJSON creates its own copy

    // Check for JSON-RPC error
    cJSON *error = cJSON_GetObjectItem(response, "error");
    if (error) {
        cJSON *message = cJSON_GetObjectItem(error, "message");
        const char *error_msg = (message && cJSON_IsString(message)) ? message->valuestring : "unknown";
        LOG_ERROR("MCP: Server returned error: %s", error_msg);
        (void)error_msg;  // Suppress unused warning when LOG_ERROR is stubbed out
        cJSON_Delete(response);
        return NULL;
    }

    return response;
}

/*
 * Discover tools from a connected MCP server
 */
int mcp_discover_tools(MCPServer *server) {
    if (!server || !server->connected) {
        LOG_ERROR("MCP: Server not connected");
        return -1;
    }

    LOG_INFO("MCP: Discovering tools from server '%s'...", server->name);

    cJSON *response = mcp_send_request(server, "tools/list", NULL);
    if (!response) {
        return -1;
    }

    // Extract tools from response
    cJSON *result = cJSON_GetObjectItem(response, "result");
    if (!result) {
        char *resp_str = cJSON_Print(response);
        LOG_ERROR("MCP: No result in tools/list response. Full response: %s", resp_str ? resp_str : "null");
        free(resp_str);
        cJSON_Delete(response);
        return -1;
    }

    cJSON *tools = cJSON_GetObjectItem(result, "tools");
    if (!tools || !cJSON_IsArray(tools)) {
        char *result_str = cJSON_Print(result);
        LOG_ERROR("MCP: Invalid tools array in response. Result: %s", result_str ? result_str : "null");
        free(result_str);
        cJSON_Delete(response);
        return -1;
    }

    int tool_count = cJSON_GetArraySize(tools);
    if (tool_count <= 0) {
        LOG_INFO("MCP: Server '%s' provides no tools", server->name);
        cJSON_Delete(response);
        return 0;
    }

    // Store tool names
    server->tool_count = tool_count;
    server->tools = calloc((size_t)tool_count, sizeof(char*));
    server->tool_schemas = cJSON_Duplicate(tools, 1);

    if (!server->tools) {
        LOG_ERROR("MCP: Failed to allocate tool array");
        cJSON_Delete(response);
        return -1;
    }

    int idx = 0;
    cJSON *tool = NULL;
    cJSON_ArrayForEach(tool, tools) {
        cJSON *name = cJSON_GetObjectItem(tool, "name");
        if (name && cJSON_IsString(name)) {
            server->tools[idx] = strdup(name->valuestring);
            if (!server->tools[idx]) {
                LOG_WARN("MCP: Failed to allocate tool name, skipping");
                continue;
            }
            LOG_INFO("MCP: Discovered tool '%s' from server '%s'", name->valuestring, server->name);
            idx++;
        }
    }

    cJSON_Delete(response);
    LOG_INFO("MCP: Discovered %d tool(s) from server '%s'", idx, server->name);
    return idx;
}

/*
 * Call an MCP tool
 */
MCPToolResult* mcp_call_tool(MCPServer *server, const char *tool_name, cJSON *arguments) {
    if (!server || !server->connected || !tool_name) {
        LOG_ERROR("MCP: Invalid parameters for tool call");
        MCPToolResult *error_result = calloc(1, sizeof(MCPToolResult));
        if (error_result) {
            error_result->tool_name = strdup(tool_name ? tool_name : "unknown");
            error_result->is_error = 1;
            error_result->result = strdup("MCP: Invalid parameters (server not connected or no tool name)");
        }
        return error_result;
    }

    LOG_INFO("MCP: Calling tool '%s' on server '%s'", tool_name, server->name);

    // Build params
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", tool_name);

    if (arguments) {
        cJSON_AddItemToObject(params, "arguments", cJSON_Duplicate(arguments, 1));
    }

    cJSON *response = mcp_send_request(server, "tools/call", params);
    cJSON_Delete(params);

    if (!response) {
        MCPToolResult *error_result = calloc(1, sizeof(MCPToolResult));
        if (error_result) {
            error_result->tool_name = strdup(tool_name);
            error_result->is_error = 1;
            error_result->result = strdup("MCP: No response from server (timeout or connection error)");
        }
        return error_result;
    }

    // Extract result
    cJSON *result_obj = cJSON_GetObjectItem(response, "result");
    if (!result_obj) {
        LOG_ERROR("MCP: No result in tools/call response");
        MCPToolResult *error_result = calloc(1, sizeof(MCPToolResult));
        if (error_result) {
            error_result->tool_name = strdup(tool_name);
            error_result->is_error = 1;
            error_result->result = strdup("MCP: Invalid response from server (no result field)");
        }
        cJSON_Delete(response);
        return error_result;
    }

    MCPToolResult *result = calloc(1, sizeof(MCPToolResult));
    if (!result) {
        cJSON_Delete(response);
        MCPToolResult *error_result = calloc(1, sizeof(MCPToolResult));
        if (error_result) {
            error_result->tool_name = strdup(tool_name);
            error_result->is_error = 1;
            error_result->result = strdup("MCP: Memory allocation failed");
        }
        return error_result;
    }

    result->tool_name = strdup(tool_name);
    result->is_error = 0;
    result->blob = NULL;
    result->blob_size = 0;
    result->mime_type = NULL;

    // MCP returns content array with different content types
    cJSON *content = cJSON_GetObjectItem(result_obj, "content");
    if (content && cJSON_IsArray(content)) {
        // Process each content item
        cJSON *item = NULL;

        cJSON_ArrayForEach(item, content) {
            // Check content type first
            cJSON *content_type = cJSON_GetObjectItem(item, "type");

            // Handle text content (type: 'text' or legacy 'text' field)
            cJSON *text = cJSON_GetObjectItem(item, "text");
            if (text && cJSON_IsString(text)) {
                // Handle text content
                if (!result->result) {
                    result->result = strdup(text->valuestring);
                } else {
                    // Append to existing text with newline
                    size_t new_len = strlen(result->result) + strlen(text->valuestring) + 2;
                    char *new_result = realloc(result->result, new_len);
                    if (new_result) {
                        result->result = new_result;
                        // Use strlcat for safety
                        strlcat(result->result, "\n", new_len);
                        strlcat(result->result, text->valuestring, new_len);
                    }
                }
            }

            // Handle image content (type: 'image' with data field)
            if (content_type && cJSON_IsString(content_type) &&
                strcmp(content_type->valuestring, "image") == 0) {
                cJSON *image_data = cJSON_GetObjectItem(item, "data");
                if (image_data && cJSON_IsString(image_data)) {
                    // Handle image content (base64 encoded)
                    const char *image_str = image_data->valuestring;
                    size_t image_len = strlen(image_str);

                    // Decode base64
                    size_t decoded_size = 0;
                    unsigned char *decoded_data = base64_decode(image_str, image_len, &decoded_size);
                    if (decoded_data) {
                        result->blob = decoded_data;
                        result->blob_size = decoded_size;
                        LOG_DEBUG("MCP: Image content received and decoded (encoded size: %zu, decoded size: %zu)", image_len, decoded_size);
                    } else {
                        LOG_WARN("MCP: Failed to decode base64 image content");
                        // Fallback: store as-is
                        result->blob = malloc(image_len);
                        if (result->blob) {
                            memcpy(result->blob, image_str, image_len);
                            result->blob_size = image_len;
                            LOG_DEBUG("MCP: Image content stored as-is (size: %zu)", image_len);
                        }
                    }
                }
            }

            // Handle blob (binary) content (legacy format)
            cJSON *blob = cJSON_GetObjectItem(item, "blob");
            if (blob && cJSON_IsString(blob) && !result->blob) {
                // Handle binary content (base64 encoded)
                const char *blob_str = blob->valuestring;
                size_t blob_len = strlen(blob_str);

                // Decode base64
                size_t decoded_size = 0;
                unsigned char *decoded_data = base64_decode(blob_str, blob_len, &decoded_size);
                if (decoded_data) {
                    result->blob = decoded_data;
                    result->blob_size = decoded_size;
                    LOG_DEBUG("MCP: Binary blob content received and decoded (encoded size: %zu, decoded size: %zu)", blob_len, decoded_size);
                } else {
                    LOG_WARN("MCP: Failed to decode base64 blob content");
                    // Fallback: store as-is
                    result->blob = malloc(blob_len);
                    if (result->blob) {
                        memcpy(result->blob, blob_str, blob_len);
                        result->blob_size = blob_len;
                        LOG_DEBUG("MCP: Binary blob content stored as-is (size: %zu)", blob_len);
                    }
                }
            }

            // Check for MIME type
            cJSON *mime_type = cJSON_GetObjectItem(item, "mimeType");
            if (mime_type && cJSON_IsString(mime_type) && !result->mime_type) {
                result->mime_type = strdup(mime_type->valuestring);
            }
        }
    }

    // Check for isError flag
    cJSON *is_error = cJSON_GetObjectItem(result_obj, "isError");
    if (is_error && cJSON_IsBool(is_error) && cJSON_IsTrue(is_error)) {
        result->is_error = 1;
    }

    cJSON_Delete(response);
    LOG_INFO("MCP: Tool call '%s' completed %s", tool_name, result->is_error ? "(with error)" : "successfully");

    return result;
}

/*
 * Free MCP tool result
 */
void mcp_free_tool_result(MCPToolResult *result) {
    if (!result) return;

    free(result->tool_name);
    free(result->result);
    free(result->mime_type);
    if (result->blob) {
        free(result->blob);
    }
    free(result);
}

/*
 * Get JSON schema for a tool from an MCP server
 */
cJSON* mcp_get_tool_schema(MCPServer *server, const char *tool_name) {
    if (!server || !tool_name || !server->tool_schemas) {
        return NULL;
    }

    cJSON *tool = NULL;
    cJSON_ArrayForEach(tool, server->tool_schemas) {
        cJSON *name = cJSON_GetObjectItem(tool, "name");
        if (name && cJSON_IsString(name) && strcmp(name->valuestring, tool_name) == 0) {
            return cJSON_Duplicate(tool, 1);
        }
    }

    return NULL;
}

/*
 * Get all tools from all connected servers as Claude API tool definitions
 */
cJSON* mcp_get_all_tools(MCPConfig *config) {
    if (!config) {
        return NULL;
    }

    // Return an array of Claude API tool definitions: { type: "function", function: { name, description, parameters } }
    cJSON *tools_array = cJSON_CreateArray();
    if (!tools_array) {
        return NULL;
    }

    for (int i = 0; i < config->server_count; i++) {
        MCPServer *server = config->servers[i];
        if (!server || !server->connected || !server->tool_schemas) {
            continue;
        }

        cJSON *tool = NULL;
        cJSON_ArrayForEach(tool, server->tool_schemas) {
            // Prepare Claude tool definition wrapper
            cJSON *tool_def = cJSON_CreateObject();
            if (!tool_def) continue;
            cJSON_AddStringToObject(tool_def, "type", "function");

            cJSON *func = cJSON_CreateObject();
            if (!func) { cJSON_Delete(tool_def); continue; }

            // Name with mcp_<server>_<tool> prefix
            const cJSON *name = cJSON_GetObjectItem(tool, "name");
            if (!name || !cJSON_IsString(name)) { cJSON_Delete(tool_def); cJSON_Delete(func); continue; }
            char prefixed_name[256];
            snprintf(prefixed_name, sizeof(prefixed_name), "mcp_%s_%s", server->name, name->valuestring);
            cJSON_AddStringToObject(func, "name", prefixed_name);

            // Description if present
            const cJSON *desc = cJSON_GetObjectItem(tool, "description");
            if (desc && cJSON_IsString(desc)) {
                cJSON_AddStringToObject(func, "description", desc->valuestring);
            }

            // Parameters: map from MCP tool's input schema to Claude parameters
            // Try common keys from MCP servers: inputSchema, input_schema, parameters
            const cJSON *input_schema = cJSON_GetObjectItem(tool, "inputSchema");
            if (!input_schema) input_schema = cJSON_GetObjectItem(tool, "input_schema");
            if (!input_schema) input_schema = cJSON_GetObjectItem(tool, "parameters");

            if (input_schema && (cJSON_IsObject(input_schema) || cJSON_IsArray(input_schema))) {
                // Duplicate schema as-is under "parameters"
                cJSON_AddItemToObject(func, "parameters", cJSON_Duplicate(input_schema, 1));
            } else {
                // Fallback to an empty object schema
                cJSON *empty_params = cJSON_CreateObject();
                cJSON_AddStringToObject(empty_params, "type", "object");
                cJSON_AddItemToObject(func, "parameters", empty_params);
            }

            cJSON_AddItemToObject(tool_def, "function", func);
            cJSON_AddItemToArray(tools_array, tool_def);
        }
    }

    return tools_array;
}

/*
 * Find which server provides a given tool
 */
MCPServer* mcp_find_tool_server(MCPConfig *config, const char *tool_name) {
    if (!config || !tool_name) {
        return NULL;
    }

    // Check if tool name has mcp_ prefix
    if (strncmp(tool_name, "mcp_", 4) != 0) {
        return NULL;
    }

    // Extract server name (format: mcp_<server>_<tool>)
    char server_name[128] = {0};
    const char *underscore = strchr(tool_name + 4, '_');
    if (!underscore) {
        return NULL;
    }

    size_t len = (size_t)(underscore - (tool_name + 4));
    if (len >= sizeof(server_name)) {
        return NULL;
    }

    strlcpy(server_name, tool_name + 4, len + 1);

    // Find server
    for (int i = 0; i < config->server_count; i++) {
        MCPServer *server = config->servers[i];
        if (server && server->name && strcmp(server->name, server_name) == 0) {
            return server;
        }
    }

    return NULL;
}

/*
 * Get MCP server status
 */
char* mcp_get_status(MCPConfig *config) {
    if (!config) {
        char *msg = strdup("MCP: Not configured");
        if (!msg) {
            return NULL;
        }
        return msg;
    }

    char *status = calloc(4096, 1);
    if (!status) {
        return NULL;
    }

    snprintf(status, 4096, "MCP Status: %d server(s)\n", config->server_count);

    for (int i = 0; i < config->server_count; i++) {
        MCPServer *server = config->servers[i];
        if (!server) continue;

        char server_status[512];
        snprintf(server_status, sizeof(server_status),
                "  - %s: %s (%d tools)\n",
                server->name,
                server->connected ? "connected" : "disconnected",
                server->tool_count);
        strlcat(status, server_status, 4096);
    }

    return status;
}

/*
 * List resources from MCP servers
 */
MCPResourceList* mcp_list_resources(MCPConfig *config, const char *server_name) {
    if (!config) {
        LOG_ERROR("MCP: Invalid config for list_resources");
        MCPResourceList *result = calloc(1, sizeof(MCPResourceList));
        if (result) {
            result->is_error = 1;
            result->error_message = strdup("Invalid MCP configuration");
        }
        return result;
    }

    MCPResourceList *result = calloc(1, sizeof(MCPResourceList));
    if (!result) {
        LOG_ERROR("MCP: Failed to allocate resource list");
        return NULL;
    }

    // Allocate array for resources (estimate max)
    int max_resources = config->server_count * 100; // Assume max 100 resources per server
    result->resources = calloc((size_t)max_resources, sizeof(MCPResource*));
    if (!result->resources) {
        LOG_ERROR("MCP: Failed to allocate resource array");
        result->is_error = 1;
        result->error_message = strdup("Memory allocation failed");
        return result;
    }

    int total_count = 0;

    // Iterate through servers
    for (int i = 0; i < config->server_count; i++) {
        MCPServer *server = config->servers[i];
        if (!server || !server->connected) {
            continue;
        }

        // Filter by server name if specified
        if (server_name && strcmp(server->name, server_name) != 0) {
            continue;
        }

        LOG_INFO("MCP: Listing resources from server '%s'", server->name);

        // Send resources/list request
        cJSON *response = mcp_send_request(server, "resources/list", NULL);
        if (!response) {
            LOG_WARN("MCP: Failed to list resources from server '%s'", server->name);
            continue;
        }

        // Extract resources from response
        cJSON *result_obj = cJSON_GetObjectItem(response, "result");
        if (!result_obj) {
            LOG_WARN("MCP: No result in resources/list response from '%s'", server->name);
            cJSON_Delete(response);
            continue;
        }

        cJSON *resources = cJSON_GetObjectItem(result_obj, "resources");
        if (!resources || !cJSON_IsArray(resources)) {
            LOG_WARN("MCP: Invalid resources array from '%s'", server->name);
            cJSON_Delete(response);
            continue;
        }

        // Parse each resource
        cJSON *resource_item = NULL;
        cJSON_ArrayForEach(resource_item, resources) {
            if (total_count >= max_resources) {
                LOG_WARN("MCP: Resource limit reached");
                break;
            }

            MCPResource *resource = calloc(1, sizeof(MCPResource));
            if (!resource) {
                continue;
            }

            // Required fields
            cJSON *uri = cJSON_GetObjectItem(resource_item, "uri");
            cJSON *name = cJSON_GetObjectItem(resource_item, "name");

            if (uri && cJSON_IsString(uri)) {
                resource->uri = strdup(uri->valuestring);
                if (!resource->uri) {
                    free(resource);
                    continue;
                }
            }

            if (name && cJSON_IsString(name)) {
                resource->name = strdup(name->valuestring);
                if (!resource->name) {
                    free(resource->uri);
                    free(resource);
                    continue;
                }
            }

            // Optional fields
            cJSON *description = cJSON_GetObjectItem(resource_item, "description");
            if (description && cJSON_IsString(description)) {
                resource->description = strdup(description->valuestring);
                // NULL is OK for optional fields, don't fail
            }

            cJSON *mime_type = cJSON_GetObjectItem(resource_item, "mimeType");
            if (mime_type && cJSON_IsString(mime_type)) {
                resource->mime_type = strdup(mime_type->valuestring);
                // NULL is OK for optional fields, don't fail
            }

            // Add server name
            resource->server = strdup(server->name);
            if (!resource->server) {
                free(resource->uri);
                free(resource->name);
                free(resource->description);
                free(resource->mime_type);
                free(resource);
                continue;
            }

            result->resources[total_count++] = resource;
        }

        cJSON_Delete(response);
    }

    result->count = total_count;
    result->is_error = 0;

    LOG_INFO("MCP: Listed %d resource(s) from %s",
             total_count,
             server_name ? server_name : "all servers");

    return result;
}

/*
 * Read a resource from an MCP server
 */
MCPResourceContent* mcp_read_resource(MCPConfig *config, const char *server_name, const char *uri) {
    if (!config || !server_name || !uri) {
        LOG_ERROR("MCP: Invalid parameters for read_resource");
        MCPResourceContent *result = calloc(1, sizeof(MCPResourceContent));
        if (result) {
            result->is_error = 1;
            result->error_message = strdup("Invalid parameters");
        }
        return result;
    }

    // Find the server
    MCPServer *server = NULL;
    for (int i = 0; i < config->server_count; i++) {
        if (config->servers[i] &&
            config->servers[i]->name &&
            strcmp(config->servers[i]->name, server_name) == 0) {
            server = config->servers[i];
            break;
        }
    }

    if (!server) {
        LOG_ERROR("MCP: Server '%s' not found", server_name);
        MCPResourceContent *result = calloc(1, sizeof(MCPResourceContent));
        if (result) {
            result->is_error = 1;
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Server '%s' not found", server_name);
            result->error_message = strdup(err_msg);
        }
        return result;
    }

    if (!server->connected) {
        LOG_ERROR("MCP: Server '%s' not connected", server_name);
        MCPResourceContent *result = calloc(1, sizeof(MCPResourceContent));
        if (result) {
            result->is_error = 1;
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Server '%s' not connected", server_name);
            result->error_message = strdup(err_msg);
            if (!result->error_message) {
                free(result);
                return NULL;
            }
        }
        return result;
    }

    LOG_INFO("MCP: Reading resource '%s' from server '%s'", uri, server_name);

    // Build params
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "uri", uri);

    // Send resources/read request
    cJSON *response = mcp_send_request(server, "resources/read", params);
    cJSON_Delete(params);

    if (!response) {
        LOG_ERROR("MCP: Failed to read resource from server '%s'", server_name);
        MCPResourceContent *result = calloc(1, sizeof(MCPResourceContent));
        if (result) {
            result->is_error = 1;
            result->error_message = strdup("Failed to read resource");
        }
        return result;
    }

    // Extract result
    cJSON *result_obj = cJSON_GetObjectItem(response, "result");
    if (!result_obj) {
        LOG_ERROR("MCP: No result in resources/read response");
        cJSON_Delete(response);
        MCPResourceContent *result = calloc(1, sizeof(MCPResourceContent));
        if (result) {
            result->is_error = 1;
            result->error_message = strdup("No result in response");
        }
        return result;
    }

    MCPResourceContent *result = calloc(1, sizeof(MCPResourceContent));
    if (!result) {
        cJSON_Delete(response);
        return NULL;
    }

    result->uri = strdup(uri);
    if (!result->uri) {
        LOG_ERROR("MCP: Failed to allocate URI string");
        free(result);
        cJSON_Delete(response);
        return NULL;
    }
    result->is_error = 0;

    // Extract contents array
    cJSON *contents = cJSON_GetObjectItem(result_obj, "contents");
    if (contents && cJSON_IsArray(contents) && cJSON_GetArraySize(contents) > 0) {
        cJSON *content_item = cJSON_GetArrayItem(contents, 0);

        // Get URI and MIME type
        cJSON *content_uri = cJSON_GetObjectItem(content_item, "uri");
        if (content_uri && cJSON_IsString(content_uri) && !result->uri) {
            result->uri = strdup(content_uri->valuestring);
            // NULL is OK here, we already have uri from parameter
        }

        cJSON *mime_type = cJSON_GetObjectItem(content_item, "mimeType");
        if (mime_type && cJSON_IsString(mime_type)) {
            result->mime_type = strdup(mime_type->valuestring);
            // NULL is OK for optional fields
        }

        // Get text content
        cJSON *text = cJSON_GetObjectItem(content_item, "text");
        if (text && cJSON_IsString(text)) {
            result->text = strdup(text->valuestring);
            // NULL is OK for optional fields
        }

        // Get image content (type: 'image' with data field)
        cJSON *content_type = cJSON_GetObjectItem(content_item, "type");
        if (content_type && cJSON_IsString(content_type) &&
            strcmp(content_type->valuestring, "image") == 0) {
            cJSON *image_data = cJSON_GetObjectItem(content_item, "data");
            if (image_data && cJSON_IsString(image_data)) {
                // Handle image content (base64 encoded)
                const char *image_str = image_data->valuestring;
                size_t image_len = strlen(image_str);

                // Decode base64
                size_t decoded_size = 0;
                unsigned char *decoded_data = base64_decode(image_str, image_len, &decoded_size);
                if (decoded_data) {
                    result->blob = decoded_data;
                    result->blob_size = decoded_size;
                    LOG_DEBUG("MCP: Image content received and decoded (encoded size: %zu, decoded size: %zu)", image_len, decoded_size);
                } else {
                    LOG_WARN("MCP: Failed to decode base64 image content");
                    // Fallback: store as-is
                    result->blob = malloc(image_len);
                    if (result->blob) {
                        memcpy(result->blob, image_str, image_len);
                        result->blob_size = image_len;
                        LOG_DEBUG("MCP: Image content stored as-is (size: %zu)", image_len);
                    }
                }
            }
        }

        // Get blob content (base64 encoded) - legacy format
        cJSON *blob = cJSON_GetObjectItem(content_item, "blob");
        if (blob && cJSON_IsString(blob) && !result->blob) {
            // Handle binary content (base64 encoded)
            const char *blob_str = blob->valuestring;
            size_t blob_len = strlen(blob_str);

            // Decode base64
            size_t decoded_size = 0;
            unsigned char *decoded_data = base64_decode(blob_str, blob_len, &decoded_size);
            if (decoded_data) {
                result->blob = decoded_data;
                result->blob_size = decoded_size;
                LOG_DEBUG("MCP: Binary blob content received and decoded (encoded size: %zu, decoded size: %zu)", blob_len, decoded_size);
            } else {
                LOG_WARN("MCP: Failed to decode base64 blob content");
                // Fallback: store as-is
                result->blob = malloc(blob_len);
                if (result->blob) {
                    memcpy(result->blob, blob_str, blob_len);
                    result->blob_size = blob_len;
                    LOG_DEBUG("MCP: Binary blob content stored as-is (size: %zu)", blob_len);
                }
            }
        }
    }

    cJSON_Delete(response);

    LOG_INFO("MCP: Successfully read resource '%s' from server '%s'", uri, server_name);

    return result;
}

/*
 * Free MCP resource list
 */
void mcp_free_resource_list(MCPResourceList *list) {
    if (!list) return;

    if (list->resources) {
        for (int i = 0; i < list->count; i++) {
            MCPResource *resource = list->resources[i];
            if (resource) {
                free(resource->server);
                free(resource->uri);
                free(resource->name);
                free(resource->description);
                free(resource->mime_type);
                free(resource);
            }
        }
        free(list->resources);
    }

    free(list->error_message);
    free(list);
}

/*
 * Free MCP resource content
 */
void mcp_free_resource_content(MCPResourceContent *content) {
    if (!content) return;

    free(content->uri);
    free(content->mime_type);
    free(content->text);
    free(content->blob);
    free(content->error_message);
    free(content);
}
