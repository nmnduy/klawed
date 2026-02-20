/*
 * api_timing.h - API Response Time Tracking with Rolling Average
 *
 * Tracks API response times normalized by input token count.
 * Uses a rolling window to compute a moving baseline for dynamic spinner speed.
 */

#ifndef API_TIMING_H
#define API_TIMING_H

#include <stdint.h>
#include <stddef.h>

#define API_TIMING_WINDOW_SIZE 10  // Number of recent calls to track

/**
 * API timing tracker - maintains rolling average of normalized response times
 *
 * normalized_time = response_duration_ms / input_tokens
 * This accounts for payload size differences between API calls.
 */
typedef struct {
    // Rolling window of recent normalized times (duration_ms / input_tokens)
    double recent_normalized_times[API_TIMING_WINDOW_SIZE];
    size_t count;                      // Number of entries in the window
    size_t index;                      // Current position for circular buffer

    // Running sum for efficient average calculation
    double sum_normalized;

    // Computed baseline (rolling average)
    double baseline_normalized;

    // Statistics
    double min_normalized;             // Minimum normalized time seen
    double max_normalized;             // Maximum normalized time seen
} APITimingTracker;

/**
 * Initialize an API timing tracker
 * Must be called before using the tracker
 */
void api_timing_init(APITimingTracker *tracker);

/**
 * Record a new API call result
 *
 * @param tracker    The timing tracker
 * @param duration_ms Response time in milliseconds
 * @param input_tokens Number of prompt/input tokens in the request
 *
 * If input_tokens is 0, the call is skipped (avoid division by zero)
 */
void api_timing_record(APITimingTracker *tracker, long duration_ms, int input_tokens);

/**
 * Get the current baseline normalized time
 *
 * Returns the rolling average of normalized times.
 * Returns 0.0 if no data has been recorded yet.
 */
double api_timing_get_baseline(const APITimingTracker *tracker);

/**
 * Calculate suggested spinner speed multiplier based on current API performance
 *
 * @param tracker       The timing tracker
 * @param current_duration_ms  Current API call duration
 * @param input_tokens  Current request's input tokens
 *
 * Returns a multiplier for the base spinner speed:
 *   > 1.0 = API is slower than baseline -> faster spinner
 *   < 1.0 = API is faster than baseline -> slower spinner
 *   = 1.0 = API is at baseline
 *
 * The multiplier is clamped between 0.2 and 3.0 to prevent extreme speeds.
 */
double api_timing_get_speed_multiplier(const APITimingTracker *tracker,
                                        long current_duration_ms,
                                        int input_tokens);

/**
 * Reset the tracker to initial state
 */
void api_timing_reset(APITimingTracker *tracker);

#endif // API_TIMING_H