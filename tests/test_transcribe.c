/*
 * test_transcribe.c — Unit test for OpenAI voice transcription backend
 *
 * Tests the VoiceTranscriber backend interface (openai_transcribe.c)
 * by feeding a raw PCM audio file and verifying the transcription.
 *
 * Usage:
 *   # First generate test audio:
 *   OPENAI_API_KEY=sk-... ./tests/test_transcribe_api.sh
 *
 *   # Then test the C backend:
 *   OPENAI_API_KEY=sk-... ./build/test_transcribe /tmp/test_speech.pcm
 *
 *   # Or with custom text/audio:
 *   OPENAI_API_KEY=sk-... ./build/test_transcribe /tmp/test_speech.pcm "hello world"
 *
 * The PCM file must be: 16kHz, mono, 16-bit signed little-endian.
 * Use ffmpeg to convert: ffmpeg -i input -ar 16000 -ac 1 -f s16le output.pcm
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/voice_transcriber.h"
#include "../src/openai_transcribe.h"

#define SAMPLE_RATE 16000

int main(int argc, char **argv) {
    const char *pcm_path;
    const char *known_text = NULL;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <audio.pcm> [known_text]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "  audio.pcm  — Raw 16kHz mono 16-bit signed LE PCM file\n");
        fprintf(stderr, "  known_text — Expected transcription text (optional, for verification)\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "  Generate test audio first:\n");
        fprintf(stderr, "    OPENAI_API_KEY=sk-... ./tests/test_transcribe_api.sh\n");
        fprintf(stderr, "    OPENAI_API_KEY=sk-... ./build/test_transcribe /tmp/test_speech.pcm\n");
        return 1;
    }

    pcm_path = argv[1];
    if (argc >= 3) {
        known_text = argv[2];
    }

    /* ── Check API key ─────────────────────────────────────────── */

    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key || !*api_key) {
        fprintf(stderr, "ERROR: OPENAI_API_KEY not set\n");
        return 1;
    }

    /* ── Read PCM file ─────────────────────────────────────────── */

    FILE *f = fopen(pcm_path, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: Cannot open '%s'\n", pcm_path);
        return 1;
    }

    /* Get file size */
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "ERROR: Cannot seek '%s'\n", pcm_path);
        fclose(f);
        return 1;
    }
    long fsize = ftell(f);
    if (fsize <= 0) {
        fprintf(stderr, "ERROR: File '%s' is empty\n", pcm_path);
        fclose(f);
        return 1;
    }
    rewind(f);

    int16_t *pcm = (int16_t *)malloc((size_t)fsize);
    if (!pcm) {
        fprintf(stderr, "ERROR: malloc(%ld) failed\n", fsize);
        fclose(f);
        return 1;
    }

    size_t n_read = fread(pcm, 1, (size_t)fsize, f);
    fclose(f);

    if (n_read != (size_t)fsize) {
        fprintf(stderr, "ERROR: Short read: %zu of %ld bytes\n", n_read, fsize);
        free(pcm);
        return 1;
    }

    size_t n_samples = n_read / sizeof(int16_t);
    double duration_sec = (double)n_samples / (double)SAMPLE_RATE;

    printf("=== Voice Transcription Backend Test ===\n");
    printf("PCM file:  %s\n", pcm_path);
    printf("Samples:   %zu (%.2f seconds)\n", n_samples, duration_sec);
    printf("Sample rate: %d Hz, mono, 16-bit signed LE\n", SAMPLE_RATE);
    printf("\n");

    /* ── Initialize transcription backend ──────────────────────── */

    const VoiceTranscriberBackend *backend = openai_transcribe_get_backend();
    voice_transcriber_register(backend);

    if (!backend->available()) {
        fprintf(stderr, "ERROR: Backend reports unavailable\n");
        free(pcm);
        return 1;
    }

    printf("Backend: %s\n", backend->description);
    printf("\n");

    /* ── Open stream ───────────────────────────────────────────── */

    VoiceTranscriber *vt = backend->open_stream(NULL, NULL, SAMPLE_RATE, 1);
    if (!vt) {
        fprintf(stderr, "ERROR: open_stream() returned NULL\n");
        free(pcm);
        return 1;
    }
    printf("[OK] open_stream() succeeded\n");

    /* ── Feed audio ────────────────────────────────────────────── */

    int ret = backend->feed_audio(vt, pcm, n_read);
    if (ret != 0) {
        fprintf(stderr, "ERROR: feed_audio() returned %d\n", ret);
        backend->close_stream(vt);
        free(pcm);
        return 1;
    }
    printf("[OK] feed_audio() succeeded (%zu bytes fed)\n", n_read);

    /* ── Finalize transcription ────────────────────────────────── */

    char *transcription = NULL;
    ret = backend->finalize(vt, &transcription);
    if (ret != 0 || !transcription) {
        fprintf(stderr, "ERROR: finalize() returned %d\n", ret);
        backend->close_stream(vt);
        free(pcm);
        return 1;
    }
    printf("[OK] finalize() succeeded\n");

    /* ── Results ───────────────────────────────────────────────── */

    printf("\n═══════════════════════════════════════════\n");
    printf("Transcription: \"%s\"\n", transcription);
    printf("═══════════════════════════════════════════\n");

    int passed = 0;
    if (known_text && *known_text) {
        printf("\nKnown text:    \"%s\"\n", known_text);

        /* Case-insensitive word-by-word comparison */
        char *known_lower = strdup(known_text);
        char *trans_lower = strdup(transcription);
        if (known_lower && trans_lower) {
            for (char *p = known_lower; *p; p++) {
                if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
            }
            for (char *p = trans_lower; *p; p++) {
                if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
            }

            /* Count matching words using simple substring search */
            int found = 0, total = 0;
            char *known_copy = strdup(known_lower);
            if (known_copy) {
                char *save;
                char *kw = strtok_r(known_copy, " ,.!?;:\"'-\t\n\r", &save);
                while (kw) {
                    total++;
                    if (strstr(trans_lower, kw)) {
                        found++;
                    }
                    kw = strtok_r(NULL, " ,.!?;:\"'-\t\n\r", &save);
                }
                free(known_copy);
            }

            int match_pct = (total > 0) ? (found * 100 / total) : 0;
            printf("Word match:   %d/%d (%d%%)\n", found, total, match_pct);

            if (found >= total * 60 / 100) {
                printf("\n  PASS \u2713 — %d/%d words matched\n", found, total);
                passed = 1;
            } else {
                printf("\n  FAIL \u2717 — only %d/%d words matched\n", found, total);
            }
        }
        free(known_lower);
        free(trans_lower);
    } else {
        /* No known text — just print the result */
        printf("\n  No known text provided — transcription stands alone.\n");
        passed = 1;  /* Not a failure if we got a result */
    }

    /* ── Cleanup ───────────────────────────────────────────────── */

    free(transcription);
    backend->close_stream(vt);
    free(pcm);

    return passed ? 0 : 1;
}
