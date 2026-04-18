/*
 * openai_streaming.c - OpenAI streaming response accumulator implementation
 */

#define _POSIX_C_SOURCE 200809L

#include "openai_streaming.h"
#include "logger.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <bsd/string.h>

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

int openai_streaming_accumulator_init(OpenAIStreamingAccumulator *acc) {
    if (!acc) return -1;

    memset(acc, 0, sizeof(OpenAIStreamingAccumulator));

    // Create arena for allocations (64KB initial)
    acc->arena = arena_create(64 * 1024);
    if (!acc->arena) {
        LOG_ERROR("Failed to create arena for streaming accumulator");
        return -1;
    }

    // Initialize text buffer
    acc->accumulated_capacity = 4096;
    acc->accumulated_text = arena_alloc(acc->arena, acc->accumulated_capacity);
    if (acc->accumulated_text) {
        acc->accumulated_text[0] = '\0';
    }

    // Initialize reasoning buffer
    acc->reasoning_capacity = 4096;
    acc->accumulated_reasoning = arena_alloc(acc->arena, acc->reasoning_capacity);
    if (acc->accumulated_reasoning) {
        acc->accumulated_reasoning[0] = '\0';
    }

    // Initialize tool call accumulator
    acc->tool_accumulator = tool_accumulator_create(acc->arena);
    if (!acc->tool_accumulator) {
        LOG_ERROR("Failed to create tool call accumulator");
        arena_destroy(acc->arena);
        acc->arena = NULL;
        return -1;
    }

    return 0;
}

void openai_streaming_accumulator_free(OpenAIStreamingAccumulator *acc) {
    if (!acc) return;

    if (acc->tool_accumulator) {
        tool_accumulator_destroy(acc->tool_accumulator);
        acc->tool_accumulator = NULL;
    }

    if (acc->arena) {
        arena_destroy(acc->arena);
        acc->arena = NULL;
    }

    // Note: other pointers are allocated from arena, so they don't need individual freeing
    memset(acc, 0, sizeof(OpenAIStreamingAccumulator));
}

/*
 * Helper to free a streaming context that contains an accumulator.
 * This handles the OpenAIProviderStreamingContext pattern where the
 * accumulator is embedded as a member named 'acc'.
 */
void openai_streaming_context_free(void *ctx) {
    if (!ctx) return;

    /* The context is expected to have an 'acc' member of type OpenAIStreamingAccumulator.
     * We access it via the known layout of OpenAIProviderStreamingContext.
     * This is a bit of a hack but maintains backwards compatibility. */
    OpenAIStreamingAccumulator *acc = (OpenAIStreamingAccumulator *)ctx;
    openai_streaming_accumulator_free(acc);
}

