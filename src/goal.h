#ifndef GOAL_H
#define GOAL_H

#include "klawed_internal.h"

/*
 * Persistent cross-turn goals (Ralph mode).
 *
 * A goal is a standing user objective that stays active across turns.
 * After each assistant turn completes, a judge call asks the model whether
 * the goal is satisfied. If not, a continuation prompt is fed back into the
 * same session and the agent keeps working until the goal is done, the turn
 * budget is exhausted, or the user pauses/clears it.
 *
 * State is stored in ConversationState->goal. It does not persist across
 * process restarts (unlike Hermes which uses SessionDB). This is acceptable
 * because klawed sessions are typically single-process.
 */

#define DEFAULT_GOAL_MAX_TURNS 20
#define GOAL_JUDGE_MAX_TOKENS 128

/* Goal status values */
#define GOAL_STATUS_ACTIVE   "active"
#define GOAL_STATUS_PAUSED   "paused"
#define GOAL_STATUS_DONE     "done"
#define GOAL_STATUS_CLEARED  "cleared"

typedef struct {
    char *text;           /* Goal description (owned) */
    char *status;         /* active | paused | done | cleared */
    int turns_used;       /* Turns consumed toward this goal */
    int max_turns;        /* Budget before auto-pause */
    time_t created_at;    /* When the goal was set */
    time_t last_turn_at;  /* Timestamp of last judged turn */
    char *last_verdict;   /* "done" | "continue" | "skipped" (owned, may be NULL) */
    char *last_reason;    /* Judge rationale (owned, may be NULL) */
} GoalState;

/*
 * Verdict returned by the judge
 */
typedef struct {
    int done;             /* 1 = goal achieved, 0 = keep going */
    char *reason;         /* Rationale (owned, caller must free) */
    int parse_failed;     /* 1 = judge output was not valid JSON */
} GoalVerdict;

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */

/* Allocate and initialize a new GoalState. Returns NULL on OOM. */
GoalState *goal_state_new(const char *text, int max_turns);

/* Free a GoalState and all owned strings. */
void goal_state_free(GoalState *goal);

/* ==========================================================================
 * State queries
 * ========================================================================== */

/* Returns 1 if goal exists and is active. */
int goal_is_active(const ConversationState *state);

/* Returns 1 if goal exists and is active or paused. */
int goal_has_goal(const ConversationState *state);

/* Build a status line for display. Caller must free the returned string. */
char *goal_status_line(const ConversationState *state);

/* ==========================================================================
 * Mutations
 * ========================================================================== */

/* Set a new goal on the conversation state. Replaces any existing goal. */
void goal_set(ConversationState *state, const char *text, int max_turns);

/* Pause the active goal. */
void goal_pause(ConversationState *state, const char *reason);

/* Resume a paused goal. Optionally reset turn budget. */
void goal_resume(ConversationState *state, int reset_budget);

/* Clear (remove) the goal. */
void goal_clear(ConversationState *state);

/* Mark goal as done. */
void goal_mark_done(ConversationState *state, const char *reason);

/* ==========================================================================
 * Judge
 * ========================================================================== */

/*
 * Ask the model whether the goal is satisfied by the last assistant response.
 *
 * Creates a temporary conversation with a minimal judge prompt and calls the
 * API. The judge model defaults to the same model as the main conversation;
 * override with KLAWED_GOAL_JUDGE_MODEL.
 *
 * Returns a GoalVerdict. Caller must free verdict.reason.
 * On any error (API failure, parse error), returns fail-open: done=0,
 * parse_failed=0 for API errors, parse_failed=1 for JSON parse errors.
 */
GoalVerdict goal_judge(const ConversationState *state, const char *last_response);

/* ==========================================================================
 * Continuation
 * ========================================================================== */

/*
 * Build the continuation user message for an active goal.
 * Caller must free the returned string.
 */
char *goal_build_continuation_prompt(const GoalState *goal);

/*
 * Extract the text of the last assistant message in the conversation.
 * Returns a pointer into the conversation state (do not free).
 * Returns "" if no assistant message found.
 */
const char *goal_get_last_assistant_text(const ConversationState *state);

#endif /* GOAL_H */
