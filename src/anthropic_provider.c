/*
 * anthropic_provider.c - Anthropic Messages API provider implementation
 */

#define _POSIX_C_SOURCE 200809L

#include "klawed_internal.h"  // Must be first to get ApiResponse definition
#include "anthropic_provider.h"
#include "openai_messages.h"  // We reuse internal message building and parse into OpenAI-like intermediate
#include "logger.h"
#include "http_client.h"
#include "tui.h"  // For streaming TUI updates
#include "message_queue.h"  // For thread-safe streaming updates
#include "util/string_utils.h" // For trim_whitespace

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <errno.h>
#include <curl/curl.h>
#include <bsd/string.h>
// Socket support removed - will be reimplemented with ZMQ
#include "retry_logic.h"  // For common retry logic

#define DEFAULT_ANTHROPIC_URL "https://api.anthropic.com/v1/messages"
#define ANTHROPIC_VERSION_HEADER "anthropic-version: 2023-06-01"

// ============================================================================
// CURL Helpers
// ============================================================================



static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;

    // clientp is the ConversationState* passed via progress_data parameter
    ConversationState *state = (ConversationState *)clientp;
    if (state && state->interrupt_requested) {
        LOG_DEBUG("Progress callback: interrupt requested, aborting HTTP request");
        return 1;  // Non-zero return aborts the curl transfer
    }

    return 0;
}

/**
 * Duplicate a string using arena allocation
 * Returns: Newly allocated string from arena, or NULL on error
 */
static char* arena_strdup(Arena *arena, const char *str) {
    if (!str || !arena) return NULL;

    size_t len = strlen(str) + 1;  // +1 for null terminator
    char *new_str = arena_alloc(arena, len);
    if (!new_str) return NULL;

    strlcpy(new_str, str, len);
    return new_str;
}

// Convert curl_slist headers to JSON string for logging
// Note: Using http_headers_to_json from http_client.h instead

// ============================================================================
// Anthropic Request/Response Conversion
// ============================================================================

