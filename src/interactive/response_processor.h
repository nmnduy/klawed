#ifndef RESPONSE_PROCESSOR_H
#define RESPONSE_PROCESSOR_H

#include "../klawed_internal.h"
#include "../api/api_response.h"
#include "../tui.h"
#include "../message_queue.h"
#include "../ai_worker.h"

/**
 * Process API response in interactive mode
 *
 * Handles assistant text content, tool calls, and recursive API calls.
 * Executes tools in parallel threads and processes follow-up responses.
 *
 * @param state         Conversation state
 * @param response      API response to process
 * @param tui           TUI state (NULL for non-TUI mode)
 * @param queue         Message queue for async updates (NULL for sync mode)
 * @param worker_ctx    AI worker context (NULL if not using worker thread)
 */
void process_response(ConversationState *state,
                     ApiResponse *response,
                     TUIState *tui,
                     TUIMessageQueue *queue,
                     AIWorkerContext *worker_ctx);

/**
 * AI worker instruction handler
 *
 * Called by AI worker thread to handle API calls and process responses.
 *
 * @param ctx           AI worker context
 * @param instruction   Instruction to execute
 */
void ai_worker_handle_instruction(AIWorkerContext *ctx, const AIInstruction *instruction);

#endif // RESPONSE_PROCESSOR_H
