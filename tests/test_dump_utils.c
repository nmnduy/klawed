#include "dump_utils.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// Helper to capture output into a buffer using fmemopen
static int capture_dump(const char *json, char *buf, size_t buflen) {
    if (!buf || buflen == 0) {
        return 0;
    }
    memset(buf, 0, buflen);

    FILE *mem = fmemopen(buf, buflen, "w");
    if (!mem) {
        return 0;
    }

    int printed = dump_response_content(json, mem);
    fclose(mem);
    return printed;
}

static void test_anthropic_content_text(void) {
    const char *json =
        "{\"content\":[{\"type\":\"text\",\"text\":\"Hello there\"}]}";
    char buf[256];
    int printed = capture_dump(json, buf, sizeof(buf));
    assert(printed == 1);
    assert(strstr(buf, "Hello there") != NULL);
}

static void test_anthropic_tool_use(void) {
    const char *json =
        "{\"content\":[{\"type\":\"tool_use\",\"name\":\"bash\",\"id\":\"call_1\"}]}";
    char buf[256];
    int printed = capture_dump(json, buf, sizeof(buf));
    assert(printed == 1);
    assert(strstr(buf, "[TOOL_USE: bash") != NULL);
    assert(strstr(buf, "call_1") != NULL);
}

static void test_openai_string_content(void) {
    const char *json =
        "{\"choices\":[{\"message\":{\"content\":\"Hi from OpenAI\"}}]}";
    char buf[256];
    int printed = capture_dump(json, buf, sizeof(buf));
    assert(printed == 1);
    assert(strstr(buf, "Hi from OpenAI") != NULL);
}

static void test_openai_array_content(void) {
    const char *json =
        "{\"choices\":[{\"message\":{\"content\":[{\"type\":\"text\",\"text\":\"Chunk A\"},{\"type\":\"text\",\"text\":\"Chunk B\"}]}}]}";
    char buf[256];
    int printed = capture_dump(json, buf, sizeof(buf));
    assert(printed == 1);
    assert(strstr(buf, "Chunk A") != NULL);
    assert(strstr(buf, "Chunk B") != NULL);
}

static void test_openai_tool_calls(void) {
    const char *json =
        "{\"choices\":[{\"message\":{\"tool_calls\":[{\"type\":\"function\",\"function\":{\"name\":\"bash\"}}]}}]}";
    char buf[256];
    int printed = capture_dump(json, buf, sizeof(buf));
    assert(printed == 1);
    assert(strstr(buf, "[TOOL_USE") != NULL);
    assert(strstr(buf, "bash") != NULL);
}

static void test_invalid_json_returns_zero(void) {
    const char *json = "{not valid";
    char buf[256];
    int printed = capture_dump(json, buf, sizeof(buf));
    assert(printed == 0);
}

// ============================================================================
// Tests for dump_api_call_json
// ============================================================================

// Helper to capture JSON dump output
static int capture_json_dump(
    const char *timestamp,
    const char *request_json,
    const char *response_json,
    const char *model,
    const char *status,
    const char *error_msg,
    int call_num,
    char *buf,
    size_t buflen
) {
    if (!buf || buflen == 0) {
        return 0;
    }
    memset(buf, 0, buflen);

    FILE *mem = fmemopen(buf, buflen, "w");
    if (!mem) {
        return 0;
    }

    int result = dump_api_call_json(timestamp, request_json, response_json,
                                    model, status, error_msg, call_num, mem);
    fclose(mem);
    return result;
}

static void test_json_dump_basic(void) {
    char buf[2048];
    int result = capture_json_dump(
        "2026-01-07 10:30:00",
        "{\"messages\":[{\"role\":\"user\",\"content\":\"Hello\"}]}",
        "{\"content\":[{\"type\":\"text\",\"text\":\"Hi there!\"}]}",
        "claude-3-opus",
        "success",
        NULL,
        1,
        buf,
        sizeof(buf)
    );

    assert(result == 1);
    assert(strstr(buf, "\"timestamp\"") != NULL);
    assert(strstr(buf, "2026-01-07 10:30:00") != NULL);
    assert(strstr(buf, "\"model\"") != NULL);
    assert(strstr(buf, "claude-3-opus") != NULL);
    assert(strstr(buf, "\"status\"") != NULL);
    assert(strstr(buf, "success") != NULL);
    assert(strstr(buf, "\"request\"") != NULL);
    assert(strstr(buf, "\"response\"") != NULL);
}

