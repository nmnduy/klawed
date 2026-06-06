/*
 * openai_transcribe.h - OpenAI transcription backend
 *
 * Uses OpenAI's /v1/audio/transcriptions endpoint with the
 * gpt-4o-transcribe model (cost-effective, $0.006/minute).
 *
 * While the REST endpoint is not natively streaming, we simulate
 * streaming by sending accumulated audio chunks at intervals
 * and surfacing partial results in real time.
 */

#ifndef OPENAI_TRANSCRIBE_H
#define OPENAI_TRANSCRIBE_H

#include "voice_transcriber.h"

/**
 * Get the OpenAI transcription backend descriptor.
 * Call voice_transcriber_register() with the returned pointer
 * to make it available for voice mode.
 */
const VoiceTranscriberBackend* openai_transcribe_get_backend(void);

#endif /* OPENAI_TRANSCRIBE_H */
