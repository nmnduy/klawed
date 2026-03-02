/**
 * test_message_queue.c - Unit tests for message queues
 *
 * Tests both TUI message queue and AI instruction queue with:
 * - Basic enqueue/dequeue operations
 * - Overflow behavior
 * - Thread safety (concurrent access)
 * - Shutdown behavior
 * - Memory leak checks
 */

#include "../src/message_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

/* Test result tracking */
static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) \
    do { \
        printf("Running test: %s\n", #name); \
        g_tests_run++; \
    } while (0)

#define ASSERT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAILED: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return; \
        } \
    } while (0)

#define TEST_PASS() \
    do { \
        g_tests_passed++; \
        printf("  PASSED\n"); \
    } while (0)

/* ========================================================================
 * TUI Message Queue Tests
 * ======================================================================== */

static void test_tui_msg_queue_init_free(void) {
    TEST(test_tui_msg_queue_init_free);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 10) == 0);
    ASSERT(queue.capacity == 10);
    ASSERT(queue.count == 0);
    ASSERT(queue.messages != NULL);

    tui_msg_queue_free(&queue);

    TEST_PASS();
}

static void test_tui_msg_post_and_poll(void) {
    TEST(test_tui_msg_post_and_poll);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 5) == 0);

    /* Post a message */
    ASSERT(post_tui_message(&queue, TUI_MSG_ADD_LINE, "Hello, World!") == 0);
    ASSERT(queue.count == 1);

    /* Poll the message */
    TUIMessage msg = {0};
    ASSERT(poll_tui_message(&queue, &msg) == 1);
    ASSERT(msg.type == TUI_MSG_ADD_LINE);
    ASSERT(strcmp(msg.text, "Hello, World!") == 0);
    ASSERT(queue.count == 0);

    free(msg.text);
    tui_msg_queue_free(&queue);

    TEST_PASS();
}

static void test_tui_msg_queue_empty_poll(void) {
    TEST(test_tui_msg_queue_empty_poll);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 5) == 0);

    /* Poll empty queue should return 0 */
    TUIMessage msg = {0};
    ASSERT(poll_tui_message(&queue, &msg) == 0);

    tui_msg_queue_free(&queue);

    TEST_PASS();
}

static void test_tui_msg_queue_overflow(void) {
    TEST(test_tui_msg_queue_overflow);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 3) == 0);

    /* Fill queue to capacity */
    ASSERT(post_tui_message(&queue, TUI_MSG_ADD_LINE, "Message 1") == 0);
    ASSERT(post_tui_message(&queue, TUI_MSG_ADD_LINE, "Message 2") == 0);
    ASSERT(post_tui_message(&queue, TUI_MSG_ADD_LINE, "Message 3") == 0);
    ASSERT(queue.count == 3);

    /* Overflow should drop oldest (Message 1) */
    ASSERT(post_tui_message(&queue, TUI_MSG_ADD_LINE, "Message 4") == 0);
    ASSERT(queue.count == 3);

    /* First message should be "Message 2" */
    TUIMessage msg = {0};
    ASSERT(poll_tui_message(&queue, &msg) == 1);
    ASSERT(strcmp(msg.text, "Message 2") == 0);
    free(msg.text);

    /* Second message should be "Message 3" */
    ASSERT(poll_tui_message(&queue, &msg) == 1);
    ASSERT(strcmp(msg.text, "Message 3") == 0);
    free(msg.text);

    /* Third message should be "Message 4" */
    ASSERT(poll_tui_message(&queue, &msg) == 1);
    ASSERT(strcmp(msg.text, "Message 4") == 0);
    free(msg.text);

    tui_msg_queue_free(&queue);

    TEST_PASS();
}

static void test_tui_msg_queue_null_text(void) {
    TEST(test_tui_msg_queue_null_text);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 5) == 0);

    /* Post message with NULL text */
    ASSERT(post_tui_message(&queue, TUI_MSG_CLEAR, NULL) == 0);

    TUIMessage msg = {0};
    ASSERT(poll_tui_message(&queue, &msg) == 1);
    ASSERT(msg.type == TUI_MSG_CLEAR);
    ASSERT(msg.text == NULL);

    tui_msg_queue_free(&queue);

    TEST_PASS();
}

