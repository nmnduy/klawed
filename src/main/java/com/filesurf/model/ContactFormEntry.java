package com.filesurf.model;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.LocalDateTime;

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
        return new ContactFormEntry(
                rs.getLong("id"),
                rs.getString("email"),
                rs.getString("company"),
                rs.getString("message"),
                rs.getTimestamp("created_at") != null ? rs.getTimestamp("created_at").toLocalDateTime() : null
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
