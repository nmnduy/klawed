/*
 * indicators.h - Visual indicators for tool execution and API calls
 *
 * Provides animated spinners and status indicators for GPU-accelerated terminals
 * Supports Unicode characters and smooth ANSI animations
 */

#ifndef INDICATORS_H
#define INDICATORS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include "fallback_colors.h"
#include "logger.h"
#define COLORSCHEME_EXTERN
#include "colorscheme.h"
#include "spinner_effects.h"

// Standard spinner frames
static const char *SPINNER_FRAMES[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
static const int SPINNER_FRAME_COUNT = 10;
static const int SPINNER_DELAY_MS = 40;

// Alternative spinner styles
static const char *SPINNER_DOTS[]   = {"⣾","⣽","⣻","⢿","⡿","⣟","⣯","⣷"};
static const char *SPINNER_LINE[]   = {"-","\\","|","/"};
static const char *SPINNER_BOX[]    = {"◰","◳","◲","◱"};
static const char *SPINNER_CIRCLE[] = {"◜","◠","◝","◞","◡","◟"};
// Audio visualizer styles using braille characters (like btop graphs)
// Wave pattern - synchronized bounce
static const char *SPINNER_BARS_WAVE[] = {
    "⣀⣠⣤⣀",
    "⣠⣴⣶⣤",
    "⣤⣶⣿⣶",
    "⣶⣿⣷⣾",
    "⣿⣷⣶⣿",
    "⣷⣶⣴⣾",
    "⣶⣴⣠⣶",
    "⣴⣠⣀⣴",
    "⣠⣀⣀⣠",
    "⣀⣀⣠⣀",
    "⣀⣠⣴⣠",
    "⣠⣴⣶⣴"
};
// Ripple pattern - wave moves left to right
static const char *SPINNER_BARS_RIPPLE[] = {
    "⣿⣶⣤⣀",
    "⣷⣿⣶⣤",
    "⣤⣷⣿⣶",
    "⣀⣤⣷⣿",
    "⣀⣀⣤⣷",
    "⣀⣀⣀⣤",
    "⣤⣀⣀⣀",
    "⣶⣤⣀⣀",
    "⣷⣶⣤⣀",
    "⣿⣷⣶⣤",
    "⣶⣿⣷⣶",
    "⣤⣶⣿⣷"
};
// Bounce pattern - alternating peaks
static const char *SPINNER_BARS_BOUNCE[] = {
    "⣿⣀⣿⣀",
    "⣶⣠⣶⣠",
    "⣤⣴⣤⣴",
    "⣠⣶⣠⣶",
    "⣀⣿⣀⣿",
    "⣠⣶⣠⣶",
    "⣤⣴⣤⣴",
    "⣶⣠⣶⣠"
};
// Random-ish pattern - chaotic equalizer
static const char *SPINNER_BARS_RANDOM[] = {
    "⣿⣤⣶⣠",
    "⣶⣿⣠⣴",
    "⣤⣶⣿⣤",
    "⣠⣤⣶⣿",
    "⣴⣠⣤⣶",
    "⣶⣴⣠⣤",
    "⣤⣶⣴⣠",
    "⣠⣤⣶⣴",
    "⣿⣠⣴⣶",
    "⣴⣿⣶⣠"
};
// Pulse pattern - center expands outward
static const char *SPINNER_BARS_PULSE[] = {
    "⣀⣀⣀⣀",
    "⣀⣠⣠⣀",
    "⣀⣴⣴⣀",
    "⣠⣶⣶⣠",
    "⣤⣿⣿⣤",
    "⣶⣿⣿⣶",
    "⣿⣿⣿⣿",
    "⣶⣿⣿⣶",
    "⣤⣿⣿⣤",
    "⣠⣶⣶⣠",
    "⣀⣴⣴⣀",
    "⣀⣠⣠⣀"
};

#if defined(__GNUC__) || defined(__clang__)
#define INDICATOR_UNUSED __attribute__((unused))
#else
#define INDICATOR_UNUSED
#endif

// Color accessors
static inline const char* get_spinner_color_status(void) {
    static char buf[32]; static int w=0;
    if (get_colorscheme_color(COLORSCHEME_STATUS, buf, sizeof(buf))==0) return buf;
    if (!w) { LOG_WARN("Using fallback color for spinner (status)"); w=1; }
    return ANSI_FALLBACK_YELLOW;
}
static inline const char* get_spinner_color_tool(void) {
    static char buf[32]; static int w=0;
    if (get_colorscheme_color(COLORSCHEME_TOOL, buf, sizeof(buf))==0) return buf;
    if (!w) { LOG_WARN("Using fallback color for spinner (tool)"); w=1; }
    return ANSI_FALLBACK_CYAN;
}
static inline const char* get_spinner_color_success(void) {
    static char buf[32]; static int w=0;
    if (get_colorscheme_color(COLORSCHEME_USER, buf, sizeof(buf))==0) return buf;
    if (!w) { LOG_WARN("Using fallback color for spinner (success)"); w=1; }
    return ANSI_FALLBACK_GREEN;
}
static inline const char* get_spinner_color_error(void) {
    static char buf[32]; static int w=0;
    if (get_colorscheme_color(COLORSCHEME_ERROR, buf, sizeof(buf))==0) return buf;
    if (!w) { LOG_WARN("Using fallback color for spinner (error)"); w=1; }
    return ANSI_FALLBACK_ERROR;
}

#define SPINNER_CYAN    get_spinner_color_tool()
#define SPINNER_YELLOW  get_spinner_color_status()
#define SPINNER_GREEN   get_spinner_color_success()
#define SPINNER_RESET   ANSI_RESET

// Collection of spinner variants
typedef struct { const char **frames; int count; } spinner_variant_t;
static const spinner_variant_t SPINNER_VARIANTS[] = {
    { SPINNER_FRAMES,  SPINNER_FRAME_COUNT },
    { SPINNER_DOTS,    (int)(sizeof(SPINNER_DOTS)/sizeof(*SPINNER_DOTS)) },
    { SPINNER_LINE,    (int)(sizeof(SPINNER_LINE)/sizeof(*SPINNER_LINE)) },
    { SPINNER_BOX,     (int)(sizeof(SPINNER_BOX)/sizeof(*SPINNER_BOX)) },
    { SPINNER_CIRCLE,  (int)(sizeof(SPINNER_CIRCLE)/sizeof(*SPINNER_CIRCLE)) },
    { SPINNER_BARS_WAVE,   (int)(sizeof(SPINNER_BARS_WAVE)/sizeof(*SPINNER_BARS_WAVE)) },
    { SPINNER_BARS_RIPPLE, (int)(sizeof(SPINNER_BARS_RIPPLE)/sizeof(*SPINNER_BARS_RIPPLE)) },
    { SPINNER_BARS_BOUNCE, (int)(sizeof(SPINNER_BARS_BOUNCE)/sizeof(*SPINNER_BARS_BOUNCE)) },
    { SPINNER_BARS_RANDOM, (int)(sizeof(SPINNER_BARS_RANDOM)/sizeof(*SPINNER_BARS_RANDOM)) },
    { SPINNER_BARS_PULSE,  (int)(sizeof(SPINNER_BARS_PULSE)/sizeof(*SPINNER_BARS_PULSE)) }
};
static const int SPINNER_VARIANT_COUNT = (int)(sizeof(SPINNER_VARIANTS)/sizeof(*SPINNER_VARIANTS));

// Wave form spinner variants (indices 5-9 in SPINNER_VARIANTS)
static const int WAVE_VARIANT_INDICES[] = {5, 6, 7, 8, 9};
static const int WAVE_VARIANT_COUNT = (int)(sizeof(WAVE_VARIANT_INDICES)/sizeof(*WAVE_VARIANT_INDICES));

// Global spinner variant: wave form only
static spinner_variant_t GLOBAL_SPINNER_VARIANT = {NULL,0};
static void init_global_spinner_variant(void) {
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned)time(NULL));
        seeded = 1;
    }
    // Pick a random wave variant each time (so each spinner session looks different)
    int idx = WAVE_VARIANT_INDICES[rand() % WAVE_VARIANT_COUNT];
    GLOBAL_SPINNER_VARIANT = SPINNER_VARIANTS[idx];
}

