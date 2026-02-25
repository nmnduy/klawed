#include <assert.h>
#include <stdio.h>
#include <ncurses.h>
#include "../src/window_manager.h"

static void test_pad_capacity_growth(void) {
    WindowManager wm = {0};

    // Initialize curses and WM
    initscr();
    noecho();
    cbreak();

    int rc = window_manager_init(&wm, &DEFAULT_WINDOW_CONFIG);
    assert(rc == 0);
    assert(wm.is_initialized);
    assert(wm.conv_pad != NULL);

    int initial_cap = wm.conv_pad_capacity;
    int request = initial_cap * 2 + 123;

    rc = window_manager_ensure_pad_capacity(&wm, request);
    assert(rc == 0);
    assert(wm.conv_pad_capacity >= request);
    assert(wm.conv_pad != NULL);

    window_manager_destroy(&wm);
    endwin();
}

static void test_input_resize_affects_layout(void) {
    WindowManager wm = {0};

    initscr();
    noecho();
    cbreak();

    int rc = window_manager_init(&wm, &DEFAULT_WINDOW_CONFIG);
    assert(rc == 0);

    int screen_h = wm.screen_height;
    int screen_w = wm.screen_width;
    (void)screen_w;

    int old_input_h = wm.input_height;
    int old_status_h = wm.status_height;
    int old_conv_vh = wm.conv_viewport_height;

    // Request a taller input area (within allowed range)
    int desired_content_lines = 3; // results in window height 5 with borders
    rc = window_manager_resize_input(&wm, desired_content_lines);
    assert(rc == 0);

    // Validate invariants
    assert(wm.input_height >= wm.config.min_input_height);
    assert(wm.input_height <= wm.config.max_input_height);

    int expected_conv_vh = screen_h - wm.input_height - wm.status_height - wm.config.padding;
    if (expected_conv_vh < wm.config.min_conv_height) expected_conv_vh = wm.config.min_conv_height;
    assert(wm.conv_viewport_height == expected_conv_vh);

    // Ensure something actually changed (unless clamped equal)
    if (wm.input_height != old_input_h || wm.status_height != old_status_h) {
        assert(wm.conv_viewport_height != old_conv_vh);
    }

    window_manager_destroy(&wm);
    endwin();
}

int main(void) {
    printf("[WM TEST] pad capacity growth...\n");
    test_pad_capacity_growth();
    printf("[WM TEST] input resize affects layout...\n");
    test_input_resize_affects_layout();
    printf("[WM TEST] all tests passed.\n");
    return 0;
}
