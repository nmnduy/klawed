/*
 * Test for config.c - verify config save/load functionality
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/config.h"

int main(void) {
    KlawedConfig config;
    int failures = 0;

    printf("=== Config Module Tests ===\n\n");

    // Test 1: Initialize defaults
    printf("Test 1: config_init_defaults()\n");
    config_init_defaults(&config);
    if (config.input_box_style != INPUT_STYLE_BLAND) {
        printf("  FAIL: Expected INPUT_STYLE_BLAND as default\n");
        failures++;
    } else {
        printf("  PASS: Default style is INPUT_STYLE_BLAND\n");
    }

    // Test 2: Style to string conversion
    printf("\nTest 2: config_input_style_to_string()\n");
    if (strcmp(config_input_style_to_string(INPUT_STYLE_BLAND), "bland") != 0) {
        printf("  FAIL: Expected 'bland' for INPUT_STYLE_BLAND\n");
        failures++;
    } else {
        printf("  PASS: INPUT_STYLE_BLAND -> 'bland'\n");
    }
    if (strcmp(config_input_style_to_string(INPUT_STYLE_BACKGROUND), "background") != 0) {
        printf("  FAIL: Expected 'background' for INPUT_STYLE_BACKGROUND\n");
        failures++;
    } else {
        printf("  PASS: INPUT_STYLE_BACKGROUND -> 'background'\n");
    }
    if (strcmp(config_input_style_to_string(INPUT_STYLE_BORDER), "border") != 0) {
        printf("  FAIL: Expected 'border' for INPUT_STYLE_BORDER\n");
        failures++;
    } else {
        printf("  PASS: INPUT_STYLE_BORDER -> 'border'\n");
    }
    if (strcmp(config_input_style_to_string(INPUT_STYLE_HORIZONTAL), "horizontal") != 0) {
        printf("  FAIL: Expected 'horizontal' for INPUT_STYLE_HORIZONTAL\n");
        failures++;
    } else {
        printf("  PASS: INPUT_STYLE_HORIZONTAL -> 'horizontal'\n");
    }

    // Test 3: String to style conversion
    printf("\nTest 3: config_input_style_from_string()\n");
    if (config_input_style_from_string("bland") != INPUT_STYLE_BLAND) {
        printf("  FAIL: Expected INPUT_STYLE_BLAND for 'bland'\n");
        failures++;
    } else {
        printf("  PASS: 'bland' -> INPUT_STYLE_BLAND\n");
    }
    if (config_input_style_from_string("background") != INPUT_STYLE_BACKGROUND) {
        printf("  FAIL: Expected INPUT_STYLE_BACKGROUND for 'background'\n");
        failures++;
    } else {
        printf("  PASS: 'background' -> INPUT_STYLE_BACKGROUND\n");
    }
    if (config_input_style_from_string("border") != INPUT_STYLE_BORDER) {
        printf("  FAIL: Expected INPUT_STYLE_BORDER for 'border'\n");
        failures++;
    } else {
        printf("  PASS: 'border' -> INPUT_STYLE_BORDER\n");
    }
    if (config_input_style_from_string("horizontal") != INPUT_STYLE_HORIZONTAL) {
        printf("  FAIL: Expected INPUT_STYLE_HORIZONTAL for 'horizontal'\n");
        failures++;
    } else {
        printf("  PASS: 'horizontal' -> INPUT_STYLE_HORIZONTAL\n");
    }
    if (config_input_style_from_string("unknown") != INPUT_STYLE_BLAND) {
        printf("  FAIL: Expected INPUT_STYLE_BLAND for unknown value\n");
        failures++;
    } else {
        printf("  PASS: 'unknown' -> INPUT_STYLE_BLAND (default)\n");
    }
    if (config_input_style_from_string(NULL) != INPUT_STYLE_BLAND) {
        printf("  FAIL: Expected INPUT_STYLE_BLAND for NULL\n");
        failures++;
    } else {
        printf("  PASS: NULL -> INPUT_STYLE_BLAND (default)\n");
    }

    // Test 4: Save and load
    printf("\nTest 4: config_save() and config_load()\n");
    config.input_box_style = INPUT_STYLE_BORDER;
    if (config_save(&config) != 0) {
        printf("  FAIL: config_save() failed\n");
        failures++;
    } else {
        printf("  PASS: config_save() succeeded\n");

        // Clear and reload
        KlawedConfig loaded_config;
        config_init_defaults(&loaded_config);
        if (config_load(&loaded_config) != 0) {
            printf("  FAIL: config_load() failed\n");
            failures++;
        } else {
            printf("  PASS: config_load() succeeded\n");
            if (loaded_config.input_box_style != INPUT_STYLE_BORDER) {
                printf("  FAIL: Loaded style doesn't match saved style\n");
                failures++;
            } else {
                printf("  PASS: Loaded style matches saved style (border)\n");
            }
        }
    }

    // Test 5: Update existing config
    printf("\nTest 5: Update existing config\n");
    config.input_box_style = INPUT_STYLE_BACKGROUND;
    if (config_save(&config) != 0) {
        printf("  FAIL: config_save() update failed\n");
        failures++;
    } else {
        printf("  PASS: config_save() update succeeded\n");

        KlawedConfig loaded_config;
        config_init_defaults(&loaded_config);
        if (config_load(&loaded_config) != 0) {
            printf("  FAIL: config_load() after update failed\n");
            failures++;
        } else {
            if (loaded_config.input_box_style != INPUT_STYLE_BACKGROUND) {
                printf("  FAIL: Updated style doesn't match (expected background)\n");
                failures++;
            } else {
                printf("  PASS: Updated style matches (background)\n");
            }
        }
    }

    printf("\n=== Results ===\n");
    if (failures == 0) {
        printf("All tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed!\n", failures);
        return 1;
    }
}
