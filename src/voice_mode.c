/*
 * voice_mode.c - TUI Voice Mode handler
 *
 * Implements push-to-talk voice recording with real-time transcription.
 * Activated by holding spacebar in INSERT mode.
 *
 * Hold detection: holding spacebar generates repeated KEY_SPACE events
 * via terminal key-repeat. A 300ms silence after the last event signals
 * release, which stops recording and finalizes transcription.
 */

#define _POSIX_C_SOURCE 200809L

#include "voice_mode.h"
#include "voice_transcriber.h"
#include "tui.h"
#include "tui_input.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>

#ifdef __APPLE__
#include <spawn.h>
extern char **environ;
#endif

/* ------------------------------------------------------------------ */
/* Defaults                                                             */
/* ------------------------------------------------------------------ */

#define DEFAULT_SAMPLE_RATE     16000
#define DEFAULT_CHANNELS        1
#define HOLD_TIMEOUT_MS         300     /* Silence after last spacebar to trigger release */
#define AUDIO_BUF_CHUNK_SAMPLES 4096    /* Samples to read from pipe at once */
#define AUDIO_BUF_MAX_SAMPLES   480000  /* ~30 seconds at 16kHz */

/* ------------------------------------------------------------------ */
/* Partial transcription callback (invoked from audio feed thread)      */
/* ------------------------------------------------------------------ */

static void partial_callback(void *user_data, const char *text) {
    VoiceModeState *vms = (VoiceModeState *)user_data;
    if (!vms || !text) return;

    /* Copy into partial_text (thread-safe enough for our use case —
     * the TUI thread reads this atomically via the dirty flag) */
    size_t len = strlen(text);
    if (len >= VOICE_MODE_MAX_PARTIAL) len = VOICE_MODE_MAX_PARTIAL - 1;
    memcpy(vms->partial_text, text, len);
    vms->partial_text[len] = '\0';
    vms->partial_dirty = 1;
}

/* ------------------------------------------------------------------ */
/* Remote session detection                                              */
/* ------------------------------------------------------------------ */

/*
 * Check if we're running in a remote session (SSH, etc.) where the
 * local microphone is not accessible. Voice mode uses ffmpeg to capture
 * from the machine's audio devices, which won't work remotely.
 *
 * Returns 1 if remote, 0 if local.
 */
static int is_remote_session(void) {
    /* Standard SSH environment variables set by sshd */
    if (getenv("SSH_TTY"))       return 1;
    if (getenv("SSH_CLIENT"))    return 1;
    if (getenv("SSH_CONNECTION")) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/* Get monotonic time in nanoseconds                                     */
/* ------------------------------------------------------------------ */

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ------------------------------------------------------------------ */
/* Start ffmpeg recording process                                        */
/* ------------------------------------------------------------------ */

static int start_ffmpeg_recording(VoiceModeState *vms) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        LOG_ERROR("[voice_mode] Failed to create pipe for ffmpeg");
        return -1;
    }

    /* Build ffmpeg command */
    const char *device = getenv("VOICE_DEVICE");
#ifdef __APPLE__
    const char *default_device = ":0";
    const char *input_format = "avfoundation";
#else
    const char *default_device = "default";
    const char *input_format = "pulse";
