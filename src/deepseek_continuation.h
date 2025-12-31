/*
 * deepseek_continuation.h - DeepSeek API continuation handling
 */

#ifndef DEEPSEEK_CONTINUATION_H
#define DEEPSEEK_CONTINUATION_H

#include "klawed_internal.h"  // For ConversationState, ToolCall

/**
 * Make a continuation API call for incomplete Write tool arguments
 * 
 * @param state The conversation state
 * @param tool The incomplete Write tool call
 * @param continuation_prompt The prompt asking for continuation
 * @return 1 on success, 0 on failure
 */
int deepseek_make_continuation_call(ConversationState *state, ToolCall *tool, const char *continuation_prompt);

#endif // DEEPSEEK_CONTINUATION_H
