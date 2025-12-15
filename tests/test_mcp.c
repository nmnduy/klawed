/*
 * test_mcp.c - Basic MCP integration tests
 *
 * Tests MCP configuration loading and basic functionality.
 * Does not require actual MCP servers to be running.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

// Stub logger functions for testing
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
void log_message(int level, const char *file, int line, const char *fmt, ...) {
    (void)level; (void)file; (void)line; (void)fmt;
}
#pragma GCC diagnostic pop

#include "../src/mcp.h"

// Test helper: Create a temporary config file
static char* create_test_config(const char *json_content) {
    static char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "/tmp/mcp_test_config_%d.json", getpid());

    FILE *fp = fopen(temp_path, "w");
    if (!fp) {
        return NULL;
    }

    fputs(json_content, fp);
    fclose(fp);

    return temp_path;
}

// Test helper: Remove temporary config file
static void remove_test_config(const char *path) {
    if (path) {
        unlink(path);
    }
}

// Test 1: MCP initialization
static void test_mcp_init(void) {
    printf("Test 1: MCP initialization... ");

    int result = mcp_init();
    assert(result == 0);

    // Should be idempotent
    result = mcp_init();
    assert(result == 0);

    mcp_cleanup();

    printf("PASSED\n");
}

// Test 2: Load valid config
static void test_load_valid_config(void) {
    printf("Test 2: Load valid config... ");

    const char *config_json =
        "{\n"
        "  \"mcpServers\": {\n"
        "    \"test_server\": {\n"
        "      \"command\": \"echo\",\n"
        "      \"args\": [\"hello\"],\n"
        "      \"env\": {\n"
        "        \"TEST_VAR\": \"test_value\"\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}\n";

    char *config_path = create_test_config(config_json);
    assert(config_path != NULL);

    MCPConfig *config = mcp_load_config(config_path);

    // Config should load successfully
    assert(config != NULL);
    assert(config->server_count == 1);
    assert(config->servers[0] != NULL);
    assert(strcmp(config->servers[0]->name, "test_server") == 0);
    assert(strcmp(config->servers[0]->command, "echo") == 0);
    assert(config->servers[0]->args_count == 1);
    assert(strcmp(config->servers[0]->args[0], "hello") == 0);

    mcp_free_config(config);
    remove_test_config(config_path);

    printf("PASSED\n");
}

// Test 3: Load config with multiple servers
static void test_load_multiple_servers(void) {
    printf("Test 3: Load config with multiple servers... ");

    const char *config_json =
        "{\n"
        "  \"mcpServers\": {\n"
        "    \"server1\": {\n"
        "      \"command\": \"cmd1\",\n"
        "      \"args\": []\n"
        "    },\n"
        "    \"server2\": {\n"
        "      \"command\": \"cmd2\",\n"
        "      \"args\": [\"arg1\", \"arg2\"]\n"
        "    }\n"
        "  }\n"
        "}\n";

    char *config_path = create_test_config(config_json);
    assert(config_path != NULL);

    MCPConfig *config = mcp_load_config(config_path);

    assert(config != NULL);
    assert(config->server_count == 2);

    // Check server1
    MCPServer *server1 = NULL;
    MCPServer *server2 = NULL;
    for (int i = 0; i < config->server_count; i++) {
        if (strcmp(config->servers[i]->name, "server1") == 0) {
            server1 = config->servers[i];
        } else if (strcmp(config->servers[i]->name, "server2") == 0) {
            server2 = config->servers[i];
        }
    }

    assert(server1 != NULL);
    assert(strcmp(server1->command, "cmd1") == 0);
    assert(server1->args_count == 0);

    assert(server2 != NULL);
    assert(strcmp(server2->command, "cmd2") == 0);
    assert(server2->args_count == 2);
    assert(strcmp(server2->args[0], "arg1") == 0);
    assert(strcmp(server2->args[1], "arg2") == 0);

    mcp_free_config(config);
    remove_test_config(config_path);

    printf("PASSED\n");
}

// Test 4: Load invalid config (should return NULL)
static void test_load_invalid_config(void) {
    printf("Test 4: Load invalid config... ");

    const char *config_json = "{ invalid json";

    char *config_path = create_test_config(config_json);
    assert(config_path != NULL);

    MCPConfig *config = mcp_load_config(config_path);

    // Should fail gracefully
    assert(config == NULL);

    remove_test_config(config_path);

    printf("PASSED\n");
}

// Test 5: Load config with no servers
static void test_load_empty_config(void) {
    printf("Test 5: Load config with no servers... ");

    const char *config_json =
        "{\n"
        "  \"mcpServers\": {}\n"
        "}\n";

    char *config_path = create_test_config(config_json);
    assert(config_path != NULL);

    MCPConfig *config = mcp_load_config(config_path);

    // Should return NULL for empty server list
    assert(config == NULL);

    remove_test_config(config_path);

    printf("PASSED\n");
}

// Test 6: Load non-existent config
static void test_load_nonexistent_config(void) {
    printf("Test 6: Load non-existent config... ");

    MCPConfig *config = mcp_load_config("/nonexistent/path/config.json");

    // Should return NULL for missing file
    assert(config == NULL);

    printf("PASSED\n");
}

// Test 7: MCP enabled/disabled state
static void test_mcp_enabled_state(void) {
    printf("Test 7: MCP enabled state... ");

    // Ensure clean env
    unsetenv("CLAUDE_MCP_ENABLED");

    // Before init, should be disabled (not initialized yet)
    int enabled = mcp_is_enabled();
    assert(enabled == 0);

    // After init without env var, should be disabled by default
    mcp_init();
    enabled = mcp_is_enabled();
    assert(enabled == 0);

    mcp_cleanup();

    // Set env var to enable and re-init
    setenv("CLAUDE_MCP_ENABLED", "1", 1);
    mcp_init();
    enabled = mcp_is_enabled();
    assert(enabled == 1);

    mcp_cleanup();
    unsetenv("CLAUDE_MCP_ENABLED");

    printf("PASSED\n");
}

// Test 8: Get status string
static void test_mcp_get_status(void) {
    printf("Test 8: Get MCP status... ");

    const char *config_json =
        "{\n"
        "  \"mcpServers\": {\n"
        "    \"test\": {\n"
        "      \"command\": \"echo\",\n"
        "      \"args\": []\n"
        "    }\n"
        "  }\n"
        "}\n";

    char *config_path = create_test_config(config_json);
    assert(config_path != NULL);

    MCPConfig *config = mcp_load_config(config_path);
    assert(config != NULL);

    char *status = mcp_get_status(config);
    assert(status != NULL);
    assert(strstr(status, "MCP Status") != NULL);
    assert(strstr(status, "1 server") != NULL);

    free(status);
    mcp_free_config(config);
    remove_test_config(config_path);

    printf("PASSED\n");
}

// Test 9: Find tool server (without actual connection)
static void test_find_tool_server(void) {
    printf("Test 9: Find tool server... ");

    const char *config_json =
        "{\n"
        "  \"mcpServers\": {\n"
        "    \"filesystem\": {\n"
        "      \"command\": \"echo\",\n"
        "      \"args\": []\n"
        "    },\n"
        "    \"github\": {\n"
        "      \"command\": \"echo\",\n"
        "      \"args\": []\n"
        "    }\n"
        "  }\n"
        "}\n";

    char *config_path = create_test_config(config_json);
    assert(config_path != NULL);

    MCPConfig *config = mcp_load_config(config_path);
    assert(config != NULL);

    // Test finding servers by tool name pattern
    MCPServer *server = mcp_find_tool_server(config, "mcp_filesystem_read_file");
    assert(server != NULL);
    assert(strcmp(server->name, "filesystem") == 0);

    server = mcp_find_tool_server(config, "mcp_github_search_repos");
    assert(server != NULL);
    assert(strcmp(server->name, "github") == 0);

    // Non-MCP tool should not be found
    server = mcp_find_tool_server(config, "Bash");
    assert(server == NULL);

    // Invalid MCP tool name should not be found
    server = mcp_find_tool_server(config, "mcp_nonexistent_tool");
    assert(server == NULL);

    mcp_free_config(config);
    remove_test_config(config_path);

    printf("PASSED\n");
}

// Test 10: Test mkdir_p utility function
static void test_mkdir_p_func(void) {
    printf("Test 10: Test mkdir_p utility... ");

    // Clean up any existing test directories
    int cleanup_result = system("rm -rf /tmp/mcp_test_dir");
    (void)cleanup_result; // Cleanup failure is not critical for test

    // Test 1: Create a simple directory
    int result = mcp_mkdir_p("/tmp/mcp_test_dir");
    assert(result == 0);

    struct stat st;
    result = stat("/tmp/mcp_test_dir", &st);
    assert(result == 0);
    assert(S_ISDIR(st.st_mode));

    // Test 2: Create nested directories
    result = mcp_mkdir_p("/tmp/mcp_test_dir/nested/deep/path");
    assert(result == 0);

    result = stat("/tmp/mcp_test_dir/nested/deep/path", &st);
    assert(result == 0);
    assert(S_ISDIR(st.st_mode));

    // Test 3: Directory already exists (should succeed)
    result = mcp_mkdir_p("/tmp/mcp_test_dir/nested");
    assert(result == 0);

    // Test 4: Create directory with trailing slash
    result = mcp_mkdir_p("/tmp/mcp_test_dir/trailing/");
    assert(result == 0);

    result = stat("/tmp/mcp_test_dir/trailing", &st);
    assert(result == 0);
    assert(S_ISDIR(st.st_mode));

    // Clean up
    cleanup_result = system("rm -rf /tmp/mcp_test_dir");
    (void)cleanup_result; // Cleanup failure is not critical for test

    printf("PASSED\\n");
}

int main(void) {
    printf("=== MCP Integration Tests ===\\n\\n");

    test_mcp_init();
    test_load_valid_config();
    test_load_multiple_servers();
    test_load_invalid_config();
    test_load_empty_config();
    test_load_nonexistent_config();
    test_mcp_enabled_state();
    test_mcp_get_status();
    test_find_tool_server();
    test_mkdir_p_func();

    printf("\\n=== All MCP tests passed! ===\\n");
    return 0;
}
