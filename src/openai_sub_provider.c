/*
 * openai_sub_provider.c - OpenAI Subscription Provider
 *
 * Implements the Provider interface for OpenAI using OAuth 2.0 device
 * authorization. Uses the same OpenAI chat completions API format as the
 * regular OpenAI provider, but authenticates via OAuth bearer token instead
 * of an API key.
 *
 * This allows users with a ChatGPT Plus/Pro subscription to use klawed
 * without needing a separate API billing account.
 */

#define _POSIX_C_SOURCE 200809L

#include "klawed_internal.h"
#include "openai_sub_provider.h"
#include "openai_oauth.h"
#include "openai_messages.h"
#include "openai_responses.h"
#include "openai_provider.h"
#include "tools/tool_definitions.h"
#include "http_client.h"
#include "logger.h"
#include "arena.h"
#include "retry_logic.h"
#include "tui.h"
#include "message_queue.h"
#include "sqlite_queue.h"
#include "util/string_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <curl/curl.h>
#include <bsd/string.h>

/* Default model and API endpoint — ChatGPT backend Responses API */
#define OPENAI_SUB_DEFAULT_MODEL    "gpt-5.3-codex"
#define OPENAI_SUB_DEFAULT_API_BASE "https://chatgpt.com/backend-api/codex/responses"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static char *arena_strdup(Arena *arena, const char *str) {
    if (!str || !arena) return NULL;
    size_t len = strlen(str) + 1;
    char *out = arena_alloc(arena, len);
    if (!out) return NULL;
    strlcpy(out, str, len);
    return out;
}

/**
 * OAuth message callback — routes messages to TUI, sqlite-queue client, or console.
 *
 * Priority order:
 *  1. sqlite-queue daemon mode: send AUTH_REQUIRED to the "client" receiver
 *     so the caller can display the login URL / user code.
 *  2. TUI mode: post to TUI message queue or add conversation line.
 *  3. Fallback: print to stdout / stderr.
 *
 * Both #1 and #2 can be active simultaneously (daemon mode with a TUI is
 * unusual but the code handles it gracefully).
 */
static void openai_sub_oauth_message_callback(void *user_data,
                                               const char *message,
                                               int is_error) {
    ConversationState *state = (ConversationState *)user_data;

    /* --- sqlite-queue relay (daemon mode) -------------------------------- */
    if (state && state->sqlite_queue_context) {
        sqlite_queue_send_auth_message(
            state->sqlite_queue_context,
            "client", message, is_error);
        /* Fall through — also show in TUI / console if available */
    }

    /* --- TUI display ----------------------------------------------------- */
    if (state && (state->tui_queue || state->tui)) {
        TUIColorPair color = is_error ? COLOR_PAIR_ERROR : COLOR_PAIR_STATUS;
        if (state->tui_queue) {
            post_tui_stream_start(state->tui_queue, "[OpenAI Auth]", (int)color);
            post_tui_message(state->tui_queue, TUI_MSG_STREAM_APPEND, message);
        } else {
            tui_add_conversation_line(state->tui, "[OpenAI Auth]", message, color);
            tui_refresh(state->tui);
        }
        return;
    }

    /* --- Console fallback (no TUI, no queue, or queue already printed) --- */
    if (!state || !state->sqlite_queue_context) {
        /* Only print to console if we haven't already relayed via queue
         * (sqlite_queue_send_auth_message echoes to console itself) */
        if (is_error) {
            fprintf(stderr, "%s\n", message);
        } else {
            printf("%s\n", message);
        }
    }
}

/* Progress callback for interrupt handling */
static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                              curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    ConversationState *state = (ConversationState *)clientp;
    if (state && state->interrupt_requested) {
        LOG_DEBUG("OpenAI Sub: interrupt requested, aborting HTTP request");
        return 1;
    }
    return 0;
}

/* ============================================================================
 * Streaming Support (reuse OpenAI streaming infrastructure)
 * ============================================================================ */

typedef struct {
    ConversationState *state;
    char *accumulated_text;
    size_t accumulated_size;
    size_t accumulated_capacity;
    char *accumulated_reasoning;
    size_t reasoning_size;
    size_t reasoning_capacity;
    char *finish_reason;
    char *model;
    char *message_id;
    int tool_calls_count;
    cJSON *tool_calls_array;
    Arena *arena;
    int reasoning_line_added;
    int assistant_line_added;
} SubStreamingContext;

static void sub_streaming_context_init(SubStreamingContext *ctx,
                                        ConversationState *state) {
    memset(ctx, 0, sizeof(SubStreamingContext));
    ctx->state = state;
    ctx->arena = arena_create(64 * 1024);
    if (!ctx->arena) return;

    ctx->accumulated_capacity = 4096;
    ctx->accumulated_text = arena_alloc(ctx->arena, ctx->accumulated_capacity);
    if (ctx->accumulated_text) ctx->accumulated_text[0] = '\0';

    ctx->reasoning_capacity = 4096;
    ctx->accumulated_reasoning = arena_alloc(ctx->arena, ctx->reasoning_capacity);
    if (ctx->accumulated_reasoning) ctx->accumulated_reasoning[0] = '\0';

    ctx->tool_calls_array = cJSON_CreateArray();
}

static void sub_streaming_context_free(SubStreamingContext *ctx) {
    if (!ctx) return;
    if (ctx->tool_calls_array) {
        cJSON_Delete(ctx->tool_calls_array);
        ctx->tool_calls_array = NULL;
    }
    if (ctx->arena) {
        arena_destroy(ctx->arena);
        ctx->arena = NULL;
    }
}

/**
 * Append text to streaming buffer, growing if needed via arena.
 * Since arena doesn't support realloc, we use a grow-by-doubling scheme
 * with a fresh arena allocation when capacity is exceeded.
 */
static int sub_streaming_append(SubStreamingContext *ctx,
                                 char **buf, size_t *size, size_t *cap,
                                 const char *text) {
    if (!ctx || !buf || !*buf || !text) return 0;

    size_t text_len = strlen(text);
    if (text_len == 0) return 1;

    /* Grow if needed */
    if (*size + text_len + 1 > *cap) {
        size_t new_cap = *cap * 2;
        while (new_cap < *size + text_len + 1) new_cap *= 2;

        char *new_buf = arena_alloc(ctx->arena, new_cap);
        if (!new_buf) return 0;

        if (*size > 0) {
            strlcpy(new_buf, *buf, new_cap);
        } else {
            new_buf[0] = '\0';
        }
        *buf = new_buf;
        *cap = new_cap;
    }

    strlcat(*buf, text, *cap);
    *size += text_len;
    return 1;
}

