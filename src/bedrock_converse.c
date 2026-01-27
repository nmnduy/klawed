/*
 * bedrock_converse.c - AWS Bedrock Converse API implementation
 *
 * Implements format conversion between OpenAI and AWS Bedrock Converse API.
 * Uses the Converse API endpoint (/model/{modelId}/converse) which provides
 * a unified interface across different foundation models.
 */

#define _POSIX_C_SOURCE 200809L

#include "bedrock_converse.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>
#include <bsd/string.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * URL-encode a string for HTTP path
 * Only encodes characters that are not URL-safe
 */
static char* url_encode_string(const char *str) {
    if (!str) return NULL;

    size_t len = strlen(str);
    /* Worst case: every char becomes %XX (3 chars) */
    if (len > (SIZE_MAX - 1) / 3) return NULL;

    char *encoded = malloc(len * 3 + 1);
    if (!encoded) return NULL;

    char *out = encoded;
    for (const char *p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        /* Keep alphanumerics and safe chars unencoded */
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            *out++ = (char)c;
        } else {
            snprintf(out, 4, "%%%02X", c);
            out += 3;
        }
    }
    *out = '\0';
    return encoded;
}

/**
 * Extract system messages from OpenAI messages array
 * Returns concatenated system content as a string
 */
static char* extract_system_content(cJSON *messages) {
    if (!messages || !cJSON_IsArray(messages)) return NULL;

    /* First pass: calculate total length needed */
    size_t total_len = 0;
    cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        if (!role || !cJSON_IsString(role)) continue;
        if (strcmp(role->valuestring, "system") != 0) continue;

        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (cJSON_IsString(content) && content->valuestring) {
            total_len += strlen(content->valuestring) + 1; /* +1 for newline */
        } else if (cJSON_IsArray(content)) {
            cJSON *block = NULL;
            cJSON_ArrayForEach(block, content) {
                cJSON *text = cJSON_GetObjectItem(block, "text");
                if (text && cJSON_IsString(text) && text->valuestring) {
                    total_len += strlen(text->valuestring) + 1;
                }
            }
        }
    }

    if (total_len == 0) return NULL;

    char *result = malloc(total_len + 1);
    if (!result) return NULL;
    result[0] = '\0';

    /* Second pass: copy content */
    size_t offset = 0;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        if (!role || !cJSON_IsString(role)) continue;
        if (strcmp(role->valuestring, "system") != 0) continue;

        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (cJSON_IsString(content) && content->valuestring) {
            size_t len = strlen(content->valuestring);
            strlcpy(result + offset, content->valuestring, total_len + 1 - offset);
            offset += len;
            if (offset < total_len) {
                result[offset++] = '\n';
            }
        } else if (cJSON_IsArray(content)) {
            cJSON *block = NULL;
            cJSON_ArrayForEach(block, content) {
                cJSON *text = cJSON_GetObjectItem(block, "text");
                if (text && cJSON_IsString(text) && text->valuestring) {
                    size_t len = strlen(text->valuestring);
                    strlcpy(result + offset, text->valuestring, total_len + 1 - offset);
                    offset += len;
                    if (offset < total_len) {
                        result[offset++] = '\n';
                    }
                }
            }
        }
    }

    /* Remove trailing newline if present */
    if (offset > 0 && result[offset - 1] == '\n') {
        result[offset - 1] = '\0';
    }

    return result;
}

/**
 * Convert OpenAI content (string or array) to Converse content array
 * Converse requires content as array of blocks: [{text: "..."}]
 */
