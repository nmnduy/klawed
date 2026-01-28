/*
 * spinner_effects.h - Enhanced spinner effects with color gradients and animations
 *
 * Provides advanced spinner effects including:
 * - Color gradients across frames
 * - Smooth color transitions
 * - Pulse, fade, and rainbow effects
 * - Configurable animation parameters
 */

#ifndef SPINNER_EFFECTS_H
#define SPINNER_EFFECTS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Fallback definition for M_PI if not provided by math.h
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "fallback_colors.h"
#include "logger.h"

// Spinner effect types
typedef enum {
    SPINNER_EFFECT_NONE = 0,      // No effect, solid color
    SPINNER_EFFECT_GRADIENT,      // Color gradient across frames
    SPINNER_EFFECT_PULSE,         // Pulsing brightness
    SPINNER_EFFECT_FADE,          // Fade in/out
    SPINNER_EFFECT_RAINBOW,       // Rainbow colors
    SPINNER_EFFECT_COUNT
} SpinnerEffectType;

// Spinner color mode
typedef enum {
    SPINNER_COLOR_SOLID = 0,      // Single color
    SPINNER_COLOR_GRADIENT,       // Gradient between two colors
    SPINNER_COLOR_RAINBOW         // Rainbow spectrum
} SpinnerColorMode;

// Configuration for spinner effects
typedef struct {
    SpinnerEffectType effect_type;
    SpinnerColorMode color_mode;

    // Base colors (ANSI color codes or 256-color codes)
    const char* base_color;
    const char* secondary_color;  // For gradients

    // Animation parameters
    float speed_multiplier;       // Animation speed (1.0 = normal)
    float intensity;              // Effect intensity (0.0 to 1.0)

    // Internal state
    float phase;                  // Current animation phase (0.0 to 1.0)
    int frame_count;              // Number of frames in current variant
} SpinnerEffectConfig;

// Convert HSV to RGB (H: 0-360, S: 0-1, V: 0-1)
// For rainbow effect: S=1, V=1, just vary H
static inline void hsv_to_rgb(float h, float s, float v,
                              int* r_out, int* g_out, int* b_out) {
    // Normalize hue to 0-360 range
    while (h >= 360.0f) h -= 360.0f;
    while (h < 0.0f) h += 360.0f;

    float c = v * s;  // Chroma
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r = 0.0f, g = 0.0f, b = 0.0f;

    if (h < 60.0f) {
        r = c; g = x; b = 0.0f;
    } else if (h < 120.0f) {
        r = x; g = c; b = 0.0f;
    } else if (h < 180.0f) {
        r = 0.0f; g = c; b = x;
    } else if (h < 240.0f) {
        r = 0.0f; g = x; b = c;
    } else if (h < 300.0f) {
        r = x; g = 0.0f; b = c;
    } else {
        r = c; g = 0.0f; b = x;
    }

    // Convert to 0-255 range and apply value offset
    *r_out = (int)((r + m) * 255.0f);
    *g_out = (int)((g + m) * 255.0f);
    *b_out = (int)((b + m) * 255.0f);

    // Clamp values
    if (*r_out > 255) *r_out = 255;
    if (*g_out > 255) *g_out = 255;
    if (*b_out > 255) *b_out = 255;
    if (*r_out < 0) *r_out = 0;
    if (*g_out < 0) *g_out = 0;
    if (*b_out < 0) *b_out = 0;
}

// Initialize spinner effect configuration (output parameter to avoid aggregate return)
static inline void spinner_effect_init(SpinnerEffectConfig *config,
                                       SpinnerEffectType effect_type,
                                       SpinnerColorMode color_mode,
                                       const char* base_color,
                                       const char* secondary_color) {
    config->effect_type = effect_type;
    config->color_mode = color_mode;
    config->base_color = base_color;
    config->secondary_color = secondary_color;
    config->speed_multiplier = 1.0f;
    config->intensity = 1.0f;
    config->phase = 0.0f;
    config->frame_count = 10;  // Default frame count
}