int openai_streaming_process_event(OpenAIStreamingAccumulator *acc, const StreamEvent *event) {
    if (!acc || !event) {
        return 0;  // Continue
    }

    // Handle [DONE] marker
    if (event->type == SSE_EVENT_OPENAI_DONE) {
        LOG_DEBUG("OpenAI stream: received [DONE] marker");
        return 0;
    }

    // Only process OpenAI chunks
    if (event->type != SSE_EVENT_OPENAI_CHUNK || !event->data) {
        return 0;  // Continue (ignore other events)
    }

    // Extract model and id if not yet seen
    if (!acc->model) {
        cJSON *model = cJSON_GetObjectItem(event->data, "model");
        if (model && cJSON_IsString(model)) {
            size_t len = strlen(model->valuestring) + 1;
            acc->model = arena_alloc(acc->arena, len);
            if (acc->model) {
                strlcpy(acc->model, model->valuestring, len);
            }
        }
    }
    if (!acc->message_id) {
        cJSON *id = cJSON_GetObjectItem(event->data, "id");
        if (id && cJSON_IsString(id)) {
            size_t len = strlen(id->valuestring) + 1;
            acc->message_id = arena_alloc(acc->arena, len);
            if (acc->message_id) {
                strlcpy(acc->message_id, id->valuestring, len);
            }
        }
    }

    // Capture usage from final chunk (some APIs send usage without choices)
    cJSON *usage = cJSON_GetObjectItem(event->data, "usage");
    if (usage && cJSON_IsObject(usage)) {
        cJSON *pt = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON *ct = cJSON_GetObjectItem(usage, "completion_tokens");
        cJSON *tt = cJSON_GetObjectItem(usage, "total_tokens");
        if (pt && cJSON_IsNumber(pt)) acc->prompt_tokens = pt->valueint;
        if (ct && cJSON_IsNumber(ct)) acc->completion_tokens = ct->valueint;
        if (tt && cJSON_IsNumber(tt)) acc->total_tokens = tt->valueint;
        LOG_DEBUG("OpenAI stream: captured usage prompt=%d completion=%d total=%d",
                  acc->prompt_tokens, acc->completion_tokens, acc->total_tokens);
    }

    // Process choices array
    cJSON *choices = cJSON_GetObjectItem(event->data, "choices");
    if (!choices || !cJSON_IsArray(choices)) {
        return 0;
    }

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    if (!choice) {
        return 0;
    }

    cJSON *delta = cJSON_GetObjectItem(choice, "delta");
    if (!delta) {
        // Handle finish_reason in non-delta case (some APIs put it directly in choice)
        cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
        if (finish_reason && cJSON_IsString(finish_reason) && finish_reason->valuestring) {
            size_t len = strlen(finish_reason->valuestring) + 1;
            acc->finish_reason = arena_alloc(acc->arena, len);
            if (acc->finish_reason) {
                strlcpy(acc->finish_reason, finish_reason->valuestring, len);
                LOG_DEBUG("OpenAI stream: finish_reason=%s", acc->finish_reason);
            }
        }
        return 0;
    }

    // Handle text content
    cJSON *content = cJSON_GetObjectItem(delta, "content");
    if (content && cJSON_IsString(content) && content->valuestring) {
        size_t new_len = strlen(content->valuestring);
        if (new_len > 0) {
            size_t needed = acc->accumulated_size + new_len + 1;

            if (needed > acc->accumulated_capacity) {
                size_t new_cap = acc->accumulated_capacity * 2;
                if (new_cap < needed) new_cap = needed;
                char *new_buf = arena_alloc(acc->arena, new_cap);
                if (new_buf) {
                    if (acc->accumulated_text && acc->accumulated_size > 0) {
                        memcpy(new_buf, acc->accumulated_text, acc->accumulated_size);
                    }
                    acc->accumulated_text = new_buf;
                    acc->accumulated_capacity = new_cap;
                }
            }

            if (acc->accumulated_text && needed <= acc->accumulated_capacity) {
                memcpy(acc->accumulated_text + acc->accumulated_size,
                       content->valuestring, new_len);
                acc->accumulated_size += new_len;
                acc->accumulated_text[acc->accumulated_size] = '\0';
            }
        }
    }

    // Handle reasoning_content (for thinking models)
    cJSON *reasoning_content = cJSON_GetObjectItem(delta, "reasoning_content");
    if (reasoning_content && cJSON_IsString(reasoning_content) && reasoning_content->valuestring) {
        size_t new_len = strlen(reasoning_content->valuestring);
        if (new_len > 0) {
            size_t needed = acc->reasoning_size + new_len + 1;

            if (needed > acc->reasoning_capacity) {
                size_t new_cap = acc->reasoning_capacity * 2;
                if (new_cap < needed) new_cap = needed;
                char *new_buf = arena_alloc(acc->arena, new_cap);
                if (new_buf) {
                    if (acc->accumulated_reasoning && acc->reasoning_size > 0) {
                        memcpy(new_buf, acc->accumulated_reasoning, acc->reasoning_size);
                    }
                    acc->accumulated_reasoning = new_buf;
                    acc->reasoning_capacity = new_cap;
                }
            }

            if (acc->accumulated_reasoning && needed <= acc->reasoning_capacity) {
                memcpy(acc->accumulated_reasoning + acc->reasoning_size,
                       reasoning_content->valuestring, new_len);
                acc->reasoning_size += new_len;
                acc->accumulated_reasoning[acc->reasoning_size] = '\0';
                LOG_DEBUG("Accumulated reasoning_content: %zu bytes", acc->reasoning_size);
            }
        }
    }

    // Handle tool calls using shared accumulator module
    cJSON *tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
    if (tool_calls && cJSON_IsArray(tool_calls) && acc->tool_accumulator) {
        tool_accumulator_process_delta(acc->tool_accumulator, tool_calls);
    }

    // Handle finish_reason in delta
    cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
    if (finish_reason && cJSON_IsString(finish_reason) && finish_reason->valuestring) {
        size_t len = strlen(finish_reason->valuestring) + 1;
        acc->finish_reason = arena_alloc(acc->arena, len);
        if (acc->finish_reason) {
            strlcpy(acc->finish_reason, finish_reason->valuestring, len);
            LOG_DEBUG("OpenAI stream: finish_reason=%s", acc->finish_reason);
        }
    }

    return 0;
}

