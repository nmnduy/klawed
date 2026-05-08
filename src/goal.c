#include "goal.h"
#include "logger.h"
#include "api/api_client.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <cjson/cJSON.h>

/*
 * Judge prompts
 */
static const char *JUDGE_SYSTEM_PROMPT =
    "You are a strict judge evaluating whether an autonomous agent has "
    "achieved a user's stated goal. You receive the goal text and the "
    "agent's most recent response. Your only job is to decide whether "
    "the goal is fully satisfied based on that response.\n\n"
    "A goal is DONE only when:\n"
    "- The response explicitly confirms the goal was completed, OR\n"
    "- The response clearly shows the final deliverable was produced, OR\n"
    "- The response explains the goal is unachievable / blocked / needs "
    "user input (treat this as DONE with reason describing the block).\n\n"
    "Otherwise the goal is NOT done — CONTINUE.\n\n"
    "Reply ONLY with a single JSON object on one line:\n"
    "{\"done\": <true|false>, \"reason\": \"<one-sentence rationale>\"}";

static const char *JUDGE_USER_TEMPLATE =
    "Goal:\n%.*s\n\n"
    "Agent's most recent response:\n%.*s\n\n"
    "Is the goal satisfied?";

static const char *CONTINUATION_TEMPLATE =
    "[Continuing toward your standing goal]\n"
    "Goal: %s\n\n"
    "Continue working toward this goal. Take the next concrete step. "
    "If you believe the goal is complete, state so explicitly and stop. "
    "If you are blocked and need input from the user, say so clearly and stop.";

/* Maximum chars of goal and response to send to the judge */
#define JUDGE_GOAL_MAX_CHARS 2000
#define JUDGE_RESPONSE_MAX_CHARS 4000

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static char *strndup_safe(const char *s, size_t n) {
    if (!s) return strdup("");
    size_t len = strnlen(s, n);
    char *d = malloc(len + 1);
    if (!d) return NULL;
    memcpy(d, s, len);
    d[len] = '\0';
    return d;
}

static const char *str_find_case(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;
    if (nlen > hlen) return NULL;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j = 0;
        for (; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j]))
                break;
        }
        if (j == nlen) return haystack + i;
    }
    return NULL;
}

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */

GoalState *goal_state_new(const char *text, int max_turns) {
    if (!text || !text[0]) return NULL;
    GoalState *g = calloc(1, sizeof(GoalState));
    if (!g) return NULL;
    g->text = strdup(text);
    if (!g->text) {
        free(g);
        return NULL;
    }
    g->status = strdup(GOAL_STATUS_ACTIVE);
    if (!g->status) {
        free(g->text);
        free(g);
        return NULL;
    }
    g->turns_used = 0;
    g->max_turns = (max_turns > 0) ? max_turns : DEFAULT_GOAL_MAX_TURNS;
    g->created_at = time(NULL);
    g->last_turn_at = 0;
    g->last_verdict = NULL;
    g->last_reason = NULL;
    return g;
}

void goal_state_free(GoalState *goal) {
    if (!goal) return;
    free(goal->text);
    free(goal->status);
    free(goal->last_verdict);
    free(goal->last_reason);
    free(goal);
}

/* ==========================================================================
 * State queries
 * ========================================================================== */

int goal_is_active(const ConversationState *state) {
    return state && state->goal && strcmp(state->goal->status, GOAL_STATUS_ACTIVE) == 0;
}

int goal_has_goal(const ConversationState *state) {
    if (!state || !state->goal) return 0;
    return strcmp(state->goal->status, GOAL_STATUS_ACTIVE) == 0 ||
           strcmp(state->goal->status, GOAL_STATUS_PAUSED) == 0;
}

char *goal_status_line(const ConversationState *state) {
    if (!state || !state->goal) {
        return strdup("No active goal. Set one with /goal <text>.");
    }
    const GoalState *g = state->goal;
    if (strcmp(g->status, GOAL_STATUS_CLEARED) == 0) {
        return strdup("No active goal. Set one with /goal <text>.");
    }

    char buf[512];
    if (strcmp(g->status, GOAL_STATUS_ACTIVE) == 0) {
        snprintf(buf, sizeof(buf), "Goal (active, %d/%d turns): %s",
                 g->turns_used, g->max_turns, g->text);
    } else if (strcmp(g->status, GOAL_STATUS_PAUSED) == 0) {
        snprintf(buf, sizeof(buf), "Goal (paused, %d/%d turns): %s",
                 g->turns_used, g->max_turns, g->text);
    } else if (strcmp(g->status, GOAL_STATUS_DONE) == 0) {
        snprintf(buf, sizeof(buf), "Goal done (%d/%d turns): %s",
                 g->turns_used, g->max_turns, g->text);
    } else {
        snprintf(buf, sizeof(buf), "Goal (%s, %d/%d turns): %s",
                 g->status, g->turns_used, g->max_turns, g->text);
    }
    return strdup(buf);
}

/* ==========================================================================
 * Mutations
 * ========================================================================== */