// Update animation phase based on elapsed time
static inline void spinner_effect_update_phase(SpinnerEffectConfig* config,
                                              float delta_time,
                                              int current_frame,
                                              int total_frames) {
    if (!config) return;

    float frame_progress = (float)current_frame / (float)total_frames;

    switch (config->effect_type) {
        case SPINNER_EFFECT_GRADIENT:
            // Gradient follows frame progression
            config->phase = frame_progress;
            break;

        case SPINNER_EFFECT_PULSE:
            // Pulse at 2Hz (0.5 second period)
            config->phase = fmodf(config->phase + delta_time * config->speed_multiplier * 2.0f, 1.0f);
            break;

        case SPINNER_EFFECT_FADE:
            // Fade in/out at 1Hz (1 second period)
            config->phase = fmodf(config->phase + delta_time * config->speed_multiplier, 1.0f);
            break;

        case SPINNER_EFFECT_RAINBOW:
            // Rainbow cycles through hue at 0.5Hz (2 second period)
            config->phase = fmodf(config->phase + delta_time * config->speed_multiplier * 0.5f, 1.0f);
            break;

        case SPINNER_EFFECT_NONE:
            // No animation, phase follows frame
            config->phase = frame_progress;
            break;

        case SPINNER_EFFECT_COUNT:
        default:
            // Should not happen, but handle gracefully
            config->phase = frame_progress;
            break;
    }

    config->frame_count = total_frames;
}

