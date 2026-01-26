package com.filesurf.model;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneId;

public class WaitlistEntry {
    private Long id;
    private String email;
    private String name;
    private String useCase;
    private LocalDateTime createdAt;
    private LocalDateTime updatedAt;

    public WaitlistEntry() {}

    public WaitlistEntry(Long id, String email, String name, String useCase, LocalDateTime createdAt, LocalDateTime updatedAt) {
        this.id = id;
        this.email = email;
        this.name = name;
        this.useCase = useCase;
        this.createdAt = createdAt;
        this.updatedAt = updatedAt;
    }

    public static WaitlistEntry fromResultSet(ResultSet rs) throws SQLException {
        long createdSeconds = rs.getLong("created_at");
        LocalDateTime createdAt = null;
        if (!rs.wasNull()) {
            createdAt = LocalDateTime.ofInstant(
                Instant.ofEpochSecond(createdSeconds), ZoneId.systemDefault());
        }
        
        long updatedSeconds = rs.getLong("updated_at");
        LocalDateTime updatedAt = null;
        if (!rs.wasNull()) {
            updatedAt = LocalDateTime.ofInstant(
                Instant.ofEpochSecond(updatedSeconds), ZoneId.systemDefault());
        }
        
        return new WaitlistEntry(
                rs.getLong("id"),
                rs.getString("email"),
                rs.getString("name"),
                rs.getString("use_case"),
                createdAt,
                updatedAt
        );
    }

    public Long getId() {
        return id;
    }

    public String getEmail() {
        return email;
    }

    public String getName() {
        return name;
    }

    public String getUseCase() {
        return useCase;
    }

    public LocalDateTime getCreatedAt() {
        return createdAt;
    }

    public LocalDateTime getUpdatedAt() {
        return updatedAt;
    }
}