/**
 * SSE streaming event handler for OpenAI-format chunks.
 */
static int sub_streaming_event_handler(StreamEvent *event, void *userdata) {
    SubStreamingContext *ctx = (SubStreamingContext *)userdata;

    if (ctx->state && ctx->state->interrupt_requested) {
        return 1;  /* Abort */
    }

    if (!event || !event->data) {
        return 0;
    }

    if (event->type != SSE_EVENT_OPENAI_CHUNK) {
        return 0;
    }

    if (!ctx->arena) return 0;

    /* Extract model and id if not yet captured */
    if (!ctx->model) {
        cJSON *model_j = cJSON_GetObjectItem(event->data, "model");
        if (model_j && cJSON_IsString(model_j)) {
            size_t len = strlen(model_j->valuestring) + 1;
            ctx->model = arena_alloc(ctx->arena, len);
            if (ctx->model) strlcpy(ctx->model, model_j->valuestring, len);
        }
    }
    if (!ctx->message_id) {
        cJSON *id_j = cJSON_GetObjectItem(event->data, "id");
        if (id_j && cJSON_IsString(id_j)) {
            size_t len = strlen(id_j->valuestring) + 1;
            ctx->message_id = arena_alloc(ctx->arena, len);
            if (ctx->message_id) strlcpy(ctx->message_id, id_j->valuestring, len);
        }
    }

    /* Extract choices[0].delta */
    cJSON *choices = cJSON_GetObjectItem(event->data, "choices");
    if (!choices || !cJSON_IsArray(choices)) return 0;

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    if (!choice) return 0;

    /* Capture finish_reason */
    cJSON *finish_reason_j = cJSON_GetObjectItem(choice, "finish_reason");
    if (finish_reason_j && cJSON_IsString(finish_reason_j) &&
        finish_reason_j->valuestring[0] != '\0') {
        size_t len = strlen(finish_reason_j->valuestring) + 1;
        ctx->finish_reason = arena_alloc(ctx->arena, len);
        if (ctx->finish_reason) {
            strlcpy(ctx->finish_reason, finish_reason_j->valuestring, len);
        }
    }

    cJSON *delta = cJSON_GetObjectItem(choice, "delta");
    if (!delta) return 0;

    /* Text content */
    cJSON *content = cJSON_GetObjectItem(delta, "content");
    if (content && cJSON_IsString(content) && content->valuestring[0] != '\0') {
        const char *text = content->valuestring;

        /* First text: add assistant line to TUI */
        if (!ctx->assistant_line_added && ctx->state) {
            if (ctx->state->tui_queue) {
                post_tui_stream_start(ctx->state->tui_queue, "[Assistant]", COLOR_PAIR_ASSISTANT);
            } else if (ctx->state->tui) {
                tui_add_conversation_line(ctx->state->tui, "[Assistant]", "", COLOR_PAIR_ASSISTANT);
            }
            ctx->assistant_line_added = 1;
        }

        /* Append to TUI */
        if (ctx->state) {
            if (ctx->state->tui_queue) {
                post_tui_message(ctx->state->tui_queue, TUI_MSG_STREAM_APPEND, text);
            } else if (ctx->state->tui) {
                tui_update_last_conversation_line(ctx->state->tui, text);
            }
        }

        sub_streaming_append(ctx,
                              &ctx->accumulated_text,
                              &ctx->accumulated_size,
                              &ctx->accumulated_capacity,
                              text);
    }

    /* reasoning_content (for o-series models) */
    cJSON *reasoning = cJSON_GetObjectItem(delta, "reasoning_content");
    if (reasoning && cJSON_IsString(reasoning) && reasoning->valuestring[0] != '\0') {
        if (!ctx->reasoning_line_added && ctx->state) {
            if (ctx->state->tui_queue) {
                post_tui_stream_start(ctx->state->tui_queue, "[Reasoning]", COLOR_PAIR_STATUS);
            }
            ctx->reasoning_line_added = 1;
        }
        sub_streaming_append(ctx,
                              &ctx->accumulated_reasoning,
                              &ctx->reasoning_size,
                              &ctx->reasoning_capacity,
                              reasoning->valuestring);
    }

    /* Tool calls */
    cJSON *tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        int tcount = cJSON_GetArraySize(tool_calls);
        for (int i = 0; i < tcount; i++) {
            cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
            if (!tc) continue;

            cJSON *idx_j = cJSON_GetObjectItem(tc, "index");
            int tc_idx = idx_j ? idx_j->valueint : 0;

            /* Grow tool_calls_array if needed */
            while (cJSON_GetArraySize(ctx->tool_calls_array) <= tc_idx) {
                cJSON *placeholder = cJSON_CreateObject();
                cJSON_AddItemToArray(ctx->tool_calls_array, placeholder);
            }

            cJSON *existing = cJSON_GetArrayItem(ctx->tool_calls_array, tc_idx);
            if (!existing) continue;

            /* Merge fields */
            cJSON *type_j = cJSON_GetObjectItem(tc, "type");
            if (type_j && cJSON_IsString(type_j)) {
                cJSON_DeleteItemFromObject(existing, "type");
                cJSON_AddStringToObject(existing, "type", type_j->valuestring);
            }
            cJSON *id_j = cJSON_GetObjectItem(tc, "id");
            if (id_j && cJSON_IsString(id_j)) {
                cJSON_DeleteItemFromObject(existing, "id");
                cJSON_AddStringToObject(existing, "id", id_j->valuestring);
            }

            cJSON *fn = cJSON_GetObjectItem(tc, "function");
            if (fn) {
                cJSON *existing_fn = cJSON_GetObjectItem(existing, "function");
                if (!existing_fn) {
                    existing_fn = cJSON_AddObjectToObject(existing, "function");
                }
                if (existing_fn) {
                    cJSON *name_j = cJSON_GetObjectItem(fn, "name");
                    if (name_j && cJSON_IsString(name_j)) {
                        cJSON_DeleteItemFromObject(existing_fn, "name");
                        cJSON_AddStringToObject(existing_fn, "name", name_j->valuestring);
                    }
                    cJSON *args_delta = cJSON_GetObjectItem(fn, "arguments");
                    if (args_delta && cJSON_IsString(args_delta)) {
                        cJSON *existing_args = cJSON_GetObjectItem(existing_fn, "arguments");
                        if (existing_args && cJSON_IsString(existing_args)) {
                            /* Append to existing arguments */
                            size_t old_len = strlen(existing_args->valuestring);
                            size_t add_len = strlen(args_delta->valuestring);
                            char *combined = malloc(old_len + add_len + 1);
                            if (combined) {
                                strlcpy(combined, existing_args->valuestring, old_len + add_len + 1);
                                strlcat(combined, args_delta->valuestring, old_len + add_len + 1);
                                cJSON_DeleteItemFromObject(existing_fn, "arguments");
                                cJSON_AddStringToObject(existing_fn, "arguments", combined);
                                free(combined);
                            }
                        } else {
                            cJSON_DeleteItemFromObject(existing_fn, "arguments");
                            cJSON_AddStringToObject(existing_fn, "arguments",
                                                    args_delta->valuestring);
                        }
                    }
                }
            }
            ctx->tool_calls_count = cJSON_GetArraySize(ctx->tool_calls_array);
        }
    }

    return 0;
}