// Convert OpenAI-style request (our internal builder outputs) to Anthropic native
char* anthropic_convert_openai_to_anthropic_request(const char *openai_req) {
    cJSON *openai_json = cJSON_Parse(openai_req);
    if (!openai_json) return NULL;

    cJSON *messages = cJSON_GetObjectItem(openai_json, "messages");
    cJSON *tools = cJSON_GetObjectItem(openai_json, "tools");
    cJSON *max_tokens = cJSON_GetObjectItem(openai_json, "max_completion_tokens");
    cJSON *model = cJSON_GetObjectItem(openai_json, "model");

    cJSON *anth = cJSON_CreateObject();

    // Required fields
    if (model && cJSON_IsString(model)) {
        cJSON_AddStringToObject(anth, "model", model->valuestring);
    }
    cJSON_AddNumberToObject(anth, "max_tokens", (max_tokens && cJSON_IsNumber(max_tokens)) ? max_tokens->valueint : MAX_TOKENS);

    // Separate system and content messages
    cJSON *anth_msgs = cJSON_CreateArray();
    // Preserve system as content array if provided (to keep cache_control)
    cJSON *system_blocks = NULL;   // array of content blocks
    cJSON *system_string = NULL;   // fallback plain string

    if (messages && cJSON_IsArray(messages)) {
        cJSON *m = NULL;
        cJSON_ArrayForEach(m, messages) {
            cJSON *role = cJSON_GetObjectItem(m, "role");
            cJSON *content = cJSON_GetObjectItem(m, "content");
            if (!role || !cJSON_IsString(role)) continue;
            const char *r = role->valuestring;
            if (strcmp(r, "system") == 0) {
                if (cJSON_IsArray(content)) {
                    // Duplicate as-is to preserve any cache_control markers
                    system_blocks = cJSON_Duplicate(content, 1);
                } else if (cJSON_IsString(content)) {
                    system_string = cJSON_CreateString(content->valuestring);
                }
                continue;
            }

            cJSON *anth_m = cJSON_CreateObject();
            if (strcmp(r, "assistant") == 0) {
                cJSON_AddStringToObject(anth_m, "role", "assistant");
                cJSON *tool_calls = cJSON_GetObjectItem(m, "tool_calls");
                if (tool_calls && cJSON_IsArray(tool_calls)) {
                    cJSON *content_arr = cJSON_CreateArray();
                    if (cJSON_IsString(content) && content->valuestring && content->valuestring[0]) {
                        cJSON *tb = cJSON_CreateObject();
                        cJSON_AddStringToObject(tb, "type", "text");
                        cJSON_AddStringToObject(tb, "text", content->valuestring);
                        cJSON_AddItemToArray(content_arr, tb);
                    }
                    cJSON *tc = NULL;
                    cJSON_ArrayForEach(tc, tool_calls) {
                        cJSON *tb = cJSON_CreateObject();
                        cJSON_AddStringToObject(tb, "type", "tool_use");
                        cJSON *id = cJSON_GetObjectItem(tc, "id");
                        if (id && cJSON_IsString(id)) cJSON_AddStringToObject(tb, "id", id->valuestring);
                        cJSON *fn = cJSON_GetObjectItem(tc, "function");
                        if (fn) {
                            cJSON *name = cJSON_GetObjectItem(fn, "name");
                            cJSON *args = cJSON_GetObjectItem(fn, "arguments");
                            if (name && cJSON_IsString(name)) cJSON_AddStringToObject(tb, "name", name->valuestring);
                            if (args && cJSON_IsString(args)) {
                                cJSON *input = cJSON_Parse(args->valuestring);
                                if (!input) input = cJSON_CreateObject();
                                cJSON_AddItemToObject(tb, "input", input);
                            }
                        }
                        cJSON_AddItemToArray(content_arr, tb);
                    }
                    if (cJSON_GetArraySize(content_arr) > 0) {
                        cJSON_AddItemToObject(anth_m, "content", content_arr);
                    } else {
                        cJSON_Delete(content_arr);
                        cJSON_Delete(anth_m);
                        anth_m = NULL;
                    }
                } else {
                    if (cJSON_IsString(content) && content->valuestring && content->valuestring[0]) {
                        cJSON_AddStringToObject(anth_m, "content", content->valuestring);
                    } else {
                        cJSON_Delete(anth_m);
                        anth_m = NULL;
                    }
                }
            } else if (strcmp(r, "user") == 0) {
                cJSON_AddStringToObject(anth_m, "role", "user");
                if (cJSON_IsString(content)) {
                    cJSON_AddStringToObject(anth_m, "content", content->valuestring);
                } else if (cJSON_IsArray(content)) {
                    // Validate and filter content blocks - remove empty text blocks
                    cJSON *filtered_content = cJSON_CreateArray();
                    cJSON *block = NULL;
                    cJSON_ArrayForEach(block, content) {
                        cJSON *type = cJSON_GetObjectItem(block, "type");
                        if (type && cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0) {
                            cJSON *text = cJSON_GetObjectItem(block, "text");
                            // Skip text blocks with empty or missing text
                            if (!text || !cJSON_IsString(text) || !text->valuestring || !text->valuestring[0]) {
                                LOG_DEBUG("Skipping empty text block in user content array");
                                continue;
                            }
                        }
                        cJSON_AddItemToArray(filtered_content, cJSON_Duplicate(block, 1));
                    }
                    if (cJSON_GetArraySize(filtered_content) > 0) {
                        cJSON_AddItemToObject(anth_m, "content", filtered_content);
                    } else {
                        cJSON_Delete(filtered_content);
                        // No valid content - skip this message
                        cJSON_Delete(anth_m);
                        anth_m = NULL;
                    }
                }
            } else if (strcmp(r, "tool") == 0) {
                cJSON *tool_call_id = cJSON_GetObjectItem(m, "tool_call_id");
                if (tool_call_id && cJSON_IsString(tool_call_id)) {
                    cJSON *content_arr = cJSON_CreateArray();
                    cJSON *tr = cJSON_CreateObject();
                    cJSON_AddStringToObject(tr, "type", "tool_result");
                    cJSON_AddStringToObject(tr, "tool_use_id", tool_call_id->valuestring);
                    if (cJSON_IsString(content)) {
                        cJSON_AddStringToObject(tr, "content", content->valuestring);
                    } else {
                        char *s = cJSON_PrintUnformatted(content);
                        cJSON_AddStringToObject(tr, "content", s ? s : "");
                        free(s);
                    }
                    cJSON_AddItemToArray(content_arr, tr);
                    cJSON_AddItemToObject(anth_m, "content", content_arr);
                }
                // Role remains user for tool results in Anthropic
                cJSON_AddStringToObject(anth_m, "role", "user");
            }

            if (anth_m && cJSON_GetObjectItem(anth_m, "role")) {
                cJSON_AddItemToArray(anth_msgs, anth_m);
            } else if (anth_m) {
                cJSON_Delete(anth_m);
            }
        }
    }

    cJSON_AddItemToObject(anth, "messages", anth_msgs);
    if (system_blocks) {
        // Anthropic supports system as either string or array of content blocks
        cJSON_AddItemToObject(anth, "system", system_blocks);
    } else if (system_string) {
        cJSON_AddItemToObject(anth, "system", system_string);
    }

    // Tools
    if (tools && cJSON_IsArray(tools)) {
        cJSON *anth_tools = cJSON_CreateArray();
        cJSON *t = NULL;
        cJSON_ArrayForEach(t, tools) {
            cJSON *fn = cJSON_GetObjectItem(t, "function");
            if (!fn) continue;
            cJSON *obj = cJSON_CreateObject();
            cJSON *name = cJSON_GetObjectItem(fn, "name");
            cJSON *desc = cJSON_GetObjectItem(fn, "description");
            cJSON *params = cJSON_GetObjectItem(fn, "parameters");
            if (name && cJSON_IsString(name)) cJSON_AddStringToObject(obj, "name", name->valuestring);
            if (desc && cJSON_IsString(desc)) cJSON_AddStringToObject(obj, "description", desc->valuestring);
            if (params) cJSON_AddItemToObject(obj, "input_schema", cJSON_Duplicate(params, 1));
            // Preserve cache_control on tool definitions to create checkpoint after tools
            cJSON *cache_ctrl = cJSON_GetObjectItem(t, "cache_control");
            if (cache_ctrl) {
                cJSON_AddItemToObject(obj, "cache_control", cJSON_Duplicate(cache_ctrl, 1));
            }
            cJSON_AddItemToArray(anth_tools, obj);
        }
        if (cJSON_GetArraySize(anth_tools) > 0) {
            cJSON_AddItemToObject(anth, "tools", anth_tools);
        } else {
            cJSON_Delete(anth_tools);
        }
    }

    // Version header is sent via HTTP header, not body. But some SDKs include anthropic_version
    const char *version_env = getenv("ANTHROPIC_VERSION");
    if (version_env && version_env[0]) {
        cJSON_AddStringToObject(anth, "anthropic_version", version_env);
    }

    char *out = cJSON_PrintUnformatted(anth);
    cJSON_Delete(openai_json);
    cJSON_Delete(anth);
    return out;
}

