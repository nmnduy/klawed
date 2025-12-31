/*
 * deepseek_response_parser.c - DeepSeek API response parser implementation
 */

#define _POSIX_C_SOURCE 200809L

#include "deepseek_response_parser.h"
#include "logger.h"
#include <string.h>
#include <ctype.h>
#include <bsd/string.h>

/**
 * Check if a string is valid JSON (has balanced braces/brackets and quotes)
 * 
 * This is a simple heuristic to detect incomplete JSON strings.
 */
static int is_valid_json_string(const char *str) {
    if (!str || !*str) {
        return 0;
    }
    
    int brace_depth = 0;
    int bracket_depth = 0;
    int in_string = 0;
    char string_char = 0;
    
    for (const char *p = str; *p; p++) {
        if (in_string) {
            if (*p == '\\' && *(p + 1)) {
                p++;  // Skip escaped character
                continue;
            }
            if (*p == string_char) {
                in_string = 0;
                string_char = 0;
            }
            continue;
        }
        
        switch (*p) {
            case '{':
                brace_depth++;
                break;
            case '}':
                if (brace_depth <= 0) {
                    return 0;  // Unmatched closing brace
                }
                brace_depth--;
                break;
            case '[':
                bracket_depth++;
                break;
            case ']':
                if (bracket_depth <= 0) {
                    return 0;  // Unmatched closing bracket
                }
                bracket_depth--;
                break;
            case '"':
            case '\'':
                in_string = 1;
                string_char = *p;
                break;
            default:
                // Other characters are fine
                break;
        }
    }
    
    // JSON is valid if we're not in a string and all braces/brackets are balanced
    return !in_string && brace_depth == 0 && bracket_depth == 0;
}

/**
 * Check if JSON arguments string is incomplete
 * 
 * Looks for common signs of truncation:
 * 1. Missing closing brace/bracket
 * 2. Unclosed string
 * 3. Ends with incomplete escape sequence
 */
static int is_incomplete_json_args(const char *args) {
    if (!args || !*args) {
        return 0;  // Empty args, not incomplete
    }
    
    // First check with our simple validator
    if (is_valid_json_string(args)) {
        return 0;  // Valid JSON
    }
    
    // Additional checks for Write tool specific patterns
    // Write tool arguments should be: {"file_path": "...", "content": "..."}
    // If it's truncated, it might end in the middle of content
    
    // Check if it looks like it ends in the middle of a string
    size_t len = strlen(args);
    if (len > 0) {
        // Count quotes from the end
        int quote_count = 0;
        for (size_t i = len; i > 0; i--) {
            if (args[i - 1] == '"') {
                // Check if it's escaped
                if (i > 1 && args[i - 2] == '\\') {
                    continue;  // Escaped quote
                }
                quote_count++;
            }
        }
        
        // If odd number of unescaped quotes, we're in the middle of a string
        if (quote_count % 2 == 1) {
            return 1;
        }
        
        // Check if ends with backslash (incomplete escape)
        if (args[len - 1] == '\\') {
            return 1;
        }
        
        // Check if ends with comma (incomplete object)
        if (args[len - 1] == ',') {
            return 1;
        }
        
        // Check if ends with colon (incomplete key-value pair)
        if (args[len - 1] == ':') {
            return 1;
        }
    }
    
    // If we got here and JSON isn't valid, assume it's incomplete
    return 1;
}

int deepseek_has_incomplete_write_tool(const cJSON *raw_response, const ApiResponse *api_response) {
    if (!raw_response || !api_response) {
        return 0;
    }
    
    // Check finish_reason
    cJSON *choices = cJSON_GetObjectItem(raw_response, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        return 0;
    }
    
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
    if (!finish_reason || !cJSON_IsString(finish_reason) || 
        strcmp(finish_reason->valuestring, "length") != 0) {
        return 0;  // Not a length-limited response
    }
    
    // Check if we have Write tool calls
    for (int i = 0; i < api_response->tool_count; i++) {
        const ToolCall *tool = &api_response->tools[i];
        if (!tool->name || strcmp(tool->name, "Write") != 0) {
            continue;  // Not a Write tool
        }
        
        // Check if parameters exist
        if (!tool->parameters) {
            continue;
        }
        
        // Get the original arguments string from raw response
        // We need to look in the raw response because the parsed parameters
        // might be NULL if JSON parsing failed
        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (!message) {
            continue;
        }
        
        cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
        if (!tool_calls || !cJSON_IsArray(tool_calls)) {
            continue;
        }
        
        // Find the corresponding tool call in raw response
        for (int j = 0; j < cJSON_GetArraySize(tool_calls); j++) {
            cJSON *raw_tool_call = cJSON_GetArrayItem(tool_calls, j);
            cJSON *function = cJSON_GetObjectItem(raw_tool_call, "function");
            if (!function) {
                continue;
            }
            
            cJSON *name = cJSON_GetObjectItem(function, "name");
            if (!name || !cJSON_IsString(name) || strcmp(name->valuestring, "Write") != 0) {
                continue;
            }
            
            cJSON *arguments = cJSON_GetObjectItem(function, "arguments");
            if (!arguments || !cJSON_IsString(arguments)) {
                continue;
            }
            
            // Check if arguments JSON is incomplete
            if (is_incomplete_json_args(arguments->valuestring)) {
                LOG_WARN("Detected incomplete Write tool arguments (finish_reason: 'length')");
                return 1;
            }
        }
    }
    
    return 0;
}