/* Producer thread for concurrent test */
static void* tui_msg_producer(void *arg) {
    TUIMessageQueue *queue = (TUIMessageQueue*)arg;

    for (int i = 0; i < 100; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Message %d", i);
        post_tui_message(queue, TUI_MSG_ADD_LINE, buf);
        usleep(100); /* Small delay to interleave with consumer */
    }

    return NULL;
}

/* Consumer thread for concurrent test */
static void* tui_msg_consumer(void *arg) {
    TUIMessageQueue *queue = (TUIMessageQueue*)arg;
    int consumed = 0;

    while (consumed < 100) {
        TUIMessage msg = {0};
        if (poll_tui_message(queue, &msg) == 1) {
            free(msg.text);
            consumed++;
        }
        usleep(100);
    }

    return NULL;
}

static void test_tui_msg_queue_concurrent(void) {
    TEST(test_tui_msg_queue_concurrent);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 20) == 0);

    pthread_t producer, consumer;
    ASSERT(pthread_create(&producer, NULL, tui_msg_producer, &queue) == 0);
    ASSERT(pthread_create(&consumer, NULL, tui_msg_consumer, &queue) == 0);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    tui_msg_queue_free(&queue);

    TEST_PASS();
}

static void test_tui_msg_queue_shutdown(void) {
    TEST(test_tui_msg_queue_shutdown);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 5) == 0);

    /* Shutdown queue */
    tui_msg_queue_shutdown(&queue);
    ASSERT(queue.shutdown == true);

    /* wait_tui_message should return 0 (shutdown) */
    TUIMessage msg = {0};
    ASSERT(wait_tui_message(&queue, &msg) == 0);

    tui_msg_queue_free(&queue);

    TEST_PASS();
}

/* ========================================================================
 * AI Instruction Queue Tests
 * ======================================================================== */

static void test_ai_queue_init_free(void) {
    TEST(test_ai_queue_init_free);

    AIInstructionQueue queue = {0};
    ASSERT(ai_queue_init(&queue, 10) == 0);
    ASSERT(queue.capacity == 10);
    ASSERT(queue.count == 0);
    ASSERT(queue.instructions != NULL);

    ai_queue_free(&queue);

    TEST_PASS();
}

static void test_ai_queue_enqueue_dequeue(void) {
    TEST(test_ai_queue_enqueue_dequeue);

    AIInstructionQueue queue = {0};
    ASSERT(ai_queue_init(&queue, 5) == 0);

    /* Enqueue instruction */
    void *dummy_state = (void*)0x1234;
    ASSERT(enqueue_instruction(&queue, "Write hello world", dummy_state) == 0);
    ASSERT(queue.count == 1);

    /* Dequeue instruction */
    AIInstruction instr = {0};
    ASSERT(dequeue_instruction(&queue, &instr) == 1);
    ASSERT(strcmp(instr.text, "Write hello world") == 0);
    ASSERT(instr.conversation_state == dummy_state);
    ASSERT(queue.count == 0);

    free(instr.text);
    ai_queue_free(&queue);

    TEST_PASS();
}

static void test_ai_queue_depth(void) {
    TEST(test_ai_queue_depth);

    AIInstructionQueue queue = {0};
    ASSERT(ai_queue_init(&queue, 5) == 0);

    ASSERT(ai_queue_depth(&queue) == 0);

    enqueue_instruction(&queue, "Task 1", NULL);
    ASSERT(ai_queue_depth(&queue) == 1);

    enqueue_instruction(&queue, "Task 2", NULL);
    ASSERT(ai_queue_depth(&queue) == 2);

    AIInstruction instr = {0};
    dequeue_instruction(&queue, &instr);
    free(instr.text);
    ASSERT(ai_queue_depth(&queue) == 1);

    ai_queue_free(&queue);

    TEST_PASS();
}

