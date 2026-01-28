/*
 * spring.h - Physics-based spring animation library
 *
 * Based on harmonica by Charmbracelet (https://github.com/charmbracelet/harmonica)
 * Ported to C with NASA C coding standards compliance.
 *
 * This provides smooth, natural motion using damped harmonic oscillators.
 * Perfect for spinners, UI animations, and any motion that should feel organic.
 *
 * Usage:
 *   Spring spring;
 *   spring_init(&spring, 1.0/60.0, 8.0, 0.3);  // 60 FPS, fast, slightly bouncy
 *   // In animation loop:
 *   spring_update(spring, &pos, &vel, target);
 */

#ifndef SPRING_H
#define SPRING_H

#include <math.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Spring physics configuration and cached coefficients.
 * Pre-compute these once, then reuse for efficient updates.
 */
typedef struct {
    double pos_pos_coef;  // Position coefficient for position
    double pos_vel_coef;  // Position coefficient for velocity
    double vel_pos_coef;  // Velocity coefficient for position
    double vel_vel_coef;  // Velocity coefficient for velocity
} Spring;

/**
 * Time delta for a given framerate.
 * @param fps Frames per second
 * @return Time step in seconds
 */
static inline double spring_fps(double fps) {
    return 1.0 / fps;
}

/**
 * Initialize a Spring with framerate, angular frequency, and damping ratio.
 *
 * @param s           Output: Initialized Spring with pre-computed coefficients
 * @param delta_time  Animation time step in seconds (e.g., 1/60.0 for 60 FPS)
 * @param frequency   Angular frequency (speed): higher = faster
 * @param damping     Damping ratio: < 1 = bouncy, 1 = no overshoot, > 1 = sluggish
 */
static inline void spring_init(Spring *s, double delta_time, double frequency, double damping) {
    s->pos_pos_coef = 0.0;
    s->pos_vel_coef = 0.0;
    s->vel_pos_coef = 0.0;
    s->vel_vel_coef = 0.0;

    // Keep values in valid range
    frequency = fmax(0.0, frequency);
    damping = fmax(0.0, damping);

    // Machine epsilon for float comparisons
    const double epsilon = 1e-10;

    // No frequency = no motion
    if (frequency < epsilon) {
        s->pos_pos_coef = 1.0;
        s->pos_vel_coef = 0.0;
        s->vel_pos_coef = 0.0;
        s->vel_vel_coef = 1.0;
        return;
    }

    if (damping > 1.0 + epsilon) {
        /* Over-damped: No oscillation, slow settling */
        double za = -frequency * damping;
        double zb = frequency * sqrt(damping * damping - 1.0);
        double z1 = za - zb;
        double z2 = za + zb;

        double e1 = exp(z1 * delta_time);
        double e2 = exp(z2 * delta_time);

        double inv_two_zb = 1.0 / (2.0 * zb);
        double e1_over_two_zb = e1 * inv_two_zb;
        double e2_over_two_zb = e2 * inv_two_zb;
        double z1e1_over_two_zb = z1 * e1_over_two_zb;
        double z2e2_over_two_zb = z2 * e2_over_two_zb;

        s->pos_pos_coef = e1_over_two_zb * z2 - z2e2_over_two_zb + e2;
        s->pos_vel_coef = -e1_over_two_zb + e2_over_two_zb;
        s->vel_pos_coef = (z1e1_over_two_zb - z2e2_over_two_zb + e2) * z2;
        s->vel_vel_coef = -z1e1_over_two_zb + z2e2_over_two_zb;

    } else if (damping < 1.0 - epsilon) {
        /* Under-damped: Fastest with oscillation and bounce */
        double omega_zeta = frequency * damping;
        double alpha = frequency * sqrt(1.0 - damping * damping);

        double exp_term = exp(-omega_zeta * delta_time);
        double cos_term = cos(alpha * delta_time);
        double sin_term = sin(alpha * delta_time);

        double inv_alpha = 1.0 / alpha;
        double exp_sin = exp_term * sin_term;
        double exp_cos = exp_term * cos_term;
        double exp_omega_zeta_sin_over_alpha = exp_term * omega_zeta * sin_term * inv_alpha;

        s->pos_pos_coef = exp_cos + exp_omega_zeta_sin_over_alpha;
        s->pos_vel_coef = exp_sin * inv_alpha;
        s->vel_pos_coef = -exp_sin * alpha - omega_zeta * exp_omega_zeta_sin_over_alpha;
        s->vel_vel_coef = exp_cos - exp_omega_zeta_sin_over_alpha;

    } else {
        /* Critically-damped: Fastest without oscillation */
        double exp_term = exp(-frequency * delta_time);
        double time_exp = delta_time * exp_term;
        double time_exp_freq = time_exp * frequency;

        s->pos_pos_coef = time_exp_freq + exp_term;
        s->pos_vel_coef = time_exp;
        s->vel_pos_coef = -frequency * time_exp_freq;
        s->vel_vel_coef = -time_exp_freq + exp_term;
    }
}

/**
 * Update spring position and velocity towards a target.
 *
 * @param spring         Pre-initialized Spring with coefficients
 * @param pos            Current position
 * @param vel            Current velocity
 * @param target_pos     Target position to move towards
 * @return               New position and velocity
 */
static inline void spring_update(
    Spring spring,
    double *pos,
    double *vel,
    double target_pos
) {
    double old_pos = *pos - target_pos;
    double old_vel = *vel;

    *pos = old_pos * spring.pos_pos_coef + old_vel * spring.pos_vel_coef + target_pos;
    *vel = old_pos * spring.vel_pos_coef + old_vel * spring.vel_vel_coef;
}

/**
 * Spring preset: Bouncy and playful (good for spinners)
 * Frequency: 10 Hz, Damping: 0.25 (under-damped)
 */
static inline double spring_preset_bouncy_fps(void) {
    return 60.0;
}

/**
 * Spring preset: Smooth and natural
 * Frequency: 8 Hz, Damping: 0.4 (slightly under-damped)
 */
static inline double spring_preset_smooth_fps(void) {
    return 60.0;
}

/**
 * Spring preset: Quick snap to target
 * Frequency: 15 Hz, Damping: 0.6 (closer to critical)
 */
static inline double spring_preset_quick_fps(void) {
    return 60.0;
}

#ifdef __cplusplus
}
#endif

#endif /* SPRING_H */