static cJSON* convert_content_to_converse(cJSON *content) {
    cJSON *converse_content = cJSON_CreateArray();
    if (!converse_content) return NULL;

    if (cJSON_IsString(content) && content->valuestring && strlen(content->valuestring) > 0) {
        /* Simple string content -> [{text: "..."}] */
        cJSON *text_block = cJSON_CreateObject();
        if (text_block) {
            cJSON_AddStringToObject(text_block, "text", content->valuestring);
            cJSON_AddItemToArray(converse_content, text_block);
        }
    } else if (cJSON_IsArray(content)) {
        /* Array of content blocks - convert each */
        cJSON *block = NULL;
        cJSON_ArrayForEach(block, content) {
            cJSON *type = cJSON_GetObjectItem(block, "type");
            if (!type || !cJSON_IsString(type)) continue;

            if (strcmp(type->valuestring, "text") == 0) {
                cJSON *text = cJSON_GetObjectItem(block, "text");
                if (text && cJSON_IsString(text) && text->valuestring) {
                    cJSON *text_block = cJSON_CreateObject();
                    if (text_block) {
                        cJSON_AddStringToObject(text_block, "text", text->valuestring);
                        cJSON_AddItemToArray(converse_content, text_block);
                    }
                }
            } else if (strcmp(type->valuestring, "image_url") == 0) {
                /* Convert image_url to Converse image format */
                cJSON *image_url = cJSON_GetObjectItem(block, "image_url");
                if (!image_url) continue;

                cJSON *url = cJSON_GetObjectItem(image_url, "url");
                if (!url || !cJSON_IsString(url)) continue;

                const char *url_str = url->valuestring;
                /* Only handle data URLs */
                if (strncmp(url_str, "data:", 5) != 0) {
                    LOG_WARN("Image URL is not a data URL, skipping: %.50s...", url_str);
                    continue;
                }

                /* Parse: data:image/jpeg;base64,<data> */
                const char *media_start = url_str + 5;
                const char *semicolon = strchr(media_start, ';');
                if (!semicolon) continue;

                size_t media_len = (size_t)(semicolon - media_start);
                char *media_type = malloc(media_len + 1);
                if (!media_type) continue;
                strlcpy(media_type, media_start, media_len + 1);

                const char *base64_marker = strstr(semicolon, "base64,");
                if (!base64_marker) {
                    free(media_type);
                    continue;
                }
                const char *base64_data = base64_marker + 7;

                /* Converse image format */
                cJSON *image_block = cJSON_CreateObject();
                if (image_block) {
                    cJSON *image = cJSON_CreateObject();
                    if (image) {
                        cJSON_AddStringToObject(image, "format",
                            strstr(media_type, "png") ? "png" :
                            strstr(media_type, "gif") ? "gif" :
                            strstr(media_type, "webp") ? "webp" : "jpeg");

                        cJSON *source = cJSON_CreateObject();
                        if (source) {
                            cJSON_AddStringToObject(source, "bytes", base64_data);
                            cJSON_AddItemToObject(image, "source", source);
                        }
                        cJSON_AddItemToObject(image_block, "image", image);
                    }
                    cJSON_AddItemToArray(converse_content, image_block);
                }
                free(media_type);
            }
            /* Other content types can be added here as needed */
        }
    }

    return converse_content;
}

/**
 * Convert OpenAI tool_calls to Converse toolUse content blocks
 */
static cJSON* convert_tool_calls_to_converse(cJSON *tool_calls) {
    cJSON *content_array = cJSON_CreateArray();
    if (!content_array) return NULL;

    cJSON *tool_call = NULL;
    cJSON_ArrayForEach(tool_call, tool_calls) {
        cJSON *id = cJSON_GetObjectItem(tool_call, "id");
        cJSON *function = cJSON_GetObjectItem(tool_call, "function");
        if (!function) continue;

        cJSON *name = cJSON_GetObjectItem(function, "name");
        cJSON *arguments = cJSON_GetObjectItem(function, "arguments");

        cJSON *tool_use_block = cJSON_CreateObject();
        if (!tool_use_block) continue;

        /* Converse toolUse format */
        cJSON *tool_use = cJSON_CreateObject();
        if (!tool_use) {
            cJSON_Delete(tool_use_block);
            continue;
        }

        if (id && cJSON_IsString(id)) {
            cJSON_AddStringToObject(tool_use, "toolUseId", id->valuestring);
        }
        if (name && cJSON_IsString(name)) {
            cJSON_AddStringToObject(tool_use, "name", name->valuestring);
        }
        if (arguments && cJSON_IsString(arguments)) {
            cJSON *input = cJSON_Parse(arguments->valuestring);
            if (input) {
                cJSON_AddItemToObject(tool_use, "input", input);
            } else {
                cJSON_AddItemToObject(tool_use, "input", cJSON_CreateObject());
            }
        } else {
            cJSON_AddItemToObject(tool_use, "input", cJSON_CreateObject());
        }

        cJSON_AddItemToObject(tool_use_block, "toolUse", tool_use);
        cJSON_AddItemToArray(content_array, tool_use_block);
    }

    return content_array;
}