// Get a random wave variant
static spinner_variant_t get_random_wave_variant(void) {
    int idx = WAVE_VARIANT_INDICES[rand() % WAVE_VARIANT_COUNT];
    return SPINNER_VARIANTS[idx];
}

// Spinner object
typedef struct {
    pthread_t thread;
    int running;
    char *message;
    const char *color;
    pthread_mutex_t lock;
    const char **frames;
    int frame_count;
    SpinnerEffectConfig effect_config;
    uint64_t last_update_ns;
    float speed_multiplier;           // Speed multiplier for animation (1.0 = normal)
    uint64_t speed_transition_ns;     // Timestamp when speed transition started
    int speed_transition_active;      // Whether speed transition is active
} Spinner;

static void *spinner_thread_func(void *arg) {
    Spinner *s = (Spinner*)arg;
    int idx = 0;
    uint64_t last_frame_ns = 0;

    // Hide cursor
    printf("\033[?25l"); fflush(stdout);

    // Initialize effect config if not already done
    if (s->effect_config.effect_type == SPINNER_EFFECT_NONE) {
        spinner_effect_init(&s->effect_config, SPINNER_EFFECT_GRADIENT,
                            SPINNER_COLOR_SOLID,
                            s->color, NULL);
    }

    while (1) {
        pthread_mutex_lock(&s->lock);
        if (!s->running) { pthread_mutex_unlock(&s->lock); break; }

        // Calculate delta time for smooth animations
        uint64_t now_ns = 0;
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
            now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
        }

        float delta_time = 0.0f;
        if (last_frame_ns > 0 && now_ns > last_frame_ns) {
            delta_time = (float)(now_ns - last_frame_ns) / 1e9f;
        }
        last_frame_ns = now_ns;

        // Handle speed transitions (fast -> normal when message changes)
        float current_speed = s->speed_multiplier;
        if (s->speed_transition_active && now_ns > 0) {
            // Transition duration: 0.5 seconds
            const uint64_t TRANSITION_DURATION_NS = 500000000ULL; // 500ms
            uint64_t elapsed = now_ns - s->speed_transition_ns;

            if (elapsed >= TRANSITION_DURATION_NS) {
                // Transition complete - back to normal speed
                s->speed_multiplier = 1.0f;
                s->speed_transition_active = 0;
            } else {
                // Smooth transition from fast (3.0) to normal (1.0)
                float progress = (float)elapsed / (float)TRANSITION_DURATION_NS;
                s->speed_multiplier = 3.0f - (2.0f * progress); // 3.0 -> 1.0
            }
            current_speed = s->speed_multiplier;
        }

        // Randomly pick a wave variant for this frame
        spinner_variant_t variant = get_random_wave_variant();
        const char **frames = variant.frames;
        int frame_count = variant.count;

        // Update effect phase
        spinner_effect_update_phase(&s->effect_config, delta_time, idx, frame_count);

        // Get color for current frame with effects
        char effect_color[32];
        spinner_effect_get_color(&s->effect_config, idx, effect_color, sizeof(effect_color));

        printf("\r\033[K%s%s%s %s", effect_color, frames[idx], SPINNER_RESET, s->message);
        fflush(stdout);
        pthread_mutex_unlock(&s->lock);

        idx = (idx + 1) % frame_count;

        // Apply speed multiplier to delay (lower delay = faster spin)
        int delay_ms = (int)((float)SPINNER_DELAY_MS / current_speed);
        if (delay_ms < 20) delay_ms = 20; // Minimum delay to avoid excessive CPU
        usleep((unsigned int)(delay_ms * 1000));
    }

    // Restore cursor
    printf("\033[?25h"); fflush(stdout);
    return NULL;
}

