package com.klawed.zmq;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class MessageTest {
    private static final ObjectMapper OBJECT_MAPPER = new ObjectMapper();
    
    @Test
    void testTextMessageCreation() {
        Message message = Message.text("Hello, world!");
        
        assertEquals(MessageType.TEXT, message.getMessageType());
        assertEquals("Hello, world!", message.getContent());
        assertNull(message.getMessageId());
        assertNull(message.getToolName());
        assertNull(message.getToolId());
        assertNull(message.getToolParameters());
        assertNull(message.getToolOutput());
        assertNull(message.getIsError());
    }
    
    @Test
    void testAckMessageCreation() {
        Message message = Message.ack("msg123");
        
        assertEquals(MessageType.ACK, message.getMessageType());
        assertEquals("msg123", message.getMessageId());
        assertNull(message.getContent());
        assertNull(message.getToolName());
        assertNull(message.getToolId());
        assertNull(message.getToolParameters());
        assertNull(message.getToolOutput());
        assertNull(message.getIsError());
    }
    
    @Test
    void testErrorMessageCreation() {
        Message message = Message.error("Something went wrong");
        
        assertEquals(MessageType.ERROR, message.getMessageType());
        assertEquals("Something went wrong", message.getContent());
        assertNull(message.getMessageId());
        assertNull(message.getToolName());
        assertNull(message.getToolId());
        assertNull(message.getToolParameters());
        assertNull(message.getToolOutput());
        assertNull(message.getIsError());
    }
    
    @Test
    void testToolMessageCreation() {
        ObjectNode parameters = OBJECT_MAPPER.createObjectNode();
        parameters.put("file_path", "README.md");
        
        Message message = Message.tool("Read", "tool123", parameters);
        
        assertEquals(MessageType.TOOL, message.getMessageType());
        assertEquals("Read", message.getToolName());
        assertEquals("tool123", message.getToolId());
        assertEquals(parameters, message.getToolParameters());
        assertNull(message.getContent());
        assertNull(message.getMessageId());
        assertNull(message.getToolOutput());
        assertNull(message.getIsError());
    }
    
    @Test
    void testToolResultMessageCreation() {
        ObjectNode output = OBJECT_MAPPER.createObjectNode();
        output.put("content", "# My Project");
        output.put("file_path", "README.md");
        
        Message message = Message.toolResult("Read", "tool123", output, false);
        
        assertEquals(MessageType.TOOL_RESULT, message.getMessageType());
        assertEquals("Read", message.getToolName());
        assertEquals("tool123", message.getToolId());
        assertEquals(output, message.getToolOutput());
        assertFalse(message.getIsError());
        assertNull(message.getContent());
        assertNull(message.getMessageId());
        assertNull(message.getToolParameters());
    }
    
    @Test
    void testJsonSerialization() throws JsonProcessingException {
        Message message = new Message.Builder()
            .messageType(MessageType.TEXT)
            .content("Hello, world!")
            .messageId("msg123")
            .build();
        
        String json = message.toJson();
        assertNotNull(json);
        assertTrue(json.contains("TEXT"));
        assertTrue(json.contains("Hello, world!"));
        assertTrue(json.contains("msg123"));
        
        // Verify round-trip
        Message parsed = Message.fromJson(json);
        assertEquals(message, parsed);
    }
    
    @Test
    void testJsonDeserialization() throws JsonProcessingException {
        String json = "{\"messageType\":\"TEXT\",\"content\":\"Hello!\",\"messageId\":\"test123\"}";
        
        Message message = Message.fromJson(json);
        
        assertEquals(MessageType.TEXT, message.getMessageType());
        assertEquals("Hello!", message.getContent());
        assertEquals("test123", message.getMessageId());
    }
    
    @Test
    void testBuilderValidation() {
        // Missing messageType should throw
        assertThrows(IllegalStateException.class, () -> {
            new Message.Builder().content("test").build();
        });
        
        // TEXT without content should throw
        assertThrows(IllegalStateException.class, () -> {
            new Message.Builder().messageType(MessageType.TEXT).build();
        });
        
        // ACK without messageId should throw
        assertThrows(IllegalStateException.class, () -> {
            new Message.Builder().messageType(MessageType.ACK).build();
        });
        
        // TOOL without toolName should throw
        assertThrows(IllegalStateException.class, () -> {
            new Message.Builder().messageType(MessageType.TOOL).toolId("id").build();
        });
        
        // TOOL without toolId should throw
        assertThrows(IllegalStateException.class, () -> {
            new Message.Builder().messageType(MessageType.TOOL).toolName("Read").build();
        });
        
        // TOOL_RESULT without toolName should throw
        assertThrows(IllegalStateException.class, () -> {
            new Message.Builder().messageType(MessageType.TOOL_RESULT).toolId("id").build();
        });
        
        // TOOL_RESULT without toolId should throw
        assertThrows(IllegalStateException.class, () -> {
            new Message.Builder().messageType(MessageType.TOOL_RESULT).toolName("Read").build();
        });
        
        // ERROR without content should throw
        assertThrows(IllegalStateException.class, () -> {
            new Message.Builder().messageType(MessageType.ERROR).build();
        });
    }
    
    @Test
    void testEqualsAndHashCode() {
        Message message1 = Message.text("Hello");
        Message message2 = Message.text("Hello");
        Message message3 = Message.text("World");
        
        assertEquals(message1, message2);
        assertNotEquals(message1, message3);
        assertEquals(message1.hashCode(), message2.hashCode());
        assertNotEquals(message1.hashCode(), message3.hashCode());
    }
    
    @Test
    void testToString() {
        Message message = Message.text("Test");
        String str = message.toString();
        assertNotNull(str);
        assertTrue(str.contains("Test") || str.contains("TEXT"));
    }
}