char* deepseek_build_continuation_prompt(const ToolCall *tool_call, const char *incomplete_args) {
    if (!tool_call || !tool_call->name || !incomplete_args) {
        return NULL;
    }
    
    // Only handle Write tool for now
    if (strcmp(tool_call->name, "Write") != 0) {
        return NULL;
    }
    
    // Build continuation prompt
    // We need to ask the API to continue the JSON payload
    // Using a fixed format string to avoid format-nonliteral warning
    size_t prompt_len = 0;
    
    // Calculate total length needed
    const char *prefix = "The previous Write tool call was cut off due to token limits. "
                         "Please continue the JSON arguments from where it left off.\n\n"
                         "The incomplete arguments are:\n"
                         "```json\n";
    const char *middle = "\n"
                         "```\n\n"
                         "Continue the JSON to complete the Write tool call. "
                         "Only output the continuation of the JSON arguments, starting from where it was cut off. "
                         "Do not repeat the beginning, just continue from the cut point.\n\n"
                         "For example, if the arguments were cut off like:\n"
                         "{\"file_path\": \"test.txt\", \"content\": \"This is some text that was cut\"\n"
                         "\n"
                         "You should output:\n"
                         "off. Here is more text...\"}\n\n"
                         "Now continue the actual incomplete arguments above:";
    
    prompt_len = strlen(prefix) + strlen(incomplete_args) + strlen(middle) + 1;
    char *prompt = malloc(prompt_len);
    if (!prompt) {
        LOG_ERROR("Failed to allocate continuation prompt");
        return NULL;
    }
    
    // Build the prompt manually to avoid format-nonliteral warning
    char *ptr = prompt;
    memcpy(ptr, prefix, strlen(prefix));
    ptr += strlen(prefix);
    memcpy(ptr, incomplete_args, strlen(incomplete_args));
    ptr += strlen(incomplete_args);
    memcpy(ptr, middle, strlen(middle));
    ptr += strlen(middle);
    *ptr = '\0';
    
    return prompt;
}

int deepseek_merge_continuation_response(const ApiResponse *continuation_response, 
                                        ToolCall *original_tool_call) {
    if (!continuation_response || !original_tool_call) {
        return 0;
    }
    
    // The continuation response should contain text with the JSON continuation
    if (!continuation_response->message.text || !*continuation_response->message.text) {
        LOG_ERROR("Continuation response has no text content");
        return 0;
    }
    
    // For now, we'll handle this at a higher level
    // The actual merging needs to happen in the tool call processing code
    // This function just validates that we have a valid continuation
    
    LOG_DEBUG("Received continuation response: %s", continuation_response->message.text);
    return 1;
}

int deepseek_should_handle_incomplete_payload(const char *api_url, const cJSON *raw_response) {
    if (!api_url || !raw_response) {
        return 0;
    }
    
    // Check if using DeepSeek API
    if (!is_deepseek_api_url(api_url)) {
        return 0;
    }
    
    // Check finish_reason
    cJSON *choices = cJSON_GetObjectItem(raw_response, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        return 0;
    }
    
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
    if (!finish_reason || !cJSON_IsString(finish_reason) || 
        strcmp(finish_reason->valuestring, "length") != 0) {
        return 0;
    }
    
    // Check if there are tool calls
    cJSON *message = cJSON_GetObjectItem(choice, "message");
    if (!message) {
        return 0;
    }
    
    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    if (!tool_calls || !cJSON_IsArray(tool_calls) || cJSON_GetArraySize(tool_calls) == 0) {
        return 0;
    }
    
    // Check if any tool call is a Write tool
    for (int i = 0; i < cJSON_GetArraySize(tool_calls); i++) {
        cJSON *tool_call = cJSON_GetArrayItem(tool_calls, i);
        cJSON *function = cJSON_GetObjectItem(tool_call, "function");
        if (!function) {
            continue;
        }
        
        cJSON *name = cJSON_GetObjectItem(function, "name");
        if (name && cJSON_IsString(name) && strcmp(name->valuestring, "Write") == 0) {
            return 1;
        }
    }
    
    return 0;
}
