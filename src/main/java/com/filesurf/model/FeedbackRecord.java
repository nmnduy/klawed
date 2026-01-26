package com.filesurf.model;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneId;

/**
 * Record representing user feedback in the system.
 * Links feedback to authenticated users.
 */
public class FeedbackRecord {
    private Long id;
    private String feedbackId;     // UUID for external reference
    private String type;           // "bug", "suggestion", "praise"
    private String description;
    private String userId;         // The userId from cookie
    private String userEmail;      // The user's email from database
    private String errorDetails;   // Optional error information
    private String environment;    // JSON string with browser/system info
    private LocalDateTime createdAt;

    public FeedbackRecord() {}

    public FeedbackRecord(Long id, String feedbackId, String type, String description,
                         String userId, String userEmail, String errorDetails,
                         String environment, LocalDateTime createdAt) {
        this.id = id;
        this.feedbackId = feedbackId;
        this.type = type;
        this.description = description;
        this.userId = userId;
        this.userEmail = userEmail;
        this.errorDetails = errorDetails;
        this.environment = environment;
        this.createdAt = createdAt;
    }

    public static FeedbackRecord fromResultSet(ResultSet rs) throws SQLException {
        FeedbackRecord record = new FeedbackRecord();
        record.setId(rs.getLong("id"));
        record.setFeedbackId(rs.getString("feedback_id"));
        record.setType(rs.getString("type"));
        record.setDescription(rs.getString("description"));
        record.setUserId(rs.getString("user_id"));
        record.setUserEmail(rs.getString("user_email"));
        record.setErrorDetails(rs.getString("error_details"));
        record.setEnvironment(rs.getString("environment"));

        long seconds = rs.getLong("created_at");
        if (!rs.wasNull()) {
            record.setCreatedAt(LocalDateTime.ofInstant(
                Instant.ofEpochSecond(seconds), ZoneId.systemDefault()));
        }

        return record;
    }

    // Getters and setters
    public Long getId() {
        return id;
    }

    public void setId(Long id) {
        this.id = id;
    }

    public String getFeedbackId() {
        return feedbackId;
    }

    public void setFeedbackId(String feedbackId) {
        this.feedbackId = feedbackId;
    }

    public String getType() {
        return type;
    }

    public void setType(String type) {
        this.type = type;
    }

    public String getDescription() {
        return description;
    }

    public void setDescription(String description) {
        this.description = description;
    }

    public String getUserId() {
        return userId;
    }

    public void setUserId(String userId) {
        this.userId = userId;
    }

    public String getUserEmail() {
        return userEmail;
    }

    public void setUserEmail(String userEmail) {
        this.userEmail = userEmail;
    }

    public String getErrorDetails() {
        return errorDetails;
    }

    public void setErrorDetails(String errorDetails) {
        this.errorDetails = errorDetails;
    }

    public String getEnvironment() {
        return environment;
    }

    public void setEnvironment(String environment) {
        this.environment = environment;
    }

    public LocalDateTime getCreatedAt() {
        return createdAt;
    }

    public void setCreatedAt(LocalDateTime createdAt) {
        this.createdAt = createdAt;
    }

    @Override
    public String toString() {
        return "FeedbackRecord{" +
                "id=" + id +
                ", feedbackId='" + feedbackId + '\'' +
                ", type='" + type + '\'' +
                ", description='" + description + '\'' +
                ", userId='" + userId + '\'' +
                ", userEmail='" + userEmail + '\'' +
                ", errorDetails='" + errorDetails + '\'' +
                ", createdAt=" + createdAt +
                '}';
    }
}