static void test_ai_queue_fifo_order(void) {
    TEST(test_ai_queue_fifo_order);

    AIInstructionQueue queue = {0};
    ASSERT(ai_queue_init(&queue, 5) == 0);

    /* Enqueue multiple instructions */
    enqueue_instruction(&queue, "First", NULL);
    enqueue_instruction(&queue, "Second", NULL);
    enqueue_instruction(&queue, "Third", NULL);

    /* Dequeue should maintain FIFO order */
    AIInstruction instr = {0};

    dequeue_instruction(&queue, &instr);
    ASSERT(strcmp(instr.text, "First") == 0);
    free(instr.text);

    dequeue_instruction(&queue, &instr);
    ASSERT(strcmp(instr.text, "Second") == 0);
    free(instr.text);

    dequeue_instruction(&queue, &instr);
    ASSERT(strcmp(instr.text, "Third") == 0);
    free(instr.text);

    ai_queue_free(&queue);

    TEST_PASS();
}

/* Producer thread for AI queue concurrent test */
static void* ai_queue_producer(void *arg) {
    AIInstructionQueue *queue = (AIInstructionQueue*)arg;

    for (int i = 0; i < 50; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Instruction %d", i);
        enqueue_instruction(queue, buf, NULL);
    }

    return NULL;
}

/* Consumer thread for AI queue concurrent test */
static void* ai_queue_consumer(void *arg) {
    AIInstructionQueue *queue = (AIInstructionQueue*)arg;

    for (int i = 0; i < 50; i++) {
        AIInstruction instr = {0};
        if (dequeue_instruction(queue, &instr) == 1) {
            free(instr.text);
        }
    }

    return NULL;
}

static void test_ai_queue_concurrent(void) {
    TEST(test_ai_queue_concurrent);

    AIInstructionQueue queue = {0};
    ASSERT(ai_queue_init(&queue, 10) == 0);

    pthread_t producer, consumer;
    ASSERT(pthread_create(&producer, NULL, ai_queue_producer, &queue) == 0);
    ASSERT(pthread_create(&consumer, NULL, ai_queue_consumer, &queue) == 0);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    ASSERT(queue.count == 0);

    ai_queue_free(&queue);

    TEST_PASS();
}

static void test_ai_queue_shutdown(void) {
    TEST(test_ai_queue_shutdown);

    AIInstructionQueue queue = {0};
    ASSERT(ai_queue_init(&queue, 5) == 0);

    /* Shutdown queue */
    ai_queue_shutdown(&queue);
    ASSERT(queue.shutdown == true);

    /* dequeue_instruction should return 0 (shutdown) */
    AIInstruction instr = {0};
    ASSERT(dequeue_instruction(&queue, &instr) == 0);

    /* enqueue_instruction should return -1 (shutdown) */
    ASSERT(enqueue_instruction(&queue, "Test", NULL) == -1);

    ai_queue_free(&queue);

    TEST_PASS();
}

/* ========================================================================
 * Streaming Message Tests (TUI_MSG_STREAM_START / TUI_MSG_STREAM_APPEND)
 * ======================================================================== */

static void test_post_tui_stream_start(void) {
    TEST(test_post_tui_stream_start);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 5) == 0);

    /* Post a stream-start message with a label and color_pair */
    ASSERT(post_tui_stream_start(&queue, "[Assistant]", 3) == 0);
    ASSERT(queue.count == 1);

    TUIMessage msg = {0};
    ASSERT(poll_tui_message(&queue, &msg) == 1);
    ASSERT(msg.type == TUI_MSG_STREAM_START);
    ASSERT(strcmp(msg.text, "[Assistant]") == 0);
    ASSERT(msg.color_pair == 3);

    free(msg.text);
    tui_msg_queue_free(&queue);

    TEST_PASS();
}