// Convert Anthropic JSON back to an OpenAI-like response so we can reuse parse code paths
cJSON* anthropic_convert_response_to_openai(const char *anthropic_raw) {
    cJSON *anth = cJSON_Parse(anthropic_raw);
    if (!anth) return NULL;

    cJSON *openai = cJSON_CreateObject();
    cJSON *choices = cJSON_CreateArray();
    cJSON *choice = cJSON_CreateObject();
    cJSON *message = cJSON_CreateObject();

    // Text content
    const char *text_out = NULL;
    cJSON *content = cJSON_GetObjectItem(anth, "content");
    if (cJSON_IsArray(content)) {
        // Find first text block
        cJSON *blk = NULL;
        cJSON_ArrayForEach(blk, content) {
            cJSON *type = cJSON_GetObjectItem(blk, "type");
            if (type && cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0) {
                cJSON *text = cJSON_GetObjectItem(blk, "text");
                if (text && cJSON_IsString(text)) {
                    text_out = text->valuestring;
                    break;
                }
            }
        }
    } else {
        cJSON *text = cJSON_GetObjectItem(anth, "content");
        if (cJSON_IsString(text)) text_out = text->valuestring;
    }

    if (text_out) cJSON_AddStringToObject(message, "content", text_out);
    else cJSON_AddNullToObject(message, "content");

    // Tool calls -> tool_calls array
    cJSON *tool_calls = NULL;
    if (cJSON_IsArray(content)) {
        cJSON *blk = NULL;
        cJSON_ArrayForEach(blk, content) {
            cJSON *type = cJSON_GetObjectItem(blk, "type");
            if (!type || !cJSON_IsString(type)) continue;
            if (strcmp(type->valuestring, "tool_use") == 0) {
                if (!tool_calls) tool_calls = cJSON_CreateArray();
                cJSON *tc = cJSON_CreateObject();
                cJSON_AddStringToObject(tc, "type", "function");
                cJSON *id = cJSON_GetObjectItem(blk, "id");
                if (id && cJSON_IsString(id)) cJSON_AddStringToObject(tc, "id", id->valuestring);
                cJSON *name = cJSON_GetObjectItem(blk, "name");
                cJSON *input = cJSON_GetObjectItem(blk, "input");
                cJSON *fn = cJSON_CreateObject();
                if (name && cJSON_IsString(name)) cJSON_AddStringToObject(fn, "name", name->valuestring);
                char *args_str = input ? cJSON_PrintUnformatted(input) : strdup("{}");
                if (args_str) {
                    cJSON_AddStringToObject(fn, "arguments", args_str);
                    free(args_str);
                }
                cJSON_AddItemToObject(tc, "function", fn);
                cJSON_AddItemToArray(tool_calls, tc);
            }
        }
    }

    if (tool_calls) cJSON_AddItemToObject(message, "tool_calls", tool_calls);

    cJSON_AddItemToObject(choice, "message", message);
    cJSON_AddItemToArray(choices, choice);
    cJSON_AddItemToObject(openai, "choices", choices);

    // Optional: usage -> convert if present
    cJSON *usage = cJSON_GetObjectItem(anth, "usage");
    if (usage) {
        cJSON *openai_usage = cJSON_CreateObject();
        cJSON *input_tokens = cJSON_GetObjectItem(usage, "input_tokens");
        cJSON *output_tokens = cJSON_GetObjectItem(usage, "output_tokens");
        if (cJSON_IsNumber(input_tokens)) cJSON_AddNumberToObject(openai_usage, "prompt_tokens", input_tokens->valuedouble);
        if (cJSON_IsNumber(output_tokens)) cJSON_AddNumberToObject(openai_usage, "completion_tokens", output_tokens->valuedouble);

        // Preserve cache-related fields for token usage tracking
        // Anthropic-style: cache_read_input_tokens
        cJSON *cache_read_input_tokens = cJSON_GetObjectItem(usage, "cache_read_input_tokens");
        if (cache_read_input_tokens && cJSON_IsNumber(cache_read_input_tokens)) {
            cJSON_AddNumberToObject(openai_usage, "cache_read_input_tokens", cache_read_input_tokens->valuedouble);
        }

        // DeepSeek/Moonshot-style: cached_tokens
        cJSON *cached_tokens = cJSON_GetObjectItem(usage, "cached_tokens");
        if (cached_tokens && cJSON_IsNumber(cached_tokens)) {
            cJSON_AddNumberToObject(openai_usage, "cached_tokens", cached_tokens->valuedouble);
        }

        // DeepSeek-style: prompt_cache_hit_tokens and prompt_cache_miss_tokens
        cJSON *prompt_cache_hit_tokens = cJSON_GetObjectItem(usage, "prompt_cache_hit_tokens");
        cJSON *prompt_cache_miss_tokens = cJSON_GetObjectItem(usage, "prompt_cache_miss_tokens");

        if (prompt_cache_hit_tokens && cJSON_IsNumber(prompt_cache_hit_tokens)) {
            cJSON_AddNumberToObject(openai_usage, "prompt_cache_hit_tokens", prompt_cache_hit_tokens->valuedouble);
        }

        if (prompt_cache_miss_tokens && cJSON_IsNumber(prompt_cache_miss_tokens)) {
            cJSON_AddNumberToObject(openai_usage, "prompt_cache_miss_tokens", prompt_cache_miss_tokens->valuedouble);
        }

        // DeepSeek-style: prompt_tokens_details with cached_tokens inside
        cJSON *prompt_tokens_details = cJSON_GetObjectItem(usage, "prompt_tokens_details");
        if (prompt_tokens_details) {
            cJSON *prompt_tokens_details_copy = cJSON_Duplicate(prompt_tokens_details, 1);
            if (prompt_tokens_details_copy) {
                cJSON_AddItemToObject(openai_usage, "prompt_tokens_details", prompt_tokens_details_copy);
            }
        }

        cJSON_AddItemToObject(openai, "usage", openai_usage);
    }

    cJSON_Delete(anth);
    return openai;
}