void goal_set(ConversationState *state, const char *text, int max_turns) {
    if (!state || !text) return;
    if (state->goal) {
        goal_state_free(state->goal);
    }
    state->goal = goal_state_new(text, max_turns);
    if (state->goal) {
        LOG_INFO("Goal set (%d-turn budget): %s", state->goal->max_turns, state->goal->text);
    }
}

void goal_pause(ConversationState *state, const char *reason) {
    if (!state || !state->goal) return;
    free(state->goal->status);
    state->goal->status = strdup(GOAL_STATUS_PAUSED);
    LOG_INFO("Goal paused: %s", reason ? reason : "user-paused");
}

void goal_resume(ConversationState *state, int reset_budget) {
    if (!state || !state->goal) return;
    free(state->goal->status);
    state->goal->status = strdup(GOAL_STATUS_ACTIVE);
    if (reset_budget) {
        state->goal->turns_used = 0;
    }
    LOG_INFO("Goal resumed: %s", state->goal->text);
}

void goal_clear(ConversationState *state) {
    if (!state || !state->goal) return;
    goal_state_free(state->goal);
    state->goal = NULL;
    LOG_INFO("Goal cleared");
}

void goal_mark_done(ConversationState *state, const char *reason) {
    if (!state || !state->goal) return;
    free(state->goal->status);
    state->goal->status = strdup(GOAL_STATUS_DONE);
    free(state->goal->last_verdict);
    state->goal->last_verdict = strdup("done");
    free(state->goal->last_reason);
    state->goal->last_reason = reason ? strdup(reason) : NULL;
    LOG_INFO("Goal achieved: %s", reason ? reason : "(no reason)");
}

/* ==========================================================================
 * Judge parsing
 * ========================================================================== */

static GoalVerdict parse_judge_response(const char *raw) {
    GoalVerdict v = {0};
    v.reason = strdup("judge returned empty response");
    v.parse_failed = 1;

    if (!raw || !raw[0]) {
        return v;
    }

    const char *text = raw;
    /* Skip markdown fences */
    if (strncmp(text, "```", 3) == 0) {
        text += 3;
        while (*text && *text != '\n') text++;
        if (*text == '\n') text++;
    }

    /* Try to parse the whole thing as JSON */
    cJSON *obj = cJSON_Parse(text);
    if (!obj) {
        /* Try to find first JSON object with a regex-like scan */
        const char *brace = strchr(text, '{');
        if (brace) {
            const char *end = strchr(brace, '}');
            if (end) {
                char *subset = strndup_safe(brace, (size_t)(end - brace + 1));
                if (subset) {
                    obj = cJSON_Parse(subset);
                    free(subset);
                }
            }
        }
    }

    if (!obj) {
        free(v.reason);
        v.reason = strdup("judge reply was not JSON");
        v.parse_failed = 1;
        return v;
    }

    cJSON *done_item = cJSON_GetObjectItem(obj, "done");
    if (done_item) {
        if (cJSON_IsBool(done_item)) {
            v.done = cJSON_IsTrue(done_item) ? 1 : 0;
        } else if (cJSON_IsString(done_item)) {
            const char *ds = done_item->valuestring;
            v.done = (ds && (strcmp(ds, "true") == 0 || strcmp(ds, "yes") == 0 ||
                             strcmp(ds, "1") == 0 || strcmp(ds, "done") == 0));
        } else if (cJSON_IsNumber(done_item)) {
            v.done = done_item->valuedouble != 0.0;
        }
    }

    cJSON *reason_item = cJSON_GetObjectItem(obj, "reason");
    if (reason_item && cJSON_IsString(reason_item) && reason_item->valuestring) {
        free(v.reason);
        v.reason = strdup(reason_item->valuestring);
    } else {
        free(v.reason);
        v.reason = strdup("no reason provided");
    }

    v.parse_failed = 0;
    cJSON_Delete(obj);
    return v;
}

/* ==========================================================================
 * Judge API call
 * ========================================================================== */

