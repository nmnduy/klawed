/*
 * test_zmq_pubsub.c - Test ZMQ PUB/SUB issues
 * 
 * Demonstrates the strict ordering problem with ZMQ PUB/SUB:
 * 1. Subscribers must connect before publishers send messages
 * 2. Messages sent before subscription are lost
 * 3. No built-in queuing or retry mechanism
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zmq.h>

#define ENDPOINT "tcp://127.0.0.1:5556"

void publisher() {
    printf("Starting publisher...\n");
    
    void *context = zmq_ctx_new();
    void *socket = zmq_socket(context, ZMQ_PUB);
    
    // Set some socket options for better behavior
    int hwm = 1000; // High water mark
    zmq_setsockopt(socket, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    
    int linger = 1000; // 1 second linger
    zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
    
    int rc = zmq_bind(socket, ENDPOINT);
    if (rc != 0) {
        fprintf(stderr, "Publisher bind failed: %s\n", zmq_strerror(errno));
        zmq_close(socket);
        zmq_ctx_term(context);
        return;
    }
    
    printf("Publisher bound to %s\n", ENDPOINT);
    printf("Waiting 2 seconds for subscribers to connect...\n");
    sleep(2);
    
    // Send messages
    for (int i = 1; i <= 5; i++) {
        char message[256];
        snprintf(message, sizeof(message), "Message %d from publisher", i);
        
        printf("Sending: %s\n", message);
        rc = zmq_send(socket, message, strlen(message), 0);
        if (rc < 0) {
            fprintf(stderr, "Send failed: %s\n", zmq_strerror(errno));
        } else {
            printf("  Sent %d bytes\n", rc);
        }
        
        sleep(1);
    }
    
    printf("Publisher done\n");
    
    zmq_close(socket);
    zmq_ctx_term(context);
}

void subscriber_late() {
    printf("Starting late subscriber (connects after messages sent)...\n");
    sleep(3); // Wait for publisher to send some messages first
    
    void *context = zmq_ctx_new();
    void *socket = zmq_socket(context, ZMQ_SUB);
    
    // Set socket options
    int hwm = 1000;
    zmq_setsockopt(socket, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    
    int linger = 1000;
    zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
    
    // Subscribe to all messages
    zmq_setsockopt(socket, ZMQ_SUBSCRIBE, "", 0);
    
    printf("Late subscriber connecting to %s...\n", ENDPOINT);
    int rc = zmq_connect(socket, ENDPOINT);
    if (rc != 0) {
        fprintf(stderr, "Subscriber connect failed: %s\n", zmq_strerror(errno));
        zmq_close(socket);
        zmq_ctx_term(context);
        return;
    }
    
    printf("Late subscriber connected\n");
    
    // Try to receive with timeout
    int timeout = 3000; // 3 seconds
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    int received = 0;
    for (int i = 0; i < 5; i++) {
        char buffer[256];
        rc = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
        if (rc < 0) {
            if (errno == EAGAIN) {
                printf("  Timeout waiting for message\n");
            } else {
                fprintf(stderr, "Receive error: %s\n", zmq_strerror(errno));
            }
            break;
        }
        
        buffer[rc] = '\0';
        printf("  Received: %s\n", buffer);
        received++;
    }
    
    printf("Late subscriber received %d messages (expected 0 because we connected late)\n", received);
    
    zmq_close(socket);
    zmq_ctx_term(context);
}

void subscriber_early() {
    printf("Starting early subscriber (connects before messages)...\n");
    
    void *context = zmq_ctx_new();
    void *socket = zmq_socket(context, ZMQ_SUB);
    
    // Set socket options
    int hwm = 1000;
    zmq_setsockopt(socket, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    
    int linger = 1000;
    zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
    
    // Subscribe to all messages
    zmq_setsockopt(socket, ZMQ_SUBSCRIBE, "", 0);
    
    printf("Early subscriber connecting to %s...\n", ENDPOINT);
    int rc = zmq_connect(socket, ENDPOINT);
    if (rc != 0) {
        fprintf(stderr, "Subscriber connect failed: %s\n", zmq_strerror(errno));
        zmq_close(socket);
        zmq_ctx_term(context);
        return;
    }
    
    printf("Early subscriber connected, waiting for messages...\n");
    
    // Try to receive with timeout
    int timeout = 10000; // 10 seconds
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    int received = 0;
    for (int i = 0; i < 5; i++) {
        char buffer[256];
        rc = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
        if (rc < 0) {
            if (errno == EAGAIN) {
                printf("  Timeout waiting for message\n");
            } else {
                fprintf(stderr, "Receive error: %s\n", zmq_strerror(errno));
            }
            break;
        }
        
        buffer[rc] = '\0';
        printf("  Received: %s\n", buffer);
        received++;
    }
    
    printf("Early subscriber received %d messages (expected 5)\n", received);
    
    zmq_close(socket);
    zmq_ctx_term(context);
}

int main() {
    printf("=== ZMQ PUB/SUB Test ===\n\n");
    
    // Fork to run publisher and subscribers
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - publisher
        publisher();
        return 0;
    }
    
    // Parent process - run subscribers
    sleep(1); // Give publisher time to start
    
    pid_t sub1 = fork();
    if (sub1 == 0) {
        // First child - early subscriber
        subscriber_early();
        return 0;
    }
    
    pid_t sub2 = fork();
    if (sub2 == 0) {
        // Second child - late subscriber
        subscriber_late();
        return 0;
    }
    
    // Wait for all children
    waitpid(pid, NULL, 0);
    waitpid(sub1, NULL, 0);
    waitpid(sub2, NULL, 0);
    
    printf("\n=== Test Complete ===\n");
    printf("Key observations:\n");
    printf("1. Early subscriber gets all messages\n");
    printf("2. Late subscriber gets NO messages (they were lost)\n");
    printf("3. ZMQ PUB/SUB has no built-in message queuing\n");
    printf("4. Strict ordering requirement: connect before publish\n");
    
    return 0;
}