static void test_json_dump_with_error(void) {
    char buf[2048];
    int result = capture_json_dump(
        "2026-01-07 10:30:00",
        "{\"messages\":[]}",
        NULL,
        "gpt-4",
        "error",
        "Rate limit exceeded",
        1,
        buf,
        sizeof(buf)
    );

    assert(result == 1);
    assert(strstr(buf, "\"status\"") != NULL);
    assert(strstr(buf, "error") != NULL);
    assert(strstr(buf, "\"error_message\"") != NULL);
    assert(strstr(buf, "Rate limit exceeded") != NULL);
}

static void test_json_dump_null_out_returns_zero(void) {
    int result = dump_api_call_json(
        "2026-01-07 10:30:00",
        "{\"messages\":[]}",
        "{\"content\":[]}",
        "claude-3-opus",
        "success",
        NULL,
        1,
        NULL  // NULL output file
    );

    assert(result == 0);
}

static void test_json_dump_null_params(void) {
    char buf[2048];
    int result = capture_json_dump(
        NULL,  // NULL timestamp
        NULL,  // NULL request
        NULL,  // NULL response
        NULL,  // NULL model
        NULL,  // NULL status
        NULL,  // NULL error_msg
        1,
        buf,
        sizeof(buf)
    );

    // Should still succeed, just with fewer fields
    assert(result == 1);
    // Buffer should have valid JSON (opening brace at minimum)
    assert(buf[0] == '{');
}

// ============================================================================
// Tests for dump_api_call_markdown
// ============================================================================

// Helper to capture Markdown dump output
static int capture_markdown_dump(
    const char *timestamp,
    const char *request_json,
    const char *response_json,
    const char *model,
    const char *status,
    const char *error_msg,
    int call_num,
    char *buf,
    size_t buflen
) {
    if (!buf || buflen == 0) {
        return 0;
    }
    memset(buf, 0, buflen);

    FILE *mem = fmemopen(buf, buflen, "w");
    if (!mem) {
        return 0;
    }

    int result = dump_api_call_markdown(timestamp, request_json, response_json,
                                        model, status, error_msg, call_num, mem);
    fclose(mem);
    return result;
}

static void test_markdown_dump_basic(void) {
    char buf[4096];
    int result = capture_markdown_dump(
        "2026-01-07 10:30:00",
        "{\"messages\":[{\"role\":\"user\",\"content\":\"Hello\"}]}",
        "{\"content\":[{\"type\":\"text\",\"text\":\"Hi there!\"}]}",
        "claude-3-opus",
        "success",
        NULL,
        1,
        buf,
        sizeof(buf)
    );

    assert(result == 1);
    assert(strstr(buf, "## Call 1") != NULL);
    assert(strstr(buf, "2026-01-07 10:30:00") != NULL);
    assert(strstr(buf, "**Model:** claude-3-opus") != NULL);
    assert(strstr(buf, "**Status:** success") != NULL);
    assert(strstr(buf, "### Request") != NULL);
    assert(strstr(buf, "#### user") != NULL);
    assert(strstr(buf, "Hello") != NULL);
    assert(strstr(buf, "### Response") != NULL);
    assert(strstr(buf, "Hi there!") != NULL);
    assert(strstr(buf, "---") != NULL);  // Separator at end
}

static void test_markdown_dump_with_error(void) {
    char buf[4096];
    int result = capture_markdown_dump(
        "2026-01-07 10:30:00",
        "{\"messages\":[]}",
        NULL,
        "gpt-4",
        "error",
        "Rate limit exceeded",
        2,
        buf,
        sizeof(buf)
    );

    assert(result == 1);
    assert(strstr(buf, "## Call 2") != NULL);
    // Error message appears in the header section
    assert(strstr(buf, "**Error:** Rate limit exceeded") != NULL);
}

