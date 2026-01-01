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

int main(void) {
    test_anthropic_content_text();
    test_anthropic_tool_use();
    test_openai_string_content();
    test_openai_array_content();
    test_openai_tool_calls();
    test_invalid_json_returns_zero();
    printf("All dump_utils tests passed.\n");
    return 0;
}
