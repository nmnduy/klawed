package com.filesurf.model;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.time.LocalDateTime;

/**
 * Record representing a user in the system.
 * Links email to userId for authentication.
 */
public class UserRecord {
    private Long id;
    private String userId;
    private String email;
    private LocalDateTime createdAt;
    private LocalDateTime lastLoginAt;
    private boolean isActive;

    public UserRecord() {}

    public UserRecord(Long id, String userId, String email, LocalDateTime createdAt, 
                      LocalDateTime lastLoginAt, boolean isActive) {
        this.id = id;
        this.userId = userId;
        this.email = email;
        this.createdAt = createdAt;
        this.lastLoginAt = lastLoginAt;
        this.isActive = isActive;
    }

    public static UserRecord fromResultSet(ResultSet rs) throws SQLException {
        UserRecord record = new UserRecord();
        record.setId(rs.getLong("id"));
        record.setUserId(rs.getString("user_id"));
        record.setEmail(rs.getString("email"));
        
        Timestamp createdTs = rs.getTimestamp("created_at");
        if (createdTs != null) {
            record.setCreatedAt(createdTs.toLocalDateTime());
        }
        
        Timestamp lastLoginTs = rs.getTimestamp("last_login_at");
        if (lastLoginTs != null) {
            record.setLastLoginAt(lastLoginTs.toLocalDateTime());
        }
        
        record.setActive(rs.getBoolean("is_active"));
        
        return record;
    }

    // Getters and setters
    public Long getId() {
        return id;
    }

    public void setId(Long id) {
        this.id = id;
    }

    public String getUserId() {
        return userId;
    }

    public void setUserId(String userId) {
        this.userId = userId;
    }

    public String getEmail() {
        return email;
    }

    public void setEmail(String email) {
        this.email = email;
    }

    public LocalDateTime getCreatedAt() {
        return createdAt;
    }

    public void setCreatedAt(LocalDateTime createdAt) {
        this.createdAt = createdAt;
    }

    public LocalDateTime getLastLoginAt() {
        return lastLoginAt;
    }

    public void setLastLoginAt(LocalDateTime lastLoginAt) {
        this.lastLoginAt = lastLoginAt;
    }

    public boolean isActive() {
        return isActive;
    }

    public void setActive(boolean active) {
        isActive = active;
    }

    @Override
    public String toString() {
        return "UserRecord{" +
                "id=" + id +
                ", userId='" + userId + '\'' +
                ", email='" + email + '\'' +
                ", createdAt=" + createdAt +
                ", lastLoginAt=" + lastLoginAt +
                ", isActive=" + isActive +
                '}';
    }
}
