/*
 * openai_chat_parser.h - Shared parsing helpers for OpenAI chat-completions responses
 */

#ifndef OPENAI_CHAT_PARSER_H
#define OPENAI_CHAT_PARSER_H

#include "klawed_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

int openai_tool_arguments_are_valid_json(cJSON *arguments);
int openai_fill_api_response_from_message(ApiResponse *api_response, cJSON *message,
                                          const char *log_prefix);
ApiResponse* openai_parse_chat_completion_response(cJSON *raw_json, const char *log_prefix);

#ifdef __cplusplus
}
#endif

#endif
