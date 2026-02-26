/*
 * perpetual_mode.c - Top-level entry point for perpetual mode
 *
 * Perpetual mode runs the agent with Bash-only access, using a persistent
 * markdown log to carry context across sessions.  The agent reads the log,
 * spawns subagents via Bash, then writes a structured PERPETUAL_SUMMARY block
 * that this module parses and appends to the log.
 */

#include "perpetual_mode.h"
#include "perpetual_log.h"
#include "perpetual_prompt.h"
#include "../klawed_internal.h"
#include "../data_dir.h"
#include "../logger.h"
#include "../tools/tool_bash.h"

#include <bsd/string.h>
#include <bsd/stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tools disabled in perpetual mode.
 * Kept: Bash, Subagent, CheckSubagentProgress, InterruptSubagent, Sleep.
 * Bash  — recon/grep the log, exec child processes
 * Subagent & friends — spawn and manage full action-taking subagents
 * Sleep — wait between subagent progress polls */
#define PERPETUAL_DISABLED_TOOLS \
    "Read,Write,Edit,MultiEdit,Glob,Grep,UploadImage," \
    "TodoWrite,MemoryStore,MemoryRecall,MemorySearch"

/* Maximum iterations for the API response loop (safety bound). */
#define MAX_LOOP_ITERATIONS 200

/* ---------------------------------------------------------------------------
 * PERPETUAL_SUMMARY parser
 * -------------------------------------------------------------------------*/

/*
 * Duplicate a trimmed line value (text after the first ": ").
 * Returns a malloc'd string, or NULL on error / missing prefix.
 */
static char *dup_field_value(const char *line, const char *prefix)
{
    size_t plen = strlen(prefix);
    if (strncmp(line, prefix, plen) != 0) {
        return NULL;
    }
    const char *val = line + plen;
    size_t vlen = strlen(val);
    char *result = reallocarray(NULL, vlen + 1, sizeof(char));
    if (!result) {
        return NULL;
    }
    strlcpy(result, val, vlen + 1);
    return result;
}

/*
 * parse_perpetual_summary — extract fields from the PERPETUAL_SUMMARY block.
 *
 * Finds the block between "PERPETUAL_SUMMARY:\n" and "END_PERPETUAL_SUMMARY".
 * Sets *request, *summary, *files, *commit to malloc'd strings (caller frees).
 * files and commit are set to NULL when the value is "none".
 *
 * Returns 1 if the block was found and all required fields parsed, 0 otherwise.
 */
