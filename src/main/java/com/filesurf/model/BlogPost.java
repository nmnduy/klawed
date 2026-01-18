package com.filesurf.model;

import java.time.LocalDateTime;
import java.util.List;

public class BlogPost {
    private Integer id;
    private String title;
    private String slug;
    private String excerpt;
    private String content;
    private Integer authorId;
    private Integer categoryId;
    private String featuredImageUrl;
    private String metaTitle;
    private String metaDescription;
    private String metaKeywords;
    private String canonicalUrl;
    private String status; // 'draft', 'published', 'archived'
    private LocalDateTime publishedAt;
    private LocalDateTime createdAt;
    private LocalDateTime updatedAt;
    private Integer views;
    private Integer readingTimeMinutes;
    
    // Joined fields
    private BlogAuthor author;
    private BlogCategory category;
    private List<BlogTag> tags;
    
    // Constructors
    public BlogPost() {
        this.views = 0;
        this.status = "draft";
        this.createdAt = LocalDateTime.now();
        this.updatedAt = LocalDateTime.now();
    }
    
    public BlogPost(String title, String slug, String content, Integer authorId) {
        this();
        this.title = title;
        this.slug = slug;
        this.content = content;
        this.authorId = authorId;
    }
    
    // Getters and Setters
    public Integer getId() {
        return id;
    }
    
    public void setId(Integer id) {
        this.id = id;
    }
    
    public String getTitle() {
        return title;
    }
    
    public void setTitle(String title) {
        this.title = title;
    }
    
    public String getSlug() {
        return slug;
    }
    
    public void setSlug(String slug) {
        this.slug = slug;
    }
    
    public String getExcerpt() {
        return excerpt;
    }
    
    public void setExcerpt(String excerpt) {
        this.excerpt = excerpt;
    }
    
    public String getContent() {
        return content;
    }
    
    public void setContent(String content) {
        this.content = content;
    }
    
    public Integer getAuthorId() {
        return authorId;
    }
    
    public void setAuthorId(Integer authorId) {
        this.authorId = authorId;
    }
    
    public Integer getCategoryId() {
        return categoryId;
    }
    
    public void setCategoryId(Integer categoryId) {
        this.categoryId = categoryId;
    }
    
    public String getFeaturedImageUrl() {
        return featuredImageUrl;
    }
    
    public void setFeaturedImageUrl(String featuredImageUrl) {
        this.featuredImageUrl = featuredImageUrl;
    }
    
    public String getMetaTitle() {
        return metaTitle;
    }
    
    public void setMetaTitle(String metaTitle) {
        this.metaTitle = metaTitle;
    }
    
    public String getMetaDescription() {
        return metaDescription;
    }
    
    public void setMetaDescription(String metaDescription) {
        this.metaDescription = metaDescription;
    }
    
    public String getMetaKeywords() {
        return metaKeywords;
    }
    
    public void setMetaKeywords(String metaKeywords) {
        this.metaKeywords = metaKeywords;
    }
    
    public String getCanonicalUrl() {
        return canonicalUrl;
    }
    
    public void setCanonicalUrl(String canonicalUrl) {
        this.canonicalUrl = canonicalUrl;
    }
    
    public String getStatus() {
        return status;
    }
    
    public void setStatus(String status) {
        this.status = status;
    }
    
    public LocalDateTime getPublishedAt() {
        return publishedAt;
    }
    
    public void setPublishedAt(LocalDateTime publishedAt) {
        this.publishedAt = publishedAt;
    }
    
    public LocalDateTime getCreatedAt() {
        return createdAt;
    }
    
    public void setCreatedAt(LocalDateTime createdAt) {
        this.createdAt = createdAt;
    }
    
    public LocalDateTime getUpdatedAt() {
        return updatedAt;
    }
    
    public void setUpdatedAt(LocalDateTime updatedAt) {
        this.updatedAt = updatedAt;
    }
    
    public Integer getViews() {
        return views;
    }
    
    public void setViews(Integer views) {
        this.views = views;
    }
    
    public Integer getReadingTimeMinutes() {
        return readingTimeMinutes;
    }
    
    public void setReadingTimeMinutes(Integer readingTimeMinutes) {
        this.readingTimeMinutes = readingTimeMinutes;
    }
    
    public BlogAuthor getAuthor() {
        return author;
    }
    
    public void setAuthor(BlogAuthor author) {
        this.author = author;
    }
    
    public BlogCategory getCategory() {
        return category;
    }
    
    public void setCategory(BlogCategory category) {
        this.category = category;
    }
    
    public List<BlogTag> getTags() {
        return tags;
    }
    
    public void setTags(List<BlogTag> tags) {
        this.tags = tags;
    }
    
    // Helper methods
    public boolean isPublished() {
        return "published".equals(status) && publishedAt != null && publishedAt.isBefore(LocalDateTime.now());
    }
    
    public String getReadingTimeText() {
        if (readingTimeMinutes == null || readingTimeMinutes <= 0) {
            return "1 min read";
        }
        return readingTimeMinutes + " min read";
    }
}