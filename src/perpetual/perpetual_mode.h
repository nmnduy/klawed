#ifndef PERPETUAL_MODE_H
#define PERPETUAL_MODE_H

#include "../klawed_internal.h"
#include "../ui/ui_output.h"

/*
 * Top-level entry point for perpetual mode.
 * Called from main() when KLAWED_PERPETUAL=1 or --perpetual flag.
 *
 * @param state      Initialized ConversationState
 * @param query      The user's request string (from argv or stdin)
 * @param tui_queue  TUI message queue for UI updates (may be NULL in one-shot mode)
 * @return 0 on success, 1 on error
 */
int perpetual_mode_run(ConversationState *state, const char *query,
                       TUIMessageQueue *tui_queue);

#endif /* PERPETUAL_MODE_H */
