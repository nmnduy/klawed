/*
 * openai_transcribe.c - OpenAI transcription backend implementation
 *
 * Uses OpenAI's /v1/audio/transcriptions endpoint with gpt-4o-transcribe.
 * Simulates streaming by sending accumulated audio chunks at ~1.5s intervals
 * and surfacing partial results via the VoiceTranscriberPartialFn callback.
 *
 * Architecture: A background worker thread handles all HTTP requests to avoid
 * blocking the TUI event loop. The main thread feeds audio into a shared
 * buffer and signals the worker. The worker sends chunks periodically and
 * invokes the partial callback from its own thread.
 */

#define _POSIX_C_SOURCE 200809L

#include "openai_transcribe.h"
#include "voice_transcriber.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <pthread.h>
#include <curl/curl.h>
#include <unistd.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* WAV header writer                                                   */
/* ------------------------------------------------------------------ */

/*
 * Write a minimal 44-byte WAV header for 16-bit mono PCM.
 * Returns 44 on success, -1 on error.
 */
static int write_wav_header(uint8_t *buf, size_t buf_size,
                            int sample_rate, uint32_t data_size) {
    if (buf_size < 44) return -1;

    /* RIFF header */
    buf[0] = 'R'; buf[1] = 'I'; buf[2] = 'F'; buf[3] = 'F';
    uint32_t file_size = 36 + data_size;
    buf[4] = (uint8_t)(file_size & 0xFF);
    buf[5] = (uint8_t)((file_size >> 8) & 0xFF);
    buf[6] = (uint8_t)((file_size >> 16) & 0xFF);
    buf[7] = (uint8_t)((file_size >> 24) & 0xFF);
    buf[8] = 'W'; buf[9] = 'A'; buf[10] = 'V'; buf[11] = 'E';

    /* fmt chunk */
    buf[12] = 'f'; buf[13] = 'm'; buf[14] = 't'; buf[15] = ' ';
    buf[16] = 16; buf[17] = 0; buf[18] = 0; buf[19] = 0;  /* chunk size = 16 */
    buf[20] = 1;  buf[21] = 0;   /* PCM format */
    buf[22] = 1;  buf[23] = 0;   /* channels = 1 */
    /* sample rate */
    buf[24] = (uint8_t)(sample_rate & 0xFF);
    buf[25] = (uint8_t)((sample_rate >> 8) & 0xFF);
    buf[26] = (uint8_t)((sample_rate >> 16) & 0xFF);
    buf[27] = (uint8_t)((sample_rate >> 24) & 0xFF);
    uint32_t byte_rate = (uint32_t)sample_rate * 2;  /* 1 channel * 16-bit */
    buf[28] = (uint8_t)(byte_rate & 0xFF);
    buf[29] = (uint8_t)((byte_rate >> 8) & 0xFF);
    buf[30] = (uint8_t)((byte_rate >> 16) & 0xFF);
    buf[31] = (uint8_t)((byte_rate >> 24) & 0xFF);
    buf[32] = 2;  buf[33] = 0;   /* block align = 2 */
    buf[34] = 16; buf[35] = 0;   /* bits per sample = 16 */

    /* data chunk */
    buf[36] = 'd'; buf[37] = 'a'; buf[38] = 't'; buf[39] = 'a';
    buf[40] = (uint8_t)(data_size & 0xFF);
    buf[41] = (uint8_t)((data_size >> 8) & 0xFF);
    buf[42] = (uint8_t)((data_size >> 16) & 0xFF);
    buf[43] = (uint8_t)((data_size >> 24) & 0xFF);

    return 44;
}

/* ------------------------------------------------------------------ */
/* OpenAI transcriber session                                          */
/* ------------------------------------------------------------------ */

#define OPENAI_TRANSCRIBE_URL  "https://api.openai.com/v1/audio/transcriptions"
#define OPENAI_DEFAULT_MODEL   "gpt-4o-transcribe"

struct VoiceTranscriber {
    int sample_rate;
    int channels;

