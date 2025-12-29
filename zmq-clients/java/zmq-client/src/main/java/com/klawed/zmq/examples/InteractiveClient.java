package com.klawed.zmq.examples;

import com.klawed.zmq.KlawedZMQClient;
import com.klawed.zmq.Message;
import com.klawed.zmq.MessageType;
import com.klawed.zmq.ZMQException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Scanner;

/**
 * Interactive client example that demonstrates handling tool calls.
 * This example shows how to process TOOL and TOOL_RESULT messages.
 */
public class InteractiveClient {
    private static final Logger logger = LoggerFactory.getLogger(InteractiveClient.class);
    private static final ObjectMapper OBJECT_MAPPER = new ObjectMapper();
    
    public static void main(String[] args) {
        if (args.length < 1) {
            System.err.println("Usage: InteractiveClient <zmq-endpoint>");
            System.err.println("Example: InteractiveClient tcp://127.0.0.1:5555");
            System.exit(1);
        }
        
        String endpoint = args[0];
        
        try (KlawedZMQClient client = new KlawedZMQClient(endpoint);
             Scanner scanner = new Scanner(System.in)) {
            
            logger.info("Connected to {}", endpoint);
            logger.info("Enter prompts (type 'quit' to exit):");
            
            while (true) {
                System.out.print("\nPrompt: ");
                String prompt = scanner.nextLine().trim();
                
                if (prompt.equalsIgnoreCase("quit")) {
                    break;
                }
                
                if (prompt.isEmpty()) {
                    continue;
                }
                
                try {
                    processInteractiveRequest(client, prompt);
                } catch (ZMQException e) {
                    logger.error("Error processing request: {}", e.getMessage(), e);
                }
            }
            
        } catch (ZMQException e) {
            logger.error("ZMQ error: {}", e.getMessage(), e);
            System.exit(1);
        } catch (Exception e) {
            logger.error("Unexpected error: {}", e.getMessage(), e);
            System.exit(1);
        }
    }
    
    private static void processInteractiveRequest(KlawedZMQClient client, String prompt) 
            throws ZMQException {
        logger.info("Sending prompt: {}", prompt);
        
        // Send initial request
        Message response = client.sendText(prompt);
        
        // Process all responses
        while (response != null) {
            switch (response.getMessageType()) {
                case TEXT:
                    handleTextResponse(response);
                    break;
                    
                case TOOL:
                    handleToolRequest(client, response);
                    break;
                    
                case TOOL_RESULT:
                    handleToolResult(response);
                    break;
                    
                case ERROR:
                    handleError(response);
                    break;
                    
                default:
                    logger.warn("Unexpected message type: {}", response.getMessageType());
            }
            
            // Get next response
            response = client.receive(1000); // 1 second timeout
        }
        
        logger.info("Request processing completed");
    }
    
    private static void handleTextResponse(Message response) {
        System.out.println("\n=== AI Response ===");
        System.out.println(response.getContent());
        System.out.println("===================\n");
    }
    
    private static void handleToolRequest(KlawedZMQClient client, Message toolMessage) 
            throws ZMQException {
        String toolName = toolMessage.getToolName();
        String toolId = toolMessage.getToolId();
        ObjectNode parameters = toolMessage.getToolParameters();
        
        System.out.println("\n=== Tool Request ===");
        System.out.println("Tool: " + toolName + " (ID: " + toolId + ")");
        System.out.println("Parameters: " + parameters);
        System.out.println("====================\n");
        
        logger.info("Tool request received: {} (ID: {})", toolName, toolId);
        logger.info("Parameters: {}", parameters);
        
        // Simulate tool execution
        ObjectNode toolOutput = OBJECT_MAPPER.createObjectNode();
        
        switch (toolName) {
            case "Read":
                // Simulate reading a file
                String filePath = parameters != null && parameters.has("file_path") 
                    ? parameters.get("file_path").asText() 
                    : "unknown.txt";
                toolOutput.put("content", "Simulated content of " + filePath);
                toolOutput.put("file_path", filePath);
                break;
                
            case "Bash":
                // Simulate bash command execution
                String command = parameters != null && parameters.has("command")
                    ? parameters.get("command").asText()
                    : "unknown command";
                toolOutput.put("output", "Simulated output of: " + command);
                toolOutput.put("exit_code", 0);
                break;
                
            default:
                // Unknown tool - return error
                toolOutput.put("error", "Unknown tool: " + toolName);
                toolOutput.put("tool_name", toolName);
                
                // Send error result
                Message errorResult = Message.toolResult(toolName, toolId, toolOutput, true);
                client.send(errorResult);
                return;
        }
        
        // Send successful tool result
        Message toolResult = Message.toolResult(toolName, toolId, toolOutput, false);
        client.send(toolResult);
        
        logger.info("Tool result sent for: {} (ID: {})", toolName, toolId);
    }
    
    private static void handleToolResult(Message toolResult) {
        String toolName = toolResult.getToolName();
        String toolId = toolResult.getToolId();
        boolean isError = toolResult.getIsError() != null && toolResult.getIsError();
        ObjectNode output = toolResult.getToolOutput();
        
        if (isError) {
            System.err.println("\n=== Tool Error ===");
            System.err.println("Tool: " + toolName + " (ID: " + toolId + ")");
            System.err.println("Error: " + output);
            System.err.println("==================\n");
            logger.error("Tool error: {} (ID: {}) - {}", toolName, toolId, output);
        } else {
            System.out.println("\n=== Tool Result ===");
            System.out.println("Tool: " + toolName + " (ID: " + toolId + ")");
            System.out.println("Output: " + output);
            System.out.println("===================\n");
            logger.info("Tool completed: {} (ID: {})", toolName, toolId);
            logger.debug("Tool output: {}", output);
        }
    }
    
    private static void handleError(Message error) {
        System.err.println("\n=== Error ===");
        System.err.println(error.getContent());
        System.err.println("=============\n");
    }
}