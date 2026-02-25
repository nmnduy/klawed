/*
 * voice_input.c - Voice Input Implementation with whisper.cpp
 *
 * Record microphone audio via ffmpeg and transcribe locally using whisper.cpp
 * No API keys required - fully offline transcription
 */

#define _CRT_SECURE_NO_WARNINGS
#include "voice_input.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <bsd/stdlib.h>
#include <bsd/string.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#ifdef HAVE_WHISPER
#include "whisper.h"

// ===== Configuration =====
#define SAMPLE_RATE   16000
#define NUM_CHANNELS  1

// Model search paths
static const char *MODEL_SEARCH_PATHS[] = {
    NULL,  // Will be set to WHISPER_MODEL_PATH env var if present
    "./whisper_models/ggml-small.en.bin",
    "./whisper_models/ggml-base.en.bin",
    "./whisper_models/ggml-tiny.en.bin",
    "/usr/local/share/whisper/ggml-small.en.bin",
    "/opt/whisper/ggml-small.en.bin",
};

// Global whisper context (loaded once, reused)
static struct whisper_context *g_whisper_ctx = NULL;
static pthread_mutex_t g_whisper_mutex = PTHREAD_MUTEX_INITIALIZER;

// ===== Helper Functions =====

// Find whisper model file
static const char* find_whisper_model(void) {
    // Check environment variable first
    const char *env_model = getenv("WHISPER_MODEL_PATH");
    if (env_model && *env_model) {
        MODEL_SEARCH_PATHS[0] = env_model;
    }

    for (size_t i = 0; i < sizeof(MODEL_SEARCH_PATHS) / sizeof(MODEL_SEARCH_PATHS[0]); i++) {
        if (MODEL_SEARCH_PATHS[i] && access(MODEL_SEARCH_PATHS[i], R_OK) == 0) {
            LOG_INFO("Found whisper model: %s", MODEL_SEARCH_PATHS[i]);
            return MODEL_SEARCH_PATHS[i];
        }
    }

    LOG_ERROR("Whisper model not found. Searched:");
    for (size_t i = 0; i < sizeof(MODEL_SEARCH_PATHS) / sizeof(MODEL_SEARCH_PATHS[0]); i++) {
        if (MODEL_SEARCH_PATHS[i]) {
            LOG_ERROR("  - %s", MODEL_SEARCH_PATHS[i]);
        }
    }
    return NULL;
}

// Load whisper model (thread-safe, singleton pattern)
static struct whisper_context* get_whisper_context(void) {
    pthread_mutex_lock(&g_whisper_mutex);

    if (!g_whisper_ctx) {
        const char *model_path = find_whisper_model();
        if (!model_path) {
            pthread_mutex_unlock(&g_whisper_mutex);
            return NULL;
        }

        LOG_INFO("Loading whisper model: %s", model_path);
        struct whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = true;  // Enable Metal on macOS if available

        g_whisper_ctx = whisper_init_from_file_with_params(model_path, cparams);
        if (!g_whisper_ctx) {
            LOG_ERROR("Failed to initialize whisper context");
            pthread_mutex_unlock(&g_whisper_mutex);
            return NULL;
        }

        LOG_INFO("Whisper model loaded successfully");
    }

    pthread_mutex_unlock(&g_whisper_mutex);
    return g_whisper_ctx;
}