    /* Accumulated PCM audio buffer (shared between main and worker threads) */
    int16_t *pcm_buf;
    size_t   pcm_buf_len;      /* Number of int16_t samples */
    size_t   pcm_buf_cap;      /* Capacity in int16_t samples */

    /* Bytes sent in the last chunk (for incremental API calls) */
    size_t   last_sent_samples;

    /* Callback for partial results */
    VoiceTranscriberPartialFn partial_fn;
    void *partial_user_data;

    /* Last partial result text (so we don't repeat same text) */
    char *last_partial;

    /* --- Background worker thread --- */
    pthread_t       worker_thread;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             worker_running;   /* 1 while worker thread should continue */
    int             shutdown;         /* 1 to signal shutdown */
    int             new_audio;        /* 1 when new audio has been added since last send */
    int             finalize_requested; /* 1 when finalize() wants the final result */
    char           *final_result;     /* Set by worker thread after finalize */
    int             final_result_ready; /* 1 when final_result is available */
};

/* Forward declarations */
static bool openai_available(void);
static int openai_init(void);
static VoiceTranscriber* openai_open_stream(VoiceTranscriberPartialFn partial_fn,
                                            void *user_data,
                                            int sample_rate, int channels);
static int openai_feed_audio(VoiceTranscriber *vt,
                             const int16_t *pcm, size_t n_bytes);
static int openai_finalize(VoiceTranscriber *vt, char **result_out);
static void openai_close_stream(VoiceTranscriber *vt);
static void openai_cleanup(void);

static const VoiceTranscriberBackend g_openai_backend = {
    .name        = "openai",
    .description = "OpenAI gpt-4o-transcribe ($0.006/min)",
    .available   = openai_available,
    .init        = openai_init,
    .open_stream = openai_open_stream,
    .feed_audio  = openai_feed_audio,
    .finalize    = openai_finalize,
    .close_stream = openai_close_stream,
    .cleanup     = openai_cleanup,
};

const VoiceTranscriberBackend* openai_transcribe_get_backend(void) {
    return &g_openai_backend;
}

/* ------------------------------------------------------------------ */
/* Memory writer callback for libcurl response body                    */
/* ------------------------------------------------------------------ */

struct curl_mem_chunk {
    char *data;
    size_t size;
};

static size_t curl_write_cb(void *contents, size_t size, size_t nmemb,
                            void *userp) {
    size_t realsize = size * nmemb;
    struct curl_mem_chunk *mem = (struct curl_mem_chunk *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        LOG_ERROR("[openai_transcribe] Out of memory in curl write callback");
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = '\0';

    return realsize;
}

/* ------------------------------------------------------------------ */
/* Parse transcription text from JSON response                         */
/* ------------------------------------------------------------------ */

/*
 * Extract the "text" field from a JSON response like: {"text":"hello world"}
 * Returns a malloc'd string or NULL.
 */
static char* parse_transcription_text(const char *json_body) {
    if (!json_body || !*json_body) return NULL;

    /* Simple JSON parsing — look for "text":" ... " */
    const char *key = strstr(json_body, "\"text\"");
    if (!key) return NULL;

    /* Find the opening quote after the colon */
    const char *val = strchr(key + 6, ':');
    if (!val) return NULL;
    val++;  /* skip colon */

    /* Skip whitespace */
    while (*val == ' ' || *val == '\t' || *val == '\n') val++;

    if (*val != '"') return NULL;
    val++;  /* skip opening quote */

    /* Find closing quote (handle escaped quotes simply) */
    const char *end = val;
    while (*end && *end != '"') {
        if (*end == '\\' && *(end + 1)) end++;  /* skip escaped char */
        end++;
    }

    if (!*end) return NULL;  /* No closing quote */

    size_t len = (size_t)(end - val);
    if (len == 0) return NULL;

    char *result = (char *)malloc(len + 1);
    if (!result) return NULL;

    /* Copy with basic unescaping */
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        if (val[i] == '\\' && i + 1 < len) {
            switch (val[i + 1]) {
                case 'n':  result[out++] = '\n'; i++; break;
                case 't':  result[out++] = '\t'; i++; break;
                case '"':  result[out++] = '"';  i++; break;
                case '\\': result[out++] = '\\'; i++; break;
                default:   result[out++] = val[i]; break;
            }
        } else {
            result[out++] = val[i];
        }
    }
    result[out] = '\0';

    return result;
}

