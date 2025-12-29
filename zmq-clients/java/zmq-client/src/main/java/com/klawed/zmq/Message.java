package com.klawed.zmq;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;

import java.util.Map;
import java.util.Objects;

/**
 * Represents a message in the Klawed ZMQ protocol.
 * This class encapsulates the JSON message format used for communication.
 */
@JsonInclude(JsonInclude.Include.NON_NULL)
public class Message {
    private static final ObjectMapper OBJECT_MAPPER = new ObjectMapper();
    
    @JsonProperty("messageType")
    private MessageType messageType;
    
    @JsonProperty("content")
    private String content;
    
    @JsonProperty("messageId")
    private String messageId;
    
    @JsonProperty("toolName")
    private String toolName;
    
    @JsonProperty("toolId")
    private String toolId;
    
    @JsonProperty("toolParameters")
    private ObjectNode toolParameters;
    
    @JsonProperty("toolOutput")
    private ObjectNode toolOutput;
    
    @JsonProperty("isError")
    private Boolean isError;
    
    // Default constructor for Jackson
    public Message() {}
    
    // Private constructor for builder
    private Message(Builder builder) {
        this.messageType = builder.messageType;
        this.content = builder.content;
        this.messageId = builder.messageId;
        this.toolName = builder.toolName;
        this.toolId = builder.toolId;
        this.toolParameters = builder.toolParameters;
        this.toolOutput = builder.toolOutput;
        this.isError = builder.isError;
    }
    
    // Getters
    public MessageType getMessageType() {
        return messageType;
    }
    
    public String getContent() {
        return content;
    }
    
    public String getMessageId() {
        return messageId;
    }
    
    public String getToolName() {
        return toolName;
    }
    
    public String getToolId() {
        return toolId;
    }
    
    public ObjectNode getToolParameters() {
        return toolParameters;
    }
    
    public ObjectNode getToolOutput() {
        return toolOutput;
    }
    
    public Boolean getIsError() {
        return isError;
    }
    
    // Setters (package-private for builder)
    void setMessageType(MessageType messageType) {
        this.messageType = messageType;
    }
    
    void setContent(String content) {
        this.content = content;
    }
    
    void setMessageId(String messageId) {
        this.messageId = messageId;
    }
    
    void setToolName(String toolName) {
        this.toolName = toolName;
    }
    
    void setToolId(String toolId) {
        this.toolId = toolId;
    }
    
    void setToolParameters(ObjectNode toolParameters) {
        this.toolParameters = toolParameters;
    }
    
    void setToolOutput(ObjectNode toolOutput) {
        this.toolOutput = toolOutput;
    }
    
    void setIsError(Boolean isError) {
        this.isError = isError;
    }
    
    /**
     * Convert the message to JSON string.
     * @return JSON representation of the message
     * @throws JsonProcessingException if JSON serialization fails
     */
    public String toJson() throws JsonProcessingException {
        return OBJECT_MAPPER.writeValueAsString(this);
    }
    
    /**
     * Parse a JSON string into a Message object.
     * @param json JSON string to parse
     * @return Parsed Message object
     * @throws JsonProcessingException if JSON parsing fails
     */
    public static Message fromJson(String json) throws JsonProcessingException {
        return OBJECT_MAPPER.readValue(json, Message.class);
    }
    
    /**
     * Create a simple TEXT message.
     * @param content The text content
     * @return A TEXT message
     */
    public static Message text(String content) {
        return new Builder()
            .messageType(MessageType.TEXT)
            .content(content)
            .build();
    }
    
    /**
     * Create a simple ACK message.
     * @param messageId The message ID to acknowledge
     * @return An ACK message
     */
    public static Message ack(String messageId) {
        return new Builder()
            .messageType(MessageType.ACK)
            .messageId(messageId)
            .build();
    }
    
    /**
     * Create a simple ERROR message.
     * @param content The error description
     * @return An ERROR message
     */
    public static Message error(String content) {
        return new Builder()
            .messageType(MessageType.ERROR)
            .content(content)
            .build();
    }
    