// Record audio using ffmpeg
static int record_audio_ffmpeg(const char *output_path, volatile sig_atomic_t *stop_flag) {
    const char *device = getenv("VOICE_DEVICE");

#ifdef __APPLE__
    // macOS: use avfoundation
    const char *default_device = ":0";  // Default microphone
    const char *input_format = "avfoundation";
#elif __linux__
    // Linux: use alsa or pulse
    const char *default_device = "default";
    const char *input_format = "pulse";  // Try pulse first, fall back to alsa
#else
    LOG_ERROR("Unsupported platform for audio recording");
    return -1;
#endif

    if (!device || !*device) {
        device = default_device;
    }

    // Build ffmpeg command
    // -f input_format: audio input format
    // -i device: input device
    // -ar SAMPLE_RATE: audio sample rate
    // -ac NUM_CHANNELS: audio channels
    // -f s16le: output format (16-bit PCM little-endian)
    // -y: overwrite output file
    char cmd[512];
    int ret = snprintf(cmd, sizeof(cmd),
                      "ffmpeg -loglevel quiet -f %s -i %s -ar %d -ac %d -f s16le -y %s 2>/dev/null",
                      input_format, device, SAMPLE_RATE, NUM_CHANNELS, output_path);
    if (ret < 0 || (size_t)ret >= sizeof(cmd)) {
        LOG_ERROR("Failed to build ffmpeg command");
        return -1;
    }

    LOG_DEBUG("Starting audio recording: %s", cmd);

    // Fork and exec ffmpeg
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("Fork failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        // Child process: exec ffmpeg
        execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
        _exit(127);  // exec failed
    }

    // Parent process: wait for user to press Enter
    fprintf(stderr, "\nRecording... press ENTER to stop.\n");
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}

    // Stop recording by killing ffmpeg
    if (stop_flag) {
        *stop_flag = 1;
    }
    kill(pid, SIGTERM);

    // Wait for ffmpeg to exit
    int status;
    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOG_WARN("ffmpeg exited with status: %d", WEXITSTATUS(status));
    }

    LOG_DEBUG("Audio recording stopped");
    return 0;
}

// Convert PCM S16LE to float samples
static float* pcm_to_float(const int16_t *pcm, size_t n_samples) {
    float *samples = (float*)malloc(n_samples * sizeof(float));
    if (!samples) {
        LOG_ERROR("Failed to allocate float samples buffer");
        return NULL;
    }

    // Convert 16-bit PCM to normalized float [-1.0, 1.0]
    for (size_t i = 0; i < n_samples; i++) {
        samples[i] = (float)pcm[i] / 32768.0f;
    }

    return samples;
}

// Read PCM audio file
static int16_t* read_pcm_file(const char *path, size_t *n_samples_out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("Failed to open audio file: %s", path);
        return NULL;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        LOG_ERROR("Empty audio file");
        fclose(f);
        return NULL;
    }

    // Allocate buffer
    size_t n_samples = (size_t)file_size / sizeof(int16_t);
    int16_t *pcm = (int16_t*)malloc((size_t)file_size);
    if (!pcm) {
        LOG_ERROR("Failed to allocate PCM buffer");
        fclose(f);
        return NULL;
    }

    // Read PCM data
    size_t read = fread(pcm, 1, (size_t)file_size, f);
    fclose(f);

    if (read != (size_t)file_size) {
        LOG_ERROR("Failed to read complete audio file");
        free(pcm);
        return NULL;
    }

    *n_samples_out = n_samples;
    return pcm;
}