/* ------------------------------------------------------------------ */
/* HTTP request: send audio to OpenAI (blocking, called from worker)   */
/* ------------------------------------------------------------------ */

/*
 * Send the accumulated PCM audio as a WAV file to OpenAI.
 * Returns the transcription text (caller must free) or NULL.
 *
 * This function performs a blocking HTTP request and should only be
 * called from the background worker thread.
 */
static char* transcribe_audio_chunk(const int16_t *pcm, size_t n_samples,
                                    int sample_rate) {
    /* Build WAV in memory */
    size_t data_size = n_samples * sizeof(int16_t);
    size_t wav_size = 44 + data_size;
    uint8_t *wav_buf = (uint8_t *)malloc(wav_size);
    if (!wav_buf) {
        LOG_ERROR("[openai_transcribe] Failed to allocate WAV buffer");
        return NULL;
    }

    if (write_wav_header(wav_buf, 44, sample_rate, (uint32_t)data_size) != 44) {
        LOG_ERROR("[openai_transcribe] Failed to write WAV header");
        free(wav_buf);
        return NULL;
    }
    memcpy(wav_buf + 44, pcm, data_size);

    /* Get API key */
    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key || !*api_key) {
        LOG_ERROR("[openai_transcribe] OPENAI_API_KEY not set");
        free(wav_buf);
        return NULL;
    }

    /* Build auth header */
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);

    /* Init curl */
    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("[openai_transcribe] Failed to initialize curl");
        free(wav_buf);
        return NULL;
    }

    /* Build multipart form */
    curl_mime *mime = curl_mime_init(curl);
    if (!mime) {
        LOG_ERROR("[openai_transcribe] Failed to create mime form");
        curl_easy_cleanup(curl);
        free(wav_buf);
        return NULL;
    }

    /* File part */
    curl_mimepart *file_part = curl_mime_addpart(mime);
    curl_mime_name(file_part, "file");
    curl_mime_filename(file_part, "audio.wav");
    curl_mime_type(file_part, "audio/wav");
    curl_mime_data(file_part, (const char *)wav_buf, wav_size);

    /* Model part */
    curl_mimepart *model_part = curl_mime_addpart(mime);
    curl_mime_name(model_part, "model");
    curl_mime_data(model_part, OPENAI_DEFAULT_MODEL, CURL_ZERO_TERMINATED);

    /* Response format: json (default, but explicit) */
    curl_mimepart *fmt_part = curl_mime_addpart(mime);
    curl_mime_name(fmt_part, "response_format");
    curl_mime_data(fmt_part, "json", CURL_ZERO_TERMINATED);

    /* Response body accumulator */
    struct curl_mem_chunk chunk = {0};
    chunk.data = malloc(1);
    if (!chunk.data) {
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
        free(wav_buf);
        return NULL;
    }
    chunk.data[0] = '\0';
    chunk.size = 0;

    /* Configure curl */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);
    if (!headers) {
        LOG_ERROR("[openai_transcribe] Failed to create headers");
        free(chunk.data);
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
        free(wav_buf);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, OPENAI_TRANSCRIBE_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    /* Perform request */
    CURLcode res = curl_easy_perform(curl);
    char *result = NULL;

    if (res != CURLE_OK) {
        LOG_ERROR("[openai_transcribe] Transcription request failed: %s",
                  curl_easy_strerror(res));
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code == 200) {
            result = parse_transcription_text(chunk.data);
            if (!result) {
                LOG_WARN("[openai_transcribe] Failed to parse transcription from response");
            }
        } else {
            LOG_ERROR("[openai_transcribe] Transcription API returned HTTP %ld: %s",
                      http_code, chunk.data ? chunk.data : "(no body)");
        }
    }

    /* Cleanup */
    curl_slist_free_all(headers);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);
    free(chunk.data);
    free(wav_buf);

    return result;
}

