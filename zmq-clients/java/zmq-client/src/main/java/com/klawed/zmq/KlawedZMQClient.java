package com.klawed.zmq;

import com.fasterxml.jackson.core.JsonProcessingException;
import org.zeromq.SocketType;
import org.zeromq.ZContext;
import org.zeromq.ZMQ;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.Closeable;
import java.io.IOException;
import java.time.Instant;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.locks.ReentrantLock;

/**
 * Main client class for communicating with Klawed's ZMQ daemon.
 * Implements reliable message delivery with ACK/NACK system.
 */
public class KlawedZMQClient implements Closeable {
    private static final Logger logger = LoggerFactory.getLogger(KlawedZMQClient.class);
    
    // Default configuration
    private static final int DEFAULT_MAX_PENDING = 50;
    private static final long DEFAULT_ACK_TIMEOUT_MS = 3000;
    private static final int DEFAULT_MAX_RETRIES = 5;
    private static final int DEFAULT_RECEIVE_TIMEOUT_MS = 10000;
    private static final int DEFAULT_SEND_TIMEOUT_MS = 5000;
    private static final int RESEND_CHECK_INTERVAL_MS = 1000;
    
    private final String endpoint;
    private final ZContext context;
    private final ZMQ.Socket socket;
    private final MessageIdGenerator messageIdGenerator;
    private final AtomicBoolean running;
    private final ReentrantLock sendLock;
    
    // Pending message tracking
    private final Map<String, PendingMessage> pendingMessages;
    private final int maxPendingMessages;
    private final long ackTimeoutMs;
    private final int maxRetries;
    
    // Background thread for resending pending messages
    private final ScheduledExecutorService scheduler;
    
    /**
     * Create a new ZMQ client with default settings.
     * @param endpoint ZMQ endpoint (e.g., "tcp://127.0.0.1:5555")
     * @throws ZMQException if connection fails
     */
    public KlawedZMQClient(String endpoint) throws ZMQException {
        this(endpoint, DEFAULT_MAX_PENDING, DEFAULT_ACK_TIMEOUT_MS, DEFAULT_MAX_RETRIES);
    }
    
