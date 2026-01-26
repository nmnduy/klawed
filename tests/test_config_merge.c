/*
 * Test for config.c - verify config merge/save behavior
 *
 * This tests the fix for the bug where saving config would copy entries
 * from global config (~/.klawed/config.json) to local config (.klawed/config.json).
 *
 * The expected behavior:
 * - When saving, only write fields that differ from defaults OR already exist in local config
 * - Global config entries should NOT be copied to local config
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <bsd/string.h>
#include <cjson/cJSON.h>
#include "../src/config.h"
#include "../src/data_dir.h"

// Helper to read local config file and check its JSON contents
static char* read_local_config_file(void) {
    char config_path[1024];
    if (data_dir_build_path(config_path, sizeof(config_path), "config.json") != 0) {
        return NULL;
    }

    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 1024 * 1024) {
        fclose(fp);
        return NULL;
    }

    char *json_str = malloc((size_t)file_size + 1);
    if (!json_str) {
        fclose(fp);
        return NULL;
    }

    size_t bytes_read = fread(json_str, 1, (size_t)file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        free(json_str);
        return NULL;
    }

    json_str[file_size] = '\0';
    return json_str;
}

// Helper to delete local config file
static void delete_local_config(void) {
    char config_path[1024];
    if (data_dir_build_path(config_path, sizeof(config_path), "config.json") == 0) {
        unlink(config_path);
    }
}

int main(void) {
    int failures = 0;

    printf("=== Config Merge Tests ===\n\n");

    // Ensure data directory exists
    if (data_dir_ensure(NULL) != 0) {
        printf("FAIL: Could not create data directory\n");
        return 1;
    }

    // Clean up before tests
    delete_local_config();

    // Test 1: Saving a default config should NOT create a full config file
    printf("Test 1: Saving default config should not add default values\n");
    {
        KlawedConfig config;
        config_init_defaults(&config);

        // Save the default config
        if (config_save(&config) != 0) {
            printf("  FAIL: config_save() failed\n");
            failures++;
        } else {
            // Read the local config file and check its contents
            char *json_str = read_local_config_file();
            if (json_str == NULL) {
                // Empty/no file is acceptable for default config
                printf("  PASS: No local config file created for default config\n");
            } else {
                cJSON *root = cJSON_Parse(json_str);
                if (!root) {
                    printf("  FAIL: Could not parse local config JSON\n");
                    failures++;
                } else {
                    // Check that llm_provider section is NOT present (since we have defaults)
                    cJSON *llm_provider = cJSON_GetObjectItem(root, "llm_provider");
                    if (llm_provider != NULL) {
                        printf("  FAIL: Default config saved llm_provider section\n");
                        failures++;
                    } else {
                        printf("  PASS: llm_provider section not saved for defaults\n");
                    }

                    // Check that providers section is NOT present
                    cJSON *providers = cJSON_GetObjectItem(root, "providers");
                    if (providers != NULL) {
                        printf("  FAIL: Default config saved providers section\n");
                        failures++;
                    } else {
                        printf("  PASS: providers section not saved for defaults\n");
                    }

                    cJSON_Delete(root);
                }
                free(json_str);
            }
        }
    }

    // Clean up
    delete_local_config();

    // Test 2: Saving a non-default value should only add that field
    printf("\nTest 2: Saving non-default value should only add that field\n");
    {
        KlawedConfig config;
        config_init_defaults(&config);

        // Set a non-default value
        config.input_box_style = INPUT_STYLE_BORDER;  // default is INPUT_STYLE_HORIZONTAL

        if (config_save(&config) != 0) {
            printf("  FAIL: config_save() failed\n");
            failures++;
        } else {
            char *json_str = read_local_config_file();
            if (json_str == NULL) {
                printf("  FAIL: Local config file not created\n");
                failures++;
            } else {
                cJSON *root = cJSON_Parse(json_str);
                if (!root) {
                    printf("  FAIL: Could not parse local config JSON\n");
                    failures++;
                } else {
                    // Check that input_box_style IS present
                    cJSON *input_style = cJSON_GetObjectItem(root, "input_box_style");
                    if (input_style == NULL || !cJSON_IsString(input_style)) {
                        printf("  FAIL: input_box_style not saved\n");
                        failures++;
                    } else if (strcmp(input_style->valuestring, "border") != 0) {
                        printf("  FAIL: input_box_style has wrong value: %s\n", input_style->valuestring);
                        failures++;
                    } else {
                        printf("  PASS: input_box_style saved correctly\n");
                    }

                    // Check that llm_provider section is NOT present (still default)
                    cJSON *llm_provider = cJSON_GetObjectItem(root, "llm_provider");
                    if (llm_provider != NULL) {
                        printf("  FAIL: Default llm_provider was saved when not changed\n");
                        failures++;
                    } else {
                        printf("  PASS: llm_provider not saved (not changed)\n");
                    }

                    cJSON_Delete(root);
                }
                free(json_str);
            }
        }
    }

    // Clean up
    delete_local_config();

    // Test 3: Setting active_provider should save only that field
    printf("\nTest 3: Setting active_provider should save only that field\n");
    {
        KlawedConfig config;
        config_init_defaults(&config);

        // Set active_provider (non-empty = differs from default)
        strlcpy(config.active_provider, "my-provider", sizeof(config.active_provider));

        if (config_save(&config) != 0) {
            printf("  FAIL: config_save() failed\n");
            failures++;
        } else {
            char *json_str = read_local_config_file();
            if (json_str == NULL) {
                printf("  FAIL: Local config file not created\n");
                failures++;
            } else {
                cJSON *root = cJSON_Parse(json_str);
                if (!root) {
                    printf("  FAIL: Could not parse local config JSON\n");
                    failures++;
                } else {
                    // Check that active_provider IS present
                    cJSON *active_provider = cJSON_GetObjectItem(root, "active_provider");
                    if (active_provider == NULL || !cJSON_IsString(active_provider)) {
                        printf("  FAIL: active_provider not saved\n");
                        failures++;
                    } else if (strcmp(active_provider->valuestring, "my-provider") != 0) {
                        printf("  FAIL: active_provider has wrong value: %s\n", active_provider->valuestring);
                        failures++;
                    } else {
                        printf("  PASS: active_provider saved correctly\n");
                    }

                    // Check that llm_provider is NOT present
                    cJSON *llm_provider = cJSON_GetObjectItem(root, "llm_provider");
                    if (llm_provider != NULL) {
                        printf("  FAIL: Default llm_provider was saved\n");
                        failures++;
                    } else {
                        printf("  PASS: llm_provider not saved (default)\n");
                    }

                    cJSON_Delete(root);
                }
                free(json_str);
            }
        }
    }

    // Clean up
    delete_local_config();

    // Test 4: Adding a named provider should save providers but not llm_provider
    printf("\nTest 4: Adding named provider should save providers section only\n");
    {
        KlawedConfig config;
        config_init_defaults(&config);

        // Add a named provider
        LLMProviderConfig provider_config;
        provider_config.provider_type = PROVIDER_OPENAI;
        strlcpy(provider_config.provider_name, "Test Provider", sizeof(provider_config.provider_name));
        strlcpy(provider_config.model, "gpt-4", sizeof(provider_config.model));
        provider_config.api_base[0] = '\0';
        provider_config.api_key[0] = '\0';
        provider_config.api_key_env[0] = '\0';
        provider_config.use_bedrock = 0;

        config_set_provider(&config, "test-provider", &provider_config);

        if (config_save(&config) != 0) {
            printf("  FAIL: config_save() failed\n");
            failures++;
        } else {
            char *json_str = read_local_config_file();
            if (json_str == NULL) {
                printf("  FAIL: Local config file not created\n");
                failures++;
            } else {
                cJSON *root = cJSON_Parse(json_str);
                if (!root) {
                    printf("  FAIL: Could not parse local config JSON\n");
                    failures++;
                } else {
                    // Check that providers section IS present
                    cJSON *providers = cJSON_GetObjectItem(root, "providers");
                    if (providers == NULL || !cJSON_IsObject(providers)) {
                        printf("  FAIL: providers section not saved\n");
                        failures++;
                    } else {
                        printf("  PASS: providers section saved\n");

                        // Check that our provider is in there
                        cJSON *test_provider = cJSON_GetObjectItem(providers, "test-provider");
                        if (test_provider == NULL) {
                            printf("  FAIL: test-provider not in providers\n");
                            failures++;
                        } else {
                            printf("  PASS: test-provider saved in providers\n");
                        }
                    }

                    // Check that llm_provider is NOT present (it should not be saved)
                    cJSON *llm_provider = cJSON_GetObjectItem(root, "llm_provider");
                    if (llm_provider != NULL) {
                        printf("  FAIL: Default llm_provider was saved\n");
                        failures++;
                    } else {
                        printf("  PASS: llm_provider not saved (default)\n");
                    }

                    cJSON_Delete(root);
                }
                free(json_str);
            }
        }
    }

    // Clean up
    delete_local_config();

    // Test 5: Updating an existing field should preserve other fields
    printf("\nTest 5: Updating existing field should preserve other fields\n");
    {
        KlawedConfig config;
        config_init_defaults(&config);

        // First, save active_provider
        strlcpy(config.active_provider, "provider-a", sizeof(config.active_provider));
        config_save(&config);

        // Now update with a different value
        config_init_defaults(&config);
        config.input_box_style = INPUT_STYLE_BORDER;
        config_save(&config);

        // Read and check
        char *json_str = read_local_config_file();
        if (json_str == NULL) {
            printf("  FAIL: Local config file not created\n");
            failures++;
        } else {
            cJSON *root = cJSON_Parse(json_str);
            if (!root) {
                printf("  FAIL: Could not parse local config JSON\n");
                failures++;
            } else {
                // Check that active_provider is still there
                cJSON *active_provider = cJSON_GetObjectItem(root, "active_provider");
                if (active_provider == NULL) {
                    printf("  FAIL: active_provider was removed\n");
                    failures++;
                } else if (strcmp(active_provider->valuestring, "provider-a") != 0) {
                    printf("  FAIL: active_provider was changed: %s\n", active_provider->valuestring);
                    failures++;
                } else {
                    printf("  PASS: active_provider preserved\n");
                }

                // Check that input_box_style was updated
                cJSON *input_style = cJSON_GetObjectItem(root, "input_box_style");
                if (input_style == NULL) {
                    printf("  FAIL: input_box_style not saved\n");
                    failures++;
                } else if (strcmp(input_style->valuestring, "border") != 0) {
                    printf("  FAIL: input_box_style wrong: %s\n", input_style->valuestring);
                    failures++;
                } else {
                    printf("  PASS: input_box_style saved correctly\n");
                }

                cJSON_Delete(root);
            }
            free(json_str);
        }
    }

    // Clean up after all tests
    delete_local_config();

    printf("\n=== Results ===\n");
    if (failures == 0) {
        printf("All tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed!\n", failures);
        return 1;
    }
}
