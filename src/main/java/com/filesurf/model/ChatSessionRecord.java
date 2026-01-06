package com.filesurf.model;

import java.time.LocalDateTime;

public class ChatSessionRecord {
    private Long id;
    private String sessionId;
    private String clientIdentity;
    private LocalDateTime createdAt;
    private LocalDateTime lastActivityAt;
    private Boolean isActive;
    
    public ChatSessionRecord() {}
    
    public ChatSessionRecord(Long id, String sessionId, String clientIdentity, 
                           LocalDateTime createdAt, LocalDateTime lastActivityAt, Boolean isActive) {
        this.id = id;
        this.sessionId = sessionId;
        this.clientIdentity = clientIdentity;
        this.createdAt = createdAt;
        this.lastActivityAt = lastActivityAt;
        this.isActive = isActive;
    }
    
    // Getters and setters
    public Long getId() { return id; }
    public void setId(Long id) { this.id = id; }
    
    public String getSessionId() { return sessionId; }
    public void setSessionId(String sessionId) { this.sessionId = sessionId; }
    
    public String getClientIdentity() { return clientIdentity; }
    public void setClientIdentity(String clientIdentity) { this.clientIdentity = clientIdentity; }
    
    public LocalDateTime getCreatedAt() { return createdAt; }
    public void setCreatedAt(LocalDateTime createdAt) { this.createdAt = createdAt; }
    
    public LocalDateTime getLastActivityAt() { return lastActivityAt; }
    public void setLastActivityAt(LocalDateTime lastActivityAt) { this.lastActivityAt = lastActivityAt; }
    
    public Boolean getIsActive() { return isActive; }
    public void setIsActive(Boolean isActive) { this.isActive = isActive; }
    
    // Helper methods
    public static ChatSessionRecord fromResultSet(java.sql.ResultSet rs) throws java.sql.SQLException {
        return new ChatSessionRecord(
            rs.getLong("id"),
            rs.getString("session_id"),
            rs.getString("client_identity"),
            rs.getTimestamp("created_at") != null ? rs.getTimestamp("created_at").toLocalDateTime() : null,
            rs.getTimestamp("last_activity_at") != null ? rs.getTimestamp("last_activity_at").toLocalDateTime() : null,
            rs.getBoolean("is_active")
        );
    }
}