static void test_post_tui_stream_append(void) {
    TEST(test_post_tui_stream_append);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 5) == 0);

    /* Post a stream-append message via post_tui_message */
    ASSERT(post_tui_message(&queue, TUI_MSG_STREAM_APPEND, "Hello") == 0);

    TUIMessage msg = {0};
    ASSERT(poll_tui_message(&queue, &msg) == 1);
    ASSERT(msg.type == TUI_MSG_STREAM_APPEND);
    ASSERT(strcmp(msg.text, "Hello") == 0);
    /* color_pair should be 0 (default, unused for APPEND) */
    ASSERT(msg.color_pair == 0);

    free(msg.text);
    tui_msg_queue_free(&queue);

    TEST_PASS();
}

static void test_streaming_sequence(void) {
    TEST(test_streaming_sequence);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 16) == 0);

    /* Simulate a full streaming response: START then several APPEND tokens */
    ASSERT(post_tui_stream_start(&queue, "[Assistant]", 5) == 0);
    ASSERT(post_tui_message(&queue, TUI_MSG_STREAM_APPEND, "Hello") == 0);
    ASSERT(post_tui_message(&queue, TUI_MSG_STREAM_APPEND, ", ") == 0);
    ASSERT(post_tui_message(&queue, TUI_MSG_STREAM_APPEND, "world!") == 0);
    ASSERT(queue.count == 4);

    const char *expected_types[] = {"START", "APPEND", "APPEND", "APPEND"};
    TUIMessageType expected[] = {
        TUI_MSG_STREAM_START,
        TUI_MSG_STREAM_APPEND,
        TUI_MSG_STREAM_APPEND,
        TUI_MSG_STREAM_APPEND
    };
    const char *expected_text[] = {"[Assistant]", "Hello", ", ", "world!"};

    for (int i = 0; i < 4; i++) {
        TUIMessage msg = {0};
        ASSERT(poll_tui_message(&queue, &msg) == 1);
        ASSERT(msg.type == expected[i]);
        ASSERT(strcmp(msg.text, expected_text[i]) == 0);
        printf("  [%d] type=%s text=\"%s\" color_pair=%d\n",
               i, expected_types[i], msg.text, msg.color_pair);
        free(msg.text);
    }

    ASSERT(queue.count == 0);
    tui_msg_queue_free(&queue);

    TEST_PASS();
}

static void test_stream_start_color_pair_preserved(void) {
    TEST(test_stream_start_color_pair_preserved);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 5) == 0);

    /* Different labels can have different color pairs */
    ASSERT(post_tui_stream_start(&queue, "[Assistant]", 7) == 0);
    ASSERT(post_tui_stream_start(&queue, "[Reasoning]", 2) == 0);

    TUIMessage msg = {0};
    ASSERT(poll_tui_message(&queue, &msg) == 1);
    ASSERT(msg.type == TUI_MSG_STREAM_START);
    ASSERT(msg.color_pair == 7);
    free(msg.text);

    ASSERT(poll_tui_message(&queue, &msg) == 1);
    ASSERT(msg.type == TUI_MSG_STREAM_START);
    ASSERT(msg.color_pair == 2);
    free(msg.text);

    tui_msg_queue_free(&queue);

    TEST_PASS();
}

static void test_stream_start_null_label(void) {
    TEST(test_stream_start_null_label);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 5) == 0);

    /* NULL label should be accepted (treated as empty) */
    ASSERT(post_tui_stream_start(&queue, NULL, 0) == 0);

    TUIMessage msg = {0};
    ASSERT(poll_tui_message(&queue, &msg) == 1);
    ASSERT(msg.type == TUI_MSG_STREAM_START);
    /* text may be NULL or empty string depending on implementation */
    free(msg.text);

    tui_msg_queue_free(&queue);

    TEST_PASS();
}

/* Thread data for streaming concurrency test */
typedef struct {
    TUIMessageQueue *queue;
    int token_count;
} StreamThreadData;

static void* stream_producer_thread(void *arg) {
    StreamThreadData *data = (StreamThreadData*)arg;

    post_tui_stream_start(data->queue, "[Assistant]", 5);
    for (int i = 0; i < data->token_count; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "tok%d", i);
        post_tui_message(data->queue, TUI_MSG_STREAM_APPEND, buf);
        usleep(50);
    }

    return NULL;
}