#endif
    if (!device || !*device) device = default_device;

    char cmd[512];
    int ret = snprintf(cmd, sizeof(cmd),
                       "ffmpeg -loglevel quiet -f %s -i %s "
                       "-ar %d -ac %d -f s16le -y pipe:1 2>/dev/null",
                       input_format, device,
                       DEFAULT_SAMPLE_RATE, DEFAULT_CHANNELS);
    if (ret < 0 || (size_t)ret >= sizeof(cmd)) {
        LOG_ERROR("[voice_mode] Failed to build ffmpeg command");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    LOG_DEBUG("[voice_mode] Starting ffmpeg: %s", cmd);

#ifdef __APPLE__
    /* Use posix_spawn for thread safety on macOS */
    posix_spawn_file_actions_t file_actions;
    int rc = posix_spawn_file_actions_init(&file_actions);
    if (rc != 0) {
        LOG_ERROR("[voice_mode] Failed to init file actions: %s", strerror(rc));
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    /* Redirect stdin from /dev/null */
    posix_spawn_file_actions_addopen(&file_actions, STDIN_FILENO,
                                     "/dev/null", O_RDONLY, 0);
    /* Redirect stdout to pipe write end */
    posix_spawn_file_actions_adddup2(&file_actions, pipefd[1], STDOUT_FILENO);
    /* Close pipe read end in child (not needed) */
    posix_spawn_file_actions_addclose(&file_actions, pipefd[0]);

    char shell_name[] = "sh";
    char dash_c[] = "-c";
    char *argv[] = {shell_name, dash_c, cmd, NULL};

    rc = posix_spawn(&vms->ffmpeg_pid, "/bin/sh", &file_actions, NULL,
                     argv, environ);
    posix_spawn_file_actions_destroy(&file_actions);

    if (rc != 0) {
        LOG_ERROR("[voice_mode] Failed to spawn ffmpeg: %s", strerror(rc));
        close(pipefd[0]);
        close(pipefd[1]);
        vms->ffmpeg_pid = 0;
        return -1;
    }
#else
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("[voice_mode] Fork failed: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child: redirect stdout to pipe, close read end */
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    vms->ffmpeg_pid = pid;
#endif

    /* Close write end in parent, keep read end */
    close(pipefd[1]);
    vms->ffmpeg_pipe_fd = pipefd[0];

    /* Set pipe to non-blocking */
    int flags = fcntl(vms->ffmpeg_pipe_fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(vms->ffmpeg_pipe_fd, F_SETFL, flags | O_NONBLOCK);
    }

    LOG_INFO("[voice_mode] ffmpeg recording started (pid=%d, fd=%d)",
             vms->ffmpeg_pid, vms->ffmpeg_pipe_fd);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Stop ffmpeg recording process                                         */
/* ------------------------------------------------------------------ */

static void stop_ffmpeg_recording(VoiceModeState *vms) {
    if (vms->ffmpeg_pid <= 0) return;

    LOG_DEBUG("[voice_mode] Stopping ffmpeg (pid=%d)", vms->ffmpeg_pid);

    /* Send SIGTERM to ffmpeg */
    kill(vms->ffmpeg_pid, SIGTERM);

    /* Wait briefly for process to exit */
    int status;
    int waited = 0;
    for (int i = 0; i < 10; i++) {
        pid_t result = waitpid(vms->ffmpeg_pid, &status, WNOHANG);
        if (result == vms->ffmpeg_pid) {
            waited = 1;
            break;
        }
        usleep(50000);  /* 50ms */
    }

    /* Force kill if still running */
    if (!waited) {
        LOG_WARN("[voice_mode] ffmpeg didn't exit, sending SIGKILL");
        kill(vms->ffmpeg_pid, SIGKILL);
        waitpid(vms->ffmpeg_pid, &status, 0);
    }

    /* Drain remaining audio from pipe */
    if (vms->ffmpeg_pipe_fd >= 0) {
        int16_t drain_buf[4096];
        ssize_t n;
        while ((n = read(vms->ffmpeg_pipe_fd, drain_buf, sizeof(drain_buf))) > 0) {
            /* Append drained audio to buffer */
            size_t n_samples = (size_t)n / sizeof(int16_t);
            if (vms->audio_buf_len + n_samples <= vms->audio_buf_cap) {
                memcpy(vms->audio_buf + vms->audio_buf_len, drain_buf, (size_t)n);
                vms->audio_buf_len += n_samples;
            }
        }
        close(vms->ffmpeg_pipe_fd);
        vms->ffmpeg_pipe_fd = -1;
    }

    vms->ffmpeg_pid = 0;
    vms->recording = 0;
}

/* ------------------------------------------------------------------ */
/* Read audio from ffmpeg pipe (non-blocking)                            */
/* ------------------------------------------------------------------ */

static void read_audio_pipe(VoiceModeState *vms) {
    if (vms->ffmpeg_pipe_fd < 0 || !vms->recording) return;

    int16_t chunk[AUDIO_BUF_CHUNK_SAMPLES];
    ssize_t n;

    while ((n = read(vms->ffmpeg_pipe_fd, chunk, sizeof(chunk))) > 0) {
        size_t n_samples = (size_t)n / sizeof(int16_t);

        /* Grow buffer if needed */
        if (vms->audio_buf_len + n_samples > vms->audio_buf_cap) {
            if (vms->audio_buf_cap >= AUDIO_BUF_MAX_SAMPLES) {
                LOG_WARN("[voice_mode] Audio buffer full, dropping samples");
                break;
            }
            size_t new_cap = vms->audio_buf_cap * 2;
            if (new_cap > AUDIO_BUF_MAX_SAMPLES) new_cap = AUDIO_BUF_MAX_SAMPLES;
            int16_t *new_buf = (int16_t *)realloc(vms->audio_buf,
                                                  new_cap * sizeof(int16_t));
            if (!new_buf) {
                LOG_ERROR("[voice_mode] Failed to grow audio buffer");
                break;
            }
            vms->audio_buf = new_buf;
            vms->audio_buf_cap = new_cap;
        }

        memcpy(vms->audio_buf + vms->audio_buf_len, chunk, (size_t)n);
        vms->audio_buf_len += n_samples;

        /* Feed to transcription backend if enough accumulated */
        /* Feed every ~0.5 seconds of audio (8000 samples at 16kHz) */
        /* The backend handles its own chunking for partial sends */
    }

    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("[voice_mode] Error reading ffmpeg pipe: %s", strerror(errno));
    }
}

/* ------------------------------------------------------------------ */
/* Feed accumulated audio to transcriber                                 */
/* ------------------------------------------------------------------ */

static void feed_transcriber(VoiceModeState *vms) {
    if (!vms->transcriber || vms->audio_buf_len == 0) return;

    /* Feed in chunks to keep latency low.
     * last_fed_samples is per-session, reset in voice_mode_enter(). */
    if (vms->last_fed_samples >= vms->audio_buf_len) {
        /* No new data */
        return;
    }

    size_t n_bytes = (vms->audio_buf_len - vms->last_fed_samples) * sizeof(int16_t);

    const VoiceTranscriberBackend *backend = voice_transcriber_get_default();
    if (backend && backend->feed_audio) {
        backend->feed_audio(vms->transcriber,
                            vms->audio_buf + vms->last_fed_samples,
                            n_bytes);
    }
    vms->last_fed_samples = vms->audio_buf_len;
}

/* ------------------------------------------------------------------ */
/* Public API                                                            */
/* ------------------------------------------------------------------ */

void voice_mode_init(VoiceModeState *vms, TUIState *tui) {
    if (!vms) return;
    memset(vms, 0, sizeof(*vms));
    vms->hold_timeout_ms = HOLD_TIMEOUT_MS;
    vms->tui = tui;
    vms->ffmpeg_pipe_fd = -1;

    /* Pre-allocate audio buffer */
    vms->audio_buf_cap = AUDIO_BUF_CHUNK_SAMPLES * 4;
    vms->audio_buf = (int16_t *)calloc(vms->audio_buf_cap, sizeof(int16_t));
    if (!vms->audio_buf) {
        LOG_ERROR("[voice_mode] Failed to allocate audio buffer");
        vms->audio_buf_cap = 0;
    }
}

void voice_mode_cleanup(VoiceModeState *vms) {
    if (!vms) return;

    /* Stop recording if active */
    if (vms->active) {
        vms->active = 0;
        vms->recording = 0;
        stop_ffmpeg_recording(vms);
    }

    /* Close transcriber */
    if (vms->transcriber) {
        const VoiceTranscriberBackend *backend = voice_transcriber_get_default();
        if (backend && backend->close_stream) {
            backend->close_stream(vms->transcriber);
        }
        vms->transcriber = NULL;
    }

    free(vms->audio_buf);
    vms->audio_buf = NULL;
    vms->audio_buf_cap = 0;

    free(vms->saved_buffer);
    vms->saved_buffer = NULL;
}

int voice_mode_enter(VoiceModeState *vms, TUIState *tui) {
    if (!vms || !tui) return -1;

    /* Check for available backend */
    const VoiceTranscriberBackend *backend = voice_transcriber_get_default();
    if (!backend) {
        LOG_ERROR("[voice_mode] No transcription backend available");
        tui_update_status(tui, "Voice mode unavailable: no transcription backend (set OPENAI_API_KEY)");
        return -1;
    }

    /* Save current input buffer state */
    TUIInputBuffer *input = tui->input_buffer;
    if (input && input->buffer) {
        free(vms->saved_buffer);
        vms->saved_buffer = strdup(input->buffer);
        vms->saved_length = input->length;
        vms->saved_cursor = input->cursor;
    }

    /* Clear input buffer for partial display */
    if (input) {
        input->buffer[0] = '\0';
        input->length = 0;
        input->cursor = 0;
    }

    /* Check for remote session — voice mode needs local microphone */
    if (is_remote_session()) {
        LOG_WARN("[voice_mode] Remote session detected, voice mode cannot access local microphone");
        tui_update_status(tui,
            "Voice mode unavailable: remote session. Your microphone is on "
            "your local machine, not the remote server. Run klawed locally "
            "for voice input.");
        /* Restore saved input */
        if (input && vms->saved_buffer) {
            strlcpy(input->buffer, vms->saved_buffer, input->capacity);
            input->length = vms->saved_length;
            input->cursor = vms->saved_cursor;
        }
        return -1;
    }

    /* Initialize transcription session */
    vms->transcriber = backend->open_stream(partial_callback, vms,
                                            DEFAULT_SAMPLE_RATE,
                                            DEFAULT_CHANNELS);
    if (!vms->transcriber) {
        LOG_ERROR("[voice_mode] Failed to open transcription stream");
        /* Restore saved input */
        if (input && vms->saved_buffer) {
            strlcpy(input->buffer, vms->saved_buffer, input->capacity);
            input->length = vms->saved_length;
            input->cursor = vms->saved_cursor;
        }
        return -1;
    }

    /* Reset audio buffer and feed tracking */
    vms->audio_buf_len = 0;
    vms->last_fed_samples = 0;
    vms->partial_text[0] = '\0';
    vms->partial_dirty = 0;

    /* Start recording */
    if (start_ffmpeg_recording(vms) != 0) {
        LOG_ERROR("[voice_mode] Failed to start ffmpeg recording");
        tui_update_status(tui,
            "Voice mode unavailable: could not access microphone. "
            "Check that a microphone is connected and ffmpeg is installed.");
        backend->close_stream(vms->transcriber);
        vms->transcriber = NULL;
        /* Restore saved input */
        if (input && vms->saved_buffer) {
            strlcpy(input->buffer, vms->saved_buffer, input->capacity);
            input->length = vms->saved_length;
            input->cursor = vms->saved_cursor;
        }
        return -1;
    }

    vms->active = 1;
    vms->recording = 1;
    vms->last_spacebar_ns = monotonic_ns();

    tui_update_status(tui, "🎤 Recording... (release spacebar to stop)");
    LOG_INFO("[voice_mode] Voice mode entered");

    return 0;
}

int voice_mode_exit(VoiceModeState *vms, TUIState *tui) {
    if (!vms || !vms->active) return 0;

    LOG_INFO("[voice_mode] Voice mode exiting (%.2fs of audio)",
             (double)vms->audio_buf_len / (double)DEFAULT_SAMPLE_RATE);

    /* Stop recording */
    stop_ffmpeg_recording(vms);

    /* Finalize transcription */
    const VoiceTranscriberBackend *backend = voice_transcriber_get_default();
    char *final_text = NULL;

    if (vms->transcriber && backend && backend->finalize) {
        backend->finalize(vms->transcriber, &final_text);
    }

    /* Close stream */
    if (vms->transcriber && backend && backend->close_stream) {
        backend->close_stream(vms->transcriber);
    }
    vms->transcriber = NULL;

    /* Insert transcription into input buffer */
    TUIInputBuffer *input = tui->input_buffer;
    if (input) {
        if (final_text && *final_text) {
            /* Restore saved buffer then append transcription */
            if (vms->saved_buffer && vms->saved_buffer[0]) {
                /* Insert saved text first, then a space, then transcription */
                strlcpy(input->buffer, vms->saved_buffer, input->capacity);
                size_t saved_len = strlen(input->buffer);

                /* Add space if needed */
                if (saved_len > 0 && saved_len + 1 < input->capacity) {
                    /* Check if saved ends with space or newline */
                    char last = input->buffer[saved_len - 1];
                    if (last != ' ' && last != '\n' && last != '\t') {
                        input->buffer[saved_len] = ' ';
                        saved_len++;
                        input->buffer[saved_len] = '\0';
                    }
                }

                /* Append transcription */
                strlcat(input->buffer, final_text, input->capacity);
            } else {
                strlcpy(input->buffer, final_text, input->capacity);
            }
            input->length = (int)strlen(input->buffer);
            input->cursor = input->length;
        } else {
            /* No transcription — restore saved buffer */
            if (vms->saved_buffer) {
                strlcpy(input->buffer, vms->saved_buffer, input->capacity);
                input->length = vms->saved_length;
                input->cursor = vms->saved_cursor;
            }
        }
    }

    free(final_text);
    free(vms->saved_buffer);
    vms->saved_buffer = NULL;
    vms->active = 0;
    vms->recording = 0;

    tui_update_status(tui, final_text ? "Transcription complete" : "No speech detected");

    return 0;
}

int voice_mode_handle_spacebar(VoiceModeState *vms, TUIState *tui,
                               uint64_t current_ns) {
    if (!vms || !tui) return -1;

    if (!vms->active) {
        /* Enter voice mode */
        if (voice_mode_enter(vms, tui) != 0) {
            return -1;
        }
        return 1;
    }

    /* Already in voice mode — reset hold timer */
    vms->last_spacebar_ns = current_ns;
    return 0;
}

void voice_mode_poll(VoiceModeState *vms, TUIState *tui,
                     const char *prompt, uint64_t current_ns) {
    if (!vms || !vms->active) return;

    /* Read audio from ffmpeg pipe */
    read_audio_pipe(vms);

    /* Feed audio to transcriber */
    feed_transcriber(vms);

    /* Check partial transcription */
    if (vms->partial_dirty && tui->input_buffer) {
        TUIInputBuffer *input = tui->input_buffer;
        /* Show partial in input area */
        size_t len = strlen(vms->partial_text);
        if (len >= input->capacity) len = input->capacity - 1;
        memcpy(input->buffer, vms->partial_text, len);
        input->buffer[len] = '\0';
        input->length = (int)len;
        input->cursor = (int)len;

        input_redraw(tui, prompt);
        vms->partial_dirty = 0;
    }

    /* Check hold timeout: if no spacebar event for hold_timeout_ms, release */
    if (vms->recording && vms->last_spacebar_ns > 0) {
        uint64_t elapsed_ns = current_ns - vms->last_spacebar_ns;
        uint64_t timeout_ns = (uint64_t)((unsigned int)vms->hold_timeout_ms) * 1000000ULL;

        if (elapsed_ns >= timeout_ns) {
            LOG_DEBUG("[voice_mode] Hold timeout reached, stopping recording");
            voice_mode_exit(vms, tui);
        }
    }
}

int voice_mode_is_active(const VoiceModeState *vms) {
    return (vms && vms->active) ? 1 : 0;
}

const char* voice_mode_get_partial(const VoiceModeState *vms) {
    if (!vms || !vms->active || vms->partial_text[0] == '\0') return NULL;
    return vms->partial_text;
}
