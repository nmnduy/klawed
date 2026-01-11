package com.filesurf.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import jakarta.inject.Inject;

import java.io.IOException;
import java.net.UnixDomainSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.logging.Logger;

/**
 * Unix domain socket client for communicating with klawed agent via Unix sockets.
 * Based on the Unix socket specification from klawedspace/docs/unix-socket.md
 */
public class UnixSocketClient {
    
    private static final Logger LOGGER = Logger.getLogger(UnixSocketClient.class.getName());
    private final ObjectMapper objectMapper = new ObjectMapper();
    
    @Inject
    FileChatService fileChatService;
    
    private String sessionId;
    private SocketChannel channel;
    private final AtomicBoolean connected = new AtomicBoolean(false);
    private ExecutorService executorService;
    private final ByteBuffer lengthBuffer = ByteBuffer.allocate(4);
    
    // Configuration
    public static class Config {
        // Socket settings
        private String socketPath;
        private String sessionId;
        private FileChatService fileChatService;
        
        // Client configuration
        private int timeoutMs = 30000; // 30 seconds default
        private int maxMessageSize = 64 * 1024 * 1024; // 64 MB default
        
        public Config(String socketPath) {
            this.socketPath = socketPath;
        }
        
        public Config withSessionId(String sessionId) {
            this.sessionId = sessionId;
            return this;
        }
        
        public Config withFileChatService(FileChatService fileChatService) {
            this.fileChatService = fileChatService;
            return this;
        }
        
        public Config withTimeoutMs(int timeoutMs) {
            this.timeoutMs = timeoutMs;
            return this;
        }
        
        public Config withMaxMessageSize(int maxMessageSize) {
            this.maxMessageSize = maxMessageSize;
            return this;
        }
        
        public String getSocketPath() { return socketPath; }
        public String getSessionId() { return sessionId; }
        public FileChatService getFileChatService() { return fileChatService; }
        public int getTimeoutMs() { return timeoutMs; }
        public int getMaxMessageSize() { return maxMessageSize; }
    }
    
    private Config config;
    
    public UnixSocketClient() {
        this.lengthBuffer.order(ByteOrder.BIG_ENDIAN);
    }
    
    public UnixSocketClient(Config config) {
        this();
        this.config = config;
        this.sessionId = config.getSessionId();
        this.fileChatService = config.getFileChatService();
    }
    