static void test_streaming_concurrent(void) {
    TEST(test_streaming_concurrent);

    TUIMessageQueue queue = {0};
    ASSERT(tui_msg_queue_init(&queue, 64) == 0);

    StreamThreadData data = { &queue, 30 };
    pthread_t producer;
    ASSERT(pthread_create(&producer, NULL, stream_producer_thread, &data) == 0);

    /* Consumer: drain all messages */
    int total = 0;
    while (total < 31) {  /* 1 START + 30 APPEND */
        TUIMessage msg = {0};
        if (poll_tui_message(&queue, &msg) == 1) {
            ASSERT(msg.type == TUI_MSG_STREAM_START ||
                   msg.type == TUI_MSG_STREAM_APPEND);
            free(msg.text);
            total++;
        }
        usleep(50);
    }

    pthread_join(producer, NULL);
    tui_msg_queue_free(&queue);

    TEST_PASS();
}

/* Multiple producer/consumer stress test */
static void* ai_queue_stress_producer(void *arg) {
    AIInstructionQueue *queue = (AIInstructionQueue*)arg;

    for (int i = 0; i < 100; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Stress %d", i);
        enqueue_instruction(queue, buf, NULL);
        usleep(10);
    }

    return NULL;
}

static void* ai_queue_stress_consumer(void *arg) {
    AIInstructionQueue *queue = (AIInstructionQueue*)arg;
    int consumed = 0;

    while (consumed < 100) {
        AIInstruction instr = {0};
        if (dequeue_instruction(queue, &instr) == 1) {
            free(instr.text);
            consumed++;
        }
    }

    return NULL;
}

static void test_ai_queue_stress(void) {
    TEST(test_ai_queue_stress);

    AIInstructionQueue queue = {0};
    ASSERT(ai_queue_init(&queue, 20) == 0);

    /* Create multiple producers and consumers */
    pthread_t producers[3];
    pthread_t consumers[3];

    for (int i = 0; i < 3; i++) {
        ASSERT(pthread_create(&producers[i], NULL, ai_queue_stress_producer, &queue) == 0);
        ASSERT(pthread_create(&consumers[i], NULL, ai_queue_stress_consumer, &queue) == 0);
    }

    for (int i = 0; i < 3; i++) {
        pthread_join(producers[i], NULL);
        pthread_join(consumers[i], NULL);
    }

    ASSERT(queue.count == 0);

    ai_queue_free(&queue);

    TEST_PASS();
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void) {
    printf("=== Message Queue Unit Tests ===\n\n");

    /* TUI Message Queue Tests */
    printf("--- TUI Message Queue Tests ---\n");
    test_tui_msg_queue_init_free();
    test_tui_msg_post_and_poll();
    test_tui_msg_queue_empty_poll();
    test_tui_msg_queue_overflow();
    test_tui_msg_queue_null_text();
    test_tui_msg_queue_concurrent();
    test_tui_msg_queue_shutdown();

    /* Streaming Message Tests */
    printf("\n--- Streaming Message Tests ---\n");
    test_post_tui_stream_start();
    test_post_tui_stream_append();
    test_streaming_sequence();
    test_stream_start_color_pair_preserved();
    test_stream_start_null_label();
    test_streaming_concurrent();

    /* AI Instruction Queue Tests */
    printf("\n--- AI Instruction Queue Tests ---\n");
    test_ai_queue_init_free();
    test_ai_queue_enqueue_dequeue();
    test_ai_queue_depth();
    test_ai_queue_fifo_order();
    test_ai_queue_concurrent();
    test_ai_queue_shutdown();
    test_ai_queue_stress();

    /* Summary */
    printf("\n=== Test Summary ===\n");
    printf("Tests run: %d\n", g_tests_run);
    printf("Tests passed: %d\n", g_tests_passed);
    printf("Tests failed: %d\n", g_tests_run - g_tests_passed);

    if (g_tests_passed == g_tests_run) {
        printf("\n✓ All tests passed!\n");
        return 0;
    } else {
        printf("\n✗ Some tests failed\n");
        return 1;
    }
}
