package com.filesurf.model;

import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneId;

public class ChatMessageRecord {
    private Long id;
    private String sessionId; // Session ID is now passed explicitly (not from chat_session_id column)
    private String sender;
    private String receiver;
    private String content;
    private String messageType;
    private Boolean sent;
    private LocalDateTime createdAt;
    private LocalDateTime sentAt;

    public ChatMessageRecord() {}

    public ChatMessageRecord(Long id, String sessionId, String sender, String receiver,
                           String content, String messageType, Boolean sent,
                           LocalDateTime createdAt, LocalDateTime sentAt) {
        this.id = id;
        this.sessionId = sessionId;
        this.sender = sender;
        this.receiver = receiver;
        this.content = content;
        this.messageType = messageType;
        this.sent = sent;
        this.createdAt = createdAt;
        this.sentAt = sentAt;
    }

    // Getters and setters
    public Long getId() { return id; }
    public void setId(Long id) { this.id = id; }

    public String getSessionId() { return sessionId; }
    public void setSessionId(String sessionId) { this.sessionId = sessionId; }

    public String getSender() { return sender; }
    public void setSender(String sender) { this.sender = sender; }

    public String getReceiver() { return receiver; }
    public void setReceiver(String receiver) { this.receiver = receiver; }

    public String getContent() { return content; }
    public void setContent(String content) { this.content = content; }

    public String getMessageType() { return messageType; }
    public void setMessageType(String messageType) { this.messageType = messageType; }

    public Boolean getSent() { return sent; }
    public void setSent(Boolean sent) { this.sent = sent; }

    public LocalDateTime getCreatedAt() { return createdAt; }
    public void setCreatedAt(LocalDateTime createdAt) { this.createdAt = createdAt; }

    public LocalDateTime getSentAt() { return sentAt; }
    public void setSentAt(LocalDateTime sentAt) { this.sentAt = sentAt; }

    /**
     * Factory method for creating ChatMessageRecord from ResultSet (for per-session databases).
     * Session ID is passed explicitly since it's implicit in the database filename.
     */
    public static ChatMessageRecord fromResultSetWithSessionId(java.sql.ResultSet rs, String sessionId) throws java.sql.SQLException {
        LocalDateTime createdAt = null;
        long createdAtSeconds = rs.getLong("created_at");
        if (!rs.wasNull()) {
            createdAt = LocalDateTime.ofInstant(
                Instant.ofEpochSecond(createdAtSeconds), ZoneId.systemDefault());
        }
        
        LocalDateTime sentAt = null;
        long sentAtSeconds = rs.getLong("sent_at");
        if (!rs.wasNull()) {
            sentAt = LocalDateTime.ofInstant(
                Instant.ofEpochSecond(sentAtSeconds), ZoneId.systemDefault());
        }
        
        return new ChatMessageRecord(
            rs.getLong("id"),
            sessionId,
            rs.getString("sender"),
            rs.getString("receiver"),
            rs.getString("content"),
            rs.getString("message_type"),
            rs.getBoolean("sent"),
            createdAt,
            sentAt
        );
    }

    /**
     * Factory method for creating ChatMessageRecord from ResultSet (for main database with chat_session join).
     * @deprecated Use fromResultSetWithSessionId for per-session databases
     */
    @Deprecated
    public static ChatMessageRecord fromResultSet(java.sql.ResultSet rs) throws java.sql.SQLException {
        LocalDateTime createdAt = null;
        long createdAtSeconds = rs.getLong("created_at");
        if (!rs.wasNull()) {
            createdAt = LocalDateTime.ofInstant(
                Instant.ofEpochSecond(createdAtSeconds), ZoneId.systemDefault());
        }
        
        LocalDateTime sentAt = null;
        long sentAtSeconds = rs.getLong("sent_at");
        if (!rs.wasNull()) {
            sentAt = LocalDateTime.ofInstant(
                Instant.ofEpochSecond(sentAtSeconds), ZoneId.systemDefault());
        }
        
        return new ChatMessageRecord(
            rs.getLong("id"),
            rs.getString("session_string_id"),
            rs.getString("sender"),
            rs.getString("receiver"),
            rs.getString("content"),
            rs.getString("message_type"),
            rs.getBoolean("sent"),
            createdAt,
            sentAt
        );
    }
}