// Transcribe audio using whisper.cpp
static char* transcribe_audio(const float *samples, size_t n_samples) {
    struct whisper_context *ctx = get_whisper_context();
    if (!ctx) {
        LOG_ERROR("Whisper context not available");
        return NULL;
    }

    // Set up transcription parameters
    struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.translate = false;
    wparams.language = "en";  // English only for now
    wparams.n_threads = 4;    // Use 4 threads
    wparams.single_segment = false;
    wparams.no_context = true;

    LOG_DEBUG("Starting whisper transcription (%zu samples, %.2f seconds)",
              n_samples, (float)n_samples / SAMPLE_RATE);

    // Run transcription
    int ret = whisper_full(ctx, wparams, samples, (int)n_samples);
    if (ret != 0) {
        LOG_ERROR("Whisper transcription failed: %d", ret);
        return NULL;
    }

    // Collect transcription segments
    int n_segments = whisper_full_n_segments(ctx);
    if (n_segments == 0) {
        LOG_WARN("No transcription segments generated");
        return NULL;
    }

    LOG_DEBUG("Transcription generated %d segments", n_segments);

    // Concatenate all segments
    size_t total_len = 0;
    for (int i = 0; i < n_segments; i++) {
        const char *text = whisper_full_get_segment_text(ctx, i);
        if (text) {
            total_len += strlen(text);
        }
    }

    // Allocate result buffer
    char *result = (char*)calloc(total_len + 1, 1);
    if (!result) {
        LOG_ERROR("Failed to allocate transcription result buffer");
        return NULL;
    }

    // Concatenate segments
    size_t offset = 0;
    for (int i = 0; i < n_segments; i++) {
        const char *text = whisper_full_get_segment_text(ctx, i);
        if (text) {
            size_t len = strlen(text);
            if (offset + len < total_len + 1) {
                memcpy(result + offset, text, len);
                offset += len;
            }
        }
    }
    result[offset] = '\0';

    // Trim leading/trailing whitespace
    char *start = result;
    while (*start == ' ' || *start == '\t' || *start == '\n') {
        start++;
    }

    size_t len = strlen(start);
    if (len > 0) {
        char *end = start + len - 1;
        while (end > start && (*end == ' ' || *end == '\t' || *end == '\n')) {
            *end = '\0';
            end--;
        }
    }

    // If result is empty after trimming, return NULL
    if (*start == '\0') {
        free(result);
        return NULL;
    }

    // Copy trimmed result
    char *trimmed = strdup(start);
    free(result);

    LOG_INFO("Transcription result: \"%s\"", trimmed);
    return trimmed;
}

// ============================================================================
// Public API (whisper.cpp-enabled)
// ============================================================================

int voice_input_init(void) {
    // Check for ffmpeg
    if (system("command -v ffmpeg >/dev/null 2>&1") != 0) {
        LOG_WARN("ffmpeg not found - voice input disabled");
        return -1;
    }

    // Check for whisper model
    if (!find_whisper_model()) {
        LOG_WARN("Whisper model not found - voice input disabled");
        return -1;
    }

    LOG_INFO("Voice input initialized successfully");
    return 0;
}

bool voice_input_available(void) {
    // Check for ffmpeg
    if (system("command -v ffmpeg >/dev/null 2>&1") != 0) {
        LOG_DEBUG("Voice input unavailable: ffmpeg not found");
        return false;
    }

    // Check for whisper model
    if (!find_whisper_model()) {
        LOG_DEBUG("Voice input unavailable: whisper model not found");
        return false;
    }

    return true;
}

int voice_input_record_and_transcribe(char **transcription_out) {
    if (!transcription_out) {
        LOG_ERROR("Invalid transcription output pointer");
        return -1;
    }
    *transcription_out = NULL;

    // Temporary file for audio recording
    const char *audio_path = ".voice_recording.pcm";

    // Record audio
    volatile sig_atomic_t stop_flag = 0;
    if (record_audio_ffmpeg(audio_path, &stop_flag) != 0) {
        LOG_ERROR("Audio recording failed");
        return -1;
    }

    // Check if file exists and has content
    if (access(audio_path, F_OK) != 0) {
        LOG_ERROR("Audio file not created");
        return -1;
    }

    // Read PCM audio
    size_t n_samples = 0;
    int16_t *pcm = read_pcm_file(audio_path, &n_samples);
    if (!pcm) {
        LOG_ERROR("Failed to read audio file");
        unlink(audio_path);
        return -1;
    }

    // Check if recording is too short (< 0.5 seconds)
    if (n_samples < SAMPLE_RATE / 2) {
        LOG_WARN("Recording too short: %.2f seconds", (float)n_samples / SAMPLE_RATE);
        free(pcm);
        unlink(audio_path);
        return -2;
    }

    // Check if audio is silent
    int32_t max_abs = 0;
    for (size_t i = 0; i < n_samples; i++) {
        int32_t abs_val = pcm[i] < 0 ? -(int32_t)pcm[i] : (int32_t)pcm[i];
        if (abs_val > max_abs) {
            max_abs = abs_val;
        }
    }

    if (max_abs < 100) {  // Very quiet threshold
        LOG_WARN("Recording appears silent (max amplitude: %d)", max_abs);
        free(pcm);
        unlink(audio_path);
        return -3;
    }

    LOG_DEBUG("Recording stats: %zu samples, %.2f seconds, max amplitude: %d",
              n_samples, (float)n_samples / SAMPLE_RATE, max_abs);

    // Convert to float samples
    float *samples = pcm_to_float(pcm, n_samples);
    free(pcm);
    if (!samples) {
        LOG_ERROR("Failed to convert PCM to float");
        unlink(audio_path);
        return -1;
    }

    // Transcribe
    char *transcription = transcribe_audio(samples, n_samples);
    free(samples);
    unlink(audio_path);

    if (!transcription) {
        LOG_ERROR("Transcription failed or produced no text");
        return -1;
    }

    *transcription_out = transcription;
    LOG_INFO("Transcription successful: %zu chars", strlen(transcription));
    return 0;
}

