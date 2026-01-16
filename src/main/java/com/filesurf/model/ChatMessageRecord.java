package com.filesurf.model;

import java.time.LocalDateTime;

public class ChatMessageRecord {
    private Long id;
    private Long sessionId;
    private String sessionStringId; // Added to store the session string ID
    private String sender;
    private String receiver;
    private String content;
    private String messageType;
    private Boolean sent;
    private LocalDateTime createdAt;
    private LocalDateTime sentAt;

    public ChatMessageRecord() {}

    public ChatMessageRecord(Long id, Long sessionId, String sessionStringId, String sender, String receiver,
                           String content, String messageType, Boolean sent,
                           LocalDateTime createdAt, LocalDateTime sentAt) {
        this.id = id;
        this.sessionId = sessionId;
        this.sessionStringId = sessionStringId;
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

    public Long getSessionId() { return sessionId; }
    public void setSessionId(Long sessionId) { this.sessionId = sessionId; }

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

    public String getSessionStringId() { return sessionStringId; }
    public void setSessionStringId(String sessionStringId) { this.sessionStringId = sessionStringId; }

    // Helper methods
    public static ChatMessageRecord fromResultSet(java.sql.ResultSet rs) throws java.sql.SQLException {
        return new ChatMessageRecord(
            rs.getLong("id"),
            rs.getLong("chat_session_id"),
            rs.getString("session_string_id"), // This will be added in queries
            rs.getString("sender"),
            rs.getString("receiver"),
            rs.getString("content"),
            rs.getString("message_type"),
            rs.getBoolean("sent"),
            rs.getTimestamp("created_at") != null ? rs.getTimestamp("created_at").toLocalDateTime() : null,
            rs.getTimestamp("sent_at") != null ? rs.getTimestamp("sent_at").toLocalDateTime() : null
        );
    }
}