/* ============================================================================
 * Response Parsing
 * ============================================================================ */

/**
 * Parse an OpenAI-format JSON response into an ApiResponse.
 * The json object is owned by the returned ApiResponse (stored in raw_response).
 * Caller owns the returned ApiResponse (and its arena).
 */
static ApiResponse *parse_response(cJSON *json) {
    Arena *arena = arena_create(16384);
    if (!arena) return NULL;

    ApiResponse *resp = arena_alloc(arena, sizeof(ApiResponse));
    if (!resp) {
        arena_destroy(arena);
        return NULL;
    }
    memset(resp, 0, sizeof(ApiResponse));
    resp->arena        = arena;
    resp->raw_response = json;  /* ApiResponse owns the json object */

    cJSON *choices = cJSON_GetObjectItem(json, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        LOG_WARN("OpenAI Sub: response has no choices");
        return resp;
    }

    cJSON *choice  = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");
    if (!message) {
        LOG_WARN("OpenAI Sub: response choice has no message");
        return resp;
    }

    /* Text content */
    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (content && cJSON_IsString(content) && content->valuestring) {
        resp->message.text = arena_strdup(arena, content->valuestring);
        if (resp->message.text) {
            trim_whitespace(resp->message.text);
        }
    }

    /* Tool calls */
    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        int raw_count = cJSON_GetArraySize(tool_calls);

        /* Count valid calls */
        int valid = 0;
        for (int i = 0; i < raw_count; i++) {
            cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
            if (cJSON_GetObjectItem(tc, "function")) valid++;
        }

        if (valid > 0) {
            resp->tools = arena_alloc(arena, (size_t)valid * sizeof(ToolCall));
            if (resp->tools) {
                memset(resp->tools, 0, (size_t)valid * sizeof(ToolCall));
                int idx = 0;
                for (int i = 0; i < raw_count; i++) {
                    cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
                    cJSON *fn = cJSON_GetObjectItem(tc, "function");
                    if (!fn) continue;

                    cJSON *id_j   = cJSON_GetObjectItem(tc, "id");
                    cJSON *name_j = cJSON_GetObjectItem(fn, "name");
                    cJSON *args_j = cJSON_GetObjectItem(fn, "arguments");

                    resp->tools[idx].id = (id_j && cJSON_IsString(id_j))
                                          ? arena_strdup(arena, id_j->valuestring) : NULL;
                    resp->tools[idx].name = (name_j && cJSON_IsString(name_j))
                                            ? arena_strdup(arena, name_j->valuestring) : NULL;

                    if (args_j && cJSON_IsString(args_j)) {
                        resp->tools[idx].parameters = cJSON_Parse(args_j->valuestring);
                        if (!resp->tools[idx].parameters) {
                            resp->tools[idx].parameters = cJSON_CreateObject();
                        }
                    } else {
                        resp->tools[idx].parameters = cJSON_CreateObject();
                    }
                    idx++;
                }
                resp->tool_count = valid;
            }
        }
    }

    return resp;
}

/* ============================================================================
 * Provider call_api Implementation
 * ============================================================================ */


/**
 * Structure to track a pending function call during SSE parsing
 */
typedef struct {
    char id[128];
    char call_id[128];
    char name[64];
    char *arguments;
    size_t args_cap;
    size_t args_len;
    int complete;
} PendingFunctionCall;
/**
 * Parse the SSE body returned by the chatgpt backend Responses API into a
 * synthetic chat-completions JSON object so the rest of the pipeline can
 * process it unchanged.
 *
 * Events we care about:
 *   response.output_text.delta          -> accumulate text
 *   response.output_item.added          -> function call started (has item.id, call_id, name)
 *   response.function_call_arguments.delta -> accumulate args
 *   response.function_call_arguments.done -> args complete
 *   response.output_item.done           -> item complete (has full arguments)
 *   response.completed                  -> extract usage
 *   response.failed / error             -> surface error
 */
