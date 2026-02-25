/*
 * text_diffusion.h - Text diffusion effect for status messages
 *
 * Creates a visual effect similar to how diffusion models generate text:
 * - Starts with random/garbage characters
 * - Characters progressively "crystallize" into the target text
 * - Creates an emergence-from-noise aesthetic
 *
 * Usage:
 *   1. Initialize with text_diffusion_init()
 *   2. Set target text with text_diffusion_set_target()
 *   3. Call text_diffusion_update() periodically (e.g., every frame)
 *   4. Get current display text with text_diffusion_get_display()
 */

#ifndef TEXT_DIFFUSION_H
#define TEXT_DIFFUSION_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Maximum length of text that can be diffused
#define TEXT_DIFFUSION_MAX_LEN 256

// Diffusion state
typedef enum {
    TEXT_DIFFUSION_IDLE,       // No animation, showing final text
    TEXT_DIFFUSION_REVEALING,  // Animation in progress
    TEXT_DIFFUSION_COMPLETE    // Animation finished, showing target
} TextDiffusionState;

// Character set for random noise characters
// Using printable ASCII that looks "glitchy" but readable
static const char TEXT_DIFFUSION_NOISE_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    "0123456789!@#$%^&*()-=+[]{}|;:',.<>?/~`";

// Configuration for the diffusion effect
typedef struct {
    // Target text to reveal
    char target[TEXT_DIFFUSION_MAX_LEN];
    int target_len;

    // Current display buffer
    char display[TEXT_DIFFUSION_MAX_LEN];

    // Per-character reveal state (0.0 = noise, 1.0 = revealed)
    float char_progress[TEXT_DIFFUSION_MAX_LEN];

    // Per-character reveal threshold (randomized for organic feel)
    float char_threshold[TEXT_DIFFUSION_MAX_LEN];

    // Animation state
    TextDiffusionState state;
    float progress;           // Overall progress (0.0 to 1.0)

    // Timing
    float duration_seconds;   // Total animation duration
    uint64_t start_time_ns;   // Animation start timestamp
    uint64_t last_update_ns;  // Last update timestamp

    // Effect parameters
    float noise_density;      // How much noise at start (0.0-1.0, default 1.0)
    float reveal_spread;      // How spread out the reveal is (0.0-1.0, default 0.3)
    int preserve_spaces;      // Whether to show spaces immediately (default 1)
    int preserve_punctuation; // Whether to show punctuation immediately (default 0)

    // Random state
    unsigned int rand_seed;
} TextDiffusionConfig;

// Get monotonic time in nanoseconds
__attribute__((unused))
static uint64_t text_diffusion_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Simple random number generator (per-instance to avoid global state)
__attribute__((unused))
static unsigned int text_diffusion_rand(TextDiffusionConfig *config) {
    // LCG parameters (same as glibc)
    config->rand_seed = config->rand_seed * 1103515245 + 12345;
    return (config->rand_seed >> 16) & 0x7FFF;
}

// Get random float between 0.0 and 1.0
__attribute__((unused))
static float text_diffusion_randf(TextDiffusionConfig *config) {
    unsigned int r = text_diffusion_rand(config);
    return (float)r / 32767.0f;
}

// Get random noise character
__attribute__((unused))
static char text_diffusion_noise_char(TextDiffusionConfig *config) {
    int noise_len = (int)sizeof(TEXT_DIFFUSION_NOISE_CHARS) - 1;
    int idx = (int)(text_diffusion_rand(config) % (unsigned int)noise_len);
    return TEXT_DIFFUSION_NOISE_CHARS[idx];
}

// Check if character is punctuation
__attribute__((unused))
static int text_diffusion_is_punctuation(char c) {
    return (c == '.' || c == ',' || c == '!' || c == '?' ||
            c == ':' || c == ';' || c == '-' || c == '\'' ||
            c == '"' || c == '(' || c == ')' || c == '[' ||
            c == ']' || c == '{' || c == '}');
}

// Initialize diffusion config with defaults
__attribute__((unused))
static void text_diffusion_init(TextDiffusionConfig *config) {
    if (!config) return;

    memset(config, 0, sizeof(TextDiffusionConfig));

    config->state = TEXT_DIFFUSION_IDLE;
    config->progress = 1.0f;  // Start fully revealed (idle)
    config->duration_seconds = 1.2f;  // Default animation duration
    config->noise_density = 1.0f;
    config->reveal_spread = 0.35f;
    config->preserve_spaces = 1;
    config->preserve_punctuation = 0;

    // Seed with current time
    config->rand_seed = (unsigned int)time(NULL) ^ (unsigned int)text_diffusion_time_ns();
}