GoalVerdict goal_judge(const ConversationState *state, const char *last_response) {
    GoalVerdict verdict = {0};
    verdict.reason = strdup("judge skipped");
    verdict.parse_failed = 0;

    if (!state || !state->goal || !last_response) {
        return verdict;
    }

    const GoalState *g = state->goal;
    if (!g->text || !g->text[0]) {
        free(verdict.reason);
        verdict.reason = strdup("empty goal");
        return verdict;
    }
    if (!last_response[0]) {
        free(verdict.reason);
        verdict.reason = strdup("empty response (nothing to evaluate)");
        return verdict;
    }

    /* Truncate inputs for the judge */
    char *goal_snip = strndup_safe(g->text, JUDGE_GOAL_MAX_CHARS);
    char *resp_snip = strndup_safe(last_response, JUDGE_RESPONSE_MAX_CHARS);
    if (!goal_snip || !resp_snip) {
        free(goal_snip);
        free(resp_snip);
        free(verdict.reason);
        verdict.reason = strdup("memory allocation failed");
        return verdict;
    }

    /* Build user prompt */
    size_t user_len = strlen(JUDGE_USER_TEMPLATE) + strlen(goal_snip) + strlen(resp_snip) + 64;
    char *user_prompt = malloc(user_len);
    if (!user_prompt) {
        free(goal_snip);
        free(resp_snip);
        free(verdict.reason);
        verdict.reason = strdup("memory allocation failed");
        return verdict;
    }
    snprintf(user_prompt, user_len, JUDGE_USER_TEMPLATE,
             (int)strlen(goal_snip), goal_snip,
             (int)strlen(resp_snip), resp_snip);

    /* Build temporary conversation state for judge */
    ConversationState *judge_state = calloc(1, sizeof(ConversationState));
    if (!judge_state) {
        free(user_prompt);
        free(goal_snip);
        free(resp_snip);
        free(verdict.reason);
        verdict.reason = strdup("memory allocation failed");
        return verdict;
    }

    /* Copy essential config from main state */
    judge_state->api_key = state->api_key;
    judge_state->api_url = state->api_url;
    judge_state->provider = state->provider;
    judge_state->max_tokens = GOAL_JUDGE_MAX_TOKENS;
    judge_state->max_retry_duration_ms = state->max_retry_duration_ms;
    judge_state->working_dir = state->working_dir;

    /* Override model if env var is set */
    const char *judge_model = getenv("KLAWED_GOAL_JUDGE_MODEL");
    if (judge_model && judge_model[0]) {
        judge_state->model = strdup(judge_model);
    } else {
        judge_state->model = state->model ? strdup(state->model) : NULL;
    }

    /* System message */
    judge_state->messages[0].role = MSG_SYSTEM;
    judge_state->messages[0].content_count = 1;
    judge_state->messages[0].contents = calloc(1, sizeof(InternalContent));
    if (judge_state->messages[0].contents) {
        judge_state->messages[0].contents[0].type = INTERNAL_TEXT;
        judge_state->messages[0].contents[0].text = strdup(JUDGE_SYSTEM_PROMPT);
    }

    /* User message */
    judge_state->messages[1].role = MSG_USER;
    judge_state->messages[1].content_count = 1;
    judge_state->messages[1].contents = calloc(1, sizeof(InternalContent));
    if (judge_state->messages[1].contents) {
        judge_state->messages[1].contents[0].type = INTERNAL_TEXT;
        judge_state->messages[1].contents[0].text = user_prompt; /* transfer ownership */
    } else {
        free(user_prompt);
    }

    judge_state->count = 2;

    LOG_INFO("Goal judge: evaluating turn %d/%d", g->turns_used, g->max_turns);

    ApiResponse *response = call_api_with_retries(judge_state);

    /* Clean up judge state (only free what we allocated) */
    for (int i = 0; i < judge_state->count; i++) {
        InternalMessage *msg = &judge_state->messages[i];
        for (int j = 0; j < msg->content_count; j++) {
            free(msg->contents[j].text);
        }
        free(msg->contents);
    }
    free(judge_state->model);
    free(judge_state);
    free(goal_snip);
    free(resp_snip);

    if (!response) {
        free(verdict.reason);
        verdict.reason = strdup("judge API call failed");
        verdict.parse_failed = 0; /* API error = transient, fail open */
        LOG_WARN("Goal judge: API call failed — failing open to continue");
        return verdict;
    }

    if (response->error_message) {
        free(verdict.reason);
        verdict.reason = strdup(response->error_message);
        verdict.parse_failed = 0;
        LOG_WARN("Goal judge: API error (%s) — failing open to continue", response->error_message);
        api_response_free(response);
        return verdict;
    }

    const char *raw = response->message.text ? response->message.text : "";
    LOG_DEBUG("Goal judge: raw response: %.200s", raw);

    verdict = parse_judge_response(raw);
    api_response_free(response);

    LOG_INFO("Goal judge: verdict=%s reason=%.120s parse_failed=%d",
             verdict.done ? "done" : "continue",
             verdict.reason ? verdict.reason : "(null)",
             verdict.parse_failed);
    return verdict;
}

/* ==========================================================================
 * Continuation
 * ========================================================================== */

char *goal_build_continuation_prompt(const GoalState *goal) {
    if (!goal || !goal->text) {
        return strdup("Continue working.");
    }
    size_t len = strlen(CONTINUATION_TEMPLATE) + strlen(goal->text) + 1;
    char *prompt = malloc(len);
    if (!prompt) return NULL;
    snprintf(prompt, len, CONTINUATION_TEMPLATE, goal->text);
    return prompt;
}

const char *goal_get_last_assistant_text(const ConversationState *state) {
    if (!state || state->count <= 0) return "";
    for (int i = state->count - 1; i >= 0; i--) {
        if (state->messages[i].role == MSG_ASSISTANT) {
            /* Find first text content block */
            for (int j = 0; j < state->messages[i].content_count; j++) {
                if (state->messages[i].contents[j].type == INTERNAL_TEXT &&
                    state->messages[i].contents[j].text) {
                    return state->messages[i].contents[j].text;
                }
            }
        }
    }
    return "";
}
