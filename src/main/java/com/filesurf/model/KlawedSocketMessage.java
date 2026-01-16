package com.filesurf.model;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Standardized socket message format for klawed IPC communication.
 * Based on the simplified socket interface specification.
 */
@JsonIgnoreProperties(ignoreUnknown = true)
@JsonInclude(JsonInclude.Include.NON_NULL)
public class KlawedSocketMessage {
    
    @JsonProperty("messageType")
    private String messageType; // Use ChatConstants for valid message types
    
    @JsonProperty("content")
    private Object content; // Can be string or object depending on messageType
    
    // Getters and setters
    public String getMessageType() {
        return messageType;
    }
    
    public void setMessageType(String messageType) {
        this.messageType = messageType;
    }
    
    public Object getContent() {
        return content;
    }
    
    public void setContent(Object content) {
        this.content = content;
    }
    

    
    /**
     * Create a socket message with the given type and content
     */
    public static KlawedSocketMessage create(String messageType, Object content) {
        KlawedSocketMessage message = new KlawedSocketMessage();
        message.setMessageType(messageType);
        message.setContent(content);
        return message;
    }
    
    /**
     * Create a TEXT message (simplified socket interface)
     */
    public static KlawedSocketMessage createText(String text) {
        return create(ChatConstants.MESSAGE_TYPE_TEXT, text);
    }
    
    /**
     * Create an ERROR message
     */
    public static KlawedSocketMessage createError(String errorMessage) {
        return create(ChatConstants.MESSAGE_TYPE_ERROR, errorMessage);
    }
    
    /**
     * Create a STATUS message
     */
    public static KlawedSocketMessage createStatus(String status) {
        return create(ChatConstants.MESSAGE_TYPE_STATUS, status);
    }
    
    /**
     * Create an AGENT_STATUS message for agent health monitoring.
     * 
     * @param content Human-readable message describing the status
     * @param status One of: "checking", "recovering", "recovered", "failed"
     * @return A KlawedSocketMessage with messageType "AGENT_STATUS"
     */
    public static KlawedSocketMessage createAgentStatus(String content, String status) {
        Map<String, Object> agentStatusContent = new HashMap<>();
        agentStatusContent.put("message", content);
        agentStatusContent.put("status", status);
        return create(ChatConstants.MESSAGE_TYPE_AGENT_STATUS, agentStatusContent);
    }

    
    /**
     * Create a SQLite queue TEXT message (uses SQLiteQueueConstants)
     */
    public static KlawedSocketMessage createSQLiteQueueText(String text) {
        return create(SQLiteQueueConstants.MESSAGE_TYPE_TEXT, text);
    }
    
    /**
     * Create a SQLite queue ERROR message (uses SQLiteQueueConstants)
     */
    public static KlawedSocketMessage createSQLiteQueueError(String errorMessage) {
        return create(SQLiteQueueConstants.MESSAGE_TYPE_ERROR, errorMessage);
    }
    
    /**
     * Create a SQLite queue TOOL message (uses SQLiteQueueConstants)
     */
    public static KlawedSocketMessage createSQLiteQueueTool(String toolName, String toolId, Object parameters) {
        Map<String, Object> toolMessage = new HashMap<>();
        toolMessage.put("toolName", toolName);
        toolMessage.put("toolId", toolId);
        toolMessage.put("toolParameters", parameters);
        return create(SQLiteQueueConstants.MESSAGE_TYPE_TOOL, toolMessage);
    }
    
    /**
     * Create a SQLite queue TOOL_RESULT message (uses SQLiteQueueConstants)
     */
    public static KlawedSocketMessage createSQLiteQueueToolResult(String toolName, String toolId, Object output, boolean isError) {
        Map<String, Object> toolResult = new HashMap<>();
        toolResult.put("toolName", toolName);
        toolResult.put("toolId", toolId);
        toolResult.put("toolOutput", output);
        toolResult.put("isError", isError);
        return create(SQLiteQueueConstants.MESSAGE_TYPE_TOOL_RESULT, toolResult);
    }
    
    /**
     * Create a SQLite queue API_CALL message (uses SQLiteQueueConstants)
     */
    public static KlawedSocketMessage createSQLiteQueueApiCall(String model, String provider, Long estimatedDurationMs) {
        Map<String, Object> apiCallMessage = new HashMap<>();
        if (model != null) {
            apiCallMessage.put("model", model);
        }
        if (provider != null) {
            apiCallMessage.put("provider", provider);
        }
        if (estimatedDurationMs != null) {
            apiCallMessage.put("estimatedDurationMs", estimatedDurationMs);
        }
        apiCallMessage.put("timestamp", System.currentTimeMillis() / 1000);
        apiCallMessage.put("timestampMs", System.currentTimeMillis());
        return create(SQLiteQueueConstants.MESSAGE_TYPE_API_CALL, apiCallMessage);
    }
    
    /**
     * Create an END_AI_TURN message to signal that the AI has completed its turn
     */
    public static KlawedSocketMessage createEndAiTurn() {
        return create(ChatConstants.MESSAGE_TYPE_END_AI_TURN, null);
    }
    
    /**
     * Extract text content from the message based on message type
     */
    public String extractTextContent() {
        if (content == null) {
            return null;
        }
        
        // For TEXT responses, content is directly the text string
        if (ChatConstants.MESSAGE_TYPE_TEXT.equals(messageType) && content instanceof String) {
            String text = (String) content;
            return text;
        }
        

        
        // Handle SQLiteQueueConstants.MESSAGE_TYPE_TEXT
        if (SQLiteQueueConstants.MESSAGE_TYPE_TEXT.equals(messageType) && content instanceof String) {
            String text = (String) content;
            return text;
        }
        
        // For ERROR responses, return error message
        if (ChatConstants.MESSAGE_TYPE_ERROR.equals(messageType)) {
            if (content instanceof String) {
                return (String) content;
            }
            return "Error occurred";
        }
        

        
        // Handle SQLiteQueueConstants.MESSAGE_TYPE_ERROR
        if (SQLiteQueueConstants.MESSAGE_TYPE_ERROR.equals(messageType)) {
            if (content instanceof String) {
                return (String) content;
            }
            return "Error occurred";
        }
        
        // Handle SQLiteQueueConstants.MESSAGE_TYPE_TOOL
        if (SQLiteQueueConstants.MESSAGE_TYPE_TOOL.equals(messageType)) {
            return "[TOOL REQUEST]";
        }
        
        // Handle SQLiteQueueConstants.MESSAGE_TYPE_TOOL_RESULT
        if (SQLiteQueueConstants.MESSAGE_TYPE_TOOL_RESULT.equals(messageType)) {
            return "[TOOL RESULT]";
        }
        
        // Handle SQLiteQueueConstants.MESSAGE_TYPE_API_CALL
        if (SQLiteQueueConstants.MESSAGE_TYPE_API_CALL.equals(messageType)) {
            return "[API CALL IN PROGRESS]";
        }
        
        // Handle END_AI_TURN message
        if (ChatConstants.MESSAGE_TYPE_END_AI_TURN.equals(messageType)) {
            return "[END AI TURN]";
        }
        
        // For STATUS responses, return status message
        if (ChatConstants.MESSAGE_TYPE_STATUS.equals(messageType) && content instanceof String) {
            return (String) content;
        }
        
        // Fallback: if content is a string, return it
        if (content instanceof String) {
            return (String) content;
        }
        
        return null;
    }
}