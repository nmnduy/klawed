package com.filesurf.model;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneId;

public class ContactFormEntry {
    private Long id;
    private String email;
    private String company;
    private String message;
    private LocalDateTime createdAt;

    public ContactFormEntry() {}

    public ContactFormEntry(Long id, String email, String company, String message, LocalDateTime createdAt) {
        this.id = id;
        this.email = email;
        this.company = company;
        this.message = message;
        this.createdAt = createdAt;
    }

    public static ContactFormEntry fromResultSet(ResultSet rs) throws SQLException {
        long seconds = rs.getLong("created_at");
        LocalDateTime createdAt = null;
        if (!rs.wasNull()) {
            createdAt = LocalDateTime.ofInstant(
                Instant.ofEpochSecond(seconds), ZoneId.systemDefault());
        }
        
        return new ContactFormEntry(
                rs.getLong("id"),
                rs.getString("email"),
                rs.getString("company"),
                rs.getString("message"),
                createdAt
        );
    }

    public Long getId() {
        return id;
    }

    public String getEmail() {
        return email;
    }

    public String getCompany() {
        return company;
    }

    public String getMessage() {
        return message;
    }

    public LocalDateTime getCreatedAt() {
        return createdAt;
    }
}
