#ifndef ONESHOT_PROCESSOR_H
#define ONESHOT_PROCESSOR_H

#include "../klawed_internal.h"
#include "../api/api_response.h"

/**
 * Oneshot Response Processor
 *
 * Processes API responses in oneshot mode, handling tool execution
 * and recursive response processing until completion.
 */

/**
 * Process a single API response in oneshot mode
 * Recursively handles tool calls and follow-up responses
 *
 * @param state Conversation state
 * @param response API response to process
 * @param output_format Output format mode (HUMAN or MACHINE)
 * @return 0 on success, 1 on error
 */
int oneshot_process_response(ConversationState *state,
                              ApiResponse *response,
                              int output_format);

#endif // ONESHOT_PROCESSOR_H