/**
 * Convert OpenAI tools array to Converse toolConfig format
 *
 * OpenAI format:
 *   {type: "function", function: {name, description, parameters}}
 *
 * Converse format:
 *   {toolSpec: {name, description, inputSchema: {json: parameters}}}
 */
static cJSON* convert_tools_to_converse(cJSON *tools) {
    cJSON *tool_config = cJSON_CreateObject();
    if (!tool_config) return NULL;

    cJSON *tools_array = cJSON_CreateArray();
    if (!tools_array) {
        cJSON_Delete(tool_config);
        return NULL;
    }

    cJSON *tool = NULL;
    cJSON_ArrayForEach(tool, tools) {
        cJSON *function = cJSON_GetObjectItem(tool, "function");
        if (!function) continue;

        cJSON *name = cJSON_GetObjectItem(function, "name");
        cJSON *description = cJSON_GetObjectItem(function, "description");
        cJSON *parameters = cJSON_GetObjectItem(function, "parameters");

        /* Build toolSpec wrapper */
        cJSON *tool_spec = cJSON_CreateObject();
        if (!tool_spec) continue;

        if (name && cJSON_IsString(name)) {
            cJSON_AddStringToObject(tool_spec, "name", name->valuestring);
        }
        if (description && cJSON_IsString(description)) {
            cJSON_AddStringToObject(tool_spec, "description", description->valuestring);
        }
        if (parameters) {
            /* Converse uses inputSchema.json wrapper */
            cJSON *input_schema = cJSON_CreateObject();
            if (input_schema) {
                cJSON_AddItemToObject(input_schema, "json", cJSON_Duplicate(parameters, 1));
                cJSON_AddItemToObject(tool_spec, "inputSchema", input_schema);
            }
        }

        /* Wrap in toolSpec container */
        cJSON *tool_container = cJSON_CreateObject();
        if (tool_container) {
            cJSON_AddItemToObject(tool_container, "toolSpec", tool_spec);
            cJSON_AddItemToArray(tools_array, tool_container);
        } else {
            cJSON_Delete(tool_spec);
        }
    }

    if (cJSON_GetArraySize(tools_array) > 0) {
        cJSON_AddItemToObject(tool_config, "tools", tools_array);
    } else {
        cJSON_Delete(tools_array);
        cJSON_Delete(tool_config);
        return NULL;
    }

    return tool_config;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

char* bedrock_converse_encode_model_id(const char *model_id) {
    return url_encode_string(model_id);
}

char* bedrock_converse_build_endpoint(const char *region, const char *model_id) {
    if (!region || !model_id) {
        LOG_ERROR("bedrock_converse_build_endpoint: region and model_id required");
        return NULL;
    }

    /* URL-encode the model ID (may contain colons, etc.) */
    char *encoded_model = url_encode_string(model_id);
    if (!encoded_model) {
        LOG_ERROR("Failed to URL-encode model ID");
        return NULL;
    }

    /* Calculate buffer size:
     * https://bedrock-runtime.{region}.amazonaws.com/model/{model-id}/converse
     * Base: ~50 chars + region + encoded_model + safety margin
     */
    size_t len = 80 + strlen(region) + strlen(encoded_model);
    char *endpoint = malloc(len);
    if (!endpoint) {
        free(encoded_model);
        LOG_ERROR("Failed to allocate endpoint buffer");
        return NULL;
    }

    snprintf(endpoint, len,
             "https://bedrock-runtime.%s.amazonaws.com/model/%s/converse",
             region, encoded_model);

    free(encoded_model);
    LOG_DEBUG("Built Converse endpoint: %s", endpoint);
    return endpoint;
}

char* bedrock_converse_build_streaming_endpoint(const char *region, const char *model_id) {
    if (!region || !model_id) {
        LOG_ERROR("bedrock_converse_build_streaming_endpoint: region and model_id required");
        return NULL;
    }

    char *encoded_model = url_encode_string(model_id);
    if (!encoded_model) {
        LOG_ERROR("Failed to URL-encode model ID");
        return NULL;
    }

    size_t len = 80 + strlen(region) + strlen(encoded_model);
    char *endpoint = malloc(len);
    if (!endpoint) {
        free(encoded_model);
        LOG_ERROR("Failed to allocate endpoint buffer");
        return NULL;
    }

    snprintf(endpoint, len,
             "https://bedrock-runtime.%s.amazonaws.com/model/%s/converse-stream",
             region, encoded_model);

    free(encoded_model);
    LOG_DEBUG("Built Converse streaming endpoint: %s", endpoint);
    return endpoint;
}

char* bedrock_converse_convert_request(const char *openai_request) {
    LOG_DEBUG("=== CONVERSE REQUEST CONVERSION START ===");

    if (!openai_request) {
        LOG_ERROR("bedrock_converse_convert_request: null input");
        return NULL;
    }

    LOG_DEBUG("OpenAI request length: %zu bytes", strlen(openai_request));

    cJSON *openai = cJSON_Parse(openai_request);
    if (!openai) {
        LOG_ERROR("Failed to parse OpenAI request JSON");
        return NULL;
    }

    cJSON *converse = cJSON_CreateObject();
    if (!converse) {
        cJSON_Delete(openai);
        LOG_ERROR("Failed to create Converse request object");
        return NULL;
    }

    /* Extract fields from OpenAI request */
    cJSON *messages = cJSON_GetObjectItem(openai, "messages");
    cJSON *tools = cJSON_GetObjectItem(openai, "tools");
    cJSON *max_tokens = cJSON_GetObjectItem(openai, "max_completion_tokens");
    cJSON *temperature = cJSON_GetObjectItem(openai, "temperature");
    cJSON *top_p = cJSON_GetObjectItem(openai, "top_p");

    /* === Build system array (extract from messages) === */
    char *system_content = extract_system_content(messages);
    if (system_content && strlen(system_content) > 0) {
        cJSON *system_array = cJSON_CreateArray();
        if (system_array) {
            cJSON *system_block = cJSON_CreateObject();
            if (system_block) {
                cJSON_AddStringToObject(system_block, "text", system_content);
                cJSON_AddItemToArray(system_array, system_block);
            }
            cJSON_AddItemToObject(converse, "system", system_array);
        }
        LOG_DEBUG("Extracted system prompt (%zu chars)", strlen(system_content));
    }
    free(system_content);

    /* === Build messages array === */
    cJSON *converse_messages = cJSON_CreateArray();
    if (!converse_messages) {
        cJSON_Delete(openai);
        cJSON_Delete(converse);
        return NULL;
    }

    if (messages && cJSON_IsArray(messages)) {
        cJSON *msg = NULL;
        cJSON_ArrayForEach(msg, messages) {
            cJSON *role = cJSON_GetObjectItem(msg, "role");
            if (!role || !cJSON_IsString(role)) continue;

            const char *role_str = role->valuestring;

            /* Skip system messages (already extracted) */
            if (strcmp(role_str, "system") == 0) continue;

            cJSON *content = cJSON_GetObjectItem(msg, "content");
            cJSON *tool_calls = cJSON_GetObjectItem(msg, "tool_calls");

            /* Handle user messages */
            if (strcmp(role_str, "user") == 0) {
                cJSON *converse_msg = cJSON_CreateObject();
                if (!converse_msg) continue;

                cJSON_AddStringToObject(converse_msg, "role", "user");

                cJSON *converse_content = convert_content_to_converse(content);
                if (converse_content && cJSON_GetArraySize(converse_content) > 0) {
                    cJSON_AddItemToObject(converse_msg, "content", converse_content);
                    cJSON_AddItemToArray(converse_messages, converse_msg);
                } else {
                    cJSON_Delete(converse_content);
                    cJSON_Delete(converse_msg);
                    LOG_WARN("Skipping user message with empty content");
                }
            }
            /* Handle assistant messages */
            else if (strcmp(role_str, "assistant") == 0) {
                cJSON *converse_msg = cJSON_CreateObject();
                if (!converse_msg) continue;

                cJSON_AddStringToObject(converse_msg, "role", "assistant");

                cJSON *content_array = cJSON_CreateArray();
                if (!content_array) {
                    cJSON_Delete(converse_msg);
                    continue;
                }

                /* Add text content if present */
                if (cJSON_IsString(content) && content->valuestring &&
                    strlen(content->valuestring) > 0) {
                    cJSON *text_block = cJSON_CreateObject();
                    if (text_block) {
                        cJSON_AddStringToObject(text_block, "text", content->valuestring);
                        cJSON_AddItemToArray(content_array, text_block);
                    }
                } else if (cJSON_IsArray(content)) {
                    cJSON *block = NULL;
                    cJSON_ArrayForEach(block, content) {
                        cJSON *type = cJSON_GetObjectItem(block, "type");
                        if (type && cJSON_IsString(type) &&
                            strcmp(type->valuestring, "text") == 0) {
                            cJSON *text = cJSON_GetObjectItem(block, "text");
                            if (text && cJSON_IsString(text) && text->valuestring) {
                                cJSON *text_block = cJSON_CreateObject();
                                if (text_block) {
                                    cJSON_AddStringToObject(text_block, "text", text->valuestring);
                                    cJSON_AddItemToArray(content_array, text_block);
                                }
                            }
                        }
                    }
                }

                /* Add tool_calls as toolUse blocks */
                if (tool_calls && cJSON_IsArray(tool_calls)) {
                    cJSON *tool_use_blocks = convert_tool_calls_to_converse(tool_calls);
                    if (tool_use_blocks) {
                        cJSON *block = NULL;
                        cJSON_ArrayForEach(block, tool_use_blocks) {
                            cJSON_AddItemToArray(content_array, cJSON_Duplicate(block, 1));
                        }
                        cJSON_Delete(tool_use_blocks);
                    }
                }

                if (cJSON_GetArraySize(content_array) > 0) {
                    cJSON_AddItemToObject(converse_msg, "content", content_array);
                    cJSON_AddItemToArray(converse_messages, converse_msg);
                } else {
                    cJSON_Delete(content_array);
                    cJSON_Delete(converse_msg);
                    LOG_WARN("Skipping assistant message with no content");
                }
            }
            /* Handle tool result messages */
            else if (strcmp(role_str, "tool") == 0) {
                cJSON *tool_call_id = cJSON_GetObjectItem(msg, "tool_call_id");
                if (!tool_call_id || !cJSON_IsString(tool_call_id)) continue;

                /* Tool results go in a user message with toolResult block */
                cJSON *converse_msg = cJSON_CreateObject();
                if (!converse_msg) continue;

                cJSON_AddStringToObject(converse_msg, "role", "user");

                cJSON *content_array = cJSON_CreateArray();
                if (!content_array) {
                    cJSON_Delete(converse_msg);
                    continue;
                }

                cJSON *tool_result_block = cJSON_CreateObject();
                if (!tool_result_block) {
                    cJSON_Delete(content_array);
                    cJSON_Delete(converse_msg);
                    continue;
                }

                /* Build toolResult structure */
                cJSON *tool_result = cJSON_CreateObject();
                if (!tool_result) {
                    cJSON_Delete(tool_result_block);
                    cJSON_Delete(content_array);
                    cJSON_Delete(converse_msg);
                    continue;
                }

                cJSON_AddStringToObject(tool_result, "toolUseId", tool_call_id->valuestring);

                /* Tool result content array */
                cJSON *result_content = cJSON_CreateArray();
                if (result_content) {
                    if (cJSON_IsString(content) && content->valuestring) {
                        cJSON *text_block = cJSON_CreateObject();
                        if (text_block) {
                            cJSON_AddStringToObject(text_block, "text", content->valuestring);
                            cJSON_AddItemToArray(result_content, text_block);
                        }
                    } else if (cJSON_IsArray(content)) {
                        /* Content is already an array, duplicate it */
                        cJSON *block = NULL;
                        cJSON_ArrayForEach(block, content) {
                            cJSON_AddItemToArray(result_content, cJSON_Duplicate(block, 1));
                        }
                    } else if (content) {
                        /* Convert other types to JSON string */
                        char *content_str = cJSON_PrintUnformatted(content);
                        if (content_str) {
                            cJSON *text_block = cJSON_CreateObject();
                            if (text_block) {
                                cJSON_AddStringToObject(text_block, "text", content_str);
                                cJSON_AddItemToArray(result_content, text_block);
                            }
                            free(content_str);
                        }
                    }
                    cJSON_AddItemToObject(tool_result, "content", result_content);
                }

                cJSON_AddItemToObject(tool_result_block, "toolResult", tool_result);
                cJSON_AddItemToArray(content_array, tool_result_block);
                cJSON_AddItemToObject(converse_msg, "content", content_array);
                cJSON_AddItemToArray(converse_messages, converse_msg);

                LOG_DEBUG("Converted tool result for toolUseId: %s", tool_call_id->valuestring);
            }
        }
    }

    cJSON_AddItemToObject(converse, "messages", converse_messages);
    LOG_DEBUG("Converted %d messages", cJSON_GetArraySize(converse_messages));

    /* === Build inferenceConfig === */
    cJSON *inference_config = cJSON_CreateObject();
    int has_inference_config = 0;

    if (max_tokens && cJSON_IsNumber(max_tokens)) {
        cJSON_AddNumberToObject(inference_config, "maxTokens", max_tokens->valueint);
        has_inference_config = 1;
    }
    if (temperature && cJSON_IsNumber(temperature)) {
        cJSON_AddNumberToObject(inference_config, "temperature", temperature->valuedouble);
        has_inference_config = 1;
    }
    if (top_p && cJSON_IsNumber(top_p)) {
        cJSON_AddNumberToObject(inference_config, "topP", top_p->valuedouble);
        has_inference_config = 1;
    }

    if (has_inference_config) {
        cJSON_AddItemToObject(converse, "inferenceConfig", inference_config);
    } else {
        cJSON_Delete(inference_config);
    }

    /* === Build toolConfig === */
    if (tools && cJSON_IsArray(tools) && cJSON_GetArraySize(tools) > 0) {
        cJSON *tool_config = convert_tools_to_converse(tools);
        if (tool_config) {
            cJSON_AddItemToObject(converse, "toolConfig", tool_config);
            LOG_DEBUG("Added toolConfig with %d tools",
                     cJSON_GetArraySize(cJSON_GetObjectItem(tool_config, "tools")));
        }
    }

    /* Generate result JSON */
    char *result = cJSON_PrintUnformatted(converse);

    LOG_DEBUG("Converse request created, length: %zu bytes", result ? strlen(result) : 0);
    LOG_DEBUG("=== CONVERSE REQUEST CONVERSION END ===");

    cJSON_Delete(openai);
    cJSON_Delete(converse);

    return result;
}

cJSON* bedrock_converse_convert_response(const char *converse_response) {
    LOG_DEBUG("=== CONVERSE RESPONSE CONVERSION START ===");

    if (!converse_response) {
        LOG_ERROR("bedrock_converse_convert_response: null input");
        return NULL;
    }

    LOG_DEBUG("Converse response length: %zu bytes", strlen(converse_response));
    LOG_DEBUG("Response preview: %.200s", converse_response);

    cJSON *converse = cJSON_Parse(converse_response);
    if (!converse) {
        LOG_ERROR("Failed to parse Converse response JSON");
        return NULL;
    }

    cJSON *openai = cJSON_CreateObject();
    if (!openai) {
        cJSON_Delete(converse);
        LOG_ERROR("Failed to create OpenAI response object");
        return NULL;
    }

    /* Add standard OpenAI response fields */
    cJSON_AddStringToObject(openai, "id", "converse-request");
    cJSON_AddStringToObject(openai, "object", "chat.completion");
    time_t now = time(NULL);
    cJSON_AddNumberToObject(openai, "created", (double)now);
    cJSON_AddStringToObject(openai, "model", "bedrock-converse");

    /* === Build choices array === */
    cJSON *choices = cJSON_CreateArray();
    if (!choices) {
        cJSON_Delete(converse);
        cJSON_Delete(openai);
        return NULL;
    }

    cJSON *choice = cJSON_CreateObject();
    if (!choice) {
        cJSON_Delete(choices);
        cJSON_Delete(converse);
        cJSON_Delete(openai);
        return NULL;
    }
    cJSON_AddNumberToObject(choice, "index", 0);

    /* === Build message from output.message === */
    cJSON *message = cJSON_CreateObject();
    if (!message) {
        cJSON_Delete(choice);
        cJSON_Delete(choices);
        cJSON_Delete(converse);
        cJSON_Delete(openai);
        return NULL;
    }
    cJSON_AddStringToObject(message, "role", "assistant");

    cJSON *output = cJSON_GetObjectItem(converse, "output");
    cJSON *output_message = output ? cJSON_GetObjectItem(output, "message") : NULL;
    cJSON *content_blocks = output_message ? cJSON_GetObjectItem(output_message, "content") : NULL;

    /* Process content blocks */
    char *text_content = NULL;
    cJSON *tool_calls = NULL;

    if (content_blocks && cJSON_IsArray(content_blocks)) {
        int block_idx = 0;
        cJSON *block = NULL;
        cJSON_ArrayForEach(block, content_blocks) {
            /* Check for text block */
            cJSON *text = cJSON_GetObjectItem(block, "text");
            if (text && cJSON_IsString(text)) {
                text_content = text->valuestring;
                LOG_DEBUG("Content block %d: text (%zu chars)", block_idx, strlen(text_content));
            }

            /* Check for toolUse block */
            cJSON *tool_use = cJSON_GetObjectItem(block, "toolUse");
            if (tool_use) {
                if (!tool_calls) {
                    tool_calls = cJSON_CreateArray();
                }
                if (!tool_calls) continue;

                cJSON *tool_call = cJSON_CreateObject();
                if (!tool_call) continue;

                /* Extract toolUse fields */
                cJSON *tool_id = cJSON_GetObjectItem(tool_use, "toolUseId");
                cJSON *tool_name = cJSON_GetObjectItem(tool_use, "name");
                cJSON *tool_input = cJSON_GetObjectItem(tool_use, "input");

                if (tool_id && cJSON_IsString(tool_id)) {
                    cJSON_AddStringToObject(tool_call, "id", tool_id->valuestring);
                    LOG_DEBUG("Content block %d: toolUse id=%s", block_idx, tool_id->valuestring);
                }

                cJSON_AddStringToObject(tool_call, "type", "function");

                cJSON *function = cJSON_CreateObject();
                if (function) {
                    if (tool_name && cJSON_IsString(tool_name)) {
                        cJSON_AddStringToObject(function, "name", tool_name->valuestring);
                        LOG_DEBUG("  Tool name: %s", tool_name->valuestring);
                    }
                    if (tool_input) {
                        char *input_str = cJSON_PrintUnformatted(tool_input);
                        if (input_str) {
                            cJSON_AddStringToObject(function, "arguments", input_str);
                            LOG_DEBUG("  Arguments length: %zu", strlen(input_str));
                            free(input_str);
                        }
                    }
                    cJSON_AddItemToObject(tool_call, "function", function);
                }

                cJSON_AddItemToArray(tool_calls, tool_call);
            }

            block_idx++;
        }
    }

    /* Add content to message */
    if (text_content) {
        cJSON_AddStringToObject(message, "content", text_content);
    } else {
        cJSON_AddNullToObject(message, "content");
    }

    /* Add tool_calls if present */
    if (tool_calls && cJSON_GetArraySize(tool_calls) > 0) {
        cJSON_AddItemToObject(message, "tool_calls", tool_calls);
        LOG_DEBUG("Added %d tool calls to response", cJSON_GetArraySize(tool_calls));
    } else if (tool_calls) {
        cJSON_Delete(tool_calls);
    }

    cJSON_AddItemToObject(choice, "message", message);

    /* === Convert stopReason to finish_reason === */
    cJSON *stop_reason = cJSON_GetObjectItem(converse, "stopReason");
    const char *finish_reason = "stop";  /* default */

    if (stop_reason && cJSON_IsString(stop_reason)) {
        const char *reason = stop_reason->valuestring;
        LOG_DEBUG("Converse stopReason: %s", reason);

        if (strcmp(reason, "end_turn") == 0) {
            finish_reason = "stop";
        } else if (strcmp(reason, "tool_use") == 0) {
            finish_reason = "tool_calls";
        } else if (strcmp(reason, "max_tokens") == 0) {
            finish_reason = "length";
        } else if (strcmp(reason, "stop_sequence") == 0) {
            finish_reason = "stop";
        } else if (strcmp(reason, "content_filtered") == 0) {
            finish_reason = "content_filter";
        } else if (strcmp(reason, "guardrail_intervened") == 0) {
            finish_reason = "content_filter";
        } else {
            /* Pass through unknown reasons */
            finish_reason = reason;
        }
    }

    cJSON_AddStringToObject(choice, "finish_reason", finish_reason);
    LOG_DEBUG("OpenAI finish_reason: %s", finish_reason);

    cJSON_AddItemToArray(choices, choice);
    cJSON_AddItemToObject(openai, "choices", choices);

    /* === Convert usage === */
    cJSON *usage_converse = cJSON_GetObjectItem(converse, "usage");
    if (usage_converse) {
        cJSON *usage = cJSON_CreateObject();
        if (usage) {
            cJSON *input_tokens = cJSON_GetObjectItem(usage_converse, "inputTokens");
            cJSON *output_tokens = cJSON_GetObjectItem(usage_converse, "outputTokens");
            cJSON *total_tokens = cJSON_GetObjectItem(usage_converse, "totalTokens");
            cJSON *cache_read = cJSON_GetObjectItem(usage_converse, "cacheReadInputTokens");
            cJSON *cache_write = cJSON_GetObjectItem(usage_converse, "cacheWriteInputTokens");

            if (input_tokens && cJSON_IsNumber(input_tokens)) {
                cJSON_AddNumberToObject(usage, "prompt_tokens", input_tokens->valueint);
            }
            if (output_tokens && cJSON_IsNumber(output_tokens)) {
                cJSON_AddNumberToObject(usage, "completion_tokens", output_tokens->valueint);
            }
            if (total_tokens && cJSON_IsNumber(total_tokens)) {
                cJSON_AddNumberToObject(usage, "total_tokens", total_tokens->valueint);
            } else {
                /* Calculate total if not provided */
                int total = 0;
                if (input_tokens && cJSON_IsNumber(input_tokens)) total += input_tokens->valueint;
                if (output_tokens && cJSON_IsNumber(output_tokens)) total += output_tokens->valueint;
                cJSON_AddNumberToObject(usage, "total_tokens", total);
            }

            /* Preserve cache-related fields */
            if (cache_read && cJSON_IsNumber(cache_read)) {
                cJSON_AddNumberToObject(usage, "cache_read_input_tokens", cache_read->valueint);
                LOG_DEBUG("Cache read tokens: %d", cache_read->valueint);
            }
            if (cache_write && cJSON_IsNumber(cache_write)) {
                cJSON_AddNumberToObject(usage, "cache_write_input_tokens", cache_write->valueint);
                LOG_DEBUG("Cache write tokens: %d", cache_write->valueint);
            }

            cJSON_AddItemToObject(openai, "usage", usage);
        }
    }

    LOG_DEBUG("=== CONVERSE RESPONSE CONVERSION END ===");
    LOG_DEBUG("Response has text=%s, tool_calls=%d, finish_reason=%s",
             text_content ? "yes" : "no",
             tool_calls ? cJSON_GetArraySize(tool_calls) : 0,
             finish_reason);

    cJSON_Delete(converse);
    return openai;
}
