/*
 * indicators.h - Visual indicators for tool execution and API calls
 *
 * Provides animated spinners and status indicators for GPU-accelerated terminals
 * Supports Unicode characters and smooth ANSI animations
 */

#ifndef INDICATORS_H
#define INDICATORS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "fallback_colors.h"
#include "logger.h"
#define COLORSCHEME_EXTERN
#include "colorscheme.h"

// Standard spinner frames
static const char *SPINNER_FRAMES[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
static const int SPINNER_FRAME_COUNT = 10;
static const int SPINNER_DELAY_MS = 80;

// Alternative spinner styles
static const char *SPINNER_DOTS[]   = {"⣾","⣽","⣻","⢿","⡿","⣟","⣯","⣷"};
static const char *SPINNER_LINE[]   = {"-","\\","|","/"};
static const char *SPINNER_BOX[]    = {"◰","◳","◲","◱"};
static const char *SPINNER_CIRCLE[] = {"◜","◠","◝","◞","◡","◟"};

#if defined(__GNUC__) || defined(__clang__)
#define INDICATOR_UNUSED __attribute__((unused))
#else
#define INDICATOR_UNUSED
#endif

// Color accessors
static inline const char* get_spinner_color_status(void) {
    static char buf[32]; static int w=0;
    if (get_colorscheme_color(COLORSCHEME_STATUS, buf, sizeof(buf))==0) return buf;
    if (!w) { LOG_WARN("Using fallback color for spinner (status)"); w=1; }
    return ANSI_FALLBACK_YELLOW;
}
static inline const char* get_spinner_color_tool(void) {
    static char buf[32]; static int w=0;
    if (get_colorscheme_color(COLORSCHEME_TOOL, buf, sizeof(buf))==0) return buf;
    if (!w) { LOG_WARN("Using fallback color for spinner (tool)"); w=1; }
    return ANSI_FALLBACK_CYAN;
}
static inline const char* get_spinner_color_success(void) {
    static char buf[32]; static int w=0;
    if (get_colorscheme_color(COLORSCHEME_USER, buf, sizeof(buf))==0) return buf;
    if (!w) { LOG_WARN("Using fallback color for spinner (success)"); w=1; }
    return ANSI_FALLBACK_GREEN;
}
static inline const char* get_spinner_color_error(void) {
    static char buf[32]; static int w=0;
    if (get_colorscheme_color(COLORSCHEME_ERROR, buf, sizeof(buf))==0) return buf;
    if (!w) { LOG_WARN("Using fallback color for spinner (error)"); w=1; }
    return ANSI_FALLBACK_ERROR;
}

#define SPINNER_CYAN    get_spinner_color_tool()
#define SPINNER_YELLOW  get_spinner_color_status()
#define SPINNER_GREEN   get_spinner_color_success()
#define SPINNER_RESET   ANSI_RESET

// Collection of spinner variants
typedef struct { const char **frames; int count; } spinner_variant_t;
static const spinner_variant_t SPINNER_VARIANTS[] = {
    { SPINNER_FRAMES,  SPINNER_FRAME_COUNT },
    { SPINNER_DOTS,    (int)(sizeof(SPINNER_DOTS)/sizeof(*SPINNER_DOTS)) },
    { SPINNER_LINE,    (int)(sizeof(SPINNER_LINE)/sizeof(*SPINNER_LINE)) },
    { SPINNER_BOX,     (int)(sizeof(SPINNER_BOX)/sizeof(*SPINNER_BOX)) },
    { SPINNER_CIRCLE,  (int)(sizeof(SPINNER_CIRCLE)/sizeof(*SPINNER_CIRCLE)) }
};
static const int SPINNER_VARIANT_COUNT = (int)(sizeof(SPINNER_VARIANTS)/sizeof(*SPINNER_VARIANTS));

// Global spinner variant: seeded once per app lifecycle
static spinner_variant_t GLOBAL_SPINNER_VARIANT = {NULL,0};
static void init_global_spinner_variant(void) {
    static int init = 0;
    if (init) return;
    srand((unsigned)time(NULL));
    int idx = rand() % SPINNER_VARIANT_COUNT;
    GLOBAL_SPINNER_VARIANT = SPINNER_VARIANTS[idx];
    init = 1;
}

// Spinner object
typedef struct {
    pthread_t thread;
    int running;
    char *message;
    const char *color;
    pthread_mutex_t lock;
    const char **frames;
    int frame_count;
} Spinner;

static void *spinner_thread_func(void *arg) {
    Spinner *s = (Spinner*)arg;
    int idx = 0;
    // Hide cursor
    printf("\033[?25l"); fflush(stdout);
    while (1) {
        pthread_mutex_lock(&s->lock);
        if (!s->running) { pthread_mutex_unlock(&s->lock); break; }
        printf("\r\033[K%s%s%s %s", s->color, s->frames[idx], SPINNER_RESET, s->message);
        fflush(stdout);
        pthread_mutex_unlock(&s->lock);
        idx = (idx + 1) % s->frame_count;
        usleep((unsigned int)(SPINNER_DELAY_MS * 1000));
    }
    // Restore cursor
    printf("\033[?25h"); fflush(stdout);
    return NULL;
}

// Start spinner; style fixed once per lifecycle
static INDICATOR_UNUSED Spinner* spinner_start(const char *message, const char *color) {
    init_global_spinner_variant();
    Spinner *s = malloc(sizeof(Spinner)); if (!s) return NULL;
    s->message = strdup(message);
    s->color = color ? color : SPINNER_CYAN;
    s->running = 1;
    pthread_mutex_init(&s->lock, NULL);
    s->frames = GLOBAL_SPINNER_VARIANT.frames;
    s->frame_count = GLOBAL_SPINNER_VARIANT.count;
    pthread_create(&s->thread, NULL, spinner_thread_func, s);
    return s;
}

// Update spinner text
static INDICATOR_UNUSED void spinner_update(Spinner *s, const char *msg) {
    if (!s) return;
    pthread_mutex_lock(&s->lock);
    free(s->message);
    s->message = strdup(msg);
    pthread_mutex_unlock(&s->lock);
}

// Stop spinner and display final state
static INDICATOR_UNUSED void spinner_stop(Spinner *s, const char *final_message, int success) {
    if (!s) return;
    pthread_mutex_lock(&s->lock);
    s->running = 0;
    pthread_mutex_unlock(&s->lock);
    pthread_join(s->thread, NULL);
    printf("\r\033[K");
    if (final_message) {
        if (success) printf("%s✓%s %s\n", SPINNER_GREEN, SPINNER_RESET, final_message);
        else         printf("%s✗%s %s\n", get_spinner_color_error(), SPINNER_RESET, final_message);
    }
    fflush(stdout);
    pthread_mutex_destroy(&s->lock);
    free(s->message);
    free(s);
}

#endif // INDICATORS_H