// Start spinner; style fixed once per lifecycle
static INDICATOR_UNUSED Spinner* spinner_start(const char *message, const char *color) {
    init_global_spinner_variant();
    Spinner *s = malloc(sizeof(Spinner)); if (!s) return NULL;
    s->message = strdup(message);
    s->color = color ? color : SPINNER_CYAN;
    s->running = 1;
    pthread_mutex_init(&s->lock, NULL);
    s->frames = GLOBAL_SPINNER_VARIANT.frames;
    s->frame_count = GLOBAL_SPINNER_VARIANT.count;
    spinner_effect_init(&s->effect_config, SPINNER_EFFECT_PULSE,
                        SPINNER_COLOR_SOLID,
                        s->color, NULL);
    s->last_update_ns = 0;
    s->speed_multiplier = 1.0f;
    s->speed_transition_ns = 0;
    s->speed_transition_active = 0;
    pthread_create(&s->thread, NULL, spinner_thread_func, s);
    return s;
}

// Start spinner with specific effect
static INDICATOR_UNUSED Spinner* spinner_start_with_effect(const char *message,
                                                          const char *color,
                                                          SpinnerEffectType effect_type,
                                                          SpinnerColorMode color_mode) {
    init_global_spinner_variant();
    Spinner *s = malloc(sizeof(Spinner)); if (!s) return NULL;
    s->message = strdup(message);
    s->color = color ? color : SPINNER_CYAN;
    s->running = 1;
    pthread_mutex_init(&s->lock, NULL);
    s->frames = GLOBAL_SPINNER_VARIANT.frames;
    s->frame_count = GLOBAL_SPINNER_VARIANT.count;
    spinner_effect_init(&s->effect_config, effect_type, color_mode, s->color, NULL);
    s->last_update_ns = 0;
    s->speed_multiplier = 1.0f;
    s->speed_transition_ns = 0;
    s->speed_transition_active = 0;
    pthread_create(&s->thread, NULL, spinner_thread_func, s);
    return s;
}

// Update spinner text
static INDICATOR_UNUSED void spinner_update(Spinner *s, const char *msg) {
    if (!s) return;
    pthread_mutex_lock(&s->lock);

    // Check if message is actually changing
    int message_changed = (strcmp(s->message, msg) != 0);

    free(s->message);
    s->message = strdup(msg);

    // If message changed, trigger speed boost (fast -> normal transition)
    if (message_changed) {
        s->speed_multiplier = 3.0f; // Start at 3x speed
        s->speed_transition_active = 1;

        // Get current timestamp for transition
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
            s->speed_transition_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
        }
    }

    pthread_mutex_unlock(&s->lock);
}

// Stop spinner and display final state
static INDICATOR_UNUSED void spinner_stop(Spinner *s, const char *final_message, int success) {
    if (!s) return;
    pthread_mutex_lock(&s->lock);
    s->running = 0;
    pthread_mutex_unlock(&s->lock);
    pthread_join(s->thread, NULL);
    printf("\r\033[K");
    if (final_message) {
        if (success) printf("%s✓%s %s\n", SPINNER_GREEN, SPINNER_RESET, final_message);
        else         printf("%s✗%s %s\n", get_spinner_color_error(), SPINNER_RESET, final_message);
    }
    fflush(stdout);
    pthread_mutex_destroy(&s->lock);
    free(s->message);
    free(s);
}

#endif // INDICATORS_H
