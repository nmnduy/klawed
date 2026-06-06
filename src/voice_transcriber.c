/*
 * voice_transcriber.c - Backend registry implementation
 */

#include "voice_transcriber.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

#define MAX_BACKENDS 8

static const VoiceTranscriberBackend *g_backends[MAX_BACKENDS];
static int g_backend_count = 0;

int voice_transcriber_register(const VoiceTranscriberBackend *backend) {
    if (!backend || !backend->name) {
        LOG_ERROR("[voice_transcriber] Attempt to register NULL/invalid backend");
        return -1;
    }
    if (g_backend_count >= MAX_BACKENDS) {
        LOG_ERROR("[voice_transcriber] Backend registry full, cannot register '%s'",
                  backend->name);
        return -1;
    }

    g_backends[g_backend_count] = backend;
    g_backend_count++;

    LOG_INFO("[voice_transcriber] Registered backend: %s", backend->name);
    return 0;
}

const VoiceTranscriberBackend* voice_transcriber_lookup(const char *name) {
    if (!name) return NULL;

    for (int i = 0; i < g_backend_count; i++) {
        if (g_backends[i] && strcmp(g_backends[i]->name, name) == 0) {
            return g_backends[i];
        }
    }

    return NULL;
}

const VoiceTranscriberBackend* voice_transcriber_get_default(void) {
    /* Return the first available backend */
    for (int i = 0; i < g_backend_count; i++) {
        if (g_backends[i] && g_backends[i]->available && g_backends[i]->available()) {
            return g_backends[i];
        }
    }

    /* No backends available — try the openai backend specifically */
    const VoiceTranscriberBackend *openai = voice_transcriber_lookup("openai");
    if (openai && openai->available && openai->available()) {
        return openai;
    }

    return NULL;
}

void voice_transcriber_shutdown(void) {
    for (int i = 0; i < g_backend_count; i++) {
        if (g_backends[i] && g_backends[i]->cleanup) {
            g_backends[i]->cleanup();
        }
    }
    g_backend_count = 0;
    memset(g_backends, 0, sizeof(g_backends));
}