static int parse_perpetual_summary(const char *text,
                                   char **request,
                                   char **summary,
                                   char **files,
                                   char **commit)
{
    if (!text || !request || !summary || !files || !commit) {
        return 0;
    }

    *request = NULL;
    *summary = NULL;
    *files   = NULL;
    *commit  = NULL;

    const char *start_marker = "PERPETUAL_SUMMARY:\n";
    const char *end_marker   = "END_PERPETUAL_SUMMARY";

    const char *block_start = strstr(text, start_marker);
    if (!block_start) {
        return 0;
    }
    block_start += strlen(start_marker);

    const char *block_end = strstr(block_start, end_marker);
    if (!block_end) {
        return 0;
    }

    /* Copy block into a working buffer so we can tokenise with strtok_r. */
    size_t block_len = (size_t)(block_end - block_start);
    char *buf = reallocarray(NULL, block_len + 1, sizeof(char));
    if (!buf) {
        return 0;
    }
    strlcpy(buf, block_start, block_len + 1);

    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line != NULL) {
        if (!*request) {
            char *v = dup_field_value(line, "Request: ");
            if (v) {
                *request = v;
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }
        }
        if (!*summary) {
            char *v = dup_field_value(line, "Summary: ");
            if (v) {
                *summary = v;
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }
        }
        if (!*files) {
            char *v = dup_field_value(line, "Files: ");
            if (v) {
                /* Treat "none" as NULL */
                if (strcmp(v, "none") == 0) {
                    free(v);
                } else {
                    *files = v;
                }
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }
        }
        if (!*commit) {
            char *v = dup_field_value(line, "Commit: ");
            if (v) {
                /* Treat "none" as NULL */
                if (strcmp(v, "none") == 0) {
                    free(v);
                } else {
                    *commit = v;
                }
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(buf);

    /* Both request and summary are required. */
    if (!*request || !*summary) {
        free(*request);
        free(*summary);
        free(*files);
        free(*commit);
        *request = NULL;
        *summary = NULL;
        *files   = NULL;
        *commit  = NULL;
        return 0;
    }

    return 1;
}

/* ---------------------------------------------------------------------------
 * Tool-call dispatch helper
 * -------------------------------------------------------------------------*/

/*
 * dispatch_bash_tool_calls — execute all Bash tool calls in response and add
 * their results back to the conversation state.
 *
 * Returns the number of Bash tool calls dispatched (may be 0 for text-only
 * responses), or -1 on a fatal allocation error.
 */
static int dispatch_bash_tool_calls(ConversationState *state,
                                    ApiResponse *response)
{
    if (!response || response->tool_count == 0) {
        return 0;
    }

    int bash_count = 0;

    /* Allocate result array for all tool calls. */
    InternalContent *results = reallocarray(NULL,
                                            (size_t)response->tool_count,
                                            sizeof(InternalContent));
    if (!results) {
        LOG_ERROR("perpetual: OOM allocating tool results");
        return -1;
    }

    for (int i = 0; i < response->tool_count; i++) {
        ToolCall *tc = &response->tools[i];
        InternalContent *res = &results[i];

        /* Zero-initialise each result slot. */
        *res = (InternalContent){0};
        res->type      = INTERNAL_TOOL_RESPONSE;
        res->tool_id   = tc->id;   /* borrowed — owned by response */
        res->tool_name = tc->name; /* borrowed — owned by response */

        if (strcmp(tc->name, "Bash") == 0) {
            LOG_DEBUG("perpetual: dispatching Bash tool call id=%s", tc->id);
            cJSON *tool_result = tool_bash(tc->parameters, state);
            res->tool_output = tool_result; /* may be NULL on OOM */
            res->is_error    = (tool_result == NULL) ? 1 : 0;
            bash_count++;
        } else {
            /* Non-Bash tool — return an error result. */
            LOG_WARN("perpetual: unexpected tool call '%s' (disabled)", tc->name);
            cJSON *err = cJSON_CreateObject();
            if (err) {
                cJSON_AddStringToObject(err, "error",
                    "Tool is disabled in perpetual mode; only Bash is available.");
            }
            res->tool_output = err;
            res->is_error    = 1;
        }
    }

    int add_rc = add_tool_results(state, results, response->tool_count);

    /* Free the tool_output values we own; do NOT free borrowed id/name. */
    for (int i = 0; i < response->tool_count; i++) {
        cJSON_Delete(results[i].tool_output);
        results[i].tool_output = NULL;
    }
    free(results);

    if (add_rc != 0) {
        LOG_ERROR("perpetual: add_tool_results failed");
        return -1;
    }

    return bash_count;
}

/* ---------------------------------------------------------------------------
 * Response loop
 * -------------------------------------------------------------------------*/

/*
 * run_response_loop — call the API repeatedly, dispatching Bash tool calls,
 * until the assistant produces a text-only (no-tool) response.
 *
 * last_text_out: set to the final assistant text (malloc'd, caller frees).
 * Returns 0 on success, 1 on error.
 */
static int run_response_loop(ConversationState *state, char **last_text_out)
{
    *last_text_out = NULL;

    for (int iteration = 0; iteration < MAX_LOOP_ITERATIONS; iteration++) {
        ApiResponse *response = call_api_with_retries(state);
        if (!response) {
            LOG_ERROR("perpetual: API call failed");
            return 1;
        }

        if (response->error_message) {
            LOG_ERROR("perpetual: API error: %s", response->error_message);
            api_response_free(response);
            return 1;
        }

        /* Capture assistant text from this turn. */
        if (response->message.text && response->message.text[0] != '\0') {
            free(*last_text_out);
            size_t tlen = strlen(response->message.text);
            *last_text_out = reallocarray(NULL, tlen + 1, sizeof(char));
            if (*last_text_out) {
                strlcpy(*last_text_out, response->message.text, tlen + 1);
            }
        }

        int tool_count = response->tool_count;

        if (tool_count > 0) {
            int dispatched = dispatch_bash_tool_calls(state, response);
            api_response_free(response);
            if (dispatched < 0) {
                return 1;
            }
            /* Continue loop to get the next assistant turn. */
        } else {
            /* No tool calls — conversation is complete. */
            api_response_free(response);
            break;
        }
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

int perpetual_mode_run(ConversationState *state, const char *query)
{
    if (!state || !query) {
        LOG_ERROR("perpetual_mode_run: NULL state or query");
        return 1;
    }

    /* 1. Resolve log path. */
    char *log_path = perpetual_log_get_path(data_dir_get_base());
    if (!log_path) {
        LOG_ERROR("perpetual_mode_run: failed to resolve log path");
        return 1;
    }

    /* 2. Restrict tools to Bash only. */
    if (setenv("KLAWED_DISABLE_TOOLS", PERPETUAL_DISABLED_TOOLS, 1) != 0) {
        LOG_WARN("perpetual_mode_run: setenv KLAWED_DISABLE_TOOLS failed");
    }

    /* 3. Build system prompt. */
    long log_size = perpetual_log_size(log_path);
    char *system_prompt = perpetual_prompt_build(log_path, log_size);
    if (!system_prompt) {
        LOG_ERROR("perpetual_mode_run: failed to build system prompt");
        free(log_path);
        return 1;
    }

    /* 4. Set system prompt on state. */
    add_system_message(state, system_prompt);
    free(system_prompt);
    system_prompt = NULL;

    /* 5. Add user message. */
    add_user_message(state, query);

    /* 6. Run the API loop until the assistant stops calling tools. */
    char *final_text = NULL;
    int loop_rc = run_response_loop(state, &final_text);
    if (loop_rc != 0) {
        free(final_text);
        free(log_path);
        return 1;
    }

    /* 7. Parse PERPETUAL_SUMMARY block from the final assistant text. */
    char *req_str   = NULL;
    char *sum_str   = NULL;
    char *files_str = NULL;
    char *commit_str = NULL;

    int parsed = final_text
                 ? parse_perpetual_summary(final_text,
                                           &req_str, &sum_str,
                                           &files_str, &commit_str)
                 : 0;

    if (!parsed) {
        LOG_WARN("perpetual_mode_run: PERPETUAL_SUMMARY block missing or malformed");
        /* Provide sensible defaults. */
        size_t qlen = strlen(query);
        req_str = reallocarray(NULL, qlen + 1, sizeof(char));
        if (req_str) {
            strlcpy(req_str, query, qlen + 1);
        }
        const char *fallback_sum = "Session completed (no summary provided)";
        size_t slen = strlen(fallback_sum);
        sum_str = reallocarray(NULL, slen + 1, sizeof(char));
        if (sum_str) {
            strlcpy(sum_str, fallback_sum, slen + 1);
        }
    }

    /* 8. Append session to log (best-effort; do not fail on log error). */
    if (req_str && sum_str) {
        int log_rc = perpetual_log_append(log_path,
                                          state->session_id,
                                          req_str,
                                          sum_str,
                                          files_str,
                                          commit_str);
        if (log_rc != 0) {
            LOG_WARN("perpetual_mode_run: failed to append to perpetual log");
        }
    }

    /* 9. Clean up. */
    free(req_str);
    free(sum_str);
    free(files_str);
    free(commit_str);
    free(final_text);
    free(log_path);

    return 0;
}
