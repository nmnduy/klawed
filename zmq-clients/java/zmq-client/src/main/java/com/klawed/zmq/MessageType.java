package com.klawed.zmq;

/**
 * Enum representing the message types in the Klawed ZMQ protocol.
 * These correspond to the "messageType" field in JSON messages.
 */
public enum MessageType {
    /** Text processing request and successful response */
    TEXT("TEXT"),
    
    /** Error response */
    ERROR("ERROR"),
    
    /** Tool execution request (sent before tool execution) */
    TOOL("TOOL"),
    
    /** Tool execution result (sent after tool execution) */
    TOOL_RESULT("TOOL_RESULT"),
    
    /** Acknowledgment message */
    ACK("ACK"),
    
    /** Negative acknowledgment message */
    NACK("NACK");
    
    private final String value;
    
    MessageType(String value) {
        this.value = value;
    }
    
    /**
     * Get the string value of the message type.
     * @return The string value used in JSON messages
     */
    public String getValue() {
        return value;
    }
    
    /**
     * Convert a string to a MessageType enum.
     * @param value The string value from JSON
     * @return The corresponding MessageType, or null if not found
     */
    public static MessageType fromString(String value) {
        if (value == null) {
            return null;
        }
        for (MessageType type : MessageType.values()) {
            if (type.value.equals(value)) {
                return type;
            }
        }
        return null;
    }
    
    @Override
    public String toString() {
        return value;
    }
}