// ============================================================================
// Streaming Support
// ============================================================================

/* StreamingContext is exported as AnthropicStreamingContext via anthropic_provider.h */
typedef AnthropicStreamingContext StreamingContext;

void anthropic_streaming_context_init(AnthropicStreamingContext *ctx, ConversationState *state) {
    memset(ctx, 0, sizeof(StreamingContext));
    ctx->state = state;
    ctx->content_block_index = -1;
    ctx->accumulated_capacity = 4096;

    // Create arena for streaming context allocations
    ctx->arena = arena_create(65536);  // 64KB arena for streaming context
    if (ctx->arena) {
        // Use arena allocation
        ctx->accumulated_text = arena_alloc(ctx->arena, ctx->accumulated_capacity);
        if (ctx->accumulated_text) {
            ctx->accumulated_text[0] = '\0';
        }
        ctx->tool_input_capacity = 4096;
        ctx->tool_input_json = arena_alloc(ctx->arena, ctx->tool_input_capacity);
        if (ctx->tool_input_json) {
            ctx->tool_input_json[0] = '\0';
        }
    } else {
        // Fallback to heap allocation
        ctx->accumulated_text = malloc(ctx->accumulated_capacity);
        if (ctx->accumulated_text) {
            ctx->accumulated_text[0] = '\0';
        }
        ctx->tool_input_capacity = 4096;
        ctx->tool_input_json = malloc(ctx->tool_input_capacity);
        if (ctx->tool_input_json) {
            ctx->tool_input_json[0] = '\0';
        }
    }
}

void anthropic_streaming_context_free(AnthropicStreamingContext *ctx) {
    if (!ctx) return;

    // If arena is present, destroy it (frees all arena-allocated memory)
    if (ctx->arena) {
        arena_destroy(ctx->arena);
        return;
    }

    // Fallback to individual free calls
    free(ctx->accumulated_text);
    free(ctx->content_block_type);
    free(ctx->tool_use_id);
    free(ctx->tool_use_name);
    free(ctx->tool_input_json);
    free(ctx->stop_reason);
    if (ctx->message_start_data) {
        cJSON_Delete(ctx->message_start_data);
    }
}

