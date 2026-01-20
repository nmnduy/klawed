package com.filesurf.model;

import java.time.LocalDateTime;

public class BlogCategory {
    private Integer id;
    private String name;
    private String slug;
    private String description;
    private LocalDateTime createdAt;

    // Constructors
    public BlogCategory() {}

    public BlogCategory(String name, String slug, String description) {
        this.name = name;
        this.slug = slug;
        this.description = description;
        this.createdAt = LocalDateTime.now();
    }

    // Getters and Setters
    public Integer getId() {
        return id;
    }

    public void setId(Integer id) {
        this.id = id;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public String getSlug() {
        return slug;
    }

    public void setSlug(String slug) {
        this.slug = slug;
    }

    public String getDescription() {
        return description;
    }

    public void setDescription(String description) {
        this.description = description;
    }

    public LocalDateTime getCreatedAt() {
        return createdAt;
    }

    public void setCreatedAt(LocalDateTime createdAt) {
        this.createdAt = createdAt;
    }
}