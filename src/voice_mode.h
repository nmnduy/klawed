/*
 * voice_mode.h - TUI Voice Mode handler
 *
 * Manages push-to-talk voice recording and real-time transcription
 * within the TUI. Activated by holding spacebar in INSERT mode.
 *
 * Architecture:
 *   - Recording: ffmpeg child process capturing microphone audio
 *   - Transcription: pluggable VoiceTranscriber backend (e.g., OpenAI)
 *   - Partial results displayed in the input buffer in real time
 *   - HOLD spacebar to record, RELEASE to stop and finalize
 *
 * Hold detection works via key-repeat: holding spacebar generates
 * repeated KEY_SPACE events. A short timeout (300ms) after the last
 * event signals release.
 */

#ifndef VOICE_MODE_H
#define VOICE_MODE_H

#include "tui.h"

/* ------------------------------------------------------------------ */
/* Voice mode state (owned by TUIState)                                 */
/* ------------------------------------------------------------------ */

#define VOICE_MODE_MAX_PARTIAL 4096

/*
 * Note: VoiceModeState is typedef'd in tui.h for forward declaration.
 * The full struct definition lives here.
 */
struct VoiceModeState {
    int active;                    /* 1 if recording/transcribing */

    /* ffmpeg recording process */
    pid_t ffmpeg_pid;              /* PID of ffmpeg child, 0 if not running */
    int   ffmpeg_pipe_fd;          /* File descriptor reading PCM from ffmpeg */

    /* Audio buffer (PCM read from ffmpeg pipe) */
    int16_t *audio_buf;           /* Circular-ish buffer of PCM samples */
    size_t   audio_buf_len;       /* Valid samples in buffer */
    size_t   audio_buf_cap;       /* Total capacity in samples */

    /* Hold-to-record detection */
    uint64_t last_spacebar_ns;    /* Monotonic timestamp of last spacebar event */
    int      recording;           /* 1 when actively recording */
    int      hold_timeout_ms;     /* Milliseconds of silence before release (default 300) */

    /* Transcription backend */
    struct VoiceTranscriber *transcriber; /* Active transcription session */

    /* Partial result (updated from callback, displayed in input area) */
    char partial_text[VOICE_MODE_MAX_PARTIAL];
    int  partial_dirty;           /* 1 when partial_text has new content to display */

    /* Original input buffer saved when entering voice mode */
    char *saved_buffer;
    int   saved_length;
    int   saved_cursor;

    /* Reference to TUI state (for updating input buffer) */
    TUIState *tui;
};

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */

/**
 * Initialize voice mode state.
 * Call once during TUI initialization.
 */
void voice_mode_init(VoiceModeState *vms, TUIState *tui);

/**
 * Cleanup voice mode state.
 * Stops any active recording and frees resources.
 */
void voice_mode_cleanup(VoiceModeState *vms);

/**
 * Enter voice mode (start recording).
 * Called when spacebar is pressed in INSERT mode.
 *
 * @param vms   Voice mode state
 * @param tui   TUI state
 * @return 0 on success, -1 if no transcription backend available
 */
int voice_mode_enter(VoiceModeState *vms, TUIState *tui);

/**
 * Exit voice mode (stop recording and finalize transcription).
 * Inserts the final transcription into the TUI input buffer.
 *
 * @param vms   Voice mode state
 * @param tui   TUI state
 * @return 0 on success, -1 on error
 */
int voice_mode_exit(VoiceModeState *vms, TUIState *tui);

/**
 * Process a spacebar key event for hold-to-record detection.
 * Should be called from the TUI event loop for each KEY_SPACE.
 *
 * @param vms         Voice mode state
 * @param tui         TUI state
 * @param current_ns  Current monotonic time in nanoseconds
 * @return 1 if spacebar triggered entry into voice mode,
 *         0 if ignored (already in voice mode, just refreshed timer),
 *         -1 on error
 */
int voice_mode_handle_spacebar(VoiceModeState *vms, TUIState *tui,
                               uint64_t current_ns);

/**
 * Poll voice mode for pending work (read audio, check hold timeout).
 * Should be called periodically from the TUI event loop.
 *
 * @param vms         Voice mode state
 * @param tui         TUI state
 * @param prompt      Current prompt string (for redraw)
 * @param current_ns  Current monotonic time in nanoseconds
 */
void voice_mode_poll(VoiceModeState *vms, TUIState *tui,
                     const char *prompt, uint64_t current_ns);

/**
 * Check if voice mode is currently active.
 */
int voice_mode_is_active(const VoiceModeState *vms);

/**
 * Get the current partial transcription text for display.
 */
const char* voice_mode_get_partial(const VoiceModeState *vms);

#endif /* VOICE_MODE_H */