// Get color for current frame based on effect configuration
static inline void spinner_effect_get_color(const SpinnerEffectConfig* config,
                                           int frame_index,
                                           char* color_buffer,
                                           size_t buffer_size) {
    if (!config || !color_buffer || buffer_size < 32) {
        if (color_buffer && buffer_size > 0) {
            snprintf(color_buffer, buffer_size, "%s", ANSI_FALLBACK_CYAN);
        }
        return;
    }

    // Calculate effect value based on phase and frame
    float frame_progress = (float)frame_index / (float)config->frame_count;

    switch (config->effect_type) {
        case SPINNER_EFFECT_PULSE: {
            // Sine wave for pulsing: 0.5 + 0.5*sin(2π*phase)
            // This gives a smooth oscillation between 0.0 and 1.0
            float pulse = 0.5f + 0.5f * sinf(config->phase * 2.0f * (float)M_PI);
            // Apply intensity to the pulse effect
            float brightness = 0.3f + 0.7f * pulse * config->intensity;

            // Default base color is cyan (0, 255, 255)
            int base_r = 0, base_g = 255, base_b = 255;

            // Try to parse base_color if it's a true color escape sequence
            // Format: \033[38;2;R;G;Bm
            if (config->base_color) {
                int r, g, b;
                if (sscanf(config->base_color, "\033[38;2;%d;%d;%dm", &r, &g, &b) == 3) {
                    base_r = r;
                    base_g = g;
                    base_b = b;
                } else if (sscanf(config->base_color, "\x1b[38;2;%d;%d;%dm", &r, &g, &b) == 3) {
                    base_r = r;
                    base_g = g;
                    base_b = b;
                }
                // If parsing fails, we use the default cyan color
            }

            // Modulate RGB values by brightness
            int mod_r = (int)((float)base_r * brightness);
            int mod_g = (int)((float)base_g * brightness);
            int mod_b = (int)((float)base_b * brightness);

            // Clamp values to valid range
            if (mod_r > 255) mod_r = 255;
            if (mod_g > 255) mod_g = 255;
            if (mod_b > 255) mod_b = 255;
            if (mod_r < 0) mod_r = 0;
            if (mod_g < 0) mod_g = 0;
            if (mod_b < 0) mod_b = 0;

            // Output as true color (24-bit) ANSI escape sequence
            snprintf(color_buffer, buffer_size, "\033[38;2;%d;%d;%dm", mod_r, mod_g, mod_b);
            return;
        }

        case SPINNER_EFFECT_FADE: {
            // Triangle wave for fade in/out: linear ramp up, then linear ramp down
            // This gives a smooth 0.0 -> 1.0 -> 0.0 transition
            float fade = 2.0f * config->phase;
            if (fade > 1.0f) fade = 2.0f - fade;
            // Apply intensity and use a wider brightness range (0.1 to 1.0)
            // for a more dramatic fade effect compared to pulse
            float brightness = 0.1f + 0.9f * fade * config->intensity;

            // Default base color is cyan (0, 255, 255)
            int base_r = 0, base_g = 255, base_b = 255;

            // Try to parse base_color if it's a true color escape sequence
            // Format: \033[38;2;R;G;Bm
            if (config->base_color) {
                int r, g, b;
                if (sscanf(config->base_color, "\033[38;2;%d;%d;%dm", &r, &g, &b) == 3) {
                    base_r = r;
                    base_g = g;
                    base_b = b;
                } else if (sscanf(config->base_color, "\x1b[38;2;%d;%d;%dm", &r, &g, &b) == 3) {
                    base_r = r;
                    base_g = g;
                    base_b = b;
                }
                // If parsing fails, we use the default cyan color
            }

            // Modulate RGB values by brightness
            int mod_r = (int)((float)base_r * brightness);
            int mod_g = (int)((float)base_g * brightness);
            int mod_b = (int)((float)base_b * brightness);

            // Clamp values to valid range
            if (mod_r > 255) mod_r = 255;
            if (mod_g > 255) mod_g = 255;
            if (mod_b > 255) mod_b = 255;
            if (mod_r < 0) mod_r = 0;
            if (mod_g < 0) mod_g = 0;
            if (mod_b < 0) mod_b = 0;

            // Output as true color (24-bit) ANSI escape sequence
            snprintf(color_buffer, buffer_size, "\033[38;2;%d;%d;%dm", mod_r, mod_g, mod_b);
            return;
        }

        case SPINNER_EFFECT_RAINBOW: {
            // Rainbow cycles through the full hue spectrum (0-360 degrees)
            // Phase (0.0-1.0) maps directly to hue (0-360)
            float hue = config->phase * 360.0f;

            // Full saturation and value for vibrant rainbow colors
            float saturation = 1.0f;
            float value = config->intensity;  // Allow intensity to control brightness

            int r, g, b;
            hsv_to_rgb(hue, saturation, value, &r, &g, &b);

            // Output as true color (24-bit) ANSI escape sequence
            snprintf(color_buffer, buffer_size, "\033[38;2;%d;%d;%dm", r, g, b);
            return;
        }

        case SPINNER_EFFECT_GRADIENT:
        case SPINNER_EFFECT_NONE:
        case SPINNER_EFFECT_COUNT:
        default:
            // Linear progression for gradient
            // effect_value = frame_progress;
            (void)frame_progress; // Currently unused
            break;
    }

    // Apply color mode
    switch (config->color_mode) {
        case SPINNER_COLOR_GRADIENT: {
            // For now, use base color with intensity adjustment
            // In a more advanced implementation, we would interpolate between colors
            if (config->base_color) {
                snprintf(color_buffer, buffer_size, "%s", config->base_color);
            } else {
                snprintf(color_buffer, buffer_size, "%s", ANSI_FALLBACK_CYAN);
            }
            break;
        }

        case SPINNER_COLOR_RAINBOW: {
            // Map phase to hue in 256-color palette
            // Rainbow colors in 256-color palette (approximate)
            int rainbow_colors[] = {196, 202, 208, 214, 220, 226, 190, 154, 118, 82,
                                   46, 47, 48, 49, 50, 51, 45, 39, 33, 27, 21, 57,
                                   93, 129, 165, 201, 200, 199, 198, 197};
            int color_count = sizeof(rainbow_colors) / sizeof(rainbow_colors[0]);
            int color_idx = (int)(config->phase * (float)color_count) % color_count;
            snprintf(color_buffer, buffer_size, "\033[38;5;%dm", rainbow_colors[color_idx]);
            break;
        }

        case SPINNER_COLOR_SOLID:
        default: {
            if (config->base_color) {
                snprintf(color_buffer, buffer_size, "%s", config->base_color);
            } else {
                snprintf(color_buffer, buffer_size, "%s", ANSI_FALLBACK_CYAN);
            }
            break;
        }
    }
}

#endif // SPINNER_EFFECTS_H
