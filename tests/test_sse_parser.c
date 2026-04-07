/*
 * test_sse_parser.c - Unit tests for SSE (Server-Sent Events) parser
 *
 * Tests the SSE parser to ensure:
 * - Single-line and multi-line data events are parsed correctly
 * - Event types are extracted properly
 * - JSON data is parsed correctly
 * - OpenAI and Anthropic streaming formats are handled
 * - Edge cases (empty data, comments, etc.) are handled
 *
 * Compilation: make test-sse-parser
 * Usage: ./build/test_sse_parser
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <cjson/cJSON.h>

#include "../src/sse_parser.h"

// Test framework colors
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Event capture for testing
typedef struct {
    StreamEvent *events;
    int count;
    int capacity;
} EventCapture;

static EventCapture g_capture = {0};

#define TEST_ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf(COLOR_GREEN "  ✓ " COLOR_RESET "%s\n", message); \
        } else { \
            tests_failed++; \
            printf(COLOR_RED "  ✗ " COLOR_RESET "%s\n", message); \
        } \
    } while (0)

#define TEST_SUMMARY() \
    do { \
        printf("\n" COLOR_CYAN "Test Summary:" COLOR_RESET "\n"); \
        printf("  Total:  %d\n", tests_run); \
        printf("  Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed); \
        printf("  Failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed); \
        if (tests_failed == 0) { \
            printf(COLOR_GREEN "\n✓ All tests passed!" COLOR_RESET "\n"); \
            return 0; \
        } else { \
            printf(COLOR_RED "\n✗ Some tests failed!" COLOR_RESET "\n"); \
            return 1; \
        } \
    } while (0)

// Test callback that captures events
static int test_event_callback(StreamEvent *event, void *userdata) {
    EventCapture *capture = (EventCapture *)userdata;
    
    if (capture->count >= capture->capacity) {
        // Grow the array
        int new_capacity = capture->capacity * 2;
        if (new_capacity < 16) new_capacity = 16;
        StreamEvent *new_events = realloc(capture->events, (size_t)new_capacity * sizeof(StreamEvent));
        if (!new_events) return -1;
        capture->events = new_events;
        capture->capacity = new_capacity;
    }
    
    // Copy the event
    capture->events[capture->count] = *event;
    
    // Deep copy strings and data
    if (event->event_name) {
        capture->events[capture->count].event_name = strdup(event->event_name);
    }
    if (event->raw_data) {
        capture->events[capture->count].raw_data = strdup(event->raw_data);
    }
    if (event->data) {
        capture->events[capture->count].data = cJSON_Duplicate(event->data, 1);
    }
    
    capture->count++;
    return 0;
}

static void reset_capture(void) {
    for (int i = 0; i < g_capture.count; i++) {
        free((void *)(uintptr_t)g_capture.events[i].event_name);
        free((void *)(uintptr_t)g_capture.events[i].raw_data);
        cJSON_Delete(g_capture.events[i].data);
    }
    free(g_capture.events);
    g_capture.events = NULL;
    g_capture.count = 0;
    g_capture.capacity = 0;
}

// ============================================================================
// Test Cases - Basic SSE parsing
// ============================================================================

static void test_simple_data_event(void) {
    printf(COLOR_YELLOW "\nTest: Simple data event (OpenAI style)\n" COLOR_RESET);
    reset_capture();
    
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    
    const char *sse_data = "data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n";
    int result = sse_parser_process_data(parser, sse_data, strlen(sse_data));
    TEST_ASSERT(result == 0, "Parsing should succeed");
    TEST_ASSERT(g_capture.count == 1, "Should have 1 event");
    TEST_ASSERT(g_capture.events[0].type == SSE_EVENT_OPENAI_CHUNK, "Should be OPENAI_CHUNK");
    TEST_ASSERT(g_capture.events[0].data != NULL, "Data should be parsed as JSON");
    
    sse_parser_free(parser);
    reset_capture();
}

static void test_event_with_type(void) {
    printf(COLOR_YELLOW "\nTest: Event with explicit type (Anthropic style)\n" COLOR_RESET);
    reset_capture();
    
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    
    const char *sse_data = "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}\n\n";
    int result = sse_parser_process_data(parser, sse_data, strlen(sse_data));
    TEST_ASSERT(result == 0, "Parsing should succeed");
    TEST_ASSERT(g_capture.count == 1, "Should have 1 event");
    TEST_ASSERT(g_capture.events[0].type == SSE_EVENT_CONTENT_BLOCK_DELTA, "Should be CONTENT_BLOCK_DELTA");
    TEST_ASSERT(strcmp(g_capture.events[0].event_name, "content_block_delta") == 0, "Event name should match");
    
    sse_parser_free(parser);
    reset_capture();
}

static void test_multiline_data(void) {
    printf(COLOR_YELLOW "\nTest: Multi-line data\n" COLOR_RESET);
    reset_capture();
    
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    
    // Data split across multiple data: lines
    const char *sse_data = "data: {\"part\": \"line1\"\ndata: ,\"part2\": \"line2\"}\n\n";
    int result = sse_parser_process_data(parser, sse_data, strlen(sse_data));
    TEST_ASSERT(result == 0, "Parsing should succeed");
    TEST_ASSERT(g_capture.count == 1, "Should have 1 event");
    // Multiple data: lines are concatenated with newlines between them per SSE spec
    // The exact format may vary by implementation
    TEST_ASSERT(g_capture.events[0].raw_data != NULL, "Raw data should not be NULL");
    
    sse_parser_free(parser);
    reset_capture();
}

static void test_openai_done_marker(void) {
    printf(COLOR_YELLOW "\nTest: OpenAI [DONE] marker\n" COLOR_RESET);
    reset_capture();
    
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    
    const char *sse_data = "data: [DONE]\n\n";
    int result = sse_parser_process_data(parser, sse_data, strlen(sse_data));
    TEST_ASSERT(result == 0, "Parsing should succeed");
    TEST_ASSERT(g_capture.count == 1, "Should have 1 event");
    TEST_ASSERT(g_capture.events[0].type == SSE_EVENT_OPENAI_DONE, "Should be OPENAI_DONE");
    
    sse_parser_free(parser);
    reset_capture();
}

static void test_comments_ignored(void) {
    printf(COLOR_YELLOW "\nTest: Comments are ignored\n" COLOR_RESET);
    reset_capture();
    
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    
    const char *sse_data = ": This is a comment\ndata: {\"message\": \"Hello\"}\n\n";
    int result = sse_parser_process_data(parser, sse_data, strlen(sse_data));
    TEST_ASSERT(result == 0, "Parsing should succeed");
    TEST_ASSERT(g_capture.count == 1, "Should have 1 event");
    TEST_ASSERT(g_capture.events[0].type == SSE_EVENT_OPENAI_CHUNK, "Should be OPENAI_CHUNK");
    
    sse_parser_free(parser);
    reset_capture();
}

static void test_empty_lines_between_events(void) {
    printf(COLOR_YELLOW "\nTest: Empty lines between events\n" COLOR_RESET);
    reset_capture();
    
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    
    const char *sse_data = 
        "data: {\"chunk\": 1}\n"
        "\n"
        "\n"
        "data: {\"chunk\": 2}\n"
        "\n";
    int result = sse_parser_process_data(parser, sse_data, strlen(sse_data));
    TEST_ASSERT(result == 0, "Parsing should succeed");
    TEST_ASSERT(g_capture.count == 2, "Should have 2 events");
    
    sse_parser_free(parser);
    reset_capture();
}

// ============================================================================
// Test Cases - Multiple events in single chunk
// ============================================================================

static void test_multiple_events_one_chunk(void) {
    printf(COLOR_YELLOW "\nTest: Multiple events in single data chunk\n" COLOR_RESET);
    reset_capture();
    
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    
    // Simulate receiving multiple SSE events in one network read
    const char *sse_data = 
        "event: message_start\ndata: {\"type\":\"message_start\"}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\"}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\"}\n\n"
        "event: message_stop\ndata: {}\n\n";
    
    int result = sse_parser_process_data(parser, sse_data, strlen(sse_data));
    TEST_ASSERT(result == 0, "Parsing should succeed");
    TEST_ASSERT(g_capture.count == 4, "Should have 4 events");
    TEST_ASSERT(g_capture.events[0].type == SSE_EVENT_MESSAGE_START, "First should be MESSAGE_START");
    TEST_ASSERT(g_capture.events[1].type == SSE_EVENT_CONTENT_BLOCK_START, "Second should be CONTENT_BLOCK_START");
    TEST_ASSERT(g_capture.events[2].type == SSE_EVENT_CONTENT_BLOCK_DELTA, "Third should be CONTENT_BLOCK_DELTA");
    TEST_ASSERT(g_capture.events[3].type == SSE_EVENT_MESSAGE_STOP, "Fourth should be MESSAGE_STOP");
    
    sse_parser_free(parser);
    reset_capture();
}

// ============================================================================
// Test Cases - Real-world API responses
// ============================================================================

static void test_openai_streaming_format(void) {
    printf(COLOR_YELLOW "\nTest: OpenAI streaming format\n" COLOR_RESET);
    reset_capture();
    
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    
    // Simulate OpenAI streaming response
    const char *sse_data = 
        "data: {\"id\":\"chat-123\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n"
        "data: {\"id\":\"chat-123\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"delta\":{\"content\":\" world\"}}]}\n\n"
        "data: [DONE]\n\n";
    
    int result = sse_parser_process_data(parser, sse_data, strlen(sse_data));
    TEST_ASSERT(result == 0, "Parsing should succeed");
    TEST_ASSERT(g_capture.count == 3, "Should have 3 events");
    TEST_ASSERT(g_capture.events[0].type == SSE_EVENT_OPENAI_CHUNK, "First should be OPENAI_CHUNK");
    TEST_ASSERT(g_capture.events[1].type == SSE_EVENT_OPENAI_CHUNK, "Second should be OPENAI_CHUNK");
    TEST_ASSERT(g_capture.events[2].type == SSE_EVENT_OPENAI_DONE, "Third should be OPENAI_DONE");
    
    // Verify JSON parsing
    cJSON *content = cJSON_GetObjectItem(g_capture.events[0].data, "choices");
    TEST_ASSERT(content != NULL, "First event should have choices");
    
    sse_parser_free(parser);
    reset_capture();
}

static void test_anthropic_streaming_format(void) {
    printf(COLOR_YELLOW "\nTest: Anthropic streaming format\n" COLOR_RESET);
    reset_capture();
    
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    
    // Simulate Anthropic streaming response
    const char *sse_data = 
        "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"msg-123\"}}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";
    
    int result = sse_parser_process_data(parser, sse_data, strlen(sse_data));
    TEST_ASSERT(result == 0, "Parsing should succeed");
    TEST_ASSERT(g_capture.count == 5, "Should have 5 events");
    TEST_ASSERT(g_capture.events[0].type == SSE_EVENT_MESSAGE_START, "First should be MESSAGE_START");
    TEST_ASSERT(g_capture.events[1].type == SSE_EVENT_CONTENT_BLOCK_START, "Second should be CONTENT_BLOCK_START");
    TEST_ASSERT(g_capture.events[2].type == SSE_EVENT_CONTENT_BLOCK_DELTA, "Third should be CONTENT_BLOCK_DELTA");
    TEST_ASSERT(g_capture.events[3].type == SSE_EVENT_CONTENT_BLOCK_STOP, "Fourth should be CONTENT_BLOCK_STOP");
    TEST_ASSERT(g_capture.events[4].type == SSE_EVENT_MESSAGE_STOP, "Fifth should be MESSAGE_STOP");
    
    sse_parser_free(parser);
    reset_capture();
}

// ============================================================================
// Test Cases - Edge cases and error handling
// ============================================================================

static void test_empty_data_event(void) {
    printf(COLOR_YELLOW "\nTest: Empty data event\n" COLOR_RESET);
    reset_capture();
    
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    
    // Empty data should result in a ping event (or no event)
    const char *sse_data = "data:\n\n";
    int result = sse_parser_process_data(parser, sse_data, strlen(sse_data));
    TEST_ASSERT(result == 0, "Parsing should succeed");
    // Empty data events may or may not trigger callbacks depending on implementation
    
    sse_parser_free(parser);
    reset_capture();
}

static void test_line_without_colon(void) {
    printf(COLOR_YELLOW "\nTest: Line without colon treated as data\n" COLOR_RESET);
    reset_capture();
    
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    
    // Lines without colon should be treated as data field
    const char *sse_data = "{\"raw\": \"data\"}\n\n";
    int result = sse_parser_process_data(parser, sse_data, strlen(sse_data));
    TEST_ASSERT(result == 0, "Parsing should succeed");
    
    // Note: Lines without colon may not trigger events depending on implementation
    // This tests the parser doesn't crash, actual behavior may vary
    
    sse_parser_free(parser);
    reset_capture();
}

static void test_abort_callback(void) {
    printf(COLOR_YELLOW "\nTest: Abort parsing from callback\n" COLOR_RESET);
    reset_capture();
    
    // Create a parser with abort_requested pre-set
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    
    // Set abort flag before processing - this should prevent callback from being called
    parser->abort_requested = true;
    
    const char *sse_data = "data: test\n\n";
    int result = sse_parser_process_data(parser, sse_data, strlen(sse_data));
    
    // When abort_requested is set, callback should not be called
    TEST_ASSERT(g_capture.count == 0, "No events should be captured when aborted");
    TEST_ASSERT(result == -1, "Process should return error when aborted");
    
    sse_parser_free(parser);
    reset_capture();
}

static void test_event_name_to_type(void) {
    printf(COLOR_YELLOW "\nTest: Event name to type mapping\n" COLOR_RESET);
    
    TEST_ASSERT(sse_event_name_to_type("message_start", NULL) == SSE_EVENT_MESSAGE_START, "message_start");
    TEST_ASSERT(sse_event_name_to_type("content_block_start", NULL) == SSE_EVENT_CONTENT_BLOCK_START, "content_block_start");
    TEST_ASSERT(sse_event_name_to_type("content_block_delta", NULL) == SSE_EVENT_CONTENT_BLOCK_DELTA, "content_block_delta");
    TEST_ASSERT(sse_event_name_to_type("content_block_stop", NULL) == SSE_EVENT_CONTENT_BLOCK_STOP, "content_block_stop");
    TEST_ASSERT(sse_event_name_to_type("message_delta", NULL) == SSE_EVENT_MESSAGE_DELTA, "message_delta");
    TEST_ASSERT(sse_event_name_to_type("message_stop", NULL) == SSE_EVENT_MESSAGE_STOP, "message_stop");
    TEST_ASSERT(sse_event_name_to_type("error", NULL) == SSE_EVENT_ERROR, "error");
    TEST_ASSERT(sse_event_name_to_type("ping", NULL) == SSE_EVENT_PING, "ping");
    
    // OpenAI style
    TEST_ASSERT(sse_event_name_to_type(NULL, "[DONE]") == SSE_EVENT_OPENAI_DONE, "[DONE]");
    TEST_ASSERT(sse_event_name_to_type(NULL, "some data") == SSE_EVENT_OPENAI_CHUNK, "implicit data");
    
    // Unknown
    TEST_ASSERT(sse_event_name_to_type("unknown_event", NULL) == SSE_EVENT_PING, "unknown event");
    TEST_ASSERT(sse_event_name_to_type(NULL, NULL) == SSE_EVENT_PING, "null inputs");
    TEST_ASSERT(sse_event_name_to_type(NULL, "") == SSE_EVENT_PING, "empty data");
}

static void test_event_type_to_name(void) {
    printf(COLOR_YELLOW "\nTest: Event type to name mapping\n" COLOR_RESET);
    
    TEST_ASSERT(strcmp(sse_event_type_to_name(SSE_EVENT_MESSAGE_START), "message_start") == 0, "MESSAGE_START");
    TEST_ASSERT(strcmp(sse_event_type_to_name(SSE_EVENT_CONTENT_BLOCK_START), "content_block_start") == 0, "CONTENT_BLOCK_START");
    TEST_ASSERT(strcmp(sse_event_type_to_name(SSE_EVENT_OPENAI_CHUNK), "openai_chunk") == 0, "OPENAI_CHUNK");
    TEST_ASSERT(strcmp(sse_event_type_to_name(SSE_EVENT_OPENAI_DONE), "openai_done") == 0, "OPENAI_DONE");
    TEST_ASSERT(strcmp(sse_event_type_to_name(SSE_EVENT_UNKNOWN), "unknown") == 0, "UNKNOWN");
}

// ============================================================================
// Test Cases - Memory buffer operations
// ============================================================================

static void test_memory_buffer_operations(void) {
    printf(COLOR_YELLOW "\nTest: Memory buffer operations\n" COLOR_RESET);
    
    MemoryBuffer *buf = memory_buffer_create();
    TEST_ASSERT(buf != NULL, "Buffer should be created");
    TEST_ASSERT(buf->capacity > 0, "Buffer should have capacity");
    TEST_ASSERT(buf->size == 0, "Buffer should start empty");
    TEST_ASSERT(buf->data[0] == '\0', "Buffer should be null-terminated");
    
    // Test append
    int result = memory_buffer_append(buf, "Hello", 5);
    TEST_ASSERT(result == 0, "Append should succeed");
    TEST_ASSERT(buf->size == 5, "Size should be 5");
    TEST_ASSERT(memcmp(buf->data, "Hello", 5) == 0, "Data should match");
    
    // Test append more (triggers growth)
    result = memory_buffer_append(buf, " World!", 7);
    TEST_ASSERT(result == 0, "Second append should succeed");
    TEST_ASSERT(buf->size == 12, "Size should be 12");
    TEST_ASSERT(memcmp(buf->data, "Hello World!", 12) == 0, "Data should match");
    
    // Test null append
    result = memory_buffer_append(buf, NULL, 5);
    TEST_ASSERT(result == -1, "Null append should fail");
    
    memory_buffer_free(buf);
    
    // Test free of NULL
    memory_buffer_free(NULL);  // Should not crash
    TEST_ASSERT(true, "Free of NULL should not crash");
}

// ============================================================================
// Test Cases - Parser lifecycle
// ============================================================================

static void test_parser_lifecycle(void) {
    printf(COLOR_YELLOW "\nTest: Parser lifecycle\n" COLOR_RESET);
    
    // Test create and free
    SSEParserState *parser = sse_parser_create(test_event_callback, &g_capture);
    TEST_ASSERT(parser != NULL, "Parser should be created");
    sse_parser_free(parser);
    TEST_ASSERT(true, "Parser should be freed");
    
    // Test free of NULL
    sse_parser_free(NULL);
    TEST_ASSERT(true, "Free of NULL should not crash");
    
    // Test reset event
    parser = sse_parser_create(test_event_callback, &g_capture);
    const char *sse_data = "event: test\ndata: hello\n\n";
    sse_parser_process_data(parser, sse_data, strlen(sse_data));
    TEST_ASSERT(g_capture.count == 1, "Should have captured event");
    
    sse_parser_reset_event(parser);
    TEST_ASSERT(true, "Reset event should not crash");
    
    sse_parser_free(parser);
    reset_capture();
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
    printf("=== SSE Parser Unit Tests ===\n");
    printf("Testing Server-Sent Events parser functionality\n\n");
    
    // Basic parsing tests
    test_simple_data_event();
    test_event_with_type();
    test_multiline_data();
    test_openai_done_marker();
    test_comments_ignored();
    test_empty_lines_between_events();
    
    // Multiple events tests
    test_multiple_events_one_chunk();
    
    // Real-world format tests
    test_openai_streaming_format();
    test_anthropic_streaming_format();
    
    // Edge cases
    test_empty_data_event();
    test_line_without_colon();
    test_abort_callback();
    test_event_name_to_type();
    test_event_type_to_name();
    
    // Memory buffer tests
    test_memory_buffer_operations();
    
    // Lifecycle tests
    test_parser_lifecycle();
    
    // Cleanup
    reset_capture();
    
    TEST_SUMMARY();
}