static void test_markdown_dump_error_with_response(void) {
    char buf[4096];
    // When there's both an error status AND a response_json, error shows in Response section
    int result = capture_markdown_dump(
        "2026-01-07 10:30:00",
        "{\"messages\":[]}",
        "{\"error\":\"API error\"}",  // Some response body
        "gpt-4",
        "error",
        "Rate limit exceeded",
        2,
        buf,
        sizeof(buf)
    );

    assert(result == 1);
    assert(strstr(buf, "## Call 2") != NULL);
    assert(strstr(buf, "**Error:** Rate limit exceeded") != NULL);
    assert(strstr(buf, "### Response") != NULL);
    assert(strstr(buf, "**ERROR:** Rate limit exceeded") != NULL);
}

static void test_markdown_dump_tool_use(void) {
    char buf[4096];
    const char *request_json =
        "{\"messages\":[{\"role\":\"assistant\",\"content\":[{\"type\":\"tool_use\",\"name\":\"bash\",\"id\":\"call_123\"}]}]}";

    int result = capture_markdown_dump(
        "2026-01-07 10:30:00",
        request_json,
        NULL,
        "claude-3-opus",
        "success",
        NULL,
        1,
        buf,
        sizeof(buf)
    );

    assert(result == 1);
    assert(strstr(buf, "**[TOOL_USE: bash") != NULL);
    assert(strstr(buf, "call_123") != NULL);
}

static void test_markdown_dump_tool_result(void) {
    char buf[4096];
    const char *request_json =
        "{\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"tool_result\",\"tool_use_id\":\"call_123\"}]}]}";

    int result = capture_markdown_dump(
        "2026-01-07 10:30:00",
        request_json,
        NULL,
        "claude-3-opus",
        "success",
        NULL,
        1,
        buf,
        sizeof(buf)
    );

    assert(result == 1);
    assert(strstr(buf, "**[TOOL_RESULT for call_123]**") != NULL);
}

static void test_markdown_dump_null_out_returns_zero(void) {
    int result = dump_api_call_markdown(
        "2026-01-07 10:30:00",
        "{\"messages\":[]}",
        "{\"content\":[]}",
        "claude-3-opus",
        "success",
        NULL,
        1,
        NULL  // NULL output file
    );

    assert(result == 0);
}

static void test_markdown_dump_null_params(void) {
    char buf[2048];
    int result = capture_markdown_dump(
        NULL,  // NULL timestamp
        NULL,  // NULL request
        NULL,  // NULL response
        NULL,  // NULL model
        NULL,  // NULL status
        NULL,  // NULL error_msg
        1,
        buf,
        sizeof(buf)
    );

    // Should still succeed with "unknown" defaults
    assert(result == 1);
    assert(strstr(buf, "## Call 1 - unknown") != NULL);
    assert(strstr(buf, "**Model:** unknown") != NULL);
    assert(strstr(buf, "**Status:** unknown") != NULL);
}

int main(void) {
    // dump_response_content tests
    test_anthropic_content_text();
    test_anthropic_tool_use();
    test_openai_string_content();
    test_openai_array_content();
    test_openai_tool_calls();
    test_invalid_json_returns_zero();

    // dump_api_call_json tests
    test_json_dump_basic();
    test_json_dump_with_error();
    test_json_dump_null_out_returns_zero();
    test_json_dump_null_params();

    // dump_api_call_markdown tests
    test_markdown_dump_basic();
    test_markdown_dump_with_error();
    test_markdown_dump_error_with_response();
    test_markdown_dump_tool_use();
    test_markdown_dump_tool_result();
    test_markdown_dump_null_out_returns_zero();
    test_markdown_dump_null_params();

    printf("All dump_utils tests passed.\n");
    return 0;
}
