package com.filesurf.model;

import java.time.LocalDateTime;

public class BlogTag {
    private Integer id;
    private String name;
    private String slug;
    private LocalDateTime createdAt;

    // Constructors
    public BlogTag() {}

    public BlogTag(String name, String slug) {
        this.name = name;
        this.slug = slug;
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

    public LocalDateTime getCreatedAt() {
        return createdAt;
    }

    public void setCreatedAt(LocalDateTime createdAt) {
        this.createdAt = createdAt;
    }
}