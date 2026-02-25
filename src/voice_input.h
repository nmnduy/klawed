/*
 * voice_input.h - Voice Input Module
 *
 * Provides voice-to-text transcription using ffmpeg + whisper.cpp
 * Fully offline transcription with no API keys required
 */

#ifndef VOICE_INPUT_H
#define VOICE_INPUT_H

#include <stdbool.h>

/**
 * Initialize voice input system
 * Checks for ffmpeg and whisper model availability
 *
 * @return 0 on success, -1 on failure
 */
int voice_input_init(void);

/**
 * Record audio from microphone and transcribe to text
 * Blocks until user presses Enter to stop recording
 *
 * @param transcription_out  Pointer to char* that will receive transcription (caller must free)
 * @return 0 on success, -1 on failure, -2 if no audio recorded, -3 if silent
 */
int voice_input_record_and_transcribe(char **transcription_out);

/**
 * Cleanup voice input resources
 */
void voice_input_cleanup(void);

/**
 * Check if voice input is available
 * Tests for ffmpeg command and whisper model file
 *
 * @return true if available, false otherwise
 */
bool voice_input_available(void);

/**
 * Print a warning about voice input availability status
 * Called at startup to inform users about missing prerequisites
 */
void voice_input_print_status(void);

#endif // VOICE_INPUT_H
