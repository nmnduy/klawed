/*
 * voice_stub.c - Voice Input Stub Implementation
 *
 * Used when VOICE=0 or voice dependencies are not available
 */

#include "voice_input.h"
#include "logger.h"
#include <stdio.h>

int voice_input_init(void) {
    LOG_DEBUG("Voice input disabled at build time");
    return -1;
}

bool voice_input_available(void) {
    return false;
}

int voice_input_record_and_transcribe(char **transcription_out) {
    (void)transcription_out;
    LOG_ERROR("Voice input not available - rebuild with VOICE=1");
    return -1;
}

void voice_input_cleanup(void) {
    // no-op
}

void voice_input_print_status(void) {
    fprintf(stderr, "⚠ Voice input unavailable: Not built with voice support\n");
    fprintf(stderr, "  Setup and build with:\n");
    fprintf(stderr, "    git submodule update --init --recursive\n");
    fprintf(stderr, "    make setup-voice\n");
    fprintf(stderr, "    make clean && make VOICE=1\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  Requirements:\n");
    fprintf(stderr, "    - ffmpeg (install with: brew install ffmpeg or apt install ffmpeg)\n");
    fprintf(stderr, "    - whisper.cpp submodule (auto-built)\n");
    fprintf(stderr, "    - whisper model file (auto-downloaded with setup-voice)\n");
}
