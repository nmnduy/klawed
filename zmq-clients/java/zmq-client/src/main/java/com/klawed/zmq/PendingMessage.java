package com.klawed.zmq;

import java.util.concurrent.atomic.AtomicInteger;

/**
 * Represents a message that is pending acknowledgment.
 * Tracks retry count and sent time for reliable delivery.
 */
class PendingMessage {
    private final String messageId;
    private final String messageJson;
    private final long sentTimeMs;
    private final AtomicInteger retryCount;
    
    /**
     * Create a new pending message.
     * @param messageId The unique message ID
     * @param messageJson The JSON message string
     * @param sentTimeMs The time when the message was sent (milliseconds since epoch)
     */
    public PendingMessage(String messageId, String messageJson, long sentTimeMs) {
        this.messageId = messageId;
        this.messageJson = messageJson;
        this.sentTimeMs = sentTimeMs;
        this.retryCount = new AtomicInteger(0);
    }
    
    /**
     * Get the message ID.
     * @return The message ID
     */
    public String getMessageId() {
        return messageId;
    }
    
    /**
     * Get the JSON message string.
     * @return The JSON message
     */
    public String getMessageJson() {
        return messageJson;
    }
    
    /**
     * Get the time when the message was sent.
     * @return Sent time in milliseconds since epoch
     */
    public long getSentTimeMs() {
        return sentTimeMs;
    }
    
    /**
     * Get the current retry count.
     * @return Number of retry attempts
     */
    public int getRetryCount() {
        return retryCount.get();
    }
    
    /**
     * Increment the retry count.
     * @return The new retry count
     */
    public int incrementRetryCount() {
        return retryCount.incrementAndGet();
    }
    
    /**
     * Calculate the elapsed time since the message was sent.
     * @param currentTimeMs Current time in milliseconds since epoch
     * @return Elapsed time in milliseconds
     */
    public long getElapsedTimeMs(long currentTimeMs) {
        return currentTimeMs - sentTimeMs;
    }
    
    @Override
    public String toString() {
        return "PendingMessage{" +
               "messageId='" + messageId + '\'' +
               ", sentTimeMs=" + sentTimeMs +
               ", retryCount=" + retryCount +
               '}';
    }
}