    /**
     * Connect to the Unix socket
     */
    public void connect() throws IOException {
        LOGGER.info("[SESSION:" + sessionId + "] Connecting to klawed Unix socket: " + config.getSocketPath());
        
        if (connected.get()) {
            LOGGER.warning("[SESSION:" + sessionId + "] Already connected to Unix socket");
            return;
        }
        
        UnixDomainSocketAddress address = UnixDomainSocketAddress.of(Path.of(config.getSocketPath()));
        
        // Use simpler connection method like TestSimpleUDS
        // Retry connection to handle race condition with klawed socket creation
        int maxAttempts = 30; // 30 attempts * 100ms = 3 seconds total
        int attempt = 0;
        
        while (attempt < maxAttempts) {
            try {
                // Use TestSimpleUDS method for simplicity
                channel = SocketChannel.open(address);
                channel.configureBlocking(true);
                
                connected.set(true);
                
                // Initialize executor service for async operations
                executorService = Executors.newSingleThreadExecutor();
                
                LOGGER.info("[SESSION:" + sessionId + "] Successfully connected to Unix socket (attempt " + (attempt + 1) + ")");
                return;
                
            } catch (IOException e) {
                attempt++;
                if (attempt >= maxAttempts) {
                    LOGGER.severe("[SESSION:" + sessionId + "] Failed to connect to Unix socket after " + maxAttempts + " attempts: " + e.getMessage());
                    throw e;
                }
                
                // Wait before retrying (klawed might still be starting up)
                try {
                    Thread.sleep(100);
                } catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    throw new IOException("Interrupted while waiting to connect", ie);
                }
                
                LOGGER.fine("[SESSION:" + sessionId + "] Connection attempt " + attempt + " failed, retrying...");
            }
        }
    }
    
    /**
     * Send message to the agent and get response
     */
    public String sendMessage(String message) throws IOException, InterruptedException {
        LOGGER.info("[SESSION:" + sessionId + "] Sending message to klawed via Unix socket");
        LOGGER.info("[SESSION:" + sessionId + "] Message: " + 
                   message.substring(0, Math.min(100, message.length())) + 
                   (message.length() > 100 ? "..." : ""));
        
        // Send TEXT message
        sendTextMessage(message);
        
        // Read single response (like TestSimpleUDS does)
        KlawedResponse response = receiveResponse();
        
        if (response.isError()) {
            throw new IOException("Klawed returned error: " + response.getContent());
        }
        
        if (!response.isText()) {
            throw new IOException("Expected TEXT response but got: " + response.getMessageType());
        }
        
        String responseText = response.getContent();
        LOGGER.info("[SESSION:" + sessionId + "] Received response from klawed via Unix socket");
        LOGGER.info("[SESSION:" + sessionId + "] Response: " + 
                   responseText.substring(0, Math.min(100, responseText.length())) + 
                   (responseText.length() > 100 ? "..." : ""));
        
        return responseText;
    }
    
    /**
     * Send message asynchronously
     */
    public void sendMessageAsync(String message) {
        LOGGER.info("[SESSION:" + sessionId + "] Sending message asynchronously to klawed via Unix socket");
        
        executorService.submit(() -> {
            try {
                sendMessage(message);
            } catch (Exception e) {
                LOGGER.severe("[SESSION:" + sessionId + "] Error in async message send: " + e.getMessage());
                e.printStackTrace();
            }
        });
    }
    
    /**
     * Send a TEXT message to klawed
     */
    private void sendTextMessage(String text) throws IOException {
        ObjectNode message = objectMapper.createObjectNode();
        message.put("messageType", "TEXT");
        message.put("content", text);
        
        sendJsonMessage(message.toString());
    }
    
    /**
     * Send a JSON message with proper framing
     */
    private void sendJsonMessage(String json) throws IOException {
        byte[] payload = json.getBytes(StandardCharsets.UTF_8);
        
        if (payload.length > config.getMaxMessageSize()) {
            throw new IOException("Message too large: " + payload.length + " bytes (max: " + config.getMaxMessageSize() + ")");
        }
        
        // Prepare length header
        lengthBuffer.clear();
        lengthBuffer.putInt(payload.length);
        lengthBuffer.flip();
        
        // Send length header
        while (lengthBuffer.hasRemaining()) {
            channel.write(lengthBuffer);
        }
        
        // Send payload
        ByteBuffer payloadBuffer = ByteBuffer.wrap(payload);
        while (payloadBuffer.hasRemaining()) {
            channel.write(payloadBuffer);
        }
        
        LOGGER.fine("[SESSION:" + sessionId + "] Sent message with length: " + payload.length);
    }
    
    /**
     * Receive a response from klawed
     */
    private KlawedResponse receiveResponse() throws IOException {
        String json = receiveMessage();
        return parseResponse(json);
    }
    
    /**
     * Receive a message with proper framing
     */
    private String receiveMessage() throws IOException {
        // Read length header
        lengthBuffer.clear();
        readFully(lengthBuffer);
        lengthBuffer.flip();
        int length = lengthBuffer.getInt();
        
        if (length <= 0 || length > config.getMaxMessageSize()) {
            throw new IOException("Invalid message length: " + length);
        }
        
        // Read payload
        ByteBuffer payloadBuffer = ByteBuffer.allocate(length);
        readFully(payloadBuffer);
        payloadBuffer.flip();
        
        String message = StandardCharsets.UTF_8.decode(payloadBuffer).toString();
        LOGGER.fine("[SESSION:" + sessionId + "] Received message with length: " + length);
        
        return message;
    }
    
    /**
     * Read buffer fully with timeout
     */
    private void readFully(ByteBuffer buffer) throws IOException {
        long deadline = System.currentTimeMillis() + config.getTimeoutMs();
        
        while (buffer.hasRemaining()) {
            if (System.currentTimeMillis() > deadline) {
                throw new IOException("Read timeout");
            }
            
            int read = channel.read(buffer);
            if (read < 0) {
                throw new IOException("Connection closed");
            }
            if (read == 0) {
                // Brief sleep to avoid busy-waiting
                try {
                    Thread.sleep(10);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new IOException("Interrupted", e);
                }
            }
        }
    }
    
    /**
     * Parse JSON response into KlawedResponse object
     */
    private KlawedResponse parseResponse(String json) throws IOException {
        try {
            ObjectNode node = objectMapper.readValue(json, ObjectNode.class);
            String messageType = node.has("messageType") ? node.get("messageType").asText() : null;
            String content = node.has("content") ? node.get("content").asText() : null;
            
            return new KlawedResponse(messageType, content);
        } catch (Exception e) {
            throw new IOException("Failed to parse response JSON: " + e.getMessage(), e);
        }
    }
    
    /**
     * Check if client is connected
     */
    public boolean isConnected() {
        return connected.get() && channel != null && channel.isConnected();
    }
    
    /**
     * Disconnect from the socket
     */
    public void disconnect() {
        LOGGER.info("[SESSION:" + sessionId + "] Disconnecting from Unix socket");
        
        connected.set(false);
        
        if (channel != null) {
            try {
                channel.close();
            } catch (IOException e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Error closing socket channel: " + e.getMessage());
            }
            channel = null;
        }
        
        if (executorService != null) {
            executorService.shutdown();
            try {
                if (!executorService.awaitTermination(5, TimeUnit.SECONDS)) {
                    executorService.shutdownNow();
                }
            } catch (InterruptedException e) {
                executorService.shutdownNow();
                Thread.currentThread().interrupt();
            }
        }
        
        LOGGER.info("[SESSION:" + sessionId + "] Unix socket client shutdown complete");
    }
    
    /**
     * Response from klawed
     */
    public static class KlawedResponse {
        private final String messageType;
        private final String content;
        
        public KlawedResponse(String messageType, String content) {
            this.messageType = messageType;
            this.content = content;
        }
        
        public String getMessageType() { return messageType; }
        public String getContent() { return content; }
        public boolean isText() { return "TEXT".equals(messageType); }
        public boolean isError() { return "ERROR".equals(messageType); }
        
        @Override
        public String toString() {
            return String.format("KlawedResponse{type=%s, content=%s}", messageType, 
                content != null && content.length() > 50 ? content.substring(0, 50) + "..." : content);
        }
    }
    
    // Getters for testing
    public Config getConfig() { return config; }
    public String getSessionId() { return sessionId; }
}