    /**
     * Create a new ZMQ client with custom settings.
     * @param endpoint ZMQ endpoint (e.g., "tcp://127.0.0.1:5555")
     * @param maxPendingMessages Maximum number of pending messages
     * @param ackTimeoutMs Timeout for ACK in milliseconds
     * @param maxRetries Maximum number of retry attempts
     * @throws ZMQException if connection fails
     */
    public KlawedZMQClient(String endpoint, int maxPendingMessages, 
                          long ackTimeoutMs, int maxRetries) throws ZMQException {
        this.endpoint = endpoint;
        this.maxPendingMessages = maxPendingMessages;
        this.ackTimeoutMs = ackTimeoutMs;
        this.maxRetries = maxRetries;
        
        this.messageIdGenerator = new MessageIdGenerator();
        this.running = new AtomicBoolean(true);
        this.sendLock = new ReentrantLock();
        this.pendingMessages = new ConcurrentHashMap<>();
        
        // Initialize ZMQ
        this.context = new ZContext();
        this.socket = context.createSocket(SocketType.PAIR);
        
        // Configure socket
        socket.setLinger(1000); // 1 second linger for clean shutdown
        socket.setTCPKeepAlive(1);
        socket.setTCPKeepAliveIdle(60);
        socket.setTCPKeepAliveInterval(5);
        socket.setTCPKeepAliveCount(3);
        socket.setReceiveTimeOut(DEFAULT_RECEIVE_TIMEOUT_MS);
        socket.setSendTimeOut(DEFAULT_SEND_TIMEOUT_MS);
        
        // Connect to endpoint
        logger.info("Connecting to ZMQ endpoint: {}", endpoint);
        boolean connected = socket.connect(endpoint);
        if (!connected) {
            context.close();
            throw new ZMQException("Failed to connect to endpoint: " + endpoint);
        }
        
        logger.info("Connected to ZMQ endpoint: {}", endpoint);
        
        // Start background scheduler for resending pending messages
        this.scheduler = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread thread = new Thread(r, "KlawedZMQClient-ResendThread");
            thread.setDaemon(true);
            return thread;
        });
        
        scheduler.scheduleAtFixedRate(this::checkAndResendPending,
            RESEND_CHECK_INTERVAL_MS, RESEND_CHECK_INTERVAL_MS, TimeUnit.MILLISECONDS);
        
        logger.info("Klawed ZMQ client initialized (maxPending={}, ackTimeout={}ms, maxRetries={})",
                   maxPendingMessages, ackTimeoutMs, maxRetries);
    }
    
    /**
     * Send a TEXT message and wait for response.
     * @param content The text content to send
     * @return The response message
     * @throws ZMQException if communication fails
     */
    public Message sendText(String content) throws ZMQException {
        Message request = Message.text(content);
        return sendWithReliableDelivery(request);
    }
    
    /**
     * Send a message with reliable delivery (message ID and ACK tracking).
     * @param message The message to send
     * @return The response message (if any)
     * @throws ZMQException if communication fails
     */
    public Message sendWithReliableDelivery(Message message) throws ZMQException {
        sendLock.lock();
        try {
            // Generate message ID if not provided
            if (message.getMessageId() == null) {
                String messageJson;
                try {
                    messageJson = message.toJson();
                } catch (JsonProcessingException e) {
                    throw new ZMQException("Failed to serialize message to JSON", e);
                }
                String messageId = messageIdGenerator.generateMessageId(messageJson);
                // We need to create a new message with the generated ID
                message = new Message.Builder()
                    .messageType(message.getMessageType())
                    .content(message.getContent())
                    .messageId(messageId)
                    .toolName(message.getToolName())
                    .toolId(message.getToolId())
                    .toolParameters(message.getToolParameters())
                    .toolOutput(message.getToolOutput())
                    .isError(message.getIsError())
                    .build();
            }
            
            // Check if we can add more pending messages
            if (pendingMessages.size() >= maxPendingMessages) {
                throw new ZMQException("Pending message queue full (max=" + maxPendingMessages + ")");
            }
            
            // Convert message to JSON
            String messageJson;
            try {
                messageJson = message.toJson();
            } catch (JsonProcessingException e) {
                throw new ZMQException("Failed to serialize message to JSON", e);
            }
            
            // Send message
            boolean sent = socket.send(messageJson);
            if (!sent) {
                throw new ZMQException("Failed to send message");
            }
            
            logger.debug("Sent message: {} (ID: {})", message.getMessageType(), message.getMessageId());
            
            // Track as pending message
            PendingMessage pending = new PendingMessage(
                message.getMessageId(), messageJson, Instant.now().toEpochMilli());
            pendingMessages.put(message.getMessageId(), pending);
            
            // Wait for response
            return waitForResponse(message.getMessageId());
            
        } finally {
            sendLock.unlock();
        }
    }
    
    /**
     * Send a message without waiting for response (fire-and-forget).
     * @param message The message to send
     * @throws ZMQException if send fails
     */
    public void send(Message message) throws ZMQException {
        sendLock.lock();
        try {
            String messageJson;
            try {
                messageJson = message.toJson();
            } catch (JsonProcessingException e) {
                throw new ZMQException("Failed to serialize message to JSON", e);
            }
            
            boolean sent = socket.send(messageJson);
            if (!sent) {
                throw new ZMQException("Failed to send message");
            }
            
            logger.debug("Sent message (fire-and-forget): {}", message.getMessageType());
        } finally {
            sendLock.unlock();
        }
    }
    
    /**
     * Receive a message with timeout.
     * @return The received message, or null if timeout
     * @throws ZMQException if receive fails
     */
    public Message receive() throws ZMQException {
        return receive(DEFAULT_RECEIVE_TIMEOUT_MS);
    }
    
    /**
     * Receive a message with custom timeout.
     * @param timeoutMs Timeout in milliseconds
     * @return The received message, or null if timeout
     * @throws ZMQException if receive fails
     */
    public Message receive(int timeoutMs) throws ZMQException {
        socket.setReceiveTimeOut(timeoutMs);
        
        byte[] data = socket.recv();
        if (data == null) {
            // Timeout
            return null;
        }
        
        String json = new String(data, java.nio.charset.StandardCharsets.UTF_8);
        
        try {
            Message message = Message.fromJson(json);
            
            // Handle ACK messages
            if (message.getMessageType() == MessageType.ACK) {
                handleAck(message);
                return null; // ACK is internal, not returned to caller
            }
            
            // Handle NACK messages
            if (message.getMessageType() == MessageType.NACK) {
                handleNack(message);
                return message; // NACK is returned as it indicates an error
            }
            
            logger.debug("Received message: {} (ID: {})", 
                        message.getMessageType(), message.getMessageId());
            
            // Send ACK for non-ACK messages
            if (message.getMessageId() != null && 
                message.getMessageType() != MessageType.ACK) {
                sendAck(message.getMessageId());
            }
            
            return message;
            
        } catch (JsonProcessingException e) {
            throw new ZMQException("Failed to parse received message as JSON", e);
        }
    }
    
    /**
     * Wait for a response to a specific message.
     * @param messageId The message ID to wait for response to
     * @return The response message
     * @throws ZMQException if timeout or error occurs
     */
    private Message waitForResponse(String messageId) throws ZMQException {
        long startTime = System.currentTimeMillis();
        long timeoutTime = startTime + ackTimeoutMs * (maxRetries + 1);
        
        while (System.currentTimeMillis() < timeoutTime) {
            Message response = receive(1000); // Check every second
            
            if (response != null) {
                // Check if this is a response to our message
                // For TEXT messages, we expect a TEXT response
                // For tool-related messages, we might get TOOL or TOOL_RESULT
                return response;
            }
            
            // Check if message is still pending
            if (!pendingMessages.containsKey(messageId)) {
                // Message was acknowledged
                return null;
            }
        }
        
        // Timeout - remove from pending messages
        pendingMessages.remove(messageId);
        throw new ZMQException("Timeout waiting for response to message: " + messageId);
    }
    
    /**
     * Send an ACK for a received message.
     * @param messageId The message ID to acknowledge
     * @throws ZMQException if send fails
     */
    private void sendAck(String messageId) throws ZMQException {
        Message ack = Message.ack(messageId);
        send(ack);
        logger.debug("Sent ACK for message: {}", messageId);
    }
    
    /**
     * Handle an incoming ACK message.
     * @param ack The ACK message
     */
    private void handleAck(Message ack) {
        String messageId = ack.getMessageId();
        if (messageId != null) {
            PendingMessage removed = pendingMessages.remove(messageId);
            if (removed != null) {
                logger.debug("Received ACK for message: {} (retries: {})", 
                           messageId, removed.getRetryCount());
            } else {
                logger.warn("Received ACK for unknown message: {}", messageId);
            }
        }
    }
    
    /**
     * Handle an incoming NACK message.
     * @param nack The NACK message
     * @throws ZMQException if NACK indicates a fatal error
     */
    private void handleNack(Message nack) throws ZMQException {
        String messageId = nack.getMessageId();
        String error = nack.getContent();
        
        // Remove from pending messages
        pendingMessages.remove(messageId);
        
        logger.error("Received NACK for message: {} - {}", messageId, error);
        throw new ZMQException("Server rejected message: " + error);
    }
    
    /**
     * Check and resend pending messages that have timed out.
     */
    private void checkAndResendPending() {
        if (!running.get()) {
            return;
        }
        
        long currentTime = Instant.now().toEpochMilli();
        
        for (PendingMessage pending : pendingMessages.values()) {
            long elapsed = pending.getElapsedTimeMs(currentTime);
            
            if (elapsed >= ackTimeoutMs) {
                int retryCount = pending.getRetryCount();
                
                if (retryCount >= maxRetries) {
                    // Max retries exceeded, give up
                    logger.error("Message {} exceeded max retries ({}), dropping",
                               pending.getMessageId(), maxRetries);
                    pendingMessages.remove(pending.getMessageId());
                    continue;
                }
                
                // Resend message
                try {
                    boolean sent = socket.send(pending.getMessageJson());
                    if (sent) {
                        pending.incrementRetryCount();
                        logger.warn("Resent message {} (attempt {}/{})",
                                  pending.getMessageId(), retryCount + 1, maxRetries);
                    } else {
                        logger.error("Failed to resend message {}", pending.getMessageId());
                    }
                } catch (Exception e) {
                    logger.error("Error resending message {}", pending.getMessageId(), e);
                }
            }
        }
    }
    
    /**
     * Get the number of pending messages waiting for ACK.
     * @return Number of pending messages
     */
    public int getPendingMessageCount() {
        return pendingMessages.size();
    }
    
    /**
     * Get the endpoint this client is connected to.
     * @return The ZMQ endpoint
     */
    public String getEndpoint() {
        return endpoint;
    }
    
    /**
     * Check if the client is running.
     * @return true if running, false if closed
     */
    public boolean isRunning() {
        return running.get();
    }
    
    /**
     * Close the client and release resources.
     */
    @Override
    public void close() throws IOException {
        if (!running.compareAndSet(true, false)) {
            return; // Already closed
        }
        
        logger.info("Closing Klawed ZMQ client");
        
        // Shutdown scheduler
        scheduler.shutdown();
        try {
            if (!scheduler.awaitTermination(5, TimeUnit.SECONDS)) {
                scheduler.shutdownNow();
            }
        } catch (InterruptedException e) {
            scheduler.shutdownNow();
            Thread.currentThread().interrupt();
        }
        
        // Close ZMQ resources
        if (socket != null) {
            socket.close();
        }
        if (context != null) {
            context.close();
        }
        
        // Clear pending messages
        pendingMessages.clear();
        
        logger.info("Klawed ZMQ client closed");
    }
}