    /**
     * Create a TOOL message.
     * @param toolName Name of the tool
     * @param toolId Unique ID for the tool call
     * @param toolParameters Tool parameters (can be null)
     * @return A TOOL message
     */
    public static Message tool(String toolName, String toolId, ObjectNode toolParameters) {
        return new Builder()
            .messageType(MessageType.TOOL)
            .toolName(toolName)
            .toolId(toolId)
            .toolParameters(toolParameters)
            .build();
    }
    
    /**
     * Create a TOOL_RESULT message.
     * @param toolName Name of the tool
     * @param toolId Unique ID for the tool call
     * @param toolOutput Tool output (can be null)
     * @param isError Whether this is an error result
     * @return A TOOL_RESULT message
     */
    public static Message toolResult(String toolName, String toolId, 
                                     ObjectNode toolOutput, boolean isError) {
        return new Builder()
            .messageType(MessageType.TOOL_RESULT)
            .toolName(toolName)
            .toolId(toolId)
            .toolOutput(toolOutput)
            .isError(isError)
            .build();
    }
    
    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        Message message = (Message) o;
        return messageType == message.messageType &&
               Objects.equals(content, message.content) &&
               Objects.equals(messageId, message.messageId) &&
               Objects.equals(toolName, message.toolName) &&
               Objects.equals(toolId, message.toolId) &&
               Objects.equals(toolParameters, message.toolParameters) &&
               Objects.equals(toolOutput, message.toolOutput) &&
               Objects.equals(isError, message.isError);
    }
    
    @Override
    public int hashCode() {
        return Objects.hash(messageType, content, messageId, toolName, 
                           toolId, toolParameters, toolOutput, isError);
    }
    
    @Override
    public String toString() {
        try {
            return OBJECT_MAPPER.writerWithDefaultPrettyPrinter().writeValueAsString(this);
        } catch (JsonProcessingException e) {
            return "Message{" +
                   "messageType=" + messageType +
                   ", content='" + content + '\'' +
                   ", messageId='" + messageId + '\'' +
                   ", toolName='" + toolName + '\'' +
                   ", toolId='" + toolId + '\'' +
                   '}';
        }
    }
    
    /**
     * Builder for creating Message objects.
     */
    public static class Builder {
        private MessageType messageType;
        private String content;
        private String messageId;
        private String toolName;
        private String toolId;
        private ObjectNode toolParameters;
        private ObjectNode toolOutput;
        private Boolean isError;
        
        public Builder messageType(MessageType messageType) {
            this.messageType = messageType;
            return this;
        }
        
        public Builder content(String content) {
            this.content = content;
            return this;
        }
        
        public Builder messageId(String messageId) {
            this.messageId = messageId;
            return this;
        }
        
        public Builder toolName(String toolName) {
            this.toolName = toolName;
            return this;
        }
        
        public Builder toolId(String toolId) {
            this.toolId = toolId;
            return this;
        }
        
        public Builder toolParameters(ObjectNode toolParameters) {
            this.toolParameters = toolParameters;
            return this;
        }
        
        public Builder toolOutput(ObjectNode toolOutput) {
            this.toolOutput = toolOutput;
            return this;
        }
        
        public Builder isError(Boolean isError) {
            this.isError = isError;
            return this;
        }
        
        public Message build() {
            // Validate required fields based on message type
            if (messageType == null) {
                throw new IllegalStateException("messageType is required");
            }
            
            switch (messageType) {
                case TEXT:
                    if (content == null) {
                        throw new IllegalStateException("content is required for TEXT messages");
                    }
                    break;
                case ACK:
                case NACK:
                    if (messageId == null) {
                        throw new IllegalStateException("messageId is required for ACK/NACK messages");
                    }
                    break;
                case TOOL:
                    if (toolName == null || toolId == null) {
                        throw new IllegalStateException("toolName and toolId are required for TOOL messages");
                    }
                    break;
                case TOOL_RESULT:
                    if (toolName == null || toolId == null) {
                        throw new IllegalStateException("toolName and toolId are required for TOOL_RESULT messages");
                    }
                    if (isError == null) {
                        isError = false;
                    }
                    break;
                case ERROR:
                    if (content == null) {
                        throw new IllegalStateException("content is required for ERROR messages");
                    }
                    break;
            }
            
            return new Message(this);
        }
    }
}