/*
 * zmq_client.c - Interactive ZMQ client for Klawed
 * 
 * Compile: gcc -o zmq_client zmq_client.c -lzmq -lcjson
 * Run: ./zmq_client tcp://127.0.0.1:5555
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <cjson/cJSON.h>

#define BUFFER_SIZE 4096

void print_usage(const char *program_name) {
    printf("Usage: %s <endpoint>\n", program_name);
    printf("Example: %s tcp://127.0.0.1:5555\n", program_name);
    printf("\nAvailable endpoints:\n");
    printf("  tcp://127.0.0.1:5555  - TCP socket on localhost port 5555\n");
    printf("  ipc:///tmp/klawed.sock - IPC socket file\n");
    printf("\nCommands in interactive mode:\n");
    printf("  /help     - Show this help\n");
    printf("  /quit     - Exit the program\n");
    printf("  /clear    - Clear screen\n");
    printf("  <text>    - Send text message to Klawed\n");
}

void send_message(void *socket, const char *message_json) {
    // Send message
    int rc = zmq_send(socket, message_json, strlen(message_json), 0);
    if (rc < 0) {
        fprintf(stderr, "Error sending message: %s\n", zmq_strerror(errno));
        return;
    }
    
    printf("✓ Message sent\n");
    
    // Wait for response
    char buffer[BUFFER_SIZE];
    rc = zmq_recv(socket, buffer, BUFFER_SIZE - 1, 0);
    if (rc < 0) {
        fprintf(stderr, "Error receiving response: %s\n", zmq_strerror(errno));
        return;
    }
    
    buffer[rc] = '\0';
    
    // Parse and pretty-print JSON response
    cJSON *json = cJSON_Parse(buffer);
    if (json) {
        char *pretty = cJSON_Print(json);
        printf("Response:\n%s\n", pretty);
        free(pretty);
        cJSON_Delete(json);
    } else {
        printf("Response (raw): %s\n", buffer);
    }
}

void send_text_message(void *socket, const char *text) {
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "messageType", "TEXT");
    cJSON_AddStringToObject(message, "content", text);
    
    char *json_str = cJSON_PrintUnformatted(message);
    send_message(socket, json_str);
    
    free(json_str);
    cJSON_Delete(message);
}



int main(int argc, char *argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *endpoint = argv[1];
    
    // Initialize ZMQ context
    void *context = zmq_ctx_new();
    if (!context) {
        fprintf(stderr, "Error creating ZMQ context\n");
        return 1;
    }
    
    // Create REQ socket
    void *socket = zmq_socket(context, ZMQ_REQ);
    if (!socket) {
        fprintf(stderr, "Error creating ZMQ socket: %s\n", zmq_strerror(errno));
        zmq_ctx_term(context);
        return 1;
    }
    
    // Set timeout for receiving (5 seconds)
    int timeout = 5000;
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Connect to endpoint
    printf("Connecting to %s...\n", endpoint);
    int rc = zmq_connect(socket, endpoint);
    if (rc != 0) {
        fprintf(stderr, "Error connecting to %s: %s\n", endpoint, zmq_strerror(errno));
        zmq_close(socket);
        zmq_ctx_term(context);
        return 1;
    }
    
    printf("Connected to %s\n", endpoint);
    printf("Type your messages (or /help for commands)\n");
    printf("-----------------------------------------\n");
    
    // Interactive loop
    char input[BUFFER_SIZE];
    while (1) {
        printf("\n> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break; // EOF or error
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = '\0';
        
        // Skip empty input
        if (strlen(input) == 0) {
            continue;
        }
        
        // Check for commands
        if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
            printf("Goodbye!\n");
            break;
        } else if (strcmp(input, "/help") == 0) {
            print_usage(argv[0]);
        } else if (strcmp(input, "/clear") == 0) {
            printf("\033[2J\033[H"); // Clear screen
        } else if (input[0] == '/') {
            printf("Unknown command: %s\n", input);
            printf("Type /help for available commands\n");
        } else {
            // Regular text message
            send_text_message(socket, input);
        }
    }
    
    // Cleanup
    zmq_close(socket);
    zmq_ctx_term(context);
    
    return 0;
}