int anthropic_streaming_event_handler(StreamEvent *event, void *userdata) {
    StreamingContext *ctx = (StreamingContext *)userdata;

    // Check for interrupt
    if (ctx->state && ctx->state->interrupt_requested) {
        LOG_DEBUG("Streaming handler: interrupt requested");
        return 1;  // Abort stream
    }

    if (!event || !event->data) {
        // Ping or invalid event
        return 0;
    }

    // Socket streaming support removed - will be reimplemented with ZMQ

    switch (event->type) {
        case SSE_EVENT_MESSAGE_START:
            // Store message metadata (model, usage, etc.)
            if (ctx->message_start_data) {
                cJSON_Delete(ctx->message_start_data);
            }
            ctx->message_start_data = cJSON_Duplicate(event->data, 1);
            LOG_DEBUG("Stream: message_start");

            // Initialize TUI for streaming by adding an empty assistant line
            if (ctx->state) {
                if (ctx->state->tui_queue) {
                    post_tui_stream_start(ctx->state->tui_queue, "[Assistant]", COLOR_PAIR_ASSISTANT);
                } else if (ctx->state->tui) {
                    // Add assistant prefix with empty text - streaming will fill it in
                    tui_add_conversation_line(ctx->state->tui, "[Assistant]", "", COLOR_PAIR_ASSISTANT);
                }
            }

            break;

        case SSE_EVENT_CONTENT_BLOCK_START: {
            // New content block starting
            cJSON *index = cJSON_GetObjectItem(event->data, "index");
            cJSON *content_block = cJSON_GetObjectItem(event->data, "content_block");
            if (index && cJSON_IsNumber(index)) {
                ctx->content_block_index = index->valueint;
            }
            if (content_block) {
                cJSON *type = cJSON_GetObjectItem(content_block, "type");
                if (type && cJSON_IsString(type)) {
                    free(ctx->content_block_type);
                    ctx->content_block_type = ctx->arena ? arena_strdup(ctx->arena, type->valuestring) : strdup(type->valuestring);

                    if (strcmp(type->valuestring, "tool_use") == 0) {
                        cJSON *id = cJSON_GetObjectItem(content_block, "id");
                        cJSON *name = cJSON_GetObjectItem(content_block, "name");
                        if (id && cJSON_IsString(id)) {
                            free(ctx->tool_use_id);
                            ctx->tool_use_id = ctx->arena ? arena_strdup(ctx->arena, id->valuestring) : strdup(id->valuestring);
                        }
                        if (name && cJSON_IsString(name)) {
                            free(ctx->tool_use_name);
                            ctx->tool_use_name = ctx->arena ? arena_strdup(ctx->arena, name->valuestring) : strdup(name->valuestring);
                        }
                        ctx->tool_input_size = 0;
                        if (ctx->tool_input_json) {
                            ctx->tool_input_json[0] = '\0';
                        }
                    }
                }
            }
            LOG_DEBUG("Stream: content_block_start (index=%d, type=%s)",
                     ctx->content_block_index,
                     ctx->content_block_type ? ctx->content_block_type : "unknown");
            break;
        }

        case SSE_EVENT_CONTENT_BLOCK_DELTA: {
            // Delta with new content
            cJSON *delta = cJSON_GetObjectItem(event->data, "delta");
            if (delta) {
                cJSON *type = cJSON_GetObjectItem(delta, "type");
                if (type && cJSON_IsString(type)) {
                    if (strcmp(type->valuestring, "text_delta") == 0) {
                        cJSON *text = cJSON_GetObjectItem(delta, "text");
                        if (text && cJSON_IsString(text) && text->valuestring) {
                            size_t new_len = strlen(text->valuestring);
                            size_t needed = ctx->accumulated_size + new_len + 1;

                            if (needed > ctx->accumulated_capacity) {
                                size_t new_cap = ctx->accumulated_capacity * 2;
                                if (new_cap < needed) new_cap = needed;

                                if (ctx->arena) {
                                    // Use arena allocation with copy
                                    char *new_buf = arena_alloc(ctx->arena, new_cap);
                                    if (new_buf) {
                                        if (ctx->accumulated_text) {
                                            memcpy(new_buf, ctx->accumulated_text, ctx->accumulated_size + 1);
                                        }
                                        ctx->accumulated_text = new_buf;
                                        ctx->accumulated_capacity = new_cap;
                                    }
                                } else {
                                    // Fallback to realloc
                                    char *new_buf = realloc(ctx->accumulated_text, new_cap);
                                    if (new_buf) {
                                        ctx->accumulated_text = new_buf;
                                        ctx->accumulated_capacity = new_cap;
                                    }
                                }
                            }

                            if (ctx->accumulated_text && needed <= ctx->accumulated_capacity) {
                                memcpy(ctx->accumulated_text + ctx->accumulated_size,
                                      text->valuestring, new_len);
                                ctx->accumulated_size += new_len;
                                ctx->accumulated_text[ctx->accumulated_size] = '\0';

                                // Stream to TUI if available
                                if (ctx->state) {
                                    if (ctx->state->tui_queue) {
                                        post_tui_message(ctx->state->tui_queue, TUI_MSG_STREAM_APPEND, text->valuestring);
                                    } else if (ctx->state->tui) {
                                        tui_update_last_conversation_line(ctx->state->tui, text->valuestring);
                                    }
                                }
                            }
                        }
                    } else if (strcmp(type->valuestring, "input_json_delta") == 0) {
                        cJSON *partial = cJSON_GetObjectItem(delta, "partial_json");
                        if (partial && cJSON_IsString(partial) && partial->valuestring) {
                            size_t new_len = strlen(partial->valuestring);
                            size_t needed = ctx->tool_input_size + new_len + 1;

                            if (needed > ctx->tool_input_capacity) {
                                size_t new_cap = ctx->tool_input_capacity * 2;
                                if (new_cap < needed) new_cap = needed;

                                if (ctx->arena) {
                                    // Use arena allocation with copy
                                    char *new_buf = arena_alloc(ctx->arena, new_cap);
                                    if (new_buf) {
                                        if (ctx->tool_input_json) {
                                            memcpy(new_buf, ctx->tool_input_json, ctx->tool_input_size + 1);
                                        }
                                        ctx->tool_input_json = new_buf;
                                        ctx->tool_input_capacity = new_cap;
                                    }
                                } else {
                                    // Fallback to realloc
                                    char *new_buf = realloc(ctx->tool_input_json, new_cap);
                                    if (new_buf) {
                                        ctx->tool_input_json = new_buf;
                                        ctx->tool_input_capacity = new_cap;
                                    }
                                }
                            }

                            if (ctx->tool_input_json && needed <= ctx->tool_input_capacity) {
                                memcpy(ctx->tool_input_json + ctx->tool_input_size,
                                      partial->valuestring, new_len);
                                ctx->tool_input_size += new_len;
                                ctx->tool_input_json[ctx->tool_input_size] = '\0';
                            }
                        }
                    }
                }
            }
            break;
        }

        case SSE_EVENT_CONTENT_BLOCK_STOP:
            LOG_DEBUG("Stream: content_block_stop (index=%d)", ctx->content_block_index);
            break;

        case SSE_EVENT_MESSAGE_DELTA: {
            cJSON *delta = cJSON_GetObjectItem(event->data, "delta");
            if (delta) {
                cJSON *stop_reason = cJSON_GetObjectItem(delta, "stop_reason");
                if (stop_reason && cJSON_IsString(stop_reason)) {
                    free(ctx->stop_reason);
                    ctx->stop_reason = ctx->arena ? arena_strdup(ctx->arena, stop_reason->valuestring) : strdup(stop_reason->valuestring);
                    LOG_DEBUG("Stream: stop_reason=%s", ctx->stop_reason);
                }
            }
            break;
        }

        case SSE_EVENT_MESSAGE_STOP:
            LOG_DEBUG("Stream: message_stop");
            break;

        case SSE_EVENT_ERROR: {
            cJSON *error = cJSON_GetObjectItem(event->data, "error");
            if (error) {
                cJSON *message = cJSON_GetObjectItem(error, "message");
                if (message && cJSON_IsString(message)) {
                    LOG_ERROR("Stream error: %s", message->valuestring);
                }
            }
            return 1;  // Abort on error
        }

        case SSE_EVENT_PING:
            // Keepalive, ignore
            break;

        case SSE_EVENT_OPENAI_CHUNK:
        case SSE_EVENT_OPENAI_DONE:
            // OpenAI-specific events, not used by Anthropic provider
            LOG_WARN("Received unexpected OpenAI event in Anthropic provider");
            break;

        default:
            // Unknown event type
            LOG_WARN("Received unknown SSE event type: %d", event->type);
            break;
    }

    return 0;  // Continue
}

// ============================================================================
// Provider Implementation
// ============================================================================

