/*
 * api_timing.c - API Response Time Tracking Implementation
 */

#define _POSIX_C_SOURCE 200809L

#include "api_timing.h"
#include <stdlib.h>
#include <string.h>

void api_timing_init(APITimingTracker *tracker) {
    if (!tracker) return;

    memset(tracker, 0, sizeof(APITimingTracker));
    tracker->min_normalized = 0.0;
    tracker->max_normalized = 0.0;
}

void api_timing_record(APITimingTracker *tracker, long duration_ms, int input_tokens) {
    if (!tracker) return;

    // Skip if no tokens (avoid division by zero)
    if (input_tokens <= 0) {
        return;
    }

    // Calculate normalized time (ms per token)
    double normalized = (double)duration_ms / (double)input_tokens;

    // Update statistics
    if (tracker->count == 0) {
        tracker->min_normalized = normalized;
        tracker->max_normalized = normalized;
    } else {
        if (normalized < tracker->min_normalized) {
            tracker->min_normalized = normalized;
        }
        if (normalized > tracker->max_normalized) {
            tracker->max_normalized = normalized;
        }
    }

    // If we're in a rolling window, subtract the value being replaced
    if (tracker->count == API_TIMING_WINDOW_SIZE) {
        tracker->sum_normalized -= tracker->recent_normalized_times[tracker->index];
    }

    // Add the new normalized time to the window
    tracker->recent_normalized_times[tracker->index] = normalized;
    tracker->sum_normalized += normalized;

    // Advance circular buffer index
    tracker->index = (tracker->index + 1) % API_TIMING_WINDOW_SIZE;

    // Update count (cap at window size)
    if (tracker->count < API_TIMING_WINDOW_SIZE) {
        tracker->count++;
    }

    // Update baseline (rolling average)
    if (tracker->count > 0) {
        tracker->baseline_normalized = tracker->sum_normalized / (double)tracker->count;
    }
}

double api_timing_get_baseline(const APITimingTracker *tracker) {
    if (!tracker) return 0.0;
    return tracker->baseline_normalized;
}

double api_timing_get_speed_multiplier(const APITimingTracker *tracker,
                                        long current_duration_ms,
                                        int input_tokens) {
    if (!tracker) return 1.0;

    // If no baseline established yet, use default speed
    if (tracker->count == 0 || tracker->baseline_normalized <= 0.0) {
        return 1.0;
    }

    // Skip if no tokens
    if (input_tokens <= 0) {
        return 1.0;
    }

    // Calculate current normalized time
    double current_normalized = (double)current_duration_ms / (double)input_tokens;

    // Calculate ratio: higher current value = slower API = faster spinner
    double ratio = current_normalized / tracker->baseline_normalized;

    // Clamp to reasonable range (0.2x to 3.0x)
    // This prevents the spinner from becoming too slow or too fast
    if (ratio < 0.2) {
        ratio = 0.2;
    } else if (ratio > 3.0) {
        ratio = 3.0;
    }

    return ratio;
}

void api_timing_reset(APITimingTracker *tracker) {
    if (!tracker) return;
    api_timing_init(tracker);
}