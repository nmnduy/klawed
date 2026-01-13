package com.filesurf.model;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.LocalDateTime;

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
        return new WaitlistEntry(
                rs.getLong("id"),
                rs.getString("email"),
                rs.getString("name"),
                rs.getString("use_case"),
                rs.getTimestamp("created_at") != null ? rs.getTimestamp("created_at").toLocalDateTime() : null,
                rs.getTimestamp("updated_at") != null ? rs.getTimestamp("updated_at").toLocalDateTime() : null
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