static void anthropic_call_api(Provider *self, ConversationState *state, ApiCallResult *out) {
    ApiCallResult result = {0};
    AnthropicConfig *config = (AnthropicConfig*)self->config;

    if (!config || !config->api_key || !config->base_url) {
        result.error_message = strdup("Anthropic config or credentials not initialized");
        result.is_retryable = 0;
        *out = result;
        return;
    }

    // Build request JSON from internal messages (OpenAI-style), then convert
    int enable_caching = 1;  // Anthropic supports caching; ON by default unless disabled via env var
    const char *disable_env = getenv("DISABLE_PROMPT_CACHING");
    if (disable_env && (strcmp(disable_env, "1") == 0 || strcasecmp(disable_env, "true") == 0)) {
        enable_caching = 0;
    }

    cJSON *openai_req_obj = build_openai_request(state, enable_caching);
    if (!openai_req_obj) {
        result.error_message = strdup("Failed to build request JSON");
        result.is_retryable = 0;
        *out = result;
        return;
    }

    LOG_DEBUG("Anthropic: Built OpenAI-format request, converting to Anthropic format");

    char *openai_req = cJSON_PrintUnformatted(openai_req_obj);
    cJSON_Delete(openai_req_obj);
    if (!openai_req) {
        result.error_message = strdup("Failed to serialize request JSON");
        result.is_retryable = 0;
        *out = result; return;
    }

    char *anth_req = anthropic_convert_openai_to_anthropic_request(openai_req);
    LOG_DEBUG("Anthropic: Converted to Anthropic format, request length: %zu bytes",
              anth_req ? strlen(anth_req) : 0);
    if (!anth_req) {
        result.error_message = strdup("Failed to convert request to Anthropic format");
        result.is_retryable = 0;
        result.request_json = openai_req;  // keep for logging
        *out = result; return;
    }

    // Set up headers
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // Authentication header: default x-api-key unless a custom template is provided
    char auth_header[512];
    if (config->auth_header_template) {
        const char *pct = strstr(config->auth_header_template, "%s");
        if (pct) {
            size_t pre = (size_t)(pct - config->auth_header_template);
            size_t keylen = strlen(config->api_key);
            size_t suf = strlen(pct + 2);
            if (pre + keylen + suf + 1 < sizeof(auth_header)) {
                strlcpy(auth_header, config->auth_header_template, pre + 1);
                strlcat(auth_header, config->api_key, sizeof(auth_header));
                strlcat(auth_header, pct + 2, sizeof(auth_header));
            } else {
                strlcpy(auth_header, config->auth_header_template, sizeof(auth_header));
                LOG_WARN("Auth header template too long, truncated");
            }
        } else {
            strlcpy(auth_header, config->auth_header_template, sizeof(auth_header));
        }
    } else {
        snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", config->api_key);
    }
    headers = curl_slist_append(headers, auth_header);

    // Anthropic version header (allow override via OPENAI_EXTRA_HEADERS / env)
    const char *version_env = getenv("ANTHROPIC_VERSION");
    if (version_env && version_env[0]) {
        char buf[128];
        snprintf(buf, sizeof(buf), "anthropic-version: %s", version_env);
        headers = curl_slist_append(headers, buf);
    } else {
        headers = curl_slist_append(headers, ANTHROPIC_VERSION_HEADER);
    }

    // Extra headers from environment
    if (config->extra_headers) {
        for (int i = 0; i < config->extra_headers_count; i++) {
            if (config->extra_headers[i]) headers = curl_slist_append(headers, config->extra_headers[i]);
        }
    }

    if (!headers) {
        result.error_message = strdup("Failed to setup HTTP headers");
        result.is_retryable = 0;
        result.request_json = anth_req;
        result.headers_json = NULL;
        free(openai_req);
        *out = result; return;
    }

    // Check if streaming is enabled via environment variable
    int enable_streaming = 0;
    const char *streaming_env = getenv("KLAWED_ENABLE_STREAMING");
    if (streaming_env && (strcmp(streaming_env, "1") == 0 || strcasecmp(streaming_env, "true") == 0)) {
        enable_streaming = 1;
    }

    // Modify request to enable streaming if needed
    if (enable_streaming) {
        cJSON *req_json = cJSON_Parse(anth_req);
        if (req_json) {
            cJSON_AddBoolToObject(req_json, "stream", 1);
            free(anth_req);
            anth_req = cJSON_PrintUnformatted(req_json);
            cJSON_Delete(req_json);
        }
    }

    // Build HTTP request using the new HTTP client
    HttpRequest req = {0};
    req.url = config->base_url;
    req.method = "POST";
    req.body = anth_req;
    req.headers = headers;
    req.connect_timeout_ms = 30000;  // 30 seconds
    req.total_timeout_ms = 300000;   // 5 minutes
    req.follow_redirects = 0;
    req.verbose = 0;
    req.enable_streaming = enable_streaming;

    // Execute HTTP request
    HttpResponse *http_resp = NULL;
    StreamingContext stream_ctx;

    if (enable_streaming) {
        anthropic_streaming_context_init(&stream_ctx, state);
        http_resp = http_client_execute_stream(&req, anthropic_streaming_event_handler, &stream_ctx, progress_callback, state);
    } else {
        http_resp = http_client_execute(&req, progress_callback, state);
    }

    // Convert headers to JSON for logging
    result.headers_json = http_headers_to_json(headers);

    // Free the headers list (http_client_execute makes its own copy)
    curl_slist_free_all(headers);

    // Keep request JSONs for logging
    result.request_json = anth_req;

    if (!http_resp) {
        result.error_message = strdup("Failed to execute HTTP request (memory allocation failed)");
        result.is_retryable = 0;
        free(result.headers_json);
        result.headers_json = NULL;
        free(openai_req);
        *out = result; return;
    }

    result.duration_ms = http_resp->duration_ms;
    result.http_status = http_resp->status_code;

    // Handle HTTP errors
    if (http_resp->error_message) {
        result.error_message = strdup(http_resp->error_message);
        result.is_retryable = http_resp->is_retryable;
        http_response_free(http_resp);
        free(result.headers_json);
        result.headers_json = NULL;
        free(openai_req);
        *out = result; return;
    }

    result.raw_response = http_resp->body ? strdup(http_resp->body) : NULL;
    http_response_free(http_resp);

    if (result.http_status >= 200 && result.http_status < 300) {
        cJSON *openai_like = NULL;

        // If streaming was used, reconstruct response from streaming context
        if (enable_streaming) {
            // Build a synthetic Anthropic response from accumulated data
            cJSON *synth_response = cJSON_CreateObject();
            if (synth_response) {
                cJSON_AddStringToObject(synth_response, "id", "streaming");
                cJSON_AddStringToObject(synth_response, "type", "message");
                cJSON_AddStringToObject(synth_response, "role", "assistant");

                cJSON *content = cJSON_CreateArray();
                if (stream_ctx.accumulated_text && stream_ctx.accumulated_size > 0) {
                    cJSON *text_block = cJSON_CreateObject();
                    cJSON_AddStringToObject(text_block, "type", "text");
                    cJSON_AddStringToObject(text_block, "text", stream_ctx.accumulated_text);
                    cJSON_AddItemToArray(content, text_block);
                }
                // TODO: Add tool_use blocks if present
                cJSON_AddItemToObject(synth_response, "content", content);

                if (stream_ctx.stop_reason) {
                    cJSON_AddStringToObject(synth_response, "stop_reason", stream_ctx.stop_reason);
                } else {
                    cJSON_AddStringToObject(synth_response, "stop_reason", "end_turn");
                }

                // Add usage if available from message_start
                if (stream_ctx.message_start_data) {
                    cJSON *usage_src = cJSON_GetObjectItem(stream_ctx.message_start_data, "usage");
                    if (usage_src) {
                        cJSON_AddItemToObject(synth_response, "usage", cJSON_Duplicate(usage_src, 1));
                    }
                }

                // Convert to JSON string for logging
                char *synth_str = cJSON_PrintUnformatted(synth_response);
                if (synth_str) {
                    free(result.raw_response);
                    result.raw_response = synth_str;
                }

                // Convert to OpenAI format
                openai_like = anthropic_convert_response_to_openai(synth_str ? synth_str : "{}");
                cJSON_Delete(synth_response);
            }

            anthropic_streaming_context_free(&stream_ctx);

        } else {
            // Non-streaming: convert normal response
            openai_like = anthropic_convert_response_to_openai(result.raw_response);
        }

        // Synthesise a response
        if (!openai_like) {
            result.error_message = strdup("Failed to parse Anthropic response");
            result.is_retryable = 1;  // Malformed response might be transient, retry
            free(result.headers_json);
            result.headers_json = NULL;
            free(openai_req);
            *out = result; return;
        }

        // Create arena for ApiResponse and all its string data
        Arena *arena = arena_create(16384);  // 16KB arena for API response
        if (!arena) {
            result.error_message = strdup("Failed to create arena for ApiResponse");
            result.is_retryable = 0;
            cJSON_Delete(openai_like);
            free(result.headers_json);
            result.headers_json = NULL;
            free(openai_req);
            *out = result; return;
        }

        // Allocate ApiResponse from arena
        ApiResponse *api_resp = arena_alloc(arena, sizeof(ApiResponse));
        if (!api_resp) {
            result.error_message = strdup("Failed to allocate ApiResponse from arena");
            result.is_retryable = 0;
            arena_destroy(arena);
            cJSON_Delete(openai_like);
            free(result.headers_json);
            result.headers_json = NULL;
            free(openai_req);
            *out = result; return;
        }

        // Initialize ApiResponse
        memset(api_resp, 0, sizeof(ApiResponse));
        api_resp->arena = arena;
        api_resp->raw_response = openai_like;

        cJSON *choices = cJSON_GetObjectItem(openai_like, "choices");
        if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
            result.error_message = strdup("Invalid response format: no choices");
            result.is_retryable = 0;
            api_response_free(api_resp);
            free(result.headers_json);
            result.headers_json = NULL;
            free(openai_req);
            *out = result; return;
        }
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (!message) {
            result.error_message = strdup("Invalid response format: no message");
            result.is_retryable = 0;
            api_response_free(api_resp);
            free(result.headers_json);
            result.headers_json = NULL;
            free(openai_req);
            *out = result; return;
        }

        cJSON *content = cJSON_GetObjectItem(message, "content");
        if (content && cJSON_IsString(content) && content->valuestring) {
            api_resp->message.text = arena_strdup(api_resp->arena, content->valuestring);
            if (api_resp->message.text) {
                // Trim whitespace from the extracted content
                trim_whitespace(api_resp->message.text);
            }
        }

        cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
        if (tool_calls && cJSON_IsArray(tool_calls)) {
            int raw_count = cJSON_GetArraySize(tool_calls);
            int valid = 0;
            for (int i = 0; i < raw_count; i++) {
                cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
                if (cJSON_GetObjectItem(tc, "function")) valid++;
            }
            if (valid > 0) {
                api_resp->tools = arena_alloc(api_resp->arena,
                                             (size_t)valid * sizeof(ToolCall));
                if (!api_resp->tools) {
                    result.error_message = strdup("Failed to allocate tool calls from arena");
                    result.is_retryable = 0;
                    api_response_free(api_resp);
                    free(result.headers_json);
                    result.headers_json = NULL;
                    free(openai_req);
                    *out = result; return;
                }

                // Initialize tool call array
                memset(api_resp->tools, 0, (size_t)valid * sizeof(ToolCall));
                int idx = 0;
                for (int i = 0; i < raw_count; i++) {
                    cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
                    cJSON *id = cJSON_GetObjectItem(tc, "id");
                    cJSON *fn = cJSON_GetObjectItem(tc, "function");
                    if (!fn) continue;
                    cJSON *name = cJSON_GetObjectItem(fn, "name");
                    cJSON *args = cJSON_GetObjectItem(fn, "arguments");
                    api_resp->tools[idx].id = (id && cJSON_IsString(id)) ? arena_strdup(api_resp->arena, id->valuestring) : NULL;
                    api_resp->tools[idx].name = (name && cJSON_IsString(name)) ? arena_strdup(api_resp->arena, name->valuestring) : NULL;
                    if (args && cJSON_IsString(args)) {
                        api_resp->tools[idx].parameters = cJSON_Parse(args->valuestring);
                        if (!api_resp->tools[idx].parameters) api_resp->tools[idx].parameters = cJSON_CreateObject();
                    } else {
                        api_resp->tools[idx].parameters = cJSON_CreateObject();
                    }
                    idx++;
                }
                api_resp->tool_count = valid;
            }
        }

        result.response = api_resp;
        free(openai_req);
        *out = result; return;
    }

    // HTTP error handling
    result.is_retryable = is_http_error_retryable(result.http_status);

    // Try to extract message
    cJSON *err = cJSON_Parse(result.raw_response);
    if (err) {
        // Anthropic error shape has error.message
        cJSON *error_obj = cJSON_GetObjectItem(err, "error");
        if (error_obj) {
            cJSON *msg = cJSON_GetObjectItem(error_obj, "message");
            cJSON *type = cJSON_GetObjectItem(error_obj, "type");
            if (msg && cJSON_IsString(msg)) {
                const char *txt = msg->valuestring;
                const char *type_txt = (type && cJSON_IsString(type)) ? type->valuestring : "";
                if (is_context_length_error(txt, type_txt)) {
                    result.error_message = get_context_length_error_message();
                    result.is_retryable = 0;
                } else {
                    result.error_message = strdup(txt);
                }
            }
        }
        cJSON_Delete(err);
    }

    if (!result.error_message) {
        char buf[256];
        snprintf(buf, sizeof(buf), "HTTP %ld", result.http_status);
        result.error_message = strdup(buf);
    }

    free(result.headers_json);
    result.headers_json = NULL;
    free(openai_req);
    *out = result; return;
}