cJSON* openai_streaming_build_response(const OpenAIStreamingAccumulator *acc) {
    if (!acc) return NULL;

    cJSON *response = cJSON_CreateObject();
    if (!response) return NULL;

    // Build synthetic response in OpenAI format
    cJSON_AddStringToObject(response, "id", acc->message_id ? acc->message_id : "streaming");
    cJSON_AddStringToObject(response, "object", "chat.completion");
    cJSON_AddStringToObject(response, "model", acc->model ? acc->model : "unknown");

    // Add timestamp
    time_t now = time(NULL);
    cJSON_AddNumberToObject(response, "created", (double)now);

    // Build choices array
    cJSON *choices = cJSON_CreateArray();
    cJSON *choice = cJSON_CreateObject();
    cJSON_AddNumberToObject(choice, "index", 0);

    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "assistant");

    // Add content
    if (acc->accumulated_text && acc->accumulated_size > 0) {
        cJSON_AddStringToObject(message, "content", acc->accumulated_text);
    } else {
        cJSON_AddNullToObject(message, "content");
    }

    // Add reasoning_content if present
    if (acc->accumulated_reasoning && acc->reasoning_size > 0) {
        cJSON_AddStringToObject(message, "reasoning_content", acc->accumulated_reasoning);
    }

    // Add tool calls (using shared accumulator's filtering)
    if (acc->tool_accumulator) {
        int valid_count = tool_accumulator_count_valid(acc->tool_accumulator);
        if (valid_count > 0) {
            cJSON *filtered_tool_calls = tool_accumulator_filter_valid(acc->tool_accumulator);
            if (filtered_tool_calls && cJSON_GetArraySize(filtered_tool_calls) > 0) {
                cJSON_AddItemToObject(message, "tool_calls", filtered_tool_calls);
            } else {
                cJSON_Delete(filtered_tool_calls);
            }
        }
    }

    cJSON_AddItemToObject(choice, "message", message);
    cJSON_AddStringToObject(choice, "finish_reason",
        acc->finish_reason ? acc->finish_reason : "stop");

    cJSON_AddItemToArray(choices, choice);
    cJSON_AddItemToObject(response, "choices", choices);

    // Add usage (captured from final chunk if API provided it)
    cJSON *usage = cJSON_CreateObject();
    cJSON_AddNumberToObject(usage, "prompt_tokens", acc->prompt_tokens);
    cJSON_AddNumberToObject(usage, "completion_tokens", acc->completion_tokens);
    cJSON_AddNumberToObject(usage, "total_tokens",
                            acc->total_tokens > 0 ? acc->total_tokens
                            : (acc->prompt_tokens + acc->completion_tokens));
    cJSON_AddItemToObject(response, "usage", usage);

    return response;
}

const char* openai_streaming_get_text(const OpenAIStreamingAccumulator *acc) {
    if (!acc) return NULL;
    return acc->accumulated_text;
}

const char* openai_streaming_get_reasoning(const OpenAIStreamingAccumulator *acc) {
    if (!acc) return NULL;
    return acc->accumulated_reasoning;
}

int openai_streaming_get_tool_call_count(const OpenAIStreamingAccumulator *acc) {
    if (!acc || !acc->tool_accumulator) return 0;
    return tool_accumulator_count_valid(acc->tool_accumulator);
}

cJSON* openai_streaming_get_tool_call(const OpenAIStreamingAccumulator *acc, int index) {
    if (!acc || !acc->tool_accumulator || index < 0) return NULL;

    /* Iterate through all tools and return the Nth valid one */
    cJSON *all_tools = tool_accumulator_get_tool_calls(acc->tool_accumulator);
    if (!all_tools) return NULL;

    int valid_idx = 0;
    int tool_count = cJSON_GetArraySize(all_tools);

    for (int i = 0; i < tool_count; i++) {
        cJSON *tool = cJSON_GetArrayItem(all_tools, i);
        if (!tool) continue;

        cJSON *id_obj = cJSON_GetObjectItem(tool, "id");
        cJSON *function_obj = cJSON_GetObjectItem(tool, "function");
        cJSON *name_obj = function_obj ? cJSON_GetObjectItem(function_obj, "name") : NULL;

        const char *id_str = (id_obj && cJSON_IsString(id_obj)) ? id_obj->valuestring : "";
        const char *name_str = (name_obj && cJSON_IsString(name_obj)) ? name_obj->valuestring : "";

        /* Skip invalid tool calls */
        if (!id_str[0] || !name_str[0]) {
            continue;
        }

        if (valid_idx == index) {
            return tool;
        }
        valid_idx++;
    }

    return NULL;
}
