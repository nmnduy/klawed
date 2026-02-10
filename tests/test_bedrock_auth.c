#ifdef TEST_BUILD
#undef TEST_BUILD
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aws_bedrock.h"
#include "logger.h"

// Simple test framework
static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests_run++; \
    if (cond) { tests_passed++; printf("[PASS] %s\n", msg); } \
    else { printf("[FAIL] %s\n", msg); } \
} while(0)

// Mock state
static int auth_done = 0;
static int system_calls = 0;
static int exec_calls = 0;

// Mock exec_command: simulate credential sources (NO validation)
static char* exec_command_mock(const char *cmd) {
    exec_calls++;
    // SSO start URL detection
    if (strstr(cmd, "aws configure get sso_start_url")) {
        return strdup("https://dummy-sso-url");
    }
    // export-credentials output (returns credentials if auth_done)
    if (strstr(cmd, "export-credentials")) {
        if (auth_done) {
            return strdup("export AWS_ACCESS_KEY_ID=AKIA\nexport AWS_SECRET_ACCESS_KEY=SECRET\n");
        } else {
            return strdup("");  // No cached credentials
        }
    }
    // configure get fallback
    if (strstr(cmd, "aws configure get")) {
        return strdup("");  // No static credentials
    }
    // Fallback empty
    return strdup("");
}

// Mock system: capture auth commands (SSO and custom)
static int system_mock(const char *cmd) {
    system_calls++;
    if (strstr(cmd, "aws sso login")) {
        auth_done = 1;
        return 0;
    }
    if (strstr(cmd, "custom-auth")) {
        auth_done = 1;
        return 0;
    }
    return 1;
}