// Set new target text and start animation
__attribute__((unused))
static void text_diffusion_set_target(TextDiffusionConfig *config,
                                              const char *text) {
    if (!config || !text) return;

    // Check if target is the same (no need to re-animate)
    if (config->target_len > 0 && strcmp(config->target, text) == 0) {
        return;
    }

    // Copy target text
    size_t len = strlen(text);
    if (len >= TEXT_DIFFUSION_MAX_LEN) {
        len = TEXT_DIFFUSION_MAX_LEN - 1;
    }
    memcpy(config->target, text, len);
    config->target[len] = '\0';
    config->target_len = (int)len;

    // Initialize per-character thresholds with randomized values
    // This creates an organic, non-uniform reveal pattern
    for (int i = 0; i < config->target_len; i++) {
        config->char_progress[i] = 0.0f;

        // Randomize threshold so characters reveal at different times
        // Base threshold increases with position (left-to-right bias)
        float base = (float)i / (float)config->target_len;
        float random_offset = (text_diffusion_randf(config) - 0.5f) * config->reveal_spread * 2.0f;
        config->char_threshold[i] = base * 0.6f + random_offset + 0.1f;

        // Clamp to valid range
        if (config->char_threshold[i] < 0.05f) config->char_threshold[i] = 0.05f;
        if (config->char_threshold[i] > 0.95f) config->char_threshold[i] = 0.95f;
    }

    // Start animation
    config->state = TEXT_DIFFUSION_REVEALING;
    config->progress = 0.0f;
    config->start_time_ns = text_diffusion_time_ns();
    config->last_update_ns = config->start_time_ns;

    // Initialize display buffer with noise
    for (int i = 0; i < config->target_len; i++) {
        char c = config->target[i];

        // Optionally preserve spaces and punctuation
        if (config->preserve_spaces && c == ' ') {
            config->display[i] = ' ';
            config->char_progress[i] = 1.0f;  // Already revealed
        } else if (config->preserve_punctuation && text_diffusion_is_punctuation(c)) {
            config->display[i] = c;
            config->char_progress[i] = 1.0f;  // Already revealed
        } else {
            config->display[i] = text_diffusion_noise_char(config);
        }
    }
    config->display[config->target_len] = '\0';
}

// Update diffusion animation
// Returns 1 if display changed, 0 if no change
__attribute__((unused))
static int text_diffusion_update(TextDiffusionConfig *config) {
    if (!config) return 0;

    // Nothing to do if idle or complete
    if (config->state == TEXT_DIFFUSION_IDLE) {
        return 0;
    }

    if (config->state == TEXT_DIFFUSION_COMPLETE) {
        return 0;
    }

    uint64_t now = text_diffusion_time_ns();
    float elapsed_s = (float)(now - config->start_time_ns) / 1e9f;

    // Calculate overall progress
    config->progress = elapsed_s / config->duration_seconds;
    if (config->progress >= 1.0f) {
        config->progress = 1.0f;
        config->state = TEXT_DIFFUSION_COMPLETE;

        // Ensure final state is exactly the target
        memcpy(config->display, config->target, (size_t)config->target_len + 1);
        return 1;
    }

    int changed = 0;

    // Update each character
    for (int i = 0; i < config->target_len; i++) {
        // Skip already revealed characters
        if (config->char_progress[i] >= 1.0f) {
            continue;
        }

        char target_char = config->target[i];

        // Check if this character should be revealed based on progress
        if (config->progress >= config->char_threshold[i]) {
            // Character is now revealed
            config->display[i] = target_char;
            config->char_progress[i] = 1.0f;
            changed = 1;
        } else {
            // Character still in noise phase
            // Occasionally flicker to different noise characters
            float noise_update_chance = 0.3f;  // 30% chance per update to change noise
            if (text_diffusion_randf(config) < noise_update_chance) {
                // As we get closer to the threshold, increase chance of showing correct char
                float proximity = config->progress / config->char_threshold[i];
                if (proximity > 0.7f && text_diffusion_randf(config) < (proximity - 0.7f) * 2.0f) {
                    // Briefly flash the correct character (teasing reveal)
                    config->display[i] = target_char;
                } else {
                    config->display[i] = text_diffusion_noise_char(config);
                }
                changed = 1;
            }
        }
    }

    config->last_update_ns = now;
    return changed;
}

// Get current display text
__attribute__((unused))
static const char* text_diffusion_get_display(const TextDiffusionConfig *config) {
    if (!config) return "";

    // If no target set, return empty string
    if (config->target_len == 0) {
        return "";
    }

    return config->display;
}

// Check if animation is active
__attribute__((unused))
static int text_diffusion_is_active(const TextDiffusionConfig *config) {
    if (!config) return 0;
    return config->state == TEXT_DIFFUSION_REVEALING;
}

// Check if animation is complete
__attribute__((unused))
static int text_diffusion_is_complete(const TextDiffusionConfig *config) {
    if (!config) return 1;
    return config->state == TEXT_DIFFUSION_COMPLETE || config->state == TEXT_DIFFUSION_IDLE;
}

// Reset to idle state (clears animation)
__attribute__((unused))
static void text_diffusion_reset(TextDiffusionConfig *config) {
    if (!config) return;

    config->state = TEXT_DIFFUSION_IDLE;
    config->progress = 1.0f;
    config->target[0] = '\0';
    config->target_len = 0;
    config->display[0] = '\0';
}

// Skip animation and show final text immediately
__attribute__((unused))
static void text_diffusion_skip(TextDiffusionConfig *config) {
    if (!config) return;

    if (config->target_len > 0) {
        memcpy(config->display, config->target, (size_t)config->target_len + 1);
        for (int i = 0; i < config->target_len; i++) {
            config->char_progress[i] = 1.0f;
        }
    }
    config->state = TEXT_DIFFUSION_COMPLETE;
    config->progress = 1.0f;
}

// Set animation duration
__attribute__((unused))
static void text_diffusion_set_duration(TextDiffusionConfig *config,
                                                float seconds) {
    if (!config) return;
    if (seconds < 0.1f) seconds = 0.1f;
    if (seconds > 10.0f) seconds = 10.0f;
    config->duration_seconds = seconds;
}

// Set reveal spread (0.0 = uniform, 1.0 = very spread out)
__attribute__((unused))
static void text_diffusion_set_spread(TextDiffusionConfig *config,
                                              float spread) {
    if (!config) return;
    if (spread < 0.0f) spread = 0.0f;
    if (spread > 1.0f) spread = 1.0f;
    config->reveal_spread = spread;
}

#endif // TEXT_DIFFUSION_H
