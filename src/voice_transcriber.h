/*
 * voice_transcriber.h - Pluggable voice transcription backend interface
 *
 * Provides an abstract interface for streaming voice-to-text transcription
 * backends. Implementations can plug in different services (OpenAI,
 * Deepgram, local whisper, etc.) without changing the TUI voice mode code.
 *
 * The interface supports two modes:
 *   - push-to-talk:  record while holding spacebar, transcribe on release
 *   - streaming:     send audio chunks periodically for partial results
 */

#ifndef VOICE_TRANSCRIBER_H
#define VOICE_TRANSCRIBER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * VoiceTranscriber — opaque handle for a transcription backend session.
 * Each backend implements this as its own struct; the TUI layer only
 * sees the opaque pointer.
 */
typedef struct VoiceTranscriber VoiceTranscriber;

/**
 * Callback invoked when partial (interim) transcription text is available.
 * Called from the transcription thread; the receiver must copy the text
 * and return quickly (do not block).
 *
 * @param user_data  Opaque pointer passed to voice_transcriber_open_stream()
 * @param text       Partial transcription text (may be NULL if no result)
 */
typedef void (*VoiceTranscriberPartialFn)(void *user_data, const char *text);

/**
 * VoiceTranscriber backend descriptor.
 * Each backend registers one of these.
 */
typedef struct {
    const char *name;        // Human-readable name (e.g., "openai")
    const char *description; // Short description for status messages

    /**
     * Check whether this backend is available (API key set, network, etc.)
     * @return true if available
     */
    bool (*available)(void);

    /**
     * Initialize backend (one-time setup, called at startup).
     * @return 0 on success, -1 on failure
     */
    int (*init)(void);

    /**
     * Open a new transcription stream.
     *
     * @param partial_fn   Callback for interim results (may be NULL)
     * @param user_data    Opaque pointer forwarded to partial_fn
     * @param sample_rate  Audio sample rate (Hz), e.g. 16000
     * @param channels     Number of audio channels (1 = mono)
     * @return Opaque session handle, or NULL on failure
     */
    VoiceTranscriber* (*open_stream)(VoiceTranscriberPartialFn partial_fn,
                                     void *user_data,
                                     int sample_rate,
                                     int channels);

    /**
     * Feed a chunk of raw PCM audio to the transcriber.
     * Audio format: 16-bit signed little-endian PCM.
     *
     * For streaming backends, this may trigger a partial_fn callback
     * from within this call.
     *
     * @param vt       Session handle from open_stream()
     * @param pcm      Raw PCM audio data
     * @param n_bytes  Number of bytes in pcm
     * @return 0 on success, -1 on error
     */
    int (*feed_audio)(VoiceTranscriber *vt,
                      const int16_t *pcm, size_t n_bytes);

    /**
     * Signal end-of-stream and get the final transcription.
     * Blocks until the final result is available.
     *
     * @param vt            Session handle from open_stream()
     * @param result_out    Receives the final transcription text.
     *                      Caller must free with free().
     * @return 0 on success, -1 on error
     */
    int (*finalize)(VoiceTranscriber *vt, char **result_out);

    /**
     * Close and free the transcription stream.
     * Does not produce a final result — call finalize() first.
     *
     * @param vt  Session handle to close
     */
    void (*close_stream)(VoiceTranscriber *vt);

    /**
     * Cleanup backend resources (called at shutdown).
     */
    void (*cleanup)(void);
} VoiceTranscriberBackend;

/* ------------------------------------------------------------------ */
/* Global registry — backends auto-register at startup                 */
/* ------------------------------------------------------------------ */

/**
 * Register a transcription backend.
 * @return 0 on success, -1 if registry full
 */
int voice_transcriber_register(const VoiceTranscriberBackend *backend);

/**
 * Look up a backend by name.
 * @return pointer to backend, or NULL if not found
 */
const VoiceTranscriberBackend* voice_transcriber_lookup(const char *name);

/**
 * Get the default backend (first registered that is available).
 * Falls back to the OpenAI backend if OPENAI_API_KEY is set.
 *
 * @return pointer to backend, or NULL if none available
 */
const VoiceTranscriberBackend* voice_transcriber_get_default(void);

/**
 * Deregister all backends and free resources.
 */
void voice_transcriber_shutdown(void);

#endif /* VOICE_TRANSCRIBER_H */