static void test_env_credentials_no_validation(void) {
    printf("\nTest: Environment credentials loaded without validation\n");
    setenv("AWS_ACCESS_KEY_ID", "AKIATEST", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "SECRET_TEST", 1);
    unsetenv("AWS_SESSION_TOKEN");
    unsetenv("AWS_AUTH_COMMAND");
    unsetenv("AWS_PROFILE");

    aws_bedrock_set_exec_command_fn(exec_command_mock);
    aws_bedrock_set_system_fn(system_mock);
    auth_done = exec_calls = system_calls = 0;

    AWSCredentials *creds = bedrock_load_credentials(NULL, NULL);
    ASSERT_TRUE(creds != NULL, "Credentials returned from environment");
    ASSERT_TRUE(strcmp(creds->access_key_id, "AKIATEST") == 0, "Access key matches env");
    ASSERT_TRUE(system_calls == 0, "No system calls (no validation/auth)");
    ASSERT_TRUE(exec_calls == 0, "No exec calls (env vars used directly)");

    bedrock_creds_free(creds);
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

static void test_sso_cached_credentials_no_validation(void) {
    printf("\nTest: SSO cached credentials loaded without validation\n");
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
    unsetenv("AWS_SESSION_TOKEN");
    unsetenv("AWS_AUTH_COMMAND");
    unsetenv("AWS_PROFILE");

    aws_bedrock_set_exec_command_fn(exec_command_mock);
    aws_bedrock_set_system_fn(system_mock);
    auth_done = 1;  // Simulate cached credentials available
    exec_calls = system_calls = 0;

    AWSCredentials *creds = bedrock_load_credentials(NULL, NULL);
    ASSERT_TRUE(creds != NULL, "Credentials returned from SSO cache");
    ASSERT_TRUE(system_calls == 0, "No system calls (no auth triggered)");
    ASSERT_TRUE(exec_calls >= 2, "exec_command called for SSO detection and export");

    bedrock_creds_free(creds);
}

static void test_no_cached_credentials_returns_null(void) {
    printf("\nTest: No cached credentials returns NULL (no auth triggered)\n");
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
    unsetenv("AWS_SESSION_TOKEN");
    unsetenv("AWS_AUTH_COMMAND");
    unsetenv("AWS_PROFILE");

    aws_bedrock_set_exec_command_fn(exec_command_mock);
    aws_bedrock_set_system_fn(system_mock);
    auth_done = 0;  // No cached credentials
    exec_calls = system_calls = 0;

    AWSCredentials *creds = bedrock_load_credentials(NULL, NULL);
    ASSERT_TRUE(creds == NULL, "NULL returned when no credentials found");
    ASSERT_TRUE(system_calls == 0, "No system calls (no auth triggered)");
    ASSERT_TRUE(exec_calls >= 2, "exec_command called to check sources");
}

static void test_authenticate_sets_credentials(void) {
    printf("\nTest: bedrock_authenticate triggers SSO login\n");
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
    unsetenv("AWS_SESSION_TOKEN");
    unsetenv("AWS_AUTH_COMMAND");
    unsetenv("AWS_PROFILE");

    aws_bedrock_set_exec_command_fn(exec_command_mock);
    aws_bedrock_set_system_fn(system_mock);
    auth_done = 0;
    system_calls = exec_calls = 0;

    int result = bedrock_authenticate(NULL);
    ASSERT_TRUE(result == 0, "bedrock_authenticate returns success");
    ASSERT_TRUE(system_calls == 1, "One system call to aws sso login");
    ASSERT_TRUE(auth_done == 1, "Auth state updated");

    // Now credentials should be available
    exec_calls = 0;
    AWSCredentials *creds = bedrock_load_credentials(NULL, NULL);
    ASSERT_TRUE(creds != NULL, "Credentials available after authenticate");

    bedrock_creds_free(creds);
}

static void test_custom_auth_command(void) {
    printf("\nTest: AWS_AUTH_COMMAND used in bedrock_authenticate\n");
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
    unsetenv("AWS_SESSION_TOKEN");
    setenv("AWS_AUTH_COMMAND", "echo custom-auth && return 0", 1);
    unsetenv("AWS_PROFILE");

    aws_bedrock_set_exec_command_fn(exec_command_mock);
    aws_bedrock_set_system_fn(system_mock);
    auth_done = 0;
    system_calls = exec_calls = 0;

    int result = bedrock_authenticate(NULL);
    ASSERT_TRUE(result == 0, "bedrock_authenticate with custom command returns success");
    ASSERT_TRUE(system_calls == 1, "One system call for custom auth");
    ASSERT_TRUE(auth_done == 1, "Auth state updated");

    unsetenv("AWS_AUTH_COMMAND");
}

static void test_bedrock_config_init_ex_bypasses_env_check(void) {
    printf("\nTest: bedrock_config_init_ex bypasses KLAWED_USE_BEDROCK check\n");
    unsetenv("KLAWED_USE_BEDROCK");
    setenv("AWS_ACCESS_KEY_ID", "AKIATEST", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "SECRET_TEST", 1);
    unsetenv("AWS_SESSION_TOKEN");

    // bedrock_config_init should fail without KLAWED_USE_BEDROCK
    BedrockConfig *config1 = bedrock_config_init("test-model");
    ASSERT_TRUE(config1 == NULL, "bedrock_config_init fails without KLAWED_USE_BEDROCK");

    // bedrock_config_init_ex should succeed without KLAWED_USE_BEDROCK
    BedrockConfig *config2 = bedrock_config_init_ex("test-model", NULL);
    ASSERT_TRUE(config2 != NULL, "bedrock_config_init_ex succeeds without KLAWED_USE_BEDROCK");
    ASSERT_TRUE(config2->enabled == 1, "Config is enabled");
    ASSERT_TRUE(strcmp(config2->model_id, "test-model") == 0, "Model ID set correctly");
    ASSERT_TRUE(config2->region != NULL, "Region is set");
    ASSERT_TRUE(config2->endpoint != NULL, "Endpoint is built");

    bedrock_config_free(config2);
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

static void test_bedrock_config_init_ex_with_explicit_region(void) {
    printf("\nTest: bedrock_config_init_ex with explicit region\n");
    setenv("AWS_ACCESS_KEY_ID", "AKIATEST", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "SECRET_TEST", 1);

    // Test with explicit region
    BedrockConfig *config = bedrock_config_init_ex("test-model", "eu-west-1");
    ASSERT_TRUE(config != NULL, "Config created with explicit region");
    ASSERT_TRUE(strcmp(config->region, "eu-west-1") == 0, "Explicit region is used");
    ASSERT_TRUE(strstr(config->endpoint, "eu-west-1") != NULL, "Endpoint uses explicit region");

    bedrock_config_free(config);
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

static void test_bedrock_config_init_ex_null_region_uses_env(void) {
    printf("\nTest: bedrock_config_init_ex with NULL region uses AWS_REGION env\n");
    setenv("AWS_ACCESS_KEY_ID", "AKIATEST", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "SECRET_TEST", 1);
    setenv("AWS_REGION", "ap-southeast-1", 1);

    // Test with NULL region - should use AWS_REGION env var
    BedrockConfig *config = bedrock_config_init_ex("test-model", NULL);
    ASSERT_TRUE(config != NULL, "Config created with NULL region");
    ASSERT_TRUE(strcmp(config->region, "ap-southeast-1") == 0, "AWS_REGION env var is used");

    bedrock_config_free(config);
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
    unsetenv("AWS_REGION");
}

static void test_bedrock_config_init_ex_null_region_uses_default(void) {
    printf("\nTest: bedrock_config_init_ex with NULL region uses default when env not set\n");
    setenv("AWS_ACCESS_KEY_ID", "AKIATEST", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "SECRET_TEST", 1);
    unsetenv("AWS_REGION");

    // Test with NULL region and no AWS_REGION env - should use default
    BedrockConfig *config = bedrock_config_init_ex("test-model", NULL);
    ASSERT_TRUE(config != NULL, "Config created with NULL region (default)");
    ASSERT_TRUE(strcmp(config->region, "us-west-2") == 0, "Default region us-west-2 is used");

    bedrock_config_free(config);
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

int main(void) {
    test_env_credentials_no_validation();
    test_sso_cached_credentials_no_validation();
    test_no_cached_credentials_returns_null();
    test_authenticate_sets_credentials();
    test_custom_auth_command();
    test_bedrock_config_init_ex_bypasses_env_check();
    test_bedrock_config_init_ex_with_explicit_region();
    test_bedrock_config_init_ex_null_region_uses_env();
    test_bedrock_config_init_ex_null_region_uses_default();

    printf("\nTests run: %d, passed: %d, failed: %d\n",
           tests_run, tests_passed, tests_run - tests_passed);
    return (tests_run == tests_passed) ? 0 : 1;
}