/* ------------------------------------------------------------------ */
/* Background worker thread                                            */
/* ------------------------------------------------------------------ */

/*
 * Worker thread function.
 * Waits for new audio to be available, then sends it to OpenAI in chunks.
 * Runs until shutdown is signaled.
 */
static void* transcriber_worker(void *arg) {
    VoiceTranscriber *vt = (VoiceTranscriber *)arg;
    if (!vt) return NULL;

    LOG_DEBUG("[openai_transcribe] Worker thread started");

    /* Minimum audio to accumulate before sending (in samples) */
    size_t chunk_interval = (size_t)(vt->sample_rate * 1500 / 1000);  /* 1.5 seconds */

    pthread_mutex_lock(&vt->mutex);

    while (vt->worker_running) {
        /* Wait for: new audio, finalize request, or shutdown */
        while (vt->worker_running && !vt->shutdown &&
               !vt->new_audio && !vt->finalize_requested) {
            pthread_cond_wait(&vt->cond, &vt->mutex);
        }

        if (vt->shutdown || !vt->worker_running) {
            break;
        }

        /* Handle finalize request */
        if (vt->finalize_requested) {
            size_t n_samples = vt->pcm_buf_len;

            /* If no audio, nothing to send */
            if (n_samples == 0) {
                vt->final_result_ready = 1;
                vt->final_result = NULL;
                vt->finalize_requested = 0;
                pthread_cond_signal(&vt->cond);
                continue;
            }

            /* Copy audio buffer snapshot (release mutex for curl call) */
            int16_t *snapshot = (int16_t *)malloc(n_samples * sizeof(int16_t));
            if (!snapshot) {
                vt->final_result_ready = 1;
                vt->final_result = NULL;
                vt->finalize_requested = 0;
                pthread_cond_signal(&vt->cond);
                continue;
            }
            memcpy(snapshot, vt->pcm_buf, n_samples * sizeof(int16_t));
            int sr = vt->sample_rate;
            int finalize_is_active = 1;
            vt->new_audio = 0;

            /* Unlock while doing the blocking HTTP call */
            pthread_mutex_unlock(&vt->mutex);

            char *result = transcribe_audio_chunk(snapshot, n_samples, sr);
            free(snapshot);

            /* Re-acquire lock to store result */
            pthread_mutex_lock(&vt->mutex);

            vt->final_result = result ? strdup(result) : NULL;
            free(result);
            vt->final_result_ready = 1;
            vt->finalize_requested = 0;
            (void)finalize_is_active;

            /* Signal the caller that the result is ready */
            pthread_cond_signal(&vt->cond);
            continue;
        }

        /* Handle new audio: send partial chunk */
        if (vt->new_audio) {
            size_t unsent = vt->pcm_buf_len - vt->last_sent_samples;

            if (unsent >= chunk_interval && vt->partial_fn) {
                size_t n_samples = vt->pcm_buf_len;

                /* Copy audio buffer snapshot */
                int16_t *snapshot = (int16_t *)malloc(n_samples * sizeof(int16_t));
                if (snapshot) {
                    memcpy(snapshot, vt->pcm_buf, n_samples * sizeof(int16_t));
                    int sr = vt->sample_rate;
                    vt->new_audio = 0;

                    /* Unlock while doing the blocking HTTP call */
                    pthread_mutex_unlock(&vt->mutex);

                    char *partial = transcribe_audio_chunk(snapshot, n_samples, sr);
                    free(snapshot);

                    if (partial) {
                        /* Callback from worker thread (thread-safe: just copies text) */
                        vt->partial_fn(vt->partial_user_data, partial);
                        free(partial);
                    }

                    /* Re-acquire lock */
                    pthread_mutex_lock(&vt->mutex);

                    /* Update sent position (audio buffer may have grown) */
                    if (vt->pcm_buf_len >= n_samples) {
                        vt->last_sent_samples = vt->pcm_buf_len;
                    }
                }
            } else {
                /* Not enough audio yet, clear flag and wait for more */
                vt->new_audio = 0;
            }
        }
    }

    LOG_DEBUG("[openai_transcribe] Worker thread exiting");
    pthread_mutex_unlock(&vt->mutex);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Backend function implementations                                     */
/* ------------------------------------------------------------------ */

static bool openai_available(void) {
    const char *key = getenv("OPENAI_API_KEY");
    return (key && *key);
}

static int openai_init(void) {
    /* Curl global init is handled by http_client_init() in main */
    LOG_INFO("[openai_transcribe] Backend initialized (model: %s)", OPENAI_DEFAULT_MODEL);
    return 0;
}

static VoiceTranscriber* openai_open_stream(VoiceTranscriberPartialFn partial_fn,
                                            void *user_data,
                                            int sample_rate, int channels) {
    (void)channels;  /* Always mono */

    VoiceTranscriber *vt = (VoiceTranscriber *)calloc(1, sizeof(VoiceTranscriber));
    if (!vt) {
        LOG_ERROR("[openai_transcribe] Failed to allocate session");
        return NULL;
    }

    vt->sample_rate = sample_rate;
    vt->channels = 1;
    vt->partial_fn = partial_fn;
    vt->partial_user_data = user_data;
    vt->last_partial = NULL;

    /* Pre-allocate a reasonable buffer for ~10 seconds of audio */
    size_t initial_samples = (size_t)sample_rate * 10;
    vt->pcm_buf_cap = initial_samples;
    vt->pcm_buf = (int16_t *)calloc(initial_samples, sizeof(int16_t));
    if (!vt->pcm_buf) {
        LOG_ERROR("[openai_transcribe] Failed to allocate PCM buffer");
        free(vt);
        return NULL;
    }
    vt->pcm_buf_len = 0;
    vt->last_sent_samples = 0;

    /* Initialize synchronization primitives */
    if (pthread_mutex_init(&vt->mutex, NULL) != 0) {
        LOG_ERROR("[openai_transcribe] Failed to initialize mutex");
        free(vt->pcm_buf);
        free(vt);
        return NULL;
    }

    if (pthread_cond_init(&vt->cond, NULL) != 0) {
        LOG_ERROR("[openai_transcribe] Failed to initialize condition variable");
        pthread_mutex_destroy(&vt->mutex);
        free(vt->pcm_buf);
        free(vt);
        return NULL;
    }

    vt->worker_running = 1;
    vt->shutdown = 0;
    vt->new_audio = 0;
    vt->finalize_requested = 0;
    vt->final_result = NULL;
    vt->final_result_ready = 0;

    /* Spawn worker thread */
    int rc = pthread_create(&vt->worker_thread, NULL, transcriber_worker, vt);
    if (rc != 0) {
        LOG_ERROR("[openai_transcribe] Failed to create worker thread: %s",
                  strerror(rc));
        pthread_cond_destroy(&vt->cond);
        pthread_mutex_destroy(&vt->mutex);
        free(vt->pcm_buf);
        free(vt);
        return NULL;
    }

    LOG_DEBUG("[openai_transcribe] Stream opened, worker thread spawned");
    return vt;
}

static int openai_feed_audio(VoiceTranscriber *vt,
                             const int16_t *pcm, size_t n_bytes) {
    if (!vt || !pcm) return -1;

    size_t n_samples = n_bytes / sizeof(int16_t);

    pthread_mutex_lock(&vt->mutex);

    /* Grow buffer if needed */
    if (vt->pcm_buf_len + n_samples > vt->pcm_buf_cap) {
        size_t new_cap = vt->pcm_buf_cap * 2;
        if (new_cap < vt->pcm_buf_len + n_samples) {
            new_cap = vt->pcm_buf_len + n_samples;
        }
        /* Cap to prevent unlimited growth (~60s at 16kHz) */
        size_t max_cap = (size_t)(vt->sample_rate * 60);
        if (new_cap > max_cap) {
            new_cap = max_cap;
        }
        int16_t *new_buf = (int16_t *)realloc(vt->pcm_buf,
                                              new_cap * sizeof(int16_t));
        if (!new_buf) {
            LOG_ERROR("[openai_transcribe] Failed to grow PCM buffer");
            pthread_mutex_unlock(&vt->mutex);
            return -1;
        }
        vt->pcm_buf = new_buf;
        vt->pcm_buf_cap = new_cap;
    }

    /* Append samples */
    if (n_samples > 0) {
        memcpy(vt->pcm_buf + vt->pcm_buf_len, pcm, n_bytes);
        vt->pcm_buf_len += n_samples;
        vt->new_audio = 1;
    }

    /* Signal worker thread that new audio is available */
    pthread_cond_signal(&vt->cond);
    pthread_mutex_unlock(&vt->mutex);

    return 0;
}

static int openai_finalize(VoiceTranscriber *vt, char **result_out) {
    if (!vt || !result_out) return -1;
    *result_out = NULL;

    LOG_DEBUG("[openai_transcribe] Finalize requested");

    pthread_mutex_lock(&vt->mutex);

    if (vt->pcm_buf_len == 0) {
        LOG_WARN("[openai_transcribe] No audio recorded, nothing to transcribe");
        vt->worker_running = 0;
        vt->shutdown = 1;
        pthread_cond_signal(&vt->cond);
        pthread_mutex_unlock(&vt->mutex);
        pthread_join(vt->worker_thread, NULL);
        return -1;
    }

    /* Request the worker to send the final chunk */
    vt->finalize_requested = 1;
    vt->final_result_ready = 0;
    pthread_cond_signal(&vt->cond);

    /* Wait for the worker to complete the final transcription */
    while (!vt->final_result_ready && vt->worker_running) {
        pthread_cond_wait(&vt->cond, &vt->mutex);
    }

    char *final = vt->final_result;
    vt->final_result = NULL;

    /* Signal worker to shut down */
    vt->worker_running = 0;
    vt->shutdown = 1;
    pthread_cond_signal(&vt->cond);
    pthread_mutex_unlock(&vt->mutex);

    /* Wait for worker thread to exit */
    pthread_join(vt->worker_thread, NULL);

    if (!final) {
        LOG_ERROR("[openai_transcribe] Final transcription failed");
        return -1;
    }

    /* Trim whitespace */
    char *start = final;
    while (*start == ' ' || *start == '\t' || *start == '\n') start++;
    if (*start == '\0') {
        free(final);
        return -1;
    }

    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n')) {
        *end = '\0';
        end--;
    }

    *result_out = strdup(start);
    free(final);

    LOG_INFO("[openai_transcribe] Final transcription: \"%s\" (%zu samples, %.1fs)",
             *result_out, vt->pcm_buf_len,
             (double)vt->pcm_buf_len / (double)vt->sample_rate);

    return (*result_out != NULL) ? 0 : -1;
}

static void openai_close_stream(VoiceTranscriber *vt) {
    if (!vt) return;

    LOG_DEBUG("[openai_transcribe] Closing stream");

    /* Signal worker to shut down if still running */
    pthread_mutex_lock(&vt->mutex);
    if (vt->worker_running) {
        vt->worker_running = 0;
        vt->shutdown = 1;
        pthread_cond_signal(&vt->cond);
    }
    pthread_mutex_unlock(&vt->mutex);

    /* Wait for worker thread to exit */
    pthread_join(vt->worker_thread, NULL);

    /* Clean up synchronization primitives */
    pthread_cond_destroy(&vt->cond);
    pthread_mutex_destroy(&vt->mutex);

    /* Free resources */
    free(vt->pcm_buf);
    free(vt->last_partial);
    free(vt->final_result);
    free(vt);
}

static void openai_cleanup(void) {
    LOG_INFO("[openai_transcribe] Backend cleaned up");
    /* No persistent state to clean */
}