static cJSON *parse_chatgpt_sse_response(const char *sse_body) {
    if (!sse_body) return NULL;

    /* Buffers for accumulated text and usage */
    size_t text_cap  = 4096;
    size_t text_len  = 0;
    char  *text_buf  = malloc(text_cap);
    if (!text_buf) return NULL;
    text_buf[0] = '\0';


    /* Track function calls */
    PendingFunctionCall *funcs = NULL;
    int func_count = 0;
    int func_cap = 0;
    long   input_tokens  = 0;
    long   output_tokens = 0;
    char   model_str[128] = "gpt-5.3-codex";
    char   resp_id[128]   = "chatgpt_sub";
    int    got_error      = 0;
    char   error_msg[512] = {0};

    /* Walk line by line */
    const char *p = sse_body;
    while (*p) {
        /* Find end of line */
        const char *eol = strchr(p, '\n');
        size_t line_len = eol ? (size_t)(eol - p) : strlen(p);

        if (line_len > 5 && strncmp(p, "data:", 5) == 0) {
            /* Skip leading space after "data:" */
            const char *json_start = p + 5;
            if (*json_start == ' ') json_start++;
            size_t json_len = line_len - (size_t)(json_start - p);

            if (json_len > 0 && strncmp(json_start, "[DONE]", 6) != 0) {
                cJSON *ev = cJSON_ParseWithLength(json_start, json_len);
                if (ev) {
                    cJSON *type_item = cJSON_GetObjectItem(ev, "type");
                    const char *ev_type = (type_item && cJSON_IsString(type_item))
                                          ? type_item->valuestring : "";

                    if (strcmp(ev_type, "response.output_text.delta") == 0) {
                        cJSON *delta = cJSON_GetObjectItem(ev, "delta");
                        if (delta && cJSON_IsString(delta) && delta->valuestring) {
                            size_t dlen = strlen(delta->valuestring);
                            if (text_len + dlen + 1 >= text_cap) {
                                text_cap = (text_len + dlen + 1) * 2;
                                char *tmp = realloc(text_buf, text_cap);
                                if (!tmp) { free(text_buf); cJSON_Delete(ev); return NULL; }
                                text_buf = tmp;
                            }
                            strlcpy(text_buf + text_len, delta->valuestring,
                                    text_cap - text_len);
                            text_len += dlen;
                        }
                    }
                    /* New function call started */
                    else if (strcmp(ev_type, "response.output_item.added") == 0) {
                        cJSON *item = cJSON_GetObjectItem(ev, "item");
                        if (item) {
                            cJSON *item_type = cJSON_GetObjectItem(item, "type");
                            if (item_type && cJSON_IsString(item_type) &&
                                strcmp(item_type->valuestring, "function_call") == 0) {
                                /* Start tracking a new function call */
                                if (func_count >= func_cap) {
                                    func_cap = func_cap ? func_cap * 2 : 4;
                                    PendingFunctionCall *tmp = realloc(funcs,
                                        (size_t)func_cap * sizeof(PendingFunctionCall));
                                    if (!tmp) { free(text_buf); cJSON_Delete(ev); return NULL; }
                                    funcs = tmp;
                                }
                                PendingFunctionCall *f = &funcs[func_count];
                                memset(f, 0, sizeof(*f));

                                cJSON *id_j = cJSON_GetObjectItem(item, "id");
                                if (id_j && cJSON_IsString(id_j)) {
                                    strlcpy(f->id, id_j->valuestring, sizeof(f->id));
                                }
                                cJSON *call_id_j = cJSON_GetObjectItem(item, "call_id");
                                if (call_id_j && cJSON_IsString(call_id_j)) {
                                    strlcpy(f->call_id, call_id_j->valuestring, sizeof(f->call_id));
                                }
                                cJSON *name_j = cJSON_GetObjectItem(item, "name");
                                if (name_j && cJSON_IsString(name_j)) {
                                    strlcpy(f->name, name_j->valuestring, sizeof(f->name));
                                }
                                func_count++;
                            }
                        }
                    }
                    /* Incremental function call arguments */
                    else if (strcmp(ev_type, "response.function_call_arguments.delta") == 0) {
                        cJSON *delta = cJSON_GetObjectItem(ev, "delta");
                        cJSON *item_id = cJSON_GetObjectItem(ev, "item_id");
                        if (delta && cJSON_IsString(delta) && delta->valuestring &&
                            item_id && cJSON_IsString(item_id)) {
                            /* Find the matching function call */
                            for (int i = 0; i < func_count; i++) {
                                if (strcmp(funcs[i].id, item_id->valuestring) == 0) {
                                    PendingFunctionCall *f = &funcs[i];
                                    size_t dlen = strlen(delta->valuestring);
                                    if (f->args_len + dlen + 1 > f->args_cap) {
                                        f->args_cap = (f->args_len + dlen + 1) * 2;
                                        if (f->args_cap < 256) f->args_cap = 256;
                                        char *tmp = realloc(f->arguments, f->args_cap);
                                        if (!tmp) break;
                                        f->arguments = tmp;
                                    }
                                    memcpy(f->arguments + f->args_len, delta->valuestring, dlen);
                                    f->args_len += dlen;
                                    f->arguments[f->args_len] = '\0';
                                    break;
                                }
                            }
                        }
                    }
                    /* Function call arguments complete */
                    else if (strcmp(ev_type, "response.function_call_arguments.done") == 0) {
                        cJSON *item_id = cJSON_GetObjectItem(ev, "item_id");
                        cJSON *args_j = cJSON_GetObjectItem(ev, "arguments");
                        if (item_id && cJSON_IsString(item_id) &&
                            args_j && cJSON_IsString(args_j)) {
                            for (int i = 0; i < func_count; i++) {
                                if (strcmp(funcs[i].id, item_id->valuestring) == 0) {
                                    PendingFunctionCall *f = &funcs[i];
                                    /* Replace accumulated args with final args */
                                    free(f->arguments);
                                    f->arguments = strdup(args_j->valuestring);
                                    f->args_len = strlen(args_j->valuestring);
                                    f->args_cap = f->args_len + 1;
                                    break;
                                }
                            }
                        }
                    }
                    /* Output item complete (may have final function call data) */
                    else if (strcmp(ev_type, "response.output_item.done") == 0) {
                        cJSON *item = cJSON_GetObjectItem(ev, "item");
                        if (item) {
                            cJSON *item_type = cJSON_GetObjectItem(item, "type");
                            if (item_type && cJSON_IsString(item_type) &&
                                strcmp(item_type->valuestring, "function_call") == 0) {
                                cJSON *id_j = cJSON_GetObjectItem(item, "id");
                                cJSON *args_j = cJSON_GetObjectItem(item, "arguments");
                                if (id_j && cJSON_IsString(id_j) &&
                                    args_j && cJSON_IsString(args_j)) {
                                    /* Find and update the matching function call */
                                    for (int i = 0; i < func_count; i++) {
                                        if (strcmp(funcs[i].id, id_j->valuestring) == 0) {
                                            PendingFunctionCall *f = &funcs[i];
                                            /* Ensure we have the final arguments */
                                            free(f->arguments);
                                            f->arguments = strdup(args_j->valuestring);
                                            f->args_len = strlen(args_j->valuestring);
                                            f->args_cap = f->args_len + 1;
                                            f->complete = 1;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    /* Response completed - extract usage */
                    else if (strcmp(ev_type, "response.completed") == 0) {
                        cJSON *resp = cJSON_GetObjectItem(ev, "response");
                        if (resp) {
                            cJSON *id_item = cJSON_GetObjectItem(resp, "id");
                            if (id_item && cJSON_IsString(id_item))
                                strlcpy(resp_id, id_item->valuestring, sizeof(resp_id));
                            cJSON *mdl = cJSON_GetObjectItem(resp, "model");
                            if (mdl && cJSON_IsString(mdl))
                                strlcpy(model_str, mdl->valuestring, sizeof(model_str));
                            cJSON *usage = cJSON_GetObjectItem(resp, "usage");
                            if (usage) {
                                cJSON *it = cJSON_GetObjectItem(usage, "input_tokens");
                                cJSON *ot = cJSON_GetObjectItem(usage, "output_tokens");
                                if (it && cJSON_IsNumber(it)) input_tokens  = (long)it->valuedouble;
                                if (ot && cJSON_IsNumber(ot)) output_tokens = (long)ot->valuedouble;
                            }
                        }
                    } else if (strcmp(ev_type, "response.failed") == 0
                               || strcmp(ev_type, "error") == 0) {
                        got_error = 1;
                        cJSON *err = cJSON_GetObjectItem(ev, "error");
                        if (err) {
                            cJSON *msg = cJSON_GetObjectItem(err, "message");
                            if (msg && cJSON_IsString(msg))
                                strlcpy(error_msg, msg->valuestring, sizeof(error_msg));
                        }
                        if (!error_msg[0])
                            strlcpy(error_msg, "ChatGPT backend returned an error", sizeof(error_msg));
                    }
                    cJSON_Delete(ev);
                }
            }
        }

        p += line_len;
        if (*p == '\n') p++;   /* skip LF */
        if (*p == '\r') p++;   /* skip CR (CRLF) */
    }

    if (got_error) {
        free(text_buf);
        for (int i = 0; i < func_count; i++) {
            free(funcs[i].arguments);
        }
        free(funcs);
        /* Return an error-shaped JSON that the caller can detect */
        cJSON *err_json = cJSON_CreateObject();
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "message", error_msg);
        cJSON_AddItemToObject(err_json, "error", e);
        return err_json;
    }

    /* Synthesise a chat-completions response */
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "id",     resp_id);
    cJSON_AddStringToObject(out, "object", "chat.completion");
    cJSON_AddStringToObject(out, "model",  model_str);
    cJSON_AddNumberToObject(out, "created", (double)(long)time(NULL));

    cJSON *choices = cJSON_CreateArray();
    cJSON *choice  = cJSON_CreateObject();
    cJSON_AddNumberToObject(choice, "index", 0);
    cJSON *msg_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_obj, "role", "assistant");
    cJSON_AddStringToObject(msg_obj, "content", text_buf ? text_buf : "");
    cJSON_AddItemToObject(choice, "message", msg_obj);

    /* Add tool_calls if we have any function calls */
    if (func_count > 0) {
        cJSON *tool_calls = cJSON_CreateArray();
        for (int i = 0; i < func_count; i++) {
            cJSON *tc = cJSON_CreateObject();
            cJSON_AddStringToObject(tc, "id", funcs[i].call_id[0] ? funcs[i].call_id : funcs[i].id);
            cJSON_AddStringToObject(tc, "type", "function");
            cJSON *fn = cJSON_CreateObject();
            cJSON_AddStringToObject(fn, "name", funcs[i].name);
            cJSON_AddStringToObject(fn, "arguments", funcs[i].arguments ? funcs[i].arguments : "{}");
            cJSON_AddItemToObject(tc, "function", fn);
            cJSON_AddItemToArray(tool_calls, tc);
        }
        cJSON_AddItemToObject(msg_obj, "tool_calls", tool_calls);
        cJSON_AddStringToObject(choice, "finish_reason", "tool_calls");
    } else {
        cJSON_AddStringToObject(choice, "finish_reason", "stop");
    }

    cJSON_AddItemToArray(choices, choice);
    cJSON_AddItemToObject(out, "choices", choices);

    cJSON *usage_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(usage_obj, "prompt_tokens",     (double)input_tokens);
    cJSON_AddNumberToObject(usage_obj, "completion_tokens", (double)output_tokens);
    cJSON_AddNumberToObject(usage_obj, "total_tokens",
                            (double)(input_tokens + output_tokens));
    cJSON_AddItemToObject(out, "usage", usage_obj);

    free(text_buf);
    for (int i = 0; i < func_count; i++) {
        free(funcs[i].arguments);
    }
    free(funcs);
    return out;
}

/**
 * Build the JSON body for the ChatGPT backend Responses API.
 *
 * The chatgpt.com/backend-api/codex/responses endpoint requires:
 *   - model:        string
 *   - instructions: string  (from the first system message)
 *   - input:        array of {role, content} messages (user + assistant turns)
 *   - store:        false   (required)
 *   - stream:       true    (required)
 *
 * max_output_tokens is NOT supported and must be omitted.
 *
 * ChatGPT Backend API content types:
 *   - input_text:  user text input (includes tool results in text format)
 *   - input_image: user image input
 *   - output_text: assistant text output (for conversation history)
 *
 * IMPORTANT: The ChatGPT backend does NOT accept 'function_call' as an input
 * content type. Tool calls are tracked via stateful previous_response_id.
 */
static cJSON *build_chatgpt_backend_request(ConversationState *state,
                                             const char *model_override,
                                             const char *previous_response_id) {
    if (!state) return NULL;

    conversation_state_lock(state);

    const char *model = (model_override && model_override[0] != '\0')
                        ? model_override
                        : (state->model ? state->model : OPENAI_SUB_DEFAULT_MODEL);

    cJSON *req = cJSON_CreateObject();
    if (!req) { conversation_state_unlock(state); return NULL; }

    cJSON_AddStringToObject(req, "model", model);
    cJSON_AddFalseToObject(req,  "store");
    cJSON_AddTrueToObject(req,   "stream");

    /* Note: ChatGPT backend doesn't support previous_response_id.
     * We track it for future use but don't send it in the request. */
    (void)previous_response_id; /* unused for now */

    /* Collect system prompt as "instructions" and all other messages as "input" */
    cJSON *input = cJSON_CreateArray();
    if (!input) { cJSON_Delete(req); conversation_state_unlock(state); return NULL; }

    /* Send full conversation history */
    int start_idx = 0;

    for (int i = start_idx; i < state->count; i++) {
        InternalMessage *msg = &state->messages[i];

        if (msg->role == MSG_SYSTEM) {
            /* Use first system message as instructions */
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *c = &msg->contents[j];
                if (c->type == INTERNAL_TEXT && c->text && c->text[0] != '\0') {
                    /* Only set if not already present */
                    if (!cJSON_GetObjectItem(req, "instructions")) {
                        cJSON_AddStringToObject(req, "instructions", c->text);
                    }
                    break;
                }
            }
            continue;
        }

        /* user / assistant turns - build content array with proper types */
        const char *role_str = (msg->role == MSG_USER) ? "user" : "assistant";
        cJSON *content_array = cJSON_CreateArray();
        if (!content_array) continue;

        for (int j = 0; j < msg->content_count; j++) {
            InternalContent *c = &msg->contents[j];

            if (c->type == INTERNAL_TEXT && c->text && c->text[0] != '\0') {
                /* Text content - input_text for user, output_text for assistant */
                cJSON *text_obj = cJSON_CreateObject();
                if (!text_obj) continue;
                const char *type_str = (msg->role == MSG_USER) ? "input_text" : "output_text";
                cJSON_AddStringToObject(text_obj, "type", type_str);
                cJSON_AddStringToObject(text_obj, "text", c->text);
                cJSON_AddItemToArray(content_array, text_obj);
            }
            /* ChatGPT backend does NOT accept function_call as input type.
             * Tool calls are tracked via stateful previous_response_id.
             * We skip INTERNAL_TOOL_CALL for assistant role when building input.
             */
            else if (c->type == INTERNAL_TOOL_RESPONSE && msg->role == MSG_USER) {
                /* ChatGPT backend (codex endpoint) doesn't support function_call_output.
                 * Convert tool result to input_text with the result included. */
                cJSON *tool_result = cJSON_CreateObject();
                if (!tool_result) continue;
                cJSON_AddStringToObject(tool_result, "type", "input_text");

                /* Build a text representation of the tool result */
                char *output_str = cJSON_PrintUnformatted(c->tool_output);
                char result_text[2048];
                snprintf(result_text, sizeof(result_text),
                         "Tool result (id: %s): %s",
                         c->tool_id ? c->tool_id : "unknown",
                         output_str ? output_str : "{}");
                free(output_str);
                cJSON_AddStringToObject(tool_result, "text", result_text);
                cJSON_AddItemToArray(content_array, tool_result);
            }
        }

        /* Only add message if we have content */
        if (cJSON_GetArraySize(content_array) > 0) {
            cJSON *item = cJSON_CreateObject();
            if (item) {
                cJSON_AddStringToObject(item, "type", "message");
                cJSON_AddStringToObject(item, "role", role_str);
                cJSON_AddItemToObject(item, "content", content_array);
                cJSON_AddItemToArray(input, item);
            } else {
                cJSON_Delete(content_array);
            }
        } else {
            cJSON_Delete(content_array);
        }
    }

    cJSON_AddItemToObject(req, "input", input);

    /* Ensure instructions field exists */
    if (!cJSON_GetObjectItem(req, "instructions")) {
        cJSON_AddStringToObject(req, "instructions",
                                "You are a helpful coding assistant.");
    }

    /* Add tools with cache_control support (including MCP tools if available) */
    cJSON *tool_defs = get_openai_subscription_tool_definitions(0, TOOL_SCHEMA_RESPONSES);
    cJSON_AddItemToObject(req, "tools", tool_defs);

    conversation_state_unlock(state);
    return req;
}

static void openai_sub_call_api(Provider *self, ConversationState *state,
                                  ApiCallResult *out) {
    ApiCallResult result = {0};
    OpenAISubConfig *config = (OpenAISubConfig *)self->config;

    if (!config || !config->oauth_manager) {
        result.error_message = strdup("OpenAI sub: config or OAuth manager not initialized");
        result.is_retryable  = 0;
        *out = result;
        return;
    }

    /* Route OAuth messages to TUI */
    openai_oauth_set_message_callback(config->oauth_manager,
                                       openai_sub_oauth_message_callback, state);

    /* Get access token (refreshes if needed) */
    const char *access_token = openai_oauth_get_access_token(config->oauth_manager);
    if (!access_token) {
        LOG_INFO("OpenAI Sub: Not authenticated, attempting login...");
        if (openai_oauth_login(config->oauth_manager) != 0) {
            result.error_message = strdup("OpenAI subscription login failed. Please try again.");
            result.is_retryable  = 0;
            *out = result;
            return;
        }
        access_token = openai_oauth_get_access_token(config->oauth_manager);
        if (!access_token) {
            result.error_message = strdup("Failed to get access token after login");
            result.is_retryable  = 0;
            *out = result;
            return;
        }
    }

    /* Check if streaming is enabled */
    int enable_streaming = 0;
    const char *streaming_env = getenv("KLAWED_ENABLE_STREAMING");
    if (streaming_env &&
        (strcmp(streaming_env, "1") == 0 || strcasecmp(streaming_env, "true") == 0)) {
        enable_streaming = 1;
    }

    /* Determine URL */
    const char *url = (config->api_base && config->api_base[0] != '\0')
                      ? config->api_base
                      : OPENAI_SUB_DEFAULT_API_BASE;

    /* Detect whether we're targeting the chatgpt backend (Responses API) or
     * a custom endpoint (chat completions).  The chatgpt backend requires:
     *   - Responses API JSON format (store=false, stream=true, instructions)
     *   - ChatGPT-Account-Id header
     * A custom api_base falls back to chat completions format. */
    int use_responses_api = (strstr(url, "chatgpt.com") != NULL);

    char *request_json = NULL;
    HttpRequest req = {0};

    if (use_responses_api) {
        /* Resolve account_id for the required ChatGPT-Account-Id header */
        const char *account_id = openai_oauth_get_account_id(config->oauth_manager);

        /* Build the ChatGPT backend Responses API request JSON directly.
         * The chatgpt backend differs from api.openai.com/v1/responses:
         *   - store must be false
         *   - stream must be true
         *   - max_output_tokens is not supported (omit it)
         *   - model + instructions + input[] format
         *   - previous_response_id for stateful conversation tracking          */
        cJSON *body = build_chatgpt_backend_request(state, config->model,
                                                     config->previous_response_id);
        if (!body) {
            result.error_message = strdup("OpenAI sub: failed to build chatgpt backend request");
            result.is_retryable  = 0;
            *out = result;
            return;
        }
        request_json = cJSON_PrintUnformatted(body);
        cJSON_Delete(body);
        if (!request_json) {
            result.error_message = strdup("OpenAI sub: failed to serialise request JSON");
            result.is_retryable  = 0;
            *out = result;
            return;
        }

        /* Build headers */
        struct curl_slist *hdrs = NULL;
        hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
        char auth_hdr[2048];
        snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", access_token);
        hdrs = curl_slist_append(hdrs, auth_hdr);
        if (account_id && account_id[0] != '\0') {
            char acct_hdr[256];
            snprintf(acct_hdr, sizeof(acct_hdr), "ChatGPT-Account-Id: %s", account_id);
            hdrs = curl_slist_append(hdrs, acct_hdr);
        }

        req.url                = url;
        req.method             = "POST";
        req.body               = request_json;
        req.headers            = hdrs;
        req.connect_timeout_ms = 30000;
        req.total_timeout_ms   = 300000;
        req.enable_streaming   = 0; /* we'll read the full SSE body ourselves */
    } else {
        /* Fallback: chat completions format for custom api_base */
        cJSON *request = build_openai_request_with_reasoning(state, 0, 0);
        if (!request) {
            result.error_message = strdup("Failed to build request JSON");
            result.is_retryable  = 0;
            *out = result;
            return;
        }
        if (config->model && config->model[0] != '\0') {
            cJSON *m = cJSON_GetObjectItem(request, "model");
            if (m) cJSON_ReplaceItemInObject(request, "model",
                                              cJSON_CreateString(config->model));
            else    cJSON_AddStringToObject(request, "model", config->model);
        }
        if (enable_streaming)
            cJSON_AddBoolToObject(request, "stream", cJSON_True);

        request_json = cJSON_PrintUnformatted(request);
        cJSON_Delete(request);
        if (!request_json) {
            result.error_message = strdup("Failed to serialize request JSON");
            result.is_retryable  = 0;
            *out = result;
            return;
        }

        /* Build headers manually for chat completions path */
        struct curl_slist *hdrs = NULL;
        hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
        char auth_hdr[2048];
        snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", access_token);
        hdrs = curl_slist_append(hdrs, auth_hdr);
        if (!hdrs) {
            result.error_message = strdup("Failed to setup HTTP headers");
            result.is_retryable  = 0;
            free(request_json);
            *out = result;
            return;
        }
        req.url                = url;
        req.method             = "POST";
        req.body               = request_json;
        req.headers            = hdrs;
        req.connect_timeout_ms = 30000;
        req.total_timeout_ms   = 300000;
        req.enable_streaming   = enable_streaming;
    }

    result.request_json = strdup(request_json ? request_json : "");
    result.headers_json = http_headers_to_json(req.headers);
    LOG_DEBUG("OpenAI Sub: sending body: %.500s", request_json ? request_json : "(null)");

    SubStreamingContext stream_ctx = {0};
    HttpResponse *http_resp = NULL;

    if (enable_streaming && !use_responses_api) {
        sub_streaming_context_init(&stream_ctx, state);
        http_resp = http_client_execute_stream(&req, sub_streaming_event_handler,
                                               &stream_ctx, progress_callback, state);
    } else {
        http_resp = http_client_execute(&req, progress_callback, state);
    }

    curl_slist_free_all(req.headers);
    free(request_json);
    req.body = NULL;

    if (!http_resp) {
        result.error_message = strdup("Failed to execute HTTP request");
        result.is_retryable  = 0;
        if (enable_streaming) sub_streaming_context_free(&stream_ctx);
        *out = result;
        return;
    }

    result.duration_ms  = http_resp->duration_ms;
    result.http_status  = http_resp->status_code;
    result.raw_response = http_resp->body ? strdup(http_resp->body) : NULL;
    if (result.headers_json) { free(result.headers_json); result.headers_json = NULL; }
    result.headers_json = http_headers_to_json(http_resp->headers);

    if (http_resp->error_message) {
        result.error_message = strdup(http_resp->error_message);
        result.is_retryable  = http_resp->is_retryable;
        http_response_free(http_resp);
        if (enable_streaming) sub_streaming_context_free(&stream_ctx);
        *out = result;
        return;
    }

    char *body_to_free = http_resp->body;
    http_resp->body = NULL;
    http_response_free(http_resp);
    free(body_to_free);

    /* Handle 401 - token may have been revoked or rotated */
    if (result.http_status == 401) {
        LOG_INFO("OpenAI Sub: 401 received, attempting token recovery...");

        /*
         * Strategy:
         *  1. Try an immediate disk reload — another process may have already
         *     written a fresh token.
         *  2. If not found, use openai_oauth_wait_for_refresh():
         *       - If a refresh is in progress (lock is held), block until it
         *         finishes, then reload from disk.  No network call needed.
         *       - If no refresh is in progress, do a final disk check.
         *  3. If still no fresh token, fall back to a local network refresh
         *     (openai_oauth_refresh acquires the lock itself, so concurrent
         *     callers will wait rather than all race to refresh).
         */
        int reloaded = openai_oauth_reload_from_disk(config->oauth_manager);
        if (reloaded) {
            LOG_INFO("OpenAI Sub: Reloaded updated token from disk");
            result.is_retryable = 1;
        } else {
            /* Wait for any in-progress cross-process refresh, then re-check disk */
            reloaded = openai_oauth_wait_for_refresh(config->oauth_manager);
            if (reloaded) {
                LOG_INFO("OpenAI Sub: Token updated by concurrent refresh");
                result.is_retryable = 1;
            } else {
                LOG_INFO("OpenAI Sub: No concurrent refresh found, attempting own refresh...");
                if (openai_oauth_refresh(config->oauth_manager, 1) == 0) {
                    LOG_INFO("OpenAI Sub: Token refreshed successfully");
                    result.is_retryable = 1;
                } else {
                    LOG_ERROR("OpenAI Sub: Token refresh failed, clearing credentials");
                    openai_oauth_logout(config->oauth_manager);
                    free(result.error_message);
                    result.error_message = strdup(
                        "OpenAI OAuth token expired or revoked. "
                        "Please run again to re-authenticate.");
                    result.is_retryable = 0;
                }
            }
        }

        if (enable_streaming) sub_streaming_context_free(&stream_ctx);
        *out = result;
        return;
    }

    /* Handle other HTTP errors */
    if (result.http_status < 200 || result.http_status >= 300) {
        LOG_WARN("OpenAI Sub: HTTP %ld body: %.500s",
                 result.http_status,
                 result.raw_response ? result.raw_response : "(empty)");
        if (!result.error_message) {
            char buf[64];
            snprintf(buf, sizeof(buf), "HTTP %ld", result.http_status);
            result.error_message = strdup(buf);
        }
        result.is_retryable = (result.http_status == 429 ||
                                result.http_status == 500 ||
                                result.http_status == 502 ||
                                result.http_status == 503 ||
                                result.http_status == 504);
        if (enable_streaming) sub_streaming_context_free(&stream_ctx);
        *out = result;
        return;
    }

    /* Parse successful response */
    cJSON *raw_json = NULL;

    if (use_responses_api && result.raw_response) {
        /* The chatgpt backend returns SSE even in non-streaming HTTP mode.
         * Walk the SSE lines and accumulate text/tool deltas, then synthesise
         * a chat-completions-shaped JSON so the rest of the pipeline is happy. */
        raw_json = parse_chatgpt_sse_response(result.raw_response);
        if (!raw_json) {
            result.error_message = strdup("Failed to parse ChatGPT SSE response");
            result.is_retryable  = 1;
            *out = result;
            return;
        }
    } else if (enable_streaming) {
        /* Reconstruct response from streaming context */
        raw_json = cJSON_CreateObject();
        cJSON_AddStringToObject(raw_json, "id",
                                stream_ctx.message_id ? stream_ctx.message_id : "streaming");
        cJSON_AddStringToObject(raw_json, "object", "chat.completion");
        cJSON_AddStringToObject(raw_json, "model",
                                stream_ctx.model ? stream_ctx.model : "unknown");
        time_t ts_now = time(NULL);
        cJSON_AddNumberToObject(raw_json, "created", (double)(long)ts_now);

        cJSON *choices = cJSON_CreateArray();
        cJSON *choice  = cJSON_CreateObject();
        cJSON_AddNumberToObject(choice, "index", 0);

        cJSON *message = cJSON_CreateObject();
        cJSON_AddStringToObject(message, "role", "assistant");
        if (stream_ctx.accumulated_text && stream_ctx.accumulated_size > 0) {
            cJSON_AddStringToObject(message, "content", stream_ctx.accumulated_text);
        } else {
            cJSON_AddNullToObject(message, "content");
        }
        if (stream_ctx.accumulated_reasoning && stream_ctx.reasoning_size > 0) {
            cJSON_AddStringToObject(message, "reasoning_content",
                                    stream_ctx.accumulated_reasoning);
        }
        if (stream_ctx.tool_calls_count > 0) {
            cJSON_AddItemToObject(message, "tool_calls",
                                  cJSON_Duplicate(stream_ctx.tool_calls_array, 1));
        }
        cJSON_AddItemToObject(choice, "message", message);
        cJSON_AddStringToObject(choice, "finish_reason",
                                stream_ctx.finish_reason ? stream_ctx.finish_reason : "stop");
        cJSON_AddItemToArray(choices, choice);
        cJSON_AddItemToObject(raw_json, "choices", choices);

        cJSON *usage = cJSON_CreateObject();
        cJSON_AddNumberToObject(usage, "prompt_tokens", 0);
        cJSON_AddNumberToObject(usage, "completion_tokens", 0);
        cJSON_AddNumberToObject(usage, "total_tokens", 0);
        cJSON_AddItemToObject(raw_json, "usage", usage);

        sub_streaming_context_free(&stream_ctx);
    } else {
        raw_json = cJSON_Parse(result.raw_response);
        if (!raw_json) {
            result.error_message = strdup("Failed to parse JSON response");
            result.is_retryable  = 1;
            *out = result;
            return;
        }
    }

    ApiResponse *api_response = parse_response(raw_json);
    /* raw_json is now owned by api_response->raw_response; do NOT cJSON_Delete it */

    if (!api_response) {
        result.error_message = strdup("Failed to parse API response");
        result.is_retryable  = 0;
        *out = result;
        return;
    }

    /* Extract response ID for stateful conversation tracking (ChatGPT backend) */
    if (use_responses_api) {
        cJSON *id_item = cJSON_GetObjectItem(raw_json, "id");
        if (id_item && cJSON_IsString(id_item) && id_item->valuestring) {
            free(config->previous_response_id);
            config->previous_response_id = strdup(id_item->valuestring);
            LOG_DEBUG("OpenAI Sub: saved response_id=%s for stateful conversation",
                      config->previous_response_id);
        }
    }

    result.response    = api_response;
    result.is_retryable = 0;
    *out = result;
}

/* ============================================================================
 * Provider Cleanup
 * ============================================================================ */

static void openai_sub_cleanup(Provider *self) {
    if (!self) return;

    if (self->config) {
        OpenAISubConfig *config = (OpenAISubConfig *)self->config;
        if (config->oauth_manager) {
            openai_oauth_stop_refresh_thread(config->oauth_manager);
            openai_oauth_manager_destroy(config->oauth_manager);
        }
        free(config->api_base);
        free(config->model);
        free(config->previous_response_id);
        free(config);
    }
    free(self);
    LOG_DEBUG("OpenAI Sub provider: cleanup complete");
}

/* ============================================================================
 * Public API
 * ============================================================================ */

Provider *openai_sub_provider_create(const char *model, const char *api_base) {
    LOG_DEBUG("Creating OpenAI subscription provider...");

    Provider *provider = calloc(1, sizeof(Provider));
    if (!provider) {
        LOG_ERROR("OpenAI Sub: failed to allocate provider");
        return NULL;
    }

    OpenAISubConfig *config = calloc(1, sizeof(OpenAISubConfig));
    if (!config) {
        LOG_ERROR("OpenAI Sub: failed to allocate config");
        free(provider);
        return NULL;
    }

    /* Create OAuth manager */
    config->oauth_manager = openai_oauth_manager_create();
    if (!config->oauth_manager) {
        LOG_ERROR("OpenAI Sub: failed to create OAuth manager");
        free(config);
        free(provider);
        return NULL;
    }

    /* Authenticate if needed */
    if (!openai_oauth_is_authenticated(config->oauth_manager)) {
        LOG_INFO("OpenAI Sub: Not authenticated, starting device authorization...");
        if (openai_oauth_login(config->oauth_manager) != 0) {
            LOG_ERROR("OpenAI Sub: OAuth login failed");
            openai_oauth_manager_destroy(config->oauth_manager);
            free(config);
            free(provider);
            return NULL;
        }
    }

    /* Start background refresh thread */
    if (openai_oauth_start_refresh_thread(config->oauth_manager) != 0) {
        LOG_WARN("OpenAI Sub: failed to start refresh thread (will refresh manually)");
    }

    /* Set API base URL */
    const char *base = (api_base && api_base[0] != '\0')
                       ? api_base
                       : OPENAI_SUB_DEFAULT_API_BASE;
    config->api_base = strdup(base);
    if (!config->api_base) {
        LOG_ERROR("OpenAI Sub: failed to set API base URL");
        openai_oauth_stop_refresh_thread(config->oauth_manager);
        openai_oauth_manager_destroy(config->oauth_manager);
        free(config);
        free(provider);
        return NULL;
    }

    /* Set model */
    const char *mdl = (model && model[0] != '\0') ? model : OPENAI_SUB_DEFAULT_MODEL;
    config->model = strdup(mdl);
    if (!config->model) {
        LOG_ERROR("OpenAI Sub: failed to set model");
        free(config->api_base);
        openai_oauth_stop_refresh_thread(config->oauth_manager);
        openai_oauth_manager_destroy(config->oauth_manager);
        free(config);
        free(provider);
        return NULL;
    }

    /* Wire up provider interface */
    provider->name     = "OpenAI Subscription";
    provider->config   = config;
    provider->call_api = openai_sub_call_api;
    provider->cleanup  = openai_sub_cleanup;

    LOG_INFO("OpenAI Sub provider created (model=%s, api_base=%s)",
             config->model, config->api_base);
    return provider;
}
