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
