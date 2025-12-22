/*
 * Example: ZMQ Interactive Mode Client
 * 
 * This example shows how to use the new ZMQ interactive mode
 * that supports recursive tool calls and maintains conversation context.
 */

#include <zmq.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 65536

int main() {
    printf("ZMQ Interactive Mode Client Example\n");
    printf("===================================\n\n");
    
    // Initialize ZMQ context
    void *context = zmq_ctx_new();
    if (!context) {
        fprintf(stderr, "Failed to create ZMQ context\n");
        return 1;
    }
    
    // Create REQ socket
    void *socket = zmq_socket(context, ZMQ_PAIR);
    if (!socket) {
        fprintf(stderr, "Failed to create ZMQ socket\n");
        zmq_ctx_term(context);
        return 1;
    }
    
    // Connect to klawed ZMQ daemon
    int rc = zmq_connect(socket, "tcp://127.0.0.1:5555");
    if (rc != 0) {
        fprintf(stderr, "Failed to connect to ZMQ endpoint: %s\n", zmq_strerror(errno));
        zmq_close(socket);
        zmq_ctx_term(context);
        return 1;
    }
    
    printf("Connected to klawed ZMQ daemon at tcp://127.0.0.1:5555\n\n");
    
    // Example 1: Simple query
    printf("Example 1: Simple query\n");
    printf("-----------------------\n");
    
    cJSON *request1 = cJSON_CreateObject();
    cJSON_AddStringToObject(request1, "messageType", "TEXT");
    cJSON_AddStringToObject(request1, "content", "What is the capital of France?");
    
    char *request_str1 = cJSON_PrintUnformatted(request1);
    printf("Sending: %s\n", request_str1);
    
    zmq_send(socket, request_str1, strlen(request_str1), 0);
    
    char buffer[BUFFER_SIZE];
    int bytes = zmq_recv(socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        cJSON *response = cJSON_Parse(buffer);
        if (response) {
            cJSON *msg_type = cJSON_GetObjectItem(response, "messageType");
            cJSON *content = cJSON_GetObjectItem(response, "content");
            if (msg_type && cJSON_IsString(msg_type) && content && cJSON_IsString(content)) {
                printf("Response: %s\n", content->valuestring);
            }
            cJSON_Delete(response);
        }
    }
    
    free(request_str1);
    cJSON_Delete(request1);
    
    printf("\n");
    
    // Example 2: Query that might use tools
    printf("Example 2: Query that might use tools\n");
    printf("-------------------------------------\n");
    
    cJSON *request2 = cJSON_CreateObject();
    cJSON_AddStringToObject(request2, "messageType", "TEXT");
    cJSON_AddStringToObject(request2, "content", "List the files in the current directory");
    
    char *request_str2 = cJSON_PrintUnformatted(request2);
    printf("Sending: %s\n", request_str2);
    
    zmq_send(socket, request_str2, strlen(request_str2), 0);
    
    // Handle multiple responses
    while (1) {
        bytes = zmq_recv(socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        
        buffer[bytes] = '\0';
        cJSON *response = cJSON_Parse(buffer);
        if (!response) {
            printf("Failed to parse JSON response\n");
            break;
        }
        
        cJSON *msg_type = cJSON_GetObjectItem(response, "messageType");
        if (!msg_type || !cJSON_IsString(msg_type)) {
            printf("Invalid response: missing messageType\n");
            cJSON_Delete(response);
            break;
        }
        
        const char *type = msg_type->valuestring;
        
        if (strcmp(type, "TEXT") == 0) {
            cJSON *content = cJSON_GetObjectItem(response, "content");
            if (content && cJSON_IsString(content)) {
                printf("AI: %s\n", content->valuestring);
            }
        } else if (strcmp(type, "TOOL_RESULT") == 0) {
            cJSON *tool_name = cJSON_GetObjectItem(response, "toolName");
            cJSON *is_error = cJSON_GetObjectItem(response, "isError");
            if (tool_name && cJSON_IsString(tool_name)) {
                if (is_error && cJSON_IsBool(is_error) && is_error->valueint) {
                    printf("Tool %s failed\n", tool_name->valuestring);
                } else {
                    printf("Tool %s executed successfully\n", tool_name->valuestring);
                }
            }
        } else if (strcmp(type, "COMPLETED") == 0) {
            printf("Processing completed\n");
            cJSON_Delete(response);
            break;
        } else if (strcmp(type, "ERROR") == 0) {
            cJSON *content = cJSON_GetObjectItem(response, "content");
            if (content && cJSON_IsString(content)) {
                printf("Error: %s\n", content->valuestring);
            }
            cJSON_Delete(response);
            break;
        }
        
        cJSON_Delete(response);
    }
    
    free(request_str2);
    cJSON_Delete(request2);
    
    printf("\n");
    
    // Cleanup
    zmq_close(socket);
    zmq_ctx_term(context);
    
    printf("Example complete.\n");
    printf("To run klawed in ZMQ daemon mode:\n");
    printf("  ./build/klawed --zmq tcp://127.0.0.1:5555\n");
    
    return 0;
}