void voice_input_cleanup(void) {
    pthread_mutex_lock(&g_whisper_mutex);
    if (g_whisper_ctx) {
        whisper_free(g_whisper_ctx);
        g_whisper_ctx = NULL;
        LOG_INFO("Whisper context cleaned up");
    }
    pthread_mutex_unlock(&g_whisper_mutex);
}

void voice_input_print_status(void) {
    // Check for ffmpeg
    bool has_ffmpeg = (system("command -v ffmpeg >/dev/null 2>&1") == 0);

    // Check for whisper model
    const char *model_path = find_whisper_model();
    bool has_model = (model_path != NULL);

    if (!has_ffmpeg) {
        LOG_WARN("Voice input: ffmpeg not found");
        fprintf(stderr, "⚠ Voice input unavailable: ffmpeg not installed\n");
        fprintf(stderr, "  Install with: brew install ffmpeg (macOS)\n");
        fprintf(stderr, "            or: sudo apt install ffmpeg (Ubuntu)\n");
        return;
    }

    if (!has_model) {
        LOG_WARN("Voice input: Whisper model not found");
        fprintf(stderr, "⚠ Voice input unavailable: Whisper model not found\n");
        fprintf(stderr, "  Download model with: make download-model\n");
        fprintf(stderr, "  Or set: export WHISPER_MODEL_PATH=/path/to/model.bin\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "  Searched paths:\n");
        for (size_t i = 0; i < sizeof(MODEL_SEARCH_PATHS) / sizeof(MODEL_SEARCH_PATHS[0]); i++) {
            if (MODEL_SEARCH_PATHS[i]) {
                fprintf(stderr, "    - %s\n", MODEL_SEARCH_PATHS[i]);
            }
        }
        return;
    }

    // Everything available
    LOG_INFO("Voice input available - use /voice command");
    fprintf(stderr, "✓ Voice input ready (offline transcription via whisper.cpp)\n");
    fprintf(stderr, "  Model: %s\n", model_path);
}

#else  // !HAVE_WHISPER

// ============================================================================
// Public API (stubs when whisper.cpp is not available)
// ============================================================================

int voice_input_init(void) {
    LOG_WARN("Voice input disabled: whisper.cpp not available at build time");
    return -1;
}

bool voice_input_available(void) {
    return false;
}

int voice_input_record_and_transcribe(char **transcription_out) {
    (void)transcription_out;
    LOG_ERROR("Voice input not built: missing whisper.cpp dependency");
    return -1;
}

void voice_input_cleanup(void) {
    // no-op
}

void voice_input_print_status(void) {
    fprintf(stderr, "⚠ Voice input unavailable: whisper.cpp not built\n");
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

#endif  // HAVE_WHISPER