static void anthropic_cleanup(Provider *self) {
    if (!self) return;
    if (self->config) {
        AnthropicConfig *cfg = (AnthropicConfig*)self->config;
        free(cfg->api_key);
        free(cfg->base_url);
        free(cfg->auth_header_template);
        if (cfg->extra_headers) {
            for (int i = 0; i < cfg->extra_headers_count; i++) free(cfg->extra_headers[i]);
            free(cfg->extra_headers);
        }
        free(cfg);
    }
    free(self);
}

Provider* anthropic_provider_create(const char *api_key, const char *base_url) {
    LOG_DEBUG("Creating Anthropic provider...");
    if (!api_key || !api_key[0]) {
        LOG_ERROR("Anthropic provider: API key is required");
        return NULL;
    }

    Provider *p = calloc(1, sizeof(Provider));
    if (!p) return NULL;
    AnthropicConfig *cfg = calloc(1, sizeof(AnthropicConfig));
    if (!cfg) { free(p); return NULL; }

    cfg->api_key = strdup(api_key);
    if (!cfg->api_key) { free(cfg); free(p); return NULL; }

    if (base_url && base_url[0]) cfg->base_url = strdup(base_url);
    else cfg->base_url = strdup(DEFAULT_ANTHROPIC_URL);
    if (!cfg->base_url) { free(cfg->api_key); free(cfg); free(p); return NULL; }

    // Auth header template: prefer OPENAI_AUTH_HEADER if set, else default to x-api-key
    const char *auth_env = getenv("OPENAI_AUTH_HEADER");
    if (auth_env && auth_env[0]) {
        cfg->auth_header_template = strdup(auth_env);
        if (!cfg->auth_header_template) { free(cfg->base_url); free(cfg->api_key); free(cfg); free(p); return NULL; }
    }

    // Extra headers
    const char *extra_env = getenv("OPENAI_EXTRA_HEADERS");
    if (extra_env && extra_env[0]) {
        char *copy = strdup(extra_env);
        if (!copy) { free(cfg->auth_header_template); free(cfg->base_url); free(cfg->api_key); free(cfg); free(p); return NULL; }
        int count = 0; char *tok = strtok(copy, ",");
        while (tok) { count++; tok = strtok(NULL, ","); }
        cfg->extra_headers = calloc((size_t)count + 1, sizeof(char*));
        if (!cfg->extra_headers) { free(copy); free(cfg->auth_header_template); free(cfg->base_url); free(cfg->api_key); free(cfg); free(p); return NULL; }
        // Reset buffer for second strtok pass
        memcpy(copy, extra_env, strlen(extra_env) + 1);
        tok = strtok(copy, ",");
        for (int i = 0; i < count && tok; i++) {
            while (*tok == ' ' || *tok == '\t') tok++;
            char *end = tok + strlen(tok) - 1;
            while (end > tok && (*end == ' ' || *end == '\t')) end--;
            *(end + 1) = '\0';
            cfg->extra_headers[i] = strdup(tok);
            if (!cfg->extra_headers[i]) {
                for (int j = 0; j < i; j++) free(cfg->extra_headers[j]);
                free(cfg->extra_headers); free(copy); free(cfg->auth_header_template); free(cfg->base_url); free(cfg->api_key); free(cfg); free(p); return NULL; }
            tok = strtok(NULL, ",");
        }
        cfg->extra_headers_count = count;
        cfg->extra_headers[count] = NULL;
        free(copy);
    }

    p->name = "Anthropic";
    p->config = cfg;
    p->call_api = anthropic_call_api;
    p->cleanup = anthropic_cleanup;

    LOG_INFO("Anthropic provider created (endpoint: %s)", cfg->base_url);
    return p;
}
